// This file defines parsing logic for types
#pragma once

#include "../ast.h"
#include "context.h"

type_ast_t *parse_type_str(const char *str);

type_ast_t *parse_list_type(parse_ctx_t *ctx, const char *pos);
type_ast_t *parse_func_type(parse_ctx_t *ctx, const char *pos);
type_ast_t *parse_non_optional_type(parse_ctx_t *ctx, const char *pos);
type_ast_t *parse_pointer_type(parse_ctx_t *ctx, const char *pos);
type_ast_t *parse_set_type(parse_ctx_t *ctx, const char *pos);
type_ast_t *parse_table_type(parse_ctx_t *ctx, const char *pos);
type_ast_t *parse_type(parse_ctx_t *ctx, const char *pos);
type_ast_t *parse_type_name(parse_ctx_t *ctx, const char *pos);
