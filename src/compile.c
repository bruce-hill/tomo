// Compilation logic
#include <gc.h>
#include <glob.h>
#include <gmp.h>
#include <stdio.h>
#include <uninorm.h>

#include "ast.h"
#include "compile.h"
#include "compile/assignments.h"
#include "compile/enums.h"
#include "compile/functions.h"
#include "compile/integers.h"
#include "compile/lists.h"
#include "compile/optionals.h"
#include "compile/pointers.h"
#include "compile/promotion.h"
#include "compile/sets.h"
#include "compile/statements.h"
#include "compile/structs.h"
#include "compile/tables.h"
#include "compile/text.h"
#include "compile/types.h"
#include "config.h"
#include "environment.h"
#include "modules.h"
#include "naming.h"
#include "stdlib/integers.h"
#include "stdlib/paths.h"
#include "stdlib/tables.h"
#include "stdlib/text.h"
#include "stdlib/util.h"
#include "typecheck.h"

static Text_t compile_unsigned_type(type_t *t);

public
Text_t with_source_info(env_t *env, ast_t *ast, Text_t code) {
    if (code.length == 0 || !ast || !ast->file || !env->do_source_mapping) return code;
    int64_t line = get_line_number(ast->file, ast->start);
    return Texts("\n#line ", String(line), "\n", code);
}

public
Text_t compile_maybe_incref(env_t *env, ast_t *ast, type_t *t) {
    if (is_idempotent(ast) && can_be_mutated(env, ast)) {
        if (t->tag == ListType) return Texts("LIST_COPY(", compile_to_type(env, ast, t), ")");
        else if (t->tag == TableType || t->tag == SetType)
            return Texts("TABLE_COPY(", compile_to_type(env, ast, t), ")");
    }
    return compile_to_type(env, ast, t);
}

