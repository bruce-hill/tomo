// This code defines functions for transforming ASTs back into Tomo source text

#pragma once

#include <stdbool.h>

#include "../ast.h"
#include "../stdlib/datatypes.h"

Text_t format_file(const char *path);
Text_t format_code(ast_t *ast, Table_t comments, Text_t indentation);
Text_t format_namespace(ast_t *namespace, Table_t comments, Text_t indent);
OptionalText_t format_inline_code(ast_t *ast, Table_t comments);
