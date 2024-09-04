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

#include "siphash.c"

typedef struct {
    int64_t num_codepoints;
    const uint32_t *codepoints;
    const uint8_t *utf8;
} synthetic_grapheme_t;

#define MAX_SYNTHETIC_GRAPHEMES 1024
#define MAX_BACKREFS 100
static synthetic_grapheme_t synthetic_graphemes[MAX_SYNTHETIC_GRAPHEMES] = {};

static int32_t num_synthetic_graphemes = 0;

static int32_t get_grapheme(Text_t text, int64_t index);

typedef struct {
    int64_t subtext, sum_of_previous_subtexts;
} iteration_state_t;

static int32_t _next_grapheme(Text_t text, iteration_state_t *state, int64_t index);

int32_t find_synthetic_grapheme(const uint32_t *codepoints, int64_t len)
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

int32_t get_synthetic_grapheme(const uint32_t *codepoints, int64_t len)
{
    int32_t index = find_synthetic_grapheme(codepoints, len);
    if (index >= 0 
        && index < num_synthetic_graphemes
        && synthetic_graphemes[index].num_codepoints == len
        && memcmp(synthetic_graphemes[index].codepoints, codepoints, len) == 0) {
        return -(index+1);
    } else {
        if (index < 0) index = 0;

        if (num_synthetic_graphemes >= MAX_SYNTHETIC_GRAPHEMES)
            fail("Too many synthetic graphemes!");

        if (num_synthetic_graphemes > 0 && index != num_synthetic_graphemes) {
            memmove(&synthetic_graphemes[index + 1], &synthetic_graphemes[index],
                    sizeof(synthetic_grapheme_t[num_synthetic_graphemes - index]));
        }

        uint32_t *buf = GC_MALLOC_ATOMIC(sizeof(uint32_t[len]));
        memcpy(buf, codepoints, sizeof(uint32_t[len]));
        synthetic_graphemes[index].codepoints = buf;
        synthetic_graphemes[index].num_codepoints = len;

        size_t u8_len = 0;
        uint8_t *u8 = u32_to_u8(codepoints, len, NULL, &u8_len);
        uint8_t *gc_u8 = GC_MALLOC_ATOMIC(u8_len + 1);
        memcpy(gc_u8, u8, u8_len);
        gc_u8[u8_len] = '\0';
        synthetic_graphemes[index].utf8 = gc_u8;
        assert(gc_u8);
        free(u8);

        ++num_synthetic_graphemes;

        return -(index+1);
    }
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
        int32_t *graphemes = t.tag == TEXT_SHORT_GRAPHEMES ? t.short_graphemes : t.graphemes;
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
                const uint8_t *u8 = synthetic_graphemes[-grapheme-1].utf8;
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

static Text_t concat2(Text_t a, Text_t b)
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
        .length=len,
        .tag=TEXT_SUBTEXT,
        .subtexts=GC_MALLOC(sizeof(Text_t[len])),
    };
    int64_t sub_i = 0;
    for (int i = 0; i < n; i++) {
        if (items[i].length == 0)
            continue;

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
    int32_t *graphemes = ret.short_graphemes;
    while (src < &codepoints[num_codepoints]) {
        if (ret.tag == TEXT_SHORT_GRAPHEMES && ret.length + 1 > 2) {
            graphemes = GC_MALLOC_ATOMIC(sizeof(int32_t[num_codepoints])); // May be a slight overallocation
            graphemes[0] = ret.short_graphemes[0];
            graphemes[1] = ret.short_graphemes[1];
            ret.tag = TEXT_GRAPHEMES;
            ret.graphemes = graphemes;
        }

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

public Text_t Text$from_str(const char *str)
{
    int64_t ascii_span = 0;
    while (str[ascii_span] && isascii(str[ascii_span]))
        ascii_span++;

    if (str[ascii_span] == '\0') { // All ASCII
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
            const uint32_t *codepoints = graphemes[g] < 0 ? synthetic_graphemes[-graphemes[g]-1].codepoints : (uint32_t*)&graphemes[g];
            int64_t num_codepoints = graphemes[g] < 0 ? synthetic_graphemes[-graphemes[g]-1].num_codepoints : 1;
            uint8_t u8_buf[64];
            size_t u8_len = sizeof(u8_buf);
            uint8_t *u8 = u32_to_u8(codepoints, num_codepoints, u8_buf, &u8_len);

            if (*i + (int64_t)u8_len > (int64_t)*capacity) {
                *capacity = *i + u8_len + 1;
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
            int64_t a_len = ai >= 0 ? 1 : synthetic_graphemes[-ai-1].num_codepoints;

            const uint32_t *b_codepoints = bi >= 0 ? (uint32_t*)&bi : synthetic_graphemes[-bi-1].codepoints;
            int64_t b_len = bi >= 0 ? 1 : synthetic_graphemes[-bi-1].num_codepoints;

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
    array_t codepoints = Text$utf32_codepoints(text);
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
    array_t codepoints = Text$utf32_codepoints(text);
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
    array_t codepoints = Text$utf32_codepoints(text);
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
        if (prev_codepoint < 0)
            prev_codepoint = synthetic_graphemes[-prev_codepoint-1].codepoints[0];
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
        if (prev_codepoint < 0)
            prev_codepoint = synthetic_graphemes[-prev_codepoint-1].codepoints[0];
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
                            start_of_quoted_text,
                            text_index - start_of_quoted_text,
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
                    start_of_interior,
                    text_index - start_of_interior - 1,
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

            bool any = false;
            uc_property_t prop;
            int32_t specific_codepoint = UNINAME_INVALID;
            bool want_to_match = !match_grapheme(pattern, &pattern_index, '!');
            const char *prop_name;
            if (match_str(pattern, &pattern_index, ".."))
                prop_name = "..";
            else
                prop_name = get_property_name(pattern, &pattern_index);

            if (!prop_name) {
                // Literal character, e.g. {1?}
                specific_codepoint = _next_grapheme(pattern, &pattern_state, pattern_index);
                pattern_index += 1;
            } else if (strlen(prop_name) == 1) {
                // Single letter names: {1+ A}
                specific_codepoint = prop_name[0];
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
           before_group, \
           (text_index - before_group), \
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
                    } else if (prop_name && strcasecmp(prop_name, "email") == 0) {
                        int64_t len = match_email(text, text_index);
                        if (len < 0)
                            FAIL();
                        text_index += len;
                        SUCCESS();
                    } else if (prop_name && strcasecmp(prop_name, "emoji") == 0) {
                        prop = UC_PROPERTY_EMOJI;
                        goto got_prop;
                    }
                    break;
                case 'i':
                    if (prop_name && strcasecmp(prop_name, "id") == 0) {
                        if (!EAT1(text, &text_state, text_index,
                                  uc_is_property(grapheme, UC_PROPERTY_XID_START)))
                            FAIL();
                        EAT_MANY(text, &text_state, text_index,
                                 uc_is_property(grapheme, UC_PROPERTY_XID_CONTINUE));
                        SUCCESS();
                    } else if (prop_name && strcasecmp(prop_name, "int") == 0) {
                        EAT1(text, &text_state, text_index, grapheme == '-');
                        int64_t n = EAT_MANY(text, &text_state, text_index,
                                             uc_is_property(grapheme, UC_PROPERTY_DECIMAL_DIGIT));
                        if (n <= 0)
                            FAIL();
                        SUCCESS();
                    } else if (prop_name && strcasecmp(prop_name, "ipv4") == 0) {
                        int64_t len = match_ipv4(text, text_index);
                        if (len < 0)
                            FAIL();
                        text_index += len;
                        SUCCESS();
                    } else if (prop_name && strcasecmp(prop_name, "ipv6") == 0) {
                        int64_t len = match_ipv6(text, text_index);
                        if (len < 0)
                            FAIL();
                        text_index += len;
                        SUCCESS();
                    } else if (prop_name && strcasecmp(prop_name, "ip") == 0) {
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
                    if (prop_name && strcasecmp(prop_name, "num") == 0) {
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
                    if (prop_name && strcasecmp(prop_name, "uri") == 0) {
                        int64_t len = match_uri(text, text_index);
                        if (len < 0)
                            FAIL();
                        text_index += len;
                        SUCCESS();
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
                        SUCCESS();
                    }
                    break;
                }

                prop = uc_property_byname(prop_name);
                if (!uc_property_is_valid(prop)) {
                    specific_codepoint = unicode_name_character(prop_name);
                    if (specific_codepoint == UNINAME_INVALID)
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
                            before_group,
                            (text_index - before_group) + match_len,
                        };
                    }
                    return (text_index - start_index) + match_len;
                }
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
                        // If we have the minimum and we match the rest of the pattern, we're good:
                        int64_t match_len = match(text, pattern, text_index, pattern_index, captures, capture_index + 1);
                        if (match_len >= 0) {
                            // Save this as a capture, including only the interior text:
                            if (captures && capture_index < MAX_BACKREFS) {
                                captures[capture_index++] = (capture_t){
                                    before_group,
                                    (text_index - before_group) + match_len,
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

public Int_t Text$find(Text_t text, Pattern_t pattern, Int_t from_index, int64_t *match_length)
{
    int64_t first = Int_to_Int64(from_index, false);
    if (first == 0) fail("Invalid index: 0");
    if (first < 0) first = text.length + first + 1;
    if (first > text.length || first < 1)
        return I_small(0);

    int32_t first_grapheme = get_grapheme(pattern, 0);
    bool find_first = (first_grapheme != '{'
                       && !uc_is_property(first_grapheme, UC_PROPERTY_QUOTATION_MARK)
                       && !uc_is_property(first_grapheme, UC_PROPERTY_PAIRED_PUNCTUATION));

    iteration_state_t text_state = {0, 0};

    for (int64_t i = first-1; i < text.length; i++) {
        // Optimization: quickly skip ahead to first char in pattern:
        if (find_first) {
            while (i < text.length && _next_grapheme(text, &text_state, i) != first_grapheme)
                ++i;
        }

        int64_t m = match(text, pattern, i, 0, NULL, 0);
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

public bool Text$has(Text_t text, Pattern_t pattern)
{
    return !I_is_zero(Text$find(text, pattern, I_small(1), NULL));
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
    array_t graphemes = {.atomic=1};
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
    Text_t as_text = _quoted(*(Text_t*)text, colorize, info == &Pattern ? '/' : '"');
    if (info && info->TextInfo.lang && info != &$Text && info != &Pattern)
        as_text = Text$concat(colorize ? Text("\x1b[1m$") : Text("$"), Text$from_str(info->TextInfo.lang), as_text);
    return as_text;
}

public Text_t Text$quoted(Text_t text, bool colorize)
{
    return _quoted(text, colorize, '"');
}

public array_t Text$find_all(Text_t text, Pattern_t pattern)
{
    if (pattern.length == 0) // special case
        return (array_t){.length=0};

    array_t matches = {};

    Int_t i = I_small(1);
    for (;;) {
        int64_t len;
        Int_t found = Text$find(text, pattern, i, &len);
        if (I_is_zero(found)) break;
        Text_t match = Text$slice(text, found, Int$plus(found, I(len-1)));
        Array$insert(&matches, &match, I_small(0), sizeof(Text_t));
        i = Int$plus(found, I(len <= 0 ? 1 : len));
    }

    return matches;
}

static Text_t apply_backrefs(Text_t text, Text_t replacement, Pattern_t backref_pat, capture_t *captures)
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

        Text_t backref_text = Text$slice(text, I(captures[backref].index+1), I(captures[backref].index + captures[backref].length));
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

public Text_t Text$replace(Text_t text, Pattern_t pattern, Text_t replacement, Pattern_t backref_pat)
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
        captures[0].index = pos;
        captures[0].length = match_len;

        Text_t replacement_text = apply_backrefs(text, replacement, backref_pat, captures);
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

public Text_t Text$replace_all(Text_t text, table_t replacements, Text_t backref_pat)
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
            Text_t replacement_text = apply_backrefs(text, replacement, backref_pat, captures);
            ret = concat2(ret, replacement_text);
            pos += len > 0 ? len : 1;
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

public array_t Text$split(Text_t text, Pattern_t pattern)
{
    if (text.length == 0) // special case
        return (array_t){.length=0};

    if (pattern.length == 0) // special case
        return Text$clusters(text);

    array_t chunks = {};

    Int_t i = I_small(1);
    for (;;) {
        int64_t len;
        Int_t found = Text$find(text, pattern, i, &len);
        if (I_is_zero(found)) break;
        Text_t chunk = Text$slice(text, i, Int$minus(found, I_small(1)));
        Array$insert(&chunks, &chunk, I_small(0), sizeof(Text_t));
        i = Int$plus(found, I(len <= 0 ? 1 : len));
    }

    Text_t last_chunk = Text$slice(text, i, I(text.length));
    Array$insert(&chunks, &last_chunk, I_small(0), sizeof(Text_t));

    return chunks;
}

public Text_t Text$join(Text_t glue, array_t pieces)
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

public array_t Text$clusters(Text_t text)
{
    array_t clusters = {.atomic=1};
    for (int64_t i = 1; i <= text.length; i++) {
        Text_t cluster = Text$slice(text, I(i), I(i));
        Array$insert(&clusters, &cluster, I_small(0), sizeof(Text_t));
    }
    return clusters;
}

public array_t Text$utf32_codepoints(Text_t text)
{
    array_t codepoints = {.atomic=1};
    iteration_state_t state = {0, 0};
    for (int64_t i = 0; i < text.length; i++) {
        int32_t grapheme = _next_grapheme(text, &state, i);
        if (grapheme < 0) {
            for (int64_t c = 0; c < synthetic_graphemes[-grapheme-1].num_codepoints; c++)
                Array$insert(&codepoints, &synthetic_graphemes[-grapheme-1].codepoints[c], I_small(0), sizeof(uint32_t));
        } else {
            Array$insert(&codepoints, &grapheme, I_small(0), sizeof(uint32_t));
        }
    }
    return codepoints;
}

public array_t Text$utf8_bytes(Text_t text)
{
    const char *str = Text$as_c_string(text);
    return (array_t){.length=strlen(str), .stride=1, .atomic=1, .data=(void*)str};
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

public array_t Text$codepoint_names(Text_t text)
{
    array_t names = {};
    iteration_state_t state = {0, 0};
    for (int64_t i = 0; i < text.length; i++) {
        int32_t grapheme = _next_grapheme(text, &state, i);
        if (grapheme < 0) {
            for (int64_t c = 0; c < synthetic_graphemes[-grapheme-1].num_codepoints; c++) {
                const char *name = codepoint_name(synthetic_graphemes[-grapheme-1].codepoints[c]);
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

public Text_t Text$from_codepoints(array_t codepoints)
{
    if (codepoints.stride != sizeof(int32_t))
        Array$compact(&codepoints, sizeof(int32_t));

    return text_from_u32(codepoints.data, codepoints.length, true);
}

public Text_t Text$from_codepoint_names(array_t codepoint_names)
{
    array_t codepoints = {};
    for (int64_t i = 0; i < codepoint_names.length; i++) {
        Text_t *name = ((Text_t*)(codepoint_names.data + i*codepoint_names.stride));
        const char *name_str = Text$as_c_string(*name);
        uint32_t codepoint = unicode_name_character(name_str);
        if (codepoint != UNINAME_INVALID)
            Array$insert(&codepoints, &codepoint, I_small(0), sizeof(uint32_t));
    }
    return Text$from_codepoints(codepoints);
}

public Text_t Text$from_bytes(array_t bytes)
{
    if (bytes.stride != sizeof(int8_t))
        Array$compact(&bytes, sizeof(int8_t));

    int8_t nul = 0;
    Array$insert(&bytes, &nul, I_small(0), sizeof(int8_t));
    return Text$from_str(bytes.data);
}

public array_t Text$lines(Text_t text)
{
    array_t lines = {};
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

public const TypeInfo $Text = {
    .size=sizeof(Text_t),
    .align=__alignof__(Text_t),
    .tag=TextInfo,
    .TextInfo={.lang="Text"},
};

public Pattern_t Pattern$escape_text(Text_t text)
{
    // TODO: optimize for ASCII and short strings
    array_t graphemes = {.atomic=1};
#define add_char(c) Array$insert_value(&graphemes, (uint32_t)c, I_small(0), sizeof(uint32_t))
#define add_str(s) ({ for (char *_c = s; *_c; ++_c) Array$insert_value(&graphemes, (uint32_t)*_c, I_small(0), sizeof(uint32_t)); })
    iteration_state_t state = {0, 0};
    for (int64_t i = 0; i < text.length; i++) {
        int32_t g = _next_grapheme(text, &state, i);
        uint32_t g0 = g < 0 ? synthetic_graphemes[-g-1].codepoints[0] : (uint32_t)g;

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

public const TypeInfo Pattern = {
    .size=sizeof(Text_t),
    .align=__alignof__(Text_t),
    .tag=TextInfo,
    .TextInfo={.lang="Pattern"},
};

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
