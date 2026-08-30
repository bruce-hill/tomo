// `tomo test`: compile and run the `test`/`fails`/`fails_compile` blocks in a
// Tomo file, reporting a per-test pass/fail summary.
//
// Runtime tests (`test` and `fails`) are batched into a single runner binary
// that forks per test (see stdlib/test_harness.c). Compile-failure tests
// (`fails_compile`) are checked here in the driver by forking the frontend and
// typechecking the block in a child -- no C compiler involved. Both kinds of
// result flow into one merged summary.

#include <gc.h>
#include <errno.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "../ast.h"
#include "../compile/blocks.h"
#include "../compile/files.h"
#include "../environment.h"
#include "../parse/files.h"
#include "../stdlib/bools.h"
#include "../stdlib/files.h"
#include "../stdlib/lists.h"
#include "../stdlib/optionals.h"
#include "../stdlib/paths.h"
#include "../stdlib/print.h"
#include "../stdlib/stdlib.h"
#include "../stdlib/test_harness.h"
#include "../stdlib/tables.h"
#include "../stdlib/text.h"
#include "commands.h"
#include "common.h"
#include "compilation.h"

typedef struct {
    test_result_t *items;
    int64_t len, cap;
} results_t;

static List_t files = EMPTY_LIST;
static OptionalText_t filter = NONE_TEXT;
static int32_t jobs = 0; // 0 = unset; see max_jobs below

static cli_arg_t test_spec[] = {
    {"files", &files, List$info(&Path$info), .positional = true, .required = true, .metavar = "file.tm",
     .description = "the files whose tests to run"}, //
    {"filter", &filter, &Text$info, .metavar = "substring",
     .description = "only run tests whose label contains this substring"}, //
    {"jobs", &jobs, &Int32$info, .short_flag = 'j', .metavar = "n",
     .description = "how many test files to build and run at once (default: one per CPU)"}, //
    OPTIMIZATION_FLAG, //
    DEBUG_FLAG, //
    VERBOSE_FLAG, //
};

// ---- streaming results ----------------------------------------------------
//
// Test jobs run several at a time, so results arrive interleaved. Each one is
// printed the moment it lands, tagged with the file it came from; the run's
// consolidated report (failures with source excerpts, per-file tallies, the
// summary) still comes at the end, from tomo_test_render.
//
// This is append-only on purpose: nothing is redrawn or erased, so the output
// is identical on a terminal and in a pipe apart from color, and scrollback
// stays intact.
static void stream_result(const test_result_t *r) {
    bool ok = r->outcome == TEST_RESULT_PASS;
    const char *green = USE_COLOR ? "\x1b[92m" : "";
    const char *red = USE_COLOR ? "\x1b[91m" : "";
    const char *dim = USE_COLOR ? "\x1b[2m" : "";
    const char *reset = USE_COLOR ? "\x1b[m" : "";
    const char *mark = ok ? (USE_COLOR ? "✔" : "ok") : (USE_COLOR ? "✘" : "FAIL");
    printf("  %s%s%s %s%s%s %s\n", ok ? green : red, mark, reset, dim, r->file ? r->file : "", reset, r->label);
    fflush(stdout);
}

static void push_result(results_t *r, test_result_t x) {
    if (r->len >= r->cap) {
        r->cap = r->cap ? r->cap * 2 : 16;
        r->items = GC_REALLOC(r->items, (size_t)r->cap * sizeof(test_result_t));
    }
    r->items[r->len++] = x;
}

// Read everything from `fd` until EOF into a NUL-terminated buffer.
static char *drain_fd(int fd) {
    size_t cap = 1024, len = 0;
    char *buf = GC_MALLOC_ATOMIC(cap);
    ssize_t n;
    while ((n = read(fd, buf + len, cap - len - 1)) > 0) {
        len += (size_t)n;
        if (len + 1 >= cap) {
            cap *= 2;
            char *bigger = GC_MALLOC_ATOMIC(cap);
            memcpy(bigger, buf, len);
            buf = bigger;
        }
    }
    buf[len] = '\0';
    return buf;
}

