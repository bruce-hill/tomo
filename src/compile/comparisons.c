// This file defines how to compile comparisons

#include "../ast.h"
#include "../config.h"
#include "../environment.h"
#include "../stdlib/text.h"
#include "../typecheck.h"
#include "compilation.h"

static CONSTFUNC const char *comparison_operator(ast_e tag) {
    switch (tag) {
    case Equals: return "==";
    case NotEquals: return "!=";
    case LessThan: return "<";
    case LessThanOrEquals: return "<=";
    case GreaterThan: return ">";
    case GreaterThanOrEquals: return ">=";
    default: return NULL;
    }
}

Text_t compile_comparison(env_t *env, ast_t *ast) {
    switch (ast->tag) {
    case Equals:
    case NotEquals: {
        binary_operands_t binop = BINARY_OPERANDS(ast);

        type_t *lhs_t = get_type(env, binop.lhs);
        type_t *rhs_t = get_type(with_enum_scope(env, lhs_t), binop.rhs);
        type_t *operand_t;
        if (type_eq(lhs_t, rhs_t)) {
            operand_t = lhs_t;
        } else if (binop.lhs->tag == Int && is_numeric_type(rhs_t)) {
            operand_t = rhs_t;
        } else if (binop.rhs->tag == Int && is_numeric_type(lhs_t)) {
            operand_t = lhs_t;
        } else if (can_compile_to_type(with_enum_scope(env, lhs_t), binop.rhs, lhs_t)) {
            operand_t = lhs_t;
        } else if (can_compile_to_type(env, binop.lhs, rhs_t)) {
            operand_t = rhs_t;
        } else {
            code_err(ast, "I can't do comparisons between ", type_to_text(lhs_t), " and ", type_to_text(rhs_t));
        }

        Text_t lhs, rhs;
        lhs = compile_to_type(env, binop.lhs, operand_t);
        rhs = compile_to_type(env, binop.rhs, operand_t);

        switch (operand_t->tag) {
        case BigIntType:
            return Texts(ast->tag == Equals ? EMPTY_TEXT : Text("!"), "Int$equal_value(", lhs, ", ", rhs, ")");
        case BoolType:
        case ByteType:
        case IntType:
        case FloatType:
        case PointerType:
        case FunctionType: return Texts("(", lhs, ast->tag == Equals ? " == " : " != ", rhs, ")");
        default:
            return Texts(ast->tag == Equals ? EMPTY_TEXT : Text("!"), "generic_equal(stack(", lhs, "), stack(", rhs,
                         "), ", compile_type_info(operand_t), ")");
        }
    }
    case LessThan:
    case LessThanOrEquals:
    case GreaterThan:
    case GreaterThanOrEquals:
    case Compare: {
        binary_operands_t cmp = BINARY_OPERANDS(ast);

        type_t *lhs_t = get_type(env, cmp.lhs);
        type_t *rhs_t = get_type(env, cmp.rhs);
        type_t *operand_t;
        if (type_eq(lhs_t, rhs_t)) {
            operand_t = lhs_t;
        } else if (cmp.lhs->tag == Int && is_numeric_type(rhs_t)) {
            operand_t = rhs_t;
        } else if (cmp.rhs->tag == Int && is_numeric_type(lhs_t)) {
            operand_t = lhs_t;
        } else if (can_compile_to_type(env, cmp.rhs, lhs_t)) {
            operand_t = lhs_t;
        } else if (can_compile_to_type(env, cmp.lhs, rhs_t)) {
            operand_t = rhs_t;
        } else {
            code_err(ast, "I can't do comparisons between ", type_to_text(lhs_t), " and ", type_to_text(rhs_t));
        }

        Text_t lhs = compile_to_type(env, cmp.lhs, operand_t);
        Text_t rhs = compile_to_type(env, cmp.rhs, operand_t);

        if (ast->tag == Compare)
            return Texts("generic_compare(stack(", lhs, "), stack(", rhs, "), ", compile_type_info(operand_t), ")");

        const char *op = comparison_operator(ast->tag);
        switch (operand_t->tag) {
        case BigIntType: return Texts("(Int$compare_value(", lhs, ", ", rhs, ") ", op, " 0)");
        case BoolType:
        case ByteType:
        case IntType:
        case FloatType:
        case PointerType:
        case FunctionType: return Texts("(", lhs, " ", op, " ", rhs, ")");
        default:
            return Texts("(generic_compare(stack(", lhs, "), stack(", rhs, "), ", compile_type_info(operand_t), ") ",
                         op, " 0)");
        }
    }
    default: code_err(ast, "This is not a comparison!");
    }
}
