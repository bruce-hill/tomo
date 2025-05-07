// This file defines type info and methods for the Text datatype, which uses
// libunistr for Unicode support and implements a datastructure based on a
// hybrid of Raku/MoarVM's space-efficient grapheme cluster representation of
// strings and Cords (Boehm et al), which have good runtime performance for
// text constructed by a series of many concatenations.
//
// For more information on MoarVM's grapheme cluster strings, see:
//     https://docs.raku.org/language/unicode
//     https://github.com/MoarVM/MoarVM/blob/main/docs/strings.asciidoc For more
// information on Cords, see the paper "Ropes: an Alternative to Strings"
// (Boehm, Atkinson, Plass 1995):
//     https://www.cs.tufts.edu/comp/150FP/archive/hans-boehm/ropes.pdf
//
// A note on grapheme clusters: In Unicode, codepoints can be represented using
// a 32-bit integer. Most codepoints correspond to the intuitive notion of a
// "letter", which is more formally known as a "grapheme cluster". A grapheme
// cluster is roughly speaking the amount of text that your cursor moves over
// when you press the arrow key once. However, some codepoints act as modifiers
// on other codepoints. For example, U+0301 (COMBINING ACUTE ACCENT) can modify
// a letter like "e" to form "é". During normalization, this frequently
// resolves down to a single unicode codepoint, in this case, "é" resolves to
// the single codepoint U+00E9 (LATIN SMALL LETTER E WITH ACUTE). However, in
// some cases, multiple codepoints make up a grapheme cluster but *don't*
// normalize to a single codepoint. For example, LATIN SMALL LETTER E (U+0065)
// + COMBINING VERTICAL LINE BELOW (U+0329) combine to form an unusual glyph
// that is not used frequently enough to warrant its own unique codepoint (this
// is basically what Zalgo text is).
//
// There are a lot of benefits to storing unicode text with one grapheme
// cluster per index in a densely packed list instead of storing the text as
// variable-width UTF8-encoded bytes. It lets us have one canonical length for
// the text that can be precomputed and is meaningful to users. It lets us
// quickly get the Nth "letter" in the text. Substring slicing is fast.
// However, since not all grapheme clusters take up the same number of
// codepoints, we're faced with the problem of how to jam multiple codepoints
// into a single 32-bit slot. Inspired by Raku and MoarVM's approach, this
// implementation uses "synthetic graphemes" (in Raku's terms, Normal Form
// Graphemes, aka NFG). A synthetic grapheme is a negative 32-bit signed
// integer that represents a multi-codepoint grapheme cluster that has been
// encountered during the program's runtime. These clusters are stored in a
// lookup list and hash map so that we can rapidly convert between the
// synthetic grapheme integer ID and the unicode codepoints associated with it.
// Essentially, it's like we create a supplement to the unicode standard with
// things that would be nice if they had their own codepoint so things worked
// out nicely because we're using them right now, and we'll give them a
// negative number so it doesn't overlap with any real codepoints.
//
// Example 1: U+0048, U+00E9 AKA: LATIN CAPITAL LETTER H, LATIN SMALL LETTER E
// WITH ACUTE This would be stored as: (int32_t[]){0x48, 0xE9} Example 2:
// U+0048, U+0065, U+0309 AKA: LATIN CAPITAL LETTER H, LATIN SMALL LETTER E,
// COMBINING VERTICAL LINE BELOW This would be stored as: (int32_t[]){0x48, -2}
// Where -2 is used as a lookup in a list that holds the actual unicode
// codepoints: (ucs4_t[]){0x65, 0x0309}

#include <assert.h>
#include <ctype.h>
#include <gc.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/param.h>

#include <unicase.h>
#include <unictype.h>
#include <unigbrk.h>
#include <uniname.h>
#include <unistr.h>
#include <unistring/version.h>
#include <uniwidth.h>

#include "datatypes.h"
#include "lists.h"
#include "integers.h"
#include "tables.h"
#include "text.h"

// Use inline version of the siphash code for performance:
#include "siphash.h"
#include "siphash-internals.h"

typedef struct {
    ucs4_t main_codepoint;
    ucs4_t *utf32_cluster; // length-prefixed
    const uint8_t *utf8;
} synthetic_grapheme_t;

// Synthetic grapheme clusters (clusters of more than one codepoint):
static Table_t grapheme_ids_by_codepoints = {}; // ucs4_t* length-prefixed codepoints -> int32_t ID

// This will hold a dynamically growing list of synthetic graphemes:
static synthetic_grapheme_t *synthetic_graphemes = NULL;
static int32_t synthetic_grapheme_capacity = 0;
static int32_t num_synthetic_graphemes = 0;

#define NUM_GRAPHEME_CODEPOINTS(id) (synthetic_graphemes[-(id)-1].utf32_cluster[0])
#define GRAPHEME_CODEPOINTS(id) (&synthetic_graphemes[-(id)-1].utf32_cluster[1])
#define GRAPHEME_UTF8(id) (synthetic_graphemes[-(id)-1].utf8)

// Somewhat arbitrarily chosen, if two short literal ASCII or grapheme chunks
// are concatenated below this length threshold, we just merge them into a
// single literal node instead of a concatenation node.
#define SHORT_ASCII_LENGTH 64
#define SHORT_GRAPHEMES_LENGTH 16

static Text_t text_from_u32(ucs4_t *codepoints, int64_t num_codepoints, bool normalize);
static Text_t simple_concatenation(Text_t a, Text_t b);

public Text_t EMPTY_TEXT = {
    .length=0,
    .tag=TEXT_ASCII,
    .ascii=0,
};

PUREFUNC static bool graphemes_equal(const void *va, const void *vb, const TypeInfo_t *info) {
    (void)info;
    ucs4_t *a = *(ucs4_t**)va;
    ucs4_t *b = *(ucs4_t**)vb;
    if (a[0] != b[0]) return false;
    for (int i = 0; i < (int)a[0]; i++)
        if (a[i] != b[i]) return false;
    return true;
}

PUREFUNC static uint64_t grapheme_hash(const void *g, const TypeInfo_t *info) {
    (void)info;
    ucs4_t *cluster = *(ucs4_t**)g;
    return siphash24((void*)&cluster[1], sizeof(ucs4_t[cluster[0]]));
}

