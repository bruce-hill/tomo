// This file defines how to compile binary operations

#include "../ast.h"
#include "../environment.h"
#include "../stdlib/datatypes.h"
#include "../stdlib/number.h"
#include "../stdlib/text.h"
#include "../util.h"
#include "../typecheck.h"
#include "../types.h"
#include "compilation.h"

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

// Integer/byte division and modulo are guarded against a zero divisor so they raise a clean runtime
// error (like force-unwrapping an out-of-bounds list index) instead of a hardware SIGFPE.
static Text_t compile_checked_int_divmod(env_t *env, ast_t *ast, type_t *overall_t) {
    binary_operands_t binop = BINARY_OPERANDS(ast);
    Text_t lhs = compile_to_type(env, binop.lhs, overall_t);
    Text_t rhs = compile_to_type(env, binop.rhs, overall_t);

    int64_t start = (int64_t)(ast->start - ast->file->text);
    int64_t end = (int64_t)(ast->end - ast->file->text);
    int64_t line = get_line_number(ast->file, ast->start);

    Text_t is_zero = overall_t->tag == BigIntType ? Text("I_is_zero($divisor)") : Text("$divisor == 0");

    Text_t op_code;
    if (overall_t->tag == ByteType) {
        op_code = ast->tag == FloorDivide ? Text("($numerator / $divisor)")
                : ast->tag == Mod    ? Text("($numerator % $divisor)")
                                     : Text("((($numerator - 1) % $divisor) + 1)");
    } else {
        binding_t *b = get_binding(get_namespace_by_type(env, overall_t), binop_info[ast->tag].method_name);
        op_code = Texts(b->code, "($numerator, $divisor)");
    }

    // Report the numerator in the error message, matching the detail of other runtime errors:
    Text_t numerator = overall_t->tag == BigIntType ? Text("$numerator") : Text("(int64_t)($numerator)");
    const char *prefix = ast->tag == FloorDivide ? "Cannot divide " : "Cannot take ";
    const char *suffix = ast->tag == FloorDivide ? " by zero\\n" : " modulo zero\\n";
    Text_t message = Texts("Texts(Text(\"", prefix, "\"), ", numerator, ", Text(\"", suffix, "\"))");

    return Texts("({ ", compile_declaration(overall_t, Text("$numerator")), " = ", lhs, ";\n",
                 compile_declaration(overall_t, Text("$divisor")), " = ", rhs, ";\n", "if unlikely (", is_zero, ")\n",
                 "#line ", line, "\n", "fail_source(", quoted_str(ast->file->filename), ", ", start, ", ", end, ", ",
                 message, ");\n", op_code, "; })");
}

public
Text_t compile_binary_op(env_t *env, ast_t *ast) {
    return compile_binary_op_to_type(env, ast, get_type(env, ast));
}

