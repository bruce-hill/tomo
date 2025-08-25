// Logic for parsing container types (lists, sets, tables)
#pragma once

#include "../ast.h"
#include "context.h"

ast_t *parse_list(parse_ctx_t *ctx, const char *pos);
ast_t *parse_set(parse_ctx_t *ctx, const char *pos);
ast_t *parse_table(parse_ctx_t *ctx, const char *pos);
