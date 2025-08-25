// Logic for parsing container types (lists, sets, tables)

#include <stdarg.h>
#include <stdbool.h>
#include <string.h>

#include "../ast.h"
#include "../stdlib/util.h"
#include "context.h"
#include "errors.h"
#include "expressions.h"
#include "suffixes.h"
#include "utils.h"

ast_t *parse_list(parse_ctx_t *ctx, const char *pos) {
    const char *start = pos;
    if (!match(&pos, "[")) return NULL;

    whitespace(ctx, &pos);

    ast_list_t *items = NULL;
    for (;;) {
        ast_t *item = optional(ctx, &pos, parse_extended_expr);
        if (!item) break;
        ast_t *suffixed = parse_comprehension_suffix(ctx, item);
        while (suffixed) {
            item = suffixed;
            pos = suffixed->end;
            suffixed = parse_comprehension_suffix(ctx, item);
        }
        items = new (ast_list_t, .ast = item, .next = items);
        if (!match_separator(ctx, &pos)) break;
    }
    whitespace(ctx, &pos);
    expect_closing(ctx, &pos, "]", "I wasn't able to parse the rest of this list");

    REVERSE_LIST(items);
    return NewAST(ctx->file, start, pos, List, .items = items);
}

ast_t *parse_table(parse_ctx_t *ctx, const char *pos) {
    const char *start = pos;
    if (!match(&pos, "{")) return NULL;

    whitespace(ctx, &pos);

    ast_list_t *entries = NULL;
    for (;;) {
        const char *entry_start = pos;
        ast_t *key = optional(ctx, &pos, parse_extended_expr);
        if (!key) break;
        whitespace(ctx, &pos);
        if (!match(&pos, "=")) return NULL;
        ast_t *value = expect(ctx, pos - 1, &pos, parse_expr, "I couldn't parse the value for this table entry");
        ast_t *entry = NewAST(ctx->file, entry_start, pos, TableEntry, .key = key, .value = value);
        ast_t *suffixed = parse_comprehension_suffix(ctx, entry);
        while (suffixed) {
            entry = suffixed;
            pos = suffixed->end;
            suffixed = parse_comprehension_suffix(ctx, entry);
        }
        entries = new (ast_list_t, .ast = entry, .next = entries);
        if (!match_separator(ctx, &pos)) break;
    }

    REVERSE_LIST(entries);

    whitespace(ctx, &pos);

    ast_t *fallback = NULL, *default_value = NULL;
    if (match(&pos, ";")) {
        for (;;) {
            whitespace(ctx, &pos);
            const char *attr_start = pos;
            if (match_word(&pos, "fallback")) {
                whitespace(ctx, &pos);
                if (!match(&pos, "=")) parser_err(ctx, attr_start, pos, "I expected an '=' after 'fallback'");
                if (fallback) parser_err(ctx, attr_start, pos, "This table already has a fallback");
                fallback = expect(ctx, attr_start, &pos, parse_expr, "I expected a fallback table");
            } else if (match_word(&pos, "default")) {
                whitespace(ctx, &pos);
                if (!match(&pos, "=")) parser_err(ctx, attr_start, pos, "I expected an '=' after 'default'");
                if (default_value) parser_err(ctx, attr_start, pos, "This table already has a default");
                default_value = expect(ctx, attr_start, &pos, parse_expr, "I expected a default value");
            } else {
                break;
            }
            whitespace(ctx, &pos);
            if (!match(&pos, ",")) break;
        }
    }

    whitespace(ctx, &pos);
    expect_closing(ctx, &pos, "}", "I wasn't able to parse the rest of this table");

    return NewAST(ctx->file, start, pos, Table, .default_value = default_value, .entries = entries,
                  .fallback = fallback);
}

ast_t *parse_set(parse_ctx_t *ctx, const char *pos) {
    const char *start = pos;
    if (match(&pos, "||")) return NewAST(ctx->file, start, pos, Set);

    if (!match(&pos, "|")) return NULL;
    whitespace(ctx, &pos);

    ast_list_t *items = NULL;
    for (;;) {
        ast_t *item = optional(ctx, &pos, parse_extended_expr);
        if (!item) break;
        whitespace(ctx, &pos);
        ast_t *suffixed = parse_comprehension_suffix(ctx, item);
        while (suffixed) {
            item = suffixed;
            pos = suffixed->end;
            suffixed = parse_comprehension_suffix(ctx, item);
        }
        items = new (ast_list_t, .ast = item, .next = items);
        if (!match_separator(ctx, &pos)) break;
    }

    REVERSE_LIST(items);

    whitespace(ctx, &pos);
    expect_closing(ctx, &pos, "|", "I wasn't able to parse the rest of this set");

    return NewAST(ctx->file, start, pos, Set, .items = items);
}
