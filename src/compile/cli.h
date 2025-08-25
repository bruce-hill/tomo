// This file defines how to compile CLI argument parsing

#pragma once

#include "../environment.h"
#include "../stdlib/datatypes.h"
#include "../types.h"

Text_t compile_cli_arg_call(env_t *env, Text_t fn_name, type_t *fn_type, const char *version);
