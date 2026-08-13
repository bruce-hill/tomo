// `tomo transpile`: transpile a Tomo file to C and print the result

#include <unistd.h>

#include "../environment.h"
#include "../stdlib/bools.h"
#include "../stdlib/lists.h"
#include "common.h"
#include "commands.h"
#include "compilation.h"

static OptionalPath_t file = NULL;
static OptionalBool_t raw = false;

static cli_arg_t transpile_spec[] = {
    {"file", &file, &Path$info, .positional = true, .required = true, .metavar = "file.tm",
     .description = "the file to transpile"}, //
    {"raw", &raw, &Bool$info,
     .description = "print the raw generated code, without formatting or syntax highlighting"}, //
    VERBOSE_FLAG, //
};

static bool command_exists(const char *cmd) {
    int status = system(String("command -v ", cmd, " >/dev/null 2>&1"));
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

static int cmd_transpile(cli_command_t *self, List_t extra_args) {
    (void)self, (void)extra_args;
    set_default_logs(0);

    List_t files = normalize_tm_paths(List(file));
    Path_t path = *(Path_t *)files.data;
    env_t *env = global_env(source_mapping);
    List_t object_files = EMPTY_LIST, extra_ldlibs = EMPTY_LIST;
    compile_files(env, List(path), &object_files, &extra_ldlibs, COMPILE_C_FILES);

    Text_t out = EMPTY_TEXT;
    const char *extensions[] = {".h", ".c"};
    for (size_t i = 0; i < sizeof(extensions) / sizeof(extensions[0]); i++) {
        Path_t generated = build_file(path, extensions[i]);
        OptionalText_t contents = Path$read(generated);
        if (contents.tag == TEXT_NONE) print_err("Could not read the generated file: ", generated);
        out = Texts(out, i > 0 ? Text("\n") : EMPTY_TEXT, "// file: ",
                    Path$relative_to(generated, Path$current_dir()), "\n", contents);
    }

    // Unless --raw, format the output with clang-format and (on a TTY)
    // syntax-highlight it with bat, whichever of the two is installed:
    Text_t pipeline = EMPTY_TEXT;
    if (!raw) {
        if (command_exists("clang-format")) pipeline = Text("clang-format");
        if (isatty(STDOUT_FILENO) && command_exists("bat")) {
            Text_t base = Path$base_name(path);
            Text_t bat = Texts("bat -l c --file-name '", base, ".h/", base, ".c'");
            pipeline = pipeline.length > 0 ? Texts(pipeline, " | ", bat) : bat;
        }
    }
    if (pipeline.length == 0) {
        print_inline(out);
        return 0;
    }
    FILE *prog = run_cmd(pipeline);
    if (!prog) print_err("Failed to run: ", pipeline);
    Text$print(prog, out);
    int status = pclose(prog);
    return (WIFEXITED(status) && WEXITSTATUS(status) == 0) ? 0 : 1;
}

cli_command_t transpile_command = {
    .name = "transpile",
    .summary = "Transpile a Tomo file to C and print the result",
    .description = "The generated header and source (also written into the file's .tomo/\n"
                   "directory) are printed to stdout, each preceded by a \"// file:\" line.\n"
                   "The output is formatted with clang-format and syntax-highlighted with\n"
                   "bat when those tools are available (and stdout is a terminal).",
    .spec_len = sizeof(transpile_spec) / sizeof(transpile_spec[0]),
    .spec = transpile_spec,
    .handler = cmd_transpile,
};
