#pragma once

// Parsing logic

#include <setjmp.h>

#include "ast.h"

type_ast_t *parse_type_str(const char *str);
ast_t *parse_file(const char *path, jmp_buf *on_err);
ast_t *parse(const char *str);
ast_t *parse_expression(const char *str);
