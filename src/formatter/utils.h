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
PUREFUNC int suggested_blank_lines(ast_t *first, ast_t *second);
PUREFUNC bool ends_deeper_than(Text_t code, Text_t indent);
Text_t indent_code(Text_t code);
Text_t parenthesize(Text_t code, Text_t indent);
CONSTFUNC ast_t *unwrap_block(ast_t *ast);
OptionalText_t bounded_inline(ast_t *ast, Table_t comments);
OptionalText_t termify_inline(ast_t *ast, Table_t comments);

// Whether a rendering fits on the page depends on the column it starts at, not
// on the indentation it would wrap to: `assert ` before an expression leaves
// seven fewer columns for it. Every function that makes that decision takes the
// column as its last argument, and the name without it stands for the common
// case, a rendering that starts at its own indentation.
Text_t bounded_at(ast_t *ast, Table_t comments, Text_t indent, int64_t column);
Text_t termify_at(ast_t *ast, Table_t comments, Text_t indent, int64_t column);

static inline Text_t bounded(ast_t *ast, Table_t comments, Text_t indent) {
    return bounded_at(ast, comments, indent, (int64_t)indent.length);
}

static inline Text_t termify(ast_t *ast, Table_t comments, Text_t indent) {
    return termify_at(ast, comments, indent, (int64_t)indent.length);
}