// Fork the frontend and typecheck `test`'s body in a child, expecting a
// compiler error. A clean compile means the test failed (it should not have
// compiled); a compiler_err (exit 1 + message on stderr) means it passed.
static test_result_t run_compile_fail(env_t *module_env, ast_t *test_node) {
    DeclareMatch(t, test_node, Test);
    int pipes[2];
    if (pipe(pipes) != 0) print_err("Could not create pipe");
    fflush(NULL);
    pid_t pid = fork();
    if (pid == 0) {
        dup2(pipes[1], STDOUT_FILENO);
        dup2(pipes[1], STDERR_FILENO);
        close(pipes[0]);
        close(pipes[1]);
        USE_COLOR = false;
        setenv("TOMO_PLAIN_ERRORS", "1", 1); // match the message, not the echoed source
        (void)compile_block(fresh_scope(module_env), t->body);
        fflush(NULL);
        _exit(0); // compiled cleanly -> did NOT fail as expected
    }
    close(pipes[1]);
    char *output = drain_fd(pipes[0]);
    close(pipes[0]);
    int status = 0;
    waitpid(pid, &status, 0);

    // Peel off the span trailer before matching, so `fails_compile "..."` never sees it:
    const char *fail_file = NULL;
    int fail_start = 0, fail_end = 0;
    tomo_test_take_span(output, &fail_file, &fail_start, &fail_end);

    bool failed = !WIFEXITED(status) || WEXITSTATUS(status) != 0;
    test_outcome_t outcome;
    if (WIFSIGNALED(status)) {
        // The compiler crashed rather than emitting a diagnostic -- that's a compiler bug, not a legitimate
        // "expected failure", so surface it as a failing test instead of a silent pass.
        output = (char *)String("(the compiler crashed with signal ", (int64_t)WTERMSIG(status),
                                " instead of reporting an error)\n", output);
        outcome = TEST_RESULT_WRONG_MESSAGE;
    } else if (!failed) outcome = TEST_RESULT_UNEXPECTED_SUCCESS;
    else if (t->expected_compile_error[0] && !strstr(output, t->expected_compile_error))
        outcome = TEST_RESULT_WRONG_MESSAGE;
    else outcome = TEST_RESULT_PASS;

    return (test_result_t){
        .outcome = outcome,
        .label = t->label,
        .expected_msg = t->expected_compile_error[0] ? t->expected_compile_error : NULL,
        .expect_failure = true,
        .output = output,
        .first_line = (int)get_line_number(test_node->file, test_node->start),
        .last_line = (int)get_line_number(test_node->file, test_node->end),
        .fail_file = fail_file,
        .fail_start = fail_start,
        .fail_end = fail_end,
    };
}

// ---- worker ---------------------------------------------------------------

#define WORKER_RUNNER_CRASHED 3 // exit code: the test runner died on a signal

// Run one file's tests, streaming each result back over `msg_fd`. This runs in
// a forked worker, so anything fatal here -- an unparseable file, a compiler
// crash -- takes down only this job, and the driver turns it into one failed
// file rather than an aborted run.
static void run_tests_for_file(Path_t path, int msg_fd) {
    env_t *env = global_env(source_mapping, instrument, debugging);
    ast_t *ast = parse_file(Path$as_c_string(path), NULL);
    if (!ast) print_err("Could not parse file: ", path);

    // Shared dependencies were built by the driver before any worker started, so this compiles only what belongs
    // to this file -- no two workers write the same artifact. It also collects the object list for the link below.
    List_t object_files = EMPTY_LIST, extra_ldlibs = EMPTY_LIST;
    compile_files(env, List(path), &object_files, &extra_ldlibs, COMPILE_OBJ);

    env_t *module_env = load_module_env(env, ast);

    // With --filter, only run tests whose label contains the substring. Compile-failure tests are skipped here;
    // runtime tests are skipped inside the runner (see TOMO_TEST_FILTER below), so this only gates whether a
    // runner is worth building.
    const char *filt = filter.length > 0 ? Text$as_c_string(filter) : NULL;

    // (1) Compile-failure tests: forked frontend, no C compiler.
    bool has_runtime_tests = false;
    for (ast_list_t *stmt = Match(ast, Block)->statements; stmt; stmt = stmt->next) {
        if (stmt->ast->tag != Test) continue;
        DeclareMatch(test, stmt->ast, Test);
        if (filt && !strstr(test->label, filt)) continue;
        if (test->expected_compile_error) {
            test_result_t r = run_compile_fail(module_env, stmt->ast);
            tomo_test_write_record(msg_fd, &r);
        } else has_runtime_tests = true;
    }

    // (2) Runtime tests: one runner binary that forks per test.
    if (!has_runtime_tests) return;
    int64_t count = 0;
    Text_t runner_source = compile_test_runner(module_env, ast, &count);
    if (count == 0) return;

    Path_t runner = build_test_runner(path, object_files, extra_ldlibs, runner_source);
    const char *runner_path = Path$as_c_string(Path$relative_to(runner, Path$current_dir()));

    // The runner writes its records straight to the driver's pipe -- no need to relay them through this process.
    fflush(NULL);
    pid_t pid = fork();
    if (pid == 0) {
        setenv("TOMO_TEST_RESULT_FD", String((int64_t)msg_fd), 1);
        // The runner's own stdout is a pipe, so left to itself it would turn color off and flatten every captured
        // `>>` value to monochrome. Whether the report is colorized is the driver's call, so pass that down.
        setenv("COLOR", USE_COLOR ? "1" : "0", 1);
        if (filt) setenv("TOMO_TEST_FILTER", filt, 1);
        if (!zig_cache_dir_from_env) unsetenv("ZIG_GLOBAL_CACHE_DIR");
        execl(runner_path, runner_path, (char *)NULL);
        _exit(127);
    }
    int run_status = 0;
    waitpid(pid, &run_status, 0);
    // A clean exit (0 = all passed, 1 = some failed) is expected; death by signal means the runner itself
    // crashed, so the records we forwarded may be truncated -- make sure that can't read as a green suite.
    if (WIFSIGNALED(run_status)) {
        // Flush first: this worker's stdout is a pipe (fully buffered), so _exit() alone would throw away the
        // build/diagnostic output that is the only explanation the driver has to show.
        fflush(NULL);
        _exit(WORKER_RUNNER_CRASHED);
    }
}

