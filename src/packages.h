// Logic for getting information about and installing packages

#pragma once

#include <stdbool.h>

#include "ast.h"
#include "stdlib/datatypes.h"

Text_t get_package_name(Path_t lib_dir);
OptionalPath_t find_installed_package(Table_t *build_info, ast_t *use);
