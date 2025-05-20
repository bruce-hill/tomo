#pragma once

#include <stdbool.h>

#include "ast.h"

typedef struct {
    const char *name, *version, *url, *git, *revision, *path;
} module_info_t;

module_info_t get_module_info(ast_t *use);
bool try_install_module(module_info_t mod);

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
