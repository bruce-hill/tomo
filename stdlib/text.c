// Type info and methods for Text datatype, which uses libunistr for Unicode
// support and implements a datastructure based on Raku/MoarVM's strings to
// efficiently store arbitrary unicode data using a mix of densely packed plain
// ASCII, 32-bit integers representing grapheme clusters (see below), and ropes
// that represent text that is a composite of multiple subtexts. Subtexts are
// only nested one level deep, not arbitrarily deep trees.
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
// There are a lot of benefits to storing text with one grapheme cluster per
// index in a densely packed array. It lets us have one canonical length for
// the text that can be precomputed and is meaningful to users. It lets us
// quickly get the Nth "letter" in the text. Substring slicing is fast.
// However, since not all grapheme clusters take up the same number of
// codepoints, we're faced with the problem of how to jam multiple codepoints
// into a single 32-bit slot. Inspired by Raku and MoarVM's approach, this
// implementation uses "synthetic graphemes" (in Raku's terms, Normal Form
// Graphemes, aka NFG). A synthetic grapheme is a negative 32-bit signed
// integer that represents a multi-codepoint grapheme cluster that has been
// encountered during the program's runtime. These clusters are stored in a
// lookup array and hash map so that we can rapidly convert between the
// synthetic grapheme integer ID and the unicode codepoints associated with it.
// Essentially, it's like we create a supplement to the unicode standard with
// things that would be nice if they had their own codepoint so things worked
// out nicely because we're using them right now, and we'll give them a
// negative number so it doesn't overlap with any real codepoints.
//
// Example 1: U+0048, U+00E9
//    AKA: LATIN CAPITAL LETTER H, LATIN SMALL LETTER E WITH ACUTE
//    This would be stored as: (int32_t[]){0x48, 0xE9}
// Example 2: U+0048, U+0065, U+0309
//    AKA: LATIN CAPITAL LETTER H, LATIN SMALL LETTER E, COMBINING VERTICAL LINE BELOW
//    This would be stored as: (int32_t[]){0x48, -2}
//    Where -2 is used as a lookup in an array that holds the actual unicode codepoints:
//       (ucs4_t[]){0x65, 0x0309}

#include <assert.h>
#include <ctype.h>
#include <gc.h>
#include <printf.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/param.h>

#include <unicase.h>
#include <unictype.h>
#include <unigbrk.h>
#include <uniname.h>

#include "arrays.h"
#include "integers.h"
#include "patterns.h"
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

// This will hold a dynamically growing array of synthetic graphemes:
static synthetic_grapheme_t *synthetic_graphemes = NULL;
static int32_t synthetic_grapheme_capacity = 0;
static int32_t num_synthetic_graphemes = 0;

#define MAIN_GRAPHEME_CODEPOINT(_g) ({ int32_t g = _g; (g) >= 0 ? (ucs4_t)(g) : synthetic_graphemes[-(g)-1].main_codepoint; })
#define NUM_GRAPHEME_CODEPOINTS(id) (synthetic_graphemes[-(id)-1].utf32_cluster[0])
#define GRAPHEME_CODEPOINTS(id) (&synthetic_graphemes[-(id)-1].utf32_cluster[1])
#define GRAPHEME_UTF8(id) (synthetic_graphemes[-(id)-1].utf8)

static Text_t text_from_u32(ucs4_t *codepoints, int64_t num_codepoints, bool normalize);

PUREFUNC static bool graphemes_equal(ucs4_t **a, ucs4_t **b) {
    if ((*a)[0] != (*b)[0]) return false;
    for (int i = 0; i < (int)(*a)[0]; i++)
        if ((*a)[i] != (*b)[i]) return false;
    return true;
}

PUREFUNC static uint64_t grapheme_hash(ucs4_t **g) {
    ucs4_t *cluster = *g;
    return siphash24((void*)&cluster[1], sizeof(ucs4_t[cluster[0]]));
}

static const TypeInfo GraphemeClusterInfo = {
    .size=sizeof(ucs4_t*),
    .align=__alignof__(ucs4_t*),
    .tag=CustomInfo,
    .CustomInfo={.equal=(void*)graphemes_equal, .hash=(void*)grapheme_hash},
};

static const TypeInfo GraphemeIDLookupTableInfo = {
    .size=sizeof(Table_t), .align=__alignof__(Table_t),
    .tag=TableInfo, .TableInfo={.key=&GraphemeClusterInfo, .value=&Int32$info},
};

#pragma GCC diagnostic ignored "-Wstack-protector"
public int32_t get_synthetic_grapheme(const ucs4_t *codepoints, int64_t utf32_len)
{
    ucs4_t length_prefixed[1+utf32_len] = {};
    length_prefixed[0] = (ucs4_t)utf32_len;
    for (int i = 0; i < utf32_len; i++)
        length_prefixed[i+1] = codepoints[i];
    ucs4_t *ptr = &length_prefixed[0];

    // Optimization for common case of one frequently used synthetic grapheme:
    static int32_t last_grapheme = 0;
    if (last_grapheme != 0 && graphemes_equal(&ptr, &synthetic_graphemes[-last_grapheme-1].utf32_cluster))
        return last_grapheme;

    int32_t *found = Table$get(grapheme_ids_by_codepoints, &ptr, &GraphemeIDLookupTableInfo);
    if (found) return *found;

    // New synthetic grapheme:
    if (num_synthetic_graphemes >= synthetic_grapheme_capacity) {
        // If we don't have space, allocate more:
        synthetic_grapheme_capacity = MAX(128, synthetic_grapheme_capacity * 2);
        synthetic_grapheme_t *new = GC_MALLOC(sizeof(synthetic_grapheme_t[synthetic_grapheme_capacity]));
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
    mempcpy(codepoint_copy, length_prefixed, sizeof(ucs4_t[1+utf32_len]));
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
        if (!__builtin_expect(uc_is_property_prepended_concatenation_mark(length_prefixed[1+i]), 0)) {
            synthetic_graphemes[-grapheme_id-1].main_codepoint = length_prefixed[1+i];
            break;
        }
    }

    // Cleanup from unicode API:
    if (u8 != u8_buf) free(u8);

    Table$set(&grapheme_ids_by_codepoints, &codepoint_copy, &grapheme_id, &GraphemeIDLookupTableInfo);

    last_grapheme = grapheme_id;
    return grapheme_id;
}

