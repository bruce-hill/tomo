// The runtime test harness used by `tomo test`. See test_harness.h.
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "stdlib.h" // USE_COLOR
#include "test_harness.h"

#define DEFAULT_TIMEOUT_SECS 10

// ---- child-output capture -------------------------------------------------

// Read everything from `fd` until EOF into a NUL-terminated, malloc'd buffer.
static char *drain_fd(int fd) {
    size_t cap = 1024, len = 0;
    char *buf = malloc(cap);
    ssize_t n;
    while ((n = read(fd, buf + len, cap - len - 1)) > 0) {
        len += (size_t)n;
        if (len + 1 >= cap) {
            cap *= 2;
            buf = realloc(buf, cap);
        }
    }
    buf[len] = '\0';
    return buf;
}

static test_outcome_t classify(int status, const char *output, const tomo_test_t *t) {
    if (WIFSIGNALED(status) && WTERMSIG(status) == SIGALRM) return TEST_RESULT_TIMEOUT;
    bool failed = !WIFEXITED(status) || WEXITSTATUS(status) != 0;
    if (!t->expect_failure) return failed ? TEST_RESULT_UNEXPECTED_FAILURE : TEST_RESULT_PASS;
    if (!failed) return TEST_RESULT_UNEXPECTED_SUCCESS;
    if (t->expected_msg && t->expected_msg[0] && !strstr(output, t->expected_msg)) return TEST_RESULT_WRONG_MESSAGE;
    return TEST_RESULT_PASS;
}

// ---- record protocol ------------------------------------------------------

static void write_all(int fd, const void *buf, size_t n) {
    const char *p = buf;
    while (n > 0) {
        ssize_t w = write(fd, p, n);
        if (w <= 0) return;
        p += w;
        n -= (size_t)w;
    }
}

// Returns the number of bytes actually read (short read only at EOF).
static size_t read_all(int fd, void *buf, size_t n) {
    char *p = buf;
    size_t got = 0;
    while (got < n) {
        ssize_t r = read(fd, p + got, n - got);
        if (r <= 0) break;
        got += (size_t)r;
    }
    return got;
}

void tomo_test_write_record(int fd, const test_result_t *r) {
    int32_t outcome = (int32_t)r->outcome;
    uint8_t ef = r->expect_failure ? 1 : 0;
    int32_t llen = (int32_t)strlen(r->label);
    int32_t elen = r->expected_msg ? (int32_t)strlen(r->expected_msg) : 0;
    int32_t olen = r->output ? (int32_t)strlen(r->output) : 0;
    write_all(fd, &outcome, sizeof(outcome));
    write_all(fd, &ef, sizeof(ef));
    write_all(fd, &llen, sizeof(llen));
    write_all(fd, r->label, (size_t)llen);
    write_all(fd, &elen, sizeof(elen));
    if (elen) write_all(fd, r->expected_msg, (size_t)elen);
    write_all(fd, &olen, sizeof(olen));
    if (olen) write_all(fd, r->output, (size_t)olen);
    int32_t first = (int32_t)r->first_line, last = (int32_t)r->last_line;
    write_all(fd, &first, sizeof(first));
    write_all(fd, &last, sizeof(last));
}

static char *read_str(int fd, int32_t len) {
    char *s = malloc((size_t)len + 1);
    read_all(fd, s, (size_t)len);
    s[len] = '\0';
    return s;
}

bool tomo_test_read_record(int fd, test_result_t *out) {
    int32_t outcome;
    if (read_all(fd, &outcome, sizeof(outcome)) != sizeof(outcome)) return false; // EOF at boundary
    uint8_t ef = 0;
    read_all(fd, &ef, sizeof(ef));
    int32_t llen = 0;
    read_all(fd, &llen, sizeof(llen));
    out->label = read_str(fd, llen);
    int32_t elen = 0;
    read_all(fd, &elen, sizeof(elen));
    out->expected_msg = elen ? read_str(fd, elen) : NULL;
    int32_t olen = 0;
    read_all(fd, &olen, sizeof(olen));
    out->output = read_str(fd, olen);
    int32_t first = 0, last = 0;
    read_all(fd, &first, sizeof(first));
    read_all(fd, &last, sizeof(last));
    out->outcome = (test_outcome_t)outcome;
    out->expect_failure = ef != 0;
    out->first_line = first;
    out->last_line = last;
    out->file = NULL;
    return true;
}

// ---- rendering ------------------------------------------------------------