static const TypeInfo_t GraphemeClusterInfo = {
    .size=sizeof(ucs4_t*),
    .align=__alignof__(ucs4_t*),
    .metamethods={
        .equal=graphemes_equal,
        .hash=grapheme_hash,
    },
};

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstack-protector"
#endif
public int32_t get_synthetic_grapheme(const ucs4_t *codepoints, int64_t utf32_len)
{
    ucs4_t length_prefixed[1+utf32_len];
    length_prefixed[0] = (ucs4_t)utf32_len;
    for (int i = 0; i < utf32_len; i++)
        length_prefixed[i+1] = codepoints[i];
    ucs4_t *ptr = &length_prefixed[0];

    // Optimization for common case of one frequently used synthetic grapheme:
    static int32_t last_grapheme = 0;
    if (last_grapheme != 0 && graphemes_equal(&ptr, &synthetic_graphemes[-last_grapheme-1].utf32_cluster, NULL))
        return last_grapheme;

    TypeInfo_t GraphemeIDLookupTableInfo = *Table$info(&GraphemeClusterInfo, &Int32$info);
    int32_t *found = Table$get(grapheme_ids_by_codepoints, &ptr, &GraphemeIDLookupTableInfo);
    if (found) return *found;

    // New synthetic grapheme:
    if (num_synthetic_graphemes >= synthetic_grapheme_capacity) {
        // If we don't have space, allocate more:
        synthetic_grapheme_capacity = MAX(128, synthetic_grapheme_capacity * 2);
        synthetic_grapheme_t *new = GC_MALLOC_ATOMIC(sizeof(synthetic_grapheme_t[synthetic_grapheme_capacity]));
        memcpy(new, synthetic_graphemes, sizeof(synthetic_grapheme_t[num_synthetic_graphemes]));
        synthetic_graphemes = new;
    }

    int32_t grapheme_id = -(num_synthetic_graphemes+1);
    num_synthetic_graphemes += 1;

    // Get UTF8 representation:
    uint8_t u8_buf[64];
    size_t u8_len = sizeof(u8_buf)/sizeof(u8_buf[0]);
    uint8_t *u8 = u32_to_u8(codepoints, (size_t)utf32_len, u8_buf, &u8_len);

    // For performance reasons, use an arena allocator here to ensure that
    // synthetic graphemes store all of their information in a densely packed
    // area with good cache locality:
    static void *arena = NULL, *arena_end = NULL;
    // Eat up any space needed to make arena 32-bit aligned:
    if ((size_t)arena % __alignof__(ucs4_t) != 0)
        arena += __alignof__(ucs4_t) - ((size_t)arena % __alignof__(ucs4_t));

    // If we have filled up this arena, allocate a new one:
    size_t needed_memory = sizeof(ucs4_t[1+utf32_len]) + sizeof(uint8_t[u8_len + 1]);
    if (arena + needed_memory > arena_end) {
        // Do reasonably big chunks at a time, so most synthetic codepoints are
        // nearby each other in memory and cache locality is good. This is a
        // rough guess at a good size:
        size_t chunk_size = MAX(needed_memory, 512);
        arena = GC_MALLOC_ATOMIC(chunk_size);
        arena_end = arena + chunk_size;
    }

    // Copy length-prefixed UTF32 codepoints into the arena and store where they live:
    ucs4_t *codepoint_copy = arena;
    memcpy(codepoint_copy, length_prefixed, sizeof(ucs4_t[1+utf32_len]));
    synthetic_graphemes[-grapheme_id-1].utf32_cluster = codepoint_copy;
    arena += sizeof(ucs4_t[1+utf32_len]);

    // Copy UTF8 bytes into the arena and store where they live:
    uint8_t *utf8_final = arena;
    memcpy(utf8_final, u8, sizeof(uint8_t[u8_len]));
    utf8_final[u8_len] = '\0'; // Add a terminating NUL byte
    synthetic_graphemes[-grapheme_id-1].utf8 = utf8_final;
    arena += sizeof(uint8_t[u8_len + 1]);

    // Sickos at the unicode consortium decreed that you can have grapheme clusters
    // that begin with *prefix* modifiers, so we gotta check for that case:
    synthetic_graphemes[-grapheme_id-1].main_codepoint = length_prefixed[1];
    for (ucs4_t i = 0; i < utf32_len; i++) {
#if _LIBUNISTRING_VERSION >= 0x010200
// libuinstring version 1.2.0 introduced uc_is_property_prepended_concatenation_mark()
// It's not critical, but it's technically more correct to have this check:
        if (unlikely(uc_is_property_prepended_concatenation_mark(length_prefixed[1+i])))
            continue;
#endif
        synthetic_graphemes[-grapheme_id-1].main_codepoint = length_prefixed[1+i];
        break;
    }

    // Cleanup from unicode API:
    if (u8 != u8_buf) free(u8);

    Table$set(&grapheme_ids_by_codepoints, &codepoint_copy, &grapheme_id, &GraphemeIDLookupTableInfo);

    last_grapheme = grapheme_id;
    return grapheme_id;
}
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

public int Text$print(FILE *stream, Text_t t)
{
    if (t.length == 0) return 0;

    switch (t.tag) {
    case TEXT_ASCII: return fwrite(t.ascii, sizeof(char), (size_t)t.length, stream);
    case TEXT_GRAPHEMES: {
        const int32_t *graphemes = t.graphemes;
        int written = 0;
        for (int64_t i = 0; i < t.length; i++) {
            int32_t grapheme = graphemes[i];
            if (grapheme >= 0) {
                uint8_t buf[8];
                size_t len = sizeof(buf);
                uint8_t *u8 = u32_to_u8((ucs4_t*)&grapheme, 1, buf, &len);
                written += (int)fwrite(u8, sizeof(char), len, stream);
                if (u8 != buf) free(u8);
            } else {
                const uint8_t *u8 = GRAPHEME_UTF8(grapheme);
                assert(u8);
                written += (int)fwrite(u8, sizeof(uint8_t), strlen((char*)u8), stream);
            }
        }
        return written;
    }
    case TEXT_CONCAT: {
        return (Text$print(stream, *t.left)
                + Text$print(stream, *t.right));
    }
    default: return 0;
    }
}

static const int64_t min_len_for_depth[MAX_TEXT_DEPTH] = {
    // Fibonacci numbers (skipping first two)
    1, 2, 3, 5, 8, 13, 21, 34, 55, 89, 144, 233, 377, 610, 987, 1597, 2584, 4181, 6765, 10946,
    17711, 28657, 46368, 75025, 121393, 196418, 317811, 514229, 832040, 1346269, 2178309, 3524578,
    5702887, 9227465, 14930352, 24157817, 39088169, 63245986, 102334155, 165580141, 267914296,
    433494437, 701408733, 1134903170, 1836311903, 2971215073, 4807526976, 7778742049,
};

#define IS_BALANCED_TEXT(t) ((t).length >= min_len_for_depth[(t).depth])

static void insert_balanced(Text_t balanced_texts[MAX_TEXT_DEPTH], Text_t to_insert)
{
    int i = 0;
    Text_t accumulator = EMPTY_TEXT;
    for (; to_insert.length > min_len_for_depth[i + 1]; i++) {
        if (balanced_texts[i].length) {
            accumulator = simple_concatenation(balanced_texts[i], accumulator);
            balanced_texts[i] = EMPTY_TEXT;
        }
    }

    accumulator = simple_concatenation(accumulator, to_insert);

    while (accumulator.length >= min_len_for_depth[i]) {
        if (balanced_texts[i].length) {
            accumulator = simple_concatenation(balanced_texts[i], accumulator);
            balanced_texts[i] = EMPTY_TEXT;
        }
        i++;
    }
    i--;
    balanced_texts[i] = accumulator;
}

static void insert_balanced_recursive(Text_t balanced_texts[MAX_TEXT_DEPTH], Text_t text)
{
    if (text.tag == TEXT_CONCAT && (!IS_BALANCED_TEXT(text) || text.depth >= MAX_TEXT_DEPTH)) {
        insert_balanced_recursive(balanced_texts, *text.left);
        insert_balanced_recursive(balanced_texts, *text.right);
    } else {
        insert_balanced(balanced_texts, text);
    }
}

static Text_t rebalanced(Text_t a, Text_t b)
{
    Text_t balanced_texts[MAX_TEXT_DEPTH];
    memset(balanced_texts, 0, sizeof(balanced_texts));
    insert_balanced_recursive(balanced_texts, a);
    insert_balanced_recursive(balanced_texts, b);

    Text_t ret = EMPTY_TEXT;
    for (int i = 0; ret.length < a.length + b.length; i++) {
        if (balanced_texts[i].length)
            ret = simple_concatenation(balanced_texts[i], ret);
    }
    return ret;
}

Text_t simple_concatenation(Text_t a, Text_t b)
{
    if (a.length == 0) return b;
    if (b.length == 0) return a;

    uint16_t new_depth = 1 + MAX(a.depth, b.depth);
    // Rebalance only if depth exceeds the maximum allowed. We don't require
    // every concatenation to yield a balanced text, since many concatenations
    // are ephemeral (e.g. doing a loop repeatedly concatenating without using
    // the intermediary values).
    if (new_depth >= MAX_TEXT_DEPTH)
        return rebalanced(a, b);

    Text_t *children = GC_MALLOC(sizeof(Text_t[2]));
    children[0] = a;
    children[1] = b;
    return (Text_t){
        .tag=TEXT_CONCAT,
        .length=a.length + b.length,
        .depth=new_depth,
        .left=&children[0],
        .right=&children[1],
    };
}

