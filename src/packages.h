// Logic for getting information about and installing packages

#pragma once

#include <stdbool.h>

#include "ast.h"

Text_t get_package_name(Path_t lib_dir);
OptionalPath_t find_installed_package(ast_t *use);
