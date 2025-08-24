// This file defines how to compile integers

#include <gmp.h>

#include "../ast.h"
#include "../compile.h"
#include "../environment.h"
#include "../stdlib/datatypes.h"
#include "../stdlib/integers.h"
#include "../stdlib/text.h"
#include "../stdlib/util.h"
#include "../typecheck.h"
#include "../types.h"
#include "promotions.h"

public
Text_t compile_int_to_type(env_t *env, ast_t *ast, type_t *target) {
    if (ast->tag != Int) {
        Text_t code = compile(env, ast);
        type_t *actual_type = get_type(env, ast);
        if (!promote(env, ast, &code, actual_type, target))
            code_err(ast, "I couldn't promote this ", type_to_str(actual_type), " to a ", type_to_str(target));
        return code;
    }

    if (target->tag == BigIntType) return compile(env, ast);

    if (target->tag == OptionalType && Match(target, OptionalType)->type)
        return compile_int_to_type(env, ast, Match(target, OptionalType)->type);

    const char *literal = Match(ast, Int)->str;
    OptionalInt_t int_val = Int$from_str(literal);
    if (int_val.small == 0) code_err(ast, "Failed to parse this integer");

    mpz_t i;
    mpz_init_set_int(i, int_val);

    char *c_literal;
    if (strncmp(literal, "0x", 2) == 0 || strncmp(literal, "0X", 2) == 0 || strncmp(literal, "0b", 2) == 0) {
        gmp_asprintf(&c_literal, "0x%ZX", i);
    } else if (strncmp(literal, "0o", 2) == 0) {
        gmp_asprintf(&c_literal, "%#Zo", i);
    } else {
        gmp_asprintf(&c_literal, "%#Zd", i);
    }

    if (target->tag == ByteType) {
        if (mpz_cmp_si(i, UINT8_MAX) <= 0 && mpz_cmp_si(i, 0) >= 0) return Texts("(Byte_t)(", c_literal, ")");
        code_err(ast, "This integer cannot fit in a byte");
    } else if (target->tag == NumType) {
        if (Match(target, NumType)->bits == TYPE_NBITS64) {
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
        code_err(ast, "I don't know how to compile this to a ", type_to_str(target));
    }
    return EMPTY_TEXT;
}

public
Text_t compile_int(ast_t *ast) {
    const char *str = Match(ast, Int)->str;
    OptionalInt_t int_val = Int$from_str(str);
    if (int_val.small == 0) code_err(ast, "Failed to parse this integer");
    mpz_t i;
    mpz_init_set_int(i, int_val);
    if (mpz_cmpabs_ui(i, BIGGEST_SMALL_INT) <= 0) {
        return Texts("I_small(", str, ")");
    } else if (mpz_cmp_si(i, INT64_MAX) <= 0 && mpz_cmp_si(i, INT64_MIN) >= 0) {
        return Texts("Int$from_int64(", str, ")");
    } else {
        return Texts("Int$from_str(\"", str, "\")");
    }
}
