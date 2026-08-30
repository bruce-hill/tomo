// Logic for parsing numbers
#pragma once

#include "../ast.h"
#include "context.h"

ast_t *parse_int(parse_ctx_t *ctx, const char *pos);
ast_t *parse_num(parse_ctx_t *ctx, const char *pos);
// Negate a numeric literal in place of wrapping it in a Negative node, giving
// the result a span that starts at `start` (the `-`). Returns NULL if `literal`
// isn't a literal that a sign can fold into.
ast_t *negate_literal(parse_ctx_t *ctx, const char *start, ast_t *literal);
