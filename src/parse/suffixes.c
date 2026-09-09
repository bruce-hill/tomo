// Logic for parsing various suffixes that can go after an expression

#include <stdbool.h>

#include "../ast.h"
#include "../formatter/utils.h"
#include "../stdlib/print.h"
#include "../stdlib/text.h"
#include "../util.h"
#include "context.h"
#include "errors.h"
#include "expressions.h"
#include "utils.h"

ast_t *parse_field_suffix(parse_ctx_t *ctx, ast_t *lhs) {
    if (!lhs) return NULL;
    const char *pos = lhs->end;
    whitespace(ctx, &pos);
    if (!match(&pos, ".")) return NULL;
    if (*pos == '.') return NULL;
    whitespace(ctx, &pos);
    bool dollar = match(&pos, "$");
    const char *field = get_id(&pos);
    if (!field) return NULL;
    if (dollar) field = String("$", field);
    return NewAST(ctx->file, lhs->start, pos, FieldAccess, .fielded = lhs, .field = field);
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
    whitespace(ctx, &pos);
    ast_t *index = optional(ctx, &pos, parse_extended_expr);
    whitespace(ctx, &pos);
    expect_closing(ctx, &pos, "]", "I wasn't able to parse the rest of this index");
    return NewAST(ctx->file, start, pos, Index, .indexed = lhs, .index = index);
}

ast_t *parse_comprehension_suffix(parse_ctx_t *ctx, ast_t *expr) {
    // <expr> "for" [<index>,]<var> "in" <iter> ["if" <condition> | "unless" <condition>]
    if (!expr) return NULL;
    const char *start = expr->start;
    const char *pos = expr->end;
    whitespace(ctx, &pos);
    if (!match_word(&pos, "for")) return NULL;

    ast_list_t *vars = NULL;
    for (;;) {
        ast_t *var = optional(ctx, &pos, parse_var);
        if (var) vars = new (ast_list_t, .ast = var, .next = vars);

        spaces(&pos);
        if (!match(&pos, ",")) break;
    }
    REVERSE_LIST(vars);

    // Optional `at i` binds an Int64 iteration counter: `[... for x at i in xs]`
    ast_t *at_var = NULL;
    if (match_word(&pos, "at")) {
        spaces(&pos);
        at_var = expect(ctx, start, &pos, parse_var, "I expected a variable name after 'at'");
        spaces(&pos);
    }
    expect_str(ctx, start, &pos, "in", "I expected an 'in' for this 'for'");
    // One or more comma-separated iterables (`... for x, y in xs, ys` iterates in lockstep).
    // NOTE: the comma is parsed greedily, so in a container literal like
    // `[x for x in xs, 99]`, the `99` is treated as another iterable (an arity
    // error) rather than another list item; put plain items before the
    // comprehension instead.
    ast_list_t *iters = NULL;
    for (;;) {
        ast_t *iter = expect(ctx, start, &pos, parse_expr, "I expected an iterable value for this 'for'");
        iters = new (ast_list_t, .ast = iter, .next = iters);
        spaces(&pos);
        if (!match(&pos, ",")) break;
        spaces(&pos);
    }
    REVERSE_LIST(iters);
    const char *next_pos = pos;
    whitespace(ctx, &next_pos);
    ast_t *filter = NULL;
    if (match_word(&next_pos, "if")) {
        pos = next_pos;
        filter = expect(ctx, pos - 2, &pos, parse_expr, "I expected a condition for this 'if'");
    } else if (match_word(&next_pos, "unless")) {
        pos = next_pos;
        filter = expect(ctx, pos - 2, &pos, parse_expr, "I expected a condition for this 'unless'");
        filter = WrapAST(filter, Not, filter);
    }
    return NewAST(ctx->file, start, pos, Comprehension, .expr = expr, .vars = vars, .at = at_var, .iters = iters,
                  .filter = filter);
}

