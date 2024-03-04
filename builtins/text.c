#include <assert.h>
#include <ctype.h>
#include <err.h>
#include <gc.h>
#include <gc/cord.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/param.h>
#include <unistr.h>
#include <unicase.h>
#include <uninorm.h>

#include "../SipHash/halfsiphash.h"
#include "array.h"
#include "functions.h"
#include "text.h"
#include "types.h"

#define CLAMP(x, lo, hi) MIN(hi, MAX(x,lo))

public CORD Text__as_text(const void *str, bool colorize, const TypeInfo *info)
{
    (void)info;
    if (!str) return "Text";
    return Text__quoted(*(CORD*)str, colorize);
}

public CORD Text__quoted(CORD str, bool colorize)
{
    // Note: it's important to have unicode strings not get broken up with
    // escapes, otherwise they won't print right.
    if (colorize) {
        CORD quoted = "\x1b[35m\"";
        CORD_pos i;
        CORD_FOR(i, str) {
            char c = CORD_pos_fetch(i);
            switch (c) {
#define BACKSLASHED(esc) "\x1b[34m\\\x1b[1m" esc "\x1b[0;35m"
            case '\a': quoted = CORD_cat(quoted, BACKSLASHED("a")); break;
            case '\b': quoted = CORD_cat(quoted, BACKSLASHED("b")); break;
            case '\x1b': quoted = CORD_cat(quoted, BACKSLASHED("e")); break;
            case '\f': quoted = CORD_cat(quoted, BACKSLASHED("f")); break;
            case '\n': quoted = CORD_cat(quoted, BACKSLASHED("n")); break;
            case '\r': quoted = CORD_cat(quoted, BACKSLASHED("r")); break;
            case '\t': quoted = CORD_cat(quoted, BACKSLASHED("t")); break;
            case '\v': quoted = CORD_cat(quoted, BACKSLASHED("v")); break;
            case '"': quoted = CORD_cat(quoted, BACKSLASHED("\"")); break;
            case '\\': quoted = CORD_cat(quoted, BACKSLASHED("\\")); break;
            case '\x00' ... '\x06': case '\x0E' ... '\x1A':
            case '\x1C' ... '\x1F': case '\x7F' ... '\x7F':
                CORD_sprintf(&quoted, "%r" BACKSLASHED("x%02X"), quoted, c);
                break;
            default: quoted = CORD_cat_char(quoted, c); break;
#undef BACKSLASHED
            }
        }
        quoted = CORD_cat(quoted, "\"\x1b[m");
        return quoted;
    } else {
        CORD quoted = "\"";
        CORD_pos i;
        CORD_FOR(i, str) {
            char c = CORD_pos_fetch(i);
            switch (c) {
            case '\a': quoted = CORD_cat(quoted, "\\a"); break;
            case '\b': quoted = CORD_cat(quoted, "\\b"); break;
            case '\x1b': quoted = CORD_cat(quoted, "\\e"); break;
            case '\f': quoted = CORD_cat(quoted, "\\f"); break;
            case '\n': quoted = CORD_cat(quoted, "\\n"); break;
            case '\r': quoted = CORD_cat(quoted, "\\r"); break;
            case '\t': quoted = CORD_cat(quoted, "\\t"); break;
            case '\v': quoted = CORD_cat(quoted, "\\v"); break;
            case '"': quoted = CORD_cat(quoted, "\\\""); break;
            case '\\': quoted = CORD_cat(quoted, "\\\\"); break;
            case '\x00' ... '\x06': case '\x0E' ... '\x1A':
            case '\x1C' ... '\x1F': case '\x7F' ... '\x7F':
                CORD_sprintf(&quoted, "%r\\x%02X", quoted, c);
                break;
            default: quoted = CORD_cat_char(quoted, c); break;
            }
        }
        quoted = CORD_cat_char(quoted, '"');
        return quoted;
    }
}

public int Text__compare(CORD *x, CORD *y)
{
    uint8_t *xx = (uint8_t*)CORD_to_const_char_star(*x);
    uint8_t *yy = (uint8_t*)CORD_to_const_char_star(*y);
    int result = 0;
    if (u8_normcmp(xx, strlen((char*)xx), yy, strlen((char*)yy), UNINORM_NFD, &result))
        fail("Something went wrong while comparing text");
    return result;
}

public bool Text__equal(CORD *x, CORD *y)
{
    return Text__compare(x, y) == 0;
}

public uint32_t Text__hash(CORD *cord)
{
    if (!*cord) return 0;

    const char *str = CORD_to_const_char_star(*cord);
    size_t len = strlen(str);
    uint8_t buf[128] = {0};
    size_t norm_len = sizeof(buf)-1;
    uint8_t *normalized = u8_normalize(UNINORM_NFD, (uint8_t*)str, len, buf, &norm_len);

    uint32_t hash;
    halfsiphash(normalized, norm_len, SSS_HASH_VECTOR, (uint8_t*)&hash, sizeof(hash));
    if (normalized != buf) free(normalized);
    return hash;
}

public CORD Text__upper(CORD str)
{
    if (!str) return str;
    size_t len = strlen(str) + 1;
    uint8_t *dest = GC_MALLOC_ATOMIC(len);
    return (CORD)u8_toupper((const uint8_t*)str, len-1, uc_locale_language(), NULL, dest, &len);
}

public CORD Text__lower(CORD str)
{
    if (!str) return str;
    size_t len = strlen(str) + 1;
    uint8_t *dest = GC_MALLOC_ATOMIC(len);
    return (CORD)u8_tolower((const uint8_t*)str, len-1, uc_locale_language(), NULL, dest, &len);
}

