// Logic for parsing text literals
#pragma once

#include <stdbool.h>

#include "../ast.h"
#include "context.h"

ast_t *parse_text(parse_ctx_t *ctx, const char *pos, bool allow_interps);
ast_t *parse_inline_c(parse_ctx_t *ctx, const char *pos);
ast_t *parse_path(parse_ctx_t *ctx, const char *pos);
