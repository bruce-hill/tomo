// Logic for parsing text literals

#include <stdarg.h>
#include <stdbool.h>
#include <string.h>

#include "../unistr-fixed.h"
#include <unictype.h>
#include <uniname.h>

#include "../ast.h"
#include "../stdlib/text.h"
#include "../util.h"
#include "context.h"
#include "errors.h"
#include "expressions.h"
#include "types.h"
#include "utils.h"

static ast_list_t *_parse_text_helper(parse_ctx_t *ctx, const char **out_pos, bool allow_interps, bool allow_escapes) {
    const char *pos = *out_pos;

    int64_t starting_indent = get_indent(ctx, pos);
    int64_t string_indent = starting_indent + SPACES_PER_INDENT;

    const char *quote, *interp;
    bool quote_allows_escapes = true;
    if (match(&pos, "\"\"\"")) { // Triple double quote
        quote = "\"\"\"", interp = "$", quote_allows_escapes = false;
    } else if (match(&pos, "'''")) { // Triple single quote
        quote = "'''", interp = "$", quote_allows_escapes = false;
    } else if (match(&pos, "```")) { // Triple backtick
        quote = "```", interp = "@", quote_allows_escapes = false;
    } else if (match(&pos, "\"")) { // Double quote
        quote = "\"", interp = "$", quote_allows_escapes = true;
    } else if (match(&pos, "'")) { // Single quote
        quote = "'", interp = "$", quote_allows_escapes = true;
    } else if (match(&pos, "`")) { // Backtick
        quote = "`", interp = "@", quote_allows_escapes = true;
    } else {
        parser_err(ctx, pos, pos, "I expected a valid text here");
    }

    // The caller can force escapes off (e.g. inline C, which must be verbatim so
    // a `\n` reaches the C compiler as a backslash-n, not a real newline).
    allow_escapes = allow_escapes && quote_allows_escapes;

    if (!allow_interps) interp = NULL;

    ast_list_t *chunks = NULL;
    Text_t chunk = EMPTY_TEXT;
    const char *chunk_start = pos;
    bool leading_newline = false;
    int64_t plain_span_len = 0;
#define FLUSH_PLAIN_SPAN()                                                                                             \
    do {                                                                                                               \
        if (plain_span_len > 0) {                                                                                      \
            chunk = Texts(chunk, Text$from_strn(pos - plain_span_len, (size_t)plain_span_len));                        \
            plain_span_len = 0;                                                                                        \
        }                                                                                                              \
    } while (0)

    for (const char *end = ctx->file->text + ctx->file->len; pos < end;) {
        const char *after_indentation = pos;
        const char *interp_start = pos;
        if (interp != NULL && strncmp(pos, interp, strlen(interp)) == 0) { // Interpolation
            FLUSH_PLAIN_SPAN();
            if (chunk.length > 0) {
                ast_t *literal = NewAST(ctx->file, chunk_start, pos, TextLiteral, .text = chunk);
                chunks = new (ast_list_t, .ast = literal, .next = chunks);
                chunk = EMPTY_TEXT;
            }
            pos += strlen(interp);
            if (*pos == ' ' || *pos == '\t')
                parser_err(ctx, pos, pos + 1, "Whitespace is not allowed before an interpolation here");
            ast_t *value =
                expect(ctx, interp_start, &pos, parse_term_no_suffix, "I expected an interpolation term here");
            chunks = new (ast_list_t, .ast = value, .next = chunks);
            chunk_start = pos;
        } else if (allow_escapes && *pos == '\\') {
            FLUSH_PLAIN_SPAN();
            const char *escape_start = pos;
            size_t escaped_len = 0;
            const char *c = unescape(ctx, &pos, &escaped_len);
            if (memchr(c, '\0', escaped_len))
                parser_err(ctx, escape_start, pos, "NUL bytes are not allowed in text literals");
            chunk = Texts(chunk, Text$from_strn(c, escaped_len));
        } else if (!leading_newline && strncmp(pos, quote, strlen(quote)) == 0) { // Nested pair end
            if (get_indent(ctx, pos) == starting_indent) break;
            plain_span_len += 1;
            ++pos;
        } else if (newline_with_indentation(&after_indentation, string_indent)) { // Newline
            FLUSH_PLAIN_SPAN();
            pos = after_indentation;
            if (!leading_newline && !(chunk.length > 0 || chunks)) {
                leading_newline = true;
            } else {
                chunk = Texts(chunk, Text("\n"));
            }
        } else if (newline_with_indentation(&after_indentation, starting_indent)) { // Line continuation (..)
            FLUSH_PLAIN_SPAN();
            pos = after_indentation;
            if (strncmp(pos, quote, strlen(quote)) == 0) {
                break;
            } else if (some_of(&pos, ".") >= 2) {
                // Multi-line split
                continue;
            } else {
                parser_err(ctx, pos, eol(pos),
                           "This multi-line string should be either indented or have '..' at the front");
            }
        } else { // Plain character
            ucs4_t codepoint;
            const char *next = (const char *)u8_next(&codepoint, (const uint8_t *)pos);
            plain_span_len += (int64_t)(next - pos);
            if (next == NULL) break;
            pos = next;
        }
    }

    FLUSH_PLAIN_SPAN();
#undef FLUSH_PLAIN_SPAN

    expect_closing(ctx, &pos, quote, "I was expecting a ", quote, " to finish this string");

    if (chunk.length > 0) {
        ast_t *literal = NewAST(ctx->file, chunk_start, pos, TextLiteral, .text = chunk);
        chunks = new (ast_list_t, .ast = literal, .next = chunks);
    }

    REVERSE_LIST(chunks);
    *out_pos = pos;
    return chunks;
}

ast_t *parse_text(parse_ctx_t *ctx, const char *pos, bool allow_interps) {
    // ('"' ... '"' / "'" ... "'" / "`" ... "`")
    // "$" [name] quote-char ... close-quote
    const char *start = pos;
    type_ast_t *lang = NULL;

    if (match(&pos, "$")) {
        lang = expect(ctx, start, &pos, parse_type, "I couldn't parse the type for this text");
    }

    if (!(*pos == '"' || *pos == '\'' || *pos == '`')) return NULL;

    ast_list_t *chunks = _parse_text_helper(ctx, &pos, allow_interps, /*allow_escapes=*/true);
    bool colorize = match(&pos, "~") && match_word(&pos, "colorized");
    return NewAST(ctx->file, start, pos, TextJoin, .lang = lang, .children = chunks, .colorize = colorize);
}

ast_t *parse_inline_c(parse_ctx_t *ctx, const char *pos) {
    const char *start = pos;
    if (!match_word(&pos, "C_code")) return NULL;

    spaces(&pos);
    type_ast_t *type = NULL;
    if (match(&pos, ":")) {
        type = expect(ctx, start, &pos, parse_type, "I couldn't parse the type for this C_code code");
        spaces(&pos);
    }

    static const char *quote_chars = "\"'`";
    if (!strchr(quote_chars, *pos))
        parser_err(ctx, pos, pos + 1, "This is not a valid string quotation character. Valid characters are: \"'`");

    // Inline C is verbatim: no backslash-escaping (so `\n` reaches the C
    // compiler as-is), though `@`-interpolation is still allowed.
    ast_list_t *chunks = _parse_text_helper(ctx, &pos, /*allow_interps=*/true, /*allow_escapes=*/false);
    return NewAST(ctx->file, start, pos, InlineCCode, .chunks = chunks, .type_ast = type);
}
