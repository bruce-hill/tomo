// `tomo transpile`: transpile Tomo files to C without compiling

#include "../environment.h"
#include "../stdlib/lists.h"
#include "common.h"
#include "commands.h"
#include "compilation.h"

static List_t files = EMPTY_LIST;

static cli_arg_t transpile_spec[] = {
    {"files", &files, List$info(&Path$info), .positional = true, .required = true, .metavar = "file.tm",
     .description = "the files to transpile"}, //
};

static int cmd_transpile(cli_command_t *self, List_t extra_args) {
    (void)extra_args;
    files = normalize_tm_paths(files);
    if (files.length == 0) print_err("No files provided to transpile!\n", self->usage);
    env_t *env = global_env(source_mapping);
    List_t object_files = EMPTY_LIST, extra_ldlibs = EMPTY_LIST;
    compile_files(env, files, &object_files, &extra_ldlibs, COMPILE_C_FILES);
    return 0;
}

cli_command_t transpile_command = {
    .name = "transpile",
    .summary = "Transpile Tomo files to C without compiling",
    .description = "The generated .h/.c files go into each file's .build/ directory.",
    .spec_len = sizeof(transpile_spec) / sizeof(transpile_spec[0]),
    .spec = transpile_spec,
    .handler = cmd_transpile,
};
