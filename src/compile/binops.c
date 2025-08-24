// This file defines how to compile binary operations

#include "../ast.h"
#include "../compile.h"
#include "../environment.h"
#include "../stdlib/datatypes.h"
#include "../stdlib/text.h"
#include "../stdlib/util.h"
#include "../typecheck.h"
#include "../types.h"
#include "assignments.h"
#include "functions.h"
#include "optionals.h"
#include "promotions.h"
#include "statements.h"
#include "types.h"

static PUREFUNC Text_t compile_unsigned_type(type_t *t) {
    if (t->tag != IntType) errx(1, "Not an int type, so unsigned doesn't make sense!");
    switch (Match(t, IntType)->bits) {
    case TYPE_IBITS8: return Text("uint8_t");
    case TYPE_IBITS16: return Text("uint16_t");
    case TYPE_IBITS32: return Text("uint32_t");
    case TYPE_IBITS64: return Text("uint64_t");
    default: errx(1, "Invalid integer bit size");
    }
    return EMPTY_TEXT;
}

public
Text_t compile_binary_op(env_t *env, ast_t *ast) {
    binary_operands_t binop = BINARY_OPERANDS(ast);
    type_t *lhs_t = get_type(env, binop.lhs);
    type_t *rhs_t = get_type(env, binop.rhs);
    type_t *overall_t = get_type(env, ast);

    binding_t *b = get_metamethod_binding(env, ast->tag, binop.lhs, binop.rhs, overall_t);
    if (!b) b = get_metamethod_binding(env, ast->tag, binop.rhs, binop.lhs, overall_t);
    if (b) {
        arg_ast_t *args = new (arg_ast_t, .value = binop.lhs, .next = new (arg_ast_t, .value = binop.rhs));
        DeclareMatch(fn, b->type, FunctionType);
        return Texts(b->code, "(", compile_arguments(env, ast, fn->args, args), ")");
    }

    if (ast->tag == Multiply && is_numeric_type(lhs_t)) {
        b = get_namespace_binding(env, binop.rhs, "scaled_by");
        if (b && b->type->tag == FunctionType) {
            DeclareMatch(fn, b->type, FunctionType);
            if (type_eq(fn->ret, rhs_t)) {
                arg_ast_t *args = new (arg_ast_t, .value = binop.rhs, .next = new (arg_ast_t, .value = binop.lhs));
                if (is_valid_call(env, fn->args, args, (call_opts_t){.promotion = true}))
                    return Texts(b->code, "(", compile_arguments(env, ast, fn->args, args), ")");
            }
        }
    } else if (ast->tag == Multiply && is_numeric_type(rhs_t)) {
        b = get_namespace_binding(env, binop.lhs, "scaled_by");
        if (b && b->type->tag == FunctionType) {
            DeclareMatch(fn, b->type, FunctionType);
            if (type_eq(fn->ret, lhs_t)) {
                arg_ast_t *args = new (arg_ast_t, .value = binop.lhs, .next = new (arg_ast_t, .value = binop.rhs));
                if (is_valid_call(env, fn->args, args, (call_opts_t){.promotion = true}))
                    return Texts(b->code, "(", compile_arguments(env, ast, fn->args, args), ")");
            }
        }
    } else if (ast->tag == Divide && is_numeric_type(rhs_t)) {
        b = get_namespace_binding(env, binop.lhs, "divided_by");
        if (b && b->type->tag == FunctionType) {
            DeclareMatch(fn, b->type, FunctionType);
            if (type_eq(fn->ret, lhs_t)) {
                arg_ast_t *args = new (arg_ast_t, .value = binop.lhs, .next = new (arg_ast_t, .value = binop.rhs));
                if (is_valid_call(env, fn->args, args, (call_opts_t){.promotion = true}))
                    return Texts(b->code, "(", compile_arguments(env, ast, fn->args, args), ")");
            }
        }
    } else if ((ast->tag == Divide || ast->tag == Mod || ast->tag == Mod1) && is_numeric_type(rhs_t)) {
        b = get_namespace_binding(env, binop.lhs, binop_method_name(ast->tag));
        if (b && b->type->tag == FunctionType) {
            DeclareMatch(fn, b->type, FunctionType);
            if (type_eq(fn->ret, lhs_t)) {
                arg_ast_t *args = new (arg_ast_t, .value = binop.lhs, .next = new (arg_ast_t, .value = binop.rhs));
                if (is_valid_call(env, fn->args, args, (call_opts_t){.promotion = true}))
                    return Texts(b->code, "(", compile_arguments(env, ast, fn->args, args), ")");
            }
        }
    }

    if (ast->tag == Or && lhs_t->tag == OptionalType) {
        if (rhs_t->tag == AbortType || rhs_t->tag == ReturnType) {
            return Texts("({ ", compile_declaration(lhs_t, Text("lhs")), " = ", compile(env, binop.lhs), "; ", "if (",
                         check_none(lhs_t, Text("lhs")), ") ", compile_statement(env, binop.rhs), " ",
                         optional_into_nonnone(lhs_t, Text("lhs")), "; })");
        }

        if (is_incomplete_type(rhs_t)) {
            type_t *complete = most_complete_type(rhs_t, Match(lhs_t, OptionalType)->type);
            if (complete == NULL)
                code_err(binop.rhs, "I don't know how to convert a ", type_to_str(rhs_t), " to a ",
                         type_to_str(Match(lhs_t, OptionalType)->type));
            rhs_t = complete;
        }

        if (rhs_t->tag == OptionalType && type_eq(lhs_t, rhs_t)) {
            return Texts("({ ", compile_declaration(lhs_t, Text("lhs")), " = ", compile(env, binop.lhs), "; ",
                         check_none(lhs_t, Text("lhs")), " ? ", compile(env, binop.rhs), " : lhs; })");
        } else if (rhs_t->tag != OptionalType && type_eq(Match(lhs_t, OptionalType)->type, rhs_t)) {
            return Texts("({ ", compile_declaration(lhs_t, Text("lhs")), " = ", compile(env, binop.lhs), "; ",
                         check_none(lhs_t, Text("lhs")), " ? ", compile(env, binop.rhs), " : ",
                         optional_into_nonnone(lhs_t, Text("lhs")), "; })");
        } else if (rhs_t->tag == BoolType) {
            return Texts("((!", check_none(lhs_t, compile(env, binop.lhs)), ") || ", compile(env, binop.rhs), ")");
        } else {
            code_err(ast, "I don't know how to do an 'or' operation between ", type_to_str(lhs_t), " and ",
                     type_to_str(rhs_t));
        }
    }

    Text_t lhs = compile_to_type(env, binop.lhs, overall_t);
    Text_t rhs = compile_to_type(env, binop.rhs, overall_t);

    switch (ast->tag) {
    case Power: {
        if (overall_t->tag != NumType)
            code_err(ast, "Exponentiation is only supported for Num types, not ", type_to_str(overall_t));
        if (overall_t->tag == NumType && Match(overall_t, NumType)->bits == TYPE_NBITS32)
            return Texts("powf(", lhs, ", ", rhs, ")");
        else return Texts("pow(", lhs, ", ", rhs, ")");
    }
    case Multiply: {
        if (overall_t->tag != IntType && overall_t->tag != NumType && overall_t->tag != ByteType)
            code_err(ast,
                     "Math operations are only supported for values of the same "
                     "numeric type, not ",
                     type_to_str(lhs_t), " and ", type_to_str(rhs_t));
        return Texts("(", lhs, " * ", rhs, ")");
    }
    case Divide: {
        if (overall_t->tag != IntType && overall_t->tag != NumType && overall_t->tag != ByteType)
            code_err(ast,
                     "Math operations are only supported for values of the same "
                     "numeric type, not ",
                     type_to_str(lhs_t), " and ", type_to_str(rhs_t));
        return Texts("(", lhs, " / ", rhs, ")");
    }
    case Mod: {
        if (overall_t->tag != IntType && overall_t->tag != NumType && overall_t->tag != ByteType)
            code_err(ast,
                     "Math operations are only supported for values of the same "
                     "numeric type, not ",
                     type_to_str(lhs_t), " and ", type_to_str(rhs_t));
        return Texts("(", lhs, " % ", rhs, ")");
    }
    case Mod1: {
        if (overall_t->tag != IntType && overall_t->tag != NumType && overall_t->tag != ByteType)
            code_err(ast,
                     "Math operations are only supported for values of the same "
                     "numeric type, not ",
                     type_to_str(lhs_t), " and ", type_to_str(rhs_t));
        return Texts("((((", lhs, ")-1) % (", rhs, ")) + 1)");
    }
    case Plus: {
        if (overall_t->tag != IntType && overall_t->tag != NumType && overall_t->tag != ByteType)
            code_err(ast,
                     "Math operations are only supported for values of the same "
                     "numeric type, not ",
                     type_to_str(lhs_t), " and ", type_to_str(rhs_t));
        return Texts("(", lhs, " + ", rhs, ")");
    }
    case Minus: {
        if (overall_t->tag == SetType)
            return Texts("Table$without(", lhs, ", ", rhs, ", ", compile_type_info(overall_t), ")");
        if (overall_t->tag != IntType && overall_t->tag != NumType && overall_t->tag != ByteType)
            code_err(ast,
                     "Math operations are only supported for values of the same "
                     "numeric type, not ",
                     type_to_str(lhs_t), " and ", type_to_str(rhs_t));
        return Texts("(", lhs, " - ", rhs, ")");
    }
    case LeftShift: {
        if (overall_t->tag != IntType && overall_t->tag != NumType && overall_t->tag != ByteType)
            code_err(ast,
                     "Math operations are only supported for values of the same "
                     "numeric type, not ",
                     type_to_str(lhs_t), " and ", type_to_str(rhs_t));
        return Texts("(", lhs, " << ", rhs, ")");
    }
    case RightShift: {
        if (overall_t->tag != IntType && overall_t->tag != NumType && overall_t->tag != ByteType)
            code_err(ast,
                     "Math operations are only supported for values of the same "
                     "numeric type, not ",
                     type_to_str(lhs_t), " and ", type_to_str(rhs_t));
        return Texts("(", lhs, " >> ", rhs, ")");
    }
    case UnsignedLeftShift: {
        if (overall_t->tag != IntType && overall_t->tag != NumType && overall_t->tag != ByteType)
            code_err(ast,
                     "Math operations are only supported for values of the same "
                     "numeric type, not ",
                     type_to_str(lhs_t), " and ", type_to_str(rhs_t));
        return Texts("(", compile_type(overall_t), ")((", compile_unsigned_type(lhs_t), ")", lhs, " << ", rhs, ")");
    }
    case UnsignedRightShift: {
        if (overall_t->tag != IntType && overall_t->tag != NumType && overall_t->tag != ByteType)
            code_err(ast,
                     "Math operations are only supported for values of the same "
                     "numeric type, not ",
                     type_to_str(lhs_t), " and ", type_to_str(rhs_t));
        return Texts("(", compile_type(overall_t), ")((", compile_unsigned_type(lhs_t), ")", lhs, " >> ", rhs, ")");
    }
    case And: {
        if (overall_t->tag == BoolType) return Texts("(", lhs, " && ", rhs, ")");
        else if (overall_t->tag == IntType || overall_t->tag == ByteType) return Texts("(", lhs, " & ", rhs, ")");
        else if (overall_t->tag == SetType)
            return Texts("Table$overlap(", lhs, ", ", rhs, ", ", compile_type_info(overall_t), ")");
        else
            code_err(ast, "The 'and' operator isn't supported between ", type_to_str(lhs_t), " and ",
                     type_to_str(rhs_t), " values");
    }
    case Compare: {
        return Texts("generic_compare(stack(", lhs, "), stack(", rhs, "), ", compile_type_info(overall_t), ")");
    }
    case Or: {
        if (overall_t->tag == BoolType) {
            return Texts("(", lhs, " || ", rhs, ")");
        } else if (overall_t->tag == IntType || overall_t->tag == ByteType) {
            return Texts("(", lhs, " | ", rhs, ")");
        } else if (overall_t->tag == SetType) {
            return Texts("Table$with(", lhs, ", ", rhs, ", ", compile_type_info(overall_t), ")");
        } else {
            code_err(ast, "The 'or' operator isn't supported between ", type_to_str(lhs_t), " and ", type_to_str(rhs_t),
                     " values");
        }
    }
    case Xor: {
        // TODO: support optional values in `xor` expressions
        if (overall_t->tag == BoolType || overall_t->tag == IntType || overall_t->tag == ByteType)
            return Texts("(", lhs, " ^ ", rhs, ")");
        else if (overall_t->tag == SetType)
            return Texts("Table$xor(", lhs, ", ", rhs, ", ", compile_type_info(overall_t), ")");
        else
            code_err(ast, "The 'xor' operator isn't supported between ", type_to_str(lhs_t), " and ",
                     type_to_str(rhs_t), " values");
    }
    case Concat: {
        if (overall_t == PATH_TYPE) return Texts("Path$concat(", lhs, ", ", rhs, ")");
        switch (overall_t->tag) {
        case TextType: {
            return Texts("Text$concat(", lhs, ", ", rhs, ")");
        }
        case ListType: {
            return Texts("List$concat(", lhs, ", ", rhs, ", sizeof(",
                         compile_type(Match(overall_t, ListType)->item_type), "))");
        }
        default:
            code_err(ast, "Concatenation isn't supported between ", type_to_str(lhs_t), " and ", type_to_str(rhs_t),
                     " values");
        }
    }
    default: errx(1, "Not a valid binary operation: %s", ast_to_sexp_str(ast));
    }
    return EMPTY_TEXT;
}
