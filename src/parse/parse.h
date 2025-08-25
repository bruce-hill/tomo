// Parsing logic
#pragma once

#include "../ast.h"
#include "context.h"

ast_t *parse_expr_str(const char *str);

ast_t *parse_bool(parse_ctx_t *ctx, const char *pos);
ast_t *parse_expr(parse_ctx_t *ctx, const char *pos);
ast_t *parse_extended_expr(parse_ctx_t *ctx, const char *pos);
ast_t *parse_heap_alloc(parse_ctx_t *ctx, const char *pos);
ast_t *parse_negative(parse_ctx_t *ctx, const char *pos);
ast_t *parse_not(parse_ctx_t *ctx, const char *pos);
ast_t *parse_none(parse_ctx_t *ctx, const char *pos);
ast_t *parse_parens(parse_ctx_t *ctx, const char *pos);
ast_t *parse_reduction(parse_ctx_t *ctx, const char *pos);
ast_t *parse_stack_reference(parse_ctx_t *ctx, const char *pos);
ast_t *parse_term(parse_ctx_t *ctx, const char *pos);
ast_t *parse_term_no_suffix(parse_ctx_t *ctx, const char *pos);
ast_t *parse_var(parse_ctx_t *ctx, const char *pos);
ast_t *parse_deserialize(parse_ctx_t *ctx, const char *pos);
