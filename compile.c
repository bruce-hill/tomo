
#include <ctype.h>
#include <gc/cord.h>
#include <gc.h>
#include <stdio.h>

#include "ast.h"
#include "compile.h"
#include "environment.h"
#include "typecheck.h"
#include "util.h"

CORD compile_type(env_t *env, type_ast_t *t)
{
    (void)env;
    switch (t->tag) {
    case VarTypeAST: return CORD_cat(Match(t, VarTypeAST)->name, "_t");
    default: code_err(t, "Not implemented");
    }
}

CORD compile_statement(env_t *env, ast_t *ast)
{
    CORD stmt;
    switch (ast->tag) {
    case If: case For: case While: case FunctionDef: case Return: case StructDef: case EnumDef:
    case Declare: case Assign: case UpdateAssign: case DocTest:
        stmt = compile(env, ast);
        break;
    default:
        stmt = CORD_asprintf("(void)%r;", compile(env, ast));
        break;
    }
    int64_t line = get_line_number(ast->file, ast->start);
    return stmt ? CORD_asprintf("#line %ld\n%r", line, stmt) : stmt;
}

CORD compile(env_t *env, ast_t *ast)
{
    switch (ast->tag) {
    case Nil: return CORD_asprintf("(%r)NULL", compile_type(env, Match(ast, Nil)->type));
    case Bool: return Match(ast, Bool)->b ? "yes" : "no";
    case Var: return Match(ast, Var)->name;
    case Int: return CORD_asprintf("I%ld(%ld)", Match(ast, Int)->bits, Match(ast, Int)->i);
    case Num: {
        // HACK: since the cord library doesn't support the '%a' specifier, this workaround
        // is necessary:
        char *buf = asprintfa(Match(ast, Num)->bits == 64 ? "%a" : "%af", Match(ast, Num)->n);
        return CORD_from_char_star(buf);
    }
    case Not: return CORD_asprintf("not(%r)", compile(env, Match(ast, Not)->value));
    case Negative: return CORD_asprintf("-(%r)", compile(env, Match(ast, Not)->value));
    case HeapAllocate: return CORD_asprintf("$heap(%r)", compile(env, Match(ast, Not)->value));
    case StackReference: return CORD_asprintf("$stack(%r)", compile(env, Match(ast, Not)->value));
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
            code_err(ast, "I can't do binary operations between %T and %T", lhs_t, rhs_t);

        switch (binop->op) {
        case BINOP_MULT: return CORD_asprintf("(%r * %r)", lhs, rhs);
        case BINOP_DIVIDE: return CORD_asprintf("(%r / %r)", lhs, rhs);
        case BINOP_MOD: return CORD_asprintf("mod(%r, %r)", lhs, rhs);
        case BINOP_MOD1: return CORD_asprintf("mod1(%r, %r)", lhs, rhs);
        case BINOP_PLUS: return CORD_asprintf("(%r + %r)", lhs, rhs);
        case BINOP_MINUS: return CORD_asprintf("(%r - %r)", lhs, rhs);
        case BINOP_LSHIFT: return CORD_asprintf("(%r << %r)", lhs, rhs);
        case BINOP_RSHIFT: return CORD_asprintf("(%r >> %r)", lhs, rhs);
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
        case BINOP_AND: return CORD_asprintf("and(%r, %r)", lhs, rhs);
        case BINOP_OR: return CORD_asprintf("or(%r, %r)", lhs, rhs);
        case BINOP_XOR: return CORD_asprintf("xor(%r, %r)", lhs, rhs);
        default: break;
        }
        code_err(ast, "unimplemented binop");
    }
    case UpdateAssign: {
        auto update = Match(ast, UpdateAssign);
        CORD lhs = compile(env, update->lhs);
        CORD rhs = compile(env, update->rhs);
        switch (update->op) {
        case BINOP_MULT: return CORD_asprintf("%r *= %r;", lhs, rhs);
        case BINOP_DIVIDE: return CORD_asprintf("%r /= %r;", lhs, rhs);
        case BINOP_MOD: return CORD_asprintf("%r = %r %% %r;", lhs, lhs, rhs);
        case BINOP_PLUS: return CORD_asprintf("%r += %r;", lhs, rhs);
        case BINOP_MINUS: return CORD_asprintf("%r -= %r;", lhs, rhs);
        case BINOP_LSHIFT: return CORD_asprintf("%r <<= %r;", lhs, rhs);
        case BINOP_RSHIFT: return CORD_asprintf("%r >>= %r;", lhs, rhs);
        case BINOP_EQ: return CORD_asprintf("%r = (%r == %r);", lhs, lhs, rhs);
        case BINOP_NE: return CORD_asprintf("%r = (%r != %r);", lhs, lhs, rhs);
        case BINOP_LT: return CORD_asprintf("%r = (%r < %r);", lhs, lhs, rhs);
        case BINOP_LE: return CORD_asprintf("%r = (%r <= %r);", lhs, lhs, rhs);
        case BINOP_GT: return CORD_asprintf("%r = (%r > %r);", lhs, lhs, rhs);
        case BINOP_GE: return CORD_asprintf("%r = (%r >= %r);", lhs, lhs, rhs);
        case BINOP_AND: return CORD_asprintf("%r = (%r && %r);", lhs, lhs, rhs);
        case BINOP_OR: return CORD_asprintf("%r = (%r || %r);", lhs, lhs, rhs);
        default: break;
        }
        code_err(ast, "unimplemented binop");
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
            CORD code = compile(env, chunks->ast);
            if (chunks->ast->tag != StringLiteral)
                code = CORD_asprintf("$cord(%r)", code);
            return code;
        } else {
            int64_t num_chunks = 0;
            for (ast_list_t *chunk = chunks; chunk; chunk = chunk->next)
                ++num_chunks;

            CORD code = CORD_asprintf("CORD_catn(%ld", num_chunks);
            for (ast_list_t *chunk = chunks; chunk; chunk = chunk->next) {
                CORD chunk_code = compile(env, chunk->ast);
                if (chunk->ast->tag != StringLiteral)
                    chunk_code = CORD_asprintf("$cord(%r)", chunk_code);
                CORD_sprintf(&code, "%r, %r", code, chunk_code);
            }
            return CORD_cat_char(code, ')');
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
        return CORD_asprintf("$var(%r, %r);", compile(env, decl->var), compile(env, decl->value));
    }
    case Assign: {
        auto assign = Match(ast, Assign);
        // Single assignment:
        if (assign->targets && !assign->targets->next)
            return CORD_asprintf("%r = %r", compile(env, assign->targets->ast), compile(env, assign->values->ast));

        CORD code = "{ // Assignment\n";
        int64_t i = 1;
        for (ast_list_t *value = assign->values; value; value = value->next)
            CORD_appendf(&code, "$var($%ld, %r);\n", i++, compile(env, value->ast));
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
    // Array, Table, TableEntry,
    case Array: {
        auto array = Match(ast, Array);
        if (!array->items)
            return "(array_t){}";
           
        CORD code = "$array(";
        for (ast_list_t *item = array->items; item; item = item->next) {
            code = CORD_cat(code, compile(env, item->ast));
            if (item->next) code = CORD_cat(code, ", ");
        }
        return CORD_cat_char(code, ')');
    }
    case FunctionDef: {
        auto fndef = Match(ast, FunctionDef);
        CORD name = compile(env, fndef->name);
        CORD_appendf(&env->code->staticdefs, "static %r %r_(", fndef->ret_type ? compile_type(env, fndef->ret_type) : "void", name);
        for (arg_ast_t *arg = fndef->args; arg; arg = arg->next) {
            CORD_appendf(&env->code->staticdefs, "%r %s", compile_type(env, arg->type), arg->name);
            if (arg->next) env->code->staticdefs = CORD_cat(env->code->staticdefs, ", ");
        }
        env->code->staticdefs = CORD_cat(env->code->staticdefs, ");\n");

        CORD kwargs = CORD_asprintf("#define %r(...) ({ struct {", name);
        CORD passed_args = CORD_EMPTY;
        CORD_appendf(&env->code->funcs, "%r %r_(", fndef->ret_type ? compile_type(env, fndef->ret_type) : "void", name);
        env_t *body_scope = fresh_scope(env);
        body_scope->locals->fallback = env->globals;
        for (arg_ast_t *arg = fndef->args; arg; arg = arg->next) {
            CORD arg_type = compile_type(env, arg->type);
            CORD_appendf(&env->code->funcs, "%r %s", arg_type, arg->name);
            if (arg->next) env->code->funcs = CORD_cat(env->code->funcs, ", ");
            CORD_appendf(&kwargs, "%r %s; ", arg_type, arg->name);
            CORD_appendf(&passed_args, "$args.%s", arg->name);
            if (arg->next) passed_args = CORD_cat(passed_args, ", ");
            set_binding(body_scope, arg->name, new(binding_t, .type=parse_type_ast(env, arg->type)));
        }
        CORD_appendf(&kwargs, "} $args = {__VA_ARGS__}; %r_(%r); })\n", name, passed_args);
        CORD_appendf(&env->code->staticdefs, "%r", kwargs);

        CORD body = compile(env, fndef->body);
        if (CORD_fetch(body, 0) != '{')
            body = CORD_asprintf("{\n%r\n}", body);
        CORD_appendf(&env->code->funcs, ") %r", body);
        return CORD_EMPTY;
    }
    case FunctionCall: {
        auto call = Match(ast, FunctionCall);
        CORD code = CORD_cat_char(compile(env, call->fn), '(');
        for (ast_list_t *arg = call->args; arg; arg = arg->next) {
            code = CORD_cat(code, compile(env, arg->ast));
            if (arg->next) code = CORD_cat(code, ", ");
        }
        return CORD_cat_char(code, ')');
    }
    // Lambda,
    case KeywordArg: {
        auto kwarg = Match(ast, KeywordArg);
        return CORD_asprintf(".%s=%r", kwarg->name, compile(env, kwarg->arg));
    }
    // KeywordArg,
    case If: {
        auto if_ = Match(ast, If);
        CORD code;
        CORD_sprintf(&code, "if (%r) %r", compile(env, if_->condition), compile(env, if_->body));
        if (if_->else_body)
            CORD_sprintf(&code, "%r\nelse %r", code, compile(env, if_->else_body));
        return code;
    }
    case While: {
        auto while_ = Match(ast, While);
        return CORD_asprintf("while (%r) %r", compile(env, while_->condition), compile(env, while_->body));
    }
    case For: {
        auto for_ = Match(ast, For);
        CORD index = for_->index ? compile(env, for_->index) : "$i";
        return CORD_asprintf("{\n"
                             "$var($iter, %r);\n"
                             "for (int64_t %r = 1, $len = $length($iter); %r <= $len; ++%r) {\n"
                             "$var(%r, $safe_index($iter, %s));\n"
                             "%r\n"
                             "}\n}",
                             compile(env, for_->iter),
                             index, index, index,
                             compile(env, for_->value), index,
                             compile(env, for_->body));
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
        auto def = Match(ast, StructDef);
        CORD_appendf(&env->code->typedefs, "typedef struct %s_s %s_t;\n", def->name, def->name);
        CORD_appendf(&env->code->typedefs, "#define %s(...) ((%s_t){__VA_ARGS__})\n", def->name, def->name);

        CORD_appendf(&env->code->typecode, "struct %s_s {\n", def->name);
        for (arg_ast_t *field = def->fields; field; field = field->next) {
            CORD type = compile_type(env, field->type);
            CORD_appendf(&env->code->typecode, "%r %s%s;\n", type, field->name,
                         CORD_cmp(type, "Bool_t") ? "" : ":1");
        }
        CORD_appendf(&env->code->typecode, "};\n");

        CORD cord_func = CORD_asprintf("CORD %s$cord(%s_t *obj, bool use_color) {\n"
                                       "\tif (!obj) return \"%s\";\n", def->name, def->name, def->name);

        if (def->secret) {
            CORD_appendf(&cord_func, "\treturn use_color ? \"\\x1b[0;1m%s\\x1b[m(\\x1b[2m...\\x1b[m)\" : \"%s(...)\";\n}",
                         def->name, def->name);
        } else {
            CORD_appendf(&cord_func, "\treturn StrF(use_color ? \"\\x1b[0;1m%s\\x1b[m(", def->name);
            for (arg_ast_t *field = def->fields; field; field = field->next) {
                CORD_appendf(&cord_func, "%s=\\x1b[35m%%r\\x1b[m", field->name);
                if (field->next) cord_func = CORD_cat(cord_func, ", ");
            }
            CORD_appendf(&cord_func, ")\" : \"%s(", def->name);
            for (arg_ast_t *field = def->fields; field; field = field->next) {
                CORD_appendf(&cord_func, "%s=%%r", field->name);
                if (field->next) cord_func = CORD_cat(cord_func, ", ");
            }
            cord_func = CORD_cat(cord_func, ")\"");
            for (arg_ast_t *field = def->fields; field; field = field->next)
                CORD_appendf(&cord_func, ", $cord(obj->%s)", field->name);
            cord_func = CORD_cat(cord_func, ");\n}");
        }

        env->code->funcs = CORD_cat(env->code->funcs, cord_func);

        return CORD_EMPTY;
    }
    case EnumDef: {
        auto def = Match(ast, EnumDef);
        CORD_appendf(&env->code->typedefs, "typedef struct %s_s %s_t;\n", def->name, def->name);
        CORD_appendf(&env->code->typecode, "struct %s_s {\nenum {", def->name);
        for (tag_ast_t *tag = def->tags; tag; tag = tag->next) {
            CORD_appendf(&env->code->typecode, "%s$%s = %ld, ", def->name, tag->name, tag->value);
        }
        env->code->typecode = CORD_cat(env->code->typecode, "} tag;\nunion {\n");
        for (tag_ast_t *tag = def->tags; tag; tag = tag->next) {
            env->code->typecode = CORD_cat(env->code->typecode, "struct {\n");
            for (arg_ast_t *field = tag->fields; field; field = field->next) {
                CORD type = compile_type(env, field->type);
                CORD_appendf(&env->code->typecode, "%r %s%s;\n", type, field->name,
                             CORD_cmp(type, "Bool_t") ? "" : ":1");
            }
            CORD_appendf(&env->code->typecode, "} %s;\n", tag->name);
        }
        env->code->typecode = CORD_cat(env->code->typecode, "} $data;\n};\n");
        return CORD_EMPTY;
    }
    case DocTest: {
        auto test = Match(ast, DocTest);
        CORD src = heap_strn(test->expr->start, (size_t)(test->expr->end - test->expr->start));
        type_t *expr_t = get_type(env, test->expr);
        if (test->expr->tag == Declare) {
            auto decl = Match(test->expr, Declare);
            return CORD_asprintf(
                "$var(%r, %r);\n"
                "__doctest(&%r, %r, %r, %r, %ld, %ld);",
                compile(env, decl->var), compile(env, decl->value),
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
                CORD_appendf(&code, "$var($%ld, %r);\n", i++, compile(env, value->ast));
            i = 1;
            for (ast_list_t *target = assign->targets; target; target = target->next)
                CORD_appendf(&code, "%r = $%ld;\n", compile(env, target->ast), i++);

            CORD expr_cord = "StrF(\"";
            for (ast_list_t *target = assign->targets; target; target = target->next)
                expr_cord = CORD_cat(expr_cord, target->next ? "%r, " : "%r");
            expr_cord = CORD_cat(expr_cord, "\"");
            i = 1;
            for (ast_list_t *target = assign->targets; target; target = target->next)
                CORD_appendf(&expr_cord, ", $cord($%ld)", i++);
            expr_cord = CORD_cat(expr_cord, ")");

            CORD_appendf(&code, "$test(%r, %r, %r);",
                compile(env, WrapAST(test->expr, StringLiteral, .cord=src)),
                expr_cord,
                compile(env, WrapAST(test->expr, StringLiteral, .cord=test->output)));
            return CORD_cat(code, "\n}");
        } else if (expr_t->tag == VoidType || expr_t->tag == AbortType) {
            return CORD_asprintf(
                "__doctest((%r, NULL), NULL, NULL, %r, %ld, %ld);",
                compile(env, test->expr),
                compile(env, WrapAST(test->expr, StringLiteral, .cord=test->expr->file->filename)),
                (int64_t)(test->expr->start - test->expr->file->text),
                (int64_t)(test->expr->end - test->expr->file->text));
        } else {
            return CORD_asprintf(
                "__doctest($stack(%r), %r, %r, %r, %ld, %ld);",
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
        return CORD_asprintf("(%r).%s", compile(env, f->fielded), f->field);
    }
    // Index, FieldAccess,
    // DocTest,
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
    case BoolType: return "&Bool_type.type";
    case IntType: return CORD_asprintf("&Int%ld_type.type", Match(t, IntType)->bits);
    case NumType: return CORD_asprintf("&Num%ld_type.type", Match(t, NumType)->bits);
    case StringType: return CORD_all("&", Match(t, StringType)->dsl ? Match(t, StringType)->dsl : "Str", "_type.type");
    case StructType: return CORD_all("&", Match(t, StructType)->name, "_type.type");
    case EnumType: return CORD_all("&", Match(t, EnumType)->name, "_type.type");
    case ArrayType: {
        type_t *item_t = Match(t, ArrayType)->item_type;
        return CORD_asprintf(
            "&((TypeInfo){.size=%zu, .align=%zu, .tag=ArrayInfo, .ArrayInfo.item=%r})",
            sizeof(array_t), alignof(array_t),
            compile_type_info(env, item_t));
    }
    case TableType: {
        type_t *key_type = Match(t, TableType)->key_type;
        type_t *value_type = Match(t, TableType)->value_type;
        return CORD_asprintf(
            "&((TypeInfo){.size=%zu, .align=%zu, .tag=TableInfo, .TableInfo.key=%r, .TableInfo.value=%r})",
            sizeof(table_t), alignof(table_t),
            compile_type_info(env, key_type),
            compile_type_info(env, value_type));
    }
    case PointerType: {
        auto ptr = Match(t, PointerType);
        CORD sigil = ptr->is_stack ? "&" : (ptr->is_optional ? "?" : "@");
        if (ptr->is_readonly) sigil = CORD_cat(sigil, "(readonly)");
        return CORD_asprintf(
            "&((TypeInfo){.size=%zu, .align=%zu, .tag=PointerInfo, .PointerInfo.sigil=\"%r\", .PointerInfo.pointed=%r})",
            sizeof(void*), alignof(void*),
            sigil, compile_type_info(env, ptr->pointed));
    }
    case FunctionType: {
        return CORD_asprintf("&((TypeInfo){.size=%zu, .align=%zu, .tag=FunctionInfo, .FunctionInfo.type_str=\"%r\"})",
                             sizeof(void*), alignof(void*), type_to_cord(t));
    }
    case ClosureType: {
        errx(1, "No typeinfo for closures yet");
    }
    default: errx(1, "No such typeinfo");
    }
}

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