static Text_t concat2_assuming_safe(Text_t a, Text_t b)
{
    if (a.length == 0) return b;
    if (b.length == 0) return a;

    if (a.tag == TEXT_ASCII && b.tag == TEXT_ASCII && (size_t)(a.length + b.length) <= SHORT_ASCII_LENGTH) {
        struct Text_s ret = {
            .tag=TEXT_ASCII,
            .length=a.length + b.length,
        };
        ret.ascii = GC_MALLOC_ATOMIC(sizeof(char[ret.length]));
        memcpy((char*)ret.ascii, a.ascii, sizeof(char[a.length]));
        memcpy((char*)&ret.ascii[a.length], b.ascii, sizeof(char[b.length]));
        return ret;
    } else if (a.tag == TEXT_GRAPHEMES && b.tag == TEXT_GRAPHEMES && (size_t)(a.length + b.length) <= SHORT_GRAPHEMES_LENGTH) {
        struct Text_s ret = {
            .tag=TEXT_GRAPHEMES,
            .length=a.length + b.length,
        };
        ret.graphemes = GC_MALLOC_ATOMIC(sizeof(int32_t[ret.length]));
        memcpy((int32_t*)ret.graphemes, a.graphemes, sizeof(int32_t[a.length]));
        memcpy((int32_t*)&ret.graphemes[a.length], b.graphemes, sizeof(int32_t[b.length]));
        return ret;
    } else if (a.tag != TEXT_CONCAT && b.tag != TEXT_CONCAT && (size_t)(a.length + b.length) <= SHORT_GRAPHEMES_LENGTH) {
        // Turn a small bit of ASCII into graphemes if it helps make things smaller
        // Text structs come with an extra 8 bytes, so allocate enough to hold the text
        struct Text_s ret = {
            .tag=TEXT_GRAPHEMES,
            .length=a.length + b.length,
        };
        ret.graphemes = GC_MALLOC_ATOMIC(sizeof(int32_t[ret.length]));
        int32_t *dest = (int32_t*)ret.graphemes;
        if (a.tag == TEXT_GRAPHEMES) {
            memcpy(dest, a.graphemes, sizeof(int32_t[a.length]));
            dest += a.length;
        } else {
            for (int64_t i = 0; i < a.length; i++)
                *(dest++) = (int32_t)a.ascii[i];
        }
        if (b.tag == TEXT_GRAPHEMES) {
            memcpy(dest, b.graphemes, sizeof(int32_t[b.length]));
        } else {
            for (int64_t i = 0; i < b.length; i++)
                *(dest++) = (int32_t)b.ascii[i];
        }
        return ret;
    }

    if (a.tag == TEXT_CONCAT && b.tag != TEXT_CONCAT && a.right->tag != TEXT_CONCAT)
        return concat2_assuming_safe(*a.left, concat2_assuming_safe(*a.right, b));

    return simple_concatenation(a, b);
}

static Text_t concat2(Text_t a, Text_t b)
{
    if (a.length == 0) return b;
    if (b.length == 0) return a;

    int32_t last_a = Text$get_grapheme(a, a.length-1);
    int32_t first_b = Text$get_grapheme(b, 0);

    // Magic number, we know that no codepoints below here trigger instability:
    static const int32_t LOWEST_CODEPOINT_TO_CHECK = 0x300; // COMBINING GRAVE ACCENT
    if (last_a >= 0 && last_a < LOWEST_CODEPOINT_TO_CHECK && first_b >= 0 && first_b < LOWEST_CODEPOINT_TO_CHECK)
        return concat2_assuming_safe(a, b);

    size_t len = (last_a >= 0) ? 1 : NUM_GRAPHEME_CODEPOINTS(last_a);
    len += (first_b >= 0) ? 1 : NUM_GRAPHEME_CODEPOINTS(first_b);

    ucs4_t codepoints[len];
    ucs4_t *dest = codepoints;
    if (last_a < 0) {
        memcpy(dest, GRAPHEME_CODEPOINTS(last_a), sizeof(ucs4_t[NUM_GRAPHEME_CODEPOINTS(last_a)]));
        dest += NUM_GRAPHEME_CODEPOINTS(last_a);
    } else {
        *(dest++) = (ucs4_t)last_a;
    }

    if (first_b < 0) {
        memcpy(dest, GRAPHEME_CODEPOINTS(first_b), sizeof(ucs4_t[NUM_GRAPHEME_CODEPOINTS(first_b)]));
        dest += NUM_GRAPHEME_CODEPOINTS(first_b);
    } else {
        *(dest++) = (ucs4_t)first_b;
    }

    // Do a normalization run for these two codepoints and see if it looks different.
    // Normalization should not exceed 3x in the input length (but if it does, it will be
    // handled gracefully)
    ucs4_t norm_buf[3*len];
    size_t norm_length = sizeof(norm_buf)/sizeof(norm_buf[0]);
    ucs4_t *normalized = u32_normalize(UNINORM_NFC, codepoints, len, norm_buf, &norm_length);
    bool stable = (norm_length == len && memcmp(codepoints, normalized, sizeof(codepoints)) == 0);

    if (stable) {
        const void *second_grapheme = u32_grapheme_next(normalized, &normalized[norm_length]);
        if (second_grapheme == &normalized[norm_length])
            stable = false;
    }

    if likely (stable) {
        if (normalized != norm_buf)
            free(normalized);
        return concat2_assuming_safe(a, b);
    }

    Text_t glue = text_from_u32(norm_buf, (int64_t)norm_length, false);

    if (normalized != norm_buf)
        free(normalized);

    if (a.length == 1 && b.length == 1)
        return glue;
    else if (a.length == 1)
        return concat2_assuming_safe(glue, Text$slice(b, I(2), I(b.length)));
    else if (b.length == 1)
        return concat2_assuming_safe(Text$slice(a, I(1), I(a.length-1)), glue);
    else
        return concat2_assuming_safe(
            concat2_assuming_safe(Text$slice(a, I(1), I(a.length-1)), glue),
            Text$slice(b, I(2), I(b.length)));
}

public Text_t Text$_concat(int n, Text_t items[n])
{
    if (n == 0) return EMPTY_TEXT;

    Text_t ret = items[0];
    for (int i = 1; i < n; i++) {
        if (items[i].length > 0)
            ret = concat2(ret, items[i]);
    }
    return ret;
}

public Text_t Text$repeat(Text_t text, Int_t count)
{
    if (text.length == 0 || Int$is_negative(count))
        return EMPTY_TEXT;

    Int_t result_len = Int$times(count, I(text.length));
    if (Int$compare_value(result_len, I(1l<<40)) > 0)
        fail("Text repeating would produce too big of an result!");

    int64_t count64 = Int64$from_int(count, false);
    Text_t ret = text;
    for (int64_t c = 1; c < count64; c++)
        ret = concat2(ret, text);
    return ret;
}

public Int_t Text$width(Text_t text, Text_t language)
{
    int width = u8_strwidth((const uint8_t*)Text$as_c_string(text), Text$as_c_string(language));
    return Int$from_int32(width);
}

static Text_t Text$repeat_to_width(Text_t to_repeat, int64_t target_width, Text_t language)
{
    if (target_width <= 0)
        return EMPTY_TEXT;

    const char *lang_str = Text$as_c_string(language);
    int64_t width = (int64_t)u8_strwidth((const uint8_t*)Text$as_c_string(to_repeat), lang_str);
    Text_t repeated = EMPTY_TEXT;
    int64_t repeated_width = 0;
    while (repeated_width + width <= target_width) {
        repeated = concat2(repeated, to_repeat);
        repeated_width += width;
    }

    if (repeated_width < target_width) {
        for (int64_t i = 0; repeated_width < target_width && i < to_repeat.length; i++) {
            Text_t c = Text$slice(to_repeat, I_small(i+1), I_small(i+1));
            int64_t w = (int64_t)u8_strwidth((const uint8_t*)Text$as_c_string(c), lang_str);
            if (repeated_width + w > target_width) {
                repeated = concat2(repeated, Text$repeat(Text(" "), I(target_width - repeated_width)));
                repeated_width = target_width;
                break;
            }
            repeated = concat2(repeated, c);
            repeated_width += w;
        }
    }

    return repeated;
}

public Text_t Text$left_pad(Text_t text, Int_t width, Text_t padding, Text_t language)
{
    if (padding.length == 0)
        fail("Cannot pad with an empty text!");

    int64_t needed = Int64$from_int(width, false) - Int64$from_int(Text$width(text, language), false);
    return concat2(Text$repeat_to_width(padding, needed, language), text);
}

public Text_t Text$right_pad(Text_t text, Int_t width, Text_t padding, Text_t language)
{
    if (padding.length == 0)
        fail("Cannot pad with an empty text!");

    int64_t needed = Int64$from_int(width, false) - Int64$from_int(Text$width(text, language), false);
    return concat2(text, Text$repeat_to_width(padding, needed, language));
}

