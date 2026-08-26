// `tomo run` (and bare `tomo file.tm`): compile and run a Tomo program

#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

#include "../config.h"
#include "../environment.h"
#include "../profile.h"
#include "../stdlib/fail.h"
#include "../stdlib/lists.h"
#include "commands.h"
#include "common.h"
#include "compilation.h"

static OptionalPath_t file = NULL;

// The file is optional so both `tomo run` and the bare-`tomo` shim can fall
// through to the stdin-program / editor-scratch behavior when none is given:
static cli_arg_t run_spec[] = {
    {"file", &file, &Path$info, .positional = true, .metavar = "file.tm",
     .description = "the program to compile and run"}, //
    OPTIMIZATION_FLAG, //
    VERBOSE_FLAG, //
};

// Compile `path` and exec it, passing extra_args (the raw argv tail after
// "--") as the program's arguments. Shared by `tomo run`, the bare-`tomo`
// fallback, and `tomo eval` (which points it at a generated file):
int compile_and_exec(Path_t path, List_t extra_args) {
    if (cross_compiling)
        print_err("Programs cross-compiled with --target can't run on this machine; "
                  "use `tomo build` to build them instead");

    // run/eval compile for a quick single execution: default to -O0 and skip
    // the size-reducing link flags (fast to link), since this executable is
    // discarded after one run. -O0 rather than -O1 because clang's -O1 runs
    // nearly the whole optimization pipeline -- measured on examples/learnxiny.tm,
    // `zig cc -O1` costs 232ms per object versus 231ms at -O3 and 127ms at -O0.
    // An explicit -O overrides the level but keeps the fast link path.
    configure_codegen(opt_flag.tag == TEXT_NONE ? Text("0") : opt_flag, /*optimize=*/false);

    List_t files = normalize_tm_paths(List(path));
    path = *(Path_t *)files.data;
    Path_t exe_path = get_exe_path(path);

    env_t *env;
    PROFILE("global env", env = global_env(source_mapping));
    List_t object_files = EMPTY_LIST, extra_ldlibs = EMPTY_LIST;
    compile_files(env, List(path), &object_files, &extra_ldlibs, COMPILE_EXE);
    // This executable is run once and discarded, so don't spend git subprocesses
    // gathering provenance metadata to embed in it:
    compile_executable(env, path, exe_path, object_files, extra_ldlibs, /*embed_git_info=*/false);

    const char *prog_args[1 + extra_args.length + 1];
    Path_t relative_exe = Path$relative_to(exe_path, Path$current_dir());
    prog_args[0] = (char *)Path$as_c_string(relative_exe);
    for (int64_t j = 0; j < (int64_t)extra_args.length; j++)
        prog_args[j + 1] = *(const char **)(extra_args.data + j * extra_args.stride);
    prog_args[1 + extra_args.length] = NULL;
    fflush(NULL);
    // Don't leak the bundled zig's cache location into the program: if the
    // program (or anything it spawns) runs the user's own zig, it should use
    // that zig's normal cache, not Tomo's.
    if (!zig_cache_dir_from_env) unsetenv("ZIG_GLOBAL_CACHE_DIR");
    // execv replaces this process, so print the profile now (atexit won't fire):
    profile_report();
    execv(prog_args[0], (char **)prog_args);
    print_err("Could not execute program: ", prog_args[0]);
    return 1;
}

// Compile `file` and exec it, passing extra_args (the raw argv tail after
// "--") as the program's arguments:
static int run_file(List_t extra_args) {
    set_default_logs(0);

    if (file == NULL) {
        // `tomo --target <platform> --install-target` with nothing else to do:
        if (install_target) return 0;

        // Piping a program into Tomo
        if (!isatty(STDIN_FILENO)) {
            Path_t parent = Path$child(xdg_tomo_dir("XDG_STATE_HOME", "~/.local/state"), Texts("tomo@", TOMO_VERSION));
            Path$create_directory(parent, 0755, true);
            Path_t path = Path$child(parent, Text("stdin.tm"));

            int fd = open(path, O_CREAT | O_TRUNC | O_WRONLY, 0644);
            if (fd < 0) {
                fail("Could not open temporary file for writing at ", path);
            }
            char buf[256];
            for (ssize_t len; (len = read(STDIN_FILENO, buf, sizeof(buf))) > 0;) {
                write(fd, buf, (size_t)len);
            }
            if (close(fd) != 0) fail("Could not close file: ", path);
            file = path;
        } else if (!isatty(STDOUT_FILENO)) {
            // Not a TTY on either end: just print the help and exit
            print(tomo_cli.help);
            return 0;
        } else {
            Path_t path =
                Path$child(Path$child(xdg_tomo_dir("XDG_STATE_HOME", "~/.local/state"), Texts("tomo@", TOMO_VERSION)),
                           Text("run.tm"));
            Path$create_directory(Path$parent(path), 0755, true);
            if (!Path$exists(path)) {
                Path$write(path,
                           Text("# This is a handy Tomo REPL-like runner\n" //
                                "# Normally you would run `tomo ./file.tm` to run a script\n" //
                                "# See `tomo --help` for full usage\n" //
                                "\n" //
                                "func main()\n" //
                                "    # Put your code here:\n" //
                                "    say(\"Hello world!\")\n" //
                                "\n" //
                                "# Save and exit to run\n"),
                           0644);
            }
            const char *editor = getenv("EDITOR");
            if (!editor || editor[0] == '\0') editor = "vim";
            int status = system(String(editor, " ", path));
            if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) return 1;
            file = path;
        }
    }

    return compile_and_exec(file, extra_args);
}

// The command handler for both `tomo run file.tm` and the bare-`tomo` shim
// (see the CLI's default_command). With no file, run_file() falls through to
// the stdin-program / editor-scratch behavior:
static int cmd_run(cli_command_t *self, List_t extra_args) {
    (void)self;
    return run_file(extra_args);
}

cli_command_t run_command = {
    .name = "run",
    .summary = "Compile and run a Tomo program",
    // Explicit usage (the escape hatch from autogeneration) to document the
    // "--" separator for program arguments:
    .usage = Text("\x1b[93;4;1mUsage:\x1b[m tomo run \x1b[1mfile.tm\x1b[m [\x1b[1m--\x1b[m program args...]"),
    .description = "Anything after a \x1b[1m--\x1b[m is passed to the program as its own arguments.\n"
                   "The command name is optional: `tomo file.tm` does the same thing.",
    .spec_len = sizeof(run_spec) / sizeof(run_spec[0]),
    .spec = run_spec,
    .handler = cmd_run,
};