// ---- the driver -----------------------------------------------------------

typedef struct {
    Path_t path;
    const char *label;
    pid_t pid;
    int msg_fd, out_fd; // results, and the worker's captured stdout/stderr
    char *out;
    size_t out_len, out_cap;
    results_t results; // buffered so the final report stays grouped by file
    bool started, finished;
} job_t;

static void job_absorb_output(job_t *j) {
    if (j->out_len + 4096 >= j->out_cap) {
        j->out_cap = j->out_cap ? j->out_cap * 2 : 8192;
        char *bigger = GC_MALLOC_ATOMIC(j->out_cap);
        if (j->out_len) memcpy(bigger, j->out, j->out_len);
        j->out = bigger;
    }
    ssize_t n = read(j->out_fd, j->out + j->out_len, j->out_cap - j->out_len - 1);
    if (n > 0) {
        j->out_len += (size_t)n;
        j->out[j->out_len] = '\0';
        return;
    }
    // Only a real end-of-stream closes this: an interrupted read is not EOF, and treating it as one would throw
    // away the rest of a failing job's diagnostic.
    if (n < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) return;
    close(j->out_fd);
    j->out_fd = -1;
}

static void start_job(job_t *j) {
    int msg_pipe[2], out_pipe[2];
    if (pipe(msg_pipe) != 0 || pipe(out_pipe) != 0) print_err("Could not create pipe");
    fflush(NULL);
    pid_t pid = fork();
    if (pid == 0) {
        close(msg_pipe[0]);
        close(out_pipe[0]);
        // Everything the worker says goes to the driver, not the terminal, so a diagnostic can't land in the
        // middle of a streamed result line:
        dup2(out_pipe[1], STDOUT_FILENO);
        dup2(out_pipe[1], STDERR_FILENO);
        close(out_pipe[1]);
        run_tests_for_file(j->path, msg_pipe[1]);
        fflush(NULL);
        _exit(EXIT_SUCCESS);
    }
    close(msg_pipe[1]);
    close(out_pipe[1]);
    j->pid = pid;
    j->msg_fd = msg_pipe[0];
    j->out_fd = out_pipe[0];
    j->started = true;
}

