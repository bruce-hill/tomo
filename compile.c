
#include <ctype.h>
#include <gc/cord.h>
#include <gc.h>
#include <stdio.h>

#include "ast.h"
#include "builtins/string.h"
#include "compile.h"
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

CORD compile_type(type_t *t)
{
    switch (t->tag) {
    case AbortType: return "void";
    case VoidType: return "void";
    case MemoryType: return "void";
    case BoolType: return "Bool_t";
    case IntType: return CORD_asprintf("Int%ld_t", Match(t, IntType)->bits);
    case NumType: return CORD_asprintf("Num%ld_t", Match(t, NumType)->bits);
    case StringType: {
        const char *dsl = Match(t, StringType)->dsl;
        return dsl ? CORD_cat(dsl, "_t") : "Str_t";
    }
    case ArrayType: return "array_t";
    case TableType: return "table_t";
    case FunctionType: return "const void*";
    case ClosureType: compiler_err(NULL, NULL, NULL, "Not implemented");
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
    case If: case When: case For: case While: case FunctionDef: case Return: case StructDef: case EnumDef:
    case Declare: case Assign: case UpdateAssign: case DocTest:
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

CORD expr_as_string(env_t *env, CORD expr, type_t *t, CORD color)
{
    switch (t->tag) {
    case MemoryType: return CORD_asprintf("Memory__as_str($stack(%r), %r, NULL)", expr, color);
    case BoolType: return CORD_asprintf("Bool__as_str($stack(%r), %r, NULL)", expr, color);
    case IntType: return CORD_asprintf("Int%ld__as_str($stack(%r), %r, NULL)", Match(t, IntType)->bits, expr, color);
    case NumType: return CORD_asprintf("Num%ld__as_str($stack(%r), %r, NULL)", Match(t, NumType)->bits, expr, color);
    case StringType: return CORD_asprintf("Str__as_str($stack(%r), %r, &Str.type)", expr, color);
    case ArrayType: return CORD_asprintf("Array__as_str($stack(%r), %r, %r)", expr, color, compile_type_info(env, t));
    case TableType: return CORD_asprintf("Table_as_str($stack(%r), %r, %r)", expr, color, compile_type_info(env, t));
    case FunctionType: return CORD_asprintf("Func__as_str($stack(%r), %r, %r)", expr, color, compile_type_info(env, t));
    case PointerType: return CORD_asprintf("Pointer__as_str($stack(%r), %r, %r)", expr, color, compile_type_info(env, t));
    case StructType: case EnumType: return CORD_asprintf("(%r)->CustomInfo.as_str($stack(%r), %r, %r)",
                                                         compile_type_info(env, t), expr, color, compile_type_info(env, t));
    default: compiler_err(NULL, NULL, NULL, "Stringifying is not supported for %T", t);
    }
}

CORD compile_string(env_t *env, ast_t *ast, CORD color)
{
    type_t *t = get_type(env, ast);
    CORD expr = compile(env, ast);
    return expr_as_string(env, expr, t, color);
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
                val = CORD_all("&", val);
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

CORD compile(env_t *env, ast_t *ast)
{
    switch (ast->tag) {
    case Nil: return CORD_asprintf("$Null(%r)", compile_type_ast(Match(ast, Nil)->type));
    case Bool: return Match(ast, Bool)->b ? "yes" : "no";
    case Var: return Match(ast, Var)->name;
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
        case StringType: {
            CORD str = compile_to_pointer_depth(env, expr, 0, false);
            return CORD_all("CORD_len(", str, ")");
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
    case StackReference: return CORD_asprintf("$stack(%r)", compile(env, Match(ast, StackReference)->value));
    case BinaryOp: {
        auto binop = Match(ast, BinaryOp);
        CORD lhs = compile(env, binop->lhs);
        CORD rhs = compile(env, binop->rhs);

        type_t *lhs_t = get_type(env, binop->lhs);
        type_t *rhs_t = get_type(env, binop->rhs);
        type_t *operand_t;
        if (can_promote(rhs_t, lhs_t))
            operand_t = lhs_t;
        else if (can_promote(lhs_t, rhs_t))
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
            case StringType: {
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
        CORD lhs = compile(env, update->lhs);
        CORD rhs = compile(env, update->rhs);

        type_t *lhs_t = get_type(env, update->lhs);
        type_t *rhs_t = get_type(env, update->rhs);
        type_t *operand_t;
        if (can_promote(rhs_t, lhs_t))
            operand_t = lhs_t;
        else if (can_promote(lhs_t, rhs_t))
            operand_t = rhs_t;
        else if (lhs_t->tag == ArrayType && can_promote(rhs_t, Match(lhs_t, ArrayType)->item_type))
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
            if (operand_t->tag == StringType) {
                return CORD_asprintf("%r = CORD_cat(%r, %r);", lhs, lhs, rhs);
            } else if (operand_t->tag == ArrayType) {
                if (can_promote(rhs_t, Match(lhs_t, ArrayType)->item_type)) {
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
    case StringLiteral: {
        CORD literal = Match(ast, StringLiteral)->cord; 
        if (literal == CORD_EMPTY)
            return "(CORD)CORD_EMPTY";
        CORD code = "\"";
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
                    CORD_sprintf(&code, "%r\\x%02X", c);
                break;
            }
            }
        }
        return CORD_cat_char(code, '"');
    }
    case StringJoin: {
        ast_list_t *chunks = Match(ast, StringJoin)->children;
        if (!chunks) {
            return "(CORD)CORD_EMPTY";
        } else if (!chunks->next) {
            type_t *t = get_type(env, chunks->ast);
            if (t->tag == StringType)
                return compile(env, chunks->ast);
            return compile_string(env, chunks->ast, "no");
        } else {
            CORD code = "CORD_all(";
            for (ast_list_t *chunk = chunks; chunk; chunk = chunk->next) {
                type_t *chunk_t = get_type(env, chunk->ast);
                CORD chunk_str = (chunk_t->tag == StringType) ?
                    compile(env, chunk->ast) : compile_string(env, chunk->ast, "no");
                code = CORD_cat(code, chunk_str);
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
            code = CORD_cat(code, compile_statement(env, stmt->ast));
            code = CORD_cat(code, "\n");
            bind_statement(env, stmt->ast);
        }
        return CORD_cat(code, "}");
    }
    case Declare: {
        auto decl = Match(ast, Declare);
        type_t *t = get_type(env, decl->value);
        // return CORD_asprintf("auto %r = %r;", compile(env, decl->var), compile(env, decl->value));
        return CORD_asprintf("%r %r = %r;", compile_type(t), compile(env, decl->var), compile(env, decl->value));
    }
    case Assign: {
        auto assign = Match(ast, Assign);
        // Single assignment:
        if (assign->targets && !assign->targets->next)
            return CORD_asprintf("%r = %r", compile(env, assign->targets->ast), compile(env, assign->values->ast));

        CORD code = "{ // Assignment\n";
        int64_t i = 1;
        for (ast_list_t *value = assign->values; value; value = value->next)
            CORD_appendf(&code, "%r $%ld = %r;\n", compile_type(get_type(env, value->ast)), i++, compile(env, value->ast));
        i = 1;
        for (ast_list_t *target = assign->targets; target; target = target->next)
            CORD_appendf(&code, "%r = $%ld;\n", compile(env, target->ast), i++);
        return CORD_cat(code, "\n}");
    }
    case Min: {
        return CORD_asprintf("min(%r, %r)", compile(env, Match(ast, Min)->lhs), compile(env, Match(ast, Min)->rhs));
    }
    case Max: {
        return CORD_asprintf("max(%r, %r)", compile(env, Match(ast, Max)->lhs), compile(env, Match(ast, Max)->rhs));
    }
    // Min, Max,
    case Array: {
        auto array = Match(ast, Array);
        if (!array->items)
            return "(array_t){}";
           
        CORD code = "$Array(";
        for (ast_list_t *item = array->items; item; item = item->next) {
            code = CORD_cat(code, compile(env, item->ast));
            if (item->next) code = CORD_cat(code, ", ");
        }
        return CORD_cat(code, ")");
    }
    case Table: {
        auto table = Match(ast, Table);
        if (!table->entries) {
            CORD code = "(table_t){";
            if (table->fallback)
                code = CORD_all(code, ".fallback=", compile(env, table->fallback),",");
            if (table->default_value)
                code = CORD_all(code, ".default_value=", compile(env, table->default_value),",");
            return CORD_cat(code, "}");
        }
           
        type_t *table_t = get_type(env, ast);
        type_t *key_t = Match(table_t, TableType)->key_type;
        type_t *value_t = Match(table_t, TableType)->value_type;
        CORD code = CORD_all("$Table(",
                             compile_type(key_t), ", ",
                             compile_type(value_t), ", ",
                             compile_type_info(env, key_t), ", ",
                             compile_type_info(env, value_t), ", /*fallback:*/");
        if (table->fallback)
            code = CORD_all(code, "$heap(", compile(env, table->fallback), ");\n");
        else
            code = CORD_all(code, "NULL, ");

        code = CORD_cat(code, "/*default:*/");
        if (table->default_value)
            code = CORD_all(code, "$heap(", compile(env, table->default_value), ");\n");
        else
            code = CORD_all(code, "NULL");

        for (ast_list_t *entry = table->entries; entry; entry = entry->next) {
            auto e = Match(entry->ast, TableEntry);
            code = CORD_all(code, ",\n\t{", compile(env, e->key), ", ", compile(env, e->value), "}");
        }
        return CORD_cat(code, ")");

    }
    // Table, TableEntry,
    case FunctionDef: {
        auto fndef = Match(ast, FunctionDef);
        CORD name = compile(env, fndef->name);
        CORD_appendf(&env->code->staticdefs, "static %r %r_(", fndef->ret_type ? compile_type_ast(fndef->ret_type) : "void", name);
        for (arg_ast_t *arg = fndef->args; arg; arg = arg->next) {
            type_t *arg_type = arg->type ? parse_type_ast(env, arg->type) : get_type(env, arg->value);
            CORD_appendf(&env->code->staticdefs, "%r %s", compile_type(arg_type), arg->name);
            if (arg->next) env->code->staticdefs = CORD_cat(env->code->staticdefs, ", ");
        }
        env->code->staticdefs = CORD_cat(env->code->staticdefs, ");\n");

        CORD kwargs = CORD_asprintf("#define %r(...) ({ struct {", name);
        CORD passed_args = CORD_EMPTY;
        CORD_appendf(&env->code->funcs, "%r %r_(", fndef->ret_type ? compile_type_ast(fndef->ret_type) : "void", name);
        env_t *body_scope = fresh_scope(env);
        body_scope->locals->fallback = env->globals;
        for (arg_ast_t *arg = fndef->args; arg; arg = arg->next) {
            type_t *arg_type = arg->type ? parse_type_ast(env, arg->type) : get_type(env, arg->value);
            CORD arg_typecode = compile_type(arg_type);
            CORD_appendf(&env->code->funcs, "%r %s", arg_typecode, arg->name);
            if (arg->next) env->code->funcs = CORD_cat(env->code->funcs, ", ");
            CORD_appendf(&kwargs, "%r %s; ", arg_typecode, arg->name);
            CORD_appendf(&passed_args, "$args.%s", arg->name);
            if (arg->next) passed_args = CORD_cat(passed_args, ", ");
            set_binding(body_scope, arg->name, new(binding_t, .type=arg_type));
        }
        CORD_appendf(&kwargs, "} $args = {__VA_ARGS__}; %r_(%r); })\n", name, passed_args);
        CORD_appendf(&env->code->staticdefs, "%r", kwargs);

        CORD body = compile(body_scope, fndef->body);
        if (CORD_fetch(body, 0) != '{')
            body = CORD_asprintf("{\n%r\n}", body);
        CORD_appendf(&env->code->funcs, ") %r", body);
        return CORD_EMPTY;
    }
    case FunctionCall: {
        auto call = Match(ast, FunctionCall);
        type_t *fn_t = get_type(env, call->fn);
        if (fn_t->tag != FunctionType)
            code_err(call->fn, "This is not a function, it's a %T", fn_t);

        CORD code = CORD_cat_char(compile(env, call->fn), '(');
        // Pass 1: assign keyword args
        // Pass 2: assign positional args
        // Pass 3: compile and typecheck each arg
        table_t arg_bindings = {};
        for (arg_ast_t *arg = call->args; arg; arg = arg->next) {
            if (arg->name)
                Table_str_set(&arg_bindings, arg->name, arg->value);
        }
        for (arg_ast_t *call_arg = call->args; call_arg; call_arg = call_arg->next) {
            if (call_arg->name)
                continue;

            const char *name = NULL;
            for (arg_t *fn_arg = Match(fn_t, FunctionType)->args; fn_arg; fn_arg = fn_arg->next) {
                if (!Table_str_get(&arg_bindings, fn_arg->name)) {
                    name = fn_arg->name;
                    break;
                }
            }
            if (name)
                Table_str_set(&arg_bindings, name, call_arg->value);
            else
                code_err(call_arg->value, "This is too many arguments to the function: %T", fn_t);
        }

        // TODO: ensure args get executed in order (e.g. `foo(y=get_next(1), x=get_next(2))`
        // should not execute out of order)
        for (arg_t *fn_arg = Match(fn_t, FunctionType)->args; fn_arg; fn_arg = fn_arg->next) {
            ast_t *arg = Table_str_get(&arg_bindings, fn_arg->name);
            if (arg) {
                Table_str_remove(&arg_bindings, fn_arg->name);
            } else {
                arg = fn_arg->default_val;
            }
            if (!arg)
                code_err(ast, "The required argument '%s' is not provided", fn_arg->name);

            code = CORD_cat(code, compile(env, arg));
            if (fn_arg->next) code = CORD_cat(code, ", ");
        }

        struct {
            const char *name;
            ast_t *ast;
        } *invalid = Table_str_entry(&arg_bindings, 1);
        if (invalid)
            code_err(invalid->ast, "There is no argument named %s for %T", invalid->name, fn_t);

        return CORD_cat_char(code, ')');
    }
    // Lambda,
    case If: {
        auto if_ = Match(ast, If);
        CORD code;
        CORD_sprintf(&code, "if (%r) %r", compile(env, if_->condition), compile(env, if_->body));
        if (if_->else_body)
            CORD_sprintf(&code, "%r\nelse %r", code, compile(env, if_->else_body));
        return code;
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
            return CORD_all(
                "{ // For loop:\n"
                "array_t $iter = ", compile(env, for_->iter), ";\n"
                "for (Int64_t ", index, " = 1; ", index, " <= $iter.length; ++", index, ") {\n"
                "\t", compile_type(item_t), " ", value, " = *(", compile_type(item_t), "*)($iter.data + (", index, "-1)*$iter.stride);\n"
                "\t", compile(scope, for_->body), "\n"
                "}\n"
                "}\n");
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
                return CORD_all(
                    "{ // For loop:\n"
                    "array_t $entries = (", compile(env, for_->iter), ").entries;\n"
                    "for (Int64_t $offset = 0; $offset < $entries.length; ++$offset) {\n"
                    "\t", compile_type(key_t), " ", key, " = *(", compile_type(key_t), "*)($entries.data + $offset*$entries.stride);\n"
                    "\t", compile_type(value_t), " ", value, " = *(", compile_type(value_t), "*)($entries.data + $offset*$entries.stride + ", CORD_asprintf("%zu", value_offset), ");\n"
                    "\t", compile(scope, for_->body), "\n"
                    "}\n"
                    "}\n");
            } else {
                key = compile(env, for_->value);
                set_binding(scope, CORD_to_const_char_star(key), new(binding_t, .type=key_t));
                return CORD_all(
                    "{ // For loop:\n"
                    "array_t $entries = (", compile(env, for_->iter), ").entries;\n"
                    "for (Int64_t $offset = 0; $offset < $entries.length; ++$offset) {\n"
                    "\t", compile_type(key_t), " ", key, " = *(", compile_type(key_t), "*)($entries.data + $offset*$entries.stride);\n"
                    "\t", compile(scope, for_->body), "\n"
                    "}\n"
                    "}\n");
            }
        }
        case IntType: {
            type_t *item_t = iter_t;
            env_t *scope = fresh_scope(env);
            if (for_->index)
                code_err(for_->index, "It's redundant to have a separate iteration index");
            CORD value = compile(env, for_->value);
            set_binding(scope, CORD_to_const_char_star(value), new(binding_t, .type=item_t));
            return CORD_all(
                "for (Int64_t ", value, " = 1, $n = ", compile(env, for_->iter), "; ", value, " <= $n; ++", value, ")\n"
                "\t", compile(scope, for_->body), "\n");
        }
        default: code_err(for_->iter, "Iteration is not implemented for type: %T", iter_t);
        }
    }
    // For,
    // Reduction,
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
        auto ret = Match(ast, Return)->value;
        return ret ? CORD_asprintf("return %r;", compile(env, ret)) : "return;";
    }
    // Extern,
    case StructDef: {
        compile_struct_def(env, ast);
        return CORD_EMPTY;
    }
    case EnumDef: {
        auto def = Match(ast, EnumDef);
        CORD_appendf(&env->code->typedefs, "typedef struct %s_s %s_t;\n", def->name, def->name);
        CORD enum_def = CORD_all("struct ", def->name, "_s {\n"
                                 "\tenum {");
        for (tag_ast_t *tag = def->tags; tag; tag = tag->next) {
            CORD_appendf(&enum_def, "$tag$%s$%s = %ld", def->name, tag->name, tag->value);
            if (tag->next) enum_def = CORD_cat(enum_def, ", ");
        }
        enum_def = CORD_cat(enum_def, "} $tag;\n"
                            "union {\n");
        for (tag_ast_t *tag = def->tags; tag; tag = tag->next) {
            (void)compile(env, WrapAST(ast, StructDef, .name=heap_strf("%s$%s", def->name, tag->name), .fields=tag->fields));
            enum_def = CORD_all(enum_def, def->name, "$", tag->name, "_t ", tag->name, ";\n");
            CORD_appendf(&env->code->typedefs, "#define %s__%s(...) ((%s_t){$tag$%s$%s, .%s={__VA_ARGS__}})\n",
                         def->name, tag->name, def->name, def->name, tag->name, tag->name);
        }
        enum_def = CORD_cat(enum_def, "};\n};\n");
        env->code->typecode = CORD_cat(env->code->typecode, enum_def);

        CORD cord_func = CORD_all("static CORD ", def->name, "__as_str(", def->name, "_t *obj, bool use_color) {\n",
                                  "\tif (!obj) return \"", def->name, "\";\n",
                                  "\tswitch (obj->$tag) {\n");
        for (tag_ast_t *tag = def->tags; tag; tag = tag->next) {
            cord_func = CORD_all(cord_func, "\tcase $tag$", def->name, "$", tag->name, ": return CORD_all(use_color ? \"\\x1b[36;1m",
                         def->name, ".", tag->name, "\\x1b[m(\" : \"", def->name, ".", tag->name, "(\"");

            for (arg_ast_t *field = tag->fields; field; field = field->next) {
                type_t *field_t = parse_type_ast(env, field->type);
                CORD field_str = expr_as_string(env, CORD_all("obj->", tag->name, ".", field->name), field_t, "use_color");
                CORD_appendf(&cord_func, ", \"%s=\", %r", field->name, field_str);
                if (field->next) CORD_appendf(&cord_func, ", \", \"");
            }
            CORD_appendf(&cord_func, ", \")\");\n");
        }
        CORD_appendf(&cord_func, "\t}\n}\n");
        env->code->funcs = CORD_cat(env->code->funcs, cord_func);

        // Typeinfo:
        CORD_appendf(&env->code->typedefs, "typedef struct { TypeInfo type; } %s_namespace_t;\n", def->name);
        CORD_appendf(&env->code->typedefs, "extern %s_namespace_t %s;\n", def->name, def->name);
        CORD_appendf(&env->code->typeinfos, "public %s_namespace_t %s = {{.tag=CustomInfo, .CustomInfo={.as_str=(void*)%s__as_str}}};\n", def->name, def->name, def->name);

        return CORD_EMPTY;
    }
    case DocTest: {
        auto test = Match(ast, DocTest);
        CORD src = heap_strn(test->expr->start, (size_t)(test->expr->end - test->expr->start));
        type_t *expr_t = get_type(env, test->expr);
        if (test->expr->tag == Declare) {
            auto decl = Match(test->expr, Declare);
            return CORD_asprintf(
                "%r\n"
                "__doctest(&%r, %r, %r, %r, %ld, %ld);",
                compile(env, test->expr),
                compile(env, decl->var),
                compile_type_info(env, get_type(env, decl->value)),
                compile(env, WrapAST(test->expr, StringLiteral, .cord=test->output)),
                compile(env, WrapAST(test->expr, StringLiteral, .cord=test->expr->file->filename)),
                (int64_t)(test->expr->start - test->expr->file->text),
                (int64_t)(test->expr->end - test->expr->file->text));
        } else if (test->expr->tag == Assign) {
            auto assign = Match(test->expr, Assign);
            CORD code = "{ // Assignment\n";
            int64_t i = 1;
            for (ast_list_t *value = assign->values; value; value = value->next)
                CORD_appendf(&code, "%r $%ld = %r;\n", compile_type(get_type(env, value->ast)), i++, compile(env, value->ast));
            i = 1;
            for (ast_list_t *target = assign->targets; target; target = target->next)
                CORD_appendf(&code, "%r = $%ld;\n", compile(env, target->ast), i++);

            CORD expr_cord = "CORD_all(";
            i = 1;
            for (ast_list_t *target = assign->targets; target; target = target->next) {
                CORD item = expr_as_string(env, CORD_asprintf("$%ld", i++), get_type(env, target->ast), "USE_COLOR");
                expr_cord = CORD_all(expr_cord, item, target->next ? ", \", \", " : CORD_EMPTY);
            }
            expr_cord = CORD_cat(expr_cord, ")");

            CORD_appendf(&code, "$test(%r, %r, %r);",
                compile(env, WrapAST(test->expr, StringLiteral, .cord=src)),
                expr_cord,
                compile(env, WrapAST(test->expr, StringLiteral, .cord=test->output)));
            return CORD_cat(code, "\n}");
        } else if (expr_t->tag == VoidType || expr_t->tag == AbortType) {
            return CORD_asprintf(
                "%r;\n"
                "__doctest(NULL, NULL, NULL, %r, %ld, %ld);",
                compile(env, test->expr),
                compile(env, WrapAST(test->expr, StringLiteral, .cord=test->expr->file->filename)),
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
                compile(env, WrapAST(test->expr, StringLiteral, .cord=test->output)),
                compile(env, WrapAST(test->expr, StringLiteral, .cord=test->expr->file->filename)),
                (int64_t)(test->expr->start - test->expr->file->text),
                (int64_t)(test->expr->end - test->expr->file->text));
        }
    }
    case FieldAccess: {
        auto f = Match(ast, FieldAccess);
        type_t *fielded_t = get_type(env, f->fielded);
        type_t *value_t = value_type(fielded_t);
        switch (value_t->tag) {
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
        default:
            code_err(ast, "Field accesses are only supported on struct and enum values");
        }
    }
    case Index: {
        auto indexing = Match(ast, Index);
        type_t *container_t = value_type(get_type(env, indexing->indexed));
        type_t *index_t = get_type(env, indexing->index);
        switch (container_t->tag) {
        case ArrayType: {
            if (index_t->tag != IntType)
                code_err(indexing->index, "Arrays can only be indexed by integers, not %T", index_t);
            type_t *item_type = Match(container_t, ArrayType)->item_type;
            CORD arr = compile_to_pointer_depth(env, indexing->indexed, 1, false);
            CORD index = compile(env, indexing->index);
            file_t *f = indexing->index->file;
            if (indexing->unchecked)
                return CORD_all("$Array_get_unchecked", compile_type(item_type), ", ", arr, ", ", index, ")");
            else
                return CORD_all("$Array_get(", compile_type(item_type), ", ", arr, ", ", index, ", ",
                                Str__quoted(f->filename, false), ", ", CORD_asprintf("%ld", (int64_t)(indexing->index->start - f->text)), ", ",
                                CORD_asprintf("%ld", (int64_t)(indexing->index->end - f->text)),
                                ")");
        }
        case TableType: {
            type_t *key_t = Match(container_t, TableType)->key_type;
            type_t *value_t = Match(container_t, TableType)->value_type;
            if (!can_promote(index_t, key_t))
                code_err(indexing->index, "This value has type %T, but this table can only be index with keys of type %T", index_t, key_t);
            CORD table = compile_to_pointer_depth(env, indexing->indexed, 1, false);
            CORD key = compile(env, indexing->index);
            file_t *f = indexing->index->file;
            return CORD_all("$Table_get(", table, ", ", compile_type(key_t), ", ", compile_type(value_t), ", ",
                            key, ", ", compile_type_info(env, container_t), ", ",
                            Str__quoted(f->filename, false), ", ", CORD_asprintf("%ld", (int64_t)(indexing->index->start - f->text)), ", ",
                            CORD_asprintf("%ld", (int64_t)(indexing->index->end - f->text)),
                            ")");
        }
        default: code_err(ast, "Indexing is not supported for type: %T", container_t);
        }
    }
    // Use,
    // LinkerDirective,
    case Unknown: code_err(ast, "Unknown AST");
    default: break;
    }
    return NULL;
}

CORD compile_type_info(env_t *env, type_t *t)
{
    switch (t->tag) {
    case BoolType: return "&Bool.type";
    case IntType: return CORD_asprintf("&Int%ld.type", Match(t, IntType)->bits);
    case NumType: return CORD_asprintf("&Num%ld.type", Match(t, NumType)->bits);
    case StringType: return CORD_all("&", Match(t, StringType)->dsl ? Match(t, StringType)->dsl : "Str", ".type");
    case StructType: return CORD_all("&", Match(t, StructType)->name, ".type");
    case EnumType: return CORD_all("&", Match(t, EnumType)->name, ".type");
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
        return CORD_asprintf("$PointerInfo(%r, %r)", Str__quoted(sigil, false), compile_type_info(env, ptr->pointed));
    }
    case FunctionType: {
        return CORD_asprintf("$FunctionInfo(%r)", Str__quoted(type_to_cord(t), false));
    }
    case ClosureType: {
        errx(1, "No typeinfo for closures yet");
    }
    default: errx(1, "No such typeinfo");
    }
}

module_code_t compile_file(ast_t *ast)
{
    env_t *env = new_compilation_unit();
    CORD_appendf(&env->code->imports, "#include \"nextlang.h\"\n");

    for (ast_list_t *stmt = Match(ast, Block)->statements; stmt; stmt = stmt->next) {
        CORD code = compile_statement(env, stmt->ast);
        if (code)
            CORD_appendf(&env->code->main, "%r\n", code);
        bind_statement(env, stmt->ast);
    }

    return (module_code_t){
        .header=CORD_all(
            // CORD_asprintf("#line 0 %r\n", Str__quoted(ast->file->filename, false)),
            env->code->imports, "\n",
            env->code->typedefs, "\n",
            env->code->typecode, "\n"),
        .c_file=CORD_all(
            // CORD_asprintf("#line 0 %r\n", Str__quoted(ast->file->filename, false)),
            env->code->staticdefs, "\n",
            env->code->funcs, "\n",
            env->code->typeinfos, "\n",
            "\n"
            "static void $load(void) {\n",
            env->code->main,
            "}\n"
        ),
   };
}

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
