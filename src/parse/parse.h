#pragma once

// Parsing logic

#include <setjmp.h>
#include <stdint.h>

#include "../ast.h"
#include "../stdlib/files.h"

typedef struct {
    file_t *file;
    jmp_buf *on_err;
    int64_t next_lambda_id;
} parse_ctx_t;

ast_t *parse_file(const char *path, jmp_buf *on_err);
ast_t *parse(const char *str);
ast_t *parse_expression(const char *str);
