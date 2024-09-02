// Type info and methods for Text datatype, which uses the Boehm "cord" library
// and libunistr

#include <assert.h>
#include <ctype.h>
#include <err.h>
#include <gc.h>
#include <gmp.h>
#include <limits.h>
#include <printf.h>
#include <readline/history.h>
#include <readline/readline.h>
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
#include "text.h"
#include "types.h"

static struct {
    size_t num_codepoints;
    const uint32_t *codepoints;
} synthetic_graphemes[1024] = {};

static int32_t num_synthetic_graphemes = 0;

static int32_t get_grapheme(Text_t text, int64_t index);

typedef struct {
    int64_t subtext, sum_of_previous_subtexts;
} iteration_state_t;

static int32_t _next_grapheme(Text_t text, iteration_state_t *state, int64_t index);

int32_t find_synthetic_grapheme(const uint32_t *codepoints, size_t len)
{
    int32_t lo = 0, hi = num_synthetic_graphemes;
    while (lo <= hi) {
        int32_t mid = (lo + hi) / 2;
        int32_t cmp = (synthetic_graphemes[mid].num_codepoints > len) - (synthetic_graphemes[mid].num_codepoints < len);
        if (cmp == 0)
            cmp = memcmp(synthetic_graphemes[mid].codepoints, codepoints, sizeof(uint32_t[len]));

        if (cmp == 0)
            return mid;
        else if (cmp < 0)
            lo = mid + 1;    
        else if (cmp > 0)
            hi = mid - 1;
    }
    return hi;
}

int32_t get_synthetic_grapheme(const uint32_t *codepoints, size_t len)
{
    int32_t index = find_synthetic_grapheme(codepoints, len);
    if (index < num_synthetic_graphemes
        && synthetic_graphemes[index].num_codepoints == len
        && memcmp(synthetic_graphemes[index].codepoints, codepoints, len) == 0) {
        return -(index+1);
    } else {
        if (num_synthetic_graphemes > 0)
            memmove(&synthetic_graphemes[index], &synthetic_graphemes[index + 1], num_synthetic_graphemes - index);

        uint32_t *buf = GC_MALLOC_ATOMIC(sizeof(uint32_t[len]));
        memcpy(buf, codepoints, sizeof(uint32_t[len]));
        synthetic_graphemes[index].codepoints = buf;
        synthetic_graphemes[index].num_codepoints = len;

        ++num_synthetic_graphemes;
        return -(index+1);
    }
}

static inline size_t num_subtexts(Text_t t)
{
    if (t.tag != TEXT_SUBTEXT) return 1;
    size_t len = t.length;
    size_t n = 0;
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
        size_t to_print = t.length;
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
    switch (t.tag) {
    case TEXT_SHORT_ASCII: return fwrite(t.short_ascii, sizeof(char), t.length, stream);
    case TEXT_ASCII: return fwrite(t.ascii, sizeof(char), t.length, stream);
    case TEXT_GRAPHEMES: case TEXT_SHORT_GRAPHEMES: {
        int32_t *graphemes = t.tag == TEXT_SHORT_GRAPHEMES ? t.short_graphemes : t.graphemes;
        int written = 0;
        for (int64_t i = 0; i < t.length; i++) {
            int32_t grapheme = graphemes[i];
            if (grapheme >= 0) {
                written += ulc_fprintf(stream, "%.*llU", 1, &grapheme);
            } else {
                written += ulc_fprintf(
                    stream, "%.*llU",
                    synthetic_graphemes[-grapheme-1].num_codepoints,
                    synthetic_graphemes[-grapheme-1].codepoints);
            }
        }
        return written;
    }
    case TEXT_SUBTEXT: {
        int written = 0;
        int i = 0;
        for (size_t to_print = t.length; to_print > 0; to_print -= t.subtexts[i].length, ++i)
            written += Text$print(stream, t.subtexts[i]);
        return written;
    }
    default: return 0;
    }
}

