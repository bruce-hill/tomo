// This file defines how to compile loops

#pragma once

#include "../ast.h"
#include "../environment.h"
#include "../stdlib/datatypes.h"

Text_t compile_for_loop(env_t *env, ast_t *ast);
Text_t compile_repeat(env_t *env, ast_t *ast);
Text_t compile_while(env_t *env, ast_t *ast);
Text_t compile_skip(env_t *env, ast_t *ast);
Text_t compile_stop(env_t *env, ast_t *ast);

// CoW-guard hoisting (see cow_hoist_env in loops.c): true if `var_ast` is a
// variable whose list header (data/stride/length) has been hoisted into
// locals by an enclosing loop. Access sites for such lists compile against
// the `cow_hoisted_local(name, ...)` locals instead of the list pointer.
bool is_cow_hoisted(env_t *env, ast_t *var_ast);
Text_t cow_hoisted_local(ast_t *var_ast, const char *field);
