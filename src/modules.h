#pragma once

#include <stdbool.h>

#include "ast.h"

typedef struct {
    const char *name, *version, *url, *git, *revision, *path;
} module_info_t;

module_info_t get_module_info(ast_t *use);
bool try_install_module(module_info_t mod);
