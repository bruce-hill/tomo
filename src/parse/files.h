// Logic for parsing a whole file
#pragma once

#include <setjmp.h>
#include <stdint.h>

#include "../ast.h"
#include "context.h"

ast_t *parse_file(const char *path, jmp_buf *on_err);
ast_t *parse_file_body(parse_ctx_t *ctx, const char *pos);
