// `tomo transpile`: transpile a Tomo file to C and print the result

#include "../environment.h"
#include "../stdlib/lists.h"
#include "common.h"
#include "commands.h"
#include "compilation.h"

static OptionalPath_t file = NULL;

static cli_arg_t transpile_spec[] = {
    {"file", &file, &Path$info, .positional = true, .required = true, .metavar = "file.tm",
     .description = "the file to transpile"}, //
};

static int cmd_transpile(cli_command_t *self, List_t extra_args) {
    (void)self, (void)extra_args;
    // Don't print "Transpiled ..." progress messages into the output:
    if (!verbose) quiet = true;

    List_t files = normalize_tm_paths(List(file));
    Path_t path = *(Path_t *)files.data;
    env_t *env = global_env(source_mapping);
    List_t object_files = EMPTY_LIST, extra_ldlibs = EMPTY_LIST;
    compile_files(env, List(path), &object_files, &extra_ldlibs, COMPILE_C_FILES);

    const char *extensions[] = {".h", ".c"};
    for (size_t i = 0; i < sizeof(extensions) / sizeof(extensions[0]); i++) {
        Path_t generated = build_file(path, extensions[i]);
        OptionalText_t contents = Path$read(generated);
        if (contents.tag == TEXT_NONE) print_err("Could not read the generated file: ", generated);
        if (i > 0) print("");
        print("// file: ", Path$relative_to(generated, Path$current_dir()));
        print_inline(contents);
    }
    return 0;
}

cli_command_t transpile_command = {
    .name = "transpile",
    .summary = "Transpile a Tomo file to C and print the result",
    .description = "The generated header and source (also written into the file's .build/\n"
                   "directory) are printed to stdout, each preceded by a \"// file:\" line.",
    .spec_len = sizeof(transpile_spec) / sizeof(transpile_spec[0]),
    .spec = transpile_spec,
    .handler = cmd_transpile,
};
