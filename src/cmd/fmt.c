// `tomo fmt`: format Tomo source code, or check that formatting is faithful.

#include <gc.h>
#include <setjmp.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "../ast.h"
#include "../formatter/formatter.h"
#include "../parse/files.h"
#include "../stdlib/bools.h"
#include "../stdlib/files.h"
#include "../stdlib/lists.h"
#include "../stdlib/report.h"
#include "../stdlib/text.h"
#include "commands.h"
#include "common.h"

static List_t files = EMPTY_LIST;
static OptionalBool_t in_place = false;
static OptionalBool_t check = false;
static int32_t jobs = 0; // 0 = unset; see check_all() below

static cli_arg_t fmt_spec[] = {
    {"files", &files, List$info(&Path$info), .positional = true, .required = true, .metavar = "file.tm",
     .description = "the files to format"},
    {"in-place", &in_place, &Bool$info, .short_flag = 'i',
     .description = "rewrite the files instead of printing to stdout"},
    {"check", &check, &Bool$info,
     .description = "don't write anything; check that formatting each file is faithful and settled"},
    {"jobs", &jobs, &Int32$info, .short_flag = 'j', .metavar = "n",
     .description = "how many files to check at once (default: one per CPU)"},
    QUIET_FLAG,
    VERBOSE_FLAG,
};

// Anonymous types are named after their byte offset in the file, which
// reformatting legitimately moves, so compare trees with those offsets elided
// ("$1234" -> "$").
static const char *elide_offsets(const char *sexp) {
    char *out = GC_MALLOC_ATOMIC(strlen(sexp) + 1);
    char *w = out;
    for (const char *p = sexp; *p; p++) {
        *w++ = *p;
        if (*p == '$')
            while (p[1] >= '0' && p[1] <= '9')
                p += 1;
    }
    *w = '\0';
    return out;
}

// Stages of the round-trip, so the longjmp handler can say which one failed:
typedef enum {
    STAGE_PARSE,
    STAGE_FORMAT,
    STAGE_REPARSE,
    STAGE_COMPARE,
    STAGE_IDEMPOTENT,
} check_stage_t;

static const char *stage_problem(check_stage_t stage) {
    switch (stage) {
    case STAGE_PARSE: return "parse failed";
    case STAGE_FORMAT: return "formatting failed";
    case STAGE_REPARSE: return "formatted source doesn't parse";
    case STAGE_COMPARE: return "formatting changed the syntax tree";
    case STAGE_IDEMPOTENT: return "formatting is not idempotent";
    default: return "formatting check failed";
    }
}

static void report_problem(check_stage_t stage, Path_t path) {
    if (USE_COLOR) fprint(stderr, "\x1b[31;1m", stage_problem(stage), "\x1b[m: ", path);
    else fprint(stderr, stage_problem(stage), ": ", path);
}

// Check that `path` survives a round-trip through the formatter: it parses, it
// formats, the formatted source parses to the same syntax tree, and formatting
// it again changes nothing. Any of those failing means the formatter rewrote a
// program into something different, unparseable, or unsettled.
static bool check_file(Path_t path) {
    const char *path_str = Path$as_c_string(path);
    volatile check_stage_t stage = STAGE_PARSE;
    jmp_buf on_err;
    // A parse error anywhere below longjmps here, having already printed its own diagnostic:
    if (setjmp(on_err) != 0) {
        report_problem(stage, path);
        return false;
    }

    ast_t *before = parse_file(path_str, &on_err);
    if (!before) {
        report_problem(STAGE_PARSE, path);
        return false;
    }

    stage = STAGE_FORMAT;
    bool formatted = false;
    Text_t once = format_source(load_file(path_str), &formatted);
    if (!formatted) {
        report_problem(STAGE_FORMAT, path);
        return false;
    }
    const char *once_str = Text$as_c_string(once);

    // parse_file takes "<name>source" for a virtual file, so the formatted text can be reparsed without
    // ever touching the disk:
    stage = STAGE_REPARSE;
    ast_t *after = parse_file(String("<", path_str, ">", once_str), &on_err);
    if (!after) {
        report_problem(STAGE_REPARSE, path);
        return false;
    }

    stage = STAGE_COMPARE;
    if (!streq(elide_offsets(ast_to_sexp_str(before)), elide_offsets(ast_to_sexp_str(after)))) {
        report_problem(STAGE_COMPARE, path);
        return false;
    }

    stage = STAGE_IDEMPOTENT;
    Text_t twice = format_source(spoof_file(String("<", path_str, ">"), once_str), &formatted);
    if (!formatted || !Text$equal_values(once, twice)) {
        report_problem(STAGE_IDEMPOTENT, path);
        return false;
    }
    return true;
}

