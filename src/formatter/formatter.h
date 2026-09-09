// This code defines functions for transforming ASTs back into Tomo source text

#pragma once

#include <stdbool.h>

#include "../ast.h"
#include "../stdlib/datatypes.h"
#include "../stdlib/files.h"

Text_t format_file(const char *path);
// Format an already-loaded file; *formatted reports whether it parsed cleanly.
Text_t format_source(file_t *file, bool *formatted);
Text_t format_code_at(ast_t *ast, Table_t comments, Text_t indentation, int64_t column);

static inline Text_t format_code(ast_t *ast, Table_t comments, Text_t indent) {
    return format_code_at(ast, comments, indent, (int64_t)indent.length);
}
Text_t format_namespace(ast_t *namespace, Table_t comments, Text_t indent);
OptionalText_t format_inline_code(ast_t *ast, Table_t comments);