static Text_t compile_binary_op(env_t *env, ast_t *ast) {
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

PUREFUNC Text_t compile_unsigned_type(type_t *t) {
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
Text_t compile_empty(type_t *t) {
    if (t == NULL) compiler_err(NULL, NULL, NULL, "I can't compile a value with no type");

    if (t->tag == OptionalType) return compile_none(t);

    if (t == PATH_TYPE) return Text("NONE_PATH");
    else if (t == PATH_TYPE_TYPE) return Text("((OptionalPathType_t){})");

    switch (t->tag) {
    case BigIntType: return Text("I(0)");
    case IntType: {
        switch (Match(t, IntType)->bits) {
        case TYPE_IBITS8: return Text("I8(0)");
        case TYPE_IBITS16: return Text("I16(0)");
        case TYPE_IBITS32: return Text("I32(0)");
        case TYPE_IBITS64: return Text("I64(0)");
        default: errx(1, "Invalid integer bit size");
        }
        break;
    }
    case ByteType: return Text("((Byte_t)0)");
    case BoolType: return Text("((Bool_t)no)");
    case ListType: return Text("((List_t){})");
    case TableType:
    case SetType: return Text("((Table_t){})");
    case TextType: return Text("Text(\"\")");
    case CStringType: return Text("\"\"");
    case PointerType: {
        DeclareMatch(ptr, t, PointerType);
        Text_t empty_pointed = compile_empty(ptr->pointed);
        return empty_pointed.length == 0 ? EMPTY_TEXT
                                         : Texts(ptr->is_stack ? Text("stack(") : Text("heap("), empty_pointed, ")");
    }
    case NumType: {
        return Match(t, NumType)->bits == TYPE_NBITS32 ? Text("N32(0.0f)") : Text("N64(0.0)");
    }
    case StructType: return compile_empty_struct(t);
    case EnumType: return compile_empty_enum(t);
    default: return EMPTY_TEXT;
    }
    return EMPTY_TEXT;
}

Text_t compile(env_t *env, ast_t *ast) {
    switch (ast->tag) {
    case None: {
        code_err(ast, "I can't figure out what this `none`'s type is!");
    }
    case Bool: return Match(ast, Bool)->b ? Text("yes") : Text("no");
    case Var: {
        binding_t *b = get_binding(env, Match(ast, Var)->name);
        if (b) return b->code.length > 0 ? b->code : Texts("_$", Match(ast, Var)->name);
        // return Texts("_$", Match(ast, Var)->name);
        code_err(ast, "I don't know of any variable by this name");
    }
    case Int: {
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
    case Num: {
        return Text$from_str(String(hex_double(Match(ast, Num)->n)));
    }
    case Not: {
        ast_t *value = Match(ast, Not)->value;
        type_t *t = get_type(env, value);

        binding_t *b = get_namespace_binding(env, value, "negated");
        if (b && b->type->tag == FunctionType) {
            DeclareMatch(fn, b->type, FunctionType);
            if (fn->args && can_compile_to_type(env, value, get_arg_type(env, fn->args)))
                return Texts(b->code, "(", compile_arguments(env, ast, fn->args, new (arg_ast_t, .value = value)), ")");
        }

        if (t->tag == BoolType) return Texts("!(", compile(env, value), ")");
        else if (t->tag == IntType || t->tag == ByteType) return Texts("~(", compile(env, value), ")");
        else if (t->tag == ListType) return Texts("((", compile(env, value), ").length == 0)");
        else if (t->tag == SetType || t->tag == TableType)
            return Texts("((", compile(env, value), ").entries.length == 0)");
        else if (t->tag == TextType) return Texts("(", compile(env, value), ".length == 0)");
        else if (t->tag == OptionalType) return check_none(t, compile(env, value));

        code_err(ast, "I don't know how to negate values of type ", type_to_str(t));
    }
    case Negative: {
        ast_t *value = Match(ast, Negative)->value;
        type_t *t = get_type(env, value);
        binding_t *b = get_namespace_binding(env, value, "negative");
        if (b && b->type->tag == FunctionType) {
            DeclareMatch(fn, b->type, FunctionType);
            if (fn->args && can_compile_to_type(env, value, get_arg_type(env, fn->args)))
                return Texts(b->code, "(", compile_arguments(env, ast, fn->args, new (arg_ast_t, .value = value)), ")");
        }

        if (t->tag == IntType || t->tag == NumType) return Texts("-(", compile(env, value), ")");

        code_err(ast, "I don't know how to get the negative value of type ", type_to_str(t));
    }
    case HeapAllocate:
    case StackReference: {
        return compile_typed_allocation(env, ast, get_type(env, ast));
    }
    case Optional: {
        ast_t *value = Match(ast, Optional)->value;
        Text_t value_code = compile(env, value);
        return promote_to_optional(get_type(env, value), value_code);
    }
    case NonOptional: {
        ast_t *value = Match(ast, NonOptional)->value;
        type_t *t = get_type(env, value);
        Text_t value_code = compile(env, value);
        int64_t line = get_line_number(ast->file, ast->start);
        return Texts("({ ", compile_declaration(t, Text("opt")), " = ", value_code, "; ", "if unlikely (",
                     check_none(t, Text("opt")), ")\n", "#line ", String(line), "\n", "fail_source(",
                     quoted_str(ast->file->filename), ", ", String((int64_t)(value->start - value->file->text)), ", ",
                     String((int64_t)(value->end - value->file->text)), ", ",
                     "\"This was expected to be a value, but it's none\");\n", optional_into_nonnone(t, Text("opt")),
                     "; })");
    }
    case Power:
    case Multiply:
    case Divide:
    case Mod:
    case Mod1:
    case Plus:
    case Minus:
    case Concat:
    case LeftShift:
    case UnsignedLeftShift:
    case RightShift:
    case UnsignedRightShift:
    case And:
    case Or:
    case Xor: {
        return compile_binary_op(env, ast);
    }
    case Equals:
    case NotEquals: {
        binary_operands_t binop = BINARY_OPERANDS(ast);

        type_t *lhs_t = get_type(env, binop.lhs);
        type_t *rhs_t = get_type(env, binop.rhs);
        type_t *operand_t;
        if (binop.lhs->tag == Int && is_numeric_type(rhs_t)) {
            operand_t = rhs_t;
        } else if (binop.rhs->tag == Int && is_numeric_type(lhs_t)) {
            operand_t = lhs_t;
        } else if (can_compile_to_type(env, binop.rhs, lhs_t)) {
            operand_t = lhs_t;
        } else if (can_compile_to_type(env, binop.lhs, rhs_t)) {
            operand_t = rhs_t;
        } else {
            code_err(ast, "I can't do comparisons between ", type_to_str(lhs_t), " and ", type_to_str(rhs_t));
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
        case NumType:
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
        if (cmp.lhs->tag == Int && is_numeric_type(rhs_t)) {
            operand_t = rhs_t;
        } else if (cmp.rhs->tag == Int && is_numeric_type(lhs_t)) {
            operand_t = lhs_t;
        } else if (can_compile_to_type(env, cmp.rhs, lhs_t)) {
            operand_t = lhs_t;
        } else if (can_compile_to_type(env, cmp.lhs, rhs_t)) {
            operand_t = rhs_t;
        } else {
            code_err(ast, "I can't do comparisons between ", type_to_str(lhs_t), " and ", type_to_str(rhs_t));
        }

        Text_t lhs = compile_to_type(env, cmp.lhs, operand_t);
        Text_t rhs = compile_to_type(env, cmp.rhs, operand_t);

        if (ast->tag == Compare)
            return Texts("generic_compare(stack(", lhs, "), stack(", rhs, "), ", compile_type_info(operand_t), ")");

        const char *op = binop_operator(ast->tag);
        switch (operand_t->tag) {
        case BigIntType: return Texts("(Int$compare_value(", lhs, ", ", rhs, ") ", op, " 0)");
        case BoolType:
        case ByteType:
        case IntType:
        case NumType:
        case PointerType:
        case FunctionType: return Texts("(", lhs, " ", op, " ", rhs, ")");
        default:
            return Texts("(generic_compare(stack(", lhs, "), stack(", rhs, "), ", compile_type_info(operand_t), ") ",
                         op, " 0)");
        }
    }
    case TextLiteral:
    case TextJoin: return compile_text_ast(env, ast);
    case Path: {
        return Texts("Path(", compile_text_literal(Text$from_str(Match(ast, Path)->path)), ")");
    }
    case Block: {
        ast_list_t *stmts = Match(ast, Block)->statements;
        if (stmts && !stmts->next) return compile(env, stmts->ast);

        Text_t code = Text("({\n");
        deferral_t *prev_deferred = env->deferred;
        env = fresh_scope(env);
        for (ast_list_t *stmt = stmts; stmt; stmt = stmt->next)
            prebind_statement(env, stmt->ast);
        for (ast_list_t *stmt = stmts; stmt; stmt = stmt->next) {
            if (stmt->next) {
                code = Texts(code, compile_statement(env, stmt->ast), "\n");
            } else {
                // TODO: put defer after evaluating block expression
                for (deferral_t *deferred = env->deferred; deferred && deferred != prev_deferred;
                     deferred = deferred->next) {
                    code = Texts(code, compile_statement(deferred->defer_env, deferred->block));
                }
                code = Texts(code, compile(env, stmt->ast), ";\n");
            }
            bind_statement(env, stmt->ast);
        }

        return Texts(code, "})");
    }
    case Min:
    case Max: {
        type_t *t = get_type(env, ast);
        ast_t *key = ast->tag == Min ? Match(ast, Min)->key : Match(ast, Max)->key;
        ast_t *lhs = ast->tag == Min ? Match(ast, Min)->lhs : Match(ast, Max)->lhs;
        ast_t *rhs = ast->tag == Min ? Match(ast, Min)->rhs : Match(ast, Max)->rhs;
        const char *key_name = "$";
        if (key == NULL) key = FakeAST(Var, key_name);

        env_t *expr_env = fresh_scope(env);
        set_binding(expr_env, key_name, t, Text("ternary$lhs"));
        Text_t lhs_key = compile(expr_env, key);

        set_binding(expr_env, key_name, t, Text("ternary$rhs"));
        Text_t rhs_key = compile(expr_env, key);

        type_t *key_t = get_type(expr_env, key);
        Text_t comparison;
        if (key_t->tag == BigIntType)
            comparison =
                Texts("(Int$compare_value(", lhs_key, ", ", rhs_key, ")", (ast->tag == Min ? "<=" : ">="), "0)");
        else if (key_t->tag == IntType || key_t->tag == NumType || key_t->tag == BoolType || key_t->tag == PointerType
                 || key_t->tag == ByteType)
            comparison = Texts("((", lhs_key, ")", (ast->tag == Min ? "<=" : ">="), "(", rhs_key, "))");
        else
            comparison = Texts("generic_compare(stack(", lhs_key, "), stack(", rhs_key, "), ", compile_type_info(key_t),
                               ")", (ast->tag == Min ? "<=" : ">="), "0");

        return Texts("({\n", compile_type(t), " ternary$lhs = ", compile(env, lhs),
                     ", ternary$rhs = ", compile(env, rhs), ";\n", comparison,
                     " ? ternary$lhs : ternary$rhs;\n"
                     "})");
    }
    case List: {
        DeclareMatch(list, ast, List);
        if (!list->items) return Text("(List_t){.length=0}");

        type_t *list_type = get_type(env, ast);
        return compile_typed_list(env, ast, list_type);
    }
    case Table: {
        DeclareMatch(table, ast, Table);
        if (!table->entries) {
            Text_t code = Text("((Table_t){");
            if (table->fallback) code = Texts(code, ".fallback=heap(", compile(env, table->fallback), ")");
            return Texts(code, "})");
        }

        type_t *table_type = get_type(env, ast);
        return compile_typed_table(env, ast, table_type);
    }
    case Set: {
        DeclareMatch(set, ast, Set);
        if (!set->items) return Text("((Table_t){})");

        type_t *set_type = get_type(env, ast);
        return compile_typed_set(env, ast, set_type);
    }
    case Comprehension: {
        ast_t *base = Match(ast, Comprehension)->expr;
        while (base->tag == Comprehension)
            base = Match(ast, Comprehension)->expr;
        if (base->tag == TableEntry) return compile(env, WrapAST(ast, Table, .entries = new (ast_list_t, .ast = ast)));
        else return compile(env, WrapAST(ast, List, .items = new (ast_list_t, .ast = ast)));
    }
    case Lambda: return compile_lambda(env, ast);
    case MethodCall: {
        DeclareMatch(call, ast, MethodCall);
        type_t *self_t = get_type(env, call->self);

        if (streq(call->name, "serialized")) {
            if (call->args) code_err(ast, ".serialized() doesn't take any arguments");
            return Texts("generic_serialize((", compile_declaration(self_t, Text("[1]")), "){",
                         compile(env, call->self), "}, ", compile_type_info(self_t), ")");
        }

        type_t *self_value_t = value_type(self_t);
        if (self_value_t->tag == TypeInfoType || self_value_t->tag == ModuleType) {
            return compile(env,
                           WrapAST(ast, FunctionCall,
                                   .fn = WrapAST(call->self, FieldAccess, .fielded = call->self, .field = call->name),
                                   .args = call->args));
        }

        type_t *field_type = get_field_type(self_value_t, call->name);
        if (field_type && field_type->tag == ClosureType) field_type = Match(field_type, ClosureType)->fn;
        if (field_type && field_type->tag == FunctionType)
            return compile(env,
                           WrapAST(ast, FunctionCall,
                                   .fn = WrapAST(call->self, FieldAccess, .fielded = call->self, .field = call->name),
                                   .args = call->args));

        switch (self_value_t->tag) {
        case ListType: return compile_list_method_call(env, ast);
        case SetType: return compile_set_method_call(env, ast);
        case TableType: return compile_table_method_call(env, ast);
        default: {
            DeclareMatch(methodcall, ast, MethodCall);
            type_t *fn_t = get_method_type(env, methodcall->self, methodcall->name);
            arg_ast_t *args = new (arg_ast_t, .value = methodcall->self, .next = methodcall->args);
            binding_t *b = get_namespace_binding(env, methodcall->self, methodcall->name);
            if (!b) code_err(ast, "No such method");
            return Texts(b->code, "(", compile_arguments(env, ast, Match(fn_t, FunctionType)->args, args), ")");
        }
        }
    }
    case FunctionCall: return compile_function_call(env, ast);
    case Deserialize: {
        ast_t *value = Match(ast, Deserialize)->value;
        type_t *value_type = get_type(env, value);
        if (!type_eq(value_type, Type(ListType, Type(ByteType))))
            code_err(value, "This value should be a list of bytes, not a ", type_to_str(value_type));
        type_t *t = parse_type_ast(env, Match(ast, Deserialize)->type);
        return Texts("({ ", compile_declaration(t, Text("deserialized")),
                     ";\n"
                     "generic_deserialize(",
                     compile(env, value), ", &deserialized, ", compile_type_info(t),
                     ");\n"
                     "deserialized; })");
    }
    case ExplicitlyTyped: {
        return compile_to_type(env, Match(ast, ExplicitlyTyped)->ast, get_type(env, ast));
    }
    case When: {
        DeclareMatch(original, ast, When);
        ast_t *when_var = WrapAST(ast, Var, .name = "when");
        when_clause_t *new_clauses = NULL;
        type_t *subject_t = get_type(env, original->subject);
        for (when_clause_t *clause = original->clauses; clause; clause = clause->next) {
            type_t *clause_type = get_clause_type(env, subject_t, clause);
            if (clause_type->tag == AbortType || clause_type->tag == ReturnType) {
                new_clauses =
                    new (when_clause_t, .pattern = clause->pattern, .body = clause->body, .next = new_clauses);
            } else {
                ast_t *assign = WrapAST(clause->body, Assign, .targets = new (ast_list_t, .ast = when_var),
                                        .values = new (ast_list_t, .ast = clause->body));
                new_clauses = new (when_clause_t, .pattern = clause->pattern, .body = assign, .next = new_clauses);
            }
        }
        REVERSE_LIST(new_clauses);
        ast_t *else_body = original->else_body;
        if (else_body) {
            type_t *clause_type = get_type(env, else_body);
            if (clause_type->tag != AbortType && clause_type->tag != ReturnType) {
                else_body = WrapAST(else_body, Assign, .targets = new (ast_list_t, .ast = when_var),
                                    .values = new (ast_list_t, .ast = else_body));
            }
        }

        type_t *t = get_type(env, ast);
        env_t *when_env = fresh_scope(env);
        set_binding(when_env, "when", t, Text("when"));
        return Texts("({ ", compile_declaration(t, Text("when")), ";\n",
                     compile_statement(when_env, WrapAST(ast, When, .subject = original->subject,
                                                         .clauses = new_clauses, .else_body = else_body)),
                     "when; })");
    }
    case If: {
        DeclareMatch(if_, ast, If);
        ast_t *condition = if_->condition;
        Text_t decl_code = EMPTY_TEXT;
        env_t *truthy_scope = env, *falsey_scope = env;

        Text_t condition_code;
        if (condition->tag == Declare) {
            DeclareMatch(decl, condition, Declare);
            if (decl->value == NULL) code_err(condition, "This declaration must have a value");
            type_t *condition_type =
                decl->type ? parse_type_ast(env, decl->type) : get_type(env, Match(condition, Declare)->value);
            if (condition_type->tag != OptionalType)
                code_err(condition,
                         "This `if var := ...:` declaration should be an "
                         "optional "
                         "type, not ",
                         type_to_str(condition_type));

            if (is_incomplete_type(condition_type)) code_err(condition, "This type is incomplete!");

            decl_code = compile_statement(env, condition);
            ast_t *var = Match(condition, Declare)->var;
            truthy_scope = fresh_scope(env);
            bind_statement(truthy_scope, condition);
            condition_code = compile_condition(truthy_scope, var);
            set_binding(truthy_scope, Match(var, Var)->name, Match(condition_type, OptionalType)->type,
                        optional_into_nonnone(condition_type, compile(truthy_scope, var)));
        } else if (condition->tag == Var) {
            type_t *condition_type = get_type(env, condition);
            condition_code = compile_condition(env, condition);
            if (condition_type->tag == OptionalType) {
                truthy_scope = fresh_scope(env);
                set_binding(truthy_scope, Match(condition, Var)->name, Match(condition_type, OptionalType)->type,
                            optional_into_nonnone(condition_type, compile(truthy_scope, condition)));
            }
        } else {
            condition_code = compile_condition(env, condition);
        }

        type_t *true_type = get_type(truthy_scope, if_->body);
        type_t *false_type = get_type(falsey_scope, if_->else_body);
        if (true_type->tag == AbortType || true_type->tag == ReturnType)
            return Texts("({ ", decl_code, "if (", condition_code, ") ", compile_statement(truthy_scope, if_->body),
                         "\n", compile(falsey_scope, if_->else_body), "; })");
        else if (false_type->tag == AbortType || false_type->tag == ReturnType)
            return Texts("({ ", decl_code, "if (!(", condition_code, ")) ",
                         compile_statement(falsey_scope, if_->else_body), "\n", compile(truthy_scope, if_->body),
                         "; })");
        else if (decl_code.length > 0)
            return Texts("({ ", decl_code, "(", condition_code, ") ? ", compile(truthy_scope, if_->body), " : ",
                         compile(falsey_scope, if_->else_body), ";})");
        else
            return Texts("((", condition_code, ") ? ", compile(truthy_scope, if_->body), " : ",
                         compile(falsey_scope, if_->else_body), ")");
    }
    case Reduction: {
        DeclareMatch(reduction, ast, Reduction);
        ast_e op = reduction->op;

        type_t *iter_t = get_type(env, reduction->iter);
        type_t *item_t = get_iterated_type(iter_t);
        if (!item_t)
            code_err(reduction->iter, "I couldn't figure out how to iterate over this type: ", type_to_str(iter_t));

        static int64_t next_id = 1;
        ast_t *item = FakeAST(Var, String("$it", next_id++));
        ast_t *body = LiteralCode(Text("{}")); // placeholder
        ast_t *loop = FakeAST(For, .vars = new (ast_list_t, .ast = item), .iter = reduction->iter, .body = body);
        env_t *body_scope = for_scope(env, loop);
        if (op == Equals || op == NotEquals || op == LessThan || op == LessThanOrEquals || op == GreaterThan
            || op == GreaterThanOrEquals) {
            // Chained comparisons like ==, <, etc.
            type_t *item_value_type = item_t;
            ast_t *item_value = item;
            if (reduction->key) {
                set_binding(body_scope, "$", item_t, compile(body_scope, item));
                item_value = reduction->key;
                item_value_type = get_type(body_scope, reduction->key);
            }

            Text_t code = Texts("({ // Reduction:\n", compile_declaration(item_value_type, Text("prev")),
                                ";\n"
                                "OptionalBool_t result = NONE_BOOL;\n");

            ast_t *comparison = new (ast_t, .file = ast->file, .start = ast->start, .end = ast->end, .tag = op,
                                     .__data.Plus.lhs = LiteralCode(Text("prev"), .type = item_value_type),
                                     .__data.Plus.rhs = item_value);
            body->__data.InlineCCode.chunks = new (
                ast_list_t, .ast = FakeAST(TextLiteral, Texts("if (result == NONE_BOOL) {\n"
                                                              "    prev = ",
                                                              compile(body_scope, item_value),
                                                              ";\n"
                                                              "    result = yes;\n"
                                                              "} else {\n"
                                                              "    if (",
                                                              compile(body_scope, comparison), ") {\n",
                                                              "        prev = ", compile(body_scope, item_value), ";\n",
                                                              "    } else {\n"
                                                              "        result = no;\n",
                                                              "        break;\n", "    }\n", "}\n")));
            code = Texts(code, compile_statement(env, loop), "\nresult;})");
            return code;
        } else if (op == Min || op == Max) {
            // Min/max:
            Text_t superlative = op == Min ? Text("min") : Text("max");
            Text_t code = Texts("({ // Reduction:\n", compile_declaration(item_t, superlative),
                                ";\n"
                                "Bool_t has_value = no;\n");

            Text_t item_code = compile(body_scope, item);
            ast_e cmp_op = op == Min ? LessThan : GreaterThan;
            if (reduction->key) {
                env_t *key_scope = fresh_scope(env);
                set_binding(key_scope, "$", item_t, item_code);
                type_t *key_type = get_type(key_scope, reduction->key);
                Text_t superlative_key = op == Min ? Text("min_key") : Text("max_key");
                code = Texts(code, compile_declaration(key_type, superlative_key), ";\n");

                ast_t *comparison = new (ast_t, .file = ast->file, .start = ast->start, .end = ast->end, .tag = cmp_op,
                                         .__data.Plus.lhs = LiteralCode(Text("key"), .type = key_type),
                                         .__data.Plus.rhs = LiteralCode(superlative_key, .type = key_type));

                body->__data.InlineCCode.chunks = new (
                    ast_list_t, .ast = FakeAST(TextLiteral, Texts(compile_declaration(key_type, Text("key")), " = ",
                                                                  compile(key_scope, reduction->key), ";\n",
                                                                  "if (!has_value || ", compile(body_scope, comparison),
                                                                  ") {\n"
                                                                  "    ",
                                                                  superlative, " = ", compile(body_scope, item),
                                                                  ";\n"
                                                                  "    ",
                                                                  superlative_key,
                                                                  " = key;\n"
                                                                  "    has_value = yes;\n"
                                                                  "}\n")));
            } else {
                ast_t *comparison =
                    new (ast_t, .file = ast->file, .start = ast->start, .end = ast->end, .tag = cmp_op,
                         .__data.Plus.lhs = item, .__data.Plus.rhs = LiteralCode(superlative, .type = item_t));
                body->__data.InlineCCode.chunks = new (
                    ast_list_t, .ast = FakeAST(TextLiteral, Texts("if (!has_value || ", compile(body_scope, comparison),
                                                                  ") {\n"
                                                                  "    ",
                                                                  superlative, " = ", compile(body_scope, item),
                                                                  ";\n"
                                                                  "    has_value = yes;\n"
                                                                  "}\n")));
            }

            code = Texts(code, compile_statement(env, loop), "\nhas_value ? ", promote_to_optional(item_t, superlative),
                         " : ", compile_none(item_t), ";})");
            return code;
        } else {
            // Accumulator-style reductions like +, ++, *, etc.
            type_t *reduction_type = Match(get_type(env, ast), OptionalType)->type;
            ast_t *item_value = item;
            if (reduction->key) {
                set_binding(body_scope, "$", item_t, compile(body_scope, item));
                item_value = reduction->key;
            }

            Text_t code = Texts("({ // Reduction:\n", compile_declaration(reduction_type, Text("reduction")),
                                ";\n"
                                "Bool_t has_value = no;\n");

            // For the special case of (or)/(and), we need to early out if we
            // can:
            Text_t early_out = EMPTY_TEXT;
            if (op == Compare) {
                if (reduction_type->tag != IntType || Match(reduction_type, IntType)->bits != TYPE_IBITS32)
                    code_err(ast, "<> reductions are only supported for Int32 "
                                  "values");
            } else if (op == And) {
                if (reduction_type->tag == BoolType) early_out = Text("if (!reduction) break;");
                else if (reduction_type->tag == OptionalType)
                    early_out = Texts("if (", check_none(reduction_type, Text("reduction")), ") break;");
            } else if (op == Or) {
                if (reduction_type->tag == BoolType) early_out = Text("if (reduction) break;");
                else if (reduction_type->tag == OptionalType)
                    early_out = Texts("if (!", check_none(reduction_type, Text("reduction")), ") break;");
            }

            ast_t *combination = new (ast_t, .file = ast->file, .start = ast->start, .end = ast->end, .tag = op,
                                      .__data.Plus.lhs = LiteralCode(Text("reduction"), .type = reduction_type),
                                      .__data.Plus.rhs = item_value);
            body->__data.InlineCCode.chunks =
                new (ast_list_t,
                     .ast = FakeAST(TextLiteral, Texts("if (!has_value) {\n"
                                                       "    reduction = ",
                                                       compile(body_scope, item_value),
                                                       ";\n"
                                                       "    has_value = yes;\n"
                                                       "} else {\n"
                                                       "    reduction = ",
                                                       compile(body_scope, combination), ";\n", early_out, "}\n")));

            code = Texts(code, compile_statement(env, loop), "\nhas_value ? ",
                         promote_to_optional(reduction_type, Text("reduction")), " : ", compile_none(reduction_type),
                         ";})");
            return code;
        }
    }
    case FieldAccess: {
        DeclareMatch(f, ast, FieldAccess);
        type_t *fielded_t = get_type(env, f->fielded);
        type_t *value_t = value_type(fielded_t);
        switch (value_t->tag) {
        case TypeInfoType: {
            DeclareMatch(info, value_t, TypeInfoType);
            if (f->field[0] == '_') {
                if (!type_eq(env->current_type, info->type))
                    code_err(ast, "Fields that start with underscores are not "
                                  "accessible "
                                  "on types outside of the type definition.");
            }
            binding_t *b = get_binding(info->env, f->field);
            if (!b) code_err(ast, "I couldn't find the field '", f->field, "' on this type");
            if (b->code.length == 0) code_err(ast, "I couldn't figure out how to compile this field");
            return b->code;
        }
        case TextType: {
            const char *lang = Match(value_t, TextType)->lang;
            if (lang && streq(f->field, "text")) {
                Text_t text = compile_to_pointer_depth(env, f->fielded, 0, false);
                return Texts("((Text_t)", text, ")");
            } else if (streq(f->field, "length")) {
                return Texts("Int$from_int64((", compile_to_pointer_depth(env, f->fielded, 0, false), ").length)");
            }
            code_err(ast, "There is no '", f->field, "' field on ", type_to_str(value_t), " values");
        }
        case StructType: {
            return compile_struct_field_access(env, ast);
        }
        case EnumType: {
            return compile_enum_field_access(env, ast);
        }
        case ListType: {
            if (streq(f->field, "length"))
                return Texts("Int$from_int64((", compile_to_pointer_depth(env, f->fielded, 0, false), ").length)");
            code_err(ast, "There is no ", f->field, " field on lists");
        }
        case SetType: {
            if (streq(f->field, "items"))
                return Texts("LIST_COPY((", compile_to_pointer_depth(env, f->fielded, 0, false), ").entries)");
            else if (streq(f->field, "length"))
                return Texts("Int$from_int64((", compile_to_pointer_depth(env, f->fielded, 0, false),
                             ").entries.length)");
            code_err(ast, "There is no '", f->field, "' field on sets");
        }
        case TableType: {
            if (streq(f->field, "length")) {
                return Texts("Int$from_int64((", compile_to_pointer_depth(env, f->fielded, 0, false),
                             ").entries.length)");
            } else if (streq(f->field, "keys")) {
                return Texts("LIST_COPY((", compile_to_pointer_depth(env, f->fielded, 0, false), ").entries)");
            } else if (streq(f->field, "values")) {
                DeclareMatch(table, value_t, TableType);
                Text_t offset = Texts("offsetof(struct { ", compile_declaration(table->key_type, Text("k")), "; ",
                                      compile_declaration(table->value_type, Text("v")), "; }, v)");
                return Texts("({ List_t *entries = &(", compile_to_pointer_depth(env, f->fielded, 0, false),
                             ").entries;\n"
                             "LIST_INCREF(*entries);\n"
                             "List_t values = *entries;\n"
                             "values.data += ",
                             offset,
                             ";\n"
                             "values; })");
            } else if (streq(f->field, "fallback")) {
                return Texts("({ Table_t *_fallback = (", compile_to_pointer_depth(env, f->fielded, 0, false),
                             ").fallback; _fallback ? *_fallback : NONE_TABLE; })");
            }
            code_err(ast, "There is no '", f->field, "' field on tables");
        }
        case ModuleType: {
            const char *name = Match(value_t, ModuleType)->name;
            env_t *module_env = Table$str_get(*env->imports, name);
            return compile(module_env, WrapAST(ast, Var, f->field));
        }
        default: code_err(ast, "Field accesses are not supported on ", type_to_str(fielded_t), " values");
        }
    }
    case Index: {
        DeclareMatch(indexing, ast, Index);
        type_t *indexed_type = get_type(env, indexing->indexed);
        if (!indexing->index) {
            if (indexed_type->tag != PointerType)
                code_err(ast, "Only pointers can use the '[]' operator to "
                              "dereference "
                              "the entire value.");
            DeclareMatch(ptr, indexed_type, PointerType);
            if (ptr->pointed->tag == ListType) {
                return Texts("*({ List_t *list = ", compile(env, indexing->indexed), "; LIST_INCREF(*list); list; })");
            } else if (ptr->pointed->tag == TableType || ptr->pointed->tag == SetType) {
                return Texts("*({ Table_t *t = ", compile(env, indexing->indexed), "; TABLE_INCREF(*t); t; })");
            } else {
                return Texts("*(", compile(env, indexing->indexed), ")");
            }
        }

        type_t *container_t = value_type(indexed_type);
        type_t *index_t = get_type(env, indexing->index);
        if (container_t->tag == ListType) {
            if (index_t->tag != IntType && index_t->tag != BigIntType && index_t->tag != ByteType)
                code_err(indexing->index, "Lists can only be indexed by integers, not ", type_to_str(index_t));
            type_t *item_type = Match(container_t, ListType)->item_type;
            Text_t list = compile_to_pointer_depth(env, indexing->indexed, 0, false);
            file_t *f = indexing->index->file;
            Text_t index_code =
                indexing->index->tag == Int
                    ? compile_int_to_type(env, indexing->index, Type(IntType, .bits = TYPE_IBITS64))
                    : (index_t->tag == BigIntType ? Texts("Int64$from_int(", compile(env, indexing->index), ", no)")
                                                  : Texts("(Int64_t)(", compile(env, indexing->index), ")"));
            if (indexing->unchecked)
                return Texts("List_get_unchecked(", compile_type(item_type), ", ", list, ", ", index_code, ")");
            else
                return Texts("List_get(", compile_type(item_type), ", ", list, ", ", index_code, ", ",
                             String((int64_t)(indexing->index->start - f->text)), ", ",
                             String((int64_t)(indexing->index->end - f->text)), ")");
        } else if (container_t->tag == TableType) {
            DeclareMatch(table_type, container_t, TableType);
            if (indexing->unchecked) code_err(ast, "Table indexes cannot be unchecked");
            if (table_type->default_value) {
                return Texts("Table$get_or_default(", compile_to_pointer_depth(env, indexing->indexed, 0, false), ", ",
                             compile_type(table_type->key_type), ", ", compile_type(table_type->value_type), ", ",
                             compile(env, indexing->index), ", ",
                             compile_to_type(env, table_type->default_value, table_type->value_type), ", ",
                             compile_type_info(container_t), ")");
            } else {
                return Texts("Table$get_optional(", compile_to_pointer_depth(env, indexing->indexed, 0, false), ", ",
                             compile_type(table_type->key_type), ", ", compile_type(table_type->value_type), ", ",
                             compile(env, indexing->index),
                             ", "
                             "_, ",
                             promote_to_optional(table_type->value_type, Text("(*_)")), ", ",
                             compile_none(table_type->value_type), ", ", compile_type_info(container_t), ")");
            }
        } else if (container_t->tag == TextType) {
            return Texts("Text$cluster(", compile_to_pointer_depth(env, indexing->indexed, 0, false), ", ",
                         compile_to_type(env, indexing->index, Type(BigIntType)), ")");
        } else {
            code_err(ast, "Indexing is not supported for type: ", type_to_str(container_t));
        }
    }
    case InlineCCode: {
        type_t *t = get_type(env, ast);
        if (t->tag == VoidType) return Texts("{\n", compile_statement(env, ast), "\n}");
        else return compile_statement(env, ast);
    }
    case Use: code_err(ast, "Compiling 'use' as expression!");
    case Defer: code_err(ast, "Compiling 'defer' as expression!");
    case Extern: code_err(ast, "Externs are not supported as expressions");
    case TableEntry: code_err(ast, "Table entries should not be compiled directly");
    case Declare:
    case Assign:
    case UPDATE_CASES:
    case For:
    case While:
    case Repeat:
    case StructDef:
    case LangDef:
    case Extend:
    case EnumDef:
    case FunctionDef:
    case ConvertDef:
    case Skip:
    case Stop:
    case Pass:
    case Return:
    case DocTest:
    case Assert: code_err(ast, "This is not a valid expression");
    case Unknown:
    default: code_err(ast, "Unknown AST: ", ast_to_sexp_str(ast));
    }
    return EMPTY_TEXT;
}

static Text_t get_flag_options(type_t *t, const char *separator) {
    if (t->tag == BoolType) {
        return Text("yes|no");
    } else if (t->tag == EnumType) {
        Text_t options = EMPTY_TEXT;
        for (tag_t *tag = Match(t, EnumType)->tags; tag; tag = tag->next) {
            options = Texts(options, tag->name);
            if (tag->next) options = Texts(options, separator);
        }
        return options;
    } else if (t->tag == IntType || t->tag == NumType || t->tag == BigIntType) {
        return Text("N");
    } else {
        return Text("...");
    }
}

Text_t compile_cli_arg_call(env_t *env, Text_t fn_name, type_t *fn_type, const char *version) {
    DeclareMatch(fn_info, fn_type, FunctionType);

    env_t *main_env = fresh_scope(env);

    Text_t code = EMPTY_TEXT;
    binding_t *usage_binding = get_binding(env, "_USAGE");
    Text_t usage_code = usage_binding ? usage_binding->code : Text("usage");
    binding_t *help_binding = get_binding(env, "_HELP");
    Text_t help_code = help_binding ? help_binding->code : usage_code;
    if (!usage_binding) {
        bool explicit_help_flag = false;
        for (arg_t *arg = fn_info->args; arg; arg = arg->next) {
            if (streq(arg->name, "help")) {
                explicit_help_flag = true;
                break;
            }
        }

        Text_t usage = explicit_help_flag ? EMPTY_TEXT : Text(" [--help]");
        for (arg_t *arg = fn_info->args; arg; arg = arg->next) {
            usage = Texts(usage, " ");
            type_t *t = get_arg_type(main_env, arg);
            Text_t flag = Text$replace(Text$from_str(arg->name), Text("_"), Text("-"));
            if (arg->default_val || arg->type->tag == OptionalType) {
                if (strlen(arg->name) == 1) {
                    if (t->tag == BoolType || (t->tag == OptionalType && Match(t, OptionalType)->type->tag == BoolType))
                        usage = Texts(usage, "[-", flag, "]");
                    else usage = Texts(usage, "[-", flag, " ", get_flag_options(t, "|"), "]");
                } else {
                    if (t->tag == BoolType || (t->tag == OptionalType && Match(t, OptionalType)->type->tag == BoolType))
                        usage = Texts(usage, "[--", flag, "]");
                    else if (t->tag == ListType) usage = Texts(usage, "[--", flag, " ", get_flag_options(t, "|"), "]");
                    else usage = Texts(usage, "[--", flag, "=", get_flag_options(t, "|"), "]");
                }
            } else {
                if (t->tag == BoolType) usage = Texts(usage, "<--", flag, "|--no-", flag, ">");
                else if (t->tag == EnumType) usage = Texts(usage, get_flag_options(t, "|"));
                else if (t->tag == ListType) usage = Texts(usage, "[", flag, "...]");
                else usage = Texts(usage, "<", flag, ">");
            }
        }
        code = Texts(code,
                     "Text_t usage = Texts(Text(\"Usage: \"), "
                     "Text$from_str(argv[0])",
                     usage.length == 0 ? EMPTY_TEXT : Texts(", Text(", quoted_text(usage), ")"), ");\n");
    }

    for (arg_t *arg = fn_info->args; arg; arg = arg->next) {
        type_t *opt_type = arg->type->tag == OptionalType ? arg->type : Type(OptionalType, .type = arg->type);
        code = Texts(code, compile_declaration(opt_type, Texts("_$", arg->name)));
        if (arg->default_val) {
            Text_t default_val = compile(env, arg->default_val);
            if (arg->type->tag != OptionalType) default_val = promote_to_optional(arg->type, default_val);
            code = Texts(code, " = ", default_val);
        } else {
            code = Texts(code, " = ", compile_none(arg->type));
        }
        code = Texts(code, ";\n");
    }

    Text_t version_code = quoted_str(version);
    code = Texts(code, "tomo_parse_args(argc, argv, ", usage_code, ", ", help_code, ", ", version_code);
    for (arg_t *arg = fn_info->args; arg; arg = arg->next) {
        code = Texts(code, ",\n{", quoted_text(Text$replace(Text$from_str(arg->name), Text("_"), Text("-"))), ", ",
                     (arg->default_val || arg->type->tag == OptionalType) ? "false" : "true", ", ",
                     compile_type_info(arg->type), ", &", Texts("_$", arg->name), "}");
    }
    code = Texts(code, ");\n");

    code = Texts(code, fn_name, "(");
    for (arg_t *arg = fn_info->args; arg; arg = arg->next) {
        Text_t arg_code = Texts("_$", arg->name);
        if (arg->type->tag != OptionalType) arg_code = optional_into_nonnone(arg->type, arg_code);

        code = Texts(code, arg_code);
        if (arg->next) code = Texts(code, ", ");
    }
    code = Texts(code, ");\n");
    return code;
}

Text_t compile_statement_type_header(env_t *env, Path_t header_path, ast_t *ast) {
    switch (ast->tag) {
    case Use: {
        DeclareMatch(use, ast, Use);
        Path_t source_path = Path$from_str(ast->file->filename);
        Path_t source_dir = Path$parent(source_path);
        Path_t build_dir = Path$resolved(Path$parent(header_path), Path$current_dir());
        switch (use->what) {
        case USE_MODULE: {
            module_info_t mod = get_module_info(ast);
            glob_t tm_files;
            const char *folder = mod.version ? String(mod.name, "_", mod.version) : mod.name;
            if (glob(String(TOMO_PREFIX "/share/tomo_" TOMO_VERSION "/installed/", folder, "/[!._0-9]*.tm"), GLOB_TILDE,
                     NULL, &tm_files)
                != 0) {
                if (!try_install_module(mod)) code_err(ast, "Could not find library");
            }

            Text_t includes = EMPTY_TEXT;
            for (size_t i = 0; i < tm_files.gl_pathc; i++) {
                const char *filename = tm_files.gl_pathv[i];
                Path_t tm_file = Path$from_str(filename);
                Path_t lib_build_dir = Path$sibling(tm_file, Text(".build"));
                Path_t header = Path$child(lib_build_dir, Texts(Path$base_name(tm_file), Text(".h")));
                includes = Texts(includes, "#include \"", Path$as_c_string(header), "\"\n");
            }
            globfree(&tm_files);
            return with_source_info(env, ast, includes);
        }
        case USE_LOCAL: {
            Path_t used_path = Path$resolved(Path$from_str(use->path), source_dir);
            Path_t used_build_dir = Path$sibling(used_path, Text(".build"));
            Path_t used_header_path = Path$child(used_build_dir, Texts(Path$base_name(used_path), Text(".h")));
            return Texts("#include \"", Path$as_c_string(Path$relative_to(used_header_path, build_dir)), "\"\n");
        }
        case USE_HEADER:
            if (use->path[0] == '<') {
                return Texts("#include ", use->path, "\n");
            } else {
                Path_t used_path = Path$resolved(Path$from_str(use->path), source_dir);
                return Texts("#include \"", Path$as_c_string(Path$relative_to(used_path, build_dir)), "\"\n");
            }
        default: return EMPTY_TEXT;
        }
    }
    case StructDef: {
        return compile_struct_header(env, ast);
    }
    case EnumDef: {
        return compile_enum_header(env, ast);
    }
    case LangDef: {
        DeclareMatch(def, ast, LangDef);
        return Texts(
            // Constructor macro:
            "#define ", namespace_name(env, env->namespace, Text$from_str(def->name)), "(text) ((",
            namespace_name(env, env->namespace, Texts(def->name, "$$type")),
            "){.length=sizeof(text)-1, .tag=TEXT_ASCII, .ascii=\"\" "
            "text})\n"
            "#define ",
            namespace_name(env, env->namespace, Text$from_str(def->name)), "s(...) ((",
            namespace_name(env, env->namespace, Texts(def->name, "$$type")),
            ")Texts(__VA_ARGS__))\n"
            "extern const TypeInfo_t ",
            namespace_name(env, env->namespace, Texts(def->name, Text("$$info"))), ";\n");
    }
    case Extend: {
        return EMPTY_TEXT;
    }
    default: return EMPTY_TEXT;
    }
}

Text_t compile_statement_namespace_header(env_t *env, Path_t header_path, ast_t *ast) {
    env_t *ns_env = NULL;
    ast_t *block = NULL;
    switch (ast->tag) {
    case LangDef: {
        DeclareMatch(def, ast, LangDef);
        ns_env = namespace_env(env, def->name);
        block = def->namespace;
        break;
    }
    case Extend: {
        DeclareMatch(extend, ast, Extend);
        ns_env = namespace_env(env, extend->name);

        env_t *extended = new (env_t);
        *extended = *ns_env;
        extended->locals = new (Table_t, .fallback = env->locals);
        extended->namespace_bindings = new (Table_t, .fallback = env->namespace_bindings);
        extended->id_suffix = env->id_suffix;
        ns_env = extended;

        block = extend->body;
        break;
    }
    case StructDef: {
        DeclareMatch(def, ast, StructDef);
        ns_env = namespace_env(env, def->name);
        block = def->namespace;
        break;
    }
    case EnumDef: {
        DeclareMatch(def, ast, EnumDef);
        ns_env = namespace_env(env, def->name);
        block = def->namespace;
        break;
    }
    case Extern: {
        DeclareMatch(ext, ast, Extern);
        type_t *t = parse_type_ast(env, ext->type);
        Text_t decl;
        if (t->tag == ClosureType) {
            t = Match(t, ClosureType)->fn;
            DeclareMatch(fn, t, FunctionType);
            decl = Texts(compile_type(fn->ret), " ", ext->name, "(");
            for (arg_t *arg = fn->args; arg; arg = arg->next) {
                decl = Texts(decl, compile_type(arg->type));
                if (arg->next) decl = Texts(decl, ", ");
            }
            decl = Texts(decl, ")");
        } else {
            decl = compile_declaration(t, Text$from_str(ext->name));
        }
        return Texts("extern ", decl, ";\n");
    }
    case Declare: {
        DeclareMatch(decl, ast, Declare);
        const char *decl_name = Match(decl->var, Var)->name;
        bool is_private = (decl_name[0] == '_');
        if (is_private) return EMPTY_TEXT;

        type_t *t = decl->type ? parse_type_ast(env, decl->type) : get_type(env, decl->value);
        if (t->tag == FunctionType) t = Type(ClosureType, t);
        assert(t->tag != ModuleType);
        if (t->tag == AbortType || t->tag == VoidType || t->tag == ReturnType)
            code_err(ast, "You can't declare a variable with a ", type_to_str(t), " value");

        return Texts(decl->value ? compile_statement_type_header(env, header_path, decl->value) : EMPTY_TEXT, "extern ",
                     compile_declaration(t, namespace_name(env, env->namespace, Text$from_str(decl_name))), ";\n");
    }
    case FunctionDef: {
        DeclareMatch(fndef, ast, FunctionDef);
        const char *decl_name = Match(fndef->name, Var)->name;
        bool is_private = decl_name[0] == '_';
        if (is_private) return EMPTY_TEXT;
        Text_t arg_signature = Text("(");
        for (arg_ast_t *arg = fndef->args; arg; arg = arg->next) {
            type_t *arg_type = get_arg_ast_type(env, arg);
            arg_signature = Texts(arg_signature, compile_declaration(arg_type, Texts("_$", arg->name)));
            if (arg->next) arg_signature = Texts(arg_signature, ", ");
        }
        arg_signature = Texts(arg_signature, ")");

        type_t *ret_t = fndef->ret_type ? parse_type_ast(env, fndef->ret_type) : Type(VoidType);
        Text_t ret_type_code = compile_type(ret_t);
        if (ret_t->tag == AbortType) ret_type_code = Texts("__attribute__((noreturn)) _Noreturn ", ret_type_code);
        Text_t name = namespace_name(env, env->namespace, Text$from_str(decl_name));
        if (env->namespace && env->namespace->parent && env->namespace->name && streq(decl_name, env->namespace->name))
            name = namespace_name(env, env->namespace, Text$from_str(String(get_line_number(ast->file, ast->start))));
        return Texts(ret_type_code, " ", name, arg_signature, ";\n");
    }
    case ConvertDef: {
        DeclareMatch(def, ast, ConvertDef);

        Text_t arg_signature = Text("(");
        for (arg_ast_t *arg = def->args; arg; arg = arg->next) {
            type_t *arg_type = get_arg_ast_type(env, arg);
            arg_signature = Texts(arg_signature, compile_declaration(arg_type, Texts("_$", arg->name)));
            if (arg->next) arg_signature = Texts(arg_signature, ", ");
        }
        arg_signature = Texts(arg_signature, ")");

        type_t *ret_t = def->ret_type ? parse_type_ast(env, def->ret_type) : Type(VoidType);
        Text_t ret_type_code = compile_type(ret_t);
        Text_t name = Text$from_str(get_type_name(ret_t));
        if (name.length == 0)
            code_err(ast,
                     "Conversions are only supported for text, struct, and enum "
                     "types, not ",
                     type_to_str(ret_t));
        Text_t name_code =
            namespace_name(env, env->namespace, Texts(name, "$", String(get_line_number(ast->file, ast->start))));
        return Texts(ret_type_code, " ", name_code, arg_signature, ";\n");
    }
    default: return EMPTY_TEXT;
    }
    assert(ns_env);
    Text_t header = EMPTY_TEXT;
    for (ast_list_t *stmt = block ? Match(block, Block)->statements : NULL; stmt; stmt = stmt->next) {
        header = Texts(header, compile_statement_namespace_header(ns_env, header_path, stmt->ast));
    }
    return header;
}

typedef struct {
    env_t *env;
    Text_t *header;
    Path_t header_path;
} compile_typedef_info_t;

static void _make_typedefs(compile_typedef_info_t *info, ast_t *ast) {
    if (ast->tag == StructDef) {
        DeclareMatch(def, ast, StructDef);
        if (def->external) return;
        Text_t struct_name = namespace_name(info->env, info->env->namespace, Texts(def->name, "$$struct"));
        Text_t type_name = namespace_name(info->env, info->env->namespace, Texts(def->name, "$$type"));
        *info->header = Texts(*info->header, "typedef struct ", struct_name, " ", type_name, ";\n");
    } else if (ast->tag == EnumDef) {
        DeclareMatch(def, ast, EnumDef);
        bool has_any_tags_with_fields = false;
        for (tag_ast_t *tag = def->tags; tag; tag = tag->next) {
            has_any_tags_with_fields = has_any_tags_with_fields || (tag->fields != NULL);
        }

        if (has_any_tags_with_fields) {
            Text_t struct_name = namespace_name(info->env, info->env->namespace, Texts(def->name, "$$struct"));
            Text_t type_name = namespace_name(info->env, info->env->namespace, Texts(def->name, "$$type"));
            *info->header = Texts(*info->header, "typedef struct ", struct_name, " ", type_name, ";\n");

            for (tag_ast_t *tag = def->tags; tag; tag = tag->next) {
                if (!tag->fields) continue;
                Text_t tag_struct =
                    namespace_name(info->env, info->env->namespace, Texts(def->name, "$", tag->name, "$$struct"));
                Text_t tag_type =
                    namespace_name(info->env, info->env->namespace, Texts(def->name, "$", tag->name, "$$type"));
                *info->header = Texts(*info->header, "typedef struct ", tag_struct, " ", tag_type, ";\n");
            }
        } else {
            Text_t enum_name = namespace_name(info->env, info->env->namespace, Texts(def->name, "$$enum"));
            Text_t type_name = namespace_name(info->env, info->env->namespace, Texts(def->name, "$$type"));
            *info->header = Texts(*info->header, "typedef enum ", enum_name, " ", type_name, ";\n");
        }
    } else if (ast->tag == LangDef) {
        DeclareMatch(def, ast, LangDef);
        *info->header = Texts(*info->header, "typedef Text_t ",
                              namespace_name(info->env, info->env->namespace, Texts(def->name, "$$type")), ";\n");
    }
}

static void _define_types_and_funcs(compile_typedef_info_t *info, ast_t *ast) {
    *info->header = Texts(*info->header, compile_statement_type_header(info->env, info->header_path, ast),
                          compile_statement_namespace_header(info->env, info->header_path, ast));
}

Text_t compile_file_header(env_t *env, Path_t header_path, ast_t *ast) {
    Text_t header =
        Texts("#pragma once\n",
              env->do_source_mapping ? Texts("#line 1 ", quoted_str(ast->file->filename), "\n") : EMPTY_TEXT,
              "#include <tomo_" TOMO_VERSION "/tomo.h>\n");

    compile_typedef_info_t info = {.env = env, .header = &header, .header_path = header_path};
    visit_topologically(Match(ast, Block)->statements, (Closure_t){.fn = (void *)_make_typedefs, &info});
    visit_topologically(Match(ast, Block)->statements, (Closure_t){.fn = (void *)_define_types_and_funcs, &info});

    header = Texts(header, "void ", namespace_name(env, env->namespace, Text("$initialize")), "(void);\n");
    return header;
}

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
