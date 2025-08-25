// A context parameter that gets passed around during parsing.
#pragma once

#include <setjmp.h>
#include <stdint.h>

#include "../stdlib/files.h"

typedef struct {
    file_t *file;
    jmp_buf *on_err;
    int64_t next_lambda_id;
} parse_ctx_t;