static Text_t concat2(Text_t a, Text_t b)
{
    if (a.length == 0) return b;
    if (b.length == 0) return a;

    if (a.tag == TEXT_SUBTEXT && b.tag == TEXT_SUBTEXT) {
        size_t na = num_subtexts(a);
        size_t nb = num_subtexts(b);
        Text_t ret = {
            .length=a.length + b.length,
            .tag=TEXT_SUBTEXT,
            .subtexts=GC_MALLOC(sizeof(Text_t[na + nb])),
        };
        memcpy(&ret.subtexts[0], a.subtexts, sizeof(Text_t[na]));
        memcpy(&ret.subtexts[na], b.subtexts, sizeof(Text_t[nb]));
        return ret;
    } else if (a.tag == TEXT_SUBTEXT) {
        size_t n = num_subtexts(a);
        Text_t ret = {
            .length=a.length + b.length,
            .tag=TEXT_SUBTEXT,
            .subtexts=GC_MALLOC(sizeof(Text_t[n + 1])),
        };
        memcpy(ret.subtexts, a.subtexts, sizeof(Text_t[n]));
        ret.subtexts[n] = b;
        return ret;
    } else if (b.tag == TEXT_SUBTEXT) {
        size_t n = num_subtexts(b);
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

public Text_t Text$_concat(int n, Text_t items[n])
{
    if (n == 0) return (Text_t){.length=0};
    if (n == 1) return items[0];
    if (n == 2) return concat2(items[0], items[1]);

    int64_t len = 0, subtexts = 0;
    for (int i = 0; i < n; i++) {
        len += items[i].length;
        subtexts += num_subtexts(items[i]);
    }

    Text_t ret = {
        .length=len,
        .tag=TEXT_SUBTEXT,
        .subtexts=GC_MALLOC(sizeof(Text_t[len])),
    };
    int64_t sub_i = 0;
    for (int i = 0; i < n; i++) {
        if (items[i].tag == TEXT_SUBTEXT) {
            for (int64_t j = 0, remainder = items[i].length; remainder > 0; j++) {
                ret.subtexts[sub_i++] = items[i].subtexts[j];
                remainder -= items[i].subtexts[j].length;
            }
        } else {
            ret.subtexts[sub_i++] = items[i];
        }
    }
    return ret;
}

public Text_t Text$slice(Text_t text, Int_t first_int, Int_t last_int)
{
    int64_t first = Int_to_Int64(first_int, false)-1;
    int64_t last = Int_to_Int64(last_int, false)-1;
    if (first == 0) errx(1, "Invalid index: 0");
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
        Text_t ret = text;
        ret.length = last - first + 1;
        if (first > 1)
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
            return Text$slice(subtexts[0], Int64_to_Int(first+1), Int64_to_Int(last+1));

        Text_t ret = {
            .length=needed_len,
            .tag=TEXT_SUBTEXT,
            .subtexts=GC_MALLOC(sizeof(Text_t[num_subtexts])),
        };
        for (int64_t i = 0; i < num_subtexts; i++) {
            ret.subtexts[i] = Text$slice(subtexts[i], Int64_to_Int(first+1), Int64_to_Int(last+1));
            first = 1;
            needed_len -= ret.subtexts[i].length;
            last = first + needed_len - 1;
        }
        return ret;
    }
    default: errx(1, "Invalid tag");
    }
}

Text_t text_from_u32(uint32_t *codepoints, size_t num_codepoints, bool normalize)
{
    uint32_t norm_buf[128];
    if (normalize) {
        size_t norm_length = sizeof(norm_buf)/sizeof(norm_buf[0]);
        uint32_t *normalized = u32_normalize(UNINORM_NFC, codepoints, num_codepoints, norm_buf, &norm_length);
        codepoints = normalized;
        num_codepoints = norm_length;
    }

    char breaks[num_codepoints];
    u32_grapheme_breaks(codepoints, num_codepoints, breaks);

    Text_t ret = {
        .length=0,
        .tag=TEXT_SHORT_GRAPHEMES,
    };
    const uint32_t *src = codepoints;
    int32_t *dest = &ret.short_graphemes[0];
    while (src != &codepoints[num_codepoints]) {
        ++ret.length;

        if (ret.tag == TEXT_SHORT_GRAPHEMES && ret.length > 2) {
            int32_t *graphemes = GC_MALLOC_ATOMIC(sizeof(int32_t[num_codepoints])); // May be a slight overallocation
            graphemes[0] = ret.short_graphemes[0];
            graphemes[1] = ret.short_graphemes[1];
            ret.tag = TEXT_GRAPHEMES;
            ret.graphemes = graphemes;
            dest = &graphemes[2];
        }

        const uint32_t *next = u32_grapheme_next(src, &codepoints[num_codepoints]);
        if (next == &src[1]) {
            *dest = (int32_t)*src;
        } else {
            // Synthetic grapheme
            *dest = get_synthetic_grapheme(src, next-src);
        }
        ++dest;
        src = next;
    }
    if (normalize && codepoints != norm_buf) free(codepoints);
    return ret;
}

