// `tomo package`: build Tomo packages into static archives

#include <unistd.h>

#include "../stdlib/lists.h"
#include "../stdlib/text.h"
#include "common.h"
#include "commands.h"
#include "compilation.h"

static List_t paths = EMPTY_LIST;
static OptionalPath_t output = NULL;

static cli_arg_t package_spec[] = {
    {"paths", &paths, List$info(&Path$info), .positional = true, .metavar = "dir-or-file",
     .description = "the package directories (or .tm files) to build (default: the current directory)"}, //
    {"output", &output, &Path$info, .short_flag = 'o', .metavar = "libname.a",
     .description = "where to put the archive (single package only; defaults to package.a in the package "
                    "directory)"}, //
    VERBOSE_FLAG, //
    QUIET_FLAG,   //
};

static int cmd_package(cli_command_t *self, List_t extra_args) {
    (void)self, (void)extra_args;
    set_default_logs(LOG_BUILD);
    Path_t cwd = Path$current_dir();
    // Default: package the current directory:
    if (paths.length == 0) List$insert(&paths, &cwd, I(0), sizeof(Path_t));

    // Positionals are package directories or explicit .tm files (which are
    // grouped into a single archive):
    List_t dirs = EMPTY_LIST, files = EMPTY_LIST;
    for (int64_t i = 0; i < (int64_t)paths.length; i++) {
        Path_t path = Path$resolved(*(Path_t *)(paths.data + i * paths.stride), cwd);
        if (Path$is_directory(path, true)) List$insert(&dirs, &path, I(0), sizeof(Path_t));
        else if (Path$has_extension(path, Text("tm"))) List$insert(&files, &path, I(0), sizeof(Path_t));
        else print_err("Not a package directory or .tm file: ", path);
    }

    if (output != NULL && dirs.length + (files.length > 0 ? 1 : 0) > 1)
        print_err("--output can only be used when building a single package");

    // Fork a child process to build each package to prevent cross-contamination
    // of side effects when building one package from affecting another package.
    // This *could* be done in parallel, but there may be some dependency issues.
    for (int64_t i = 0; i < (int64_t)dirs.length; i++) {
        Path_t dir = *(Path_t *)(dirs.data + i * dirs.stride);
        pid_t child = fork();
        if (child == 0) {
            if (output != NULL) {
                List_t tm_files = Path$glob(Path$child(dir, Text("[!._0-9]*.tm")));
                build_package_archive(dir, tm_files, Path$resolved(output, cwd));
            } else {
                build_package(dir);
            }
            fflush(NULL);
            _exit(0);
        }
        wait_for_child_success(child);
    }

    if (files.length > 0) {
        files = normalize_tm_paths(files);
        Path_t pkg_dir = Path$parent(*(Path_t *)files.data);
        Path_t archive = output != NULL ? Path$resolved(output, cwd) : Path$child(pkg_dir, Text("package.a"));
        pid_t child = fork();
        if (child == 0) {
            build_package_archive(pkg_dir, files, archive);
            fflush(NULL);
            _exit(0);
        }
        wait_for_child_success(child);
    }
    return 0;
}

cli_command_t package_command = {
    .name = "package",
    .summary = "Build Tomo packages into static archives",
    .description = "With no arguments, the current directory is built as a package: every\n"
                   ".tm file not starting with an underscore (or dot or digit) is compiled\n"
                   "and archived into package.a. Directory arguments are each built the\n"
                   "same way; .tm file arguments are compiled into a single archive.",
    .spec_len = sizeof(package_spec) / sizeof(package_spec[0]),
    .spec = package_spec,
    .handler = cmd_package,
};
