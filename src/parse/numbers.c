// Logic for parsing numbers

#include <ctype.h>
#include <gc.h>
#include <stdarg.h>
#include <stdbool.h>
#include <string.h>

#include <unictype.h>
#include <uniname.h>

#include "../ast.h"
#include "context.h"
#include "errors.h"
#include "utils.h"

ast_t *parse_int(parse_ctx_t *ctx, const char *pos) {
    const char *start = pos;
    (void)match(&pos, "-");
    if (!isdigit(*pos)) return NULL;
    if (match(&pos, "0x")) { // Hex
        pos += strspn(pos, "0123456789abcdefABCDEF_");
    } else if (match(&pos, "0b")) { // Binary
        pos += strspn(pos, "01_");
    } else if (match(&pos, "0o")) { // Octal
        pos += strspn(pos, "01234567_");
    } else { // Decimal
        pos += strspn(pos, "0123456789_");
    }
    char *str = GC_MALLOC_ATOMIC((size_t)(pos - start) + 1);
    memset(str, 0, (size_t)(pos - start) + 1);
    for (char *src = (char *)start, *dest = str; src < pos; ++src) {
        if (*src != '_') *(dest++) = *src;
    }

    if (match(&pos, "e") || match(&pos, "f")) // floating point literal
        return NULL;

    // `50%` and `90deg` are numeric literals, not integers: both scale the
    // written digits by an exact factor (1/100, and pi/180 respectively).
    if (match(&pos, "%")) return NewAST(ctx->file, start, pos, Num, .str = str, .suffix = NUM_PERCENT);
    else if (match(&pos, "deg")) return NewAST(ctx->file, start, pos, Num, .str = str, .suffix = NUM_DEGREES);

    return NewAST(ctx->file, start, pos, Int, .str = str);
}

ast_t *parse_num(parse_ctx_t *ctx, const char *pos) {
    const char *start = pos;
    bool negative = match(&pos, "-");
    if (!isdigit(*pos) && *pos != '.') return NULL;
    else if (*pos == '.' && !isdigit(pos[1])) return NULL;

    size_t len = strspn(pos, "0123456789_");
    if (strncmp(pos + len, "..", 2) == 0) return NULL;
    else if (pos[len] == '.' && is_xid_start_next(pos + len + 1)) return NULL;
    else if (pos[len] == '.') len += 1 + strspn(pos + len + 1, "0123456789");
    else if (pos[len] != 'e' && pos[len] != 'f' && pos[len] != '%') return NULL;
    if (pos[len] == 'e') {
        len += 1;
        if (pos[len] == '-' || pos[len] == '+') len += 1;
        len += strspn(pos + len, "0123456789_");
    }
    char *buf = GC_MALLOC_ATOMIC(len + 1);
    memset(buf, 0, len + 1);
    for (char *src = (char *)pos, *dest = buf; src < pos + len; ++src) {
        if (*src != '_') *(dest++) = *src;
    }
    pos += len;

    if (negative) buf = String("-", buf);

    // The digits are kept verbatim rather than converted here: a Num literal
    // is exact, and going through a double first would round `3.15` to
    // 3.14999999999999991... before the exact value was ever built.
    int suffix = NUM_PLAIN;
    if (match(&pos, "%")) suffix = NUM_PERCENT;
    else if (match(&pos, "deg")) suffix = NUM_DEGREES;

    return NewAST(ctx->file, start, pos, Num, .str = buf, .suffix = suffix);
}
