// Logic for parsing a whole file
#pragma once

#include <setjmp.h>
#include <stdint.h>

#include "../ast.h"
#include "context.h"

ast_t *parse_file_str(const char *str);
// Parse a whole file (or a "<name>source" virtual file). If `error_out` is
// given, a parse failure is stored there and NULL is returned, leaving it to
// the caller to decide whether the failure is worth reporting; otherwise a
// parse failure is reported and exits. NULL is also returned when the file
// simply can't be read, so a caller telling the two apart should zero out its
// parse_error_t first and check `.message`.
ast_t *parse_file(const char *path, parse_error_t *error_out);

ast_t *parse_file_body(parse_ctx_t *ctx, const char *pos);
ast_t *parse_use(parse_ctx_t *ctx, const char *pos);
