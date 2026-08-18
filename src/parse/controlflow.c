// Logic for parsing control flow

#include <stdbool.h>
#include <string.h>

#include "../ast.h"
#include "../util.h"
#include "context.h"
#include "controlflow.h"
#include "errors.h"
#include "expressions.h"
#include "statements.h"
#include "suffixes.h"
#include "utils.h"

ast_t *parse_block(parse_ctx_t *ctx, const char *pos) {
    const char *start = pos;
    ast_list_t *statements = NULL;
    spaces(&pos);
    if (comment(ctx, &pos)) {
        goto try_indented;
    }

    if (!indent(ctx, &pos)) {
        // Inline block
        spaces(&pos);
        while (*pos) {
            spaces(&pos);
            ast_t *stmt = optional(ctx, &pos, parse_statement);
            if (!stmt) break;
            statements = new (ast_list_t, .ast = stmt, .next = statements);
            spaces(&pos);
            if (!match(&pos, ";")) break;
        }
    } else {
        goto indented;
    }

try_indented:;
    if (indent(ctx, &pos)) {
    indented:;
        int64_t block_indent = get_indent(ctx, pos);
        whitespace(ctx, &pos);
        while (*pos) {
            ast_t *stmt = optional(ctx, &pos, parse_statement);
            if (!stmt) {
                const char *line_start = pos;
                if (match_word(&pos, "struct"))
                    parser_err(ctx, line_start, eol(pos), "Struct definitions are only allowed at the top level");
                else if (match_word(&pos, "enum"))
                    parser_err(ctx, line_start, eol(pos), "Enum definitions are only allowed at the top level");
                else if (match_word(&pos, "func"))
                    parser_err(ctx, line_start, eol(pos), "Function definitions are only allowed at the top level");
                else if (match_word(&pos, "use"))
                    parser_err(ctx, line_start, eol(pos), "'use' statements are only allowed at the top level");
                else if (match_word(&pos, "test"))
                    parser_err(ctx, line_start, eol(pos), "'test' blocks are only allowed at the top level");

                spaces(&pos);
                if (*pos && *pos != '\r' && *pos != '\n') parser_err(ctx, pos, eol(pos), "I couldn't parse this line");
                break;
            }
            statements = new (ast_list_t, .ast = stmt, .next = statements);
            whitespace(ctx, &pos);

            // Guard against having two valid statements on the same line, separated by spaces (but no newlines):
            if (!memchr(stmt->end, '\n', (size_t)(pos - stmt->end))) {
                if (*pos) parser_err(ctx, pos, eol(pos), "I don't know how to parse the rest of this line");
                pos = stmt->end;
                break;
            }

            if (get_indent(ctx, pos) != block_indent) {
                pos = stmt->end; // backtrack
                break;
            }
        }

        const char *after_comments = pos;
        while (some_of(&after_comments, " \t\r\n")) {
            if (get_indent(ctx, after_comments) == block_indent && comment(ctx, &after_comments)) {
                pos = after_comments;
            } else {
                break;
            }
        }
    }
    REVERSE_LIST(statements);
    return NewAST(ctx->file, start, pos, Block, .statements = statements);
}

ast_t *parse_pass(parse_ctx_t *ctx, const char *pos) {
    const char *start = pos;
    return match_word(&pos, "pass") ? NewAST(ctx->file, start, pos, Pass) : NULL;
}

ast_t *parse_defer(parse_ctx_t *ctx, const char *pos) {
    const char *start = pos;
    if (!match_word(&pos, "defer")) return NULL;
    spaces(&pos);
    if (match(&pos, ":")) parser_err(ctx, pos - 1, pos, "There shouldn't be a colon here.");
    ast_t *body = expect(ctx, start, &pos, parse_block, "I expected a block to be deferred here");
    return NewAST(ctx->file, start, pos, Defer, .body = body);
}

ast_t *parse_skip(parse_ctx_t *ctx, const char *pos) {
    const char *start = pos;
    const char *keyword = "skip";
    if (match_word(&pos, "continue")) {
        keyword = "continue";
    } else if (match_word(&pos, "skip")) {
        keyword = "skip";
    } else {
        return NULL;
    }
    const char *target;
    if (match_word(&pos, "for")) target = "for";
    else if (match_word(&pos, "while")) target = "while";
    else target = get_id(&pos);
    ast_t *skip = NewAST(ctx->file, start, pos, Skip, .target = target, .keyword = keyword);
    skip = parse_optional_conditional_suffix(ctx, skip);
    return skip;
}