public Text_t Text$middle_pad(Text_t text, Int_t width, Text_t padding, Text_t language)
{
    if (padding.length == 0)
        fail("Cannot pad with an empty text!");

    int64_t needed = Int64$from_int(width, false) - Int64$from_int(Text$width(text, language), false);
    return Texts(Text$repeat_to_width(padding, needed/2, language), text, Text$repeat_to_width(padding, (needed+1)/2, language));
}

public Text_t Text$slice(Text_t text, Int_t first_int, Int_t last_int)
{
    int64_t first = Int64$from_int(first_int, false);
    int64_t last = Int64$from_int(last_int, false);
    if (first == 0) fail("Invalid index: 0");
    if (last == 0) return EMPTY_TEXT;

    if (first < 0) first = text.length + first + 1;
    if (last < 0) last = text.length + last + 1;

    if (last > text.length) last = text.length;

    if (first > text.length || last < first)
        return EMPTY_TEXT;

    if (first == 1 && last == text.length)
        return text;

    while (text.tag == TEXT_CONCAT) {
        if (last < text.left->length) {
            text = *text.left;
        } else if (first > text.left->length) {
            first -= text.left->length;
            last -= text.left->length;
            text = *text.right;
        } else {
            return concat2(Text$slice(*text.left, I(first), I(text.length)),
                           Text$slice(*text.right, I(1), I(last-text.left->length)));
        }
    }

    switch (text.tag) {
    case TEXT_ASCII: {
        return (Text_t){
            .tag=TEXT_ASCII,
            .length=last - first + 1,
            .ascii=text.ascii + (first-1),
        };
    }
    case TEXT_GRAPHEMES: {
        return (Text_t){
            .tag=TEXT_GRAPHEMES,
            .length=last - first + 1,
            .graphemes=text.graphemes + (first-1),
        };
    }
    default: errx(1, "Invalid tag");
    }
    return EMPTY_TEXT;
}

public Text_t Text$from(Text_t text, Int_t first)
{
    return Text$slice(text, first, I_small(-1));
}

public Text_t Text$to(Text_t text, Int_t last)
{
    return Text$slice(text, I_small(1), last);
}

public Text_t Text$reversed(Text_t text)
{
    switch (text.tag) {
    case TEXT_ASCII: {
        struct Text_s ret = {
            .tag=TEXT_ASCII,
            .length=text.length,
        };
        ret.ascii = GC_MALLOC_ATOMIC(sizeof(char[ret.length]));
        for (int64_t i = 0; i < text.length; i++)
            ((char*)ret.ascii)[text.length-1-i] = text.ascii[i];
        return ret;
    }
    case TEXT_GRAPHEMES: {
        struct Text_s ret = {
            .tag=TEXT_GRAPHEMES,
            .length=text.length,
        };
        ret.graphemes = GC_MALLOC_ATOMIC(sizeof(int32_t[ret.length]));
        for (int64_t i = 0; i < text.length; i++)
            ((int32_t*)ret.graphemes)[text.length-1-i] = text.graphemes[i];
        return ret;
    }
    case TEXT_CONCAT: {
        return concat2(Text$reversed(*text.right), Text$reversed(*text.left));
    }
    default: errx(1, "Invalid tag");
    }
    return EMPTY_TEXT;
}

public PUREFUNC Text_t Text$cluster(Text_t text, Int_t index_int)
{
    int64_t index = Int64$from_int(index_int, false);
    if (index == 0) fail("Invalid index: 0");

    if (index < 0) index = text.length + index + 1;

    if (index > text.length || index < 1)
        fail("Invalid index: ", index_int, " is beyond the length of the text (length = ", (int64_t)text.length, ")");

    while (text.tag == TEXT_CONCAT) {
        if (index <= text.left->length)
            text = *text.left;
        else
            text = *text.right;
    }

    switch (text.tag) {
    case TEXT_ASCII: {
        struct Text_s ret = {
            .tag=TEXT_ASCII,
            .length=1,
            .ascii=GC_MALLOC_ATOMIC(sizeof(char)),
        };
        *(char*)&ret.ascii[0] = text.ascii[index-1];
        return ret;
    }
    case TEXT_GRAPHEMES: {
        struct Text_s ret = {
            .tag=TEXT_GRAPHEMES,
            .length=1,
            .graphemes=GC_MALLOC_ATOMIC(sizeof(int32_t)),
        };
        *(int32_t*)&ret.graphemes[0] = text.graphemes[index-1];
        return ret;
    }
    default: errx(1, "Invalid tag");
    }
    return EMPTY_TEXT;
}

Text_t text_from_u32(ucs4_t *codepoints, int64_t num_codepoints, bool normalize)
{
    // Normalization is apparently guaranteed to never exceed 3x in the input length
    ucs4_t norm_buf[MIN(256, 3*num_codepoints)];
    if (normalize) {
        size_t norm_length = sizeof(norm_buf)/sizeof(norm_buf[0]);
        ucs4_t *normalized = u32_normalize(UNINORM_NFC, codepoints, (size_t)num_codepoints, norm_buf, &norm_length);
        codepoints = normalized;
        num_codepoints = (int64_t)norm_length;
    }

    // Intentionally overallocate here: allocate assuming each codepoint is a
    // grapheme cluster. If that's not true, we'll have extra space at the end
    // of the list, but the length will still be calculated correctly.
    int32_t *graphemes = GC_MALLOC_ATOMIC(sizeof(int32_t[num_codepoints]));
    struct Text_s ret = {
        .tag=TEXT_GRAPHEMES,
        .length=0,
        .graphemes=graphemes,
    };
    const ucs4_t *src = codepoints;
    while (src < &codepoints[num_codepoints]) {
        // TODO: use grapheme breaks instead of u32_grapheme_next()?
        const ucs4_t *next = u32_grapheme_next(src, &codepoints[num_codepoints]);
        if (next == &src[1]) {
            graphemes[ret.length] = (int32_t)*src;
        } else {
            // Synthetic grapheme
            graphemes[ret.length] = get_synthetic_grapheme(src, next-src);
        }
        ++ret.length;
        src = next;
    }
    if (normalize && codepoints != norm_buf) free(codepoints);
    return ret;
}

public OptionalText_t Text$from_strn(const char *str, size_t len)
{
    int64_t ascii_span = 0;
    for (size_t i = 0; i < len && isascii(str[i]); i++)
        ascii_span++;

    if (ascii_span == (int64_t)len) { // All ASCII
        char *copy = GC_MALLOC_ATOMIC(len);
        memcpy(copy, str, len);
        return (Text_t){
            .tag=TEXT_ASCII,
            .length=ascii_span,
            .ascii=copy,
        };
    } else {
        if (u8_check((uint8_t*)str, len) != NULL)
            return NONE_TEXT;

        ucs4_t buf[128];
        size_t length = sizeof(buf)/sizeof(buf[0]);

        ucs4_t *codepoints = u8_to_u32((uint8_t*)str, (size_t)ascii_span + strlen(str + ascii_span), buf, &length);
        Text_t ret = text_from_u32(codepoints, (int64_t)length, true);
        if (codepoints != buf) free(codepoints);
        return ret;
    }
}

public OptionalText_t Text$from_str(const char *str)
{
    return str ? Text$from_strn(str, strlen(str)) : Text("");
}

static void u8_buf_append(Text_t text, char **buf, int64_t *capacity, int64_t *i)
{
    switch (text.tag) {
    case TEXT_ASCII: {
        if (*i + text.length > (int64_t)*capacity) {
            *capacity = *i + text.length + 1;
            *buf = GC_REALLOC(*buf, (size_t)*capacity);
        }

        const char *bytes = text.ascii;
        memcpy(*buf + *i, bytes, (size_t)text.length);
        *i += text.length;
        break;
    }
    case TEXT_GRAPHEMES: {
        const int32_t *graphemes = text.graphemes;
        for (int64_t g = 0; g < text.length; g++) {
            if (graphemes[g] >= 0) {
                uint8_t u8_buf[64];
                size_t u8_len = sizeof(u8_buf);
                uint8_t *u8 = u32_to_u8((ucs4_t*)&graphemes[g], 1, u8_buf, &u8_len);

                if (*i + (int64_t)u8_len > (int64_t)*capacity) {
                    *capacity = *i + (int64_t)u8_len + 1;
                    *buf = GC_REALLOC(*buf, (size_t)*capacity);
                }

                memcpy(*buf + *i, u8, u8_len);
                *i += (int64_t)u8_len;
                if (u8 != u8_buf) free(u8);
            } else {
                const uint8_t *u8 = GRAPHEME_UTF8(graphemes[g]);
                size_t u8_len = u8_strlen(u8);
                if (*i + (int64_t)u8_len > (int64_t)*capacity) {
                    *capacity = *i + (int64_t)u8_len + 1;
                    *buf = GC_REALLOC(*buf, (size_t)*capacity);
                }

                memcpy(*buf + *i, u8, u8_len);
                *i += (int64_t)u8_len;
            }
        }
        break;
    }
    case TEXT_CONCAT: {
        u8_buf_append(*text.left, buf, capacity, i);
        u8_buf_append(*text.right, buf, capacity, i);
        break;
    }
    default: break;
    }
}

