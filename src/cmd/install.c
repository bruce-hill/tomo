// `tomo install`: install Tomo programs and packages into TOMO_PATH

#include <unistd.h>

#include "../config.h"
#include "../environment.h"
#include "../stdlib/lists.h"
#include "../util.h"
#include "common.h"
#include "commands.h"
#include "compilation.h"

static List_t paths = EMPTY_LIST;

static cli_arg_t install_spec[] = {
    {"paths", &paths, List$info(&Path$info), .positional = true, .metavar = "dir-or-file",
     .description = "the package directories or .tm programs to install (default: the current directory)"}, //
};

static int cmd_install(cli_command_t *self, List_t extra_args) {
    (void)self, (void)extra_args;
    if (cross_compiling) print_err("`tomo install` can't be combined with --target: the binary wouldn't run here");

    Path_t cwd = Path$current_dir();
    // Default: install the current directory as a package:
    if (paths.length == 0) List$insert(&paths, &cwd, I(0), sizeof(Path_t));

    List_t dirs = EMPTY_LIST, files = EMPTY_LIST;
    for (int64_t i = 0; i < (int64_t)paths.length; i++) {
        Path_t path = Path$resolved(*(Path_t *)(paths.data + i * paths.stride), cwd);
        if (Path$is_directory(path, true)) List$insert(&dirs, &path, I(0), sizeof(Path_t));
        else if (Path$has_extension(path, Text("tm"))) List$insert(&files, &path, I(0), sizeof(Path_t));
        else print_err("Not a package directory or .tm file: ", path);
    }

    // Package directories build+install in serial, each in its own forked
    // child to isolate build side effects:
    for (int64_t i = 0; i < (int64_t)dirs.length; i++) {
        Path_t dir = *(Path_t *)(dirs.data + i * dirs.stride);
        pid_t child = fork();
        if (child == 0) {
            build_package(dir);
            install_package(dir);
            fflush(NULL);
            _exit(0);
        }
        wait_for_child_success(child);
    }

    // Programs compile and install in parallel:
    files = normalize_tm_paths(files);
    struct child_s {
        struct child_s *next;
        pid_t pid;
    } *child_processes = NULL;
    for (int64_t i = 0; i < (int64_t)files.length; i++) {
        Path_t path = *(Path_t *)(files.data + i * files.stride);
        Path_t exe_path = Path$sibling(path, Path$base_name(get_exe_path(path)));
        pid_t child = fork();
        if (child == 0) {
            env_t *env = global_env(source_mapping);
            List_t object_files = EMPTY_LIST, extra_ldlibs = EMPTY_LIST;
            compile_files(env, List(path), &object_files, &extra_ldlibs, COMPILE_EXE);
            compile_executable(env, path, exe_path, object_files, extra_ldlibs);
            xsystem(as_owner, "mkdir -p '", TOMO_PATH, "/bin' '", TOMO_PATH, "/man/man1'");
            xsystem(as_owner, "cp -v '", exe_path, "' '", TOMO_PATH, "/bin/'");
            Path_t manpage_file = build_file(Path$with_extension(path, Text(".1"), true), "");
            xsystem(as_owner, "cp -v '", manpage_file, "' '", TOMO_PATH, "/man/man1/'");
            fflush(NULL);
            _exit(0);
        }
        child_processes = new (struct child_s, .next = child_processes, .pid = child);
    }
    for (; child_processes; child_processes = child_processes->next)
        wait_for_child_success(child_processes->pid);
    return 0;
}

cli_command_t install_command = {
    .name = "install",
    .summary = "Install Tomo programs and packages into TOMO_PATH",
    .description = "Directory arguments are built as packages and installed into\n"
                   "TOMO_PATH/lib; .tm file arguments are compiled to executables and\n"
                   "installed into TOMO_PATH/bin (with their manpages). With no arguments,\n"
                   "the current directory is installed as a package.",
    .spec_len = sizeof(install_spec) / sizeof(install_spec[0]),
    .spec = install_spec,
    .handler = cmd_install,
};
