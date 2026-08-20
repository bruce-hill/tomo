// This file defines how to compile Num literals

#include "../ast.h"
#include "../environment.h"
#include "../stdlib/datatypes.h"
#include "../stdlib/number.h"
#include "../stdlib/text.h"
#include "../typecheck.h"
#include "../types.h"
#include "../util.h"
#include "compilation.h"

// The tightest C representation of an exact value known at compile time.
//
// Any value whose reduced fraction fits the immediate tier -- 32 bits of
// numerator, 30 of denominator, which covers nearly every literal anyone
// writes -- becomes a NUMBER_SMALL constant expression: no allocation, no
// runtime call, nothing but a 64-bit load. `0.5` is NUMBER_SMALL(1, 2).
//
// Anything else needs a bigint or an irrational factor, neither of which has a
// constant-expression form, so the caller supplies an expression that rebuilds
// the value at runtime instead.
public
Text_t compile_num_value(Num_t n, Text_t fallback) {
    // The immediate layout is public (see NUMBER_SMALL in number.h): tag 01 in
    // the low two bits, denominator in bits 2-31, signed numerator in the high
    // 32. A value carrying that tag is already reduced and in range.
    if ((n.bits & 0x3) != 0x1) return fallback;

    int32_t numerator = (int32_t)(n.bits >> 32);
    uint32_t denominator = (uint32_t)((n.bits >> 2) & NUMBER_SMALL_DEN_MAX);
    if (denominator == 1) {
        if (numerator == 0) return Text("NUMBER_ZERO");
        if (numerator == 1) return Text("NUMBER_ONE");
        if (numerator == -1) return Text("NUMBER_NEG_ONE");
    }
    return Texts("NUMBER_SMALL(", (int64_t)numerator, ", ", (int64_t)denominator, ")");
}

// A numeric literal compiled into a specific target type. Only the Num case
// keeps the exact value; a Float64/Float32 target rounds it, which is the
// point of asking for one.
public
Text_t compile_num_to_type(env_t *env, ast_t *ast, type_t *target) {
    if (ast->tag != Num) {
        Text_t code = compile(env, ast);
        type_t *actual_type = get_type(env, ast);
        if (!promote(env, ast, &code, actual_type, target))
            code_err(ast, "I couldn't promote this ", type_to_text(actual_type), " to a ", type_to_text(target));
        return code;
    }

    if (target->tag == OptionalType && Match(target, OptionalType)->type) {
        return promote_to_optional(Match(target, OptionalType)->type,
                                   compile_num_to_type(env, ast, Match(target, OptionalType)->type));
    }

    switch (target->tag) {
    case NumType: return compile_num(ast);
    case FloatType: {
        double n = num_literal_double(ast);
        switch (Match(target, FloatType)->bits) {
        case TYPE_NBITS64: return Text$from_str(String(hex_double(n)));
        case TYPE_NBITS32: return Text$from_str(String(hex_double(n), "f"));
        default: code_err(ast, "This is not a valid number bit width");
        }
    }
    default: code_err(ast, "I don't know how to compile this to a ", type_to_text(target));
    }
    return EMPTY_TEXT;
}

public
Text_t compile_num(ast_t *ast) {
    DeclareMatch(num, ast, Num);
    // The fallback rebuilds the value from the source digits rather than from
    // the parsed one, since a heap `number` can't be written as a constant.
    Text_t digits = Texts("number_from_decimal(\"", Text$from_str(num->str), "\")");
    Text_t fallback;
    switch (num->suffix) {
    case NUM_PERCENT: fallback = Texts("number_div(", digits, ", NUMBER_SMALL(100, 1))"); break;
    case NUM_DEGREES:
        fallback = Texts("number_mul(", digits, ", number_div(number_pi(), NUMBER_SMALL(180, 1)))");
        break;
    default: fallback = digits; break;
    }
    return compile_num_value(num->n, fallback);
}