ast_t *parse_stop(parse_ctx_t *ctx, const char *pos) {
    const char *start = pos;
    const char *keyword = "stop";
    if (match_word(&pos, "stop")) {
        keyword = "stop";
    } else if (match_word(&pos, "break")) {
        keyword = "break";
    } else {
        return NULL;
    }
    const char *target;
    if (match_word(&pos, "for")) target = "for";
    else if (match_word(&pos, "while")) target = "while";
    else target = get_id(&pos);
    ast_t *stop = NewAST(ctx->file, start, pos, Stop, .target = target, .keyword = keyword);
    stop = parse_optional_conditional_suffix(ctx, stop);
    return stop;
}

ast_t *parse_return(parse_ctx_t *ctx, const char *pos) {
    const char *start = pos;
    if (!match_word(&pos, "return")) return NULL;
    ast_t *value = optional(ctx, &pos, parse_expr);
    ast_t *ret = NewAST(ctx->file, start, pos, Return, .value = value);
    ret = parse_optional_conditional_suffix(ctx, ret);
    return ret;
}

ast_t *parse_do(parse_ctx_t *ctx, const char *pos) {
    // do [<indent>] body
    const char *start = pos;
    if (!match_word(&pos, "do")) return NULL;
    spaces(&pos);
    if (match(&pos, ":")) parser_err(ctx, pos - 1, pos, "There shouldn't be a colon here.");
    ast_t *body = expect(ctx, start, &pos, parse_block, "I expected a body for this 'do'");
    return NewAST(ctx->file, start, pos, Block, .statements = Match(body, Block)->statements);
}

ast_t *parse_while(parse_ctx_t *ctx, const char *pos) {
    // while condition ["do"] [<indent>] body
    const char *start = pos;
    if (!match_word(&pos, "while")) return NULL;
    ast_t *condition = expect(ctx, start, &pos, parse_expr, "I don't see a viable condition for this 'while'");
    spaces(&pos);
    if (match(&pos, ":")) parser_err(ctx, pos - 1, pos, "There shouldn't be a colon here.");
    (void)match_word(&pos, "do"); // Optional 'do'
    ast_t *body = expect(ctx, start, &pos, parse_block, "I expected a body for this 'while'");
    return NewAST(ctx->file, start, pos, While, .condition = condition, .body = body);
}

ast_t *parse_repeat(parse_ctx_t *ctx, const char *pos) {
    // repeat [<indent>] body
    const char *start = pos;
    if (!match_word(&pos, "repeat")) return NULL;
    spaces(&pos);
    if (match(&pos, ":")) parser_err(ctx, pos - 1, pos, "There shouldn't be a colon here.");
    ast_t *body = expect(ctx, start, &pos, parse_block, "I expected a body for this 'repeat'");
    return NewAST(ctx->file, start, pos, Repeat, .body = body);
}

ast_t *parse_if(parse_ctx_t *ctx, const char *pos) {
    // "if" <condition> ["then"] <body> ["else" <body>] | "unless" <condition> <body> ["else" <body>]
    const char *start = pos;
    int64_t starting_indent = get_indent(ctx, pos);

    bool unless;
    if (match_word(&pos, "if")) unless = false;
    else if (match_word(&pos, "unless")) unless = true;
    else return NULL;

    ast_t *condition = unless ? NULL : optional(ctx, &pos, parse_declaration);
    if (!condition) condition = expect(ctx, start, &pos, parse_expr, "I expected to find a condition for this 'if'");

    if (unless) condition = WrapAST(condition, Not, condition);

    spaces(&pos);
    if (match(&pos, ":")) parser_err(ctx, pos - 1, pos, "There shouldn't be a colon here.");

    (void)match_word(&pos, "then"); // Optional 'then'
    ast_t *body = expect(ctx, start, &pos, parse_block, "I expected a body for this 'if' statement");

    const char *tmp = pos;
    whitespace(ctx, &tmp);
    ast_t *else_body = NULL;
    const char *else_start = pos;
    if (get_indent(ctx, tmp) == starting_indent && match_word(&tmp, "else")) {
        pos = tmp;
        spaces(&pos);
        if (match(&pos, ":")) parser_err(ctx, pos - 1, pos, "There shouldn't be a colon here.");
        else_body = optional(ctx, &pos, parse_if);
        if (!else_body) else_body = expect(ctx, else_start, &pos, parse_block, "I expected a body for this 'else'");
    }
    return NewAST(ctx->file, start, pos, If, .condition = condition, .body = body, .else_body = else_body);
}

