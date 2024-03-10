
#include <ctype.h>
#include <gc/cord.h>
#include <gc.h>
#include <stdio.h>
#include <uninorm.h>

#include "ast.h"
#include "builtins/text.h"
#include "compile.h"
#include "enums.h"
#include "structs.h"
#include "environment.h"
#include "typecheck.h"
#include "util.h"

CORD compile_type_ast(type_ast_t *t)
{
    switch (t->tag) {
    case VarTypeAST: return CORD_cat(Match(t, VarTypeAST)->name, "_t");
    case PointerTypeAST: return CORD_cat(compile_type_ast(Match(t, PointerTypeAST)->pointed), "*");
    case TableTypeAST: return "table_t";
    case ArrayTypeAST: return "array_t";
    case FunctionTypeAST: return "const void*";
    default: code_err(t, "Not implemented");
    }
}

static bool promote(env_t *env, CORD *code, type_t *actual, type_t *needed)
{
    if (type_eq(actual, needed))
        return true;

    if (!can_promote(actual, needed))
        return false;

    if (actual->tag == IntType || actual->tag == NumType)
        return true;

    // Automatic dereferencing:
    if (actual->tag == PointerType && !Match(actual, PointerType)->is_optional
        && can_promote(Match(actual, PointerType)->pointed, needed)) {
        *code = CORD_all("*(", *code, ")");
        return promote(env, code, Match(actual, PointerType)->pointed, needed);
    }

    // Optional promotion:
    if (actual->tag == PointerType && needed->tag == PointerType)
        return true;

    if (needed->tag == ClosureType && actual->tag == FunctionType && type_eq(actual, Match(needed, ClosureType)->fn)) {
        *code = CORD_all("(closure_t){", *code, ", NULL}");
        return true;
    }

    return false;
}


CORD compile_declaration(type_t *t, const char *name)
{
    if (t->tag == FunctionType) {
        auto fn = Match(t, FunctionType);
        CORD code = CORD_all(compile_type(fn->ret), " (*", name, ")(");
        for (arg_t *arg = fn->args; arg; arg = arg->next) {
            code = CORD_all(code, compile_type(arg->type));
            if (arg->next) code = CORD_cat(code, ", ");
        }
        return CORD_all(code, ")");
    } else {
        return CORD_all(compile_type(t), " ", name);
    }
}

CORD compile_type(type_t *t)
{
    switch (t->tag) {
    case AbortType: return "void";
    case VoidType: return "void";
    case MemoryType: return "void";
    case BoolType: return "Bool_t";
    case IntType: return Match(t, IntType)->bits == 64 ? "Int_t" : CORD_asprintf("Int%ld_t", Match(t, IntType)->bits);
    case NumType: return Match(t, NumType)->bits == 64 ? "Num_t" : CORD_asprintf("Num%ld_t", Match(t, NumType)->bits);
    case TextType: {
        const char *dsl = Match(t, TextType)->lang;
        return dsl ? CORD_cat(dsl, "_t") : "Text_t";
    }
    case ArrayType: return "array_t";
    case TableType: return "table_t";
    case FunctionType: {
        auto fn = Match(t, FunctionType);
        CORD code = CORD_all(compile_type(fn->ret), " (*)(");
        for (arg_t *arg = fn->args; arg; arg = arg->next) {
            code = CORD_all(code, compile_type(arg->type));
            if (arg->next) code = CORD_cat(code, ", ");
        }
        return CORD_all(code, ")");
    }
    case ClosureType: return "closure_t";
    case PointerType: return CORD_cat(compile_type(Match(t, PointerType)->pointed), "*");
    case StructType: return CORD_cat(Match(t, StructType)->name, "_t");
    case EnumType: return CORD_cat(Match(t, EnumType)->name, "_t");
    case TypeInfoType: return "TypeInfo";
    default: compiler_err(NULL, NULL, NULL, "Not implemented");
    }
}

CORD compile_statement(env_t *env, ast_t *ast)
{
    CORD stmt;
    switch (ast->tag) {
    case If: case When: case For: case While: case FunctionDef: case Return: case StructDef: case EnumDef: case LangDef:
    case Declare: case Assign: case UpdateAssign: case DocTest: case Block:
        stmt = compile(env, ast);
        break;
    default:
        stmt = CORD_asprintf("(void)%r;", compile(env, ast));
        break;
    }
    // int64_t line = get_line_number(ast->file, ast->start);
    // return stmt ? CORD_asprintf("#line %ld\n%r", line, stmt) : stmt;
    return stmt;
}

CORD expr_as_text(env_t *env, CORD expr, type_t *t, CORD color)
{
    switch (t->tag) {
    case MemoryType: return CORD_asprintf("Memory__as_text($stack(%r), %r, &Memory)", expr, color);
    case BoolType: return CORD_asprintf("Bool__as_text($stack(%r), %r, &Bool)", expr, color);
    case IntType: {
        CORD name = type_to_cord(t);
        return CORD_asprintf("%r__as_text($stack(%r), %r, &%r)", name, expr, color, name);
    }
    case NumType: {
        CORD name = type_to_cord(t);
        return CORD_asprintf("%r__as_text($stack(%r), %r, &Num%r)", name, expr, color, name);
    }
    case TextType: {
        const char *lang = Match(t, TextType)->lang;
        return CORD_asprintf("Text__as_text($stack(%r), %r, &%s)", expr, color, lang ? lang : "Text");
    }
    case ArrayType: return CORD_asprintf("Array__as_text($stack(%r), %r, %r)", expr, color, compile_type_info(env, t));
    case TableType: return CORD_asprintf("Table_as_text($stack(%r), %r, %r)", expr, color, compile_type_info(env, t));
    case FunctionType: return CORD_asprintf("Func__as_text($stack(%r), %r, %r)", expr, color, compile_type_info(env, t));
    case PointerType: return CORD_asprintf("Pointer__as_text($stack(%r), %r, %r)", expr, color, compile_type_info(env, t));
    case StructType: case EnumType: return CORD_asprintf("(%r)->CustomInfo.as_text($stack(%r), %r, %r)",
                                                         compile_type_info(env, t), expr, color, compile_type_info(env, t));
    default: compiler_err(NULL, NULL, NULL, "Stringifying is not supported for %T", t);
    }
}

CORD compile_string(env_t *env, ast_t *ast, CORD color)
{
    type_t *t = get_type(env, ast);
    CORD expr = compile(env, ast);
    return expr_as_text(env, expr, t, color);
}

static CORD compile_to_pointer_depth(env_t *env, ast_t *ast, int64_t target_depth, bool allow_optional)
{
    CORD val = compile(env, ast);
    type_t *t = get_type(env, ast);
    int64_t depth = 0;
    for (type_t *tt = t; tt->tag == PointerType; tt = Match(tt, PointerType)->pointed)
        ++depth;

    while (depth != target_depth) {
        if (depth < target_depth) {
            if (ast->tag == Var && target_depth == 1)
                val = CORD_all("(&", val, ")");
            else
                val = CORD_all("$stack(", val, ")");
            t = Type(PointerType, .pointed=t, .is_stack=true);
            ++depth;
        } else {
            auto ptr = Match(t, PointerType);
            if (ptr->is_optional)
                code_err(ast, "You can't dereference this value, since it's not guaranteed to be non-null");
            val = CORD_all("*(", val, ")");
            t = ptr->pointed;
            --depth;
        }
    }
    if (!allow_optional) {
        while (t->tag == PointerType) {
            auto ptr = Match(t, PointerType);
            if (ptr->is_optional)
                code_err(ast, "You can't dereference this value, since it's not guaranteed to be non-null");
            t = ptr->pointed;
        }
    }

    return val;
}

static void check_assignable(env_t *env, ast_t *ast)
{
    if (!can_be_mutated(env, ast)) {
        if (ast->tag == Index || ast->tag == FieldAccess) {
            ast_t *subject = ast->tag == Index ? Match(ast, Index)->indexed : Match(ast, FieldAccess)->fielded;
            code_err(subject, "This is a readonly pointer, which can't be assigned to");
        } else {
            code_err(ast, "This is a value of type %T and can't be assigned to", get_type(env, ast));
        }
    }
}

