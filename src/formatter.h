// This code defines functions for transforming ASTs back into Tomo source text

#pragma once

#include "ast.h"
#include "stdlib/datatypes.h"

Text_t code_format(ast_t *ast, Table_t *comments);
OptionalText_t code_format_inline(ast_t *ast, Table_t *comments);