ast_t *parse_when(parse_ctx_t *ctx, const char *pos) {
    // when <expr> (is var : Tag <body>)* [else <body>]
    const char *start = pos;
    int64_t starting_indent = get_indent(ctx, pos);

    if (!match_word(&pos, "when")) return NULL;

    ast_t *subject = optional(ctx, &pos, parse_declaration);
    if (!subject) subject = expect(ctx, start, &pos, parse_expr, "I expected to find an expression for this 'when'");

    when_clause_t *clauses = NULL;
    const char *tmp = pos;
    whitespace(ctx, &tmp);
    while (get_indent(ctx, tmp) == starting_indent && match_word(&tmp, "is")) {
        pos = tmp;
        spaces(&pos);
        if (match(&pos, ":")) parser_err(ctx, pos - 1, pos, "There shouldn't be a colon here.");
        ast_t *pattern = expect(ctx, start, &pos, parse_expr, "I expected a pattern to match here");
        spaces(&pos);
        when_clause_t *new_clauses = new (when_clause_t, .pattern = pattern, .next = clauses);
        while (match(&pos, ",")) {
            pattern = expect(ctx, start, &pos, parse_expr, "I expected a pattern to match here");
            new_clauses = new (when_clause_t, .pattern = pattern, .next = new_clauses);
            spaces(&pos);
        }
        (void)match_word(&pos, "then"); // Optional 'then'
        ast_t *body = expect(ctx, start, &pos, parse_block, "I expected a body for this 'when' clause");
        for (when_clause_t *c = new_clauses; c && c != clauses; c = c->next) {
            c->body = body;
        }
        clauses = new_clauses;
        tmp = pos;
        whitespace(ctx, &tmp);
    }
    REVERSE_LIST(clauses);

    ast_t *else_body = NULL;
    const char *else_start = pos;
    if (get_indent(ctx, tmp) == starting_indent && match_word(&tmp, "else")) {
        pos = tmp;
        else_body = expect(ctx, else_start, &pos, parse_block, "I expected a body for this 'else'");
    }
    return NewAST(ctx->file, start, pos, When, .subject = subject, .clauses = clauses, .else_body = else_body);
}

ast_t *parse_for(parse_ctx_t *ctx, const char *pos) {
    // for [k,] v in iter [<indent>] body
    const char *start = pos;
    if (!match_word(&pos, "for")) return NULL;
    int64_t starting_indent = get_indent(ctx, pos);
    spaces(&pos);
    ast_list_t *vars = NULL;
    for (;;) {
        spaces(&pos);
        const char *var_start = pos;
        if (match(&pos, "&")) {
            // By-reference iteration variable: `for &x in xs` yields `&x` pointers
            // into the list so the loop can update elements in place.
            spaces(&pos);
            ast_t *var = expect(ctx, var_start, &pos, parse_var, "I expected a variable name after this '&'");
            vars = new (ast_list_t, .ast = NewAST(ctx->file, var_start, var->end, StackReference, .value = var),
                        .next = vars);
        } else {
            ast_t *var = optional(ctx, &pos, parse_var);
            if (var) vars = new (ast_list_t, .ast = var, .next = vars);
        }

        spaces(&pos);
        if (!match(&pos, ",")) break;
    }

    spaces(&pos);
    // Optional `at i` binds an Int64 iteration counter: `for x at i in xs`
    ast_t *at_var = NULL;
    if (match_word(&pos, "at")) {
        spaces(&pos);
        at_var = expect(ctx, start, &pos, parse_var, "I expected a variable name after 'at'");
        spaces(&pos);
    }
    expect_str(ctx, start, &pos, "in", "I expected an 'in' for this 'for'");

    // One or more comma-separated iterables (`for x, y in xs, ys` iterates in lockstep):
    ast_list_t *iters = NULL;
    for (;;) {
        ast_t *iter = expect(ctx, start, &pos, parse_expr, "I expected an iterable value for this 'for'");
        iters = new (ast_list_t, .ast = iter, .next = iters);
        spaces(&pos);
        if (!match(&pos, ",")) break;
        spaces(&pos);
    }
    REVERSE_LIST(iters);

    spaces(&pos);
    if (match(&pos, ":")) parser_err(ctx, pos - 1, pos, "There shouldn't be a colon here.");
    (void)match_word(&pos, "do"); // Optional 'do'

    ast_t *body = expect(ctx, start, &pos, parse_block, "I expected a body for this 'for'");

    const char *else_start = pos;
    whitespace(ctx, &else_start);
    ast_t *empty = NULL;
    if (match_word(&else_start, "else") && get_indent(ctx, else_start) == starting_indent) {
        pos = else_start;
        spaces(&pos);
        if (match(&pos, ":")) parser_err(ctx, pos - 1, pos, "There shouldn't be a colon here.");
        empty = expect(ctx, pos, &pos, parse_block, "I expected a body for this 'else'");
    }
    REVERSE_LIST(vars);
    return NewAST(ctx->file, start, pos, For, .vars = vars, .at = at_var, .iters = iters, .body = body, .empty = empty);
}