static CORD compile_arguments(env_t *env, ast_t *call_ast, arg_t *spec_args, arg_ast_t *call_args)
{
    table_t used_args = {};
    CORD code = CORD_EMPTY;
    env_t *default_scope = global_scope(env);
    for (arg_t *spec_arg = spec_args; spec_arg; spec_arg = spec_arg->next) {
        // Find keyword:
        if (spec_arg->name) {
            for (arg_ast_t *call_arg = call_args; call_arg; call_arg = call_arg->next) {
                if (call_arg->name && streq(call_arg->name, spec_arg->name)) {
                    type_t *actual_t = get_type(env, call_arg->value);
                    CORD value = compile(env, call_arg->value);
                    if (!promote(env, &value, actual_t, spec_arg->type))
                        code_err(call_arg->value, "This argument is supposed to be a %T, but this value is a %T", spec_arg->type, actual_t);
                    Table_str_set(&used_args, call_arg->name, call_arg);
                    if (code) code = CORD_cat(code, ", ");
                    code = CORD_cat(code, value);
                    goto found_it;
                }
            }
        }
        // Find positional:
        int64_t i = 1;
        for (arg_ast_t *call_arg = call_args; call_arg; call_arg = call_arg->next) {
            if (call_arg->name) continue;
            const char *pseudoname = heap_strf("%ld", i++);
            if (!Table_str_get(used_args, pseudoname)) {
                type_t *actual_t = get_type(env, call_arg->value);
                CORD value = compile(env, call_arg->value);
                if (!promote(env, &value, actual_t, spec_arg->type))
                    code_err(call_arg->value, "This argument is supposed to be a %T, but this value is a %T", spec_arg->type, actual_t);
                Table_str_set(&used_args, pseudoname, call_arg);
                if (code) code = CORD_cat(code, ", ");
                code = CORD_cat(code, value);
                goto found_it;
            }
        }

        if (spec_arg->default_val) {
            if (code) code = CORD_cat(code, ", ");
            code = CORD_cat(code, compile(default_scope, spec_arg->default_val));
            goto found_it;
        }

        assert(spec_arg->name);
        code_err(call_ast, "The required argument '%s' was not provided", spec_arg->name);
      found_it: continue;
    }

    int64_t i = 1;
    for (arg_ast_t *call_arg = call_args; call_arg; call_arg = call_arg->next) {
        if (call_arg->name) {
            if (!Table_str_get(used_args, call_arg->name))
                code_err(call_arg->value, "There is no argument with the name '%s'", call_arg->name);
        } else {
            const char *pseudoname = heap_strf("%ld", i++);
            if (!Table_str_get(used_args, pseudoname))
                code_err(call_arg->value, "This is one argument too many!");
        }
    }
    return code;
}