public Text_t Text$from_str(const char *str)
{
    size_t ascii_span = 0;
    while (str[ascii_span] && isascii(str[ascii_span]))
        ascii_span++;

    if (str[ascii_span] == '\0') { // All ASCII
        Text_t ret = {.length=ascii_span};
        if (ascii_span <= 8) {
            ret.tag = TEXT_SHORT_ASCII;
            for (size_t i = 0; i < ascii_span; i++)
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

static void u8_buf_append(Text_t text, char **buf, size_t *capacity, int64_t *i)
{
    switch (text.tag) {
    case TEXT_ASCII: case TEXT_SHORT_ASCII: {
        if (*i + text.length > (int64_t)*capacity) {
            *capacity = *i + text.length;
            *buf = GC_REALLOC(*buf, *capacity);
        }

        const char *bytes = text.tag == TEXT_ASCII ? text.ascii : text.short_ascii;
        memcpy(*buf + *i, bytes, text.length);
        *i += text.length;
        break;
    }
    case TEXT_GRAPHEMES: case TEXT_SHORT_GRAPHEMES: {
        const int32_t *graphemes = text.tag == TEXT_GRAPHEMES ? text.graphemes : text.short_graphemes;
        for (int64_t g = 0; g + 1 < text.length; g++) {
            const uint32_t *codepoints = graphemes[g] < 0 ? synthetic_graphemes[-graphemes[g]-1].codepoints : (uint32_t*)&graphemes[g];
            size_t num_codepoints = graphemes[g] < 0 ? synthetic_graphemes[-graphemes[g]-1].num_codepoints : 1;
            uint8_t u8_buf[64];
            size_t u8_len = sizeof(u8_buf);
            uint8_t *u8 = u32_to_u8(codepoints, num_codepoints, u8_buf, &u8_len);

            if (*i + (int64_t)u8_len > (int64_t)*capacity) {
                *capacity = *i + u8_len;
                *buf = GC_REALLOC(*buf, *capacity);
            }

            memcpy(*buf + *i, u8, u8_len);
            *i += u8_len;
            if (u8 != u8_buf) free(u8);
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
    size_t capacity = text.length;
    char *buf = GC_MALLOC_ATOMIC(capacity);
    int64_t i = 0;
    u8_buf_append(text, &buf, &capacity, &i);
    return buf;
}

uint32_t *text_to_u32(Text_t text, size_t *length)
{
    // Precalculate size:
    size_t len = 0;
    if (text.tag == TEXT_ASCII) {
        len = text.length;
    } else {
        iteration_state_t state = {0, 0};
        for (int64_t i = 0; i < text.length; i++) {
            int32_t grapheme = _next_grapheme(text, &state, i);
            if (grapheme < 0)
                len += synthetic_graphemes[-grapheme-1].num_codepoints;
            else
                len += 1;
        }
    }
    assert(length);
    *length = len;

    // Copy over codepoints one grapheme cluster at a time:
    uint32_t *ret = GC_MALLOC_ATOMIC(sizeof(uint32_t[len]));
    uint32_t *dest = ret;
    iteration_state_t state = {0, 0};
    for (int64_t i = 0; i < text.length; i++) {
        int32_t grapheme = _next_grapheme(text, &state, i);
        if (grapheme < 0) {
            const uint32_t *codepoints = synthetic_graphemes[-grapheme-1].codepoints;
            size_t num_codepoints = synthetic_graphemes[-grapheme-1].num_codepoints;
            for (size_t j = 0; j < num_codepoints; j++)
                *(dest++) = codepoints[j];
        } else {
            *(dest++) = (uint32_t)grapheme;
        }
    }
    return ret;
}

#include "siphash.c"

public uint64_t Text$hash(Text_t *text)
{
    if (text->hash != 0) return text->hash;
    siphash sh;
    siphashinit(&sh, sizeof(int32_t[text->length]), (uint64_t*)TOMO_HASH_KEY);

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
                int32_t *graphemes = subtext.graphemes;
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
                return _next_grapheme(text.subtexts[state->subtext], NULL, index);
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

int32_t Text$compare(const Text_t *a, const Text_t *b)
{
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
                synthetic_graphemes[-bi-1].codepoints,
                synthetic_graphemes[-bi-1].num_codepoints);
        } else if (bi > 0) {
            cmp = u32_cmp2(
                synthetic_graphemes[-ai-1].codepoints,
                synthetic_graphemes[-ai-1].num_codepoints,
                (uint32_t*)&bi, 1);
        } else {
            cmp = u32_cmp2(
                synthetic_graphemes[-ai-1].codepoints,
                synthetic_graphemes[-ai-1].num_codepoints,
                synthetic_graphemes[-bi-1].codepoints,
                synthetic_graphemes[-bi-1].num_codepoints);
        }
        if (cmp != 0) return cmp;
    }
    return 0;
}

public bool Text$equal(const Text_t *a, const Text_t *b)
{
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
            const uint32_t *a_codepoints = ai >= 0 ? (uint32_t*)&ai : synthetic_graphemes[-ai-1].codepoints;
            size_t a_len = ai >= 0 ? 1 : synthetic_graphemes[-ai-1].num_codepoints;

            const uint32_t *b_codepoints = bi >= 0 ? (uint32_t*)&bi : synthetic_graphemes[-bi-1].codepoints;
            size_t b_len = bi >= 0 ? 1 : synthetic_graphemes[-bi-1].num_codepoints;

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
    size_t length;
    uint32_t *codepoints = text_to_u32(text, &length);
    const char *language = uc_locale_language();
    uint32_t buf[128]; 
    size_t out_len;
    uint32_t *upper = u32_toupper(codepoints, length, language, UNINORM_NFC, buf, &out_len);
    Text_t ret = text_from_u32(upper, out_len, false);
    if (upper != buf) free(upper);
    return ret;
}

public Text_t Text$lower(Text_t text)
{
    size_t length;
    uint32_t *codepoints = text_to_u32(text, &length);
    const char *language = uc_locale_language();
    uint32_t buf[128]; 
    size_t out_len;
    uint32_t *lower = u32_tolower(codepoints, length, language, UNINORM_NFC, buf, &out_len);
    Text_t ret = text_from_u32(lower, out_len, false);
    if (lower != codepoints) free(lower);
    return ret;
}

public Text_t Text$title(Text_t text)
{
    size_t length;
    uint32_t *codepoints = text_to_u32(text, &length);
    const char *language = uc_locale_language();
    uint32_t buf[128]; 
    size_t out_len;
    uint32_t *title = u32_totitle(codepoints, length, language, UNINORM_NFC, buf, &out_len);
    Text_t ret = text_from_u32(title, out_len, false);
    if (title != codepoints) free(title);
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
    if (grapheme < 0) // TODO: check every codepoint in the cluster?
        grapheme = synthetic_graphemes[-grapheme-1].codepoints[0];

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
        if (grapheme < 0)
            grapheme = synthetic_graphemes[-grapheme-1].codepoints[0];
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
        } else if (dest == name && grapheme >= 0 && grapheme != ']') {
            // Literal character escape: [..[] --> "LEFT SQUARE BRACKET"
            name = unicode_character_name(grapheme, name);
            *i += 1;
            return name;
        } else {
            break;
        }
        *i += 1;
    }
    if (dest == name) return NULL;
    *dest = '\0';
    return name;
}

#define EAT1(state, cond) ({\
        int32_t grapheme = _next_grapheme(text, state, text_index); \
        bool success = (cond); \
        if (success) text_index += 1; \
        success; })

