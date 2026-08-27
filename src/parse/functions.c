// Logic for parsing functions

#include <gc.h>
#include <stdarg.h>
#include <stdbool.h>
#include <string.h>

#include <unictype.h>
#include <uniname.h>

#include "../ast.h"
#include "../formatter/utils.h"
#include "../stdlib/datatypes.h"
#include "../stdlib/text.h"
#include "../util.h"
#include "context.h"
#include "controlflow.h"
#include "errors.h"
#include "expressions.h"
#include "functions.h"
#include "types.h"
#include "utils.h"

arg_ast_t *parse_args(parse_ctx_t *ctx, const char **pos) {
    const char *comment_start = *pos;
    arg_ast_t *args = NULL;
    for (;;) {
        const char *batch_start = *pos;
        ast_t *default_val = NULL;
        type_ast_t *type = NULL;

        typedef struct name_list_s {
            const char *start, *end;
            const char *name, *alias;
            Text_t comment;
            struct name_list_s *next;
        } name_list_t;

        name_list_t *names = NULL;
        for (;;) {
            whitespace(ctx, pos);
            const char *name = get_id(pos);
            if (!name) break;
            const char *name_start = *pos;
            whitespace(ctx, pos);

            const char *alias = NULL;
            if (match(pos, "|")) {
                whitespace(ctx, pos);
                alias = get_id(pos);
                if (!alias) parser_err(ctx, *pos, *pos, "I expected an argument alias after `|`");
            }

            Text_t comments = EMPTY_TEXT;
            for (OptionalText_t com;
                 (com = next_comment(ctx->comments, &comment_start, name_start)).tag != TEXT_NONE;) {
                if (comments.length > 0) comments = Texts(comments, " ");
                comments = Texts(comments, Text$trim(Text$without_prefix(com, Text("#")), Text(" \t"), true, true));
            }

            if (match(pos, ":")) {
                type = expect(ctx, *pos, pos, parse_type, "I expected a type here");
                // Only same-line whitespace here, not a newline: a bare `field:Type`
                // followed by a newline should end this field so the newline can act
                // as a separator before the next one (see match_separator() below).
                spaces(pos);
                if (match(pos, "=")) default_val = expect(ctx, *pos, pos, parse_term, "I expected a value here");
                names = new (name_list_t, .start = name_start, .end = *pos, .name = name, .alias = alias, .next = names,
                             .comment = comments);
                break;
            } else if (strncmp(*pos, "==", 2) != 0 && match(pos, "=")) {
                default_val = expect(ctx, *pos, pos, parse_term, "I expected a value here");
                names = new (name_list_t, .start = name_start, .end = *pos, .name = name, .alias = alias, .next = names,
                             .comment = comments);
                break;
            } else if (name) {
                names = new (name_list_t, .start = name_start, .end = *pos, .name = name, .alias = alias, .next = names,
                             .comment = comments);
                spaces(pos);
                if (!match(pos, ",")) break;
            } else {
                break;
            }
        }
        if (!names) break;
        if (!default_val && !type)
            parser_err(ctx, batch_start, *pos,
                       "I expected a ':' and type, or '=' and a default value after this parameter (", names->name,
                       ")");

        REVERSE_LIST(names);
        for (; names; names = names->next)
            args = new (arg_ast_t, .start = names->start, .end = names->end, .name = names->name, .alias = names->alias,
                        .comment = names->comment, .type = type, .value = default_val, .next = args);

        if (!match_separator(ctx, pos)) break;
    }

    REVERSE_LIST(args);
    return args;
}

