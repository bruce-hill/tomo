#pragma once

#include <setjmp.h>

#include "ast.h"

type_ast_t *parse_type_str(const char *str);
ast_t *parse_expression_str(const char *str);
ast_t *parse_file(sss_file_t *file, jmp_buf *on_err);
