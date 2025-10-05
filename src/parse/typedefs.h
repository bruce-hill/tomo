// Parsing logic for type definitions
#pragma once

#include "../ast.h"
#include "context.h"

ast_t *parse_lang_def(parse_ctx_t *ctx, const char *pos);
ast_t *parse_enum_def(parse_ctx_t *ctx, const char *pos);
ast_t *parse_struct_def(parse_ctx_t *ctx, const char *pos);
ast_t *parse_namespace(parse_ctx_t *ctx, const char *pos);