static void catch_common_errors_after_parens(parse_ctx_t *ctx, const char *start, const char *pos, type_ast_t *ret_type,
                                             bool is_lambda) {
    const char *closing_paren = pos - 1;
    spaces(&pos);

    if (match(&pos, ":")) {
        const char *colon = pos - 1;
        if (!is_lambda && ret_type == NULL && (ret_type = optional(ctx, &pos, parse_type)) != NULL)
            parser_err(ctx, colon, pos, "Function return types go inside the function parentheses like this: ",
                       string_slice(start, (size_t)(closing_paren - start)), " -> ",
                       string_slice(ret_type->start, (size_t)(ret_type->end - ret_type->start)), ")");
        else
            parser_err(ctx, colon, pos,
                       "There should not be a colon here, just put the function body without the colon.");
    } else if (ret_type == NULL && match(&pos, "->")) {
        const char *err_start = pos - 2;
        ret_type = optional(ctx, &pos, parse_type);
        if (ret_type) {
            parser_err(ctx, err_start, pos, "Function return types go inside the function parentheses like this: ",
                       string_slice(start, (size_t)(closing_paren - start)), " -> ",
                       string_slice(ret_type->start, (size_t)(ret_type->end - ret_type->start)), ")");
        } else {
            parser_err(ctx, err_start, pos,
                       "There shouldn't be a '->' here. Function return types go inside the function parentheses.");
        }
    }
}

// Collect the contiguous `#` comment lines directly above a definition (no
// blank line in between, and indented the same as the definition) as a single
// space-joined doc comment:
static Text_t doc_comment_above(parse_ctx_t *ctx, const char *start) {
    const char *text = ctx->file->text;
    const char *line_start = start;
    while (line_start > text && line_start[-1] != '\n')
        line_start--;
    // The definition's own indentation: a comment has to match it exactly to
    // count as a doc comment, otherwise a trailing comment at the end of the
    // *previous* definition's body would be picked up as this one's summary.
    size_t indent = (size_t)(start - line_start);
    const char *region = line_start;
    while (region > text && region[-1] == '\n') {
        const char *prev = region - 1;
        while (prev > text && prev[-1] != '\n')
            prev--;
        const char *p = prev;
        while (*p == ' ' || *p == '\t')
            p++;
        if (*p != '#') break;
        if ((size_t)(p - prev) != indent || strncmp(prev, line_start, indent) != 0) break;
        region = prev;
    }
    Text_t comment = EMPTY_TEXT;
    for (OptionalText_t com; (com = next_comment(ctx->comments, &region, line_start)).tag != TEXT_NONE;) {
        if (comment.length > 0) comment = Texts(comment, " ");
        comment = Texts(comment, Text$trim(Text$without_prefix(com, Text("#")), Text(" \t"), true, true));
    }
    return comment;
}

ast_t *parse_func_def(parse_ctx_t *ctx, const char *pos) {
    const char *start = pos;
    if (!match_word(&pos, "func")) return NULL;

    ast_t *name = optional(ctx, &pos, parse_var);
    if (!name) return NULL;

    // Subcommand functions: `func main.add(...)`, `func main.submodule.init(...)`
    if (*pos == '.' && !streq(Match(name, Var)->name, "main"))
        parser_err(ctx, pos, pos + 1,
                   "Subcommand functions must be named `main.<something>`, other functions can't have dots in their "
                   "names");
    while (*pos == '.') {
        const char *dot = pos;
        pos += 1;
        const char *word = get_id(&pos);
        if (!word) parser_err(ctx, dot, pos, "I expected a subcommand name after this period");
        name = NewAST(ctx->file, name->start, pos, Var, .name = String(Match(name, Var)->name, ".", word));
    }

    spaces(&pos);

    expect_str(ctx, start, &pos, "(", "I expected a parenthesis for this function's arguments");

    arg_ast_t *args = parse_args(ctx, &pos);
    spaces(&pos);
    type_ast_t *ret_type = match(&pos, "->") ? optional(ctx, &pos, parse_type) : NULL;
    whitespace(ctx, &pos);
    bool is_inline = false;
    ast_t *cache_ast = NULL;
    for (bool specials = match(&pos, ";"); specials; specials = match_separator(ctx, &pos)) {
        const char *flag_start = pos;
        if (match_word(&pos, "inline")) {
            is_inline = true;
        } else if (match_word(&pos, "cached")) {
            if (!cache_ast) cache_ast = NewAST(ctx->file, pos, pos, Int, .str = "-1");
        } else if (match_word(&pos, "cache_size")) {
            whitespace(ctx, &pos);
            if (!match(&pos, "=")) parser_err(ctx, flag_start, pos, "I expected a value for 'cache_size'");
            whitespace(ctx, &pos);
            cache_ast = expect(ctx, start, &pos, parse_expr, "I expected a maximum size for the cache");
        }
    }
    expect_closing(ctx, &pos, ")", "I wasn't able to parse the rest of this function definition");

    catch_common_errors_after_parens(ctx, start, pos, ret_type, false);

    ast_t *body = expect(ctx, start, &pos, parse_block, "This function needs a body block");
    return NewAST(ctx->file, start, pos, FunctionDef, .name = name, .args = args, .ret_type = ret_type, .body = body,
                  .cache = cache_ast, .is_inline = is_inline, .comment = doc_comment_above(ctx, start));
}