public CORD Text__title(CORD str)
{
    if (!str) return str;
    size_t len = strlen(str) + 1;
    uint8_t *dest = GC_MALLOC_ATOMIC(len);
    return (CORD)u8_totitle((const uint8_t*)str, len-1, uc_locale_language(), NULL, dest, &len);
}

public bool Text__has(CORD str, CORD target, where_e where)
{
    if (!target) return true;
    if (!str) return false;

    if (where == WHERE_START) {
        return (CORD_ncmp(str, 0, target, 0, CORD_len(target)) == 0);
    } else if (where == WHERE_END) {
        size_t str_len = CORD_len(str);
        size_t target_len = CORD_len(target);
        return (str_len >= target_len && CORD_ncmp(str, str_len-target_len, target, 0, target_len) == 0);
    } else {
        size_t pos = CORD_str(str, 0, target);
        return (pos != CORD_NOT_FOUND);
    }
}

public CORD Text__without(CORD str, CORD target, where_e where)
{
    if (!str || !target) return str;

    size_t target_len = CORD_len(target);
    if (where == WHERE_START) {
        if (CORD_ncmp(str, 0, target, 0, target_len) == 0)
            return CORD_substr(str, target_len, SIZE_MAX);
        return str;
    } else if (where == WHERE_END) {
        size_t str_len = CORD_len(str);
        if (CORD_ncmp(str, str_len-target_len, target, 0, target_len) == 0)
            return CORD_substr(str, 0, str_len - target_len);
        return str;
    } else {
        errx(1, "Not implemented");
    }
}

public CORD Text__trimmed(CORD str, CORD skip, where_e where)
{
    if (!str || !skip) return str;
    const uint8_t *ustr = (const uint8_t*)CORD_to_const_char_star(str);
    const uint8_t *uskip = (const uint8_t*)CORD_to_const_char_star(skip);
    if (where == WHERE_START) {
        size_t span = u8_strspn(ustr, uskip);
        return (CORD)ustr + span;
    } else if (where == WHERE_END) {
        size_t len = u8_strlen(ustr);
        const uint8_t *back = ustr + len;
        size_t back_span = 0;
        while (back - back_span > ustr && u8_strspn(back-back_span-1, uskip) > back_span)
            ++back_span;
        return CORD_substr((CORD)ustr, 0, len - back_span);
    } else {
        size_t span = u8_strspn(ustr, uskip);
        size_t len = u8_strlen(ustr);
        const uint8_t *back = ustr + len;
        size_t back_span = 0;
        while (back - back_span > ustr + span && u8_strspn(back-back_span-1, uskip) > back_span)
            ++back_span;
        return CORD_substr((CORD)(ustr + span), 0, len - span - back_span);
    }
}

public find_result_t Text__find(CORD str, CORD pat)
{
    if (!pat) return (find_result_t){.status=FIND_SUCCESS, .index=1};
    size_t pos = CORD_str(str, 0, pat);
    return (pos == CORD_NOT_FOUND) ? (find_result_t){.status=FIND_FAILURE} : (find_result_t){.status=FIND_SUCCESS, .index=(int32_t)pos};
}

public CORD Text__replace(CORD text, CORD pat, CORD replacement, int64_t limit)
{
    if (!text || !pat) return text;
    CORD ret = NULL;
    size_t pos = 0, pat_len = CORD_len(pat);
    for (size_t found; limit > 0 && (found=CORD_str(text, pos, pat)) != CORD_NOT_FOUND; --limit) {
        ret = CORD_cat(ret, CORD_substr(text, pos, found));
        ret = CORD_cat(ret, replacement);
        pos = found + pat_len;
    }
    return CORD_cat(ret, CORD_substr(text, pos, SIZE_MAX));
}

public array_t Text__split(CORD str, CORD split)
{
    if (!str) return (array_t){.data=GC_MALLOC(sizeof(CORD)), .atomic=1, .length=1, .stride=sizeof(CORD)};
    array_t strings = {.stride=sizeof(CORD), .atomic=1};
    int64_t capacity = 0;

    const uint8_t *ustr = (uint8_t*)CORD_to_const_char_star(str);
    const uint8_t *usplit = (uint8_t*)CORD_to_const_char_star(split);
    for (int64_t i = 0; ; ) {
        size_t non_split = u8_strcspn(ustr + i, usplit);
        CORD chunk = CORD_substr((CORD)ustr, i, non_split);
        if (capacity <= 0)
            strings.data = GC_REALLOC(strings.data, sizeof(CORD)*(capacity += 10));
        ((CORD*)strings.data)[strings.length++] = chunk;

        i += non_split;

        size_t split = u8_strspn(ustr + i, usplit);
        if (split == 0) break;
        i += split;
    }
    return strings;
}

public CORD Text__join(CORD glue, array_t pieces)
{
    if (pieces.length == 0) return CORD_EMPTY;

    CORD ret = CORD_EMPTY;
    for (int64_t i = 0; i < pieces.length; i++) {
        if (i > 0) ret = CORD_cat(ret, glue);
        ret = CORD_cat(ret, *(CORD*)((void*)pieces.data + i*pieces.stride));
    }
    return ret;
}

public const TypeInfo Text = {
    .size=sizeof(CORD),
    .align=__alignof__(CORD),
    .tag=CustomInfo,
    .CustomInfo={
        .as_text=(void*)Text__as_text,
        .compare=(void*)Text__compare,
        .equal=(void*)Text__equal,
        .hash=(void*)Text__hash,
    },
};

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