CORD compile(env_t *env, ast_t *ast)
{
    switch (ast->tag) {
    case Nil: return CORD_asprintf("$Null(%r)", compile_type_ast(Match(ast, Nil)->type));
    case Bool: return Match(ast, Bool)->b ? "yes" : "no";
    case Var: {
        binding_t *b = get_binding(env, Match(ast, Var)->name);
        if (b)
            return b->code ? b->code : Match(ast, Var)->name;
        return Match(ast, Var)->name;
        // code_err(ast, "I don't know of any variable by this name");
    }
    case Int: return CORD_asprintf("I%ld(%ld)", Match(ast, Int)->bits, Match(ast, Int)->i);
    case Num: {
        // HACK: since the cord library doesn't support the '%a' specifier, this workaround
        // is necessary:
        char *buf = asprintfa(Match(ast, Num)->bits == 64 ? "%a" : "%af", Match(ast, Num)->n);
        return CORD_from_char_star(buf);
    }
    case Length: {
        ast_t *expr = Match(ast, Length)->value;
        type_t *t = get_type(env, expr);
        switch (value_type(t)->tag) {
        case TextType: {
            CORD str = compile_to_pointer_depth(env, expr, 0, false);
            return CORD_all("Text__num_clusters(", str, ")");
        }
        case ArrayType: {
            if (t->tag == PointerType) {
                CORD arr = compile_to_pointer_depth(env, expr, 1, false);
                return CORD_all("I64((", arr, ")->length)");
            } else {
                CORD arr = compile_to_pointer_depth(env, expr, 0, false);
                return CORD_all("I64((", arr, ").length)");
            }
        }
        case TableType: {
            if (t->tag == PointerType) {
                CORD table = compile_to_pointer_depth(env, expr, 1, false);
                return CORD_all("I64((", table, ")->entries.length)");
            } else {
                CORD table = compile_to_pointer_depth(env, expr, 0, false);
                return CORD_all("I64((", table, ").entries.length)");
            }
        }
        default: code_err(ast, "Length is only supported for strings, arrays, and tables, not: %T", t);
        }
        break;
    }
    case Not: return CORD_asprintf("not(%r)", compile(env, Match(ast, Not)->value));
    case Negative: return CORD_asprintf("-(%r)", compile(env, Match(ast, Negative)->value));
    case HeapAllocate: return CORD_asprintf("$heap(%r)", compile(env, Match(ast, HeapAllocate)->value));
    case StackReference: {
        ast_t *subject = Match(ast, StackReference)->value;
        if (can_be_mutated(env, subject))
            return CORD_all("(&", compile(env, subject), ")");
        return CORD_all("$stack(", compile(env, subject), ")");
    }
    case BinaryOp: {
        auto binop = Match(ast, BinaryOp);
        CORD lhs = compile(env, binop->lhs);
        CORD rhs = compile(env, binop->rhs);

        type_t *lhs_t = get_type(env, binop->lhs);
        type_t *rhs_t = get_type(env, binop->rhs);
        type_t *operand_t;
        if (promote(env, &rhs, rhs_t, lhs_t))
            operand_t = lhs_t;
        else if (promote(env, &lhs, lhs_t, rhs_t))
            operand_t = rhs_t;
        else
            code_err(ast, "I can't do operations between %T and %T", lhs_t, rhs_t);

        switch (binop->op) {
        case BINOP_POWER: {
            if (operand_t->tag != NumType && operand_t->tag != IntType)
                code_err(ast, "Exponentiation is only supported for numeric types");
            if (operand_t->tag == NumType && Match(operand_t, NumType)->bits == 32)
                return CORD_all("powf(", lhs, ", ", rhs, ")");
            else
                return CORD_all("pow(", lhs, ", ", rhs, ")");
        }
        case BINOP_MULT: {
            if (operand_t->tag != IntType && operand_t->tag != NumType)
                code_err(ast, "Math operations are only supported for numeric types");
            return CORD_asprintf("(%r * %r)", lhs, rhs);
        }
        case BINOP_DIVIDE: {
            if (operand_t->tag != IntType && operand_t->tag != NumType)
                code_err(ast, "Math operations are only supported for numeric types");
            return CORD_asprintf("(%r / %r)", lhs, rhs);
        }
        case BINOP_MOD: {
            if (operand_t->tag != IntType && operand_t->tag != NumType)
                code_err(ast, "Math operations are only supported for numeric types");
            return CORD_asprintf("mod(%r, %r)", lhs, rhs);
        }
        case BINOP_MOD1: {
            if (operand_t->tag != IntType && operand_t->tag != NumType)
                code_err(ast, "Math operations are only supported for numeric types");
            return CORD_asprintf("mod1(%r, %r)", lhs, rhs);
        }
        case BINOP_PLUS: {
            if (operand_t->tag != IntType && operand_t->tag != NumType)
                code_err(ast, "Math operations are only supported for numeric types");
            return CORD_asprintf("(%r + %r)", lhs, rhs);
        }
        case BINOP_MINUS: {
            if (operand_t->tag != IntType && operand_t->tag != NumType)
                code_err(ast, "Math operations are only supported for numeric types");
            return CORD_asprintf("(%r - %r)", lhs, rhs);
        }
        case BINOP_LSHIFT: {
            if (operand_t->tag != IntType && operand_t->tag != NumType)
                code_err(ast, "Math operations are only supported for numeric types");
            return CORD_asprintf("(%r << %r)", lhs, rhs);
        }
        case BINOP_RSHIFT: {
            if (operand_t->tag != IntType && operand_t->tag != NumType)
                code_err(ast, "Math operations are only supported for numeric types");
            return CORD_asprintf("(%r >> %r)", lhs, rhs);
        }
        case BINOP_EQ: {
            switch (operand_t->tag) {
            case BoolType: case IntType: case NumType: case PointerType: case FunctionType:
                return CORD_asprintf("(%r == %r)", lhs, rhs);
            default:
                return CORD_asprintf("generic_equal($stack(%r), $stack(%r), %r)", lhs, rhs, compile_type_info(env, operand_t));
            }
        }
        case BINOP_NE: {
            switch (operand_t->tag) {
            case BoolType: case IntType: case NumType: case PointerType: case FunctionType:
                return CORD_asprintf("(%r != %r)", lhs, rhs);
            default:
                return CORD_asprintf("!generic_equal($stack(%r), $stack(%r), %r)", lhs, rhs, compile_type_info(env, operand_t));
            }
        }
        case BINOP_LT: {
            switch (operand_t->tag) {
            case BoolType: case IntType: case NumType: case PointerType: case FunctionType:
                return CORD_asprintf("(%r < %r)", lhs, rhs);
            default:
                return CORD_asprintf("(generic_compare($stack(%r), $stack(%r), %r) < 0)", lhs, rhs, compile_type_info(env, operand_t));
            }
        }
        case BINOP_LE: {
            switch (operand_t->tag) {
            case BoolType: case IntType: case NumType: case PointerType: case FunctionType:
                return CORD_asprintf("(%r <= %r)", lhs, rhs);
            default:
                return CORD_asprintf("(generic_compare($stack(%r), $stack(%r), %r) <= 0)", lhs, rhs, compile_type_info(env, operand_t));
            }
        }
        case BINOP_GT: {
            switch (operand_t->tag) {
            case BoolType: case IntType: case NumType: case PointerType: case FunctionType:
                return CORD_asprintf("(%r > %r)", lhs, rhs);
            default:
                return CORD_asprintf("(generic_compare($stack(%r), $stack(%r), %r) > 0)", lhs, rhs, compile_type_info(env, operand_t));
            }
        }
        case BINOP_GE: {
            switch (operand_t->tag) {
            case BoolType: case IntType: case NumType: case PointerType: case FunctionType:
                return CORD_asprintf("(%r >= %r)", lhs, rhs);
            default:
                return CORD_asprintf("(generic_compare($stack(%r), $stack(%r), %r) >= 0)", lhs, rhs, compile_type_info(env, operand_t));
            }
        }
        case BINOP_AND: {
            if (operand_t->tag == BoolType)
                return CORD_asprintf("(%r && %r)", lhs, rhs);
            else if (operand_t->tag == IntType)
                return CORD_asprintf("(%r & %r)", lhs, rhs);
            else
                code_err(ast, "Boolean operators are only supported for Bool and integer types");
        }
        case BINOP_OR: {
            if (operand_t->tag == BoolType)
                return CORD_asprintf("(%r || %r)", lhs, rhs);
            else if (operand_t->tag == IntType)
                return CORD_asprintf("(%r | %r)", lhs, rhs);
            else
                code_err(ast, "Boolean operators are only supported for Bool and integer types");
        }
        case BINOP_XOR: {
            if (operand_t->tag == BoolType || operand_t->tag == IntType)
                return CORD_asprintf("(%r ^ %r)", lhs, rhs);
            else
                code_err(ast, "Boolean operators are only supported for Bool and integer types");
        }
        case BINOP_CONCAT: {
            switch (operand_t->tag) {
            case TextType: {
                return CORD_all("CORD_cat(", lhs, ", ", rhs, ")");
            }
            case ArrayType: {
                return CORD_all("Array__concat(", lhs, ", ", rhs, ", ", compile_type_info(env, operand_t), ")");
            }
            default:
                code_err(ast, "Concatenation isn't supported for %T types", operand_t);
            }
        }
        default: break;
        }
        code_err(ast, "unimplemented binop");
    }
    case UpdateAssign: {
        auto update = Match(ast, UpdateAssign);
        check_assignable(env, update->lhs);
        CORD lhs = compile(env, update->lhs);
        CORD rhs = compile(env, update->rhs);

        type_t *lhs_t = get_type(env, update->lhs);
        type_t *rhs_t = get_type(env, update->rhs);
        type_t *operand_t;
        if (promote(env, &rhs, rhs_t, lhs_t))
            operand_t = lhs_t;
        else if (promote(env, &lhs, lhs_t, rhs_t))
            operand_t = rhs_t;
        else if (lhs_t->tag == ArrayType && promote(env, &rhs, rhs_t, Match(lhs_t, ArrayType)->item_type))
            operand_t = lhs_t;
        else
            code_err(ast, "I can't do operations between %T and %T", lhs_t, rhs_t);

        switch (update->op) {
        case BINOP_MULT: return CORD_asprintf("%r *= %r;", lhs, rhs);
        case BINOP_DIVIDE: return CORD_asprintf("%r /= %r;", lhs, rhs);
        case BINOP_MOD: return CORD_asprintf("%r = mod(%r, %r);", lhs, lhs, rhs);
        case BINOP_MOD1: return CORD_asprintf("%r = mod1(%r, %r);", lhs, lhs, rhs);
        case BINOP_PLUS: return CORD_asprintf("%r += %r;", lhs, rhs);
        case BINOP_MINUS: return CORD_asprintf("%r -= %r;", lhs, rhs);
        case BINOP_POWER: {
            if (lhs_t->tag != NumType)
                code_err(ast, "'^=' is only supported for Num types");
            if (lhs_t->tag == NumType && Match(lhs_t, NumType)->bits == 32)
                return CORD_all(lhs, " = powf(", lhs, ", ", rhs, ")");
            else
                return CORD_all(lhs, " = pow(", lhs, ", ", rhs, ")");
        }
        case BINOP_LSHIFT: return CORD_asprintf("%r <<= %r;", lhs, rhs);
        case BINOP_RSHIFT: return CORD_asprintf("%r >>= %r;", lhs, rhs);
        case BINOP_AND: {
            if (operand_t->tag == BoolType)
                return CORD_asprintf("if (%r) %r = %r;", lhs, lhs, rhs);
            else if (operand_t->tag == IntType)
                return CORD_asprintf("%r &= %r;", lhs, rhs);
            else
                code_err(ast, "'or=' is not implemented for %T types", operand_t);
        }
        case BINOP_OR: {
            if (operand_t->tag == BoolType)
                return CORD_asprintf("if (!(%r)) %r = %r;", lhs, lhs, rhs);
            else if (operand_t->tag == IntType)
                return CORD_asprintf("%r |= %r;", lhs, rhs);
            else
                code_err(ast, "'or=' is not implemented for %T types", operand_t);
        }
        case BINOP_XOR: return CORD_asprintf("%r ^= %r;", lhs, rhs);
        case BINOP_CONCAT: {
            if (operand_t->tag == TextType) {
                return CORD_asprintf("%r = CORD_cat(%r, %r);", lhs, lhs, rhs);
            } else if (operand_t->tag == ArrayType) {
                if (promote(env, &rhs, rhs_t, Match(lhs_t, ArrayType)->item_type)) {
                    // arr ++= item
                    if (update->lhs->tag == Var)
                        return CORD_all("Array__insert(&", lhs, ", $stack(", rhs, "), 0, ", compile_type_info(env, operand_t), ")");
                    else
                        return CORD_all(lhs, "Array__concat(", lhs, ", $Array(", rhs, "), ", compile_type_info(env, operand_t), ")");
                } else {
                    // arr ++= [...]
                    if (update->lhs->tag == Var)
                        return CORD_all("Array__insert_all(&", lhs, ", ", rhs, ", 0, ", compile_type_info(env, operand_t), ")");
                    else
                        return CORD_all(lhs, "Array__concat(", lhs, ", ", rhs, ", ", compile_type_info(env, operand_t), ")");
                }
            } else {
                code_err(ast, "'++=' is not implemented for %T types", operand_t);
            }
        }
        default: code_err(ast, "Update assignments are not implemented for this operation");
        }
    }
    case TextLiteral: {
        CORD literal = Match(ast, TextLiteral)->cord; 
        if (literal == CORD_EMPTY)
            return "(CORD)CORD_EMPTY";
        CORD code = "(CORD)\"";
        CORD_pos i;
        CORD_FOR(i, literal) {
            char c = CORD_pos_fetch(i);
            switch (c) {
            case '\\': code = CORD_cat(code, "\\\\"); break;
            case '"': code = CORD_cat(code, "\\\""); break;
            case '\a': code = CORD_cat(code, "\\a"); break;
            case '\b': code = CORD_cat(code, "\\b"); break;
            case '\n': code = CORD_cat(code, "\\n"); break;
            case '\r': code = CORD_cat(code, "\\r"); break;
            case '\t': code = CORD_cat(code, "\\t"); break;
            case '\v': code = CORD_cat(code, "\\v"); break;
            default: {
                if (isprint(c))
                    code = CORD_cat_char(code, c);
                else
                    CORD_sprintf(&code, "%r\\x%02X", code, (uint8_t)c);
                break;
            }
            }
        }
        return CORD_cat_char(code, '"');
    }
    case TextJoin: {
        const char *lang = Match(ast, TextJoin)->lang;
        type_t *text_t = Table_str_get(*env->types, lang ? lang : "Text");
        if (!text_t || text_t->tag != TextType)
            code_err(ast, "%s is not a valid text language name", lang);
        table_t *lang_ns = lang ? Table_str_get(*env->type_namespaces, lang) : NULL;
        ast_list_t *chunks = Match(ast, TextJoin)->children;
        if (!chunks) {
            return "(CORD)CORD_EMPTY";
        } else if (!chunks->next && chunks->ast->tag == TextLiteral) {
            return compile(env, chunks->ast);
        } else {
            CORD code = "CORD_all(";
            for (ast_list_t *chunk = chunks; chunk; chunk = chunk->next) {
                CORD chunk_code;
                type_t *chunk_t = get_type(env, chunk->ast);
                if (chunk->ast->tag == TextLiteral) {
                    chunk_code = compile(env, chunk->ast);
                } else if (chunk_t->tag == TextType && streq(Match(chunk_t, TextType)->lang, lang)) {
                    chunk_code = compile(env, chunk->ast);
                } else if (lang && lang_ns) {
                    // Get conversion function:
                    chunk_code = compile(env, chunk->ast);
                    for (int64_t i = 1; i <= Table_length(*lang_ns); i++) {
                        struct {const char *name; binding_t *b; } *entry = Table_entry(*lang_ns, i);
                        if (entry->b->type->tag != FunctionType) continue;
                        if (!(streq(entry->name, "escape") || strncmp(entry->name, "escape_", strlen("escape_")) == 0))
                            continue;
                        auto fn = Match(entry->b->type, FunctionType);
                        if (!fn->args || fn->args->next) continue;
                        if (fn->ret->tag != TextType || !streq(Match(fn->ret, TextType)->lang, lang))
                            continue;
                        if (!promote(env, &chunk_code, chunk_t, fn->args->type))
                            continue;
                        chunk_code = CORD_all(entry->b->code, "(", chunk_code, ")");
                        goto found_conversion;
                    }
                    code_err(chunk->ast, "I don't know how to convert %T to %T", chunk_t, text_t);
                  found_conversion:;
                } else {
                    chunk_code = compile_string(env, chunk->ast, "no");
                }
                code = CORD_cat(code, chunk_code);
                if (chunk->next) code = CORD_cat(code, ", ");
            }
            return CORD_cat(code, ")");
        }
    }
    case Block: {
        ast_list_t *stmts = Match(ast, Block)->statements;
        if (stmts && !stmts->next)
            return compile_statement(env, stmts->ast);

        CORD code = "{\n";
        env = fresh_scope(env);
        for (ast_list_t *stmt = stmts; stmt; stmt = stmt->next) {
            bind_statement(env, stmt->ast);
            code = CORD_all(code, compile_statement(env, stmt->ast), "\n");
        }
        return CORD_cat(code, "}");
    }
    case Declare: {
        auto decl = Match(ast, Declare);
        type_t *t = get_type(env, decl->value);
        return CORD_all(compile_declaration(t, Match(decl->var, Var)->name), " = ", compile(env, decl->value), ";");
    }
    case Assign: {
        auto assign = Match(ast, Assign);
        // Single assignment:
        if (assign->targets && !assign->targets->next)
            return CORD_asprintf("%r = %r;", compile(env, assign->targets->ast), compile(env, assign->values->ast));

        CORD code = "{ // Assignment\n";
        int64_t i = 1;
        for (ast_list_t *value = assign->values; value; value = value->next)
            CORD_appendf(&code, "%r $%ld = %r;\n", compile_type(get_type(env, value->ast)), i++, compile(env, value->ast));
        i = 1;
        for (ast_list_t *target = assign->targets; target; target = target->next) {
            check_assignable(env, target->ast);
            CORD_appendf(&code, "%r = $%ld;\n", compile(env, target->ast), i++);
        }
        return CORD_cat(code, "\n}");
    }
    case Min: case Max: {
        type_t *t = get_type(env, ast);
        ast_t *key = ast->tag == Min ? Match(ast, Min)->key : Match(ast, Max)->key;
        ast_t *lhs = ast->tag == Min ? Match(ast, Min)->lhs : Match(ast, Max)->lhs;
        ast_t *rhs = ast->tag == Min ? Match(ast, Min)->rhs : Match(ast, Max)->rhs;
        const char *key_name = "$";
        if (key == NULL) key = FakeAST(Var, key_name);

        env_t *expr_env = fresh_scope(env);
        set_binding(expr_env, key_name, new(binding_t, .type=t, .code="$ternary$lhs"));
        CORD lhs_key = compile(expr_env, key);

        set_binding(expr_env, key_name, new(binding_t, .type=t, .code="$ternary$rhs"));
        CORD rhs_key = compile(expr_env, key);

        type_t *key_t = get_type(expr_env, key);
        CORD comparison;
        if (key_t->tag == IntType || key_t->tag == NumType || key_t->tag == BoolType || key_t->tag == PointerType)
            comparison = CORD_all("((", lhs_key, ")", (ast->tag == Min ? "<=" : ">="), "(", rhs_key, "))");
        else if (key_t->tag == TextType)
            comparison = CORD_all("CORD_cmp(", lhs_key, ", ", rhs_key, ")", (ast->tag == Min ? "<=" : ">="), "0");
        else
            comparison = CORD_all("generic_compare($stack(", lhs_key, "), $stack(", rhs_key, "), ", compile_type_info(env, key_t), ")",
                                  (ast->tag == Min ? "<=" : ">="), "0");

        return CORD_all(
            "({\n",
            compile_type(t), " $ternary$lhs = ", compile(env, lhs), ", $ternary$rhs = ", compile(env, rhs), ";\n",
            comparison, " ? $ternary$lhs : $ternary$rhs;\n"
            "})");
    }
    case Array: {
        auto array = Match(ast, Array);
        if (!array->items)
            return "(array_t){.length=0}";

        int64_t n = 0;
        for (ast_list_t *item = array->items; item; item = item->next)
            ++n;

        type_t *item_type = Match(get_type(env, ast), ArrayType)->item_type;
        CORD code = CORD_all("$TypedArrayN(", compile_type(item_type), CORD_asprintf(", %ld", n));
        for (ast_list_t *item = array->items; item; item = item->next)
            code = CORD_all(code, ", ", compile(env, item->ast));
        return CORD_cat(code, ")");
    }
    case Table: {
        auto table = Match(ast, Table);
        if (!table->entries) {
            CORD code = "(table_t){";
            if (table->fallback)
                code = CORD_all(code, ".fallback=", compile(env, table->fallback),",");
            if (table->default_value)
                code = CORD_all(code, ".default_value=$heap(", compile(env, table->default_value),"),");
            return CORD_cat(code, "}");
        }
           
        type_t *table_t = get_type(env, ast);
        type_t *key_t = Match(table_t, TableType)->key_type;
        type_t *value_t = Match(table_t, TableType)->value_type;
        CORD code = CORD_all("$Table(",
                             compile_type(key_t), ", ",
                             compile_type(value_t), ", ",
                             compile_type_info(env, key_t), ", ",
                             compile_type_info(env, value_t));
        if (table->fallback)
            code = CORD_all(code, ", /*fallback:*/ $heap(", compile(env, table->fallback), ")");
        else
            code = CORD_all(code, ", /*fallback:*/ NULL");

        if (table->default_value)
            code = CORD_all(code, ", /*default:*/ $heap(", compile(env, table->default_value), ")");
        else
            code = CORD_all(code, ", /*default:*/ NULL");

        size_t n = 0;
        for (ast_list_t *entry = table->entries; entry; entry = entry->next)
            ++n;
        CORD_appendf(&code, ", %zu", n);

        for (ast_list_t *entry = table->entries; entry; entry = entry->next) {
            auto e = Match(entry->ast, TableEntry);
            code = CORD_all(code, ",\n\t{", compile(env, e->key), ", ", compile(env, e->value), "}");
        }
        return CORD_cat(code, ")");

    }
    case FunctionDef: {
        auto fndef = Match(ast, FunctionDef);
        CORD name = compile(env, fndef->name);
        type_t *ret_t = fndef->ret_type ? parse_type_ast(env, fndef->ret_type) : Type(VoidType);

        CORD arg_signature = "(";
        for (arg_ast_t *arg = fndef->args; arg; arg = arg->next) {
            type_t *arg_type = get_arg_ast_type(env, arg);
            arg_signature = CORD_cat(arg_signature, compile_declaration(arg_type, arg->name));
            if (arg->next) arg_signature = CORD_cat(arg_signature, ", ");
        }
        arg_signature = CORD_cat(arg_signature, ")");

        CORD ret_type_code = compile_type(ret_t);

        if (fndef->is_private)
            env->code->staticdefs = CORD_all(env->code->staticdefs, "static ", ret_type_code, " ", name, arg_signature, ";\n");
        else
            env->code->fndefs = CORD_all(env->code->fndefs, ret_type_code, " ", name, arg_signature, ";\n");

        CORD code;
        if (fndef->cache) {
            code = CORD_all("static ", ret_type_code, " ", name, "$uncached", arg_signature);
        } else {
            code = CORD_all(ret_type_code, " ", name, arg_signature);
            if (fndef->is_inline)
                code = CORD_cat("inline ", code);
            if (!fndef->is_private)
                code = CORD_cat("public ", code);
        }

        env_t *body_scope = global_scope(env);
        for (arg_ast_t *arg = fndef->args; arg; arg = arg->next) {
            type_t *arg_type = get_arg_ast_type(env, arg);
            set_binding(body_scope, arg->name, new(binding_t, .type=arg_type, .code=arg->name));
        }

        fn_context_t fn_ctx = (fn_context_t){
            .return_type=ret_t,
            .closure_scope=NULL,
            .closed_vars=NULL,
        };
        body_scope->fn_ctx = &fn_ctx;

        CORD body = compile(body_scope, fndef->body);
        if (CORD_fetch(body, 0) != '{')
            body = CORD_asprintf("{\n%r\n}", body);
        env->code->funcs = CORD_all(env->code->funcs, code, " ", body, "\n");

        if (fndef->cache && fndef->cache->tag == Int && Match(fndef->cache, Int)->i > 0) {
            const char *arg_type_name = heap_strf("%s$args", CORD_to_const_char_star(name));
            ast_t *args_def = FakeAST(StructDef, .name=arg_type_name, .fields=fndef->args);
            bind_statement(env, args_def);
            (void)compile(env, args_def);
            type_t *args_t = Table_str_get(*env->types, arg_type_name);
            assert(args_t);

            CORD all_args = CORD_EMPTY;
            for (arg_ast_t *arg = fndef->args; arg; arg = arg->next)
                all_args = CORD_all(all_args, arg->name, arg->next ? ", " : CORD_EMPTY);

            CORD pop_code = CORD_EMPTY;
            if (fndef->cache->tag == Int && Match(fndef->cache, Int)->i < INT64_MAX) {
                pop_code = CORD_all("if (Table_length($cache) > ", compile(body_scope, fndef->cache),
                                    ") Table_remove(&$cache, NULL, $table_info);\n");
            }

            CORD wrapper = CORD_all(
                fndef->is_private ? CORD_EMPTY : "public ", ret_type_code, " ", name, arg_signature, "{\n"
                "static table_t $cache = {};\n",
                compile_type(args_t), " $args = {", all_args, "};\n"
                "static const TypeInfo *$table_type = $TableInfo(", compile_type_info(env, args_t), ", ", compile_type_info(env, ret_t), ");\n",
                ret_type_code, "*$cached = Table_get_raw($cache, &$args, $table_type);\n"
                "if ($cached) return *$cached;\n",
                ret_type_code, " $ret = ", name, "$uncached(", all_args, ");\n",
                pop_code,
                "Table_set(&$cache, &$args, &$ret, $table_type);\n"
                "return $ret;\n"
                "}\n");
            env->code->funcs = CORD_cat(env->code->funcs, wrapper);
        }

        return CORD_EMPTY;
    }
    case Lambda: {
        auto lambda = Match(ast, Lambda);
        static int64_t lambda_number = 1;
        CORD name = CORD_asprintf("lambda$%ld", lambda_number++);

        env_t *body_scope = fresh_scope(env);
        for (arg_ast_t *arg = lambda->args; arg; arg = arg->next) {
            type_t *arg_type = get_arg_ast_type(env, arg);
            set_binding(body_scope, arg->name, new(binding_t, .type=arg_type, .code=arg->name));
        }

        type_t *ret_t = get_type(body_scope, lambda->body);

        fn_context_t fn_ctx = (fn_context_t){
            .return_type=ret_t,
            .closure_scope=body_scope->locals->fallback,
            .closed_vars=new(table_t),
        };
        body_scope->fn_ctx = &fn_ctx;
        body_scope->locals->fallback = env->globals;

        CORD code = CORD_all("static ", compile_type(ret_t), " ", name, "(");
        for (arg_ast_t *arg = lambda->args; arg; arg = arg->next) {
            type_t *arg_type = get_arg_ast_type(env, arg);
            code = CORD_all(code, compile_type(arg_type), " ", arg->name, ", ");
        }

        for (ast_list_t *stmt = Match(lambda->body, Block)->statements; stmt; stmt = stmt->next)
            (void)compile_statement(body_scope, stmt->ast);

        CORD userdata;
        if (Table_length(*fn_ctx.closed_vars) == 0) {
            code = CORD_cat(code, "void *$userdata)");
            userdata = "NULL";
        } else {
            CORD def = "typedef struct {";
            userdata = CORD_all("new(", name, "$userdata_t");
            for (int64_t i = 1; i <= Table_length(*fn_ctx.closed_vars); i++) {
                struct { const char *name; binding_t *b; } *entry = Table_entry(*fn_ctx.closed_vars, i);
                def = CORD_all(def, compile_declaration(entry->b->type, entry->name), "; ");
                userdata = CORD_all(userdata, ", ", entry->b->code);
            }
            userdata = CORD_all(userdata, ")");
            def = CORD_all(def, "} ", name, "$userdata_t;");
            env->code->typedefs = CORD_cat(env->code->typedefs, def);
            code = CORD_all(code, name, "$userdata_t *$userdata)");
        }

        CORD body = CORD_EMPTY;
        for (ast_list_t *stmt = Match(lambda->body, Block)->statements; stmt; stmt = stmt->next) {
            if (stmt->next || ret_t->tag == VoidType || ret_t->tag == AbortType)
                body = CORD_all(body, compile_statement(body_scope, stmt->ast), "\n");
            else
                body = CORD_all(body, compile_statement(body_scope, FakeAST(Return, stmt->ast)), "\n");
        }
        env->code->funcs = CORD_all(env->code->funcs, code, " {\n", body, "\n}");
        return CORD_all("(closure_t){", name, ", ", userdata, "}");
    }
    case MethodCall: {
        auto call = Match(ast, MethodCall);
        type_t *self_t = get_type(env, call->self);
        type_t *self_value_t = value_type(self_t);
        switch (self_value_t->tag) {
        case ArrayType: {
            // TODO: check for readonly
            if (streq(call->name, "insert")) {
                type_t *item_t = Match(self_value_t, ArrayType)->item_type;
                CORD self = compile_to_pointer_depth(env, call->self, 1, false);
                arg_t *arg_spec = new(arg_t, .name="item", .type=Type(PointerType, .pointed=item_t, .is_stack=true, .is_readonly=true),
                                      .next=new(arg_t, .name="at", .type=Type(IntType, .bits=64), .default_val=FakeAST(Int, .i=0, .bits=64)));
                return CORD_all("Array__insert(", self, ", ", compile_arguments(env, ast, arg_spec, call->args), ", ",
                                compile_type_info(env, self_value_t), ")");
            } else if (streq(call->name, "insert_all")) {
                CORD self = compile_to_pointer_depth(env, call->self, 1, false);
                arg_t *arg_spec = new(arg_t, .name="items", .type=self_value_t,
                                      .next=new(arg_t, .name="at", .type=Type(IntType, .bits=64), .default_val=FakeAST(Int, .i=0, .bits=64)));
                return CORD_all("Array__insert_all(", self, ", ", compile_arguments(env, ast, arg_spec, call->args), ", ",
                                compile_type_info(env, self_value_t), ")");
            } else if (streq(call->name, "remove")) {
                CORD self = compile_to_pointer_depth(env, call->self, 1, false);
                arg_t *arg_spec = new(arg_t, .name="index", .type=Type(IntType, .bits=64), .default_val=FakeAST(Int, .i=-1, .bits=64),
                                      .next=new(arg_t, .name="count", .type=Type(IntType, .bits=64), .default_val=FakeAST(Int, .i=1, .bits=64)));
                return CORD_all("Array__remove(", self, ", ", compile_arguments(env, ast, arg_spec, call->args), ", ",
                                compile_type_info(env, self_value_t), ")");
            } else if (streq(call->name, "random")) {
                CORD self = compile_to_pointer_depth(env, call->self, 0, false);
                return CORD_all("Array__random(", self, ")");
            } else if (streq(call->name, "shuffle")) {
                CORD self = compile_to_pointer_depth(env, call->self, 1, false);
                return CORD_all("Array__shuffle(", self, ", ", compile_type_info(env, self_value_t), ")");
            } else if (streq(call->name, "sort")) {
                CORD self = compile_to_pointer_depth(env, call->self, 1, false);
                return CORD_all("Array__sort(", self, ", ", compile_type_info(env, self_value_t), ")");
            } else if (streq(call->name, "clear")) {
                CORD self = compile_to_pointer_depth(env, call->self, 1, false);
                return CORD_all("Array__compact(", self, ")");
            } else if (streq(call->name, "slice")) {
                CORD self = compile_to_pointer_depth(env, call->self, 1, false);
                arg_t *arg_spec = new(arg_t, .name="first", .type=Type(IntType, .bits=64), .default_val=FakeAST(Int, .i=1, .bits=64),
                                      .next=new(arg_t, .name="length", .type=Type(IntType, .bits=64), .default_val=FakeAST(Int, .i=INT64_MAX, .bits=64),
                                                .next=new(arg_t, .name="stride", .type=Type(IntType, .bits=64), .default_val=FakeAST(Int, .i=1, .bits=64))));
                return CORD_all("Array__slice(", self, ", ", compile_arguments(env, ast, arg_spec, call->args), ", ",
                                compile_type_info(env, self_value_t), ")");
            } else code_err(ast, "There is no '%s' method for arrays", call->name);
        }
        case TableType: {
            auto table = Match(self_value_t, TableType);
            if (streq(call->name, "get")) {
                CORD self = compile_to_pointer_depth(env, call->self, 0, false);
                arg_t *arg_spec = new(arg_t, .name="key", .type=Type(PointerType, .pointed=table->key_type, .is_stack=true, .is_readonly=true));
                return CORD_all("Table_get(", self, ", ", compile_arguments(env, ast, arg_spec, call->args), ", ",
                                compile_type_info(env, self_value_t), ")");
            } else if (streq(call->name, "set")) {
                CORD self = compile_to_pointer_depth(env, call->self, 1, false);
                arg_t *arg_spec = new(arg_t, .name="key", .type=Type(PointerType, .pointed=table->key_type, .is_stack=true, .is_readonly=true),
                                      .next=new(arg_t, .name="value", .type=Type(PointerType, .pointed=table->value_type, .is_stack=true, .is_readonly=true)));
                return CORD_all("Table_set(", self, ", ", compile_arguments(env, ast, arg_spec, call->args), ", ",
                                compile_type_info(env, self_value_t), ")");
            } else if (streq(call->name, "remove")) {
                CORD self = compile_to_pointer_depth(env, call->self, 1, false);
                arg_t *arg_spec = new(arg_t, .name="key", .type=Type(PointerType, .pointed=table->key_type, .is_stack=true, .is_readonly=true));
                return CORD_all("Table_remove(", self, ", ", compile_arguments(env, ast, arg_spec, call->args), ", ",
                                compile_type_info(env, self_value_t), ")");
            } else if (streq(call->name, "clear")) {
                CORD self = compile_to_pointer_depth(env, call->self, 1, false);
                return CORD_all("Table_clear(", self, ")");
            } else code_err(ast, "There is no '%s' method for tables", call->name);
        }
        default: {
            auto call = Match(ast, MethodCall);
            type_t *fn_t = get_method_type(env, call->self, call->name);
            arg_ast_t *args = new(arg_ast_t, .value=call->self, .next=call->args);
            binding_t *b = get_namespace_binding(env, call->self, call->name);
            if (!b) code_err(ast, "No such method");
            return CORD_all(b->code, "(", compile_arguments(env, ast, Match(fn_t, FunctionType)->args, args), ")");
        }
        }
    }
    case FunctionCall: {
        auto call = Match(ast, FunctionCall);
        type_t *fn_t = get_type(env, call->fn);
        if (fn_t->tag == FunctionType) {
            CORD fn = compile(env, call->fn);
            return CORD_all(fn, "(", compile_arguments(env, ast, Match(fn_t, FunctionType)->args, call->args), ")");
        } else if (fn_t->tag == TypeInfoType) {
            type_t *t = Match(fn_t, TypeInfoType)->type;
            if (!(t->tag == StructType))
                code_err(call->fn, "This is not a type that has a constructor");
            fn_t = Type(FunctionType, .args=Match(t, StructType)->fields, .ret=t);
            CORD fn = compile(env, call->fn);
            return CORD_all(fn, "(", compile_arguments(env, ast, Match(fn_t, FunctionType)->args, call->args), ")");
        } else if (fn_t->tag == ClosureType) {
            fn_t = Match(fn_t, ClosureType)->fn;
            arg_t *type_args = Match(fn_t, FunctionType)->args;

            arg_t *closure_fn_args = NULL;
            for (arg_t *arg = Match(fn_t, FunctionType)->args; arg; arg = arg->next)
                closure_fn_args = new(arg_t, .name=arg->name, .type=arg->type, .default_val=arg->default_val, .next=closure_fn_args);
            closure_fn_args = new(arg_t, .name="$userdata", .type=Type(PointerType, .pointed=Type(MemoryType)), .next=closure_fn_args);
            REVERSE_LIST(closure_fn_args);
            CORD fn_type_code = compile_type(Type(FunctionType, .args=closure_fn_args, .ret=Match(fn_t, FunctionType)->ret));

            CORD closure = compile(env, call->fn);
            CORD arg_code = compile_arguments(env, ast, type_args, call->args);
            if (arg_code) arg_code = CORD_cat(arg_code, ", ");
            if (call->fn->tag == Var) {
                return CORD_all("((", fn_type_code, ")", closure, ".fn)(", arg_code, closure, ".userdata)");
            } else {
                return CORD_all("({ closure_t $closure = ", closure, "; ((", fn_type_code, ")$closure.fn)(",
                                arg_code, "$closure.userdata); })");
            }
        } else {
            code_err(call->fn, "This is not a function, it's a %T", fn_t);
        }
    }
    case If: {
        auto if_ = Match(ast, If);
        if (if_->condition->tag == Declare) {
            auto decl = Match(if_->condition, Declare);
            env_t *true_scope = fresh_scope(env);
            const char *name = Match(decl->var, Var)->name;
            CORD var_code = CORD_cat(env->scope_prefix, name);
            type_t *var_t = get_type(env, decl->value);
            if (var_t->tag == PointerType) {
                auto ptr = Match(var_t, PointerType);
                if (!ptr->is_optional)
                    code_err(if_->condition, "This pointer will always be non-null, so it should not be used in a conditional.");
                var_t = Type(PointerType, .pointed=ptr->pointed, .is_optional=false, .is_stack=ptr->is_stack, .is_readonly=ptr->is_readonly);
            } else {
                code_err(if_->condition, "Only optional pointer types can be used in 'if var := ...' statements (this is a %T)", var_t);
            }
            set_binding(true_scope, name, new(binding_t, .type=var_t, .code=var_code));
            CORD code = CORD_all("{\n",
                                 compile_type(var_t), " ", var_code, " = ", compile(env, decl->value), ";\n"
                                 "if (", var_code, ") ", compile_statement(true_scope, if_->body));
            if (if_->else_body)
                code = CORD_all(code, "\nelse ", compile_statement(env, if_->else_body));
            code = CORD_cat(code, "\n}");
            return code;
        } else {
            type_t *cond_t = get_type(env, if_->condition);
            if (cond_t->tag == PointerType) {
                if (!Match(cond_t, PointerType)->is_optional)
                    code_err(if_->condition, "This pointer will always be non-null, so it should not be used in a conditional.");
            } else if (cond_t->tag != BoolType) {
                code_err(if_->condition, "Only boolean values and optional pointers can be used in conditionals (this is a %T)", cond_t);
            }
            CORD code;
            CORD_sprintf(&code, "if (%r) %r", compile(env, if_->condition), compile_statement(env, if_->body));
            if (if_->else_body)
                code = CORD_all(code, "\nelse ", compile_statement(env, if_->else_body));
            return code;
        }
    }
    case When: {
        auto when = Match(ast, When);
        type_t *subject_t = get_type(env, when->subject);
        auto enum_t = Match(subject_t, EnumType);
        CORD code = CORD_all("{ ", compile_type(subject_t), " $subject = ", compile(env, when->subject), ";\n"
                             "switch ($subject.$tag) {");
        type_t *result_t = get_type(env, ast);
        (void)result_t;
        for (when_clause_t *clause = when->clauses; clause; clause = clause->next) {
            const char *clause_tag_name = Match(clause->tag_name, Var)->name;
            code = CORD_all(code, "case $tag$", enum_t->name, "$", clause_tag_name, ": {\n");
            type_t *tag_type = NULL;
            for (tag_t *tag = enum_t->tags; tag; tag = tag->next) {
                if (streq(tag->name, clause_tag_name)) {
                    tag_type = tag->type;
                    break;
                }
            }
            assert(tag_type);
            env_t *scope = env;
            if (clause->var) {
                code = CORD_all(code, compile_type(tag_type), " ", compile(env, clause->var), " = $subject.", clause_tag_name, ";\n");
                scope = fresh_scope(env);
                set_binding(scope, Match(clause->var, Var)->name, new(binding_t, .type=tag_type));
            }
            code = CORD_all(code, compile(scope, clause->body), "\nbreak;\n}\n");
        }
        if (when->else_body) {
            code = CORD_all(code, "default: {\n", compile(env, when->else_body), "\nbreak;\n}");
        }
        code = CORD_all(code, "\n}\n}");
        return code;
    }
    case While: {
        auto while_ = Match(ast, While);
        return CORD_asprintf("while (%r) %r", compile(env, while_->condition), compile(env, while_->body));
    }
    case For: {
        auto for_ = Match(ast, For);
        type_t *iter_t = get_type(env, for_->iter);
        switch (iter_t->tag) {
        case ArrayType: {
            type_t *item_t = Match(iter_t, ArrayType)->item_type;
            env_t *scope = fresh_scope(env);
            CORD index = for_->index ? compile(env, for_->index) : "$i";
            if (for_->index)
                set_binding(scope, CORD_to_const_char_star(index), new(binding_t, .type=Type(IntType, .bits=64)));
            CORD value = compile(env, for_->value);
            set_binding(scope, CORD_to_const_char_star(value), new(binding_t, .type=item_t));
            return CORD_all("$ARRAY_FOREACH(", compile(env, for_->iter), ", ", index, ", ", compile_type(item_t), ", ", value, ", ",
                            compile(scope, for_->body), ", ", for_->empty ? compile(env, for_->empty) : "{}", ")");
        }
        case TableType: {
            type_t *key_t = Match(iter_t, TableType)->key_type;
            type_t *value_t = Match(iter_t, TableType)->value_type;
            env_t *scope = fresh_scope(env);
            CORD key, value;
            if (for_->index) {
                key = compile(env, for_->index);
                value = compile(env, for_->value);
                set_binding(scope, CORD_to_const_char_star(key), new(binding_t, .type=key_t));
                set_binding(scope, CORD_to_const_char_star(value), new(binding_t, .type=value_t));

                size_t value_offset = type_size(key_t);
                if (type_align(value_t) > 1 && value_offset % type_align(value_t))
                    value_offset += type_align(value_t) - (value_offset % type_align(value_t)); // padding
                return CORD_all("$TABLE_FOREACH(", compile(env, for_->iter), ", ", compile_type(key_t), ", ", key, ", ",
                                compile_type(value_t), ", ", value, ", ", heap_strf("%zu", value_offset),
                                ", ", compile(scope, for_->body), ", ", for_->empty ? compile(env, for_->empty) : "{}", ")");
            } else {
                key = compile(env, for_->value);
                set_binding(scope, CORD_to_const_char_star(key), new(binding_t, .type=key_t));
                return CORD_all("$ARRAY_FOREACH((", compile(env, for_->iter), ").entries, $i, ", compile_type(key_t), ", ", key, ", ",
                                compile(scope, for_->body), ", ", for_->empty ? compile(env, for_->empty) : "{}", ")");
            }
        }
        case IntType: {
            type_t *item_t = iter_t;
            env_t *scope = fresh_scope(env);
            CORD value = compile(env, for_->value);
            set_binding(scope, CORD_to_const_char_star(value), new(binding_t, .type=item_t, .code=value));

            CORD n = compile(env, for_->iter);
            CORD index = CORD_EMPTY;
            if (for_->index) {
                index = compile(env, for_->index);
                set_binding(scope, CORD_to_const_char_star(index), new(binding_t, .type=Type(IntType, .bits=64), .code=index));
            }

            if (for_->empty && index) {
                return CORD_all(
                    "{\n"
                    "int64_t $n = ", n, ";\n"
                    "if ($n > 0) {\n"
                    "for (int64_t ", index, " = 1, ", value, "; (", value, "=", index,") <= $n; ++", index, ")\n"
                    "\t", compile(scope, for_->body), "\n"
                    "}\n else ", compile(env, for_->empty),
                    "\n}");
            } else if (for_->empty) {
                return CORD_all(
                    "{\n"
                    "int64_t $n = ", n, ";\n"
                    "if ($n > 0) {\n"
                    "for (int64_t ", value, " = 1; ", value, " <= $n; ++", value, ")\n"
                    "\t", compile(scope, for_->body), "\n"
                    "}\n else ", compile(env, for_->empty),
                    "\n}");
            } else if (index) {
                return CORD_all(
                    "for (int64_t ", value, ", ", index, " = 1, $n = ", n, "; (", value, "=", index,") <= $n; ++", value, ")\n"
                    "\t", compile(scope, for_->body), "\n");
            } else {
                return CORD_all(
                    "for (int64_t ", value, " = 1, $n = ", compile(env, for_->iter), "; ", value, " <= $n; ++", value, ")\n"
                    "\t", compile(scope, for_->body), "\n");
            }
        }
        default: code_err(for_->iter, "Iteration is not implemented for type: %T", iter_t);
        }
    }
    case Reduction: {
        auto reduction = Match(ast, Reduction);
        type_t *t = get_type(env, ast);
        CORD code = CORD_all(
            "({ // Reduction:\n",
            compile_declaration(t, "$reduction"), ";\n"
            );
        env_t *scope = fresh_scope(env);
        ast_t *result = FakeAST(Var, "$reduction");
        set_binding(scope, "$reduction", new(binding_t, .type=t));
        ast_t *empty = NULL;
        if (reduction->fallback) {
            type_t *fallback_type = get_type(scope, reduction->fallback);
            if (fallback_type->tag == AbortType) {
                empty = reduction->fallback;
            } else {
                empty = FakeAST(Assign, .targets=new(ast_list_t, .ast=result), .values=new(ast_list_t, .ast=reduction->fallback));
            }
        } else {
            empty = FakeAST(
                InlineCCode, 
                CORD_asprintf("fail_source(%s, %ld, %ld, \"This collection was empty!\");\n",
                              Text__quoted(ast->file->filename, false), (long)(reduction->iter->start - reduction->iter->file->text),
                              (long)(reduction->iter->end - reduction->iter->file->text)));
        }
        ast_t *i = FakeAST(Var, "$i");
        ast_t *item = FakeAST(Var, "$iter_value");
        set_binding(scope, "$iter_value", new(binding_t, .type=t));
        ast_t *body = FakeAST(InlineCCode, CORD_all("$reduction = $i == 1 ? $iter_value : ", compile(scope, reduction->combination), ";"));
        ast_t *loop = FakeAST(For, .index=i, .value=item, .iter=reduction->iter, .body=body, .empty=empty);
        code = CORD_all(code, compile(scope, loop), "\n$reduction;})");
        return code;
    }
    case Skip: {
        if (Match(ast, Skip)->target) code_err(ast, "Named skips not yet implemented");
        return "continue";
    }
    case Stop: {
        if (Match(ast, Stop)->target) code_err(ast, "Named stops not yet implemented");
        return "break";
    }
    case Pass: return ";";
    case Return: {
        if (!env->fn_ctx) code_err(ast, "This return statement is not inside any function");
        auto ret = Match(ast, Return)->value;
        assert(env->fn_ctx->return_type);
        if (ret) {
            type_t *ret_t = get_type(env, ret);
            CORD value = compile(env, ret);
            if (!promote(env, &value, ret_t, env->fn_ctx->return_type))
                code_err(ast, "This function expects a return value of type %T, but this return has type %T", 
                         env->fn_ctx->return_type, ret_t);
            return CORD_all("return ", value, ";");
        } else {
            if (env->fn_ctx->return_type->tag != VoidType)
                code_err(ast, "This function expects a return value of type %T", env->fn_ctx->return_type->tag);
            return "return;";
        }
    }
    case StructDef: {
        compile_struct_def(env, ast);
        return CORD_EMPTY;
    }
    case EnumDef: {
        compile_enum_def(env, ast);
        return CORD_EMPTY;
    }
    case LangDef: {
        // TODO: implement
        auto def = Match(ast, LangDef);
        CORD_appendf(&env->code->typedefs, "typedef CORD %s_t;\n", def->name);
        CORD_appendf(&env->code->typedefs, "extern const TypeInfo %s;\n", def->name);
        CORD_appendf(&env->code->typeinfos, "public const TypeInfo %s = {%zu, %zu, {.tag=TextInfo, .TextInfo={%s}}};\n",
                     def->name, sizeof(CORD), __alignof__(CORD),
                     Text__quoted(def->name, false));
        compile_namespace(env, def->name, def->namespace);
        return CORD_EMPTY;
    }
    case DocTest: {
        auto test = Match(ast, DocTest);
        CORD src = heap_strn(test->expr->start, (size_t)(test->expr->end - test->expr->start));
        type_t *expr_t = get_type(env, test->expr);
        if (!expr_t)
            code_err(test->expr, "I couldn't figure out the type of this expression");

        CORD output = NULL;
        if (test->output) {
            const uint8_t *raw = (const uint8_t*)CORD_to_const_char_star(test->output);
            uint8_t buf[128] = {0};
            size_t norm_len = sizeof(buf);
            uint8_t *norm = u8_normalize(UNINORM_NFD, (uint8_t*)raw, strlen((char*)raw)+1, buf, &norm_len);
            assert(norm[norm_len-1] == 0);
            output = CORD_from_char_star((char*)norm);
            if (norm && norm != buf) free(norm);
        }

        if (test->expr->tag == Declare) {
            auto decl = Match(test->expr, Declare);
            return CORD_asprintf(
                "%r\n"
                "__doctest(&%r, %r, %r, %r, %ld, %ld);",
                compile(env, test->expr),
                compile(env, decl->var),
                compile_type_info(env, get_type(env, decl->value)),
                compile(env, WrapAST(test->expr, TextLiteral, .cord=output)),
                compile(env, WrapAST(test->expr, TextLiteral, .cord=test->expr->file->filename)),
                (int64_t)(test->expr->start - test->expr->file->text),
                (int64_t)(test->expr->end - test->expr->file->text));
        } else if (test->expr->tag == Assign) {
            auto assign = Match(test->expr, Assign);
            CORD code = "{ // Assignment\n";
            int64_t i = 1;
            for (ast_list_t *value = assign->values; value; value = value->next)
                CORD_appendf(&code, "%r $%ld = %r;\n", compile_type(get_type(env, value->ast)), i++, compile(env, value->ast));
            i = 1;
            for (ast_list_t *target = assign->targets; target; target = target->next) {
                check_assignable(env, target->ast);
                CORD_appendf(&code, "%r = $%ld;\n", compile(env, target->ast), i++);
            }

            CORD expr_cord = "CORD_all(";
            i = 1;
            for (ast_list_t *target = assign->targets; target; target = target->next) {
                CORD item = expr_as_text(env, CORD_asprintf("$%ld", i++), get_type(env, target->ast), "USE_COLOR");
                expr_cord = CORD_all(expr_cord, item, target->next ? ", \", \", " : CORD_EMPTY);
            }
            expr_cord = CORD_cat(expr_cord, ")");

            CORD_appendf(&code, "$test(%r, %r, %r);",
                compile(env, WrapAST(test->expr, TextLiteral, .cord=src)),
                expr_cord,
                compile(env, WrapAST(test->expr, TextLiteral, .cord=output)));
            return CORD_cat(code, "\n}");
        } else if (expr_t->tag == VoidType || expr_t->tag == AbortType) {
            return CORD_asprintf(
                "%r;\n"
                "__doctest(NULL, NULL, NULL, %r, %ld, %ld);",
                compile(env, test->expr),
                compile(env, WrapAST(test->expr, TextLiteral, .cord=test->expr->file->filename)),
                (int64_t)(test->expr->start - test->expr->file->text),
                (int64_t)(test->expr->end - test->expr->file->text));
        } else {
            return CORD_asprintf(
                "{ // Test:\n%r $expr = %r;\n"
                "__doctest(&$expr, %r, %r, %r, %ld, %ld);\n"
                "}",
                compile_type(expr_t),
                compile(env, test->expr),
                compile_type_info(env, expr_t),
                compile(env, WrapAST(test->expr, TextLiteral, .cord=output)),
                compile(env, WrapAST(test->expr, TextLiteral, .cord=test->expr->file->filename)),
                (int64_t)(test->expr->start - test->expr->file->text),
                (int64_t)(test->expr->end - test->expr->file->text));
        }
    }
    case FieldAccess: {
        auto f = Match(ast, FieldAccess);
        type_t *fielded_t = get_type(env, f->fielded);
        type_t *value_t = value_type(fielded_t);
        switch (value_t->tag) {
        case TypeInfoType: {
            auto info = Match(value_t, TypeInfoType);
            table_t *namespace = Table_str_get(*env->type_namespaces, info->name);
            if (!namespace) code_err(f->fielded, "I couldn't find a namespace for this type");
            binding_t *b = Table_str_get(*namespace, f->field);
            if (!b) code_err(ast, "I couldn't find the field '%s' on this type", f->field);
            if (!b->code) code_err(ast, "I couldn't figure out how to compile this field");
            return b->code;
        }
        case StructType: {
            for (arg_t *field = Match(value_t, StructType)->fields; field; field = field->next) {
                if (streq(field->name, f->field)) {
                    if (fielded_t->tag == PointerType) {
                        CORD fielded = compile_to_pointer_depth(env, f->fielded, 1, false);
                        return CORD_asprintf("(%r)->%s", fielded, f->field);
                    } else {
                        CORD fielded = compile(env, f->fielded);
                        return CORD_asprintf("(%r).%s", fielded, f->field);
                    }
                }
            }
            code_err(ast, "The field '%s' is not a valid field name of %T", f->field, value_t);
        }
        case EnumType: {
            auto enum_ = Match(value_t, EnumType);
            for (tag_t *tag = enum_->tags; tag; tag = tag->next) {
                if (streq(tag->name, f->field)) {
                    CORD fielded = compile_to_pointer_depth(env, f->fielded, 0, false);
                    return CORD_asprintf("$tagged(%r, %s, %s)", fielded, enum_->name, f->field);
                }
            }
            code_err(ast, "The field '%s' is not a valid field name of %T", f->field, value_t);
        }
        case TableType: {
            if (streq(f->field, "keys")) {
                return CORD_all("({ table_t *$t = ", compile_to_pointer_depth(env, f->fielded, 1, false), ";\n"
                                "$t->entries.data_refcount = 3;\n"
                                "$t->entries; })");
            } else if (streq(f->field, "values")) {
                auto table = Match(value_t, TableType);
                size_t offset = type_size(table->key_type);
                size_t align = type_align(table->value_type);
                if (align > 1 && offset % align > 0)
                    offset += align - (offset % align);
                return CORD_all("({ table_t *$t = ", compile_to_pointer_depth(env, f->fielded, 1, false), ";\n"
                                "$t->entries.data_refcount = 3;\n"
                                "(array_t){.data = $t->entries.data + ", CORD_asprintf("%zu", offset),
                                ",\n .length=$t->entries.length,\n .stride=$t->entries.stride,\n .data_refcount=3};})");
            } else if (streq(f->field, "fallback")) {
                return CORD_all("(", compile_to_pointer_depth(env, f->fielded, 0, false), ").fallback");
            } else if (streq(f->field, "default")) {
                return CORD_all("(", compile_to_pointer_depth(env, f->fielded, 0, false), ").default_value");
            }
            code_err(ast, "There is no '%s' field on tables", f->field);
        }
        default:
            code_err(ast, "Field accesses are only supported on struct and enum values");
        }
    }
    case Index: {
        auto indexing = Match(ast, Index);
        type_t *indexed_type = get_type(env, indexing->indexed);
        if (!indexing->index && indexed_type->tag == PointerType) {
            auto ptr = Match(indexed_type, PointerType);
            if (ptr->is_optional)
                code_err(ast, "This pointer is potentially null, so it can't be safely dereferenced");
            if (ptr->pointed->tag == ArrayType) {
                return CORD_all("({ array_t *$arr = ", compile(env, indexing->indexed), "; $arr->data_refcount = 3; *$arr; })");
            } else if (ptr->pointed->tag == TableType) {
                return CORD_all("({ table_t *$t = ", compile(env, indexing->indexed), "; Table_mark_copy_on_write($t); *$t; })");
            } else {
                return CORD_all("*(", compile(env, indexing->indexed), ")");
            }
        }
        type_t *container_t = value_type(indexed_type);
        type_t *index_t = get_type(env, indexing->index);
        switch (container_t->tag) {
        case ArrayType: {
            if (index_t->tag != IntType)
                code_err(indexing->index, "Arrays can only be indexed by integers, not %T", index_t);
            type_t *item_type = Match(container_t, ArrayType)->item_type;
            CORD arr = compile_to_pointer_depth(env, indexing->indexed, 0, false);
            CORD index = compile(env, indexing->index);
            file_t *f = indexing->index->file;
            if (indexing->unchecked)
                return CORD_all("$Array_get_unchecked", compile_type(item_type), ", ", arr, ", ", index, ")");
            else
                return CORD_all("$Array_get(", compile_type(item_type), ", ", arr, ", ", index, ", ",
                                Text__quoted(f->filename, false), ", ", CORD_asprintf("%ld", (int64_t)(indexing->index->start - f->text)), ", ",
                                CORD_asprintf("%ld", (int64_t)(indexing->index->end - f->text)),
                                ")");
        }
        case TableType: {
            type_t *key_t = Match(container_t, TableType)->key_type;
            type_t *value_t = Match(container_t, TableType)->value_type;
            CORD table = compile_to_pointer_depth(env, indexing->indexed, 0, false);
            CORD key = compile(env, indexing->index);
            if (!promote(env, &key, index_t, key_t))
                code_err(indexing->index, "This value has type %T, but this table can only be index with keys of type %T", index_t, key_t);
            file_t *f = indexing->index->file;
            return CORD_all("$Table_get(", table, ", ", compile_type(key_t), ", ", compile_type(value_t), ", ",
                            key, ", ", compile_type_info(env, container_t), ", ",
                            Text__quoted(f->filename, false), ", ", CORD_asprintf("%ld", (int64_t)(indexing->index->start - f->text)), ", ",
                            CORD_asprintf("%ld", (int64_t)(indexing->index->end - f->text)),
                            ")");
        }
        default: code_err(ast, "Indexing is not supported for type: %T", container_t);
        }
    }
    case InlineCCode: return Match(ast, InlineCCode)->code;
    case Use: code_err(ast, "Uses are not supported yet");
    case LinkerDirective: code_err(ast, "Linker directives are not supported yet");
    case Extern: code_err(ast, "Externs are not supported yet");
    case TableEntry: code_err(ast, "Table entries should not be compiled directly");
    case Unknown: code_err(ast, "Unknown AST");
    }
    code_err(ast, "Unknown AST: %W", ast);
    return NULL;
}

