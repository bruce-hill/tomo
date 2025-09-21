// Logic for parsing statements
#pragma once

#include "../ast.h"
#include "context.h"

ast_t *parse_assignment(parse_ctx_t *ctx, const char *pos);
ast_t *parse_declaration(parse_ctx_t *ctx, const char *pos);
ast_t *parse_debug_log(parse_ctx_t *ctx, const char *pos);
ast_t *parse_assert(parse_ctx_t *ctx, const char *pos);
ast_t *parse_statement(parse_ctx_t *ctx, const char *pos);
ast_t *parse_update(parse_ctx_t *ctx, const char *pos);
