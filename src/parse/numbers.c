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

ast_t *parse_int(parse_ctx_t *ctx, const char *pos) {
    const char *start = pos;
    size_t len = 0;
    if (start[len] == '-') len += 1;

    if (!isdigit(start[len])) return NULL;

    if (start[len] == '0') {
        if (start[len + 1] == 'x' || start[len + 1] == 'X') { // Hex
            len += 2;
            len += strspn(start + len, "0123456789abcdefABCDEF_");
        } else if (start[len + 1] == 'b' || start[len + 1] == 'B') { // Binary
            len += 2;
            len += strspn(start + len, "01_");
        } else if (start[len + 1] == 'o' || start[len + 1] == 'O') { // Octal
            len += 2;
            len += strspn(start + len, "01234567_");
        } else { // Decimal
            len += strspn(start + len, "0123456789_");
        }
    } else { // Decimal
        size_t digits = strspn(start + len, "0123456789_");
        if (digits <= 0) {
            return NULL;
        }
        len += digits;
    }

    // Rational literals: 1.2, 1e2, 1E2, 1%, 1deg
    if (start[len] == '.' || start[len] == 'e' || start[len] == 'E' || start[len] == '%'
        || strncmp(start + len, "deg", 3) == 0) {
        return NULL;
    }

    Text_t text = Text$from_strn(start, len);
    OptionalInt_t i = Int$parse(text, NONE_INT, NULL);
    assert(i.small != 0);
    return NewAST(ctx->file, start, start + len, Integer, .i = i);
}

ast_t *parse_num(parse_ctx_t *ctx, const char *pos) {
    const char *start = pos;
    if (!isdigit(start[0]) && start[0] != '.') return NULL;
    else if (start[0] == '.' && !isdigit(start[1])) return NULL;

    size_t len = 0;
    if (pos[len] == '-') len += 1;
    size_t pre_digits = strspn(pos, "0123456789_"), post_digits = 0;
    len += pre_digits;
    if (pos[len] == '.') {
        len += 1;
        post_digits = strspn(pos + len, "0123456789_");
        len += post_digits;
    }

    // No digits, no number
    if (pre_digits <= 0 && post_digits <= 0) return NULL;

    if (pos[len] == 'e' || pos[len] == 'E') {
        len += 1;
        if (pos[len] == '-') len += 1;
        len += strspn(pos + len, "0123456789_");
    }

    Text_t text = Text$from_strn(start, len);

    Text_t remainder;
    OptionalReal_t real = Real$parse(text, &remainder);
    if (Real$is_none(&real, &Real$info)) return NULL;

    pos += len;

    if (match(&pos, "%")) {
        // The '%' percentage suffix means "divide by 100"
        if (!Real$is_zero(real)) real = Real$divided_by(real, Real$from_float64(100.));
    } else if (match(&pos, "deg")) {
        // The 'deg' suffix means convert from degrees to radians (i.e. 90deg => (90/360)*(2*pi))
        if (!Real$is_zero(real)) real = Real$times(Real$divided_by(real, Real$from_float64(360.)), Real$tau);
    }

    if (Real$tag(real) == REAL_TAG_SYMBOLIC && REAL_SYMBOLIC(real)->op == SYM_INVALID) {
        parser_err(ctx, start, pos, "Failed to convert this to a real number");
    }

    return NewAST(ctx->file, start, pos, Number, .n = real);
}