public char *Text$as_c_string(Text_t text)
{
    int64_t capacity = text.length + 1;
    char *buf = GC_MALLOC_ATOMIC((size_t)capacity);
    int64_t i = 0;
    u8_buf_append(text, &buf, &capacity, &i);

    if (i + 1 > (int64_t)capacity) {
        capacity = i + 1;
        buf = GC_REALLOC(buf, (size_t)capacity);
    }
    buf[i] = '\0';
    return buf;
}

PUREFUNC public uint64_t Text$hash(const void *obj, const TypeInfo_t *info)
{
    (void)info;
    Text_t text = *(Text_t*)obj;
    siphash sh;
    siphashinit(&sh, sizeof(int32_t[text.length]));

    union {
        int32_t chunks[2];
        uint64_t whole;
    } tmp;
    switch (text.tag) {
    case TEXT_ASCII: {
        const char *bytes = text.ascii;
        for (int64_t i = 0; i + 1 < text.length; i += 2) {
            tmp.chunks[0] = (int32_t)bytes[i];
            tmp.chunks[1] = (int32_t)bytes[i+1];
            siphashadd64bits(&sh, tmp.whole);
        }
        int32_t last = text.length & 0x1 ? (int32_t)bytes[text.length-1] : 0; // Odd number of graphemes
        return siphashfinish_last_part(&sh, (uint64_t)last);
    }
    case TEXT_GRAPHEMES: {
        const int32_t *graphemes = text.graphemes;
        for (int64_t i = 0; i + 1 < text.length; i += 2) {
            tmp.chunks[0] = graphemes[i];
            tmp.chunks[1] = graphemes[i+1];
            siphashadd64bits(&sh, tmp.whole);
        }
        int32_t last = text.length & 0x1 ? graphemes[text.length-1] : 0; // Odd number of graphemes
        return siphashfinish_last_part(&sh, (uint64_t)last);
    }
    case TEXT_CONCAT: {
        TextIter_t state = NEW_TEXT_ITER_STATE(text);
        for (int64_t i = 0; i + 1 < text.length; i += 2) {
            tmp.chunks[0] = Text$get_grapheme_fast(&state, i);
            tmp.chunks[1] = Text$get_grapheme_fast(&state, i+1);
            siphashadd64bits(&sh, tmp.whole);
        }

        int32_t last = (text.length & 0x1) ? Text$get_grapheme_fast(&state, text.length-1) : 0;
        return siphashfinish_last_part(&sh, (uint64_t)last);
    }
    default: errx(1, "Invalid text");
    }
    return 0;
}

public int32_t Text$get_grapheme_fast(TextIter_t *state, int64_t index)
{
    if (index < 0) return 0;
    if (index >= state->stack[0].text.length) return 0;

    assert(state->stack[0].text.depth <= MAX_TEXT_DEPTH);

    // Go up the stack as needed:
    while (index < state->stack[state->stack_index].offset
           || index >= state->stack[state->stack_index].offset + state->stack[state->stack_index].text.length) {
        state->stack_index -= 1;
        assert(state->stack_index >= 0);
    }

    assert(state->stack_index >= 0 && state->stack_index <= MAX_TEXT_DEPTH);

    // Go down the stack as needed:
    while (state->stack[state->stack_index].text.tag == TEXT_CONCAT) {
        Text_t text = state->stack[state->stack_index].text;
        int64_t offset = state->stack[state->stack_index].offset;
        assert(state->stack_index <= MAX_TEXT_DEPTH);
        assert(index >= offset);
        assert(index < offset + text.length);

        state->stack_index += 1;
        if (index < offset + text.left->length) {
            state->stack[state->stack_index].text = *text.left;
            state->stack[state->stack_index].offset = offset;
        } else {
            state->stack[state->stack_index].text = *text.right;
            state->stack[state->stack_index].offset = offset + text.left->length;
        }
        assert(state->stack_index >= 0 && state->stack_index <= MAX_TEXT_DEPTH);
    }

    Text_t text = state->stack[state->stack_index].text;
    int64_t offset = state->stack[state->stack_index].offset;

    if (index < offset || index >= offset + text.length) {
        return 0;
    }

    switch (text.tag) {
    case TEXT_ASCII: return (int32_t)text.ascii[index - offset];
    case TEXT_GRAPHEMES: return text.graphemes[index - offset];
    default: errx(1, "Invalid text");
    }
    return 0;
}

public uint32_t Text$get_main_grapheme_fast(TextIter_t *state, int64_t index)
{
    int32_t g = Text$get_grapheme_fast(state, index);
    return (g) >= 0 ? (ucs4_t)(g) : synthetic_graphemes[-(g)-1].main_codepoint;
}

PUREFUNC public int32_t Text$compare(const void *va, const void *vb, const TypeInfo_t *info)
{
    (void)info;
    if (va == vb) return 0;
    const Text_t a = *(const Text_t*)va;
    const Text_t b = *(const Text_t*)vb;

    // TODO: make this smarter and more efficient
    int64_t len = MAX(a.length, b.length);
    TextIter_t a_state = NEW_TEXT_ITER_STATE(a), b_state = NEW_TEXT_ITER_STATE(b);
    for (int64_t i = 0; i < len; i++) {
        int32_t ai = Text$get_grapheme_fast(&a_state, i);
        int32_t bi = Text$get_grapheme_fast(&b_state, i);
        if (ai == bi) continue;
        int32_t cmp;
        if (ai > 0 && bi > 0) {
            cmp = u32_cmp((ucs4_t*)&ai, (ucs4_t*)&bi, 1);
        } else if (ai > 0) {
            cmp = u32_cmp2(
                (ucs4_t*)&ai, 1,
                GRAPHEME_CODEPOINTS(bi),
                NUM_GRAPHEME_CODEPOINTS(bi));
        } else if (bi > 0) {
            cmp = u32_cmp2(
                GRAPHEME_CODEPOINTS(ai),
                NUM_GRAPHEME_CODEPOINTS(ai),
                (ucs4_t*)&bi, 1);
        } else {
            cmp = u32_cmp2(
                GRAPHEME_CODEPOINTS(ai),
                NUM_GRAPHEME_CODEPOINTS(ai),
                GRAPHEME_CODEPOINTS(bi),
                NUM_GRAPHEME_CODEPOINTS(bi));
        }
        if (cmp != 0) return cmp;
    }
    return 0;
}

bool _matches(TextIter_t *text_state, TextIter_t *target_state, int64_t pos)
{
    for (int64_t i = 0; i < target_state->stack[0].text.length; i++) {
        int32_t text_i = Text$get_grapheme_fast(text_state, pos + i);
        int32_t prefix_i = Text$get_grapheme_fast(target_state, i);
        if (text_i != prefix_i) return false;
    }
    return true;
}

PUREFUNC public bool Text$starts_with(Text_t text, Text_t prefix)
{
    if (text.length < prefix.length)
        return false;
    TextIter_t text_state = NEW_TEXT_ITER_STATE(text), prefix_state = NEW_TEXT_ITER_STATE(prefix);
    return _matches(&text_state, &prefix_state, 0);
}

PUREFUNC public bool Text$ends_with(Text_t text, Text_t suffix)
{
    if (text.length < suffix.length)
        return false;
    TextIter_t text_state = NEW_TEXT_ITER_STATE(text), suffix_state = NEW_TEXT_ITER_STATE(suffix);
    return _matches(&text_state, &suffix_state, text.length - suffix.length);
}

