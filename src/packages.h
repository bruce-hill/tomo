// Logic for getting information about and installing packages

#pragma once

#include <stdbool.h>

#include "ast.h"
#include "stdlib/datatypes.h"

Text_t get_package_name(Path_t lib_dir);
OptionalPath_t find_installed_package(Table_t *build_info, ast_t *use);
// The store entry (a .build/store/<digest>/ directory) containing the given path, if any:
OptionalPath_t package_store_entry(Path_t path);
// Vendor the named package into the current project's vendor/ directory:
void vendor_package(const char *name, bool editable);
// The pinned digest for `name` per the ini chain a use in `using_file` consults (parse-only), or NULL:
const char *find_pinned_digest(Path_t using_file, const char *name);
