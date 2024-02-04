
#include <ctype.h>
#include <gc/cord.h>
#include <gc.h>
#include <stdio.h>

#include "ast.h"
#include "util.h"

static CORD compile_type(type_ast_t *t)
{
    switch (t->tag) {
    case TypeVar: return Match(t, TypeVar)->var.name;
    default: errx(1, "Not implemented");
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
        CORD c = "\"";
        for (; *str; ++str) {
            switch (*str) {
            case '\\': c = CORD_cat(c, "\\\\"); break;
            case '"': c = CORD_cat(c, "\\\""); break;
            case '\a': c = CORD_cat(c, "\\a"); break;
            case '\b': c = CORD_cat(c, "\\b"); break;
            case '\n': c = CORD_cat(c, "\\n"); break;
            case '\r': c = CORD_cat(c, "\\r"); break;
            case '\t': c = CORD_cat(c, "\\t"); break;
            case '\v': c = CORD_cat(c, "\\v"); break;
            default: {
                if (isprint(*str))
                    c = CORD_cat_char(c, *str);
                else
                    CORD_sprintf(&c, "%r\\x%02X", *str);
                break;
            }
            }
        }
        return CORD_cat_char(c, '"');
    }
    case StringJoin: {
        CORD c = NULL;
        for (ast_list_t *chunk = Match(ast, StringJoin)->children; chunk; chunk = chunk->next) {
            if (c) CORD_sprintf(&c, "CORD_cat(%r, %r)", c, compile(chunk->ast));
            else c = compile(chunk->ast);
        }
        return c;
    }
    case Interp: {
        return CORD_asprintf("__cord(%r)", compile(Match(ast, Interp)->value));
    }
    case Block: {
        CORD c = NULL;
        for (ast_list_t *stmt = Match(ast, Block)->statements; stmt; stmt = stmt->next) {
            c = CORD_cat(c, compile(stmt->ast));
            c = CORD_cat(c, ";\n");
        }
        return c;
    }
    case Declare: {
        auto decl = Match(ast, Declare);
        return CORD_asprintf("auto %r = %r", decl->var, decl->value);
    }
    case Assign: {
        auto assign = Match(ast, Assign);
        CORD c = NULL;
        for (ast_list_t *target = assign->targets, *value = assign->values; target && value; target = target->next, value = value->next) {
            CORD_sprintf(&c, "%r = %r", compile(target->ast), compile(value->ast));
            if (target->next) c = CORD_cat(c, ", ");
        }
        return c;
    }
    // Min, Max,
    // Array, Table, TableEntry,
    case FunctionDef: {
        auto fndef = Match(ast, FunctionDef);
        CORD c = CORD_asprintf("%r %r(", fndef->ret_type ? compile_type(fndef->ret_type) : "void", compile(fndef->name));
        for (arg_list_t *arg = fndef->args; arg; arg = arg->next) {
            CORD_sprintf(&c, "%r%r %s", c, compile_type(arg->type), arg->var->name);
            if (arg->next) c = CORD_cat(c, ", ");
        }
        c = CORD_cat(c, ") {\n");
        c = CORD_cat(c, compile(fndef->body));
        c = CORD_cat(c, "}");
        return c;
    }
    case FunctionCall: {
        auto call = Match(ast, FunctionCall);
        CORD c = CORD_cat_char(compile(call->fn), '(');
        for (ast_list_t *arg = call->args; arg; arg = arg->next) {
            c = CORD_cat(c, compile(arg->ast));
            if (arg->next) c = CORD_cat(c, ", ");
        }
        return CORD_cat_char(c, ')');
    }
    // Lambda,
    // FunctionCall, KeywordArg,
    // Block,
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