ast_t *parse_convert_def(parse_ctx_t *ctx, const char *pos) {
    const char *start = pos;
    if (!match_word(&pos, "convert")) return NULL;

    spaces(&pos);

    if (!match(&pos, "(")) return NULL;

    arg_ast_t *args = parse_args(ctx, &pos);
    spaces(&pos);
    type_ast_t *ret_type = match(&pos, "->") ? optional(ctx, &pos, parse_type) : NULL;
    whitespace(ctx, &pos);
    bool is_inline = false;
    ast_t *cache_ast = NULL;
    for (bool specials = match(&pos, ";"); specials; specials = match_separator(ctx, &pos)) {
        const char *flag_start = pos;
        if (match_word(&pos, "inline")) {
            is_inline = true;
        } else if (match_word(&pos, "cached")) {
            if (!cache_ast) cache_ast = NewAST(ctx->file, pos, pos, Int, .str = "-1");
        } else if (match_word(&pos, "cache_size")) {
            whitespace(ctx, &pos);
            if (!match(&pos, "=")) parser_err(ctx, flag_start, pos, "I expected a value for 'cache_size'");
            whitespace(ctx, &pos);
            cache_ast = expect(ctx, start, &pos, parse_expr, "I expected a maximum size for the cache");
        }
    }
    expect_closing(ctx, &pos, ")", "I wasn't able to parse the rest of this function definition");

    catch_common_errors_after_parens(ctx, start, pos, ret_type, false);

    ast_t *body = expect(ctx, start, &pos, parse_block, "This function needs a body block");
    return NewAST(ctx->file, start, pos, ConvertDef, .args = args, .ret_type = ret_type, .body = body,
                  .cache = cache_ast, .is_inline = is_inline);
}

ast_t *parse_lambda(parse_ctx_t *ctx, const char *pos) {
    const char *start = pos;
    if (!match_word(&pos, "func")) return NULL;
    spaces(&pos);
    if (!match(&pos, "(")) return NULL;
    arg_ast_t *args = parse_args(ctx, &pos);
    spaces(&pos);
    type_ast_t *ret = match(&pos, "->") ? optional(ctx, &pos, parse_type) : NULL;
    spaces(&pos);
    expect_closing(ctx, &pos, ")", "I was expecting a ')' to finish this anonymous function's arguments");
    catch_common_errors_after_parens(ctx, start, pos, ret, true);
    ast_t *body = optional(ctx, &pos, parse_block);
    if (!body) body = NewAST(ctx->file, pos, pos, Block, .statements = NULL);
    return NewAST(ctx->file, start, pos, Lambda, .id = ctx->next_lambda_id++, .args = args, .ret_type = ret,
                  .body = body);
}