#define EAT_MANY(state, cond) ({ int64_t n = 0; while (EAT1(state, cond)) { n += 1; } n; })

int64_t match_email(Text_t text, int64_t text_index)
{
    // email = local "@" domain
    // local = 1-64 ([a-zA-Z0-9!#$%&‘*+–/=?^_`.{|}~] | non-ascii)
    // domain = dns-label ("." dns-label)*
    // dns-label = 1-63 ([a-zA-Z0-9-] | non-ascii)

    iteration_state_t state = {0, 0};
    if (text_index > 0) {
        int32_t prev_codepoint = _next_grapheme(text, &state, text_index - 1);
        if (prev_codepoint < 0)
            prev_codepoint = synthetic_graphemes[-prev_codepoint-1].codepoints[0];
        if (uc_is_property_alphabetic(prev_codepoint))
            return -1;
    }

    int64_t start_index = text_index;

    // Local part:
    int64_t local_len = 0;
    static const char *allowed_local = "!#$%&‘*+–/=?^_`.{|}~";
    while (EAT1(&state, (grapheme & ~0x7F) || isalnum((char)grapheme) || strchr(allowed_local, (char)grapheme))) {
        local_len += 1;
        if (local_len > 64) return -1;
    }
    
    if (!EAT1(&state, grapheme == '@'))
        return -1;

    // Host
    int64_t host_len = 0;
    do {
        int64_t label_len = 0;
        while (EAT1(&state, (grapheme & ~0x7F) || isalnum((char)grapheme) || grapheme == '-')) {
            label_len += 1;
            if (label_len > 63) return -1;
        }

        if (label_len == 0)
            return -1;

        host_len += label_len;
        if (host_len > 255)
            return -1;
        host_len += 1;
    } while (EAT1(&state, grapheme == '.'));

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
            if (!EAT1(&state, ~(grapheme & ~0x7F) && isxdigit((char)grapheme)))
                break;
        }
        if (EAT1(&state, ~(grapheme & ~0x7F) && isxdigit((char)grapheme)))
            return -1; // Too many digits

        if (cluster == NUM_CLUSTERS-1) {
            break;
        } else if (!EAT1(&state, grapheme == ':')) {
            if (double_colon_used)
                break;
            return -1;
        }

        if (EAT1(&state, grapheme == ':')) {
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
            if (!EAT1(&state, ~(grapheme & ~0x7F) && isdigit((char)grapheme))) {
                if (digits == 0) return -1;
                break;
            }
        }

        if (EAT1(&state, ~(grapheme & ~0x7F) && isdigit((char)grapheme)))
            return -1; // Too many digits

        if (cluster == NUM_CLUSTERS-1)
            break;
        else if (!EAT1(&state, grapheme == '.'))
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
        if (prev_codepoint < 0)
            prev_codepoint = synthetic_graphemes[-prev_codepoint-1].codepoints[0];
        if (uc_is_property_alphabetic(prev_codepoint))
            return -1;
    }

    int64_t start_index = text_index;

    // Scheme:
    if (!EAT1(&state, isalpha(grapheme)))
        return -1;

    EAT_MANY(&state, !(grapheme & ~0x7F) && (isalnum(grapheme) || grapheme == '+' || grapheme == '.' || grapheme == '-'));

    if (text_index == start_index)
        return -1;

    if (!match_grapheme(text, &text_index, ':'))
        return -1;

    // Authority:
    if (match_str(text, &text_index, "//")) {
        int64_t authority_start = text_index;
        // Username or host:
        static const char *forbidden = "#?:@ \t\r\n<>[]{}\\^|\"`/";
        if (EAT_MANY(&state, (grapheme & ~0x7F) || !strchr(forbidden, (char)grapheme)) == 0)
            return -1;

        if (EAT1(&state, grapheme == '@')) {
            // Found a username, now get a host:
            if (EAT_MANY(&state, (grapheme & ~0x7F) || !strchr(forbidden, (char)grapheme)) == 0)
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
        if (EAT1(&state, grapheme == ':')) {
            if (EAT_MANY(&state, !(grapheme & ~0x7F) && isdigit(grapheme)) == 0)
                return -1;
        }
        if (!EAT1(&state, grapheme == '/'))
            return (text_index - start_index); // No path
    } else {
        // Optional path root:
        EAT1(&state, grapheme == '/');
    }

    // Path:
    static const char *non_path = " \"#?<>[]{}\\^`|";
    EAT_MANY(&state, (grapheme & ~0x7F) || !strchr(non_path, (char)grapheme));

    if (EAT1(&state, grapheme == '?')) { // Query
        static const char *non_query = " \"#<>[]{}\\^`|";
        EAT_MANY(&state, (grapheme & ~0x7F) || !strchr(non_query, (char)grapheme));
    }
    
    if (EAT1(&state, grapheme == '#')) { // Fragment
        static const char *non_fragment = " \"#<>[]{}\\^`|";
        EAT_MANY(&state, (grapheme & ~0x7F) || !strchr(non_fragment, (char)grapheme));
    }
    return text_index - start_index;
}

