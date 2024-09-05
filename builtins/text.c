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
//       (uint32_t[]){0x65, 0x0309}

#include <assert.h>
#include <ctype.h>
#include <err.h>
#include <gc.h>
#include <limits.h>
#include <printf.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/param.h>

#include <unicase.h>
#include <unictype.h>
#include <unigbrk.h>
#include <uniname.h>
#include <uninorm.h>
#include <unistd.h>
#include <unistdio.h>
#include <unistr.h>

#include "array.h"
#include "functions.h"
#include "integers.h"
#include "table.h"
#include "text.h"
#include "types.h"

// Use inline version of the siphash code for performance:
#include "siphash.h"
#include "siphash-internals.h"

typedef struct {
    uint32_t main_codepoint;
    uint32_t *utf32_cluster; // length-prefixed
    const uint8_t *utf8;
} synthetic_grapheme_t;

typedef struct {
    int64_t subtext, sum_of_previous_subtexts;
} iteration_state_t;

#define MAX_BACKREFS 100

// Synthetic grapheme clusters (clusters of more than one codepoint):
static Table_t grapheme_ids_by_codepoints = {}; // uint32_t* length-prefixed codepoints -> int32_t ID

// This will hold a dynamically growing array of synthetic graphemes:
static synthetic_grapheme_t *synthetic_graphemes = NULL;
static int32_t synthetic_grapheme_capacity = 0;
static int32_t num_synthetic_graphemes = 0;

#define MAIN_GRAPHEME_CODEPOINT(g) ((g) >= 0 ? (uint32_t)(g) : synthetic_graphemes[-(g)-1].main_codepoint)
#define NUM_GRAPHEME_CODEPOINTS(id) (synthetic_graphemes[-(id)-1].utf32_cluster[0])
#define GRAPHEME_CODEPOINTS(id) (&synthetic_graphemes[-(id)-1].utf32_cluster[1])
#define GRAPHEME_UTF8(id) (synthetic_graphemes[-(id)-1].utf8)

static int32_t get_grapheme(Text_t text, int64_t index);
static int32_t _next_grapheme(Text_t text, iteration_state_t *state, int64_t index);
static Text_t text_from_u32(uint32_t *codepoints, int64_t num_codepoints, bool normalize);

static bool graphemes_equal(uint32_t **a, uint32_t **b) {
    if ((*a)[0] != (*b)[0]) return false;
    for (int i = 0; i < (int)(*a)[0]; i++)
        if ((*a)[i] != (*b)[i]) return false;
    return true;
}

static uint64_t grapheme_hash(uint32_t **g) {
    uint32_t *cluster = *g;
    return siphash24((void*)&cluster[1], sizeof(uint32_t[cluster[0]]));
}

static const TypeInfo GraphemeClusterInfo = {
    .size=sizeof(uint32_t*),
    .align=__alignof__(uint32_t*),
    .tag=CustomInfo,
    .CustomInfo={.equal=(void*)graphemes_equal, .hash=(void*)grapheme_hash},
};

static const TypeInfo GraphemeIDLookupTableInfo = {
    .size=sizeof(Table_t), .align=__alignof__(Table_t),
    .tag=TableInfo, .TableInfo={.key=&GraphemeClusterInfo, .value=&Int32$info},
};

