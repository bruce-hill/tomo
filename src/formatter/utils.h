// This file defines utility functions for autoformatting code

#pragma once

#include <stdbool.h>

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

void add_line(Text_t *code, Text_t line, Text_t indent);
OptionalText_t next_comment(Table_t comments, const char **pos, const char *end);
bool range_has_comment(const char *start, const char *end, Table_t comments);
CONSTFUNC bool should_have_blank_line(ast_t *ast);
Text_t indent_code(Text_t code);
Text_t parenthesize(Text_t code, Text_t indent);
CONSTFUNC ast_t *unwrap_block(ast_t *ast);
CONSTFUNC const char *binop_tomo_operator(ast_e tag);