int64_t match(Text_t text, Text_t pattern, int64_t text_index, int64_t pattern_index)
{
    if (pattern_index >= pattern.length) return 0;
    int64_t start_index = text_index;
    iteration_state_t pattern_state = {0, 0}, text_state = {0, 0};
    while (pattern_index < pattern.length) {
        int64_t old_pat_index = pattern_index;
        if (match_str(pattern, &pattern_index, "[..")) {
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
            bool want_to_match = !match_grapheme(pattern, &pattern_index, '!');
            const char *prop_name = get_property_name(pattern, &pattern_index);

            skip_whitespace(pattern, &pattern_index);
            if (!match_grapheme(pattern, &pattern_index, ']'))
                errx(1, "Missing closing ']' in pattern: \"%T\"", &pattern);

            int64_t before_group = text_index;
            bool any = false;
            uc_property_t prop;
            int32_t specific_codepoint = UNINAME_INVALID;

#define FAIL() ({ if (min < 1) { text_index = before_group; continue; } else { return -1; } })
            if (prop_name) {
                switch (tolower(prop_name[0])) {
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
                        continue;
                    } else if (prop_name && strcasecmp(prop_name, "email") == 0) {
                        int64_t len = match_email(text, text_index);
                        if (len < 0)
                            FAIL();
                        text_index += len;
                        continue;
                    } else if (prop_name && strcasecmp(prop_name, "emoji") == 0) {
                        prop = UC_PROPERTY_EMOJI;
                        goto got_prop;
                    }
                    break;
                case 'i':
                    if (prop_name && strcasecmp(prop_name, "id") == 0) {
                        if (!EAT1(&text_state, uc_is_property(grapheme, UC_PROPERTY_XID_START)))
                            FAIL();
                        EAT_MANY(&text_state, uc_is_property(grapheme, UC_PROPERTY_XID_CONTINUE));
                        continue;
                    } else if (prop_name && strcasecmp(prop_name, "ipv4") == 0) {
                        int64_t len = match_ipv4(text, text_index);
                        if (len < 0)
                            FAIL();
                        text_index += len;
                        continue;
                    } else if (prop_name && strcasecmp(prop_name, "ipv6") == 0) {
                        int64_t len = match_ipv6(text, text_index);
                        if (len < 0)
                            FAIL();
                        text_index += len;
                        continue;
                    } else if (prop_name && strcasecmp(prop_name, "ip") == 0) {
                        int64_t len = match_ipv6(text, text_index);
                        if (len < 0)
                            len = match_ipv4(text, text_index);
                        if (len < 0)
                            FAIL();
                        text_index += len;
                        continue;
                    }
                    break;
                case 's':
                    if (strcasecmp(prop_name, "start") == 0) {
                        if (text_index != 0) return -1;
                        continue;
                    }
                    break;
                case 'u':
                    if (prop_name && strcasecmp(prop_name, "uri") == 0) {
                        int64_t len = match_uri(text, text_index);
                        if (len < 0)
                            FAIL();
                        text_index += len;
                        continue;
                    } else if (prop_name && strcasecmp(prop_name, "url") == 0) {
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
                        continue;
                    }
                    break;
                }

                prop = uc_property_byname(prop_name);
                if (!uc_property_is_valid(prop)) {
                    specific_codepoint = unicode_name_character(prop_name);
                    if (specific_codepoint == UNINAME_INVALID)
                        errx(1, "Not a valid property or character name: %s", prop_name);
                }
            } else {
                any = true;
                prop = UC_PROPERTY_PRIVATE_USE;
            }
      got_prop:;

            if (min == 0 && pattern_index < pattern.length) {
                int64_t match_len = match(text, pattern, text_index, pattern_index);
                if (match_len >= 0)
                    return (text_index - start_index) + match_len;
            }

            for (int64_t count = 0; count < max; ) {
                int32_t grapheme = _next_grapheme(text, &text_state, text_index);
                if (grapheme < 0)
                    grapheme = synthetic_graphemes[-grapheme-1].codepoints[0];

                bool success;
                if (any)
                    success = true;
                else if (specific_codepoint != UNINAME_INVALID)
                    success = (grapheme == specific_codepoint);
                else
                    success = uc_is_property(grapheme, prop);

                if (success != want_to_match) {
                    if (count < min) return -1;
                    else break;
                }

                text_index += 1;
                count += 1;

                if (count >= min) {
                    if (pattern_index < pattern.length) {
                        int64_t match_len = match(text, pattern, text_index, pattern_index);
                        if (match_len >= 0) {
                            return (text_index - start_index) + match_len;
                        }
                    } else if (text_index >= text.length) {
                        break;
                    }
                }
            }
        } else if (uc_is_property(_next_grapheme(pattern, &pattern_state, pattern_index), UC_PROPERTY_QUOTATION_MARK)
                   && (pattern_index += 1, match_grapheme(pattern, &pattern_index, '?'))) {
            // Quotation: "?", '?', etc
            int32_t open = _next_grapheme(pattern, &pattern_state, pattern_index-2);
            if (!match_grapheme(text, &text_index, open)) return -1;
            int32_t close = open;
            uc_mirror_char(open, (uint32_t*)&close);
            if (!match_grapheme(pattern, &pattern_index, close))
                errx(1, "I expected a closing brace");
            while (text_index < text.length) {
                int32_t c = _next_grapheme(text, &text_state, text_index);
                if (c == close)
                    return (text_index - start_index);

                if (c == '\\' && text_index < text.length) {
                    text_index += 2;
                } else {
                    text_index += 1;
                }
            }
            return -1;
        } else if (uc_is_property(_next_grapheme(pattern, &pattern_state, pattern_index), UC_PROPERTY_PAIRED_PUNCTUATION)
                   && (pattern_index += 1, match_grapheme(pattern, &pattern_index, '?'))) {
            // Nested punctuation: (?), [?], etc
            int32_t open = _next_grapheme(pattern, &pattern_state, pattern_index-2);
            if (!match_grapheme(text, &text_index, open)) return -1;
            int32_t close = open;
            uc_mirror_char(open, (uint32_t*)&close);
            if (!match_grapheme(pattern, &pattern_index, close))
                errx(1, "I expected a closing brace");
            int64_t depth = 1;
            for (; depth > 0 && text_index < text.length; ++text_index) {
                int32_t c = _next_grapheme(text, &text_state, text_index);
                if (c == open)
                    depth += 1;
                else if (c == close)
                    depth -= 1;
            }
            if (depth > 0) return -1;
        } else {
            // Plain character:
            pattern_index = old_pat_index;
            int32_t pat_grapheme = _next_grapheme(pattern, &pattern_state, pattern_index);

            if (pattern_index == 0 && text_index > 0) {
                int32_t pat_codepoint = pat_grapheme;
                if (pat_codepoint < 0)
                    pat_codepoint = synthetic_graphemes[-pat_codepoint-1].codepoints[0];

                int32_t prev_codepoint = _next_grapheme(text, &text_state, text_index - 1);
                if (prev_codepoint < 0)
                    prev_codepoint = synthetic_graphemes[-prev_codepoint-1].codepoints[0];
                if (uc_is_property_alphabetic(pat_codepoint) && uc_is_property_alphabetic(prev_codepoint))
                    return -1;
            }

            int32_t text_grapheme = _next_grapheme(text, &text_state, text_index);
            if (pat_grapheme != text_grapheme)
                return -1;

            pattern_index += 1;
            text_index += 1;

            if (pattern_index == pattern.length && text_index < text.length) {
                int32_t pat_codepoint = pat_grapheme;
                if (pat_codepoint < 0)
                    pat_codepoint = synthetic_graphemes[-pat_codepoint-1].codepoints[0];

                int32_t next_codepoint = _next_grapheme(text, &text_state, text_index);
                if (next_codepoint < 0)
                    next_codepoint = synthetic_graphemes[-next_codepoint-1].codepoints[0];
                if (uc_is_property_alphabetic(pat_codepoint) && uc_is_property_alphabetic(next_codepoint))
                    return -1;
            }
        }
    }
    if (text_index >= text.length && pattern_index < pattern.length)
        return -1;
    return (text_index - start_index);
}

