// Command line argument parsing

#pragma once

#include <stdbool.h>

#include "datatypes.h"
#include "types.h"

typedef struct {
    const char *name;
    void *dest;
    const TypeInfo_t *type;
    char short_flag;
    bool required, populated;
} cli_arg_t;

void tomo_parse_args(int argc, char *argv[], Text_t usage, Text_t help, const char *version, int spec_len,
                     cli_arg_t spec[spec_len]);

bool pop_cli_flag(List_t *args, char short_flag, const char *flag, void *dest, const TypeInfo_t *type);
bool pop_cli_positional(List_t *args, const char *flag, void *dest, const TypeInfo_t *type, bool allow_dashes);
