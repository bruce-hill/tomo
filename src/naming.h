#pragma once

// Compilation environments

#include "environment.h"
#include "stdlib/datatypes.h"

Text_t valid_c_name(const char *name);
Text_t namespace_name(env_t *env, namespace_t *ns, Text_t name);
Text_t get_id_suffix(const char *filename);

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