ast_t *parse_optional_conditional_suffix(parse_ctx_t *ctx, ast_t *stmt) {
    // <statement> "if" <condition> | <statement> "unless" <condition>
    if (!stmt) return stmt;
    const char *start = stmt->start;
    const char *pos = stmt->end;
    if (match_word(&pos, "if")) {
        ast_t *condition = expect(ctx, pos - 2, &pos, parse_expr, "I expected a condition for this 'if'");
        return NewAST(ctx->file, start, pos, If, .condition = condition, .body = stmt, .postfix = true);
    } else if (match_word(&pos, "unless")) {
        ast_t *condition = expect(ctx, pos - 2, &pos, parse_expr, "I expected a condition for this 'unless'");
        condition = WrapAST(condition, Not, condition);
        return NewAST(ctx->file, start, pos, If, .condition = condition, .body = stmt, .postfix = true);
    } else {
        return stmt;
    }
}

// The arguments between a call's or a record literal's delimiters, from just
// after the opening one to just before the closing one. All three forms parse
// them the same way, comments included: a comment written among the arguments
// has nowhere to live but the argument it precedes, and an argument that keeps
// no record of one is an argument whose comment the formatter deletes.
static arg_ast_t *parse_call_args(parse_ctx_t *ctx, const char **pos, const char *missing_arg) {
    // Taken before whitespace() runs, since that is what steps over comments.
    const char *scan = *pos;
    whitespace(ctx, pos);

    arg_ast_t *args = NULL;
    for (;;) {
        const char *arg_start = *pos;
        const char *name = get_id(pos);
        whitespace(ctx, pos);
        // A single '=' names an argument, but `==` is a comparison: `f(x == y)`
        // passes one boolean, it doesn't name an argument `x`.
        if (!name || !match(pos, "=") || **pos == '=') {
            name = NULL;
            *pos = arg_start;
        }

        // The gap since the last argument splits at the end of the line that
        // one finished on: what was written there trails it, and only what is
        // written below leads this one.
        const char *split = scan;
        while (split < arg_start && *split != '\n')
            split++;
        if (args != NULL) args->comments_end = split;
        const char *leading = args != NULL ? split : scan;

        const char *text = leading;
        Text_t arg_comments = collect_comments(ctx, &text, arg_start);

        ast_t *arg = optional(ctx, pos, parse_expr);
        if (!arg) {
            if (name) parser_err(ctx, arg_start, *pos, missing_arg);
            break;
        }
        args =
            new (arg_ast_t, .file = ctx->file, .start = arg_start, .end = *pos, .name = name, .comment = arg_comments,
                 .comments_start = leading, .comments_end = *pos, .value = arg, .next = args);
        scan = *pos;
        if (!match_separator(ctx, pos)) break;
    }

    whitespace(ctx, pos);
    // The last argument takes whatever is left before the closing delimiter:
    // no argument follows it, so nothing else would. This runs before the
    // reversal, while `args` is still that last argument.
    if (args != NULL) args->comments_end = *pos;
    REVERSE_LIST(args);
    return args;
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

    arg_ast_t *args = parse_call_args(ctx, &pos, "I expected an argument here");

    if (!match(&pos, ")")) parser_err(ctx, start, pos, "This parenthesis is unclosed");

    return NewAST(ctx->file, start, pos, MethodCall, .self = self, .name = fn, .args = args);
}

ast_t *parse_record_literal_suffix(parse_ctx_t *ctx, ast_t *type) {
    // `Foo{...}` / `Baz.A{...}`: field-wise construction of a struct or enum
    // variant. The '{' must follow the type name immediately (no space), so a
    // table/set literal in an adjacent position is never swallowed.
    if (!type) return NULL;
    if (type->tag != Var && type->tag != FieldAccess) return NULL;

    const char *start = type->start;
    const char *pos = type->end;

    if (!match(&pos, "{")) return NULL;

    arg_ast_t *args = parse_call_args(ctx, &pos, "I expected a field value here");

    if (!match(&pos, "}")) parser_err(ctx, start, pos, "This curly brace is unclosed");

    return NewAST(ctx->file, start, pos, RecordLiteral, .type = type, .args = args);
}

ast_t *parse_fncall_suffix(parse_ctx_t *ctx, ast_t *fn) {
    if (!fn) return NULL;

    const char *start = fn->start;
    const char *pos = fn->end;

    if (!match(&pos, "(")) return NULL;

    arg_ast_t *args = parse_call_args(ctx, &pos, "I expected an argument here");

    if (!match(&pos, ")")) parser_err(ctx, start, pos, "This parenthesis is unclosed");

    return NewAST(ctx->file, start, pos, FunctionCall, .fn = fn, .args = args);
}
