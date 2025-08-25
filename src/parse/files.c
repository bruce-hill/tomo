
#include <gc.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <string.h>

#include "../ast.h"
#include "../stdlib/stdlib.h"
#include "../stdlib/tables.h"
#include "../stdlib/util.h"
#include "context.h"
#include "errors.h"
#include "files.h"
#include "functions.h"
#include "parse.h"
#include "utils.h"

// The cache of {filename -> parsed AST} will hold at most this many entries:
#ifndef PARSE_CACHE_SIZE
#define PARSE_CACHE_SIZE 100
#endif

static ast_t *parse_top_declaration(parse_ctx_t *ctx, const char *pos) {
    ast_t *declaration = parse_declaration(ctx, pos);
    if (declaration) declaration->__data.Declare.top_level = true;
    return declaration;
}

public
ast_t *parse_file_body(parse_ctx_t *ctx, const char *pos) {
    const char *start = pos;
    whitespace(&pos);
    ast_list_t *statements = NULL;
    for (;;) {
        const char *next = pos;
        whitespace(&next);
        if (get_indent(ctx, next) != 0) break;
        ast_t *stmt;
        if ((stmt = optional(ctx, &pos, parse_struct_def)) || (stmt = optional(ctx, &pos, parse_func_def))
            || (stmt = optional(ctx, &pos, parse_enum_def)) || (stmt = optional(ctx, &pos, parse_lang_def))
            || (stmt = optional(ctx, &pos, parse_extend)) || (stmt = optional(ctx, &pos, parse_convert_def))
            || (stmt = optional(ctx, &pos, parse_use)) || (stmt = optional(ctx, &pos, parse_extern))
            || (stmt = optional(ctx, &pos, parse_inline_c)) || (stmt = optional(ctx, &pos, parse_top_declaration))) {
            statements = new (ast_list_t, .ast = stmt, .next = statements);
            pos = stmt->end;
            whitespace(&pos); // TODO: check for newline
        } else {
            break;
        }
    }
    whitespace(&pos);
    if (pos < ctx->file->text + ctx->file->len && *pos != '\0') {
        parser_err(ctx, pos, eol(pos), "I expect all top-level statements to be declarations of some kind");
    }
    REVERSE_LIST(statements);
    return NewAST(ctx->file, start, pos, Block, .statements = statements);
}

public
ast_t *parse_file(const char *path, jmp_buf *on_err) {
    if (path[0] != '<' && path[0] != '/') fail("Path is not fully resolved: ", path);
    // NOTE: this cache leaks a bounded amount of memory. The cache will never
    // hold more than PARSE_CACHE_SIZE entries (see below), but each entry's
    // AST holds onto a reference to the file it came from, so they could
    // potentially be somewhat large.
    static Table_t cached = {};
    ast_t *ast = Table$str_get(cached, path);
    if (ast) return ast;

    file_t *file;
    if (path[0] == '<') {
        const char *endbracket = strchr(path, '>');
        if (!endbracket) return NULL;
        file = spoof_file(GC_strndup(path, (size_t)(endbracket + 1 - path)), endbracket + 1);
    } else {
        file = load_file(path);
        if (!file) return NULL;
    }

    parse_ctx_t ctx = {
        .file = file,
        .on_err = on_err,
    };

    const char *pos = file->text;
    if (match(&pos, "#!")) // shebang
        some_not(&pos, "\r\n");

    whitespace(&pos);
    ast = parse_file_body(&ctx, pos);
    pos = ast->end;
    whitespace(&pos);
    if (pos < file->text + file->len && *pos != '\0') {
        parser_err(&ctx, pos, pos + strlen(pos), "I couldn't parse this part of the file");
    }

    // If cache is getting too big, evict a random entry:
    if (cached.entries.length > PARSE_CACHE_SIZE) {
        // FIXME: this currently evicts the first entry, but it should be more like
        // an LRU cache
        struct {
            const char *path;
            ast_t *ast;
        } *to_remove = Table$entry(cached, 1);
        Table$str_remove(&cached, to_remove->path);
    }

    // Save the AST in the cache:
    Table$str_set(&cached, path, ast);
    return ast;
}
