#pragma once

#include <gc/cord.h>

#include "types.h"
#include "builtins/table.h"

typedef struct {
	table_t globals, types;
	table_t *locals;
	CORD imports;
	CORD typedefs;
	CORD typecode;
	CORD staticdefs;
	CORD funcs;
	CORD main;
} env_t;

typedef struct {
	CORD code;
	type_t *type;
} binding_t;

env_t *new_environment(void);

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