int32_t get_synthetic_grapheme(const uint32_t *codepoints, int64_t utf32_len)
{
    uint32_t length_prefixed[1+utf32_len] = {};
    length_prefixed[0] = (uint32_t)utf32_len;
    for (int i = 0; i < utf32_len; i++)
        length_prefixed[i+1] = codepoints[i];
    uint32_t *ptr = &length_prefixed[0];

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
    uint8_t *u8 = u32_to_u8(codepoints, utf32_len, u8_buf, &u8_len);

    // For performance reasons, use an arena allocator here to ensure that
    // synthetic graphemes store all of their information in a densely packed
    // area with good cache locality:
    static void *arena = NULL, *arena_end = NULL;
    // Eat up any space needed to make arena 32-bit aligned:
    if ((long)arena % __alignof__(uint32_t) != 0)
        arena += __alignof__(uint32_t) - ((long)arena % __alignof__(uint32_t));

    // If we have filled up this arena, allocate a new one:
    size_t needed_memory = sizeof(uint32_t[1+utf32_len]) + sizeof(uint8_t[u8_len + 1]);
    if (arena + needed_memory > arena_end) {
        // Do reasonably big chunks at a time, so most synthetic codepoints are
        // nearby each other in memory and cache locality is good. This is a
        // rough guess at a good size:
        size_t chunk_size = MAX(needed_memory, 512);
        arena = GC_MALLOC_ATOMIC(chunk_size);
        arena_end = arena + chunk_size;
    }

    // Copy length-prefixed UTF32 codepoints into the arena and store where they live:
    uint32_t *codepoint_copy = arena;
    mempcpy(codepoint_copy, length_prefixed, sizeof(uint32_t[1+utf32_len]));
    synthetic_graphemes[-grapheme_id-1].utf32_cluster = codepoint_copy;
    arena += sizeof(uint32_t[1+utf32_len]);

    // Copy UTF8 bytes into the arena and store where they live:
    uint8_t *utf8_final = arena;
    memcpy(utf8_final, u8, sizeof(uint8_t[u8_len]));
    utf8_final[u8_len] = '\0'; // Add a terminating NUL byte
    synthetic_graphemes[-grapheme_id-1].utf8 = utf8_final;
    arena += sizeof(uint8_t[u8_len + 1]);

    // Sickos at the unicode consortium decreed that you can have grapheme clusters
    // that begin with *prefix* modifiers, so we gotta check for that case:
    synthetic_graphemes[-grapheme_id-1].main_codepoint = length_prefixed[1];
    for (uint32_t i = 0; i < utf32_len; i++) {
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

static inline int64_t num_subtexts(Text_t t)
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
    case TEXT_SHORT_ASCII: return fwrite(t.short_ascii, sizeof(char), t.length, stream);
    case TEXT_ASCII: return fwrite(t.ascii, sizeof(char), t.length, stream);
    case TEXT_GRAPHEMES: case TEXT_SHORT_GRAPHEMES: {
        const int32_t *graphemes = t.tag == TEXT_SHORT_GRAPHEMES ? t.short_graphemes : t.graphemes;
        int written = 0;
        for (int64_t i = 0; i < t.length; i++) {
            int32_t grapheme = graphemes[i];
            if (grapheme >= 0) {
                uint8_t buf[8];
                size_t len = sizeof(buf);
                uint8_t *u8 = u32_to_u8((uint32_t*)&grapheme, 1, buf, &len);
                written += fwrite(u8, sizeof(char), len, stream);
                if (u8 != buf) free(u8);
            } else {
                const uint8_t *u8 = GRAPHEME_UTF8(grapheme);
                assert(u8);
                written += fwrite(u8, sizeof(uint8_t), strlen((char*)u8), stream);
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

    int32_t last_a = get_grapheme(a, a.length-1);
    int32_t first_b = get_grapheme(b, 0);

    // Synthetic graphemes are weird and probably need to check with normalization:
    if (last_a < 0 || first_b < 0)
        return 0;

    // Magic number, we know that no codepoints below here trigger instability:
    static const int32_t LOWEST_CODEPOINT_TO_CHECK = 0x300;
    if (last_a < LOWEST_CODEPOINT_TO_CHECK && first_b < LOWEST_CODEPOINT_TO_CHECK)
        return true;

    // Do a normalization run for these two codepoints and see if it looks different:
    uint32_t codepoints[2] = {(uint32_t)last_a, (uint32_t)first_b};
    uint32_t norm_buf[3*2]; // Normalization should not exceed 3x in the input length
    size_t norm_length = sizeof(norm_buf)/sizeof(norm_buf[0]);
    uint32_t *normalized = u32_normalize(UNINORM_NFC, codepoints, 2, norm_buf, &norm_length);
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
    int32_t last_a = get_grapheme(a, a.length-1);
    int32_t first_b = get_grapheme(b, 0);

    size_t utf32_len = (last_a >= 0 ? 1 : NUM_GRAPHEME_CODEPOINTS(last_a)) + (first_b >= 0 ? 1 : NUM_GRAPHEME_CODEPOINTS(first_b)); 
    uint32_t join_graphemes[utf32_len] = {};
    uint32_t *p = &join_graphemes[0];
    if (last_a < 0) p = mempcpy(p, GRAPHEME_CODEPOINTS(last_a), NUM_GRAPHEME_CODEPOINTS(last_a));
    else *(p++) = last_a;
    if (first_b < 0) p = mempcpy(p, GRAPHEME_CODEPOINTS(first_b), NUM_GRAPHEME_CODEPOINTS(first_b));
    else *(p++) = first_b;

    Text_t glue = text_from_u32(join_graphemes, utf32_len, true);

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
        memcpy(ret.short_ascii, text.short_ascii + (first-1), ret.length);
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

Text_t text_from_u32(uint32_t *codepoints, int64_t num_codepoints, bool normalize)
{
    // Normalization is apparently guaranteed to never exceed 3x in the input length
    uint32_t norm_buf[MIN(256, 3*num_codepoints)];
    if (normalize) {
        size_t norm_length = sizeof(norm_buf)/sizeof(norm_buf[0]);
        uint32_t *normalized = u32_normalize(UNINORM_NFC, codepoints, num_codepoints, norm_buf, &norm_length);
        codepoints = normalized;
        num_codepoints = norm_length;
    }

    // char breaks[num_codepoints];
    // u32_grapheme_breaks(codepoints, num_codepoints, breaks);

    Text_t ret = {
        .length=0,
        .tag=TEXT_SHORT_GRAPHEMES,
    };
    const uint32_t *src = codepoints;
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
        const uint32_t *next = u32_grapheme_next(src, &codepoints[num_codepoints]);
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
        uint32_t buf[128];
        size_t length = sizeof(buf)/sizeof(buf[0]);
        uint32_t *codepoints = u8_to_u32((uint8_t*)str, ascii_span + strlen(str + ascii_span), buf, &length);
        Text_t ret = text_from_u32(codepoints, length, true);
        if (codepoints != buf) free(codepoints);
        return ret;
    }
}

public Text_t Text$from_str(const char *str)
{
    return Text$from_strn(str, strlen(str));
}

static void u8_buf_append(Text_t text, char **buf, int64_t *capacity, int64_t *i)
{
    switch (text.tag) {
    case TEXT_ASCII: case TEXT_SHORT_ASCII: {
        if (*i + text.length > (int64_t)*capacity) {
            *capacity = *i + text.length + 1;
            *buf = GC_REALLOC(*buf, *capacity);
        }

        const char *bytes = text.tag == TEXT_ASCII ? text.ascii : text.short_ascii;
        memcpy(*buf + *i, bytes, text.length);
        *i += text.length;
        break;
    }
    case TEXT_GRAPHEMES: case TEXT_SHORT_GRAPHEMES: {
        const int32_t *graphemes = text.tag == TEXT_GRAPHEMES ? text.graphemes : text.short_graphemes;
        for (int64_t g = 0; g < text.length; g++) {
            if (graphemes[g] >= 0) {
                uint8_t u8_buf[64];
                size_t u8_len = sizeof(u8_buf);
                uint8_t *u8 = u32_to_u8((uint32_t*)&graphemes[g], 1, u8_buf, &u8_len);

                if (*i + (int64_t)u8_len > (int64_t)*capacity) {
                    *capacity = *i + u8_len + 1;
                    *buf = GC_REALLOC(*buf, *capacity);
                }

                memcpy(*buf + *i, u8, u8_len);
                *i += u8_len;
                if (u8 != u8_buf) free(u8);
            } else {
                const uint8_t *u8 = GRAPHEME_UTF8(graphemes[g]);
                size_t u8_len = u8_strlen(u8);
                if (*i + (int64_t)u8_len > (int64_t)*capacity) {
                    *capacity = *i + u8_len + 1;
                    *buf = GC_REALLOC(*buf, *capacity);
                }

                memcpy(*buf + *i, u8, u8_len);
                *i += u8_len;
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

public const char *Text$as_c_string(Text_t text)
{
    int64_t capacity = text.length + 1;
    char *buf = GC_MALLOC_ATOMIC(capacity);
    int64_t i = 0;
    u8_buf_append(text, &buf, &capacity, &i);

    if (i + 1 > (int64_t)capacity) {
        capacity = i + 1;
        buf = GC_REALLOC(buf, capacity);
    }
    buf[i] = '\0';
    return buf;
}

public uint64_t Text$hash(Text_t *text)
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

        text->hash = siphashfinish_last_part(&sh, leftover);
        break;
    }
    default: errx(1, "Invalid text");
    }

    if (text->hash == 0)
        text->hash = 1;

    return text->hash;
}

int32_t _next_grapheme(Text_t text, iteration_state_t *state, int64_t index)
{
    switch (text.tag) {
    case TEXT_ASCII: return index < text.length ? (int32_t)text.ascii[index] : 0;
    case TEXT_SHORT_ASCII: return index < text.length ? (int32_t)text.short_ascii[index] : 0;
    case TEXT_GRAPHEMES: return index < text.length ? text.graphemes[index] : 0;
    case TEXT_SHORT_GRAPHEMES: return index < text.length ? text.short_graphemes[index] : 0;
    case TEXT_SUBTEXT: {
        iteration_state_t backup_state = {0, 0};
        if (!state) state = &backup_state;

        if (index < 0 || index >= text.length)
            return 0;

        while (index < state->sum_of_previous_subtexts && state->subtext > 0) {
            state->sum_of_previous_subtexts -= text.subtexts[state->subtext].length;
            state->subtext -= 1;
        }
        for (;;) {
            if (index < state->sum_of_previous_subtexts + text.subtexts[state->subtext].length)
                return _next_grapheme(text.subtexts[state->subtext], NULL, index - state->sum_of_previous_subtexts);
            state->sum_of_previous_subtexts += text.subtexts[state->subtext].length;
            state->subtext += 1;
        }
        return 0;
    }
    default: errx(1, "Invalid text");
    }
    return 0;
}

int32_t get_grapheme(Text_t text, int64_t index)
{
    iteration_state_t state = {0, 0};
    return _next_grapheme(text, &state, index);
}

public int32_t Text$compare(const Text_t *a, const Text_t *b)
{
    if (a == b) return 0;

    int64_t len = MAX(a->length, b->length);
    iteration_state_t a_state = {0, 0}, b_state = {0, 0};
    for (int64_t i = 0; i < len; i++) {
        int32_t ai = _next_grapheme(*a, &a_state, i);
        int32_t bi = _next_grapheme(*b, &b_state, i);
        if (ai == bi) continue;
        int32_t cmp;
        if (ai > 0 && bi > 0) {
            cmp = u32_cmp((uint32_t*)&ai, (uint32_t*)&bi, 1);
        } else if (ai > 0) {
            cmp = u32_cmp2(
                (uint32_t*)&ai, 1,
                GRAPHEME_CODEPOINTS(bi),
                NUM_GRAPHEME_CODEPOINTS(bi));
        } else if (bi > 0) {
            cmp = u32_cmp2(
                GRAPHEME_CODEPOINTS(ai),
                NUM_GRAPHEME_CODEPOINTS(ai),
                (uint32_t*)&bi, 1);
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

public bool Text$equal(const Text_t *a, const Text_t *b)
{
    if (a == b) return true;

    if (a->length != b->length || (a->hash != 0 && b->hash != 0 && a->hash != b->hash))
        return false;
    int64_t len = a->length;
    iteration_state_t a_state = {0, 0}, b_state = {0, 0};
    for (int64_t i = 0; i < len; i++) {
        int32_t ai = _next_grapheme(*a, &a_state, i);
        int32_t bi = _next_grapheme(*b, &b_state, i);
        if (ai != bi) return false;
    }
    return true;
}

public bool Text$equal_ignoring_case(Text_t a, Text_t b)
{
    if (a.length != b.length)
        return false;
    int64_t len = a.length;
    iteration_state_t a_state = {0, 0}, b_state = {0, 0};
    const char *language = uc_locale_language();
    for (int64_t i = 0; i < len; i++) {
        int32_t ai = _next_grapheme(a, &a_state, i);
        int32_t bi = _next_grapheme(b, &b_state, i);
        if (ai != bi) {
            const uint32_t *a_codepoints = ai >= 0 ? (uint32_t*)&ai : GRAPHEME_CODEPOINTS(ai);
            int64_t a_len = ai >= 0 ? 1 : NUM_GRAPHEME_CODEPOINTS(ai);

            const uint32_t *b_codepoints = bi >= 0 ? (uint32_t*)&bi : GRAPHEME_CODEPOINTS(bi);
            int64_t b_len = bi >= 0 ? 1 : NUM_GRAPHEME_CODEPOINTS(bi);

            int cmp;
            (void)u32_casecmp(a_codepoints, a_len, b_codepoints, b_len, language, UNINORM_NFC, &cmp);
            if (cmp != 0)
                return false;
        }
    }
    return true;
}

public Text_t Text$upper(Text_t text)
{
    Array_t codepoints = Text$utf32_codepoints(text);
    const char *language = uc_locale_language();
    uint32_t buf[128]; 
    size_t out_len;
    uint32_t *upper = u32_toupper(codepoints.data, codepoints.length, language, UNINORM_NFC, buf, &out_len);
    Text_t ret = text_from_u32(upper, out_len, false);
    if (upper != buf) free(upper);
    return ret;
}

public Text_t Text$lower(Text_t text)
{
    Array_t codepoints = Text$utf32_codepoints(text);
    const char *language = uc_locale_language();
    uint32_t buf[128]; 
    size_t out_len;
    uint32_t *lower = u32_tolower(codepoints.data, codepoints.length, language, UNINORM_NFC, buf, &out_len);
    Text_t ret = text_from_u32(lower, out_len, false);
    if (lower != buf) free(lower);
    return ret;
}

public Text_t Text$title(Text_t text)
{
    Array_t codepoints = Text$utf32_codepoints(text);
    const char *language = uc_locale_language();
    uint32_t buf[128]; 
    size_t out_len;
    uint32_t *title = u32_totitle(codepoints.data, codepoints.length, language, UNINORM_NFC, buf, &out_len);
    Text_t ret = text_from_u32(title, out_len, false);
    if (title != buf) free(title);
    return ret;
}

static inline void skip_whitespace(Text_t text, int64_t *i)
{
    iteration_state_t state = {0, 0};
    while (*i < text.length) {
        int32_t grapheme = _next_grapheme(text, &state, *i);
        if (grapheme > 0 && !uc_is_property_white_space(grapheme))
            return;
        *i += 1;
    }
}

static inline bool match_grapheme(Text_t text, int64_t *i, int32_t grapheme)
{
    if (*i < text.length && get_grapheme(text, *i) == grapheme) {
        *i += 1;
        return true;
    }
    return false;
}

static inline bool match_str(Text_t text, int64_t *i, const char *str)
{
    iteration_state_t state = {0, 0};
    int64_t matched = 0;
    while (matched[str]) {
        if (*i + matched >= text.length || _next_grapheme(text, &state, *i + matched) != str[matched])
            return false;
        matched += 1;
    }
    *i += matched;
    return true;
}

static inline bool match_property(Text_t text, int64_t *i, uc_property_t prop)
{
    if (*i >= text.length) return false;
    int32_t grapheme = get_grapheme(text, *i);
    // TODO: check every codepoint in the cluster?
    grapheme = MAIN_GRAPHEME_CODEPOINT(grapheme);

    if (uc_is_property(grapheme, prop)) {
        *i += 1;
        return true;
    }
    return false;
}

static int64_t parse_int(Text_t text, int64_t *i)
{
    iteration_state_t state = {0, 0};
    int64_t value = 0;
    for (;; *i += 1) {
        int32_t grapheme = _next_grapheme(text, &state, *i);
        grapheme = MAIN_GRAPHEME_CODEPOINT(grapheme);
        int digit = uc_digit_value(grapheme);
        if (digit < 0) break;
        if (value >= INT64_MAX/10) break;
        value = 10*value + digit;
    }
    return value;
}

const char *get_property_name(Text_t text, int64_t *i)
{
    skip_whitespace(text, i);
    char *name = GC_MALLOC_ATOMIC(UNINAME_MAX);
    char *dest = name;
    iteration_state_t state = {0, 0};
    while (*i < text.length) {
        int32_t grapheme = _next_grapheme(text, &state, *i);
        if (!(grapheme & ~0xFF) && (isalnum(grapheme) || grapheme == ' ' || grapheme == '_' || grapheme == '-')) {
            *dest = (char)grapheme;
            ++dest;
            if (dest >= name + UNINAME_MAX - 1)
                break;
        } else {
            break;
        }
        *i += 1;
    }

    while (dest > name && dest[-1] == ' ')
        *(dest--) = '\0';

    if (dest == name) return NULL;
    *dest = '\0';
    return name;
}

#define EAT1(text, state, index, cond) ({\
        int32_t grapheme = _next_grapheme(text, state, index); \
        bool success = (cond); \
        if (success) index += 1; \
        success; })

#define EAT2(text, state, index, cond1, cond2) ({\
        int32_t grapheme = _next_grapheme(text, state, index); \
        bool success = (cond1); \
        if (success) { \
            grapheme = _next_grapheme(text, state, index + 1); \
            success = (cond2); \
            if (success) \
                index += 2; \
        } \
        success; })


#define EAT_MANY(text, state, index, cond) ({ int64_t _n = 0; while (EAT1(text, state, index, cond)) { _n += 1; } _n; })

int64_t match_email(Text_t text, int64_t text_index)
{
    // email = local "@" domain
    // local = 1-64 ([a-zA-Z0-9!#$%&‘*+–/=?^_`.{|}~] | non-ascii)
    // domain = dns-label ("." dns-label)*
    // dns-label = 1-63 ([a-zA-Z0-9-] | non-ascii)

    iteration_state_t state = {0, 0};
    if (text_index > 0) {
        int32_t prev_codepoint = _next_grapheme(text, &state, text_index - 1);
        prev_codepoint = MAIN_GRAPHEME_CODEPOINT(prev_codepoint);
        if (uc_is_property_alphabetic(prev_codepoint))
            return -1;
    }

    int64_t start_index = text_index;

    // Local part:
    int64_t local_len = 0;
    static const char *allowed_local = "!#$%&‘*+–/=?^_`.{|}~";
    while (EAT1(text, &state, text_index,
                (grapheme & ~0x7F) || isalnum((char)grapheme) || strchr(allowed_local, (char)grapheme))) {
        local_len += 1;
        if (local_len > 64) return -1;
    }
    
    if (!EAT1(text, &state, text_index, grapheme == '@'))
        return -1;

    // Host
    int64_t host_len = 0;
    do {
        int64_t label_len = 0;
        while (EAT1(text, &state, text_index,
                    (grapheme & ~0x7F) || isalnum((char)grapheme) || grapheme == '-')) {
            label_len += 1;
            if (label_len > 63) return -1;
        }

        if (label_len == 0)
            return -1;

        host_len += label_len;
        if (host_len > 255)
            return -1;
        host_len += 1;
    } while (EAT1(text, &state, text_index, grapheme == '.'));

    return text_index - start_index;
}

int64_t match_ipv6(Text_t text, int64_t text_index)
{
    iteration_state_t state = {0, 0};
    if (text_index > 0) {
        int32_t prev_codepoint = _next_grapheme(text, &state, text_index - 1);
        if ((prev_codepoint & ~0x7F) && (isxdigit(prev_codepoint) || prev_codepoint == ':'))
            return -1;
    }
    int64_t start_index = text_index;
    const int NUM_CLUSTERS = 8;
    bool double_colon_used = false;
    for (int cluster = 0; cluster < NUM_CLUSTERS; cluster++) {
        for (int digits = 0; digits < 4; digits++) {
            if (!EAT1(text, &state, text_index, ~(grapheme & ~0x7F) && isxdigit((char)grapheme)))
                break;
        }
        if (EAT1(text, &state, text_index, ~(grapheme & ~0x7F) && isxdigit((char)grapheme)))
            return -1; // Too many digits

        if (cluster == NUM_CLUSTERS-1) {
            break;
        } else if (!EAT1(text, &state, text_index, grapheme == ':')) {
            if (double_colon_used)
                break;
            return -1;
        }

        if (EAT1(text, &state, text_index, grapheme == ':')) {
            if (double_colon_used)
                return -1;
            double_colon_used = true;
        }
    }
    return text_index - start_index;
}

static int64_t match_ipv4(Text_t text, int64_t text_index)
{
    iteration_state_t state = {0, 0};
    if (text_index > 0) {
        int32_t prev_codepoint = _next_grapheme(text, &state, text_index - 1);
        if ((prev_codepoint & ~0x7F) && (isdigit(prev_codepoint) || prev_codepoint == '.'))
            return -1;
    }
    int64_t start_index = text_index;

    const int NUM_CLUSTERS = 4;
    for (int cluster = 0; cluster < NUM_CLUSTERS; cluster++) {
        for (int digits = 0; digits < 3; digits++) {
            if (!EAT1(text, &state, text_index, ~(grapheme & ~0x7F) && isdigit((char)grapheme))) {
                if (digits == 0) return -1;
                break;
            }
        }

        if (EAT1(text, &state, text_index, ~(grapheme & ~0x7F) && isdigit((char)grapheme)))
            return -1; // Too many digits

        if (cluster == NUM_CLUSTERS-1)
            break;
        else if (!EAT1(text, &state, text_index, grapheme == '.'))
            return -1;
    }
    return (text_index - start_index);
}

int64_t match_uri(Text_t text, int64_t text_index)
{
    // URI = scheme ":" ["//" authority] path ["?" query] ["#" fragment]
    // scheme = [a-zA-Z] [a-zA-Z0-9+.-]
    // authority = [userinfo "@"] host [":" port]

    iteration_state_t state = {0, 0};
    if (text_index > 0) {
        int32_t prev_codepoint = _next_grapheme(text, &state, text_index - 1);
        prev_codepoint = MAIN_GRAPHEME_CODEPOINT(prev_codepoint);
        if (uc_is_property_alphabetic(prev_codepoint))
            return -1;
    }

    int64_t start_index = text_index;

    // Scheme:
    if (!EAT1(text, &state, text_index, isalpha(grapheme)))
        return -1;

    EAT_MANY(text, &state, text_index,
             !(grapheme & ~0x7F) && (isalnum(grapheme) || grapheme == '+' || grapheme == '.' || grapheme == '-'));

    if (text_index == start_index)
        return -1;

    if (!match_grapheme(text, &text_index, ':'))
        return -1;

    // Authority:
    if (match_str(text, &text_index, "//")) {
        int64_t authority_start = text_index;
        // Username or host:
        static const char *forbidden = "#?:@ \t\r\n<>[]{}\\^|\"`/";
        if (EAT_MANY(text, &state, text_index, (grapheme & ~0x7F) || !strchr(forbidden, (char)grapheme)) == 0)
            return -1;

        if (EAT1(text, &state, text_index, grapheme == '@')) {
            // Found a username, now get a host:
            if (EAT_MANY(text, &state, text_index, (grapheme & ~0x7F) || !strchr(forbidden, (char)grapheme)) == 0)
                return -1;
        } else {
            int64_t ip = authority_start;
            int64_t ipv4_len = match_ipv4(text, ip);
            if (ipv4_len > 0) {
                ip += ipv4_len;
            } else if (match_grapheme(text, &ip, '[')) {
                ip += match_ipv6(text, ip);
                if (ip > authority_start + 1 && match_grapheme(text, &ip, ']'))
                    text_index = ip;
            }
        }

        // Port:
        if (EAT1(text, &state, text_index, grapheme == ':')) {
            if (EAT_MANY(text, &state, text_index, !(grapheme & ~0x7F) && isdigit(grapheme)) == 0)
                return -1;
        }
        if (!EAT1(text, &state, text_index, grapheme == '/'))
            return (text_index - start_index); // No path
    } else {
        // Optional path root:
        EAT1(text, &state, text_index, grapheme == '/');
    }

    // Path:
    static const char *non_path = " \"#?<>[]{}\\^`|";
    EAT_MANY(text, &state, text_index, (grapheme & ~0x7F) || !strchr(non_path, (char)grapheme));

    if (EAT1(text, &state, text_index, grapheme == '?')) { // Query
        static const char *non_query = " \"#<>[]{}\\^`|";
        EAT_MANY(text, &state, text_index, (grapheme & ~0x7F) || !strchr(non_query, (char)grapheme));
    }
    
    if (EAT1(text, &state, text_index, grapheme == '#')) { // Fragment
        static const char *non_fragment = " \"#<>[]{}\\^`|";
        EAT_MANY(text, &state, text_index, (grapheme & ~0x7F) || !strchr(non_fragment, (char)grapheme));
    }
    return text_index - start_index;
}

typedef struct {
    int64_t index, length;
    bool occupied, recursive;
} capture_t;

int64_t match(Text_t text, Pattern_t pattern, int64_t text_index, int64_t pattern_index, capture_t *captures, int64_t capture_index)
{
    if (pattern_index >= pattern.length) return 0;
    int64_t start_index = text_index;
    iteration_state_t pattern_state = {0, 0}, text_state = {0, 0};
    while (pattern_index < pattern.length) {
        int64_t old_pat_index = pattern_index;
        if (EAT2(pattern, &pattern_state, pattern_index,
                 uc_is_property(grapheme, UC_PROPERTY_QUOTATION_MARK),
                 grapheme == '?')) {
            // Quotations: "?", '?', etc
            int32_t open = _next_grapheme(pattern, &pattern_state, pattern_index-2);
            if (!match_grapheme(text, &text_index, open)) return -1;
            int64_t start_of_quoted_text = text_index;
            int32_t close = open;
            uc_mirror_char(open, (uint32_t*)&close);
            if (!match_grapheme(pattern, &pattern_index, close))
                fail("Pattern's closing brace is missing: %k", &pattern);
            while (text_index < text.length) {
                int32_t c = _next_grapheme(text, &text_state, text_index);
                if (c == close) {
                    // Save this as a capture, including only the interior text:
                    if (captures && capture_index < MAX_BACKREFS) {
                        captures[capture_index++] = (capture_t){
                            .index=start_of_quoted_text,
                            .length=text_index - start_of_quoted_text,
                            .occupied=true,
                            .recursive=false,
                        };
                    }

                    ++text_index;
                    goto next_part_of_pattern;
                }

                if (c == '\\' && text_index < text.length) {
                    text_index += 2;
                } else {
                    text_index += 1;
                }
            }
            return -1;
        } else if (EAT2(pattern, &pattern_state, pattern_index,
                        uc_is_property(grapheme, UC_PROPERTY_PAIRED_PUNCTUATION),
                        grapheme == '?')) {
            // Nested punctuation: (?), [?], etc
            int32_t open = _next_grapheme(pattern, &pattern_state, pattern_index-2);
            if (!match_grapheme(text, &text_index, open)) return -1;
            int64_t start_of_interior = text_index;
            int32_t close = open;
            uc_mirror_char(open, (uint32_t*)&close);
            if (!match_grapheme(pattern, &pattern_index, close))
                fail("Pattern's closing brace is missing: %k", &pattern);
            int64_t depth = 1;
            for (; depth > 0 && text_index < text.length; ++text_index) {
                int32_t c = _next_grapheme(text, &text_state, text_index);
                if (c == open)
                    depth += 1;
                else if (c == close)
                    depth -= 1;
            }

            // Save this as a capture, including only the interior text:
            if (captures && capture_index < MAX_BACKREFS) {
                captures[capture_index++] = (capture_t){
                    .index=start_of_interior,
                    .length=text_index - start_of_interior - 1,
                    .occupied=true,
                    .recursive=true,
                };
            }

            if (depth > 0) return -1;
        } else if (EAT1(pattern, &pattern_state, pattern_index,
                        grapheme == '{')) { // named patterns {id}, {2-3 hex}, etc.
            skip_whitespace(pattern, &pattern_index);
            int64_t min, max;
            if (uc_is_digit(_next_grapheme(pattern, &pattern_state, pattern_index))) {
                min = parse_int(pattern, &pattern_index);
                skip_whitespace(pattern, &pattern_index);
                if (match_grapheme(pattern, &pattern_index, '+')) {
                    max = INT64_MAX;
                } else if (match_grapheme(pattern, &pattern_index, '-')) {
                    max = parse_int(pattern, &pattern_index);
                } else {
                    max = min;
                }
            } else {
                min = 1, max = INT64_MAX;
            }

            skip_whitespace(pattern, &pattern_index);

            bool any = false, crlf = false;
            uc_property_t prop;
            int32_t specific_grapheme = UNINAME_INVALID;
            bool want_to_match = !match_grapheme(pattern, &pattern_index, '!');
            const char *prop_name;
            if (match_str(pattern, &pattern_index, ".."))
                prop_name = "..";
            else
                prop_name = get_property_name(pattern, &pattern_index);

            if (!prop_name) {
                // Literal character, e.g. {1?}
                specific_grapheme = _next_grapheme(pattern, &pattern_state, pattern_index);
                pattern_index += 1;
            } else if (strlen(prop_name) == 1) {
                // Single letter names: {1+ A}
                specific_grapheme = prop_name[0];
                prop_name = NULL;
            }

            skip_whitespace(pattern, &pattern_index);
            if (!match_grapheme(pattern, &pattern_index, '}'))
                fail("Missing closing '}' in pattern: %k", &pattern);

            int64_t before_group = text_index;
#define FAIL() ({ if (min < 1) { text_index = before_group; continue; } else { return -1; } })
#define SUCCESS() ({ \
   if (captures && capture_index < MAX_BACKREFS) { \
       captures[capture_index++] = (capture_t){ \
           .index=before_group, \
           .length=(text_index - before_group), \
           .occupied=true, \
           .recursive=false, \
       }; \
   }; continue; 0; })
            if (prop_name) {
                switch (tolower(prop_name[0])) {
                case '.':
                    if (prop_name[1] == '.') {
                        any = true;
                        prop = UC_PROPERTY_PRIVATE_USE;
                        goto got_prop;
                    }
                    break;
                case 'd':
                    if (strcasecmp(prop_name, "digit") == 0) {
                        prop = UC_PROPERTY_DECIMAL_DIGIT;
                        goto got_prop;
                    }
                    break;
                case 'e':
                    if (strcasecmp(prop_name, "end") == 0) {
                        if (text_index != text.length)
                            FAIL();
                        SUCCESS();
                    } else if (strcasecmp(prop_name, "email") == 0) {
                        int64_t len = match_email(text, text_index);
                        if (len < 0)
                            FAIL();
                        text_index += len;
                        SUCCESS();
                    } else if (strcasecmp(prop_name, "emoji") == 0) {
                        prop = UC_PROPERTY_EMOJI;
                        goto got_prop;
                    }
                    break;
                case 'i':
                    if (strcasecmp(prop_name, "id") == 0) {
                        if (!EAT1(text, &text_state, text_index,
                                  uc_is_property(grapheme, UC_PROPERTY_XID_START)))
                            FAIL();
                        EAT_MANY(text, &text_state, text_index,
                                 uc_is_property(grapheme, UC_PROPERTY_XID_CONTINUE));
                        SUCCESS();
                    } else if (strcasecmp(prop_name, "int") == 0) {
                        EAT1(text, &text_state, text_index, grapheme == '-');
                        int64_t n = EAT_MANY(text, &text_state, text_index,
                                             uc_is_property(grapheme, UC_PROPERTY_DECIMAL_DIGIT));
                        if (n <= 0)
                            FAIL();
                        SUCCESS();
                    } else if (strcasecmp(prop_name, "ipv4") == 0) {
                        int64_t len = match_ipv4(text, text_index);
                        if (len < 0)
                            FAIL();
                        text_index += len;
                        SUCCESS();
                    } else if (strcasecmp(prop_name, "ipv6") == 0) {
                        int64_t len = match_ipv6(text, text_index);
                        if (len < 0)
                            FAIL();
                        text_index += len;
                        SUCCESS();
                    } else if (strcasecmp(prop_name, "ip") == 0) {
                        int64_t len = match_ipv6(text, text_index);
                        if (len < 0)
                            len = match_ipv4(text, text_index);
                        if (len < 0)
                            FAIL();
                        text_index += len;
                        SUCCESS();
                    }
                    break;
                case 'n':
                    if (strcasecmp(prop_name, "nl") == 0 || strcasecmp(prop_name, "newline") == 0
                        || strcasecmp(prop_name, "crlf")) {
                        crlf = true;
                        prop = UC_PROPERTY_PRIVATE_USE;
                        goto got_prop;
                    } else if (strcasecmp(prop_name, "num") == 0) {
                        EAT1(text, &text_state, text_index, grapheme == '-');
                        int64_t pre_decimal = EAT_MANY(text, &text_state, text_index,
                                                       uc_is_property(grapheme, UC_PROPERTY_DECIMAL_DIGIT));
                        bool decimal = (EAT1(text, &text_state, text_index, grapheme == '.') == 1);
                        int64_t post_decimal = decimal ? EAT_MANY(text, &text_state, text_index,
                                                                  uc_is_property(grapheme, UC_PROPERTY_DECIMAL_DIGIT)) : 0;
                        if (pre_decimal == 0 && post_decimal == 0)
                            FAIL();
                        SUCCESS();
                    }
                    break;
                case 's':
                    if (strcasecmp(prop_name, "start") == 0) {
                        if (text_index != 0) return -1;
                        SUCCESS();
                    }
                    break;
                case 'u':
                    if (strcasecmp(prop_name, "uri") == 0) {
                        int64_t len = match_uri(text, text_index);
                        if (len < 0)
                            FAIL();
                        text_index += len;
                        SUCCESS();
                    } else if (strcasecmp(prop_name, "url") == 0) {
                        int64_t lookahead = text_index;
                        if (!(match_str(text, &lookahead, "https:")
                            || match_str(text, &lookahead, "http:")
                            || match_str(text, &lookahead, "ftp:")
                            || match_str(text, &lookahead, "wss:")
                            || match_str(text, &lookahead, "ws:")))
                            FAIL();

                        int64_t len = match_uri(text, text_index);
                        if (len < 0)
                            FAIL();
                        text_index += len;
                        SUCCESS();
                    }
                    break;
                }

                prop = uc_property_byname(prop_name);
                if (!uc_property_is_valid(prop)) {
                    specific_grapheme = unicode_name_character(prop_name);
                    if (specific_grapheme == UNINAME_INVALID)
                        fail("Not a valid property or character name: %s", prop_name);
                }
            }
      got_prop:;

            if (min == 0 && pattern_index < pattern.length) {
                // Try matching the rest of the pattern immediately:
                int64_t match_len = match(text, pattern, text_index, pattern_index, captures, capture_index + 1);
                if (match_len >= 0) {
                    // Save this as a capture, including only the interior text:
                    if (captures && capture_index < MAX_BACKREFS) {
                        captures[capture_index++] = (capture_t){
                            .index=before_group,
                            .length=(text_index - before_group) + match_len,
                            .occupied=true,
                            .recursive=false,
                        };
                    }
                    return (text_index - start_index) + match_len;
                }
            }

            for (int64_t count = 0; count < max; ) {
                int32_t grapheme = _next_grapheme(text, &text_state, text_index);
                grapheme = MAIN_GRAPHEME_CODEPOINT(grapheme);

                bool success;
                if (any) {
                    success = true;
                } else if (crlf) {
                    if (grapheme == '\r' && _next_grapheme(text, &text_state, text_index + 1) == '\n') {
                        text_index += 1;
                        grapheme = '\n';
                    }
                    success = (grapheme == '\n');
                } else if (specific_grapheme != UNINAME_INVALID) {
                    success = (grapheme == specific_grapheme);
                } else {
                    success = uc_is_property(grapheme, prop);
                }

                if (success != want_to_match) {
                    if (count < min) return -1;
                    else break;
                }

                text_index += 1;
                count += 1;

                if (count >= min) {
                    if (pattern_index < pattern.length) {
                        // If we have the minimum and we match the rest of the pattern, we're good:
                        int64_t match_len = match(text, pattern, text_index, pattern_index, captures, capture_index + 1);
                        if (match_len >= 0) {
                            // Save this as a capture, including only the interior text:
                            if (captures && capture_index < MAX_BACKREFS) {
                                captures[capture_index++] = (capture_t){
                                    .index=before_group,
                                    .length=(text_index - before_group) + match_len,
                                    .occupied=true,
                                    .recursive=false,
                                };
                            }

                            return (text_index - start_index) + match_len;
                        }
                    } else if (text_index >= text.length) {
                        break;
                    }
                }
            }
        } else {
            // Plain character:
            pattern_index = old_pat_index;
            int32_t pat_grapheme = _next_grapheme(pattern, &pattern_state, pattern_index);
            int32_t text_grapheme = _next_grapheme(text, &text_state, text_index);
            if (pat_grapheme != text_grapheme)
                return -1;

            pattern_index += 1;
            text_index += 1;
        }

      next_part_of_pattern:;
    }
    if (text_index >= text.length && pattern_index < pattern.length)
        return -1;
    return (text_index - start_index);
}

#undef EAT1
#undef EAT2
#undef EAT_MANY

static int64_t _find(Text_t text, Pattern_t pattern, int64_t first, int64_t last, int64_t *match_length)
{
    int32_t first_grapheme = get_grapheme(pattern, 0);
    bool find_first = (first_grapheme != '{'
                       && !uc_is_property(first_grapheme, UC_PROPERTY_QUOTATION_MARK)
                       && !uc_is_property(first_grapheme, UC_PROPERTY_PAIRED_PUNCTUATION));

    iteration_state_t text_state = {0, 0};

    for (int64_t i = first; i <= last; i++) {
        // Optimization: quickly skip ahead to first char in pattern:
        if (find_first) {
            while (i < text.length && _next_grapheme(text, &text_state, i) != first_grapheme)
                ++i;
        }

        int64_t m = match(text, pattern, i, 0, NULL, 0);
        if (m >= 0) {
            if (match_length)
                *match_length = m;
            return i;
        }
    }
    if (match_length)
        *match_length = -1;
    return -1;
}

public Int_t Text$find(Text_t text, Pattern_t pattern, Int_t from_index, int64_t *match_length)
{
    int64_t first = Int_to_Int64(from_index, false);
    if (first == 0) fail("Invalid index: 0");
    if (first < 0) first = text.length + first + 1;
    if (first > text.length || first < 1)
        return I(0);
    int64_t found = _find(text, pattern, first-1, text.length-1, match_length);
    return I(found+1);
}

public bool Text$has(Text_t text, Pattern_t pattern)
{
    int64_t found = _find(text, pattern, 0, text.length-1, NULL);
    return (found >= 0);
}

public bool Text$matches(Text_t text, Pattern_t pattern)
{
    int64_t len;
    int64_t found = _find(text, pattern, 0, 0, &len);
    return (found >= 0) && len == text.length;
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
#define add_char(c) Array$insert_value(&graphemes, (uint32_t)c, I_small(0), sizeof(uint32_t))
#define add_str(s) ({ for (char *_c = s; *_c; ++_c) Array$insert_value(&graphemes, (uint32_t)*_c, I_small(0), sizeof(uint32_t)); })
    if (colorize)
        add_str("\x1b[35m");
    if (quote_char != '"' && quote_char != '\"' && quote_char != '`')
        add_char('$');
    add_char(quote_char);

#define add_escaped(str) ({ if (colorize) add_str("\x1b[34;1m"); add_char('\\'); add_str(str); if (colorize) add_str("\x1b[0;35m"); })
    iteration_state_t state = {0, 0};
    for (int64_t i = 0; i < text.length; i++) {
        int32_t g = _next_grapheme(text, &state, i);
        switch (g) {
        case '\a': add_escaped("a"); break;
        case '\b': add_escaped("b"); break;
        case '\x1b': add_escaped("e"); break;
        case '\f': add_escaped("f"); break;
        case '\n': add_escaped("n"); break;
        case '\r': add_escaped("r"); break;
        case '\t': add_escaped("t"); break;
        case '\v': add_escaped("v"); break;
        case '\\': add_escaped("\\"); break;
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
            break;
        }
        default: {
            if (g == quote_char)
                add_escaped(((char[2]){quote_char, 0}));
            else
               add_char(g);
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
    if (!text) return info && info->TextInfo.lang ? Text$from_str(info->TextInfo.lang) : Text("Text");
    Text_t as_text = _quoted(*(Text_t*)text, colorize, info == &Pattern$info ? '/' : '"');
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

public Array_t Text$find_all(Text_t text, Pattern_t pattern)
{
    if (pattern.length == 0) // special case
        return (Array_t){.length=0};

    Array_t matches = {};

    for (int64_t i = 0; ; ) {
        int64_t len;
        int64_t found = _find(text, pattern, i, text.length-1, &len);
        if (found < 0) break;
        Text_t match = Text$slice(text, I(found+1), I(found + len));
        Array$insert(&matches, &match, I_small(0), sizeof(Text_t));
        i = found + MAX(len, 1);
    }

    return matches;
}

static Text_t apply_backrefs(Text_t text, Pattern_t original_pattern, Text_t replacement, Pattern_t backref_pat, capture_t *captures)
{
    if (backref_pat.length == 0)
        return replacement;

    int32_t first_grapheme = get_grapheme(backref_pat, 0);
    bool find_first = (first_grapheme != '{'
                       && !uc_is_property(first_grapheme, UC_PROPERTY_QUOTATION_MARK)
                       && !uc_is_property(first_grapheme, UC_PROPERTY_PAIRED_PUNCTUATION));

    Text_t ret = Text("");
    iteration_state_t state = {0, 0};
    int64_t nonmatching_pos = 0;
    for (int64_t pos = 0; pos < replacement.length; ) {
        // Optimization: quickly skip ahead to first char in the backref pattern:
        if (find_first) {
            while (pos < replacement.length && _next_grapheme(replacement, &state, pos) != first_grapheme)
                ++pos;
        }

        int64_t backref_len = match(replacement, backref_pat, pos, 0, NULL, 0);
        if (backref_len < 0) {
            pos += 1;
            continue;
        }

        int64_t after_backref = pos + backref_len;
        int64_t backref = parse_int(replacement, &after_backref);
        if (after_backref == pos + backref_len) { // Not actually a backref if there's no number
            pos += 1;
            continue;
        }
        if (backref < 0 || backref > 9) fail("Invalid backref index: %ld (only 0-%d are allowed)", backref, MAX_BACKREFS-1);
        backref_len = (after_backref - pos);

        if (_next_grapheme(replacement, &state, pos + backref_len) == ';')
            backref_len += 1; // skip optional semicolon

        if (!captures[backref].occupied)
            fail("There is no capture number %ld!", backref);

        Text_t backref_text = Text$slice(text, I(captures[backref].index+1), I(captures[backref].index + captures[backref].length));

        if (captures[backref].recursive && original_pattern.length > 0)
            backref_text = Text$replace(backref_text, original_pattern, replacement, backref_pat, true);

        if (pos > nonmatching_pos) {
            Text_t before_slice = Text$slice(replacement, I(nonmatching_pos+1), I(pos));
            ret = Text$concat(ret, before_slice, backref_text);
        } else {
            ret = concat2(ret, backref_text);
        }

        pos += backref_len;
        nonmatching_pos = pos;
    }
    if (nonmatching_pos < replacement.length) {
        Text_t last_slice = Text$slice(replacement, I(nonmatching_pos+1), I(replacement.length));
        ret = concat2(ret, last_slice);
    }
    return ret;
}

public Text_t Text$replace(Text_t text, Pattern_t pattern, Text_t replacement, Pattern_t backref_pat, bool recursive)
{
    Text_t ret = {.length=0};

    int32_t first_grapheme = get_grapheme(pattern, 0);
    bool find_first = (first_grapheme != '{'
                       && !uc_is_property(first_grapheme, UC_PROPERTY_QUOTATION_MARK)
                       && !uc_is_property(first_grapheme, UC_PROPERTY_PAIRED_PUNCTUATION));

    iteration_state_t text_state = {0, 0};
    int64_t nonmatching_pos = 0;
    for (int64_t pos = 0; pos < text.length; pos++) {
        // Optimization: quickly skip ahead to first char in pattern:
        if (find_first) {
            while (pos < text.length && _next_grapheme(text, &text_state, pos) != first_grapheme)
                ++pos;
        }

        capture_t captures[MAX_BACKREFS] = {};
        int64_t match_len = match(text, pattern, pos, 0, captures, 1);
        if (match_len < 0) continue;
        captures[0] = (capture_t){
            .index = pos, .length = match_len,
            .occupied = true, .recursive = false,
        };

        Text_t replacement_text = apply_backrefs(text, recursive ? pattern : Text(""), replacement, backref_pat, captures);
        if (pos > nonmatching_pos) {
            Text_t before_slice = Text$slice(text, I(nonmatching_pos+1), I(pos));
            ret = Text$concat(ret, before_slice, replacement_text);
        } else {
            ret = concat2(ret, replacement_text);
        }
        nonmatching_pos = pos + match_len;
        pos += (match_len - 1);
    }
    if (nonmatching_pos < text.length) {
        Text_t last_slice = Text$slice(text, I(nonmatching_pos+1), I(text.length));
        ret = concat2(ret, last_slice);
    }
    return ret;
}

public Text_t Text$map(Text_t text, Pattern_t pattern, closure_t fn)
{
    Text_t ret = {.length=0};

    int32_t first_grapheme = get_grapheme(pattern, 0);
    bool find_first = (first_grapheme != '{'
                       && !uc_is_property(first_grapheme, UC_PROPERTY_QUOTATION_MARK)
                       && !uc_is_property(first_grapheme, UC_PROPERTY_PAIRED_PUNCTUATION));

    iteration_state_t text_state = {0, 0};
    int64_t nonmatching_pos = 0;

    Text_t (*text_mapper)(Text_t, void*) = fn.fn;
    for (int64_t pos = 0; pos < text.length; pos++) {
        // Optimization: quickly skip ahead to first char in pattern:
        if (find_first) {
            while (pos < text.length && _next_grapheme(text, &text_state, pos) != first_grapheme)
                ++pos;
        }

        int64_t match_len = match(text, pattern, pos, 0, NULL, 0);
        if (match_len < 0) continue;

        Text_t replacement = text_mapper(Text$slice(text, I(pos+1), I(pos+match_len)), fn.userdata);
        if (pos > nonmatching_pos) {
            Text_t before_slice = Text$slice(text, I(nonmatching_pos+1), I(pos));
            ret = Text$concat(ret, before_slice, replacement);
        } else {
            ret = concat2(ret, replacement);
        }
        nonmatching_pos = pos + match_len;
        pos += (match_len - 1);
    }
    if (nonmatching_pos < text.length) {
        Text_t last_slice = Text$slice(text, I(nonmatching_pos+1), I(text.length));
        ret = concat2(ret, last_slice);
    }
    return ret;
}

public Text_t Text$replace_all(Text_t text, Table_t replacements, Text_t backref_pat, bool recursive)
{
    if (replacements.entries.length == 0) return text;

    Text_t ret = {.length=0};

    int64_t nonmatch_pos = 0;
    for (int64_t pos = 0; pos < text.length; ) {
        // Find the first matching pattern at this position:
        for (int64_t i = 0; i < replacements.entries.length; i++) {
            Pattern_t pattern = *(Pattern_t*)(replacements.entries.data + i*replacements.entries.stride);
            capture_t captures[MAX_BACKREFS] = {};
            int64_t len = match(text, pattern, pos, 0, captures, 1);
            if (len < 0) continue;
            captures[0].index = pos;
            captures[0].length = len;

            // If we skipped over some non-matching text before finding a match, insert it here:
            if (pos > nonmatch_pos) {
                Text_t before_slice = Text$slice(text, I(nonmatch_pos+1), I(pos));
                ret = concat2(ret, before_slice);
            }

            // Concatenate the replacement:
            Text_t replacement = *(Text_t*)(replacements.entries.data + i*replacements.entries.stride + sizeof(Text_t));
            Text_t replacement_text = apply_backrefs(text, recursive ? pattern : Text(""), replacement, backref_pat, captures);
            ret = concat2(ret, replacement_text);
            pos += MAX(len, 1);
            nonmatch_pos = pos;
            goto next_pos;
        }

        pos += 1;
      next_pos:
        continue;
    }

    if (nonmatch_pos <= text.length) {
        Text_t last_slice = Text$slice(text, I(nonmatch_pos+1), I(text.length));
        ret = concat2(ret, last_slice);
    }
    return ret;
}

public Array_t Text$split(Text_t text, Pattern_t pattern)
{
    if (text.length == 0) // special case
        return (Array_t){.length=0};

    if (pattern.length == 0) // special case
        return Text$clusters(text);

    Array_t chunks = {};

    Int_t i = I_small(1);
    for (;;) {
        int64_t len;
        Int_t found = Text$find(text, pattern, i, &len);
        if (I_is_zero(found)) break;
        Text_t chunk = Text$slice(text, i, Int$minus(found, I_small(1)));
        Array$insert(&chunks, &chunk, I_small(0), sizeof(Text_t));
        i = Int$plus(found, I(MAX(len, 1)));
    }

    Text_t last_chunk = Text$slice(text, i, I(text.length));
    Array$insert(&chunks, &last_chunk, I_small(0), sizeof(Text_t));

    return chunks;
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
        char *str = GC_MALLOC_ATOMIC(len+1);
        vsnprintf(str, len+1, fmt, args);
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
    iteration_state_t state = {0, 0};
    for (int64_t i = 0; i < text.length; i++) {
        int32_t grapheme = _next_grapheme(text, &state, i);
        if (grapheme < 0) {
            for (int64_t c = 0; c < NUM_GRAPHEME_CODEPOINTS(grapheme); c++)
                Array$insert(&codepoints, &GRAPHEME_CODEPOINTS(grapheme)[c], I_small(0), sizeof(uint32_t));
        } else {
            Array$insert(&codepoints, &grapheme, I_small(0), sizeof(uint32_t));
        }
    }
    return codepoints;
}

public Array_t Text$utf8_bytes(Text_t text)
{
    const char *str = Text$as_c_string(text);
    return (Array_t){.length=strlen(str), .stride=1, .atomic=1, .data=(void*)str};
}

static inline const char *codepoint_name(uint32_t c)
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
    iteration_state_t state = {0, 0};
    for (int64_t i = 0; i < text.length; i++) {
        int32_t grapheme = _next_grapheme(text, &state, i);
        if (grapheme < 0) {
            for (int64_t c = 0; c < NUM_GRAPHEME_CODEPOINTS(grapheme); c++) {
                const char *name = codepoint_name(GRAPHEME_CODEPOINTS(grapheme)[c]);
                Text_t name_text = (Text_t){.tag=TEXT_ASCII, .length=strlen(name), .ascii=name};
                Array$insert(&names, &name_text, I_small(0), sizeof(Text_t));
            }
        } else {
            const char *name = codepoint_name(grapheme);
            Text_t name_text = (Text_t){.tag=TEXT_ASCII, .length=strlen(name), .ascii=name};
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
        uint32_t codepoint = unicode_name_character(name_str);
        if (codepoint != UNINAME_INVALID)
            Array$insert(&codepoints, &codepoint, I_small(0), sizeof(uint32_t));
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
    iteration_state_t state = {0, 0};
    for (int64_t i = 0, line_start = 0; i < text.length; i++) {
        int32_t grapheme = _next_grapheme(text, &state, i);
        if (grapheme == '\r' && _next_grapheme(text, &state, i + 1) == '\n') { // CRLF
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
#define add_char(c) Array$insert_value(&graphemes, (uint32_t)c, I_small(0), sizeof(uint32_t))
#define add_str(s) ({ for (char *_c = s; *_c; ++_c) Array$insert_value(&graphemes, (uint32_t)*_c, I_small(0), sizeof(uint32_t)); })
    iteration_state_t state = {0, 0};
    for (int64_t i = 0; i < text.length; i++) {
        int32_t g = _next_grapheme(text, &state, i);
        uint32_t g0 = g < 0 ? GRAPHEME_CODEPOINTS(g)[0] : (uint32_t)g;

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
    .size=sizeof(Text_t),
    .align=__alignof__(Text_t),
    .tag=TextInfo,
    .TextInfo={.lang="Pattern"},
};

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