public Text_t Text$without_prefix(Text_t text, Text_t prefix)
{
    return Text$starts_with(text, prefix) ? Text$slice(text, I(prefix.length + 1), I(text.length)) : text;
}

public Text_t Text$without_suffix(Text_t text, Text_t suffix)
{
    return Text$ends_with(text, suffix) ? Text$slice(text, I(1), I(text.length - suffix.length)) : text;
}

static bool _has_grapheme(TextIter_t *text, int32_t g)
{
    for (int64_t t = 0; t < text->stack[0].text.length; t++) {
        if (g == Text$get_grapheme_fast(text, t)) {
            return true;
        }
    }
    return false;
}

public Text_t Text$trim(Text_t text, Text_t to_trim, bool left, bool right)
{
    int64_t first = 0;
    TextIter_t text_state = NEW_TEXT_ITER_STATE(text), trim_state = NEW_TEXT_ITER_STATE(to_trim);
    if (left) {
        while (first < text.length && _has_grapheme(&trim_state, Text$get_grapheme_fast(&text_state, first))) {
            first += 1;
        }
    }
    int64_t last = text.length-1;
    if (right) {
        while (last >= first && _has_grapheme(&trim_state, Text$get_grapheme_fast(&text_state, last))) {
            last -= 1;
        }
    }
    return (first != 0 || last != text.length-1) ? Text$slice(text, I(first+1), I(last+1)) : text;
}

public Text_t Text$translate(Text_t text, Table_t translations)
{
    TextIter_t text_state = NEW_TEXT_ITER_STATE(text);
    Text_t result = EMPTY_TEXT;
    int64_t span_start = 0;
    List_t replacement_list = translations.entries;
    for (int64_t i = 0; i < text.length; ) {
        for (int64_t r = 0; r < replacement_list.length; r++) {
            struct { Text_t target, replacement; } *entry = replacement_list.data + r*replacement_list.stride;
            TextIter_t target_state = NEW_TEXT_ITER_STATE(entry->target);
            if (_matches(&text_state, &target_state, i)) {
                if (i > span_start)
                    result = concat2(result, Text$slice(text, I(span_start+1), I(i)));

                result = concat2(result, entry->replacement);
                i += entry->target.length;
                span_start = i;
                goto found_match;
            }
        }
        i += 1;
      found_match: continue;
    }
    if (span_start < text.length)
        result = concat2(result, Text$slice(text, I(span_start+1), I(text.length)));
    return result;
}

public Text_t Text$replace(Text_t text, Text_t target, Text_t replacement)
{
    TextIter_t text_state = NEW_TEXT_ITER_STATE(text), target_state = NEW_TEXT_ITER_STATE(target);
    Text_t result = EMPTY_TEXT;
    int64_t span_start = 0;
    for (int64_t i = 0; i < text.length; ) {
        if (_matches(&text_state, &target_state, i)) {
            if (i > span_start)
                result = concat2(result, Text$slice(text, I(span_start+1), I(i)));

            result = concat2(result, replacement);
            i += target.length;
            span_start = i;
        } else {
            i += 1;
        }
    }
    if (span_start < text.length)
        result = concat2(result, Text$slice(text, I(span_start+1), I(text.length)));
    return result;
}

public PUREFUNC bool Text$has(Text_t text, Text_t target)
{
    TextIter_t text_state = NEW_TEXT_ITER_STATE(text), target_state = NEW_TEXT_ITER_STATE(target);
    for (int64_t i = 0; i < text.length; i++) {
        if (_matches(&text_state, &target_state, i))
            return true;
    }
    return false;
}

public List_t Text$split(Text_t text, Text_t delimiters)
{
    if (delimiters.length == 0)
        return Text$clusters(text);

    TextIter_t text_state = NEW_TEXT_ITER_STATE(text), delim_state = NEW_TEXT_ITER_STATE(delimiters);
    List_t splits = {};
    for (int64_t i = 0; i < text.length; ) {
        int64_t span_len = 0;
        while (i + span_len < text.length && !_matches(&text_state, &delim_state, i + span_len)) {
            span_len += 1;
        }
        Text_t slice = Text$slice(text, I(i+1), I(i+span_len));
        List$insert(&splits, &slice, I(0), sizeof(slice));
        i += span_len + delimiters.length;
        if (i == text.length) {
            Text_t empty = Text("");
            List$insert(&splits, &empty, I(0), sizeof(empty));
        }
    }
    return splits;
}

public List_t Text$split_any(Text_t text, Text_t delimiters)
{
    if (delimiters.length == 0)
        return List(text);

    TextIter_t text_state = NEW_TEXT_ITER_STATE(text), delim_state = NEW_TEXT_ITER_STATE(delimiters);
    List_t splits = {};
    for (int64_t i = 0; i < text.length; ) {
        int64_t span_len = 0;
        while (i + span_len < text.length && !_has_grapheme(&delim_state, Text$get_grapheme_fast(&text_state, i + span_len))) {
            span_len += 1;
        }
        bool trailing_delim = i + span_len < text.length;
        Text_t slice = Text$slice(text, I(i+1), I(i+span_len));
        List$insert(&splits, &slice, I(0), sizeof(slice));
        i += span_len + 1;
        while (i < text.length && _has_grapheme(&delim_state, Text$get_grapheme_fast(&text_state, i))) {
            i += 1;
        }
        if (i >= text.length && trailing_delim) {
            Text_t empty = Text("");
            List$insert(&splits, &empty, I(0), sizeof(empty));
        }
    }
    return splits;
}

typedef struct {
    TextIter_t state;
    int64_t i;
    Text_t delimiter;
} split_iter_state_t;

static OptionalText_t next_split(split_iter_state_t *state)
{
    Text_t text = state->state.stack[0].text;
    if (state->i >= text.length) {
        if (state->delimiter.length > 0 && state->i == text.length) { // special case
            state->i = text.length + 1;
            return EMPTY_TEXT;
        }
        return NONE_TEXT;
    }

    if (state->delimiter.length == 0) { // special case
        state->i = text.length + 1;
        return text;
    }

    TextIter_t delim_state = NEW_TEXT_ITER_STATE(state->delimiter);
    int64_t i = state->i;
    int64_t span_len = 0;
    while (i + span_len < text.length && !_matches(&state->state, &delim_state, i + span_len)) {
        span_len += 1;
    }
    Text_t slice = Text$slice(text, I(i+1), I(i+span_len));
    state->i = i + span_len + state->delimiter.length;
    return slice;
}

public Closure_t Text$by_split(Text_t text, Text_t delimiter)
{
    return (Closure_t){
        .fn=(void*)next_split,
        .userdata=new(split_iter_state_t, .state=NEW_TEXT_ITER_STATE(text), .i=0, .delimiter=delimiter),
    };
}

static OptionalText_t next_split_any(split_iter_state_t *state)
{
    Text_t text = state->state.stack[0].text;
    if (state->i >= text.length) {
        if (state->delimiter.length > 0 && state->i == text.length) { // special case
            state->i = text.length + 1;
            return EMPTY_TEXT;
        }
        return NONE_TEXT;
    }

    if (state->delimiter.length == 0) { // special case
        Text_t ret = Text$cluster(text, I(state->i+1));
        state->i += 1;
        return ret;
    }

    TextIter_t delim_state = NEW_TEXT_ITER_STATE(state->delimiter);
    int64_t i = state->i;
    int64_t span_len = 0;
    while (i + span_len < text.length && !_has_grapheme(&delim_state, Text$get_grapheme_fast(&state->state, i + span_len))) {
        span_len += 1;
    }
    Text_t slice = Text$slice(text, I(i+1), I(i+span_len));
    i += span_len + 1;
    while (i < text.length && _has_grapheme(&delim_state, Text$get_grapheme_fast(&state->state, i))) {
        i += 1;
    }
    state->i = i;
    return slice;
}

public Closure_t Text$by_split_any(Text_t text, Text_t delimiters)
{
    return (Closure_t){
        .fn=(void*)next_split_any,
        .userdata=new(split_iter_state_t, .state=NEW_TEXT_ITER_STATE(text), .i=0, .delimiter=delimiters),
    };
}