// Build the modules that more than one input depends on, before any worker
// starts. Two workers that shared a dependency would otherwise race writing its
// .h/.c/.o. Everything else -- each file's own module -- is left to its worker,
// so compilation still happens in parallel and results start arriving as soon
// as the first file is ready.
static void build_shared_dependencies(env_t *env, List_t paths) {
    struct {
        Path_t path;
        int64_t count;
    } *seen = NULL;
    int64_t n_seen = 0, cap = 0;

    for (int64_t i = 0; i < (int64_t)paths.length; i++) {
        Path_t path = *(Path_t *)(paths.data + i * paths.stride);
        // Parse it here first, catching a parse error rather than dying on it: this scan runs in the driver, so an
        // unparseable file would otherwise abort the whole run. Left out of the scan, it simply gets no shared
        // deps, and its own worker hits the same error and reports it as that one file failing -- which is also
        // why the captured error is dropped rather than printed here.
        parse_error_t parse_err = {};
        if (!parse_file(Path$as_c_string(path), &parse_err)) continue;

        Table_t deps = EMPTY_TABLE, links = EMPTY_TABLE;
        build_file_dependency_graph(env->build_info, path, &deps, &links);
        for (int64_t d = 0; d < (int64_t)deps.entries.length; d++) {
            // Entries are (Path_t key, staleness value); we only want the key, which sits at the front:
            Path_t dep = *(Path_t *)(deps.entries.data + d * deps.entries.stride);
            int64_t at = -1;
            for (int64_t k = 0; k < n_seen; k++)
                if (streq(seen[k].path, dep)) at = k;
            if (at >= 0) {
                seen[at].count += 1;
            } else {
                if (n_seen >= cap) {
                    cap = cap ? cap * 2 : 32;
                    seen = GC_REALLOC(seen, (size_t)cap * sizeof(seen[0]));
                }
                seen[n_seen].path = dep;
                seen[n_seen].count = 1;
                n_seen += 1;
            }
        }
    }

    // A file's own graph includes itself, so an input nothing else depends on has a count of 1 and is left alone.
    // Transitivity needs no special handling: a dependency of a shared module is reached by at least as many
    // inputs, so it's counted shared too.
    List_t shared = EMPTY_LIST;
    for (int64_t k = 0; k < n_seen; k++)
        if (seen[k].count > 1) List$insert(&shared, &seen[k].path, I(0), sizeof(Path_t));

    if (shared.length > 0) compile_files(env, shared, NULL, NULL, COMPILE_OBJ);
}