PUREFUNC static inline int64_t num_subtexts(Text_t t)
{
    if (t.tag != TEXT_SUBTEXT) return 1;
    int64_t len = t.length;
    int64_t n = 0;
    while (len > 0) {
        len -= t.subtexts[n].length;
        ++n;
    }
    return n;
}

int text_visualize(FILE *stream, Text_t t)
{
    switch (t.tag) {
    case TEXT_SHORT_ASCII: return fprintf(stream, "<ascii length=%ld>%.*s</ascii>", t.length, t.length, t.short_ascii);
    case TEXT_ASCII: return fprintf(stream, "<ascii length=%ld>%.*s</ascii>", t.length, t.length, t.ascii);
    case TEXT_GRAPHEMES: case TEXT_SHORT_GRAPHEMES: {
        int printed = fprintf(stream, "<graphemes length=%ld>", t.length);
        printed += Text$print(stream, t);
        printed += fprintf(stream, "</graphemes>");
        return printed;
    }
    case TEXT_SUBTEXT: {
        int printed = fprintf(stream, "<text length=%ld>", t.length);
        int64_t to_print = t.length;
        for (int i = 0; to_print > 0; ++i) {
            printed += fprintf(stream, "\n  ");
            printed += text_visualize(stream, t.subtexts[i]);
            to_print -= t.subtexts[i].length;
            if (t.subtexts[i].length == 0) break;
        }
        printed += fprintf(stream, "\n</text>");
        return printed;
    }
    default: return 0;
    }
}

