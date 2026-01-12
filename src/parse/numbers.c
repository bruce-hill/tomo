// Logic for parsing numbers

#include <ctype.h>
#include <gc.h>
#include <stdarg.h>
#include <stdbool.h>
#include <string.h>

#include <unictype.h>
#include <uniname.h>

#include "../ast.h"
#include "../stdlib/datatypes.h"
#include "../stdlib/integers.h"
#include "../stdlib/optionals.h"
#include "../stdlib/reals.h"
#include "../stdlib/text.h"
#include "context.h"
#include "errors.h"
#include "numbers.h"
#include "utils.h"

static const double RADIANS_PER_DEGREE = 0.0174532925199432957692369076848861271344287188854172545609719144;

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

    if (match(&pos, "%")) {
        return parse_num(ctx, start);
    } else if (match(&pos, "deg")) {
        return parse_num(ctx, start);
    }

    Text_t text = Text$from_strn(start, (size_t)(pos - start));
    Text_t remainder;
    OptionalInt_t i = Int$parse(text, NONE_INT, &remainder);
    assert(i.small != 0);
    return NewAST(ctx->file, start, pos, Integer, .i = i);
}

ast_t *parse_num(parse_ctx_t *ctx, const char *pos) {
    const char *start = pos;
    bool negative = match(&pos, "-");
    if (!isdigit(*pos) && *pos != '.') return NULL;
    else if (*pos == '.' && !isdigit(pos[1])) return NULL;

    size_t len = strspn(pos, "0123456789_");
    if (strncmp(pos + len, "..", 2) == 0) return NULL;
    else if (pos[len] == '.') len += 1 + strspn(pos + len + 1, "0123456789");
    else if (pos[len] != 'e' && pos[len] != 'f' && pos[len] != '%') return NULL;
    if (pos[len] == 'e') {
        len += 1;
        if (pos[len] == '-') len += 1;
        len += strspn(pos + len, "0123456789_");
    }
    char *buf = GC_MALLOC_ATOMIC(len + 1);
    memset(buf, 0, len + 1);
    for (char *src = (char *)pos, *dest = buf; src < pos + len; ++src) {
        if (*src != '_') *(dest++) = *src;
    }
    double d = strtod(buf, NULL);
    pos += len;

    if (negative) d *= -1;

    if (match(&pos, "%")) d /= 100.;
    else if (match(&pos, "deg")) d *= RADIANS_PER_DEGREE;

    Text_t text = Text$from_strn(start, (size_t)(pos - start));
    Text_t remainder;
    OptionalReal_t real = Real$parse(text, &remainder);
    if (Real$is_none(&real, &Real$info)) return NULL;
    return NewAST(ctx->file, start, pos, Number, .n = real);
}
