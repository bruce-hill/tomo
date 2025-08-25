// A context parameter that gets passed around during parsing.
#pragma once

#include <setjmp.h>
#include <stdint.h>

#include "../stdlib/datatypes.h"
#include "../stdlib/files.h"
#include "../stdlib/types.h"

const TypeInfo_t *parse_comments_info;

typedef struct {
    file_t *file;
    jmp_buf *on_err;
    int64_t next_lambda_id;
    Table_t comments; // Map of <start pos> -> <end pos>
} parse_ctx_t;