#undef EAT1
#undef EAT_MANY

public Int_t Text$find(Text_t text, Text_t pattern, Int_t from_index, int64_t *match_length)
{
    int32_t first = get_grapheme(pattern, 0);
    bool find_first = (first != '['
                       && !uc_is_property(first, UC_PROPERTY_QUOTATION_MARK)
                       && !uc_is_property(first, UC_PROPERTY_PAIRED_PUNCTUATION));

    iteration_state_t text_state = {0, 0};
    for (int64_t i = Int_to_Int64(from_index, false)-1; i < text.length; i++) {
        // Optimization: quickly skip ahead to first char in pattern:
        if (find_first) {
            while (i < text.length && _next_grapheme(text, &text_state, i) != first)
                ++i;
        }

        int64_t m = match(text, pattern, i, 0);
        if (m >= 0) {
            if (match_length)
                *match_length = m;
            return I(i+1);
        }
    }
    if (match_length)
        *match_length = -1;
    return I(0);
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
    (void)info;
    Text_t t = **(Text_t**)args[0];
    return Text$print(stream, t);
}

public Text_t Text$as_text(const void *text, bool colorize, const TypeInfo *info)
{
    (void)info;
    if (!text) return Text$from_str("Text");
    return Text$quoted(*(Text_t*)text, colorize);
}

