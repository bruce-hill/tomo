// Logic for parsing numbers
#pragma once

#include "../ast.h"
#include "context.h"

ast_e match_binary_operator(const char **pos);
ast_t *parse_infix_expr(parse_ctx_t *ctx, const char *pos, int min_tightness);