void compile_namespace(env_t *env, const char *ns_name, ast_t *block)
{
    env_t *ns_env = namespace_env(env, ns_name);
    for (ast_list_t *stmt = block ? Match(block, Block)->statements : NULL; stmt; stmt = stmt->next) {
        ast_t *ast = stmt->ast;
        switch (ast->tag) {
        case FunctionDef:
            CORD code = compile_statement(ns_env, ast);
            env->code->funcs = CORD_cat(env->code->funcs, code);
            break;
        case Declare: {
            auto decl = Match(ast, Declare);
            type_t *t = get_type(ns_env, decl->value);

            CORD var_decl = CORD_all(compile_type(t), " ", compile(ns_env, decl->var), ";\n");
            env->code->staticdefs = CORD_cat(env->code->staticdefs, var_decl);

            CORD init = CORD_all(compile(ns_env, decl->var), " = ", compile(ns_env, decl->value), ";\n");
            env->code->main = CORD_cat(env->code->main, init);

            env->code->fndefs = CORD_all(env->code->fndefs, "extern ", compile_type(t), " ", compile(ns_env, decl->var), ";\n");
            break;
        }
        default: {
            CORD code = compile_statement(ns_env, ast);
            env->code->main = CORD_cat(env->code->main, code);
            break;
        }
    }
    }
}

