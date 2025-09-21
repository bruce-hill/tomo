// Logic for parsing control flow
#pragma once

#include "../ast.h"
#include "context.h"

ast_t *parse_block(parse_ctx_t *ctx, const char *pos);
ast_t *parse_do(parse_ctx_t *ctx, const char *pos);
ast_t *parse_for(parse_ctx_t *ctx, const char *pos);
ast_t *parse_if(parse_ctx_t *ctx, const char *pos);
ast_t *parse_pass(parse_ctx_t *ctx, const char *pos);
ast_t *parse_repeat(parse_ctx_t *ctx, const char *pos);
ast_t *parse_return(parse_ctx_t *ctx, const char *pos);
ast_t *parse_skip(parse_ctx_t *ctx, const char *pos);
ast_t *parse_stop(parse_ctx_t *ctx, const char *pos);
ast_t *parse_when(parse_ctx_t *ctx, const char *pos);
ast_t *parse_while(parse_ctx_t *ctx, const char *pos);
