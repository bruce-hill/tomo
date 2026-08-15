// `tomo test`: compile and run the `test`/`fails`/`fails_compile` blocks in a
// Tomo file, reporting a per-test pass/fail summary.
//
// Runtime tests (`test` and `fails`) are batched into a single runner binary
// that forks per test (see stdlib/test_harness.c). Compile-failure tests
// (`fails_compile`) are checked here in the driver by forking the frontend and
// typechecking the block in a child -- no C compiler involved. Both kinds of
// result flow into one merged summary.

#include <gc.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
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

static cli_arg_t test_spec[] = {
    {"files", &files, List$info(&Path$info), .positional = true, .required = true, .metavar = "file.tm",
     .description = "the files whose tests to run"}, //
    {"filter", &filter, &Text$info, .metavar = "substring",
     .description = "only run tests whose label contains this substring"}, //
    OPTIMIZATION_FLAG, //
    VERBOSE_FLAG, //
};

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
    };
}

// Run every test in one file, appending results to `results`. Returns true if
// that file's runtime runner terminated abnormally (a crash). Each file gets a
// fresh global env so top-level definitions don't leak between files.
static bool run_tests_for_file(Path_t path, results_t *results) {
    env_t *env = global_env(source_mapping);
    // A cwd-relative name to tag each result with, for the per-file header in verbose mode:
    const char *file_label = Path$as_c_string(Path$relative_to(path, Path$current_dir()));
    ast_t *ast = parse_file(Path$as_c_string(path), NULL);
    if (!ast) print_err("Could not parse file: ", path);

    // Build the module (and its dependencies) to object files first: this also
    // generates the .id/.h artifacts that name-mangling relies on, so the test
    // compilation passes below can reference the module's bindings.
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
            r.file = file_label;
            push_result(results, r);
        } else has_runtime_tests = true;
    }

    // (2) Runtime tests: one runner binary that forks per test.
    if (!has_runtime_tests) return false;
    int64_t count = 0;
    Text_t runner_source = compile_test_runner(module_env, ast, &count);
    if (count == 0) return false;

    Path_t runner = build_test_runner(path, object_files, extra_ldlibs, runner_source);

    int pipes[2];
    if (pipe(pipes) != 0) print_err("Could not create pipe");
    const char *runner_path = Path$as_c_string(Path$relative_to(runner, Path$current_dir()));

    fflush(NULL);
    pid_t pid = fork();
    if (pid == 0) {
        close(pipes[0]);
        setenv("TOMO_TEST_RESULT_FD", String((int64_t)pipes[1]), 1);
        if (filt) setenv("TOMO_TEST_FILTER", filt, 1);
        if (!zig_cache_dir_from_env) unsetenv("ZIG_GLOBAL_CACHE_DIR");
        execl(runner_path, runner_path, (char *)NULL);
        _exit(127);
    }
    close(pipes[1]);
    test_result_t rec;
    while (tomo_test_read_record(pipes[0], &rec)) {
        rec.file = file_label;
        push_result(results, rec);
    }
    close(pipes[0]);
    int run_status = 0;
    waitpid(pid, &run_status, 0);
    // A clean exit (0 = all passed, 1 = some failed) is expected; death by signal means the runner itself
    // crashed, so the results we collected may be truncated -- make sure that can't read as a green suite.
    return WIFSIGNALED(run_status);
}

static int cmd_test(cli_command_t *self, List_t extra_args) {
    (void)self, (void)extra_args;
    set_default_logs(0);
    // For `tomo test`, --verbose means "show each test's output", not "show build/toolchain logs", so keep the
    // compiler-progress logs off even when verbose (set_default_logs turns them all on for --verbose):
    enabled_logs = 0;
    // Tests are compiled once and run once; favor fast compilation:
    configure_codegen(opt_flag.tag == TEXT_NONE ? Text("1") : opt_flag, /*optimize=*/false);

    // A directory argument is not expanded here; use shell globbing to test many files at once.
    List_t paths = normalize_tm_paths(files);
    if (paths.length == 0) print_err("No files provided to test!\n", self->usage);

    results_t results = {0};
    bool runner_crashed = false;
    for (int64_t i = 0; i < (int64_t)paths.length; i++) {
        Path_t path = *(Path_t *)(paths.data + i * paths.stride);
        runner_crashed |= run_tests_for_file(path, &results);
    }

    tomo_test_render(results.items, results.len, verbose == yes);
    if (runner_crashed) fprint(stderr, "\nThe test runner terminated abnormally; results may be incomplete.");

    if (runner_crashed) return 1;
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
                   "rest of the suite. Pass --verbose to show every test and its output.",
    .spec_len = sizeof(test_spec) / sizeof(test_spec[0]),
    .spec = test_spec,
    .handler = cmd_test,
};