CORD compile_type_info(env_t *env, type_t *t)
{
    switch (t->tag) {
    case BoolType: case IntType: case NumType: return CORD_asprintf("&%r", type_to_cord(t));
    case TextType: return CORD_all("(&", Match(t, TextType)->lang ? Match(t, TextType)->lang : "Text", ")");
    case StructType: return CORD_all("(&", Match(t, StructType)->name, ")");
    case EnumType: return CORD_all("(&", Match(t, EnumType)->name, ")");
    case ArrayType: {
        type_t *item_t = Match(t, ArrayType)->item_type;
        return CORD_asprintf("$ArrayInfo(%r)", compile_type_info(env, item_t));
    }
    case TableType: {
        type_t *key_type = Match(t, TableType)->key_type;
        type_t *value_type = Match(t, TableType)->value_type;
        return CORD_asprintf("$TableInfo(%r, %r)", compile_type_info(env, key_type), compile_type_info(env, value_type));
    }
    case PointerType: {
        auto ptr = Match(t, PointerType);
        CORD sigil = ptr->is_stack ? "&" : (ptr->is_optional ? "?" : "@");
        if (ptr->is_readonly) sigil = CORD_cat(sigil, "(readonly)");
        return CORD_asprintf("$PointerInfo(%r, %r)", Text__quoted(sigil, false), compile_type_info(env, ptr->pointed));
    }
    case FunctionType: {
        return CORD_asprintf("$FunctionInfo(%r)", Text__quoted(type_to_cord(t), false));
    }
    case ClosureType: {
        return CORD_asprintf("$ClosureInfo(%r)", Text__quoted(type_to_cord(t), false));
    }
    case TypeInfoType: return "&TypeInfo_info";
    default:
        compiler_err(NULL, 0, 0, "I couldn't convert to a type info: %T", t);
    }
}

