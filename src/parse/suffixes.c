// Logic for parsing various suffixes that can go after an expression

#include <stdbool.h>
#include <string.h>

#include "../ast.h"
#include "../stdlib/print.h"
#include "../stdlib/util.h"
#include "context.h"
#include "errors.h"
#include "expressions.h"
#include "utils.h"

ast_t *parse_field_suffix(parse_ctx_t *ctx, ast_t *lhs) {
    if (!lhs) return NULL;
    const char *pos = lhs->end;
    whitespace(&pos);
    if (!match(&pos, ".")) return NULL;
    if (*pos == '.') return NULL;
    whitespace(&pos);
    bool dollar = match(&pos, "$");
    const char *field = get_id(&pos);
    if (!field) return NULL;
    if (dollar) field = String("$", field);
    return NewAST(ctx->file, lhs->start, pos, FieldAccess, .fielded = lhs, .field = field);
}

ast_t *parse_optional_suffix(parse_ctx_t *ctx, ast_t *lhs) {
    if (!lhs) return NULL;
    const char *pos = lhs->end;
    if (match(&pos, "?")) return NewAST(ctx->file, lhs->start, pos, Optional, .value = lhs);
    else return NULL;
}

ast_t *parse_non_optional_suffix(parse_ctx_t *ctx, ast_t *lhs) {
    if (!lhs) return NULL;
    const char *pos = lhs->end;
    if (match(&pos, "!")) return NewAST(ctx->file, lhs->start, pos, NonOptional, .value = lhs);
    else return NULL;
}

ast_t *parse_index_suffix(parse_ctx_t *ctx, ast_t *lhs) {
    if (!lhs) return NULL;
    const char *start = lhs->start;
    const char *pos = lhs->end;
    if (!match(&pos, "[")) return NULL;
    whitespace(&pos);
    ast_t *index = optional(ctx, &pos, parse_extended_expr);
    whitespace(&pos);
    bool unchecked = match(&pos, ";") && (spaces(&pos), match_word(&pos, "unchecked") != 0);
    expect_closing(ctx, &pos, "]", "I wasn't able to parse the rest of this index");
    return NewAST(ctx->file, start, pos, Index, .indexed = lhs, .index = index, .unchecked = unchecked);
}

ast_t *parse_comprehension_suffix(parse_ctx_t *ctx, ast_t *expr) {
    // <expr> "for" [<index>,]<var> "in" <iter> ["if" <condition> | "unless" <condition>]
    if (!expr) return NULL;
    const char *start = expr->start;
    const char *pos = expr->end;
    whitespace(&pos);
    if (!match_word(&pos, "for")) return NULL;

    ast_list_t *vars = NULL;
    for (;;) {
        ast_t *var = optional(ctx, &pos, parse_var);
        if (var) vars = new (ast_list_t, .ast = var, .next = vars);

        spaces(&pos);
        if (!match(&pos, ",")) break;
    }
    REVERSE_LIST(vars);

    expect_str(ctx, start, &pos, "in", "I expected an 'in' for this 'for'");
    ast_t *iter = expect(ctx, start, &pos, parse_expr, "I expected an iterable value for this 'for'");
    const char *next_pos = pos;
    whitespace(&next_pos);
    ast_t *filter = NULL;
    if (match_word(&next_pos, "if")) {
        pos = next_pos;
        filter = expect(ctx, pos - 2, &pos, parse_expr, "I expected a condition for this 'if'");
    } else if (match_word(&next_pos, "unless")) {
        pos = next_pos;
        filter = expect(ctx, pos - 2, &pos, parse_expr, "I expected a condition for this 'unless'");
        filter = WrapAST(filter, Not, filter);
    }
    return NewAST(ctx->file, start, pos, Comprehension, .expr = expr, .vars = vars, .iter = iter, .filter = filter);
}

ast_t *parse_optional_conditional_suffix(parse_ctx_t *ctx, ast_t *stmt) {
    // <statement> "if" <condition> | <statement> "unless" <condition>
    if (!stmt) return stmt;
    const char *start = stmt->start;
    const char *pos = stmt->end;
    if (match_word(&pos, "if")) {
        ast_t *condition = expect(ctx, pos - 2, &pos, parse_expr, "I expected a condition for this 'if'");
        return NewAST(ctx->file, start, pos, If, .condition = condition, .body = stmt);
    } else if (match_word(&pos, "unless")) {
        ast_t *condition = expect(ctx, pos - 2, &pos, parse_expr, "I expected a condition for this 'unless'");
        condition = WrapAST(condition, Not, condition);
        return NewAST(ctx->file, start, pos, If, .condition = condition, .body = stmt);
    } else {
        return stmt;
    }
}

ast_t *parse_method_call_suffix(parse_ctx_t *ctx, ast_t *self) {
    if (!self) return NULL;

    const char *start = self->start;
    const char *pos = self->end;

    if (!match(&pos, ".")) return NULL;
    if (*pos == ' ') return NULL;
    const char *fn = get_id(&pos);
    if (!fn) return NULL;
    spaces(&pos);
    if (!match(&pos, "(")) return NULL;
    whitespace(&pos);

    arg_ast_t *args = NULL;
    for (;;) {
        const char *arg_start = pos;
        const char *name = get_id(&pos);
        whitespace(&pos);
        if (!name || !match(&pos, "=")) {
            name = NULL;
            pos = arg_start;
        }

        ast_t *arg = optional(ctx, &pos, parse_expr);
        if (!arg) {
            if (name) parser_err(ctx, arg_start, pos, "I expected an argument here");
            break;
        }
        args = new (arg_ast_t, .name = name, .value = arg, .next = args);
        if (!match_separator(&pos)) break;
    }
    REVERSE_LIST(args);

    whitespace(&pos);

    if (!match(&pos, ")")) parser_err(ctx, start, pos, "This parenthesis is unclosed");

    return NewAST(ctx->file, start, pos, MethodCall, .self = self, .name = fn, .args = args);
}

ast_t *parse_fncall_suffix(parse_ctx_t *ctx, ast_t *fn) {
    if (!fn) return NULL;

    const char *start = fn->start;
    const char *pos = fn->end;

    if (!match(&pos, "(")) return NULL;

    whitespace(&pos);

    arg_ast_t *args = NULL;
    for (;;) {
        const char *arg_start = pos;
        const char *name = get_id(&pos);
        whitespace(&pos);
        if (!name || !match(&pos, "=")) {
            name = NULL;
            pos = arg_start;
        }

        ast_t *arg = optional(ctx, &pos, parse_expr);
        if (!arg) {
            if (name) parser_err(ctx, arg_start, pos, "I expected an argument here");
            break;
        }
        args = new (arg_ast_t, .name = name, .value = arg, .next = args);
        if (!match_separator(&pos)) break;
    }

    whitespace(&pos);

    if (!match(&pos, ")")) parser_err(ctx, start, pos, "This parenthesis is unclosed");

    REVERSE_LIST(args);
    return NewAST(ctx->file, start, pos, FunctionCall, .fn = fn, .args = args);
}
