// `tomo build`: compile a Tomo program to a standalone executable

#include "../environment.h"
#include "../stdlib/lists.h"
#include "commands.h"
#include "common.h"
#include "compilation.h"

static OptionalPath_t file = NULL, output = NULL;

static cli_arg_t build_spec[] = {
    {"file", &file, &Path$info, .positional = true, .required = true, .metavar = "file.tm",
     .description = "the program to compile"}, //
    {"output", &output, &Path$info, .short_flag = 'o',
     .description = "where to put the executable (defaults to a sibling of the .tm file)"}, //
    OPTIMIZATION_FLAG, //
    VERBOSE_FLAG, //
    QUIET_FLAG, //
};

static int cmd_build(cli_command_t *self, List_t extra_args) {
    (void)self, (void)extra_args;
    set_default_logs(LOG_BUILD);
    // A built executable is a persistent artifact, so default to the highest
    // safe optimization level and the size-reducing link flags; -O overrides
    // the level:
    configure_codegen(opt_flag.tag == TEXT_NONE ? Text("3") : opt_flag, /*optimize=*/true);
    List_t files = normalize_tm_paths(List(file));
    Path_t path = *(Path_t *)files.data;

    Path_t exe_path;
    if (output != NULL) {
        exe_path = Path$resolved(output, Path$current_dir());
    } else {
        exe_path = get_exe_path(path);
        // Put the executable as a sibling to the .tm file instead of in the
        // .tomo directory. Cross-compiled executables get the target platform
        // as a suffix (foo.aarch64-macos) so they don't collide with the
        // native executable or each other:
        Text_t exe_name = Path$base_name(exe_path);
        if (cross_compiling) exe_name = Texts(exe_name, ".", target);
        exe_path = Path$sibling(path, exe_name);
    }

    env_t *env = global_env(source_mapping);
    List_t object_files = EMPTY_LIST, extra_ldlibs = EMPTY_LIST;
    compile_files(env, List(path), &object_files, &extra_ldlibs, COMPILE_EXE);
    compile_executable(env, path, exe_path, object_files, extra_ldlibs, /*embed_git_info=*/true);
    return 0;
}

cli_command_t build_command = {
    .name = "build",
    .summary = "Compile a Tomo program to a standalone executable",
    .spec_len = sizeof(build_spec) / sizeof(build_spec[0]),
    .spec = build_spec,
    .handler = cmd_build,
};
