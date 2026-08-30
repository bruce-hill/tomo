// This file defines how to compile integers

#include <gmp.h>

#include "../ast.h"
#include "../environment.h"
#include "../stdlib/datatypes.h"
#include "../stdlib/integers.h"
#include "../stdlib/number.h"
#include "../stdlib/text.h"
#include "../typecheck.h"
#include "../types.h"
#include "../util.h"
#include "compilation.h"

// Load an integer value into an mpz_t, which is what the range checks here
// (and the loop bound in compile/loops.c) are written against.
public
void mpz_init_int(mpz_t out, Int_t i) {
    if likely (i.small & 1L) mpz_init_set_si(out, i.small >> 2L);
    else mpz_init_set(out, i.big);
}

public
Text_t compile_int_to_type(env_t *env, ast_t *ast, type_t *target) {
    if (ast->tag != Int) {
        Text_t code = compile(env, ast);
        type_t *actual_type = get_type(env, ast);
        if (!promote(env, ast, &code, actual_type, target))
            code_err(ast, "I couldn't promote this ", type_to_text(actual_type), " to a ", type_to_text(target));
        return code;
    }

    if (non_optional(target)->tag == BigIntType) return compile(env, ast);

    if (target->tag == OptionalType && Match(target, OptionalType)->type) {
        return promote_to_optional(Match(target, OptionalType)->type,
                                   compile_int_to_type(env, ast, Match(target, OptionalType)->type));
    }

    mpz_t i;
    mpz_init_int(i, Match(ast, Int)->i);

    char *c_literal;
    gmp_asprintf(&c_literal, "%#Zd", i);

    if (target->tag == ByteType) {
        if (mpz_cmp_si(i, UINT8_MAX) <= 0 && mpz_cmp_si(i, 0) >= 0) return Texts("(Byte_t)(", c_literal, ")");
        code_err(ast, "This integer cannot fit in a byte");
    } else if (target->tag == NumType) {
        // An integer is always an exact Num, so this only ever loses the
        // literal's base: `0xFF` becomes 255. Big ones fall back to a runtime
        // constructor, since only the immediate tier is a C constant.
        char *decimal = mpz_get_str(NULL, 10, i);
        return compile_num_value(number_from_decimal(decimal),
                                 Texts("number_from_decimal(\"", decimal, "\")"));
    } else if (target->tag == FloatType) {
        if (Match(target, FloatType)->bits == TYPE_NBITS64) {
            return Texts("N64(", c_literal, ")");
        } else {
            return Texts("N32(", c_literal, ")");
        }
    } else if (target->tag == IntType) {
        int64_t target_bits = (int64_t)Match(target, IntType)->bits;
        switch (target_bits) {
        case TYPE_IBITS64:
            if (mpz_cmp_si(i, INT64_MIN) == 0) return Text("I64(INT64_MIN)");
            if (mpz_cmp_si(i, INT64_MAX) <= 0 && mpz_cmp_si(i, INT64_MIN) >= 0) return Texts("I64(", c_literal, "L)");
            break;
        case TYPE_IBITS32:
            if (mpz_cmp_si(i, INT32_MAX) <= 0 && mpz_cmp_si(i, INT32_MIN) >= 0) return Texts("I32(", c_literal, ")");
            break;
        case TYPE_IBITS16:
            if (mpz_cmp_si(i, INT16_MAX) <= 0 && mpz_cmp_si(i, INT16_MIN) >= 0) return Texts("I16(", c_literal, ")");
            break;
        case TYPE_IBITS8:
            if (mpz_cmp_si(i, INT8_MAX) <= 0 && mpz_cmp_si(i, INT8_MIN) >= 0) return Texts("I8(", c_literal, ")");
            break;
        default: break;
        }
        code_err(ast, "This integer cannot fit in a ", target_bits, "-bit value");
    } else {
        code_err(ast, "I don't know how to compile this to a ", type_to_text(target));
    }
    return EMPTY_TEXT;
}

public
Text_t compile_int(ast_t *ast) {
    mpz_t i;
    mpz_init_int(i, Match(ast, Int)->i);
    char *str;
    gmp_asprintf(&str, "%Zd", i);

    if (mpz_cmpabs_ui(i, BIGGEST_SMALL_INT) <= 0) {
        return Texts("I_small(", str, ")");
    } else if (mpz_cmp_si(i, INT64_MAX) <= 0 && mpz_cmp_si(i, INT64_MIN) >= 0) {
        return Texts("Int$from_int64(", str, ")");
    } else {
        return Texts("Int$from_str(\"", str, "\")");
    }
}