PUREFUNC public bool Text$equal_values(Text_t a, Text_t b)
{
    if (a.length != b.length)
        return false;
    int64_t len = a.length;
    TextIter_t a_state = NEW_TEXT_ITER_STATE(a), b_state = NEW_TEXT_ITER_STATE(b);
    // TODO: make this smarter and more efficient
    for (int64_t i = 0; i < len; i++) {
        int32_t ai = Text$get_grapheme_fast(&a_state, i);
        int32_t bi = Text$get_grapheme_fast(&b_state, i);
        if (ai != bi) return false;
    }
    return true;
}

PUREFUNC public bool Text$equal(const void *a, const void *b, const TypeInfo_t *info)
{
    (void)info;
    if (a == b) return true;
    return Text$equal_values(*(Text_t*)a, *(Text_t*)b);
}

PUREFUNC public bool Text$equal_ignoring_case(Text_t a, Text_t b, Text_t language)
{
    if (a.length != b.length)
        return false;
    int64_t len = a.length;
    TextIter_t a_state = NEW_TEXT_ITER_STATE(a), b_state = NEW_TEXT_ITER_STATE(b);
    const char *uc_language = Text$as_c_string(language);
    for (int64_t i = 0; i < len; i++) {
        int32_t ai = Text$get_grapheme_fast(&a_state, i);
        int32_t bi = Text$get_grapheme_fast(&b_state, i);
        if (ai != bi) {
            const ucs4_t *a_codepoints = ai >= 0 ? (ucs4_t*)&ai : GRAPHEME_CODEPOINTS(ai);
            int64_t a_len = ai >= 0 ? 1 : NUM_GRAPHEME_CODEPOINTS(ai);

            const ucs4_t *b_codepoints = bi >= 0 ? (ucs4_t*)&bi : GRAPHEME_CODEPOINTS(bi);
            int64_t b_len = bi >= 0 ? 1 : NUM_GRAPHEME_CODEPOINTS(bi);

            int cmp = 0;
            (void)u32_casecmp(a_codepoints, (size_t)a_len, b_codepoints, (size_t)b_len, uc_language, UNINORM_NFC, &cmp);
            if (cmp != 0)
                return false;
        }
    }
    return true;
}

public Text_t Text$upper(Text_t text, Text_t language)
{
    if (text.length == 0) return text;
    List_t codepoints = Text$utf32_codepoints(text);
    const char *uc_language = Text$as_c_string(language);
    ucs4_t buf[128]; 
    size_t out_len = sizeof(buf)/sizeof(buf[0]);
    ucs4_t *upper = u32_toupper(codepoints.data, (size_t)codepoints.length, uc_language, UNINORM_NFC, buf, &out_len);
    Text_t ret = text_from_u32(upper, (int64_t)out_len, false);
    if (upper != buf) free(upper);
    return ret;
}

public Text_t Text$lower(Text_t text, Text_t language)
{
    if (text.length == 0) return text;
    List_t codepoints = Text$utf32_codepoints(text);
    const char *uc_language = Text$as_c_string(language);
    ucs4_t buf[128]; 
    size_t out_len = sizeof(buf)/sizeof(buf[0]);
    ucs4_t *lower = u32_tolower(codepoints.data, (size_t)codepoints.length, uc_language, UNINORM_NFC, buf, &out_len);
    Text_t ret = text_from_u32(lower, (int64_t)out_len, false);
    if (lower != buf) free(lower);
    return ret;
}

public Text_t Text$title(Text_t text, Text_t language)
{
    if (text.length == 0) return text;
    List_t codepoints = Text$utf32_codepoints(text);
    const char *uc_language = Text$as_c_string(language);
    ucs4_t buf[128]; 
    size_t out_len = sizeof(buf)/sizeof(buf[0]);
    ucs4_t *title = u32_totitle(codepoints.data, (size_t)codepoints.length, uc_language, UNINORM_NFC, buf, &out_len);
    Text_t ret = text_from_u32(title, (int64_t)out_len, false);
    if (title != buf) free(title);
    return ret;
}

public Text_t Text$quoted(Text_t text, bool colorize, Text_t quotation_mark)
{
    if (quotation_mark.length != 1)
        fail("Invalid quote text: ", quotation_mark, " (must have length == 1)");

    Text_t ret = colorize ? Text("\x1b[35m") : EMPTY_TEXT;
    if (!Text$equal_values(quotation_mark, Text("\"")) && !Text$equal_values(quotation_mark, Text("'")) && !Text$equal_values(quotation_mark, Text("`")))
        ret = concat2_assuming_safe(ret, Text("$"));

    ret = concat2_assuming_safe(ret, quotation_mark);
    int32_t quote_char = Text$get_grapheme(quotation_mark, 0);

#define add_escaped(str) ({ if (colorize) ret = concat2_assuming_safe(ret, Text("\x1b[34;1m")); \
                          ret = concat2_assuming_safe(ret, Text("\\" str)); \
                          if (colorize) ret = concat2_assuming_safe(ret, Text("\x1b[0;35m")); })
    TextIter_t state = NEW_TEXT_ITER_STATE(text);
    // TODO: optimize for spans of non-escaped text
    for (int64_t i = 0; i < text.length; i++) {
        int32_t g = Text$get_grapheme_fast(&state, i);
        switch (g) {
        case '\a': add_escaped("a"); break;
        case '\b': add_escaped("b"); break;
        case '\x1b': add_escaped("e"); break;
        case '\f': add_escaped("f"); break;
        case '\n': add_escaped("n"); break;
        case '\r': add_escaped("r"); break;
        case '\t': add_escaped("t"); break;
        case '\v': add_escaped("v"); break;
        case '\\': {
            add_escaped("\\");
            break;
        }
        case '$': {
            add_escaped("$");
            break;
        }
        case '\x00' ... '\x06': case '\x0E' ... '\x1A':
        case '\x1C' ... '\x1F': case '\x7F' ... '\x7F': {
            if (colorize) ret = concat2_assuming_safe(ret, Text("\x1b[34;1m"));
            ret = concat2_assuming_safe(ret, Text("\\x"));
            char tmp[3] = {
                (g / 16) > 9 ? 'a' + (g / 16) - 10 : '0' + (g / 16),
                (g & 15) > 9 ? 'a' + (g & 15) - 10 : '0' + (g & 15),
                '\0',
            };
            ret = concat2_assuming_safe(ret, Text$from_strn(tmp, 2));
            if (colorize)
                ret = concat2_assuming_safe(ret, Text("\x1b[0;35m"));
            break;
        }
        default: {
            if (g == quote_char) {
                ret = concat2_assuming_safe(ret, quotation_mark);
            } else {
                ret = concat2_assuming_safe(ret, Text$slice(text, I(i+1), I(i+1)));
            }
            break;
        }
        }
    }
#undef add_escaped

    ret = concat2_assuming_safe(ret, quotation_mark);
    if (colorize)
        ret = concat2_assuming_safe(ret, Text("\x1b[m"));

    return ret;
}

public Text_t Text$as_text(const void *vtext, bool colorize, const TypeInfo_t *info)
{
    (void)info;
    if (!vtext) return info && info->TextInfo.lang ? Text$from_str(info->TextInfo.lang) : Text("Text");

    Text_t text = *(Text_t*)vtext;
    // Figure out the best quotation mark to use:
    bool has_double_quote = false, has_backtick = false,
         has_single_quote = false, needs_escapes = false;
    TextIter_t state = NEW_TEXT_ITER_STATE(text);
    for (int64_t i = 0; i < text.length; i++) {
        int32_t g = Text$get_grapheme_fast(&state, i);
        if (g == '"') {
            has_double_quote = true;
        } else if (g == '`') {
            has_backtick = true;
        } else if (g == (g & 0x7F) && (g == '\'' || g == '\n' || g == '\r' || g == '\t' || !isprint((char)g))) {
            needs_escapes = true;
        }
    }

    Text_t quote;
    // If there's double quotes in the string, it would be nice to avoid
    // needing to escape them by using single quotes, but only if we don't have
    // single quotes or need to escape anything else (because single quotes
    // don't have interpolation):
    if (has_double_quote && !has_single_quote)
        quote = Text("'");
    // If there is a double quote, but no backtick, we can save a bit of
    // escaping by using backtick instead of double quote:
    else if (has_double_quote && has_single_quote && !has_backtick && !needs_escapes)
        quote = Text("`");
    // Otherwise fall back to double quotes as the default quoting style:
    else
        quote = Text("\"");

    Text_t as_text = Text$quoted(text, colorize, quote);
    if (info && info->TextInfo.lang && info != &Text$info)
        as_text = Text$concat(
            colorize ? Text("\x1b[1m$") : Text("$"),
            Text$from_str(info->TextInfo.lang),
            colorize ? Text("\x1b[0m") : Text(""),
            as_text);
    return as_text;
}

