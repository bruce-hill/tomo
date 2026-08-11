// `tomo build`: compile Tomo programs to standalone executables

#include <stdlib.h>
#include <unistd.h>

#include "../environment.h"
#include "../stdlib/lists.h"
#include "../util.h"
#include "common.h"
#include "commands.h"
#include "compilation.h"

static List_t files = EMPTY_LIST;
static OptionalPath_t output = NULL;

static cli_arg_t build_spec[] = {
    {"files", &files, List$info(&Path$info), .positional = true, .required = true, .metavar = "file.tm",
     .description = "the programs to compile"}, //
    {"output", &output, &Path$info, .short_flag = 'o',
     .description = "where to put the executable (single file only; defaults to a sibling of the .tm file)"}, //
};

static int cmd_build(cli_command_t *self, List_t extra_args) {
    (void)extra_args;
    files = normalize_tm_paths(files);
    if (files.length == 0) print_err("No files provided to build!\n", self->usage);
    if (output != NULL && files.length != 1)
        print_err("--output can only be used when building a single executable");

    struct child_s {
        struct child_s *next;
        pid_t pid;
    } *child_processes = NULL;

    // Compile executables in parallel:
    for (int64_t i = 0; i < (int64_t)files.length; i++) {
        Path_t path = *(Path_t *)(files.data + i * files.stride);

        Path_t exe_path;
        if (output != NULL) {
            exe_path = Path$resolved(output, Path$current_dir());
        } else {
            exe_path = get_exe_path(path);
            // Put executable as a sibling to the .tm file instead of in the .build
            // directory. Cross-compiled executables get the target platform as a
            // suffix (foo.aarch64-macos) so they don't collide with the native
            // executable or each other:
            Text_t exe_name = Path$base_name(exe_path);
            if (cross_compiling) exe_name = Texts(exe_name, ".", target);
            exe_path = Path$sibling(path, exe_name);
        }
        pid_t child = fork();
        if (child == 0) {
            env_t *env = global_env(source_mapping);
            List_t object_files = EMPTY_LIST, extra_ldlibs = EMPTY_LIST;
            compile_files(env, List(path), &object_files, &extra_ldlibs, COMPILE_EXE);
            compile_executable(env, path, exe_path, object_files, extra_ldlibs);
            fflush(NULL);
            _exit(0);
        }

        child_processes = new (struct child_s, .next = child_processes, .pid = child);
    }

    for (; child_processes; child_processes = child_processes->next)
        wait_for_child_success(child_processes->pid);

    return 0;
}

cli_command_t build_command = {
    .name = "build",
    .summary = "Compile Tomo programs to standalone executables",
    .spec_len = sizeof(build_spec) / sizeof(build_spec[0]),
    .spec = build_spec,
    .handler = cmd_build,
};