static int cmd_test(cli_command_t *self, List_t extra_args) {
    (void)self, (void)extra_args;
    set_default_logs(0);
    // For `tomo test`, --verbose means "show each test's output", not "show build/toolchain logs", so keep the
    // compiler-progress logs off even when verbose (set_default_logs turns them all on for --verbose):
    enabled_logs = 0;
    // Tests are compiled once and run once; favor fast compilation. -O0, not
    // -O1: clang's -O1 costs nearly as much as -O3 (see cmd/run.c).
    configure_codegen(opt_flag.tag == TEXT_NONE ? Text("0") : opt_flag, /*optimize=*/false);

    // A directory argument is not expanded here; use shell globbing to test many files at once.
    List_t paths = normalize_tm_paths(files);
    if (paths.length == 0) print_err("No files provided to test!\n", self->usage);

    long cpus = sysconf(_SC_NPROCESSORS_ONLN);
    int64_t max_jobs = jobs > 0 ? (int64_t)jobs : (cpus > 0 ? (int64_t)cpus : 1);
    if (max_jobs < 1) max_jobs = 1;

    build_shared_dependencies(global_env(source_mapping, instrument, debugging), paths);
    // Every worker below compiles at least its own object and test runner, and
    // they all preload the same precompiled header. Build it here, before the
    // first fork, so a cold cache costs one compile of <tomo.h> rather than one
    // per worker (build_shared_dependencies only warms it when the inputs
    // actually share a dependency).
    warm_precompiled_header();

    int64_t n = (int64_t)paths.length;
    job_t *job_list = GC_MALLOC((size_t)n * sizeof(job_t));
    memset(job_list, 0, (size_t)n * sizeof(job_t));
    for (int64_t i = 0; i < n; i++) {
        job_list[i].path = *(Path_t *)(paths.data + i * paths.stride);
        job_list[i].label = Path$as_c_string(Path$relative_to(job_list[i].path, Path$current_dir()));
        job_list[i].msg_fd = job_list[i].out_fd = -1;
    }

    // Two fds per in-flight job, and never more in flight than max_jobs:
    size_t poll_cap = 2 * (size_t)max_jobs;
    struct pollfd *fds = GC_MALLOC_ATOMIC(poll_cap * sizeof(*fds));
    job_t **owner = GC_MALLOC(poll_cap * sizeof(*owner));
    bool *is_msg = GC_MALLOC_ATOMIC(poll_cap * sizeof(*is_msg));

    bool crashed = false;
    int64_t next_to_start = 0, running = 0, finished = 0;
    while (finished < n) {
        while (running < max_jobs && next_to_start < n) {
            start_job(&job_list[next_to_start++]);
            running += 1;
        }

        nfds_t nfds = 0;
        for (int64_t i = 0; i < n; i++) {
            job_t *j = &job_list[i];
            if (!j->started || j->finished) continue;
            if (j->msg_fd >= 0) {
                owner[nfds] = j, is_msg[nfds] = true;
                fds[nfds++] = (struct pollfd){.fd = j->msg_fd, .events = POLLIN};
            }
            if (j->out_fd >= 0) {
                owner[nfds] = j, is_msg[nfds] = false;
                fds[nfds++] = (struct pollfd){.fd = j->out_fd, .events = POLLIN};
            }
        }

        if (nfds > 0 && poll(fds, nfds, -1) < 0) {
            if (errno == EINTR) continue;
            print_err("poll() failed while waiting for test jobs");
        }

        for (nfds_t k = 0; k < nfds; k++) {
            if (!(fds[k].revents & (POLLIN | POLLHUP | POLLERR))) continue;
            job_t *j = owner[k];
            if (is_msg[k]) {
                test_result_t rec;
                if (tomo_test_read_record(j->msg_fd, &rec)) {
                    rec.file = j->label;
                    push_result(&j->results, rec);
                    if (verbose != yes) stream_result(&rec);
                } else {
                    close(j->msg_fd);
                    j->msg_fd = -1;
                }
            } else job_absorb_output(j);
        }

        // A job is done once both its streams are at EOF; only then is it safe to reap it.
        for (int64_t i = 0; i < n; i++) {
            job_t *j = &job_list[i];
            if (!j->started || j->finished || j->msg_fd >= 0 || j->out_fd >= 0) continue;
            int status = 0;
            waitpid(j->pid, &status, 0);
            bool died = !WIFEXITED(status) || WEXITSTATUS(status) != 0;
            if (died) {
                // The worker never finished: report it as one failed file rather than losing the whole run. Its
                // captured output is the diagnostic that explains why.
                bool runner_crashed = WIFEXITED(status) && WEXITSTATUS(status) == WORKER_RUNNER_CRASHED;
                if (runner_crashed) crashed = true;
                test_result_t r = {
                    .outcome = TEST_RESULT_UNEXPECTED_FAILURE,
                    .label = (WIFSIGNALED(status) || runner_crashed) ? "<the test job crashed>"
                                                                     : "<the test job could not be built>",
                    .output = j->out_len ? j->out : (char *)"",
                    .file = j->label,
                };
                push_result(&j->results, r);
                if (verbose != yes) stream_result(&r);
            }
            j->finished = true;
            finished += 1;
            running -= 1;
        }
    }

    // Concatenate in input order, not completion order, so the report below is grouped by file and identical
    // from run to run however the jobs happened to interleave:
    results_t results = {0};
    for (int64_t i = 0; i < n; i++)
        for (int64_t k = 0; k < job_list[i].results.len; k++)
            push_result(&results, job_list[i].results.items[k]);

    if (results.len > 0 && verbose != yes) printf("\n");
    tomo_test_render(results.items, results.len, verbose == yes);
    if (crashed) fprint(stderr, "\nA test runner terminated abnormally; results may be incomplete.");

    if (crashed) return 1;
    for (int64_t i = 0; i < results.len; i++)
        if (results.items[i].outcome != TEST_RESULT_PASS) return 1;
    return 0;
}

cli_command_t test_command = {
    .name = "test",
    .summary = "Run the tests in a Tomo file",
    .description = "Compiles and runs the `test`, `fails`, and `fails_compile` blocks in a file,\n"
                   "reporting a per-test pass/fail summary. Each runtime test runs in its own\n"
                   "forked process, so a panic or infinite loop in one test can't take down the\n"
                   "rest of the suite, and test files are built and run in parallel (see --jobs).\n"
                   "Pass --verbose to show every test and its output.",
    .spec_len = sizeof(test_spec) / sizeof(test_spec[0]),
    .spec = test_spec,
    .handler = cmd_test,
};
