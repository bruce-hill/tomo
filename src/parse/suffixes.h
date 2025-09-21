// Logic for parsing various suffixes that can go after an expression
#pragma once

#include "../ast.h"
#include "context.h"

ast_t *parse_comprehension_suffix(parse_ctx_t *ctx, ast_t *expr);
ast_t *parse_field_suffix(parse_ctx_t *ctx, ast_t *lhs);
ast_t *parse_fncall_suffix(parse_ctx_t *ctx, ast_t *fn);
ast_t *parse_index_suffix(parse_ctx_t *ctx, ast_t *lhs);
ast_t *parse_method_call_suffix(parse_ctx_t *ctx, ast_t *self);
ast_t *parse_non_optional_suffix(parse_ctx_t *ctx, ast_t *lhs);
ast_t *parse_optional_conditional_suffix(parse_ctx_t *ctx, ast_t *stmt);