public Text_t Text$join(Text_t glue, List_t pieces)
{
    if (pieces.length == 0) return EMPTY_TEXT;

    Text_t result = *(Text_t*)pieces.data;
    for (int64_t i = 1; i < pieces.length; i++) {
        result = Text$concat(result, glue, *(Text_t*)(pieces.data + i*pieces.stride));
    }
    return result;
}

public List_t Text$clusters(Text_t text)
{
    List_t clusters = {};
    for (int64_t i = 1; i <= text.length; i++) {
        Text_t cluster = Text$slice(text, I(i), I(i));
        List$insert(&clusters, &cluster, I_small(0), sizeof(Text_t));
    }
    return clusters;
}

public List_t Text$utf32_codepoints(Text_t text)
{
    List_t codepoints = {.atomic=1};
    TextIter_t state = NEW_TEXT_ITER_STATE(text);
    for (int64_t i = 0; i < text.length; i++) {
        int32_t grapheme = Text$get_grapheme_fast(&state, i);
        if (grapheme < 0) {
            for (int64_t c = 0; c < NUM_GRAPHEME_CODEPOINTS(grapheme); c++) {
                ucs4_t subg = GRAPHEME_CODEPOINTS(grapheme)[c];
                List$insert(&codepoints, &subg, I_small(0), sizeof(ucs4_t));
            }
        } else {
            List$insert(&codepoints, &grapheme, I_small(0), sizeof(ucs4_t));
        }
    }
    return codepoints;
}

public List_t Text$utf8_bytes(Text_t text)
{
    const char *str = Text$as_c_string(text);
    return (List_t){.length=strlen(str), .stride=1, .atomic=1, .data=(void*)str};
}

static INLINE const char *codepoint_name(ucs4_t c)
{
    char *name = GC_MALLOC_ATOMIC(UNINAME_MAX);
    char *found_name = unicode_character_name(c, name);
    if (found_name) return found_name;
    const uc_block_t *block = uc_block(c);
    assert(block);
    return String(block->name, "-", hex(c, .no_prefix=true, .uppercase=true));
}

public List_t Text$codepoint_names(Text_t text)
{
    List_t names = {};
    TextIter_t state = NEW_TEXT_ITER_STATE(text);
    for (int64_t i = 0; i < text.length; i++) {
        int32_t grapheme = Text$get_grapheme_fast(&state, i);
        if (grapheme < 0) {
            for (int64_t c = 0; c < NUM_GRAPHEME_CODEPOINTS(grapheme); c++) {
                const char *name = codepoint_name(GRAPHEME_CODEPOINTS(grapheme)[c]);
                Text_t name_text = Text$from_str(name);
                List$insert(&names, &name_text, I_small(0), sizeof(Text_t));
            }
        } else {
            const char *name = codepoint_name((ucs4_t)grapheme);
            Text_t name_text = Text$from_str(name);
            List$insert(&names, &name_text, I_small(0), sizeof(Text_t));
        }
    }
    return names;
}

public Text_t Text$from_codepoints(List_t codepoints)
{
    if (codepoints.stride != sizeof(int32_t))
        List$compact(&codepoints, sizeof(int32_t));

    return text_from_u32(codepoints.data, codepoints.length, true);
}

public OptionalText_t Text$from_codepoint_names(List_t codepoint_names)
{
    List_t codepoints = {};
    for (int64_t i = 0; i < codepoint_names.length; i++) {
        Text_t *name = ((Text_t*)(codepoint_names.data + i*codepoint_names.stride));
        const char *name_str = Text$as_c_string(*name);
        ucs4_t codepoint = unicode_name_character(name_str);
        if (codepoint == UNINAME_INVALID)
            return NONE_TEXT;
        List$insert(&codepoints, &codepoint, I_small(0), sizeof(ucs4_t));
    }
    return Text$from_codepoints(codepoints);
}

public OptionalText_t Text$from_bytes(List_t bytes)
{
    if (bytes.stride != sizeof(int8_t))
        List$compact(&bytes, sizeof(int8_t));

    return Text$from_strn(bytes.data, (size_t)bytes.length);
}

public List_t Text$lines(Text_t text)
{
    List_t lines = {};
    TextIter_t state = NEW_TEXT_ITER_STATE(text);
    for (int64_t i = 0, line_start = 0; i < text.length; i++) {
        int32_t grapheme = Text$get_grapheme_fast(&state, i);
        if (grapheme == '\r' && Text$get_grapheme_fast(&state, i + 1) == '\n') { // CRLF
            Text_t line = Text$slice(text, I(line_start+1), I(i));
            List$insert(&lines, &line, I_small(0), sizeof(Text_t));
            i += 1; // skip one extra for CR
            line_start = i + 1;
        } else if (grapheme == '\n') { // newline
            Text_t line = Text$slice(text, I(line_start+1), I(i));
            List$insert(&lines, &line, I_small(0), sizeof(Text_t));
            line_start = i + 1;
        } else if (i == text.length-1 && line_start != i) { // last line
            Text_t line = Text$slice(text, I(line_start+1), I(i+1));
            List$insert(&lines, &line, I_small(0), sizeof(Text_t));
        }
    }
    return lines;
}

typedef struct {
    TextIter_t state;
    int64_t i;
} line_iter_state_t;

static OptionalText_t next_line(line_iter_state_t *state)
{
    Text_t text = state->state.stack[0].text;
    for (int64_t i = state->i; i < text.length; i++) {
        int32_t grapheme = Text$get_grapheme_fast(&state->state, i);
        if (grapheme == '\r' && Text$get_grapheme_fast(&state->state, i + 1) == '\n') { // CRLF
            Text_t line = Text$slice(text, I(state->i+1), I(i));
            state->i = i + 2; // skip one extra for CR
            return line;
        } else if (grapheme == '\n') { // newline
            Text_t line = Text$slice(text, I(state->i+1), I(i));
            state->i = i + 1;
            return line;
        } else if (i == text.length-1 && state->i != i) { // last line
            Text_t line = Text$slice(text, I(state->i+1), I(i+1));
            state->i = i + 1;
            return line;
        }
    }
    return NONE_TEXT;
}

public Closure_t Text$by_line(Text_t text)
{
    return (Closure_t){
        .fn=(void*)next_line,
        .userdata=new(line_iter_state_t, .state=NEW_TEXT_ITER_STATE(text), .i=0),
    };
}

PUREFUNC public bool Text$is_none(const void *t, const TypeInfo_t *info)
{
    (void)info;
    return ((Text_t*)t)->length < 0;
}

public void Text$serialize(const void *obj, FILE *out, Table_t *pointers, const TypeInfo_t *info)
{
    (void)info;
    const char *str = Text$as_c_string(*(Text_t*)obj);
    int64_t len = (int64_t)strlen(str);
    Int64$serialize(&len, out, pointers, &Int64$info);
    fwrite(str, sizeof(char), (size_t)len, out);
}

public void Text$deserialize(FILE *in, void *out, List_t *pointers, const TypeInfo_t *info)
{
    (void)info;
    int64_t len = -1;
    Int64$deserialize(in, &len, pointers, &Int64$info);
    char *buf = GC_MALLOC_ATOMIC((size_t)len+1);
    if (fread(buf, sizeof(char), (size_t)len, in) != (size_t)len)
        fail("Not enough data in stream to deserialize");
    buf[len+1] = '\0';
    *(Text_t*)out = Text$from_strn(buf, (size_t)len);
}

public const TypeInfo_t Text$info = {
    .size=sizeof(Text_t),
    .align=__alignof__(Text_t),
    .tag=TextInfo,
    .TextInfo={.lang="Text"},
    .metamethods=Text$metamethods,
};

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