public int Text$print(FILE *stream, Text_t t)
{
    if (t.length == 0) return 0;

    switch (t.tag) {
    case TEXT_SHORT_ASCII: return fwrite(t.short_ascii, sizeof(char), (size_t)t.length, stream);
    case TEXT_ASCII: return fwrite(t.ascii, sizeof(char), (size_t)t.length, stream);
    case TEXT_GRAPHEMES: case TEXT_SHORT_GRAPHEMES: {
        const int32_t *graphemes = t.tag == TEXT_SHORT_GRAPHEMES ? t.short_graphemes : t.graphemes;
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
    case TEXT_SUBTEXT: {
        int written = 0;
        int i = 0;
        for (int64_t to_print = t.length; to_print > 0; to_print -= t.subtexts[i].length, ++i)
            written += Text$print(stream, t.subtexts[i]);
        return written;
    }
    default: return 0;
    }
}

static bool is_concat_stable(Text_t a, Text_t b)
{
    if (a.length == 0 || b.length == 0)
        return true;

    int32_t last_a = Text$get_grapheme(a, a.length-1);
    int32_t first_b = Text$get_grapheme(b, 0);

    // Synthetic graphemes are weird and probably need to check with normalization:
    if (last_a < 0 || first_b < 0)
        return 0;

    // Magic number, we know that no codepoints below here trigger instability:
    static const int32_t LOWEST_CODEPOINT_TO_CHECK = 0x300;
    if (last_a < LOWEST_CODEPOINT_TO_CHECK && first_b < LOWEST_CODEPOINT_TO_CHECK)
        return true;

    // Do a normalization run for these two codepoints and see if it looks different:
    ucs4_t codepoints[2] = {(ucs4_t)last_a, (ucs4_t)first_b};
    ucs4_t norm_buf[3*2]; // Normalization should not exceed 3x in the input length
    size_t norm_length = sizeof(norm_buf)/sizeof(norm_buf[0]);
    ucs4_t *normalized = u32_normalize(UNINORM_NFC, codepoints, 2, norm_buf, &norm_length);
    if (norm_length != 2) {
        // Looks like these two codepoints merged into one (or maybe had a child, who knows?)
        if (normalized != norm_buf) free(normalized);
        return false;
    }

    // If there's still two codepoints, we might end up with a single grapheme
    // cluster which will need to turn into a synthetic grapheme:
    const void *second_grapheme = u32_grapheme_next(normalized, &normalized[2]);
    if (normalized != norm_buf) free(normalized);
    return (second_grapheme == &normalized[1]);
}

static Text_t concat2_assuming_safe(Text_t a, Text_t b)
{
    if (a.length == 0) return b;
    if (b.length == 0) return a;

    if (a.tag == TEXT_SUBTEXT && b.tag == TEXT_SUBTEXT) {
        int64_t na = num_subtexts(a);
        int64_t nb = num_subtexts(b);
        Text_t ret = {
            .length=a.length + b.length,
            .tag=TEXT_SUBTEXT,
            .subtexts=GC_MALLOC(sizeof(Text_t[na + nb])),
        };
        memcpy(&ret.subtexts[0], a.subtexts, sizeof(Text_t[na]));
        memcpy(&ret.subtexts[na], b.subtexts, sizeof(Text_t[nb]));
        return ret;
    } else if (a.tag == TEXT_SUBTEXT) {
        int64_t n = num_subtexts(a);
        Text_t ret = {
            .length=a.length + b.length,
            .tag=TEXT_SUBTEXT,
            .subtexts=GC_MALLOC(sizeof(Text_t[n + 1])),
        };
        memcpy(ret.subtexts, a.subtexts, sizeof(Text_t[n]));
        ret.subtexts[n] = b;
        return ret;
    } else if (b.tag == TEXT_SUBTEXT) {
        int64_t n = num_subtexts(b);
        Text_t ret = {
            .length=a.length + b.length,
            .tag=TEXT_SUBTEXT,
            .subtexts=GC_MALLOC(sizeof(Text_t[n + 1])),
        };
        ret.subtexts[0] = a;
        memcpy(&ret.subtexts[1], b.subtexts, sizeof(Text_t[n]));
        return ret;
    } else {
        Text_t ret = {
            .length=a.length + b.length,
            .tag=TEXT_SUBTEXT,
            .subtexts=GC_MALLOC(sizeof(Text_t[2])),
        };
        ret.subtexts[0] = a;
        ret.subtexts[1] = b;
        return ret;
    }
}

static Text_t concat2(Text_t a, Text_t b)
{
    if (a.length == 0) return b;
    if (b.length == 0) return a;

    if (__builtin_expect(is_concat_stable(a, b), 1))
        return concat2_assuming_safe(a, b);

    // Do full normalization of the last/first characters
    int32_t last_a = Text$get_grapheme(a, a.length-1);
    int32_t first_b = Text$get_grapheme(b, 0);

    size_t utf32_len = (last_a >= 0 ? 1 : NUM_GRAPHEME_CODEPOINTS(last_a)) + (first_b >= 0 ? 1 : NUM_GRAPHEME_CODEPOINTS(first_b)); 
    ucs4_t join_graphemes[utf32_len] = {};
    ucs4_t *p = &join_graphemes[0];
    if (last_a < 0) p = mempcpy(p, GRAPHEME_CODEPOINTS(last_a), NUM_GRAPHEME_CODEPOINTS(last_a));
    else *(p++) = (ucs4_t)last_a;
    if (first_b < 0) p = mempcpy(p, GRAPHEME_CODEPOINTS(first_b), NUM_GRAPHEME_CODEPOINTS(first_b));
    else *(p++) = (ucs4_t)first_b;

    Text_t glue = text_from_u32(join_graphemes, (int64_t)utf32_len, true);

    if (a.length == 1 && b.length == 1)
        return glue;
    else if (a.length == 1)
        return concat2_assuming_safe(glue, Text$slice(b, I(2), I(b.length)));
    else if (b.length == 1)
        return concat2_assuming_safe(Text$slice(a, I(1), I(a.length-1)), glue);
    else
        return concat2_assuming_safe(
            concat2_assuming_safe(Text$slice(a, I(1), I(a.length-1)), glue),
            b);
}

public Text_t Text$_concat(int n, Text_t items[n])
{
    if (n == 0) return (Text_t){.length=0};
    if (n == 1) return items[0];
    if (n == 2) return concat2(items[0], items[1]);

    int64_t len = 0, subtexts = 0;
    for (int i = 0; i < n; i++) {
        len += items[i].length;
        if (items[i].length > 0)
            subtexts += num_subtexts(items[i]);
    }

    Text_t ret = {
        .length=0,
        .tag=TEXT_SUBTEXT,
        .subtexts=GC_MALLOC(sizeof(Text_t[len])),
    };
    int64_t sub_i = 0;
    for (int i = 0; i < n; i++) {
        if (items[i].length == 0)
            continue;

        if (i > 0 && !__builtin_expect(is_concat_stable(items[i-1], items[i]), 1)) {
            // Oops, guess this wasn't stable for concatenation, let's break it
            // up into subtasks:
            return concat2(ret, Text$_concat(n-i, &items[i]));
        }

        if (items[i].tag == TEXT_SUBTEXT) {
            for (int64_t j = 0, remainder = items[i].length; remainder > 0; j++) {
                ret.subtexts[sub_i++] = items[i].subtexts[j];
                remainder -= items[i].subtexts[j].length;
            }
        } else {
            ret.subtexts[sub_i++] = items[i];
        }
        ret.length += items[i].length;
    }
    return ret;
}

public Text_t Text$repeat(Text_t text, Int_t count)
{
    if (text.length == 0 || Int$is_negative(count))
        return Text("");

    Int_t result_len = Int$times(count, I(text.length));
    if (Int$compare_value(result_len, I(1l<<40)) > 0)
        fail("Text repeating would produce too big of an result!");

    int64_t count64 = Int_to_Int64(count, false);
    if (text.tag == TEXT_SUBTEXT) {
        int64_t subtexts = num_subtexts(text);
        Text_t ret = {
            .length=text.length * count64,
            .tag=TEXT_SUBTEXT,
            .subtexts=GC_MALLOC(sizeof(Text_t[subtexts * count64])),
        };
        for (int64_t c = 0; c < count64; c++) {
            for (int64_t i = 0; i < subtexts; i++) {
                if (text.subtexts[i].length > 0)
                    ret.subtexts[c*subtexts + i] = text.subtexts[i];
            }
        }
        return ret;
    } else {
        Text_t ret = {
            .length=text.length * count64,
            .tag=TEXT_SUBTEXT,
            .subtexts=GC_MALLOC(sizeof(Text_t[count64])),
        };
        for (int64_t i = 0; i < count64; i++)
            ret.subtexts[i] = text;
        return ret;
    }
}

public Text_t Text$slice(Text_t text, Int_t first_int, Int_t last_int)
{
    int64_t first = Int_to_Int64(first_int, false);
    int64_t last = Int_to_Int64(last_int, false);
    if (first == 0) fail("Invalid index: 0");
    if (last == 0) return (Text_t){.length=0};

    if (first < 0) first = text.length + first + 1;
    if (last < 0) last = text.length + last + 1;

    if (last > text.length) last = text.length;

    if (first > text.length || last < first)
        return (Text_t){.length=0};

    if (first == 1 && last == text.length)
        return text;

    switch (text.tag) {
    case TEXT_SHORT_ASCII: {
        Text_t ret = (Text_t) {
            .tag=TEXT_SHORT_ASCII,
            .length=last - first + 1,
        };
        memcpy(ret.short_ascii, text.short_ascii + (first-1), (size_t)ret.length);
        return ret;
    }
    case TEXT_ASCII: {
        Text_t ret = {
            .tag=TEXT_ASCII,
            .length=last - first + 1,
            .ascii=text.ascii + (first-1),
        };
        return ret;
    }
    case TEXT_SHORT_GRAPHEMES: {
        assert((first == 1 && last == 1) || (first == 2 && last == 2));
        Text_t ret = {
            .tag=TEXT_SHORT_GRAPHEMES,
            .length=1,
            .short_graphemes={text.short_graphemes[first-1]},
        };
        return ret;
    }
    case TEXT_GRAPHEMES: {
        Text_t ret = {
            .tag=TEXT_GRAPHEMES,
            .length=last - first + 1,
            .graphemes=text.graphemes + (first-1),
        };
        return ret;
    }
    case TEXT_SUBTEXT: {
        Text_t *subtexts = text.subtexts;
        while (first > subtexts[0].length) {
            first -= subtexts[0].length;
            last -= subtexts[0].length;
            ++subtexts;
        }

        int64_t needed_len = (last - first) + 1;
        int64_t num_subtexts = 0;
        for (int64_t included = 0; included < needed_len; ) {
            if (included == 0)
                included += subtexts[num_subtexts].length - first + 1;
            else
                included += subtexts[num_subtexts].length;
            num_subtexts += 1;
        }
        if (num_subtexts == 1)
            return Text$slice(subtexts[0], I(first), I(last));

        Text_t ret = {
            .length=needed_len,
            .tag=TEXT_SUBTEXT,
            .subtexts=GC_MALLOC(sizeof(Text_t[num_subtexts])),
        };
        for (int64_t i = 0; i < num_subtexts; i++) {
            ret.subtexts[i] = Text$slice(subtexts[i], I(first), I(last));
            first = 1;
            needed_len -= ret.subtexts[i].length;
            last = first + needed_len - 1;
        }
        return ret;
    }
    default: errx(1, "Invalid tag");
    }
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

    // char breaks[num_codepoints];
    // u32_grapheme_breaks(codepoints, num_codepoints, breaks);

    Text_t ret = {
        .length=0,
        .tag=TEXT_SHORT_GRAPHEMES,
    };
    const ucs4_t *src = codepoints;
    int32_t *graphemes = ret.short_graphemes;
    while (src < &codepoints[num_codepoints]) {
        if (ret.tag == TEXT_SHORT_GRAPHEMES && ret.length + 1 > 2) {
            graphemes = GC_MALLOC_ATOMIC(sizeof(int32_t[num_codepoints])); // May be a slight overallocation
            graphemes[0] = ret.short_graphemes[0];
            graphemes[1] = ret.short_graphemes[1];
            ret.tag = TEXT_GRAPHEMES;
            ret.graphemes = graphemes;
        }

        // TODO: use grapheme breaks instead of u32_grapheme_next()
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

public Text_t Text$from_strn(const char *str, size_t len)
{
    int64_t ascii_span = 0;
    for (size_t i = 0; i < len && isascii(str[i]); i++)
        ascii_span++;

    if (ascii_span == (int64_t)len) { // All ASCII
        Text_t ret = {.length=ascii_span};
        if (ascii_span <= 8) {
            ret.tag = TEXT_SHORT_ASCII;
            for (int64_t i = 0; i < ascii_span; i++)
                ret.short_ascii[i] = str[i];
        } else {
            ret.tag = TEXT_ASCII;
            ret.ascii = str;
        }
        return ret;
    } else {
        if (u8_check((uint8_t*)str, len) != NULL)
            return Text("");

        ucs4_t buf[128];
        size_t length = sizeof(buf)/sizeof(buf[0]);

        ucs4_t *codepoints = u8_to_u32((uint8_t*)str, (size_t)ascii_span + strlen(str + ascii_span), buf, &length);
        Text_t ret = text_from_u32(codepoints, (int64_t)length, true);
        if (codepoints != buf) free(codepoints);
        return ret;
    }
}

public Text_t Text$from_str(const char *str)
{
    return str ? Text$from_strn(str, strlen(str)) : Text("");
}

static void u8_buf_append(Text_t text, char **buf, int64_t *capacity, int64_t *i)
{
    switch (text.tag) {
    case TEXT_ASCII: case TEXT_SHORT_ASCII: {
        if (*i + text.length > (int64_t)*capacity) {
            *capacity = *i + text.length + 1;
            *buf = GC_REALLOC(*buf, (size_t)*capacity);
        }

        const char *bytes = text.tag == TEXT_ASCII ? text.ascii : text.short_ascii;
        memcpy(*buf + *i, bytes, (size_t)text.length);
        *i += text.length;
        break;
    }
    case TEXT_GRAPHEMES: case TEXT_SHORT_GRAPHEMES: {
        const int32_t *graphemes = text.tag == TEXT_GRAPHEMES ? text.graphemes : text.short_graphemes;
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
    case TEXT_SUBTEXT: {
        for (int64_t s = 0, remaining = text.length; remaining > 0; s++) {
            u8_buf_append(text.subtexts[s], buf, capacity, i);
            remaining -= text.subtexts[s].length;
        }
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

PUREFUNC public uint64_t Text$hash(Text_t *text)
{
    if (text->hash != 0) return text->hash;
    siphash sh;
    siphashinit(&sh, sizeof(int32_t[text->length]));

    union {
        int32_t chunks[2];
        uint64_t whole;
    } tmp;
    switch (text->tag) {
    case TEXT_ASCII: case TEXT_SHORT_ASCII: {
        const char *bytes = text->tag == TEXT_ASCII ? text->ascii : text->short_ascii;
        for (int64_t i = 0; i + 1 < text->length; i++) {
            tmp.chunks[0] = (int32_t)bytes[i];
            tmp.chunks[1] = (int32_t)bytes[i+1];
            siphashadd64bits(&sh, tmp.whole);
        }
        int32_t last = text->length & 0x1 ? (int32_t)bytes[text->length-1] : 0; // Odd number of graphemes
        text->hash = siphashfinish_last_part(&sh, (uint64_t)last);
        break;
    }
    case TEXT_GRAPHEMES: {
        const int32_t *graphemes = text->graphemes;
        for (int64_t i = 0; i + 1 < text->length; i++) {
            tmp.chunks[0] = graphemes[i];
            tmp.chunks[1] = graphemes[i];
            siphashadd64bits(&sh, tmp.whole);
        }
        int32_t last = text->length & 0x1 ? graphemes[text->length-1] : 0; // Odd number of graphemes
        text->hash = siphashfinish_last_part(&sh, (uint64_t)last);
        break;
    }
    case TEXT_SHORT_GRAPHEMES: {
        tmp.chunks[0] = text->short_graphemes[0];
        if (text->length > 1)
            tmp.chunks[1] = text->short_graphemes[1];
        text->hash = siphashfinish_last_part(&sh, (uint64_t)tmp.whole);
        break;
    }
    case TEXT_SUBTEXT: {
        int32_t leftover = 0;
        for (int64_t sub_i = 0, to_hash = text->length; to_hash > 0; ) {
            Text_t subtext = text->subtexts[sub_i];
            if (subtext.tag == TEXT_ASCII || subtext.tag == TEXT_SHORT_ASCII) {
                const char *bytes = subtext.tag == TEXT_ASCII ? subtext.ascii : subtext.short_ascii;
                int64_t grapheme = 0;
                if (leftover) {
                    tmp.chunks[0] = leftover;
                    tmp.chunks[1] = (int32_t)bytes[0];
                    siphashadd64bits(&sh, tmp.whole);
                    grapheme += 1;
                }
                for (; grapheme + 1 < subtext.length; grapheme += 2) {
                    tmp.chunks[0] = (int32_t)bytes[grapheme];
                    tmp.chunks[1] = (int32_t)bytes[grapheme+1];
                    siphashadd64bits(&sh, tmp.whole);
                }
                leftover = grapheme < subtext.length ? (int32_t)bytes[grapheme] : 0;
            } else if (subtext.tag == TEXT_SHORT_GRAPHEMES) {
                if (leftover) {
                    tmp.chunks[0] = leftover;
                    tmp.chunks[1] = subtext.short_graphemes[0];
                    siphashadd64bits(&sh, tmp.whole);
                    leftover = subtext.length > 1 ? subtext.short_graphemes[1] : 0;
                } else if (subtext.length == 1) {
                    leftover = subtext.short_graphemes[0];
                } else {
                    tmp.chunks[0] = subtext.short_graphemes[0];
                    tmp.chunks[1] = subtext.short_graphemes[1];
                    siphashadd64bits(&sh, tmp.whole);
                }
            } else if (subtext.tag == TEXT_GRAPHEMES) {
                const int32_t *graphemes = subtext.graphemes;
                int64_t grapheme = 0;
                if (leftover) {
                    tmp.chunks[0] = leftover;
                    tmp.chunks[1] = graphemes[0];
                    siphashadd64bits(&sh, tmp.whole);
                    grapheme += 1;
                }
                for (; grapheme + 1 < subtext.length; grapheme += 2) {
                    tmp.chunks[0] = graphemes[grapheme];
                    tmp.chunks[1] = graphemes[grapheme+1];
                    siphashadd64bits(&sh, tmp.whole);
                }
                leftover = grapheme < subtext.length ? graphemes[grapheme] : 0;
            }
        
            to_hash -= text->subtexts[sub_i].length;

            ++sub_i;
        }

        text->hash = siphashfinish_last_part(&sh, (uint64_t)leftover);
        break;
    }
    default: errx(1, "Invalid text");
    }

    if (text->hash == 0)
        text->hash = 1;

    return text->hash;
}

public int32_t Text$get_grapheme_fast(TextIter_t *state, int64_t index)
{
    Text_t text = state->text;
    switch (text.tag) {
    case TEXT_ASCII: return index < text.length ? (int32_t)text.ascii[index] : 0;
    case TEXT_SHORT_ASCII: return index < text.length ? (int32_t)text.short_ascii[index] : 0;
    case TEXT_GRAPHEMES: return index < text.length ? text.graphemes[index] : 0;
    case TEXT_SHORT_GRAPHEMES: return index < text.length ? text.short_graphemes[index] : 0;
    case TEXT_SUBTEXT: {
        if (index < 0 || index >= text.length)
            return 0;

        while (index < state->sum_of_previous_subtexts && state->subtext > 0) {
            state->sum_of_previous_subtexts -= text.subtexts[state->subtext].length;
            state->subtext -= 1;
        }
        for (;;) {
            if (index < state->sum_of_previous_subtexts + text.subtexts[state->subtext].length)
                return Text$get_grapheme(text.subtexts[state->subtext], index - state->sum_of_previous_subtexts);
            state->sum_of_previous_subtexts += text.subtexts[state->subtext].length;
            state->subtext += 1;
        }
        return 0;
    }
    default: errx(1, "Invalid text");
    }
    return 0;
}

public ucs4_t Text$get_main_grapheme_fast(TextIter_t *state, int64_t index)
{
    return MAIN_GRAPHEME_CODEPOINT(Text$get_grapheme_fast(state, index));
}

PUREFUNC public int32_t Text$compare(const Text_t *a, const Text_t *b)
{
    if (a == b) return 0;

    int64_t len = MAX(a->length, b->length);
    TextIter_t a_state = {*a, 0, 0}, b_state = {*b, 0, 0};
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

PUREFUNC public bool Text$starts_with(Text_t text, Text_t prefix)
{
    if (text.length < prefix.length)
        return false;
    TextIter_t text_state = {text, 0, 0}, prefix_state = {prefix, 0, 0};
    for (int64_t i = 0; i < prefix.length; i++) {
        int32_t text_i = Text$get_grapheme_fast(&text_state, i);
        int32_t prefix_i = Text$get_grapheme_fast(&prefix_state, i);
        if (text_i != prefix_i) return false;
    }
    return true;
}

PUREFUNC public bool Text$ends_with(Text_t text, Text_t suffix)
{
    if (text.length < suffix.length)
        return false;
    TextIter_t text_state = {text, 0, 0}, suffix_state = {suffix, 0, 0};
    for (int64_t i = 0; i < suffix.length; i++) {
        int32_t text_i = Text$get_grapheme_fast(&text_state, text.length - suffix.length + i);
        int32_t suffix_i = Text$get_grapheme_fast(&suffix_state, i);
        if (text_i != suffix_i) return false;
    }
    return true;
}

PUREFUNC public bool Text$equal_values(Text_t a, Text_t b)
{
    if (a.length != b.length || (a.hash != 0 && b.hash != 0 && a.hash != b.hash))
        return false;
    int64_t len = a.length;
    TextIter_t a_state = {a, 0, 0}, b_state = {b, 0, 0};
    for (int64_t i = 0; i < len; i++) {
        int32_t ai = Text$get_grapheme_fast(&a_state, i);
        int32_t bi = Text$get_grapheme_fast(&b_state, i);
        if (ai != bi) return false;
    }
    return true;
}

PUREFUNC public bool Text$equal(const Text_t *a, const Text_t *b)
{
    if (a == b) return true;
    return Text$equal_values(*a, *b);
}

PUREFUNC public bool Text$equal_ignoring_case(Text_t a, Text_t b)
{
    if (a.length != b.length)
        return false;
    int64_t len = a.length;
    TextIter_t a_state = {a, 0, 0}, b_state = {b, 0, 0};
    const char *language = uc_locale_language();
    for (int64_t i = 0; i < len; i++) {
        int32_t ai = Text$get_grapheme_fast(&a_state, i);
        int32_t bi = Text$get_grapheme_fast(&b_state, i);
        if (ai != bi) {
            const ucs4_t *a_codepoints = ai >= 0 ? (ucs4_t*)&ai : GRAPHEME_CODEPOINTS(ai);
            int64_t a_len = ai >= 0 ? 1 : NUM_GRAPHEME_CODEPOINTS(ai);

            const ucs4_t *b_codepoints = bi >= 0 ? (ucs4_t*)&bi : GRAPHEME_CODEPOINTS(bi);
            int64_t b_len = bi >= 0 ? 1 : NUM_GRAPHEME_CODEPOINTS(bi);

            int cmp = 0;
            (void)u32_casecmp(a_codepoints, (size_t)a_len, b_codepoints, (size_t)b_len, language, UNINORM_NFC, &cmp);
            if (cmp != 0)
                return false;
        }
    }
    return true;
}

public Text_t Text$upper(Text_t text)
{
    if (text.length == 0) return text;
    Array_t codepoints = Text$utf32_codepoints(text);
    const char *language = uc_locale_language();
    ucs4_t buf[128]; 
    size_t out_len = sizeof(buf)/sizeof(buf[0]);
    ucs4_t *upper = u32_toupper(codepoints.data, (size_t)codepoints.length, language, UNINORM_NFC, buf, &out_len);
    Text_t ret = text_from_u32(upper, (int64_t)out_len, false);
    if (upper != buf) free(upper);
    return ret;
}

public Text_t Text$lower(Text_t text)
{
    if (text.length == 0) return text;
    Array_t codepoints = Text$utf32_codepoints(text);
    const char *language = uc_locale_language();
    ucs4_t buf[128]; 
    size_t out_len = sizeof(buf)/sizeof(buf[0]);
    ucs4_t *lower = u32_tolower(codepoints.data, (size_t)codepoints.length, language, UNINORM_NFC, buf, &out_len);
    Text_t ret = text_from_u32(lower, (int64_t)out_len, false);
    if (lower != buf) free(lower);
    return ret;
}

public Text_t Text$title(Text_t text)
{
    if (text.length == 0) return text;
    Array_t codepoints = Text$utf32_codepoints(text);
    const char *language = uc_locale_language();
    ucs4_t buf[128]; 
    size_t out_len = sizeof(buf)/sizeof(buf[0]);
    ucs4_t *title = u32_totitle(codepoints.data, (size_t)codepoints.length, language, UNINORM_NFC, buf, &out_len);
    Text_t ret = text_from_u32(title, (int64_t)out_len, false);
    if (title != buf) free(title);
    return ret;
}

public int printf_text_size(const struct printf_info *info, size_t n, int argtypes[n], int sizes[n])
{
    if (n < 1) return -1;
    (void)info;
    argtypes[0] = PA_POINTER;
    sizes[0] = sizeof(Text_t*);
    return 1;
}

public int printf_text(FILE *stream, const struct printf_info *info, const void *const args[])
{
    Text_t t = **(Text_t**)args[0];
    if (info->alt)
        return text_visualize(stream, t);
    else
        return Text$print(stream, t);
}

static inline Text_t _quoted(Text_t text, bool colorize, char quote_char)
{
    // TODO: optimize for ASCII and short strings
    Array_t graphemes = {.atomic=1};
#define add_char(c) Array$insert_value(&graphemes, (ucs4_t)c, I_small(0), sizeof(ucs4_t))
#define add_str(s) ({ for (const char *_c = s; *_c; ++_c) Array$insert_value(&graphemes, (ucs4_t)*_c, I_small(0), sizeof(ucs4_t)); })
    if (colorize)
        add_str("\x1b[35m");
    if (quote_char != '"' && quote_char != '\'' && quote_char != '`')
        add_char('$');
    add_char(quote_char);

#define add_escaped(str) ({ if (colorize) add_str("\x1b[34;1m"); \
                          if (!just_escaped) add_char('$'); \
                          add_char('\\'); add_str(str); just_escaped = true; \
                          if (colorize) add_str("\x1b[0;35m"); })
    TextIter_t state = {text, 0, 0};
    bool just_escaped = false;
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
            if (just_escaped) {
                add_escaped("\\");
            } else {
                add_char('\\');
                just_escaped = false;
            }
            break;
        }
        case '$': {
            if (quote_char == '\'') {
                add_char('$');
                just_escaped = false;
            } else {
                add_escaped("$");
            }
            break;
        }
        case '\x00' ... '\x06': case '\x0E' ... '\x1A':
        case '\x1C' ... '\x1F': case '\x7F' ... '\x7F': {
            if (colorize) add_str("\x1b[34;1m");
            add_char('\\');
            add_char('x');
            char tmp[4];
            sprintf(tmp, "%02X", g);
            add_str(tmp);
            if (colorize)
                add_str("\x1b[0;35m");
            just_escaped = true;
            break;
        }
        default: {
            if (g == quote_char) {
                add_escaped(((char[2]){quote_char, 0}));
            } else {
               add_char(g);
               just_escaped = false;
            }
            break;
        }
        }
    }

    add_char(quote_char);
    if (colorize)
        add_str("\x1b[m");

    return (Text_t){.length=graphemes.length, .tag=TEXT_GRAPHEMES, .graphemes=graphemes.data};
#undef add_str
#undef add_char
#undef add_escaped
}

public Text_t Text$as_text(const void *text, bool colorize, const TypeInfo *info)
{
    (void)info;
    if (info->TextInfo.lang && streq(info->TextInfo.lang, "Path")) {
        if (!text) return Text("Path");
        return Text$format("(%s%k%s)", colorize ? "\x1b[35m" : "", text, colorize ? "\x1b[m" : "");
    }

    if (!text) return info && info->TextInfo.lang ? Text$from_str(info->TextInfo.lang) : Text("Text");
    char quote_char;
    if (info == &Pattern$info) {
        quote_char = Text$has(*(Text_t*)text, Pattern("/")) && !Text$has(*(Text_t*)text, Pattern("|")) ? '|' : '/';
    } else {
        // Figure out the best quotation mark to use:
        bool has_dollar = false, has_double_quote = false, has_backtick = false,
             has_single_quote = false, needs_escapes = false;
        TextIter_t state = {*(Text_t*)text, 0, 0};
        for (int64_t i = 0; i < state.text.length; i++) {
            int32_t g = Text$get_grapheme_fast(&state, i);
            if (g == '$') {
                has_dollar = true;
            } else if (g == '"') {
                has_double_quote = true;
            } else if (g == '`') {
                has_backtick = true;
            } else if (g == (g & 0x7F) && (g == '\'' || g == '\n' || g == '\r' || g == '\t' || !isprint((char)g))) {
                needs_escapes = true;
            }
        }

        // If there's dollar signs and/or double quotes in the string, it would
        // be nice to avoid needing to escape them by using single quotes, but
        // only if we don't have single quotes or need to escape anything else
        // (because single quotes don't have interpolation):
        if ((has_dollar || has_double_quote) && !has_single_quote && !needs_escapes)
            quote_char = '\'';
        // If there is a double quote, but no backtick, we can save a bit of
        // escaping by using backtick instead of double quote:
        else if (has_double_quote && !has_backtick)
            quote_char = '`';
        // Otherwise fall back to double quotes as the default quoting style:
        else
            quote_char = '"';
    }

    Text_t as_text = _quoted(*(Text_t*)text, colorize, quote_char);
    if (info && info->TextInfo.lang && info != &Text$info && info != &Pattern$info)
        as_text = Text$concat(
            colorize ? Text("\x1b[1m$") : Text("$"),
            Text$from_str(info->TextInfo.lang),
            colorize ? Text("\x1b[0m") : Text(""),
            as_text);
    return as_text;
}

public Text_t Text$quoted(Text_t text, bool colorize)
{
    return _quoted(text, colorize, '"');
}

public Text_t Text$join(Text_t glue, Array_t pieces)
{
    if (pieces.length == 0) return (Text_t){.length=0};

    Text_t result = *(Text_t*)pieces.data;
    for (int64_t i = 1; i < pieces.length; i++) {
        result = Text$concat(result, glue, *(Text_t*)(pieces.data + i*pieces.stride));
    }
    return result;
}

__attribute__((format(printf, 1, 2)))
public Text_t Text$format(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);

    char buf[9];
    int len = vsnprintf(buf, sizeof(buf), fmt, args);
    Text_t ret;
    if (len <= 8) {
        ret = (Text_t){
            .length=len,
            .tag=TEXT_SHORT_ASCII,
        };
        for (int i = 0; i < len; i++)
            ret.short_ascii[i] = buf[i];
    } else {
        char *str = GC_MALLOC_ATOMIC((size_t)(len+1));
        vsnprintf(str, (size_t)(len+1), fmt, args);
        ret = Text$from_str(str);
    }
    va_end(args);
    return ret;
}

public Array_t Text$clusters(Text_t text)
{
    Array_t clusters = {.atomic=1};
    for (int64_t i = 1; i <= text.length; i++) {
        Text_t cluster = Text$slice(text, I(i), I(i));
        Array$insert(&clusters, &cluster, I_small(0), sizeof(Text_t));
    }
    return clusters;
}

public Array_t Text$utf32_codepoints(Text_t text)
{
    Array_t codepoints = {.atomic=1};
    TextIter_t state = {text, 0, 0};
    for (int64_t i = 0; i < text.length; i++) {
        int32_t grapheme = Text$get_grapheme_fast(&state, i);
        if (grapheme < 0) {
            for (int64_t c = 0; c < NUM_GRAPHEME_CODEPOINTS(grapheme); c++) {
                ucs4_t subg = GRAPHEME_CODEPOINTS(grapheme)[c];
                Array$insert(&codepoints, &subg, I_small(0), sizeof(ucs4_t));
            }
        } else {
            Array$insert(&codepoints, &grapheme, I_small(0), sizeof(ucs4_t));
        }
    }
    return codepoints;
}

public Array_t Text$utf8_bytes(Text_t text)
{
    const char *str = Text$as_c_string(text);
    return (Array_t){.length=strlen(str), .stride=1, .atomic=1, .data=(void*)str};
}

static inline const char *codepoint_name(ucs4_t c)
{
    char *name = GC_MALLOC_ATOMIC(UNINAME_MAX);
    char *found_name = unicode_character_name(c, name);
    if (found_name) return found_name;
    const uc_block_t *block = uc_block(c);
    assert(block);
    snprintf(name, UNINAME_MAX, "%s-%X", block->name, c);
    return name;
}

public Array_t Text$codepoint_names(Text_t text)
{
    Array_t names = {};
    TextIter_t state = {text, 0, 0};
    for (int64_t i = 0; i < text.length; i++) {
        int32_t grapheme = Text$get_grapheme_fast(&state, i);
        if (grapheme < 0) {
            for (int64_t c = 0; c < NUM_GRAPHEME_CODEPOINTS(grapheme); c++) {
                const char *name = codepoint_name(GRAPHEME_CODEPOINTS(grapheme)[c]);
                Text_t name_text = (Text_t){.tag=TEXT_ASCII, .length=(int64_t)strlen(name), .ascii=name};
                Array$insert(&names, &name_text, I_small(0), sizeof(Text_t));
            }
        } else {
            const char *name = codepoint_name((ucs4_t)grapheme);
            Text_t name_text = (Text_t){.tag=TEXT_ASCII, .length=(int64_t)strlen(name), .ascii=name};
            Array$insert(&names, &name_text, I_small(0), sizeof(Text_t));
        }
    }
    return names;
}

public Text_t Text$from_codepoints(Array_t codepoints)
{
    if (codepoints.stride != sizeof(int32_t))
        Array$compact(&codepoints, sizeof(int32_t));

    return text_from_u32(codepoints.data, codepoints.length, true);
}

public Text_t Text$from_codepoint_names(Array_t codepoint_names)
{
    Array_t codepoints = {};
    for (int64_t i = 0; i < codepoint_names.length; i++) {
        Text_t *name = ((Text_t*)(codepoint_names.data + i*codepoint_names.stride));
        const char *name_str = Text$as_c_string(*name);
        ucs4_t codepoint = unicode_name_character(name_str);
        if (codepoint != UNINAME_INVALID)
            Array$insert(&codepoints, &codepoint, I_small(0), sizeof(ucs4_t));
    }
    return Text$from_codepoints(codepoints);
}

public Text_t Text$from_bytes(Array_t bytes)
{
    if (bytes.stride != sizeof(int8_t))
        Array$compact(&bytes, sizeof(int8_t));

    int8_t nul = 0;
    Array$insert(&bytes, &nul, I_small(0), sizeof(int8_t));
    return Text$from_str(bytes.data);
}

public Array_t Text$lines(Text_t text)
{
    Array_t lines = {};
    TextIter_t state = {text, 0, 0};
    for (int64_t i = 0, line_start = 0; i < text.length; i++) {
        int32_t grapheme = Text$get_grapheme_fast(&state, i);
        if (grapheme == '\r' && Text$get_grapheme_fast(&state, i + 1) == '\n') { // CRLF
            Text_t line = Text$slice(text, I(line_start+1), I(i));
            Array$insert(&lines, &line, I_small(0), sizeof(Text_t));
            i += 1; // skip one extra for CR
            line_start = i + 1;
        } else if (grapheme == '\n') { // newline
            Text_t line = Text$slice(text, I(line_start+1), I(i));
            Array$insert(&lines, &line, I_small(0), sizeof(Text_t));
            line_start = i + 1;
        } else if (i == text.length-1 && line_start != i) { // last line
            Text_t line = Text$slice(text, I(line_start+1), I(i+1));
            Array$insert(&lines, &line, I_small(0), sizeof(Text_t));
        }
    }
    return lines;
}

public const TypeInfo Text$info = {
    .size=sizeof(Text_t),
    .align=__alignof__(Text_t),
    .tag=TextInfo,
    .TextInfo={.lang="Text"},
};

public Pattern_t Pattern$escape_text(Text_t text)
{
    // TODO: optimize for ASCII and short strings
    Array_t graphemes = {.atomic=1};
#define add_char(c) Array$insert_value(&graphemes, (ucs4_t)c, I_small(0), sizeof(ucs4_t))
#define add_str(s) ({ for (const char *_c = s; *_c; ++_c) Array$insert_value(&graphemes, (ucs4_t)*_c, I_small(0), sizeof(ucs4_t)); })
    TextIter_t state = {text, 0, 0};
    for (int64_t i = 0; i < text.length; i++) {
        int32_t g = Text$get_grapheme_fast(&state, i);
        ucs4_t g0 = g < 0 ? GRAPHEME_CODEPOINTS(g)[0] : (ucs4_t)g;

        if (g == '{') {
            add_str("{1{}");
        } else if (g0 == '?'
                   || uc_is_property_quotation_mark(g0)
                   || (uc_is_property_paired_punctuation(g0) && uc_is_property_left_of_pair(g0))) {
            add_char('{');
            add_char('1');
            add_char(g);
            add_char('}');
        } else {
            add_char(g);
        }
    }
    return (Text_t){.length=graphemes.length, .tag=TEXT_GRAPHEMES, .graphemes=graphemes.data};
#undef add_str
#undef add_char
#undef add_escaped
}

public const TypeInfo Pattern$info = {
    .size=sizeof(Pattern_t),
    .align=__alignof__(Pattern_t),
    .tag=TextInfo,
    .TextInfo={.lang="Pattern"},
};

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