// Check every file, a jobful at a time. Each check runs in its own process:
// the work is CPU-bound and independent, and a parse error longjmps out of
// deep inside the parser, so a crash or a stray exit can only take down the one
// file it belongs to.
//
// A failing check writes a multi-line diagnostic, so children write to their
// own temp file rather than sharing stderr; the parent echoes each one whole
// when that child is reaped. A temp file rather than a pipe because nothing
// then has to drain it to keep a child from blocking on a full pipe.
static int64_t check_all(List_t paths, int64_t max_jobs) {
    struct {
        pid_t pid;
        FILE *log;
        Path_t path;
        struct timespec started;
    } *in_flight_jobs = GC_MALLOC((size_t)max_jobs * sizeof(*in_flight_jobs));

    // Every file's line ends with an elapsed time, so size that column once up front rather than letting the dot
    // leader absorb the difference between "9ms" and "1.83s" and wobble from row to row:
    int time_w = (int)strlen("1m 03s"); // the widest report_duration() produces short of an hour
    int indent = USE_COLOR ? 2 : 0;

    int64_t n = (int64_t)paths.length, next = 0, in_flight = 0, failures = 0;
    while (next < n || in_flight > 0) {
        while (in_flight < max_jobs && next < n) {
            Path_t path = *(Path_t *)(paths.data + next * paths.stride);
            next += 1;
            FILE *log = tmpfile();
            struct timespec started;
            clock_gettime(CLOCK_MONOTONIC, &started);
            fflush(NULL);
            pid_t pid = fork();
            if (pid == 0) {
                if (log) dup2(fileno(log), STDERR_FILENO);
                bool ok = check_file(path);
                fflush(NULL);
                _exit(ok ? EXIT_SUCCESS : EXIT_FAILURE);
            }
            in_flight_jobs[in_flight].pid = pid;
            in_flight_jobs[in_flight].log = log;
            in_flight_jobs[in_flight].path = path;
            in_flight_jobs[in_flight].started = started;
            in_flight += 1;
        }

        int status = 0;
        pid_t done = wait(&status);
        if (done < 0) break;
        bool ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
        for (int64_t i = 0; i < in_flight; i++) {
            if (in_flight_jobs[i].pid != done) continue;
            if (quiet != yes) {
                struct timespec now;
                clock_gettime(CLOCK_MONOTONIC, &now);
                double elapsed = (double)(now.tv_sec - in_flight_jobs[i].started.tv_sec)
                                 + 1e-9 * (double)(now.tv_nsec - in_flight_jobs[i].started.tv_nsec);
                char time_str[32];
                report_duration(time_str, sizeof(time_str), elapsed);
                style_t style = report_style();
                // cwd-relative, the way `tomo test` names its files:
                const char *name =
                    Path$as_c_string(Path$relative_to(in_flight_jobs[i].path, Path$current_dir()));
                report_leader(stdout, indent, ok, name, time_w);
                print(style.dim, time_str, style.reset);
            }
            // The diagnostic goes to stderr, so flush the line above it first or the two can cross:
            fflush(stdout);
            if (in_flight_jobs[i].log) {
                fflush(in_flight_jobs[i].log);
                rewind(in_flight_jobs[i].log);
                for (int c; (c = fgetc(in_flight_jobs[i].log)) != EOF;)
                    fputc(c, stderr);
                fclose(in_flight_jobs[i].log);
            }
            in_flight_jobs[i] = in_flight_jobs[in_flight - 1];
            in_flight -= 1;
            break;
        }
        if (!ok) failures += 1;
    }
    return failures;
}

static int cmd_fmt(cli_command_t *self, List_t extra_args) {
    (void)extra_args;
    set_default_logs(0);
    files = normalize_tm_paths(files);
    if (files.length == 0) print_err("No files provided to format!\n", self->usage);

    if (check == yes) {
        long cpus = sysconf(_SC_NPROCESSORS_ONLN);
        int64_t max_jobs = jobs > 0 ? (int64_t)jobs : (cpus > 0 ? (int64_t)cpus : 1);
        if (max_jobs < 1) max_jobs = 1;
        if (max_jobs > (int64_t)files.length) max_jobs = (int64_t)files.length;

        int64_t failures = check_all(files, max_jobs);
        if (failures > 0) {
            if (USE_COLOR) print("\x1b[31;7m ", failures, " formatting failure(s) \x1b[m");
            else print(failures, " formatting failure(s)");
            return 1;
        }
        if (USE_COLOR) print("\x1b[92;1m ✅ formatting is faithful on every file\x1b[m");
        else print("formatting is faithful on every file");
        return 0;
    }

    for (int64_t i = 0; i < (int64_t)files.length; i++) {
        Path_t path = *(Path_t *)(files.data + i * files.stride);
        Text_t formatted = format_file(Path$as_c_string(path));
        if (in_place) {
            print("Formatted ", path);
            Path$write(path, formatted, 0644);
        } else {
            print_inline(formatted);
        }
    }
    return 0;
}

cli_command_t fmt_command = {
    .name = "fmt",
    .summary = "Format Tomo source code",
    .description = "Formats Tomo source, printing to stdout (or rewriting the files with\n"
                   "--in-place). With --check, nothing is written: each file is checked to\n"
                   "make sure it parses, that the formatted source parses to the same syntax\n"
                   "tree, and that formatting it again changes nothing. Files are checked in\n"
                   "parallel and listed as they finish, unless --quiet.",
    .spec_len = sizeof(fmt_spec) / sizeof(fmt_spec[0]),
    .spec = fmt_spec,
    .handler = cmd_fmt,
};
