// This code defines functions for transforming ASTs back into Tomo source text

#include <gc.h>
#include <setjmp.h>
#include <string.h>

#include "ast.h"
#include "formatter.h"
#include "parse/context.h"
#include "parse/files.h"
#include "parse/utils.h"
#include "stdlib/datatypes.h"
#include "stdlib/optionals.h"
#include "stdlib/tables.h"
#include "stdlib/text.h"

OptionalText_t next_comment(Table_t comments, const char **pos, const char *end) {
    for (const char *p = *pos; p < end; p++) {
        const char **comment_end = Table$get(comments, &p, parse_comments_info);
        if (comment_end) {
            *pos = *comment_end;
            return Text$from_strn(p, (int64_t)(*comment_end - p));
        }
    }
    return NONE_TEXT;
}

Text_t format_code(ast_t *ast, Table_t comments) {
    (void)comments;
    switch (ast->tag) {
    default: {
        Text_t code = Text$from_strn(ast->start, (int64_t)(ast->end - ast->start));
        return Text$replace(code, Text("\t"), Text("    "));
    }
    }
}

OptionalText_t format_inline_code(ast_t *ast, Table_t comments) {
    for (const char *p = ast->start; p < ast->end; p++) {
        if (*p == '\n' || *p == '\r') return NONE_TEXT;
    }
    const char *pos = ast->start;
    OptionalText_t comment = next_comment(comments, &pos, ast->end);
    if (comment.length >= 0) return NONE_TEXT;
    switch (ast->tag) {
    default: return Text$from_strn(ast->start, (int64_t)(ast->end - ast->start));
    }
}

Text_t format_file(const char *path) {
    file_t *file = load_file(path);
    if (!file) return EMPTY_TEXT;

    jmp_buf on_err;
    if (setjmp(on_err) != 0) {
        return Text$from_str(file->text);
    }
    parse_ctx_t ctx = {
        .file = file,
        .on_err = &on_err,
        .comments = {},
    };

    const char *pos = file->text;
    if (match(&pos, "#!")) // shebang
        some_not(&pos, "\r\n");

    whitespace(&ctx, &pos);
    ast_t *ast = parse_file_body(&ctx, pos);
    if (!ast) return Text$from_str(file->text);
    pos = ast->end;
    whitespace(&ctx, &pos);
    if (pos < file->text + file->len && *pos != '\0') {
        return Text$from_str(file->text);
    }

    const char *fmt_pos = file->text;
    Text_t code = EMPTY_TEXT;
    for (OptionalText_t comment; (comment = next_comment(ctx.comments, &fmt_pos, ast->start)).length > 0;) {
        code = Texts(code, comment, "\n");
    }
    code = Texts(code, format_code(ast, ctx.comments));
    for (OptionalText_t comment; (comment = next_comment(ctx.comments, &fmt_pos, ast->start)).length > 0;) {
        code = Texts(code, comment, "\n");
    }
    return code;
}
