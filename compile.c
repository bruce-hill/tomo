
#include <ctype.h>
#include <gc/cord.h>
#include <gc.h>
#include <stdio.h>

#include "ast.h"
#include "compile.h"
#include "util.h"

CORD compile_type(type_ast_t *t)
{
    switch (t->tag) {
    case VarTypeAST: return CORD_cat(Match(t, VarTypeAST)->name, "_t");
    default: errx(1, "Not implemented");
    }
}

static inline CORD compile_statement(ast_t *ast)
{
    switch (ast->tag) {
    case If: case For: case While: case FunctionDef: case Return: case StructDef: case EnumDef:
    case Declare: case Assign: case UpdateAssign:
        return compile(ast);
    default:
        return CORD_asprintf("(void)%r;", compile(ast));
    }
}

CORD compile(ast_t *ast)
{
    switch (ast->tag) {
    case Nil: return CORD_asprintf("(%r)NULL", compile_type(Match(ast, Nil)->type));
    case Bool: return Match(ast, Bool)->b ? "true" : "false";
    case Var: return Match(ast, Var)->name;
    case Int: return CORD_asprintf("((Int%ld_t)%ld)", Match(ast, Int)->precision, Match(ast, Int)->i);
    case Num: return CORD_asprintf(Match(ast, Num)->precision == 64 ? "%g" : "%gf", Match(ast, Num)->n);
    case Char: return CORD_asprintf("(char)'\\x%02X'", (int)Match(ast, Char)->c);
    case UnaryOp: {
        auto unop = Match(ast, UnaryOp);
        CORD expr = compile(unop->value);
        switch (unop->op) {
        case UNOP_NOT: return CORD_asprintf("not(%r)", expr);
        case UNOP_NEGATIVE: return CORD_cat("-", expr);
        case UNOP_HEAP_ALLOCATE: return CORD_asprintf("__heap(%r)", expr);
        case UNOP_STACK_REFERENCE: return CORD_asprintf("__stack(%r)", expr);
        default: break;
        }
        errx(1, "Invalid unop");
    }
    case BinaryOp: {
        auto binop = Match(ast, BinaryOp);
        CORD lhs = compile(binop->lhs);
        CORD rhs = compile(binop->rhs);
        switch (binop->op) {
        case BINOP_MULT: return CORD_asprintf("(%r * %r)", lhs, rhs);
        case BINOP_DIVIDE: return CORD_asprintf("(%r / %r)", lhs, rhs);
        case BINOP_MOD: return CORD_asprintf("mod(%r, %r)", lhs, rhs);
        case BINOP_MOD1: return CORD_asprintf("mod1(%r, %r)", lhs, rhs);
        case BINOP_PLUS: return CORD_asprintf("(%r + %r)", lhs, rhs);
        case BINOP_MINUS: return CORD_asprintf("(%r - %r)", lhs, rhs);
        case BINOP_LSHIFT: return CORD_asprintf("(%r << %r)", lhs, rhs);
        case BINOP_RSHIFT: return CORD_asprintf("(%r >> %r)", lhs, rhs);
        case BINOP_EQ: return CORD_asprintf("(%r == %r)", lhs, rhs);
        case BINOP_NE: return CORD_asprintf("(%r != %r)", lhs, rhs);
        case BINOP_LT: return CORD_asprintf("(%r < %r)", lhs, rhs);
        case BINOP_LE: return CORD_asprintf("(%r <= %r)", lhs, rhs);
        case BINOP_GT: return CORD_asprintf("(%r > %r)", lhs, rhs);
        case BINOP_GE: return CORD_asprintf("(%r >= %r)", lhs, rhs);
        case BINOP_AND: return CORD_asprintf("and(%r, %r)", lhs, rhs);
        case BINOP_OR: return CORD_asprintf("or(%r, %r)", lhs, rhs);
        case BINOP_XOR: return CORD_asprintf("xor(%r, %r)", lhs, rhs);
        default: break;
        }
        errx(1, "unimplemented binop");
    }
    case UpdateAssign: {
        auto update = Match(ast, UpdateAssign);
        CORD lhs = compile(update->lhs);
        CORD rhs = compile(update->rhs);
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
            int c = CORD_pos_fetch(i);
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
            CORD code = compile(chunks->ast);
            if (chunks->ast->tag != StringLiteral)
                code = CORD_asprintf("__cord(%r)", code);
            return code;
        } else {
            int64_t num_chunks = 0;
            for (ast_list_t *chunk = chunks; chunk; chunk = chunk->next)
                ++num_chunks;

            CORD code = CORD_asprintf("CORD_catn(%ld", num_chunks);
            for (ast_list_t *chunk = chunks; chunk; chunk = chunk->next) {
                CORD chunk_code = compile(chunk->ast);
                if (chunk->ast->tag != StringLiteral)
                    chunk_code = CORD_asprintf("__cord(%r)", chunk_code);
                CORD_sprintf(&code, "%r, %r", code, chunk_code);
            }
            return CORD_cat_char(code, ')');
        }
    }
    case Block: {
        ast_list_t *stmts = Match(ast, Block)->statements;
        if (stmts && !stmts->next)
            return compile_statement(stmts->ast);

        CORD code = "{\n";
        for (ast_list_t *stmt = stmts; stmt; stmt = stmt->next) {
            code = CORD_cat(code, compile_statement(stmt->ast));
            code = CORD_cat(code, "\n");
        }
        return CORD_cat(code, "}");
    }
    case Declare: {
        auto decl = Match(ast, Declare);
        return CORD_asprintf("__declare(%r, %r)", compile(decl->var), compile(decl->value));
    }
    case Assign: {
        auto assign = Match(ast, Assign);
        CORD code = CORD_EMPTY;
        for (ast_list_t *target = assign->targets, *value = assign->values; target && value; target = target->next, value = value->next) {
            CORD_sprintf(&code, "%r = %r", compile(target->ast), compile(value->ast));
            if (target->next) code = CORD_cat(code, ", ");
        }
        return CORD_cat(code, ";");
    }
    case Min: {
        return CORD_asprintf("min(%r, %r)", compile(Match(ast, Min)->lhs), compile(Match(ast, Min)->rhs));
    }
    case Max: {
        return CORD_asprintf("max(%r, %r)", compile(Match(ast, Max)->lhs), compile(Match(ast, Max)->rhs));
    }
    // Min, Max,
    // Array, Table, TableEntry,
    case Array: {
        auto array = Match(ast, Array);
        if (!array->items)
            return "(array_t){}";
           
        CORD code = "__array(";
        for (ast_list_t *item = array->items; item; item = item->next) {
            code = CORD_cat(code, compile(item->ast));
            if (item->next) code = CORD_cat(code, ", ");
        }
        return CORD_cat_char(code, ')');
    }
    case FunctionDef: {
        auto fndef = Match(ast, FunctionDef);
        CORD code = CORD_asprintf("%r %r(", fndef->ret_type ? compile_type(fndef->ret_type) : "void", compile(fndef->name));
        for (arg_ast_t *arg = fndef->args; arg; arg = arg->next) {
            CORD_sprintf(&code, "%r%r %s", code, compile_type(arg->type), arg->name);
            if (arg->next) code = CORD_cat(code, ", ");
        }
        code = CORD_cat(code, ") ");
        code = CORD_cat(code, compile(fndef->body));
        return code;
    }
    case FunctionCall: {
        auto call = Match(ast, FunctionCall);
        CORD code = CORD_cat_char(compile(call->fn), '(');
        for (ast_list_t *arg = call->args; arg; arg = arg->next) {
            code = CORD_cat(code, compile(arg->ast));
            if (arg->next) code = CORD_cat(code, ", ");
        }
        return CORD_cat_char(code, ')');
    }
    // Lambda,
    // KeywordArg,
    case If: {
        auto if_ = Match(ast, If);
        CORD code;
        CORD_sprintf(&code, "if (%r) %r", compile(if_->condition), compile(if_->body));
        if (if_->else_body)
            CORD_sprintf(&code, "%r\nelse %r", code, compile(if_->else_body));
        return code;
    }
    case While: {
        auto while_ = Match(ast, While);
        return CORD_asprintf("while (%r) %r", compile(while_->condition), compile(while_->body));
    }
    case For: {
        auto for_ = Match(ast, For);
        CORD index = for_->index ? compile(for_->index) : "__i";
        return CORD_asprintf("{\n"
                             "__declare(__iter, %r);\n"
                             "for (int64_t %r = 1, __len = __length(__iter); %r <= __len; ++%r) {\n"
                             "__declare(%r, __safe_index(__iter, %s));\n"
                             "%r\n"
                             "}\n}",
                             compile(for_->iter),
                             index, index, index,
                             compile(for_->value), index,
                             compile(for_->body));
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
        return ret ? CORD_asprintf("return %r;", compile(ret)) : "return;";
    }
    // Extern,
    case StructDef: {
        auto def = Match(ast, StructDef);
        CORD code = CORD_asprintf("typedef struct %s_s %s_t;\nstruct %s_s {\n", def->name, def->name, def->name);
        for (arg_ast_t *field = def->fields; field; field = field->next) {
            CORD_sprintf(&code, "%r%r %s;\n", code, compile_type(field->type), field->name);
        }
        code = CORD_cat(code, "};\n");
        return code;
    }
    case EnumDef: {
        auto def = Match(ast, EnumDef);
        CORD code = CORD_asprintf("typedef struct %s_s %s_t;\nstruct %s_s {\nenum {", def->name, def->name, def->name);
        for (tag_ast_t *tag = def->tags; tag; tag = tag->next) {
            CORD_sprintf(&code, "%r%s__%s = %ld, ", code, def->name, tag->name, tag->value);
        }
        code = CORD_cat(code, "} tag;\nunion {\n");
        for (tag_ast_t *tag = def->tags; tag; tag = tag->next) {
            code = CORD_cat(code, "struct {\n");
            for (arg_ast_t *field = tag->fields; field; field = field->next) {
                CORD_sprintf(&code, "%r%r %s;\n", code, compile_type(field->type), field->name);
            }
            CORD_sprintf(&code, "%r} %s;\n", code, tag->name);
        }
        code = CORD_cat(code, "} __data;\n};\n");
        return code;
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
