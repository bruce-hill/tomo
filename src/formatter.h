// This code defines functions for transforming ASTs back into Tomo source text

#pragma once

#include "ast.h"
#include "stdlib/datatypes.h"

Text_t format_file(const char *path);
Text_t format_code(ast_t *ast, Table_t comments, Text_t indentation);
OptionalText_t format_inline_code(ast_t *ast, Table_t comments);
OptionalText_t next_comment(Table_t comments, const char **pos, const char *end);
