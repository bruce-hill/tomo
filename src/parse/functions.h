// Logic for parsing functions
#pragma once

#include "../ast.h"
#include "context.h"

arg_ast_t *parse_args(parse_ctx_t *ctx, const char **pos);
ast_t *parse_lambda(parse_ctx_t *ctx, const char *pos);
ast_t *parse_func_def(parse_ctx_t *ctx, const char *pos);
ast_t *parse_convert_def(parse_ctx_t *ctx, const char *pos);