// Print the captured output of a failing test, dimmed and indented by `indent`
// spaces. Uses a box-drawing gutter on a terminal, a plain '|' when piped:
static void print_output(FILE *f, const char *output, int indent, const char *dim, const char *reset) {
    if (!output) return;
    // Ignore trailing newlines so we don't render a dangling empty line:
    size_t end = strlen(output);
    while (end > 0 && output[end - 1] == '\n')
        end -= 1;
    if (end == 0) return;
    const char *bar = USE_COLOR ? "\xe2\x94\x86" : "|";
    fprintf(f, "%s", dim);
    for (const char *line = output; line < output + end;) {
        const char *nl = memchr(line, '\n', (size_t)(output + end - line));
        size_t len = nl ? (size_t)(nl - line) : (size_t)(output + end - line);
        fprintf(f, "%*s%s %.*s\n", indent, "", bar, (int)len, line);
        if (!nl) break;
        line = nl + 1;
    }
    fprintf(f, "%s", reset);
}

// Append a dim " file:first-last" location tag to the current line (editor-jumpable):
static void print_location(FILE *f, const test_result_t *r, const char *dim, const char *reset) {
    if (!r->file || r->first_line <= 0) return;
    if (r->last_line > r->first_line)
        fprintf(f, " %s%s:%d-%d%s", dim, r->file, r->first_line, r->last_line, reset);
    else fprintf(f, " %s%s:%d%s", dim, r->file, r->first_line, reset);
}

static void print_failure(FILE *f, const test_result_t *r, int indent, const char *red, const char *dim,
                          const char *reset) {
    // On a terminal use ❌; when piped (no color) use a plain "FAIL:" tag instead of an emoji:
    const char *mark = USE_COLOR ? "\xe2\x9d\x8c" : "FAIL:";
    int sub = indent + 4;
    fprintf(f, "%*s%s%s %s%s", indent, "", red, mark, r->label, reset);
    print_location(f, r, dim, reset);
    fprintf(f, "\n");
    // A failed test's output is the important part, so render it at normal brightness (not dimmed):
    switch (r->outcome) {
    case TEST_RESULT_UNEXPECTED_FAILURE:
        fprintf(f, "%*sexpected to pass, but it failed:\n", sub, "");
        print_output(f, r->output, sub, "", "");
        break;
    case TEST_RESULT_UNEXPECTED_SUCCESS:
        if (r->expected_msg)
            fprintf(f, "%*sexpected a failure matching \"%s\", but it passed\n", sub, "", r->expected_msg);
        else fprintf(f, "%*sexpected a failure, but it passed\n", sub, "");
        break;
    case TEST_RESULT_WRONG_MESSAGE:
        fprintf(f, "%*sexpected a failure matching \"%s\", but got:\n", sub, "",
                r->expected_msg ? r->expected_msg : "");
        print_output(f, r->output, sub, "", "");
        break;
    case TEST_RESULT_TIMEOUT:
        fprintf(f, "%*stimed out (exceeded the time limit; set TOMO_TEST_TIMEOUT to change it)\n", sub, "");
        break;
    case TEST_RESULT_PASS:
    default: break;
    }
}

