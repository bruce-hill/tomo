#include "../ast.h"
#include "../environment.h"
#include "../stdlib/datatypes.h"

Text_t compile_file(env_t *env, ast_t *ast);
Text_t compile_file_header(env_t *env, Path_t header_path, ast_t *ast);
