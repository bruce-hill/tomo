// This file defines how to compile Num literals

#include "../ast.h"
#include "../environment.h"
#include "../stdlib/datatypes.h"
#include "../stdlib/number.h"
#include "../stdlib/text.h"
#include "../types.h"
#include "../util.h"
#include "compilation.h"

// A Num literal's C representation, tightest first.
//
// The exact value was computed at parse time, so the common cases -- every
// literal whose reduced fraction fits 32 bits of numerator and 30 of
// denominator, which is nearly all of them -- become a NUMBER_SMALL immediate:
// a compile-time constant, no allocation, no runtime call. `0.5` is
// NUMBER_SMALL(1, 2), `50%` is NUMBER_SMALL(1, 2) as well.
//
// Anything else needs a real bigint or an irrational factor, neither of which
// fits in an immediate, so it falls back to reconstructing the value from the
// digits at runtime. That path rebuilds it from the source text rather than
// the parsed value, since a heap `number` has no constant-expression form.
public
Text_t compile_num(ast_t *ast) {
    DeclareMatch(num, ast, Num);
    // The immediate layout is public (see NUMBER_SMALL in number.h): tag 01 in
    // the low two bits, denominator in bits 2-31, signed numerator in the high
    // 32. A value carrying that tag is already reduced and in range.
    if ((num->n.bits & 0x3) == 0x1) {
        int32_t numerator = (int32_t)(num->n.bits >> 32);
        uint32_t denominator = (uint32_t)((num->n.bits >> 2) & NUMBER_SMALL_DEN_MAX);
        if (denominator == 1) {
            if (numerator == 0) return Text("NUMBER_ZERO");
            if (numerator == 1) return Text("NUMBER_ONE");
            if (numerator == -1) return Text("NUMBER_NEG_ONE");
        }
        return Texts("NUMBER_SMALL(", (int64_t)numerator, ", ", (int64_t)denominator, ")");
    }

    Text_t digits = Texts("number_from_decimal(\"", Text$from_str(num->str), "\")");
    switch (num->suffix) {
    case NUM_PERCENT: return Texts("number_div(", digits, ", NUMBER_SMALL(100, 1))");
    case NUM_DEGREES:
        return Texts("number_mul(", digits, ", number_div(number_pi(), NUMBER_SMALL(180, 1)))");
    default: return digits;
    }
}