// Compile a binary operation, treating its result (and, via compile_to_type,
// its operands) as `overall_t`. `compile_binary_op` passes the inferred type;
// callers that know the value flows into a specific numeric type (e.g. an
// `[Int64]` list literal holding `1 + 2`, whose operands would otherwise infer
// as bignum `Int`) pass that type so the arithmetic is done natively in it
// instead of in bignum-then-converted.
public
Text_t compile_binary_op_to_type(env_t *env, ast_t *ast, type_t *overall_t) {
    binary_operands_t binop = BINARY_OPERANDS(ast);
    type_t *lhs_t = get_type(env, binop.lhs);
    type_t *rhs_t = get_type(env, binop.rhs);

    if ((ast->tag == FloorDivide || ast->tag == Mod || ast->tag == Mod1)
        && (overall_t->tag == IntType || overall_t->tag == BigIntType || overall_t->tag == ByteType))
        return compile_checked_int_divmod(env, ast, overall_t);

    // Arithmetic on compile-time-constant Nums folds to the constant it
    // denotes: `1/3` is NUMBER_SMALL(1, 3), not a runtime division, and
    // `0.1 + 0.1` is NUMBER_SMALL(1, 5). A folded value outside the immediate
    // tier (2^100) still collapses the whole expression to one runtime parse
    // of its exact form.
    if (overall_t->tag == NumType) {
        Num_t folded;
        if (fold_num_constant(ast, &folded))
            return compile_num_value(
                folded, Texts("number_from_symbolic(\"", Text$from_str(number_to_symbolic(folded)), "\")"));
    }

    // `/` on integers is exact: both operands convert to Num (losslessly) and
    // the division happens there. The typechecker picked NumType for exactly
    // this case; Num/Num operands take the metamethod path below instead.
    if (ast->tag == Divide && overall_t->tag == NumType && (get_type(env, binop.lhs)->tag != NumType || get_type(env, binop.rhs)->tag != NumType))
        return Texts("Num$divided_by(", compile_to_type(env, binop.lhs, overall_t), ", ",
                     compile_to_type(env, binop.rhs, overall_t), ")");

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
    } else if ((ast->tag == Divide || ast->tag == FloorDivide || ast->tag == Mod || ast->tag == Mod1)
               && is_numeric_type(rhs_t)) {
        b = get_namespace_binding(env, binop.lhs, binop_info[ast->tag].method_name);
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
                code_err(binop.rhs, "I don't know how to convert a ", type_to_text(rhs_t), " to a ",
                         type_to_text(Match(lhs_t, OptionalType)->type));
            rhs_t = complete;
        }

        if (rhs_t->tag == OptionalType && type_eq(lhs_t, rhs_t)) {
            return Texts("({ ", compile_declaration(lhs_t, Text("lhs")), " = ", compile(env, binop.lhs), "; ",
                         check_none(lhs_t, Text("lhs")), " ? ", compile(env, binop.rhs), " : lhs; })");
        } else if (rhs_t->tag != OptionalType
                   && (type_eq(Match(lhs_t, OptionalType)->type, rhs_t)
                       || can_compile_to_type(env, binop.rhs, Match(lhs_t, OptionalType)->type))) {
            // The fallback is compiled to the optional's own type, so an
            // untyped literal (`x or 0`) becomes a value of that type rather
            // than its own inferred one.
            type_t *inner = Match(lhs_t, OptionalType)->type;
            return Texts("({ ", compile_declaration(lhs_t, Text("lhs")), " = ", compile(env, binop.lhs), "; ",
                         check_none(lhs_t, Text("lhs")), " ? ", compile_to_type(env, binop.rhs, inner), " : ",
                         optional_into_nonnone(lhs_t, Text("lhs")), "; })");
        } else if (rhs_t->tag == BoolType) {
            return Texts("((!", check_none(lhs_t, compile(env, binop.lhs)), ") || ", compile(env, binop.rhs), ")");
        } else {
            code_err(ast, "I don't know how to do an 'or' operation between ", type_to_text(lhs_t), " and ",
                     type_to_text(rhs_t));
        }
    }

    Text_t lhs = compile_to_type(env, binop.lhs, overall_t);
    Text_t rhs = compile_to_type(env, binop.rhs, overall_t);

    switch (ast->tag) {
    case Power: {
        if (overall_t->tag != FloatType)
            code_err(ast, "Exponentiation is only supported for Num types, not ", type_to_text(overall_t));
        if (overall_t->tag == FloatType && Match(overall_t, FloatType)->bits == TYPE_NBITS32)
            return Texts("powf(", lhs, ", ", rhs, ")");
        else return Texts("pow(", lhs, ", ", rhs, ")");
    }
    case Multiply: {
        if (overall_t->tag != IntType && overall_t->tag != FloatType && overall_t->tag != ByteType)
            code_err(ast,
                     "Math operations are only supported for values of the same "
                     "numeric type, not ",
                     type_to_text(lhs_t), " and ", type_to_text(rhs_t));
        return Texts("(", lhs, " * ", rhs, ")");
    }
    case Divide: {
        if (overall_t->tag != IntType && overall_t->tag != FloatType && overall_t->tag != ByteType)
            code_err(ast,
                     "Math operations are only supported for values of the same "
                     "numeric type, not ",
                     type_to_text(lhs_t), " and ", type_to_text(rhs_t));
        return Texts("(", lhs, " / ", rhs, ")");
    }
    case Mod: {
        if (overall_t->tag != IntType && overall_t->tag != FloatType && overall_t->tag != ByteType)
            code_err(ast,
                     "Math operations are only supported for values of the same "
                     "numeric type, not ",
                     type_to_text(lhs_t), " and ", type_to_text(rhs_t));
        return Texts("(", lhs, " % ", rhs, ")");
    }
    case Mod1: {
        if (overall_t->tag != IntType && overall_t->tag != FloatType && overall_t->tag != ByteType)
            code_err(ast,
                     "Math operations are only supported for values of the same "
                     "numeric type, not ",
                     type_to_text(lhs_t), " and ", type_to_text(rhs_t));
        return Texts("((((", lhs, ")-1) % (", rhs, ")) + 1)");
    }
    case Plus: {
        if (overall_t->tag != IntType && overall_t->tag != FloatType && overall_t->tag != ByteType)
            code_err(ast,
                     "Math operations are only supported for values of the same "
                     "numeric type, not ",
                     type_to_text(lhs_t), " and ", type_to_text(rhs_t));
        return Texts("(", lhs, " + ", rhs, ")");
    }
    case Minus: {
        if (overall_t->tag != IntType && overall_t->tag != FloatType && overall_t->tag != ByteType)
            code_err(ast,
                     "Math operations are only supported for values of the same "
                     "numeric type, not ",
                     type_to_text(lhs_t), " and ", type_to_text(rhs_t));
        return Texts("(", lhs, " - ", rhs, ")");
    }
    case LeftShift: {
        if (overall_t->tag != IntType && overall_t->tag != FloatType && overall_t->tag != ByteType)
            code_err(ast,
                     "Math operations are only supported for values of the same "
                     "numeric type, not ",
                     type_to_text(lhs_t), " and ", type_to_text(rhs_t));
        return Texts("(", lhs, " << ", rhs, ")");
    }
    case RightShift: {
        if (overall_t->tag != IntType && overall_t->tag != FloatType && overall_t->tag != ByteType)
            code_err(ast,
                     "Math operations are only supported for values of the same "
                     "numeric type, not ",
                     type_to_text(lhs_t), " and ", type_to_text(rhs_t));
        return Texts("(", lhs, " >> ", rhs, ")");
    }
    case UnsignedLeftShift: {
        if (overall_t->tag != IntType && overall_t->tag != FloatType && overall_t->tag != ByteType)
            code_err(ast,
                     "Math operations are only supported for values of the same "
                     "numeric type, not ",
                     type_to_text(lhs_t), " and ", type_to_text(rhs_t));
        return Texts("(", compile_type(overall_t), ")((", compile_unsigned_type(lhs_t), ")", lhs, " << ", rhs, ")");
    }
    case UnsignedRightShift: {
        if (overall_t->tag != IntType && overall_t->tag != FloatType && overall_t->tag != ByteType)
            code_err(ast,
                     "Math operations are only supported for values of the same "
                     "numeric type, not ",
                     type_to_text(lhs_t), " and ", type_to_text(rhs_t));
        return Texts("(", compile_type(overall_t), ")((", compile_unsigned_type(lhs_t), ")", lhs, " >> ", rhs, ")");
    }
    case And: {
        if (overall_t->tag == BoolType) return Texts("(", lhs, " && ", rhs, ")");
        else if (overall_t->tag == IntType || overall_t->tag == ByteType) return Texts("(", lhs, " & ", rhs, ")");
        else
            code_err(ast, "The 'and' operator isn't supported between ", type_to_text(lhs_t), " and ",
                     type_to_text(rhs_t), " values");
    }
    case FloorDivide: {
        // Integer and Num floor division is Euclidean and was handled above
        // (the checked-divmod path and the floor_divided_by metamethods); for
        // hardware floats `//` is plain floor(x/y), keeping the whole
        // operation in floating point.
        if (overall_t->tag != FloatType)
            code_err(ast, "Floored division (`//`) is only supported for numeric values, not ",
                     type_to_text(overall_t));
        if (Match(overall_t, FloatType)->bits == TYPE_NBITS32) return Texts("floorf(", lhs, " / ", rhs, ")");
        return Texts("floor(", lhs, " / ", rhs, ")");
    }
    case Compare: {
        return Texts("generic_compare(stack(", lhs, "), stack(", rhs, "), ", compile_type_info(overall_t), ")");
    }
    case Or: {
        if (overall_t->tag == BoolType) {
            return Texts("(", lhs, " || ", rhs, ")");
        } else if (overall_t->tag == IntType || overall_t->tag == ByteType) {
            return Texts("(", lhs, " | ", rhs, ")");
        } else {
            code_err(ast, "The 'or' operator isn't supported between ", type_to_text(lhs_t), " and ",
                     type_to_text(rhs_t), " values");
        }
    }
    case Xor: {
        // TODO: support optional values in `xor` expressions
        if (overall_t->tag == BoolType || overall_t->tag == IntType || overall_t->tag == ByteType)
            return Texts("(", lhs, " ^ ", rhs, ")");
        else
            code_err(ast, "The 'xor' operator isn't supported between ", type_to_text(lhs_t), " and ",
                     type_to_text(rhs_t), " values");
    }
    case Concat: {
        switch (overall_t->tag) {
        case PathType: {
            return Texts("Path$concat(", lhs, ", ", rhs, ")");
        }
        case TextType: {
            return Texts("Text$concat(", lhs, ", ", rhs, ")");
        }
        case ListType: {
            return Texts("List$concat(", lhs, ", ", rhs, ", sizeof(",
                         compile_type(Match(overall_t, ListType)->item_type), "))");
        }
        case TableType: {
            return Texts("Table$with(", lhs, ", ", rhs, ", ", compile_type_info(overall_t), ")");
        }
        default:
            code_err(ast, "Concatenation isn't supported between ", type_to_text(lhs_t), " and ", type_to_text(rhs_t),
                     " values");
        }
    }
    default: errx(1, "Not a valid binary operation: %s", ast_to_sexp_str(ast));
    }
    return EMPTY_TEXT;
}
