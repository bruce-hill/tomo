// This file defines utility functions for autoformatting code

#pragma once

#include <stdbool.h>

#include "../ast.h"
#include "../stdlib/datatypes.h"
#include "../stdlib/optionals.h"

#define MAX_WIDTH 100

#define must(expr)                                                                                                     \
    ({                                                                                                                 \
        OptionalText_t _expr = expr;                                                                                   \
        if (_expr.tag == TEXT_NONE) return NONE_TEXT;                                                                  \
        (Text_t) _expr;                                                                                                \
    })

extern const Text_t single_indent;

void add_line(Text_t *code, Text_t line, Text_t indent);
OptionalText_t next_comment(Table_t comments, const char **pos, const char *end);
bool range_has_comment(const char *start, const char *end, Table_t comments);
CONSTFUNC int suggested_blank_lines(ast_t *first, ast_t *second);
Text_t indent_code(Text_t code);
Text_t parenthesize(Text_t code, Text_t indent);
CONSTFUNC ast_t *unwrap_block(ast_t *ast);
OptionalText_t termify_inline(ast_t *ast, Table_t comments);
Text_t termify(ast_t *ast, Table_t comments, Text_t indent);
