// This file defines how to compile types and type info values

#pragma once

#include "../stdlib/datatypes.h"
#include "../types.h"

Text_t compile_type(type_t *t);
Text_t compile_type_info(type_t *t);
// Whether compile_type_info() can produce a TypeInfo for `t` (see types.c):
bool has_type_info(type_t *t);
