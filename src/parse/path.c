// Logic for parsing path literals

#include <stdarg.h>
#include <stdbool.h>
#include <string.h>

#include <unictype.h>
#include <uniname.h>

#include "../ast.h"
#include "context.h"
#include "errors.h"

ast_t *parse_path(parse_ctx_t *ctx, const char *pos) {
    // [~./] ("\" . / [^ \r\n\t])*
    const char *start = pos;

    bool inside_parens = match(&pos, "(") > 0;

    if (!(*pos == '~' || *pos == '.' || *pos == '/')) return NULL;

    // These are some characters that might reasonably follow after a path literal in Tomo code,
    // for example: `[./one, ./two]` (should not include ',' or ']' in the parsed literals)
    const bool needs_escaping[128] = {[' '] = true, ['\t'] = true, ['\r'] = true, ['\n'] = true,
                                      [','] = true, [';'] = true,  [':'] = true,  ['='] = true,
                                      [')'] = true, ['}'] = true,  [']'] = true};
    int paren_depth = 0;
    const char *path_start = pos;
    size_t len = 1;
    bool terminated = false; // whether a break below already advanced pos
    while (pos + len < ctx->file->text + ctx->file->len - 1) {
        // Indexed as an unsigned byte and range-checked: a `char` is signed
        // here, so any non-ASCII byte (`./café.txt`) would index before the
        // table and read whatever happened to be there.
        unsigned char c = (unsigned char)pos[len];
        if (pos[len] == '\\') {
            len += 2;
            continue;
        } else if (!inside_parens && paren_depth == 0 && c < sizeof(needs_escaping) / sizeof(needs_escaping[0])
                   && needs_escaping[c]) {
            pos += len;
            terminated = true;
            break;
        } else if (pos[len] == '(') {
            paren_depth += 1;
        } else if (pos[len] == ')') {
            if (paren_depth <= 0) {
                pos += len;
                terminated = true;
                break;
            }
            paren_depth -= 1;
        } else if (pos[len] == '\r' || pos[len] == '\n') {
            if (paren_depth == 0) {
                pos += len;
                terminated = true;
                break;
            }
            parser_err(ctx, path_start, &pos[len], "This path was not closed");
        }
        len += 1;
    }
    // A path that runs up against the end of the file exits the loop without
    // hitting any terminator above; pos must still advance past what was
    // consumed, or the "path" is a zero-width AST and every enclosing parse
    // loop (parse_file_body's for(;;), block statement loops) spins forever
    // re-parsing the same spot. `x := 12..y` used to hang the compiler.
    if (!terminated) pos += len;
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
    if (inside_parens && !match(&pos, ")")) return NULL;
    return NewAST(ctx->file, start, pos, Path, .path = path);
}

ast_t *parse_embed(parse_ctx_t *ctx, const char *pos) {
    const char *start = pos;
    if (!match_word(&pos, "_embed_")) return NULL;
    ast_t *path_ast = expect(ctx, start, &pos, parse_path, "I expected a file path here");
    return NewAST(ctx->file, start, pos, Embed, .path = path_ast);
}
