// This code defines functions for transforming ASTs back into Tomo source text

#pragma once

#include "../ast.h"
#include "../stdlib/datatypes.h"

#define MAX_WIDTH 100

#define must(expr)                                                                                                     \
    ({                                                                                                                 \
        OptionalText_t _expr = expr;                                                                                   \
        if (_expr.length < 0) return NONE_TEXT;                                                                        \
        (Text_t) _expr;                                                                                                \
    })

extern const Text_t single_indent;

Text_t format_file(const char *path);
Text_t format_code(ast_t *ast, Table_t comments, Text_t indentation);
OptionalText_t format_inline_code(ast_t *ast, Table_t comments);

OptionalText_t next_comment(Table_t comments, const char **pos, const char *end);
bool range_has_comment(const char *start, const char *end, Table_t comments);
OptionalText_t next_comment(Table_t comments, const char **pos, const char *end);
void add_line(Text_t *code, Text_t line, Text_t indent);