void tomo_test_render(test_result_t *results, int64_t n, bool verbose) {
    const char *green = USE_COLOR ? "\x1b[92;1m" : "";
    const char *red = USE_COLOR ? "\x1b[91;1m" : "";
    const char *dim = USE_COLOR ? "\x1b[2m" : "";
    const char *reset = USE_COLOR ? "\x1b[m" : "";
    const char *file_hdr = USE_COLOR ? "\x1b[93;1;4m" : ""; // yellow, bold, underlined
    const char *err_file_hdr = USE_COLOR ? "\x1b[91;1;4m" : ""; // yellow, bold, underlined

    int64_t passed = 0, failed = 0;
    for (int64_t i = 0; i < n; i++)
        if (results[i].outcome == TEST_RESULT_PASS) passed += 1;
        else failed += 1;

    FILE *f = stdout;

    // Phase 1 (verbose only): the full per-file listing of every test. Passing tests are muted (dim ✓) with their
    // captured output; failing tests get just a marker line here -- their details are consolidated below, right above
    // the summary, so you never have to scroll back up through the logs to find them.
    if (verbose) {
        const char *cur_file = NULL;
        for (int64_t i = 0; i < n; i++) {
            test_result_t *r = &results[i];
            if (r->file && (cur_file == NULL || strcmp(cur_file, r->file) != 0)) {
                cur_file = r->file;
                int64_t file_passed = 0, file_total = 0;
                for (int64_t j = i; j < n && results[j].file && strcmp(results[j].file, cur_file) == 0; j++) {
                    file_total += 1;
                    if (results[j].outcome == TEST_RESULT_PASS) file_passed += 1;
                }
                fprintf(f, "%s%s%s%s %s%ld/%ld%s\n", i > 0 ? "\n" : "", file_hdr, r->file, reset, dim,
                        (long)file_passed, (long)file_total, reset);
            }
            if (r->outcome == TEST_RESULT_PASS) {
                const char *mark = USE_COLOR ? "\xe2\x9c\x93" : "PASS:"; // ✓
                fprintf(f, "\x1b[32m%s%s %s%s", mark, dim, r->label, reset);
                print_location(f, r, dim, reset);
                fprintf(f, "\n");
                print_output(f, r->output, 2, dim, reset); // the test's own stdout/stderr
            } else {
                const char *mark = USE_COLOR ? "\xe2\x9d\x8c" : "FAIL:"; // ❌
                fprintf(f, "%s%s %s%s", red, mark, r->label, reset);
                print_location(f, r, dim, reset);
                fprintf(f, "\n");
            }
        }
    }

    // Phase 2 (both modes): every failure with its full log output, grouped by file, immediately above the summary.
    if (failed > 0) {
        if (verbose) fprintf(f, "\n"); // separate the failures section from the full listing above
        const char *cur_file = NULL;
        for (int64_t i = 0; i < n; i++) {
            test_result_t *r = &results[i];
            if (r->outcome == TEST_RESULT_PASS) continue;
            if (r->file && (cur_file == NULL || strcmp(cur_file, r->file) != 0)) {
                cur_file = r->file;
                fprintf(f, "%s%s%s\n", err_file_hdr, r->file, reset);
            }
            print_failure(f, r, 0, red, dim, reset);
        }
    }

    // Separate the summary from any lines printed above, but avoid a stray leading
    // blank line when nothing was printed (quiet, all passed):
    const char *sep = (verbose || failed > 0) ? "\n" : "";
    if (failed == 0) {
        const char *mark = USE_COLOR ? "\xe2\x9c\x85" : "PASS:"; // ✅
        fprintf(f, "%s %s%s %ld/%ld tests passed%s\n", sep, green, mark, (long)passed, (long)n, reset);
    } else {
        const char *mark = USE_COLOR ? "\xe2\x9d\x8c" : "FAIL:"; // ❌
        fprintf(f, "%s %s%s %ld/%ld tests failed%s  (%ld passed)\n", sep, red, mark, (long)failed, (long)n, reset,
                (long)passed);
    }
    fflush(f);
}

// ---- runner ---------------------------------------------------------------

int _tomo_run_tests(tomo_test_t *tests, int64_t n) {
    const char *fd_env = getenv("TOMO_TEST_RESULT_FD");
    int result_fd = fd_env ? atoi(fd_env) : -1;
    bool verbose = getenv("TOMO_TEST_VERBOSE") != NULL;
    const char *timeout_env = getenv("TOMO_TEST_TIMEOUT");
    unsigned int timeout = timeout_env ? (unsigned int)atoi(timeout_env) : DEFAULT_TIMEOUT_SECS;
    const char *filter = getenv("TOMO_TEST_FILTER"); // only run tests whose label contains this substring

    test_result_t *results = result_fd < 0 ? calloc((size_t)n, sizeof(test_result_t)) : NULL;
    int64_t failures = 0, ran = 0;

    for (int64_t i = 0; i < n; i++) {
        if (filter && !strstr(tests[i].label, filter)) continue;
        int pipes[2];
        if (pipe(pipes) != 0) {
            perror("pipe");
            return 2;
        }
        fflush(NULL);
        pid_t pid = fork();
        if (pid == 0) { // ---- child ----
            dup2(pipes[1], STDOUT_FILENO);
            dup2(pipes[1], STDERR_FILENO);
            close(pipes[0]);
            close(pipes[1]);
            if (result_fd >= 0) close(result_fd);
            USE_COLOR = false; // keep captured output clean for substring matching
            // Emit only the failure message (no source echo, which would include the test's own source and cause
            // false substring matches):
            setenv("TOMO_PLAIN_ERRORS", "1", 1);
            alarm(timeout);
            tests[i].fn();
            fflush(NULL);
            _exit(0); // reached only if the body ran without failing
        }
        // ---- parent ----
        close(pipes[1]);
        char *output = drain_fd(pipes[0]); // drain before waitpid to avoid deadlock
        close(pipes[0]);
        int status = 0;
        waitpid(pid, &status, 0);

        test_result_t r = {
            .outcome = classify(status, output, &tests[i]),
            .label = tests[i].label,
            .expected_msg = tests[i].expected_msg,
            .expect_failure = tests[i].expect_failure,
            .output = output,
            .first_line = tests[i].first_line,
            .last_line = tests[i].last_line,
        };
        if (r.outcome != TEST_RESULT_PASS) failures += 1;
        if (result_fd >= 0) tomo_test_write_record(result_fd, &r);
        else results[ran] = r;
        ran += 1;
    }

    if (result_fd < 0) tomo_test_render(results, ran, verbose);
    return failures > 0 ? 1 : 0;
}
