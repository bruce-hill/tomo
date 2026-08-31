// `tomo run` (and bare `tomo file.tm`): compile and run a Tomo program

#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

#include "../config.h"
#include "../environment.h"
#include "../stdlib/fail.h"
#include "../stdlib/lists.h"
#include "../stdlib/number.h"
#include "../stdlib/paths.h"
#include "../stdlib/profiling.h"
#include "../stdlib/text.h"
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
    INSTRUMENT_FLAG, //
    DEBUG_FLAG, //
    VERBOSE_FLAG, //
};

// Run the compiled program under a debugger, with Tomo's gdb integration
// loaded (see src/stdlib/tomo-gdb.py). `prog_args` is the program and its
// arguments, exactly as the non-debug path would have exec'd them. Like that
// path, this replaces the current process, so it only returns on failure.
static int exec_under_debugger(const char *prog_args[], int64_t num_prog_args) {
    const char *debugger = getenv("TOMO_DEBUGGER");
    if (debugger == NULL || debugger[0] == '\0') debugger = "gdb";

    Path_t script = Path$from_str(String(TOMO_PATH, "/lib/tomo@", TOMO_VERSION, "/tomo-gdb.py"));
    if (!Path$is_file(script, true))
        print_err("This Tomo installation is missing its debugger support file:\n", script);

    // A Tomo runtime error, whether `fail()`, a failed assertion, or an
    // out-of-bounds access, prints its report and exits, and an exit is not something a
    // debugger can stop on. TOMO_CORE_DUMP makes those raise SIGABRT after
    // printing instead, so the debugger takes over with the failing frame and
    // its variables still on the stack. An explicit setting is left alone.
    setenv("TOMO_CORE_DUMP", "yes", /*overwrite=*/0);

    const char *argv[8 + num_prog_args];
    int64_t n = 0;
    argv[n++] = debugger;
    argv[n++] = "-q"; // no gdb banner; tomo-gdb.py prints its own
    argv[n++] = "-x";
    argv[n++] = Path$as_c_string(script);
    // Start the program immediately. `tomo-run` (defined by the script) is
    // `run` plus one rule: if the program finishes on its own, leave the
    // debugger with the program's exit status instead of sitting at a prompt
    // with nothing left to debug.
    argv[n++] = "-ex";
    argv[n++] = "tomo-run";
    argv[n++] = "--args";
    for (int64_t i = 0; i < num_prog_args; i++)
        argv[n++] = prog_args[i];
    argv[n] = NULL;

    fflush(NULL);
    execvp(debugger, (char **)argv);
    print_err("Could not run `", debugger,
              "`.\n`tomo run --debug` needs gdb installed, or $TOMO_DEBUGGER "
              "pointing at a gdb-compatible debugger.");
    return 1;
}

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
    // nearly the whole optimization pipeline. Measured on examples/learnxiny.tm,
    // `zig cc -O1` costs 232ms per object versus 231ms at -O3 and 127ms at -O0.
    // An explicit -O overrides the level but keeps the fast link path.
    configure_codegen(opt_flag.tag == TEXT_NONE ? Text("0") : opt_flag, /*optimize=*/false);

    List_t files = normalize_tm_paths(List(path));
    path = *(Path_t *)files.data;
    Path_t exe_path = get_exe_path(path);

    env_t *env;
    TOMO_PROFILE_SPAN("global env", env = global_env(source_mapping, instrument, debugging));
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
    tomo_profile_report();
    if (debugging) return exec_under_debugger(prog_args, 1 + (int64_t)extra_args.length);
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
            print(tomo_cli.root.help);
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

// The command handler for `tomo run file.tm`. With no file, run_file() falls
// through to the stdin-program / editor-scratch behavior:
static int cmd_run(cli_command_t *self, List_t extra_args) {
    (void)self;
    return run_file(extra_args);
}

// `tomo`'s root handler: what runs when the first word isn't a command name.
// The root takes the same arguments `tomo run` does, so `tomo file.tm` means
// `tomo run file.tm`. One bit of processing first: `tomo bulid` parses "bulid"
// as the file to run, and a file that doesn't exist and has no `.` in its name
// doesn't look like a filename at all, so it's much more likely a mistyped
// command name.
int tomo_main(cli_command_t *self, List_t extra_args) {
    if (file != NULL && !Path$exists(file) && !Text$has(Path$base_name(file), Text("."))) {
        Text_t name = Path$base_name(file);
        List_t names = EMPTY_LIST;
        Text_t listing = EMPTY_TEXT;
        for (int i = 0; i < tomo_cli.root.num_children; i++) {
            Text_t command = Text$from_str(tomo_cli.root.children[i]->name);
            List$insert(&names, &command, I(0), sizeof(Text_t));
            listing = Texts(listing, i > 0 ? Text(", ") : EMPTY_TEXT, command);
            // An alias is a real name for the command, so it belongs among the
            // suggestions, but not in the listing of commands:
            if (tomo_cli.root.children[i]->alias) {
                Text_t alias = Text$from_str(tomo_cli.root.children[i]->alias);
                List$insert(&names, &alias, I(0), sizeof(Text_t));
            }
        }
        OptionalText_t nearest = Text$nearest(name, names, NUMBER_SMALL(3, 5) /* 0.6 */);
        cli_style_t style = tomo_cli_style();
        print_err("There's no command or file called ", style.bold, name, style.reset,
                  nearest.tag == TEXT_NONE ? EMPTY_TEXT
                                           : Texts("\nDid you mean ", style.bold, nearest, style.reset, "?"),
                  "\nAvailable commands: ", listing, "\nSee `tomo --help` for full usage");
    }
    return cmd_run(self, extra_args);
}

// `run`'s usage and description are written by hand (the escape hatch from
// autogeneration) to document the "--" separator. They're styled here rather
// than in the struct below because the palette isn't known until tomo_init()
// has decided whether the output is colored:
void style_run_command(void) {
    cli_style_t style = tomo_cli_style();
    run_command.usage = Texts(style.usage, "Usage:", style.reset, " tomo run ", style.bold, "file.tm", style.reset,
                              " [", style.bold, "--", style.reset, " program args...]");
    run_command.description = String("Anything after a ", style.bold, "--", style.reset,
                                     " is passed to the program as its own arguments.\n"
                                     "The command name is optional: `tomo file.tm` does the same thing.");
}

cli_command_t run_command = {
    .name = "run",
    .summary = "Compile and run a Tomo program",
    .spec_len = sizeof(run_spec) / sizeof(run_spec[0]),
    .spec = run_spec,
    .handler = cmd_run,
};
