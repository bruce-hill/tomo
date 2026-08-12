// Logic for parsing paths
#pragma once

#include "../ast.h"
#include "context.h"

ast_t *parse_path(parse_ctx_t *ctx, const char *pos);
ast_t *parse_embed(parse_ctx_t *ctx, const char *pos);
