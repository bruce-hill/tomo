
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
    case TypeVar: return CORD_cat(Match(t, TypeVar)->var.name, "_t");
    default: errx(1, "Not implemented");
    }
}

static inline CORD compile_statement(ast_t *ast)
{
    CORD code = compile(ast);
    switch (ast->tag) {
    case If: case For: case While: case FunctionDef:
        return code;
    default:
        return CORD_cat(code, ";");
    }
}

CORD compile(ast_t *ast)
{
    switch (ast->tag) {
    case Nil: return "NULL";
    case Bool: return Match(ast, Bool)->b ? "true" : "false";
    case Var: return Match(ast, Var)->var.name;
    case Int: return CORD_asprintf("((Int%ld_t)%ld)", Match(ast, Int)->precision, Match(ast, Int)->i);
    case Num: return CORD_asprintf(Match(ast, Num)->precision == 64 ? "%g" : "%gf", Match(ast, Num)->n);
    case Char: return CORD_asprintf("'\\x%02X'", (int)Match(ast, Char)->c);
    case UnaryOp: {
        auto unop = Match(ast, UnaryOp);
        CORD expr = compile(unop->value);
        switch (unop->op) {
        case UNOP_NOT: return CORD_cat("!", expr);
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
        case BINOP_MOD: return CORD_asprintf("(%r %% %r)", lhs, rhs);
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
        case BINOP_AND: return CORD_asprintf("(%r && %r)", lhs, rhs);
        case BINOP_OR: return CORD_asprintf("(%r || %r)", lhs, rhs);
        default: break;
        }
        errx(1, "unimplemented binop");
    }
    case UpdateAssign: {
        auto update = Match(ast, UpdateAssign);
        CORD lhs = compile(update->lhs);
        CORD rhs = compile(update->rhs);
        switch (update->op) {
        case BINOP_MULT: return CORD_asprintf("%r *= %r", lhs, rhs);
        case BINOP_DIVIDE: return CORD_asprintf("%r /= %r", lhs, rhs);
        case BINOP_MOD: return CORD_asprintf("%r = %r %% %r", lhs, lhs, rhs);
        case BINOP_PLUS: return CORD_asprintf("%r += %r", lhs, rhs);
        case BINOP_MINUS: return CORD_asprintf("%r -= %r", lhs, rhs);
        case BINOP_LSHIFT: return CORD_asprintf("%r <<= %r", lhs, rhs);
        case BINOP_RSHIFT: return CORD_asprintf("%r >>= %r", lhs, rhs);
        case BINOP_EQ: return CORD_asprintf("%r = (%r == %r)", lhs, lhs, rhs);
        case BINOP_NE: return CORD_asprintf("%r = (%r != %r)", lhs, lhs, rhs);
        case BINOP_LT: return CORD_asprintf("%r = (%r < %r)", lhs, lhs, rhs);
        case BINOP_LE: return CORD_asprintf("%r = (%r <= %r)", lhs, lhs, rhs);
        case BINOP_GT: return CORD_asprintf("%r = (%r > %r)", lhs, lhs, rhs);
        case BINOP_GE: return CORD_asprintf("%r = (%r >= %r)", lhs, lhs, rhs);
        case BINOP_AND: return CORD_asprintf("%r = (%r && %r)", lhs, lhs, rhs);
        case BINOP_OR: return CORD_asprintf("%r = (%r || %r)", lhs, lhs, rhs);
        default: break;
        }
        errx(1, "unimplemented binop");
    }
    case StringLiteral: {
        const char *str = Match(ast, StringLiteral)->str; 
        CORD code = "\"";
        for (; *str; ++str) {
            switch (*str) {
            case '\\': code = CORD_cat(code, "\\\\"); break;
            case '"': code = CORD_cat(code, "\\\""); break;
            case '\a': code = CORD_cat(code, "\\a"); break;
            case '\b': code = CORD_cat(code, "\\b"); break;
            case '\n': code = CORD_cat(code, "\\n"); break;
            case '\r': code = CORD_cat(code, "\\r"); break;
            case '\t': code = CORD_cat(code, "\\t"); break;
            case '\v': code = CORD_cat(code, "\\v"); break;
            default: {
                if (isprint(*str))
                    code = CORD_cat_char(code, *str);
                else
                    CORD_sprintf(&code, "%r\\x%02X", *str);
                break;
            }
            }
        }
        return CORD_cat_char(code, '"');
    }
    case StringJoin: {
        CORD code = NULL;
        for (ast_list_t *chunk = Match(ast, StringJoin)->children; chunk; chunk = chunk->next) {
            if (code) CORD_sprintf(&code, "CORD_cat(%r, %r)", code, compile(chunk->ast));
            else code = compile(chunk->ast);
        }
        return code;
    }
    case Interp: {
        return CORD_asprintf("__cord(%r)", compile(Match(ast, Interp)->value));
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
        CORD code = NULL;
        for (ast_list_t *target = assign->targets, *value = assign->values; target && value; target = target->next, value = value->next) {
            CORD_sprintf(&code, "%r = %r", compile(target->ast), compile(value->ast));
            if (target->next) code = CORD_cat(code, ", ");
        }
        return code;
    }
    // Min, Max,
    // Array, Table, TableEntry,
    case FunctionDef: {
        auto fndef = Match(ast, FunctionDef);
        CORD code = CORD_asprintf("%r %r(", fndef->ret_type ? compile_type(fndef->ret_type) : "void", compile(fndef->name));
        for (arg_list_t *arg = fndef->args; arg; arg = arg->next) {
            CORD_sprintf(&code, "%r%r %s", code, compile_type(arg->type), arg->var.name);
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
    // For, While, If,
    // Reduction,
    // Skip, Stop, Pass,
    // Return,
    // Extern,
    // TypeDef,
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
