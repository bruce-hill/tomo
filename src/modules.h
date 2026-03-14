// Logic for getting information about and installing modules

#pragma once

#include <stdbool.h>

#include "ast.h"

typedef struct {
    const char *name;
    Table_t info;
} module_info_t;

Text_t get_library_name(Path_t lib_dir);
OptionalPath_t find_installed_module(ast_t *use);
