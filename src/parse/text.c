// Logic for parsing text literals

#include <stdarg.h>
#include <stdbool.h>
#include <string.h>

#include "../unistr-fixed.h"
#include <unictype.h>
#include <uniname.h>

#include "../ast.h"
#include "../stdlib/text.h"
#include "../stdlib/util.h"
#include "context.h"
#include "errors.h"
#include "expressions.h"
#include "types.h"
#include "utils.h"

static const char closing[128] = {['('] = ')', ['['] = ']', ['<'] = '>', ['{'] = '}'};

static ast_list_t *_parse_text_helper(parse_ctx_t *ctx, const char **out_pos, char open_quote, char close_quote,
                                      char open_interp, bool allow_escapes) {
    const char *pos = *out_pos;
    int64_t starting_indent = get_indent(ctx, pos);
    int64_t string_indent = starting_indent + SPACES_PER_INDENT;
    ast_list_t *chunks = NULL;
    Text_t chunk = EMPTY_TEXT;
    const char *chunk_start = pos;
    int depth = 1;
    bool leading_newline = false;
    int64_t plain_span_len = 0;
#define FLUSH_PLAIN_SPAN()                                                                                             \
    do {                                                                                                               \
        if (plain_span_len > 0) {                                                                                      \
            chunk = Texts(chunk, Text$from_strn(pos - plain_span_len, (size_t)plain_span_len));                        \
            plain_span_len = 0;                                                                                        \
        }                                                                                                              \
    } while (0)
    for (const char *end = ctx->file->text + ctx->file->len; pos < end && depth > 0;) {
        const char *after_indentation = pos;
        if (*pos == open_interp) { // Interpolation
            FLUSH_PLAIN_SPAN();
            const char *interp_start = pos;
            if (chunk.length > 0) {
                ast_t *literal = NewAST(ctx->file, chunk_start, pos, TextLiteral, .text = chunk);
                chunks = new (ast_list_t, .ast = literal, .next = chunks);
                chunk = EMPTY_TEXT;
            }
            ++pos;
            ast_t *interp;
            if (*pos == ' ' || *pos == '\t')
                parser_err(ctx, pos, pos + 1, "Whitespace is not allowed before an interpolation here");
            interp = expect(ctx, interp_start, &pos, parse_term_no_suffix, "I expected an interpolation term here");
            chunks = new (ast_list_t, .ast = interp, .next = chunks);
            chunk_start = pos;
        } else if (allow_escapes && *pos == '\\') {
            FLUSH_PLAIN_SPAN();
            const char *c = unescape(ctx, &pos);
            chunk = Texts(chunk, Text$from_str(c));
        } else if (!leading_newline && *pos == open_quote && closing[(int)open_quote]) { // Nested pair begin
            if (get_indent(ctx, pos) == starting_indent) {
                ++depth;
            }
            plain_span_len += 1;
            ++pos;
        } else if (!leading_newline && *pos == close_quote) { // Nested pair end
            if (get_indent(ctx, pos) == starting_indent) {
                --depth;
                if (depth == 0) break;
            }
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
            if (*pos == close_quote) {
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

    if (chunk.length > 0) {
        ast_t *literal = NewAST(ctx->file, chunk_start, pos, TextLiteral, .text = chunk);
        chunks = new (ast_list_t, .ast = literal, .next = chunks);
        chunk = EMPTY_TEXT;
    }

    REVERSE_LIST(chunks);
    char close_str[2] = {close_quote, 0};
    expect_closing(ctx, &pos, close_str, "I was expecting a ", close_quote, " to finish this string");
    *out_pos = pos;
    return chunks;
}

ast_t *parse_text(parse_ctx_t *ctx, const char *pos) {
    // ('"' ... '"' / "'" ... "'" / "`" ... "`")
    // "$" [name] [interp-char] quote-char ... close-quote
    const char *start = pos;
    const char *lang = NULL;

    char open_quote, close_quote, open_interp = '$';
    if (match(&pos, "\"")) { // Double quote
        open_quote = '"', close_quote = '"', open_interp = '$';
    } else if (match(&pos, "`")) { // Backtick
        open_quote = '`', close_quote = '`', open_interp = '$';
    } else if (match(&pos, "'")) { // Single quote
        open_quote = '\'', close_quote = '\'', open_interp = '$';
    } else if (match(&pos, "$")) { // Customized strings
        lang = get_id(&pos);
        // $"..." or $@"...."
        static const char *interp_chars = "~!@#$%^&*+=\\?";
        if (match(&pos, "$")) { // Disable interpolation with $$
            open_interp = '\x03';
        } else if (strchr(interp_chars, *pos)) {
            open_interp = *pos;
            ++pos;
        }
        static const char *quote_chars = "\"'`|/;([{<";
        if (!strchr(quote_chars, *pos))
            parser_err(ctx, pos, pos + 1,
                       "This is not a valid string quotation character. Valid characters are: \"'`|/;([{<");
        open_quote = *pos;
        ++pos;
        close_quote = closing[(int)open_quote] ? closing[(int)open_quote] : open_quote;
    } else {
        return NULL;
    }

    bool allow_escapes = (open_quote != '`');
    ast_list_t *chunks = _parse_text_helper(ctx, &pos, open_quote, close_quote, open_interp, allow_escapes);
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

    static const char *quote_chars = "\"'`|/;([{<";
    if (!strchr(quote_chars, *pos))
        parser_err(ctx, pos, pos + 1,
                   "This is not a valid string quotation character. Valid characters are: \"'`|/;([{<");

    char quote = *(pos++);
    char unquote = closing[(int)quote] ? closing[(int)quote] : quote;
    ast_list_t *chunks = _parse_text_helper(ctx, &pos, quote, unquote, '@', false);
    return NewAST(ctx->file, start, pos, InlineCCode, .chunks = chunks, .type_ast = type);
}

ast_t *parse_path(parse_ctx_t *ctx, const char *pos) {
    // "(" ("~/" / "./" / "../" / "/") ... ")"
    const char *start = pos;

    if (!match(&pos, "(")) return NULL;

    if (!(*pos == '~' || *pos == '.' || *pos == '/')) return NULL;

    const char *path_start = pos;
    size_t len = 1;
    int paren_depth = 1;
    while (pos + len < ctx->file->text + ctx->file->len - 1) {
        if (pos[len] == '\\') {
            len += 2;
            continue;
        } else if (pos[len] == '(') {
            paren_depth += 1;
        } else if (pos[len] == ')') {
            paren_depth -= 1;
            if (paren_depth <= 0) break;
        } else if (pos[len] == '\r' || pos[len] == '\n') {
            parser_err(ctx, path_start, &pos[len - 1], "This path was not closed");
        }
        len += 1;
    }
    pos += len + 1;
    char *path = String(string_slice(path_start, .length = len));
    for (char *src = path, *dest = path;;) {
        if (src[0] == '\\') {
            *(dest++) = src[1];
            src += 2;
        } else if (*src) {
            *(dest++) = *(src++);
        } else {
            *(dest++) = '\0';
            break;
        }
    }
    return NewAST(ctx->file, start, pos, Path, .path = path);
}
