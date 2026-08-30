// Logic for parsing numbers

#include <ctype.h>
#include <gc.h>
#include <stdarg.h>
#include <stdbool.h>
#include <string.h>

#include <unictype.h>
#include <uniname.h>

#include "../ast.h"
#include "../stdlib/number.h"
#include "context.h"
#include "errors.h"
#include "utils.h"

// The exact value a numeric literal denotes. Built from the written digits
// rather than a double, so `3.15` is 63/20 and not the nearest double to it,
// and scaled exactly by any suffix: `%` is a division by 100, and `deg` a
// multiplication by pi/180 -- which, pi being exact here, stays exact.
static Num_t num_literal_value(parse_ctx_t *ctx, const char *pos, const char *digits, int suffix) {
    Num_t n = number_from_decimal(digits);
    if (number_is_error(n)) parser_err(ctx, pos, pos, "I couldn't parse this number");
    switch (suffix) {
    case NUM_PERCENT: return number_div(n, number_from_int(100));
    case NUM_DEGREES: return number_mul(n, number_div(number_pi(), number_from_int(180)));
    default: return n;
    }
}

ast_t *parse_int(parse_ctx_t *ctx, const char *pos) {
    const char *start = pos;
    if (!isdigit(*pos)) return NULL;
    if (match(&pos, "0x")) { // Hex
        pos += span_of(pos, "0123456789abcdefABCDEF_");
    } else if (match(&pos, "0b")) { // Binary
        pos += span_of(pos, "01_");
    } else if (match(&pos, "0o")) { // Octal
        pos += span_of(pos, "01234567_");
    } else { // Decimal
        pos += span_of(pos, "0123456789_");
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
    if (match(&pos, "%"))
        return NewAST(ctx->file, start, pos, Num, .n = num_literal_value(ctx, start, str, NUM_PERCENT), .str = str,
                      .suffix = NUM_PERCENT);
    else if (match(&pos, "deg"))
        return NewAST(ctx->file, start, pos, Num, .n = num_literal_value(ctx, start, str, NUM_DEGREES), .str = str,
                      .suffix = NUM_DEGREES);

    return NewAST(ctx->file, start, pos, Int, .str = str);
}

ast_t *parse_num(parse_ctx_t *ctx, const char *pos) {
    const char *start = pos;
    if (!isdigit(*pos) && *pos != '.') return NULL;
    else if (*pos == '.' && !isdigit(pos[1])) return NULL;

    size_t len = span_of(pos, "0123456789_");
    // Digits followed by `.identifier` are an Int with a method/field, not a
    // Num: `12.sqrt()` calls sqrt on the integer 12. But a SECOND dot ends
    // that reading -- `12..round()` is the Num `12.` with `.round()` called
    // on it, since `12.` followed by a path literal would be nonsense.
    if (pos[len] == '.' && pos[len + 1] != '.' && is_xid_start_next(pos + len + 1)) return NULL;
    else if (pos[len] == '.') len += 1 + span_of(pos + len + 1, "0123456789");
    else if (pos[len] != 'e' && pos[len] != 'f' && pos[len] != '%') return NULL;
    if (pos[len] == 'e') {
        len += 1;
        if (pos[len] == '-' || pos[len] == '+') len += 1;
        len += span_of(pos + len, "0123456789_");
    }
    char *buf = GC_MALLOC_ATOMIC(len + 1);
    memset(buf, 0, len + 1);
    for (char *src = (char *)pos, *dest = buf; src < pos + len; ++src) {
        if (*src != '_') *(dest++) = *src;
    }
    pos += len;

    int suffix = NUM_PLAIN;
    if (match(&pos, "%")) suffix = NUM_PERCENT;
    else if (match(&pos, "deg")) suffix = NUM_DEGREES;

    return NewAST(ctx->file, start, pos, Num, .n = num_literal_value(ctx, start, buf, suffix), .str = buf,
                  .suffix = suffix);
}

// Fold a leading `-` into a numeric literal, so `-128` is a single Int literal
// rather than a negation applied to `128`: only a literal can be compiled
// straight to a sized target, so `Int8(-128)` fits where negating an `Int8(128)`
// would not. Returns NULL when there's no literal for the sign to fold into --
// `-(x + 1)` is a negation, and so is `- -1`, whose literal is already signed.
ast_t *negate_literal(parse_ctx_t *ctx, const char *start, ast_t *literal) {
    switch (literal->tag) {
    case Int: {
        const char *str = Match(literal, Int)->str;
        if (str[0] == '-') return NULL;
        return NewAST(ctx->file, start, literal->end, Int, .str = String("-", str));
    }
    case Num: {
        DeclareMatch(num, literal, Num);
        if (num->str[0] == '-') return NULL;
        return NewAST(ctx->file, start, literal->end, Num, .n = number_neg(num->n), .str = String("-", num->str),
                      .suffix = num->suffix);
    }
    default: return NULL;
    }
}
