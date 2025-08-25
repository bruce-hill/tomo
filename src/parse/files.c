
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
#include "expressions.h"
#include "statements.h"
#include "text.h"
#include "typedefs.h"
#include "types.h"
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

ast_t *parse_use(parse_ctx_t *ctx, const char *pos) {
    const char *start = pos;

    ast_t *var = parse_var(ctx, pos);
    if (var) {
        pos = var->end;
        spaces(&pos);
        if (!match(&pos, ":=")) return NULL;
        spaces(&pos);
    }

    if (!match_word(&pos, "use")) return NULL;
    spaces(&pos);
    size_t name_len = strcspn(pos, " \t\r\n;");
    if (name_len < 1) parser_err(ctx, start, pos, "There is no module name here to use");
    char *name = GC_strndup(pos, name_len);
    pos += name_len;
    while (match(&pos, ";"))
        continue;
    int what;
    if (name[0] == '<' || ends_with(name, ".h")) {
        what = USE_HEADER;
    } else if (starts_with(name, "-l")) {
        what = USE_SHARED_OBJECT;
    } else if (ends_with(name, ".c")) {
        what = USE_C_CODE;
    } else if (ends_with(name, ".S") || ends_with(name, ".s")) {
        what = USE_ASM;
    } else if (starts_with(name, "./") || starts_with(name, "/") || starts_with(name, "../")
               || starts_with(name, "~/")) {
        what = USE_LOCAL;
    } else {
        what = USE_MODULE;
    }
    return NewAST(ctx->file, start, pos, Use, .var = var, .path = name, .what = what);
}

ast_t *parse_extern(parse_ctx_t *ctx, const char *pos) {
    const char *start = pos;
    if (!match_word(&pos, "extern")) return NULL;
    spaces(&pos);
    const char *name = get_id(&pos);
    spaces(&pos);
    if (!match(&pos, ":")) parser_err(ctx, start, pos, "I couldn't get a type for this extern");
    type_ast_t *type = expect(ctx, start, &pos, parse_type, "I couldn't parse the type for this extern");
    return NewAST(ctx->file, start, pos, Extern, .name = name, .type = type);
}

public
ast_t *parse_file_str(const char *str) {
    file_t *file = spoof_file("<string>", str);
    parse_ctx_t ctx = {
        .file = file,
        .on_err = NULL,
    };

    const char *pos = file->text;
    whitespace(&pos);
    ast_t *ast = parse_file_body(&ctx, pos);
    pos = ast->end;
    whitespace(&pos);
    if (pos < file->text + file->len && *pos != '\0')
        parser_err(&ctx, pos, pos + strlen(pos), "I couldn't parse this part of the string");
    return ast;
}