public Text_t Text$quoted(Text_t text, bool colorize)
{
    // TODO: optimize for ASCII and short strings
    array_t graphemes = {.atomic=1};
#define add_char(c) Array$insert_value(&graphemes, (uint32_t)c, I_small(0), sizeof(uint32_t))
#define add_str(s) ({ for (char *_c = s; *_c; ++_c) Array$insert_value(&graphemes, (uint32_t)*_c, I_small(0), sizeof(uint32_t)); })
    if (colorize)
        add_str("\x1b[35m\"");
    else
        add_char('"');

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
        case '"': add_escaped("\""); break;
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
        default: add_char(g); break;
        }
    }

    if (colorize)
        add_str("\"\x1b[m");
    else
        add_char('"');

    return (Text_t){.length=graphemes.length, .tag=TEXT_GRAPHEMES, .graphemes=graphemes.data};
#undef add_str
#undef add_char
#undef add_escaped
}

public Text_t Text$replace(Text_t text, Text_t pattern, Text_t replacement)
{
    Text_t ret = {.length=0};

    Int_t i = I_small(0);
    for (;;) {
        int64_t len;
        Int_t found = Text$find(text, pattern, i, &len);
        if (found.small == I_small(0).small) break;
        if (Int$compare(&found, &i, &$Text) > 0) {
            ret = Text$concat(
                ret,
                Text$slice(text, i, Int$minus(found, I_small(1))),
                replacement
            );
        } else {
            ret = concat2(ret, replacement);
        }
    }
    if (Int_to_Int64(i, false) <= text.length) {
        ret = concat2(ret, Text$slice(text, i, Int64_to_Int(text.length)));
    }
    return ret;
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

public const TypeInfo $Text = {
    .size=sizeof(Text_t),
    .align=__alignof__(Text_t),
    .tag=TextInfo,
    .TextInfo={.lang="Text"},
};

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
