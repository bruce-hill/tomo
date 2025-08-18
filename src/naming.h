#pragma once

// Compilation environments

#include "environment.h"
#include "stdlib/datatypes.h"

extern const Text_t SEP;
extern const Text_t ID_PREFIX;
extern const Text_t INTERNAL_PREFIX;

#define INTERNAL_ID(id) Textヽconcat(INTERNAL_PREFIX, _Generic(id, const char*: Textヽfrom_str, char*: Textヽfrom_str, Text_t: Text_from_text)(id))
#define USER_ID(id) Textヽconcat(ID_PREFIX, _Generic(id, const char*: Textヽfrom_str, char*: Textヽfrom_str, Text_t: Text_from_text)(id))

Text_t valid_c_name(const char *name);
Text_t namespace_name(env_t *env, namespace_t *ns, Text_t name);
Text_t get_id_suffix(const char *filename);

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