module_code_t compile_file(ast_t *ast)
{
    env_t *env = new_compilation_unit();
    CORD_appendf(&env->code->imports, "#include <tomo.h>\n");

    for (ast_list_t *stmt = Match(ast, Block)->statements; stmt; stmt = stmt->next) {
        bind_statement(env, stmt->ast);
        CORD code = compile_statement(env, stmt->ast);
        if (code)
            CORD_appendf(&env->code->main, "%r\n", code);
    }

    const char *slash = strrchr(ast->file->filename, '/');
    const char *name = slash ? slash+1 : ast->file->filename;
    size_t name_len = 0;
    while (name[name_len] && (isalnum(name[name_len]) || name[name_len] == '_'))
        ++name_len;
    if (name_len == 0)
        errx(1, "No module name found for: %s", ast->file->filename);
    const char *module_name = heap_strn(name, name_len);

    return (module_code_t){
        .module_name=module_name,
        .header=CORD_all(
            // CORD_asprintf("#line 0 %r\n", Text__quoted(ast->file->filename, false)),
            env->code->imports, "\n",
            env->code->typedefs, "\n",
            env->code->typecode, "\n",
            env->code->fndefs, "\n",
            "public void use(void);\n"
        ),
        .c_file=CORD_all(
            // CORD_asprintf("#line 0 %r\n", Text__quoted(ast->file->filename, false)),
            env->code->staticdefs, "\n",
            env->code->funcs, "\n",
            env->code->typeinfos, "\n",
            "\n"
            "public void use(void) {\n",
            env->code->main,
            "}\n"
        ),
   };
}

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
