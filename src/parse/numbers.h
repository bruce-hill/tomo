// Logic for parsing numbers
#pragma once

#include "../ast.h"
#include "context.h"

ast_t *parse_int(parse_ctx_t *ctx, const char *pos);
ast_t *parse_num(parse_ctx_t *ctx, const char *pos);
