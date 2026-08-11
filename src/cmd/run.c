// `tomo run` (and bare `tomo file.tm`): compile and run Tomo programs

#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

#include "../config.h"
#include "../environment.h"
#include "../stdlib/fail.h"
#include "../stdlib/lists.h"
#include "../util.h"
#include "common.h"
#include "commands.h"
#include "compilation.h"

static List_t files = EMPTY_LIST;

static cli_arg_t run_spec[] = {
    {"files", &files, List$info(&Path$info), .positional = true, .metavar = "file.tm",
     .description = "the programs to compile and run"}, //
};

// Run the parsed `files` (compiled in parallel, executed in serial), passing
// extra_args (the raw argv tail after "--") to each program:
static int run_parsed_files(List_t extra_args) {
    // When running files, don't print "compiled to ..." messages unless --verbose:
    if (!verbose) quiet = true;

    files = normalize_tm_paths(files);

    if (files.length == 0) {
        // `tomo --target <platform> --install-target` with nothing else to do:
        if (install_target) return 0;

        // Piping a program into Tomo
        if (!isatty(STDIN_FILENO)) {
            Path_t parent = Path$expand_home(Path$from_str(String("~/.local/state/tomo/tomo@", TOMO_VERSION)));
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
            List$insert(&files, &path, I(0), sizeof(path));
            goto run_files;
        }

        // If not on a TTY, then just print help and exit
        if (!isatty(STDOUT_FILENO)) {
            print(tomo_cli.help);
            return 0;
        }

        Path_t path = Path$from_str(String("~/.local/state/tomo/tomo@", TOMO_VERSION, "/run.tm"));
        path = Path$expand_home(path);
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
        List$insert(&files, &path, I(0), sizeof(path));
        const char *editor = getenv("EDITOR");
        if (!editor || editor[0] == '\0') editor = "vim";
        int status = system(String(editor, " ", path));
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) return 1;
    }

run_files:;

    if (cross_compiling && files.length > 0)
        print_err("Programs cross-compiled with --target can't run on this machine; "
                  "use `tomo build` to build them instead");

    struct child_s {
        struct child_s *next;
        pid_t pid;
    } *child_processes = NULL;

    // Compile runnable files in parallel, then execute in serial:
    for (int64_t i = 0; i < (int64_t)files.length; i++) {
        Path_t path = *(Path_t *)(files.data + i * files.stride);
        Path_t exe_path = get_exe_path(path);
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

    // After parallel compilation, do serial execution:
    for (int64_t i = 0; i < (int64_t)files.length; i++) {
        Path_t path = *(Path_t *)(files.data + i * files.stride);
        Path_t exe_path = get_exe_path(path);
        // Don't fork for the last program
        pid_t child = i == (int64_t)files.length - 1 ? 0 : fork();
        if (child == 0) {
            const char *prog_args[1 + extra_args.length + 1];
            Path_t relative_exe = Path$relative_to(exe_path, Path$current_dir());
            prog_args[0] = (char *)Path$as_c_string(relative_exe);
            for (int64_t j = 0; j < (int64_t)extra_args.length; j++)
                prog_args[j + 1] = *(const char **)(extra_args.data + j * extra_args.stride);
            prog_args[1 + extra_args.length] = NULL;
            execv(prog_args[0], (char **)prog_args);
            print_err("Could not execute program: ", prog_args[0]);
        }
        wait_for_child_success(child);
    }

    return 0;
}

static int cmd_run(cli_command_t *self, List_t extra_args) {
    (void)self;
    return run_parsed_files(extra_args);
}

int run_fallback(List_t args, List_t extra_args) {
    cli_help_info_t info = {
        .usage = run_command.usage, .help = run_command.help, .help_short = 'h', .strict_positionals = true};
    tomo_parse_arg_list(args, info, sizeof(run_spec) / sizeof(run_spec[0]), run_spec);
    return run_parsed_files(extra_args);
}

cli_command_t run_command = {
    .name = "run",
    .summary = "Compile and run Tomo programs",
    // Explicit usage (the escape hatch from autogeneration) to document the
    // "--" separator for program arguments:
    .usage = Text("\x1b[93;4;1mUsage:\x1b[m tomo run \x1b[1mfile.tm...\x1b[m [\x1b[1m--\x1b[m program args...]"),
    .description = "Each file is compiled (in parallel) and then run (in serial). Anything\n"
                   "after a \x1b[1m--\x1b[m is passed to the programs as their own arguments.\n"
                   "The command name is optional: `tomo file.tm` does the same thing.",
    .spec_len = sizeof(run_spec) / sizeof(run_spec[0]),
    .spec = run_spec,
    .handler = cmd_run,
};
