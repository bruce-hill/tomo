// Logic for getting information about and installing modules

#pragma once

#include <stdbool.h>

#include "ast.h"

typedef struct {
    const char *name, *version, *url, *git, *revision, *path;
} module_info_t;

Text_t get_library_name(Path_t lib_dir);
const char *get_library_version(Path_t lib_dir);
module_info_t get_used_module_info(ast_t *use);
bool install_from_modules_ini(Path_t ini_file, bool ask_confirmation);
bool try_install_module(module_info_t mod, bool ask_confirmation);
