
#include <ctype.h>
#include <gc/cord.h>
#include <gc.h>
#include <stdio.h>

#include "ast.h"
#include "compile.h"
#include "util.h"

CORD compile_type(env_t *env, type_ast_t *t)
{
    (void)env;
    switch (t->tag) {
    case VarTypeAST: return CORD_cat(Match(t, VarTypeAST)->name, "_t");
    default: errx(1, "Not implemented");
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
    case Int: return CORD_asprintf("I%ld(%ld)", Match(ast, Int)->precision, Match(ast, Int)->i);
    case Num: {
        // HACK: since the cord library doesn't support the '%a' specifier, this workaround
        // is necessary:
        char *buf = asprintfa(Match(ast, Num)->precision == 64 ? "%a" : "%af", Match(ast, Num)->n);
        return CORD_from_char_star(buf);
    }
    case UnaryOp: {
        auto unop = Match(ast, UnaryOp);
        CORD expr = compile(env, unop->value);
        switch (unop->op) {
        case UNOP_NOT: return CORD_asprintf("not(%r)", expr);
        case UNOP_NEGATIVE: return CORD_cat("-", expr);
        case UNOP_HEAP_ALLOCATE: return CORD_asprintf("$heap(%r)", expr);
        case UNOP_STACK_REFERENCE: return CORD_asprintf("$stack(%r)", expr);
        default: break;
        }
        errx(1, "Invalid unop");
    }
    case BinaryOp: {
        auto binop = Match(ast, BinaryOp);
        CORD lhs = compile(env, binop->lhs);
        CORD rhs = compile(env, binop->rhs);
        switch (binop->op) {
        case BINOP_MULT: return CORD_asprintf("(%r * %r)", lhs, rhs);
        case BINOP_DIVIDE: return CORD_asprintf("(%r / %r)", lhs, rhs);
        case BINOP_MOD: return CORD_asprintf("mod(%r, %r)", lhs, rhs);
        case BINOP_MOD1: return CORD_asprintf("mod1(%r, %r)", lhs, rhs);
        case BINOP_PLUS: return CORD_asprintf("(%r + %r)", lhs, rhs);
        case BINOP_MINUS: return CORD_asprintf("(%r - %r)", lhs, rhs);
        case BINOP_LSHIFT: return CORD_asprintf("(%r << %r)", lhs, rhs);
        case BINOP_RSHIFT: return CORD_asprintf("(%r >> %r)", lhs, rhs);
        case BINOP_EQ: return CORD_asprintf("$eq(%r, %r)", lhs, rhs);
        case BINOP_NE: return CORD_asprintf("$ne(%r, %r)", lhs, rhs);
        case BINOP_LT: return CORD_asprintf("$lt(%r, %r)", lhs, rhs);
        case BINOP_LE: return CORD_asprintf("$le(%r, %r)", lhs, rhs);
        case BINOP_GT: return CORD_asprintf("$gt(%r, %r)", lhs, rhs);
        case BINOP_GE: return CORD_asprintf("$ge(%r, %r)", lhs, rhs);
        case BINOP_AND: return CORD_asprintf("and(%r, %r)", lhs, rhs);
        case BINOP_OR: return CORD_asprintf("or(%r, %r)", lhs, rhs);
        case BINOP_XOR: return CORD_asprintf("xor(%r, %r)", lhs, rhs);
        default: break;
        }
        errx(1, "unimplemented binop");
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
        errx(1, "unimplemented binop");
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
        for (ast_list_t *stmt = stmts; stmt; stmt = stmt->next) {
            code = CORD_cat(code, compile_statement(env, stmt->ast));
            code = CORD_cat(code, "\n");
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
        CORD_appendf(&env->staticdefs, "static %r %r_(", fndef->ret_type ? compile_type(env, fndef->ret_type) : "void", name);
        for (arg_ast_t *arg = fndef->args; arg; arg = arg->next) {
            CORD_appendf(&env->staticdefs, "%r %s", compile_type(env, arg->type), arg->name);
            if (arg->next) env->staticdefs = CORD_cat(env->staticdefs, ", ");
        }
        env->staticdefs = CORD_cat(env->staticdefs, ");\n");

        CORD kwargs = CORD_asprintf("#define %r(...) ({ struct {", name);
        CORD passed_args = CORD_EMPTY;
        CORD_appendf(&env->funcs, "%r %r_(", fndef->ret_type ? compile_type(env, fndef->ret_type) : "void", name);
        for (arg_ast_t *arg = fndef->args; arg; arg = arg->next) {
            CORD arg_type = compile_type(env, arg->type);
            CORD_appendf(&env->funcs, "%r %s", arg_type, arg->name);
            if (arg->next) env->funcs = CORD_cat(env->funcs, ", ");
            CORD_appendf(&kwargs, "%r %s; ", arg_type, arg->name);
            CORD_appendf(&passed_args, "$args.%s", arg->name);
            if (arg->next) passed_args = CORD_cat(passed_args, ", ");
        }
        CORD_appendf(&kwargs, "} $args = {__VA_ARGS__}; %r_(%r); })\n", name, passed_args);
        CORD_appendf(&env->staticdefs, "%r", kwargs);

        CORD body = compile(env, fndef->body);
        if (CORD_fetch(body, 0) != '{')
            body = CORD_asprintf("{\n%r\n}", body);
        CORD_appendf(&env->funcs, ") %r", body);
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
        if (Match(ast, Skip)->target) errx(1, "Named skips not yet implemented");
        return "continue";
    }
    case Stop: {
        if (Match(ast, Stop)->target) errx(1, "Named stops not yet implemented");
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
        CORD_appendf(&env->typedefs, "typedef struct %s_s %s_t;\n", def->name, def->name);
        CORD_appendf(&env->typedefs, "#define %s(...) ((%s_t){__VA_ARGS__})\n", def->name, def->name);

        CORD_appendf(&env->typecode, "struct %s_s {\n", def->name);
        for (arg_ast_t *field = def->fields; field; field = field->next) {
            CORD type = compile_type(env, field->type);
            CORD_appendf(&env->typecode, "%r %s%s;\n", type, field->name,
                         CORD_cmp(type, "Bool_t") ? "" : ":1");
        }
        CORD_appendf(&env->typecode, "};\n");

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

        env->funcs = CORD_cat(env->funcs, cord_func);

        return CORD_EMPTY;
    }
    case EnumDef: {
        auto def = Match(ast, EnumDef);
        CORD_appendf(&env->typedefs, "typedef struct %s_s %s_t;\n", def->name, def->name);
        CORD_appendf(&env->typecode, "struct %s_s {\nenum {", def->name);
        for (tag_ast_t *tag = def->tags; tag; tag = tag->next) {
            CORD_appendf(&env->typecode, "%s$%s = %ld, ", def->name, tag->name, tag->value);
        }
        env->typecode = CORD_cat(env->typecode, "} tag;\nunion {\n");
        for (tag_ast_t *tag = def->tags; tag; tag = tag->next) {
            env->typecode = CORD_cat(env->typecode, "struct {\n");
            for (arg_ast_t *field = tag->fields; field; field = field->next) {
                CORD type = compile_type(env, field->type);
                CORD_appendf(&env->typecode, "%r %s%s;\n", type, field->name,
                             CORD_cmp(type, "Bool_t") ? "" : ":1");
            }
            CORD_appendf(&env->typecode, "} %s;\n", tag->name);
        }
        env->typecode = CORD_cat(env->typecode, "} $data;\n};\n");
        return CORD_EMPTY;
    }
    case DocTest: {
        auto test = Match(ast, DocTest);
        CORD src = heap_strn(test->expr->start, (size_t)(test->expr->end - test->expr->start));
        if (test->expr->tag == Declare) {
            auto decl = Match(test->expr, Declare);
            return CORD_asprintf(
                "$var(%r, %r);\n$test(%r, %r, %r);",
                compile(env, decl->var), compile(env, decl->value),
                compile(env, WrapAST(test->expr, StringLiteral, .cord=src)),
                compile(env, decl->var),
                compile(env, WrapAST(test->expr, StringLiteral, .cord=test->output)));
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
        } else {
            return CORD_asprintf(
                "$test(%r, %r, %r);",
                compile(env, WrapAST(test->expr, StringLiteral, .cord=src)),
                compile(env, test->expr),
                compile(env, WrapAST(test->expr, StringLiteral, .cord=test->output)));
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
    case Unknown: errx(1, "Unknown AST");
    default: break;
    }
    return NULL;
}

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
