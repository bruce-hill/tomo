// A context parameter that gets passed around during parsing.
#pragma once

#include <setjmp.h>
#include <stdint.h>

#include "../stdlib/datatypes.h"
#include "../stdlib/files.h"
#include "../stdlib/types.h"

extern const TypeInfo_t *parse_comments_info;

// A parse failure, captured as a value so the caller decides whether it's
// worth reporting. `message` is GC-allocated and the span points into the
// file's own text, so the error stays valid after the parse unwinds.
typedef struct {
    file_t *file;
    const char *start, *end;
    const char *message;
} parse_error_t;

// Render a parse error to stderr in the compiler's standard format.
void print_parse_error(parse_error_t err);

typedef struct {
    file_t *file;
    // Where to unwind to when parsing fails, if anywhere.
    jmp_buf *on_err;
    // Where to record a parse failure, if the caller wants it as a value.
    parse_error_t *error;
    int64_t next_lambda_id;
    Table_t comments; // Map of <start pos> -> <end pos>
} parse_ctx_t;
