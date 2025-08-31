// Command line argument parsing

#pragma once

#include <stdbool.h>

#include "datatypes.h"
#include "types.h"

typedef struct {
    const char *name;
    bool required;
    const TypeInfo_t *type;
    void *dest;
} cli_arg_t;

void _tomo_parse_args(int argc, char *argv[], Text_t usage, Text_t help, const char *version, int spec_len,
                      cli_arg_t spec[spec_len]);
#define tomo_parse_args(argc, argv, usage, help, version, ...)                                                         \
    _tomo_parse_args(argc, argv, usage, help, version, sizeof((cli_arg_t[]){__VA_ARGS__}) / sizeof(cli_arg_t),         \
                     (cli_arg_t[]){__VA_ARGS__})
