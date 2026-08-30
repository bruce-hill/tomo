// This file defines how to compile integers

#pragma once

#include <gmp.h>

#include "../ast.h"
#include "../environment.h"
#include "../types.h"

void mpz_init_int(mpz_t out, Int_t i);
Text_t compile_int_to_type(env_t *env, ast_t *ast, type_t *target);
Text_t compile_int(ast_t *ast);
