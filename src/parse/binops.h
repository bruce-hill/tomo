// Logic for parsing numbers
#pragma once

#include "../ast.h"
#include "context.h"

ast_e match_binary_operator(const char **pos);
// Parse the infix expression enclosed by the operator `outer_op`, absorbing
// every operator that binds tightly enough to belong to it (see absorbs_rhs()).
// Pass Unknown for an expression with no operator around it.
ast_t *parse_infix_expr(parse_ctx_t *ctx, const char *pos, ast_e outer_op);
