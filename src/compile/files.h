// This file defines how to compile files

#pragma once

#include "../ast.h"
#include "../environment.h"
#include "../stdlib/datatypes.h"

Text_t compile_file(env_t *env, ast_t *ast);

// Generate a self-contained C translation unit that runs the `test`/`fails`
// blocks in `ast` (compiled against the module's public API in <name>.tm.h) via
// the test harness. `fails_compile` blocks are skipped (the driver handles them
// in-process). Returns EMPTY_TEXT and sets *out_count to 0 if there are none.
Text_t compile_test_runner(env_t *env, ast_t *ast, int64_t *out_count);
