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

// The flags a definition can carry after its fields. Each is written as its own
// `; name` clause -- that is what the parser separates them by -- so they are
// held apart here rather than joined into one text: wrapped, each gets a line.
typedef struct {
    Text_t items[4];
    int count;
} flag_list_t;

void add_flag(flag_list_t *flags, bool present, Text_t name);

// All of them on one line, `; a; b`.
Text_t inline_flags(flag_list_t flags);
OptionalText_t next_comment(Table_t comments, const char **pos, const char *end);
bool range_has_comment(const char *start, const char *end, Table_t comments);
PUREFUNC int suggested_blank_lines(ast_t *first, ast_t *second);
PUREFUNC bool ends_deeper_than(Text_t code, Text_t indent);
PUREFUNC int64_t trailing_line_len(Text_t text);
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
OptionalText_t dotted_inline(ast_t *ast, Table_t comments);
Text_t dotted_at(ast_t *ast, Table_t comments, Text_t indent, int64_t column);

static inline Text_t bounded(ast_t *ast, Table_t comments, Text_t indent) {
    return bounded_at(ast, comments, indent, (int64_t)indent.length);
}

static inline Text_t termify(ast_t *ast, Table_t comments, Text_t indent) {
    return termify_at(ast, comments, indent, (int64_t)indent.length);
}
