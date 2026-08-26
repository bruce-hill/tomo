// The runtime test harness used by `tomo test`. See test_harness.h.
#include <signal.h>
#include <sys/ioctl.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "files.h" // highlight_error_to, load_file
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

// Strip the TOMO_FAIL_SPAN_TAG trailer (if any) off the end of `output`, reporting the span it named. The trailer is
// removed *before* classification so `fails "..."` substring matching never sees it.
void tomo_test_take_span(char *output, const char **file, int *start, int *end) {
    if (!output) return;
    char *tag = NULL;
    for (char *p = output; (p = strstr(p, TOMO_FAIL_SPAN_TAG)); p += 1)
        tag = p; // the last one wins: an earlier one could only be program output faking the tag
    if (!tag) return;
    char *fields = tag + sizeof(TOMO_FAIL_SPAN_TAG) - 1;
    char *sep1 = strchr(fields, '\x1e');
    if (!sep1) return;
    char *sep2 = strchr(sep1 + 1, '\x1e');
    if (!sep2) return;
    *sep1 = '\0';
    *file = fields;
    *start = atoi(sep1 + 1);
    *end = atoi(sep2 + 1);
    // Drop the trailer, plus the newline that separated it from the message:
    while (tag > output && tag[-1] == '\n')
        tag -= 1;
    *tag = '\0';
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
    int32_t flen = r->fail_file ? (int32_t)strlen(r->fail_file) : 0;
    write_all(fd, &flen, sizeof(flen));
    if (flen) write_all(fd, r->fail_file, (size_t)flen);
    int32_t fstart = (int32_t)r->fail_start, fend = (int32_t)r->fail_end;
    write_all(fd, &fstart, sizeof(fstart));
    write_all(fd, &fend, sizeof(fend));
    write_all(fd, &r->seconds, sizeof(r->seconds));
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
    int32_t flen = 0;
    read_all(fd, &flen, sizeof(flen));
    out->fail_file = flen ? read_str(fd, flen) : NULL;
    int32_t fstart = 0, fend = 0;
    read_all(fd, &fstart, sizeof(fstart));
    read_all(fd, &fend, sizeof(fend));
    double seconds = 0.0;
    read_all(fd, &seconds, sizeof(seconds));
    out->outcome = (test_outcome_t)outcome;
    out->expect_failure = ef != 0;
    out->first_line = first;
    out->last_line = last;
    out->fail_start = fstart;
    out->fail_end = fend;
    out->seconds = seconds;
    out->file = NULL;
    return true;
}

// ---- rendering ------------------------------------------------------------
//
// Two looks, chosen by USE_COLOR (a tty, or COLOR=1): a rich report with color,
// box drawing and source excerpts; and a plain ASCII one that
// stays greppable and log-friendly when piped. Both carry the same information
// -- the plain version drops only decoration, never a detail you'd need to fix
// a failure.

#define REPORT_MAX_WIDTH 96

// How wide to draw the report. Terminals narrower than the content get the
// content anyway (wrapping is better than truncating a diagnostic):
static int report_width(void) {
    const char *cols = getenv("COLUMNS");
    int w = cols ? atoi(cols) : 0;
    if (w < 40) {
        struct winsize ws;
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) w = (int)ws.ws_col;
    }
    if (w < 40) w = 80;
    return w > REPORT_MAX_WIDTH ? REPORT_MAX_WIDTH : w;
}

typedef struct {
    const char *green, *red, *dim, *bold, *reset, *hdr;
    const char *pass_mark, *fail_mark, *gutter, *point, *dot, *rule, *under, *sep;
} style_t;

static style_t styling(void) {
    if (USE_COLOR)
        return (style_t){
            .green = "\x1b[92m", .red = "\x1b[91m", .dim = "\x1b[2m", .bold = "\x1b[1m", .reset = "\x1b[m",
            .hdr = "\x1b[93;1;4m",
            .pass_mark = "✔", .fail_mark = "✘", .gutter = "│",
            .point = "▸", .dot = "·", .rule = "─", .under = "━", .sep = "·",
        };
    return (style_t){
        .green = "", .red = "", .dim = "", .bold = "", .reset = "", .hdr = "",
        .pass_mark = "ok", .fail_mark = "FAIL", .gutter = "|",
        .point = ">", .dot = ".", .rule = "-", .under = "^", .sep = ",",
    };
}

// "0.42s" / "84ms" / "1m 03s" -- short enough to sit at the end of a line.
static void fmt_duration(char *buf, size_t n, double seconds) {
    if (seconds >= 60.0) snprintf(buf, n, "%dm %02ds", (int)(seconds / 60), (int)seconds % 60);
    else if (seconds >= 1.0) snprintf(buf, n, "%.2fs", seconds);
    else snprintf(buf, n, "%dms", (int)(seconds * 1000.0 + 0.5));
}

static void repeat(FILE *f, const char *s, int n) {
    for (int i = 0; i < n; i++)
        fputs(s, f);
}

// ---- source excerpts ------------------------------------------------------

// Show the source around a failure. This hands off to the same renderer the
// compiler and runtime error paths use, so a failing test's excerpt is shaped
// exactly like every other diagnostic Tomo prints: the file name in bold
// yellow, dim line numbers behind a box-drawing gutter, the offending span
// highlighted inline (or underlined with carets when there's no color).
static void print_excerpt(FILE *f, const test_result_t *r, int indent) {
    file_t *file = NULL;
    const char *start = NULL, *end = NULL;

    if (r->fail_file && r->fail_end > r->fail_start) {
        file = load_file(r->fail_file);
        if (file && (int64_t)r->fail_end <= file->len) {
            start = file->text + r->fail_start;
            end = file->text + r->fail_end;
        } else file = NULL;
    }
    if (!file && r->file && r->first_line > 0) {
        // No precise span (a timeout, or a test that should have failed and didn't): point at the `test` line so
        // there's still somewhere to look.
        file = load_file(r->file);
        if (file) {
            start = get_line(file, r->first_line);
            if (start) end = start + strcspn(start, "\r\n");
            else file = NULL;
        }
    }
    if (!file || !start) return;

    highlight_error_to(f, indent, file, start, end, "\x1b[91;7;1m", 2, USE_COLOR);
    fprintf(f, "\n");
}

// ---- failure details ------------------------------------------------------

// The test's own stdout/stderr, behind a gutter so it can't be mistaken for
// the report's own text.
static void print_output(FILE *f, const char *output, int indent, style_t s, const char *color) {
    if (!output) return;
    size_t end = strlen(output);
    while (end > 0 && output[end - 1] == '\n')
        end -= 1; // no dangling blank line
    if (end == 0) return;
    for (const char *line = output; line < output + end;) {
        const char *nl = memchr(line, '\n', (size_t)(output + end - line));
        size_t len = nl ? (size_t)(nl - line) : (size_t)(output + end - line);
        fprintf(f, "%*s%s%s%s %s%.*s%s\n", indent, "", s.dim, USE_COLOR ? "┆" : "|", s.reset, color, (int)len,
                line, s.reset);
        if (!nl) break;
        line = nl + 1;
    }
}

// The "-- expected / ++ got" pair used whenever a `fails` message didn't match.
static void print_mismatch(FILE *f, const test_result_t *r, int indent, style_t s) {
    fprintf(f, "%*s%s-%s %sexpected the failure to contain%s\n", indent, "", s.green, s.reset, s.dim, s.reset);
    fprintf(f, "%*s  %s%s%s\n", indent, "", s.green, r->expected_msg ? r->expected_msg : "", s.reset);
    fprintf(f, "%*s%s+%s %sbut it actually said%s\n", indent, "", s.red, s.reset, s.dim, s.reset);
    print_output(f, r->output, indent + 2, s, s.red);
}

static const char *outcome_headline(const test_result_t *r) {
    switch (r->outcome) {
    case TEST_RESULT_UNEXPECTED_FAILURE: return "this test was expected to pass, but it failed";
    case TEST_RESULT_UNEXPECTED_SUCCESS: return "this test was expected to fail, but it passed";
    case TEST_RESULT_WRONG_MESSAGE: return "this test failed with the wrong message";
    case TEST_RESULT_TIMEOUT: return "this test never finished";
    default: return "";
    }
}

// Laid out like every other Tomo diagnostic: what went wrong, the source it went
// wrong in, then the message -- the same order `parser_err` and `compiler_err`
// use, with the test's name standing in for the error-kind badge.
static void print_failure(FILE *f, const test_result_t *r, int indent, style_t s) {
    int sub = indent + 2;
    fprintf(f, "%*s%s%s%s %s%s%s\n", indent, "", s.red, s.fail_mark, s.reset, s.bold, r->label, s.reset);
    print_excerpt(f, r, sub);
    fprintf(f, "%*s%s%s%s%s\n", sub, "", s.red, s.bold, outcome_headline(r), s.reset);

    switch (r->outcome) {
    case TEST_RESULT_UNEXPECTED_FAILURE:
        print_output(f, r->output, sub, s, "");
        break;
    case TEST_RESULT_UNEXPECTED_SUCCESS:
        if (r->expected_msg)
            fprintf(f, "%*s%sit should have failed with a message containing%s\n", sub, "", s.dim, s.reset);
        if (r->expected_msg) fprintf(f, "%*s  %s%s%s\n", sub, "", s.green, r->expected_msg, s.reset);
        break;
    case TEST_RESULT_WRONG_MESSAGE:
        print_mismatch(f, r, sub, s);
        break;
    case TEST_RESULT_TIMEOUT:
        fprintf(f, "%*s%sit hit the time limit and was killed -- raise it with TOMO_TEST_TIMEOUT=<seconds>%s\n", sub,
                "", s.dim, s.reset);
        print_output(f, r->output, sub, s, "");
        break;
    case TEST_RESULT_PASS:
    default: break;
    }
    fprintf(f, "\n");
}

// ---- per-file roll-up -----------------------------------------------------

// Results are tagged with a per-file label; compare by text so the grouping
// doesn't depend on the caller interning those strings.
static bool same_file(const char *a, const char *b) {
    if (a == b) return true;
    return a && b && strcmp(a, b) == 0;
}

// Results arrive grouped by file (the driver appends a file at a time), so a
// single pass over consecutive runs gives the per-file tallies.
// One file's tallies, plus where the next group starts.
typedef struct {
    const char *file;
    int64_t passed, failed;
    double seconds;
    int64_t next;
} file_group_t;

static file_group_t file_group_at(test_result_t *results, int64_t n, int64_t i) {
    file_group_t g = {.file = results[i].file, .next = i};
    for (; g.next < n && same_file(results[g.next].file, g.file); g.next++) {
        if (results[g.next].outcome == TEST_RESULT_PASS) g.passed += 1;
        else g.failed += 1;
        g.seconds += results[g.next].seconds;
    }
    return g;
}

// Columns, not bytes: the marks are multi-byte UTF-8 ("✔" is three bytes wide
// but one column), so measuring with strlen would make every line come up short.
static int display_width(const char *s) {
    int w = 0;
    for (; *s; s++)
        if ((*s & 0xC0) != 0x80) w += 1;
    return w;
}

static int digits_of(int64_t v) {
    int d = 1;
    for (; v >= 10; v /= 10)
        d += 1;
    return d;
}

// The counts and the elapsed time are laid out as fixed-width columns, sized in
// a first pass over the groups. Letting the dot leader absorb their variation
// instead would slide "N passed" left and right from row to row, because both
// the digit count and the time ("9ms" vs "513ms" vs "1.20s") vary.
static void print_file_lines(FILE *f, test_result_t *results, int64_t n, int indent, int width, style_t s) {
    int pass_digits = 1, fail_digits = 1, time_w = 0;
    bool any_failed = false;
    for (int64_t i = 0; i < n;) {
        file_group_t g = file_group_at(results, n, i);
        if (digits_of(g.passed) > pass_digits) pass_digits = digits_of(g.passed);
        if (g.failed > 0) {
            any_failed = true;
            if (digits_of(g.failed) > fail_digits) fail_digits = digits_of(g.failed);
        }
        char time_str[32];
        fmt_duration(time_str, sizeof(time_str), g.seconds);
        if ((int)strlen(time_str) > time_w) time_w = (int)strlen(time_str);
        i = g.next;
    }

    int pass_w = pass_digits + (int)strlen(" passed");
    int fail_w = fail_digits + (int)strlen(" failed");
    int counts_w = pass_w + (any_failed ? 2 + fail_w : 0);

    for (int64_t i = 0; i < n;) {
        file_group_t g = file_group_at(results, n, i);
        char time_str[32];
        fmt_duration(time_str, sizeof(time_str), g.seconds);

        const char *name = g.file ? g.file : "(unknown file)";
        const char *mark = g.failed == 0 ? s.pass_mark : s.fail_mark;
        int left = display_width(mark) + 1 + display_width(name);
        int dots = width - 2 * indent - left - 2 - counts_w - 2 - time_w;
        if (dots < 1) dots = 1;

        fprintf(f, "%*s%s%s%s %s", indent, "", g.failed == 0 ? s.green : s.red, mark, s.reset, name);
        fprintf(f, " %s", s.dim);
        repeat(f, s.dot, dots);
        fprintf(f, "%s ", s.reset);

        // The passed column is always the same width, so it lines up whether or not this file had failures:
        fprintf(f, "%s%*lld passed%s", g.failed == 0 ? s.green : s.dim, pass_digits, (long long)g.passed, s.reset);
        int used = pass_w;
        if (g.failed > 0) {
            fprintf(f, "  %s%*lld failed%s", s.red, fail_digits, (long long)g.failed, s.reset);
            used += 2 + fail_w;
        }
        repeat(f, " ", counts_w - used);
        fprintf(f, "  %s%*s%s\n", s.dim, time_w, time_str, s.reset);
        i = g.next;
    }
}

// ---- the whole report -----------------------------------------------------

void tomo_test_render(test_result_t *results, int64_t n, bool verbose) {
    // Results arrive grouped by file, but within a file the compile-failure tests are checked before the runtime
    // ones. Put each file's tests back in source order so the report reads top-to-bottom like the file does:
    for (int64_t i = 0; i < n;) {
        int64_t j = i;
        while (j < n && same_file(results[j].file, results[i].file))
            j += 1;
        for (int64_t a = i + 1; a < j; a++) // insertion sort: stable, and these runs are short
            for (int64_t b = a; b > i && results[b].first_line < results[b - 1].first_line; b--) {
                test_result_t tmp = results[b];
                results[b] = results[b - 1];
                results[b - 1] = tmp;
            }
        i = j;
    }

    style_t s = styling();
    int width = report_width();
    int indent = USE_COLOR ? 2 : 0;
    FILE *f = stdout;

    int64_t passed = 0, failed = 0, files = 0;
    double seconds = 0.0;
    for (int64_t i = 0; i < n; i++) {
        if (results[i].outcome == TEST_RESULT_PASS) passed += 1;
        else failed += 1;
        seconds += results[i].seconds;
        if (i == 0 || !same_file(results[i].file, results[i - 1].file)) files += 1;
    }

    // Verbose: the full roster, so you can see what actually ran (and what each
    // test printed). Failures are only marked here -- their details come below,
    // right above the summary, so you never scroll back up through the logs.
    if (verbose) {
        const char *cur = NULL;
        for (int64_t i = 0; i < n; i++) {
            test_result_t *r = &results[i];
            if (i == 0 || !same_file(r->file, cur)) {
                cur = r->file;
                fprintf(f, "%s%*s%s%s%s\n", i > 0 ? "\n" : "", indent, "", s.hdr, cur ? cur : "(unknown file)",
                        s.reset);
            }
            bool ok = r->outcome == TEST_RESULT_PASS;
            // Most tests finish in well under a millisecond; a column of "0ms" is just noise, so only the ones
            // slow enough to be worth noticing get a time:
            char time_str[32] = "";
            if (r->seconds >= 0.001) {
                char d[24];
                fmt_duration(d, sizeof(d), r->seconds);
                snprintf(time_str, sizeof(time_str), " %s%s%s", s.dim, d, s.reset);
            }
            fprintf(f, "%*s%s%s%s %s%s%s%s\n", indent + 2, "", ok ? s.green : s.red, ok ? s.pass_mark : s.fail_mark,
                    s.reset, ok ? s.dim : s.bold, r->label, s.reset, time_str);
            if (ok) print_output(f, r->output, indent + 4, s, s.dim);
        }
        fprintf(f, "\n");
    }

    if (failed > 0) {
        // A banner, so the eye lands on the failures even in a wall of build output:
        if (USE_COLOR)
            fprintf(f, "\n\x1b[91;7;1m %lld test%s failed \x1b[m\n\n", (long long)failed,
                    failed == 1 ? "" : "s");
        else fprintf(f, "\n%lld test%s failed\n\n", (long long)failed, failed == 1 ? "" : "s");
        for (int64_t i = 0; i < n; i++)
            if (results[i].outcome != TEST_RESULT_PASS) print_failure(f, &results[i], indent, s);
    }

    if (n > 0 && (verbose || failed > 0 || files > 1)) {
        print_file_lines(f, results, n, indent, width, s);
        fprintf(f, "\n");
    }

    if (n == 0) {
        // Not a pass and not a failure -- say so plainly rather than reporting a triumphant "0 passed":
        fprintf(f, "%*s%sno tests found%s\n", indent, "", s.dim, s.reset);
        fflush(f);
        return;
    }

    char time_str[32];
    fmt_duration(time_str, sizeof(time_str), seconds);
    if (failed == 0) {
        // The payoff line. Everything above it is green, and so is this.
        fprintf(f, "%*s%s%s%s %lld passed%s %s%s %lld %s %s %s%s\n", indent, "", s.green, s.bold,
                USE_COLOR ? s.pass_mark : "PASS:", (long long)passed, s.reset, s.dim, s.sep, (long long)files,
                files == 1 ? "file" : "files", s.sep, time_str, s.reset);
    } else {
        fprintf(f, "%*s%s%s%s %lld failed%s %s%s %lld passed %s %s%s\n", indent, "", s.red, s.bold,
                USE_COLOR ? s.fail_mark : "FAIL:", (long long)failed, s.reset, s.dim, s.sep, (long long)passed, s.sep,
                time_str, s.reset);
        // The single most useful next command, spelled out so it can be copied:
        for (int64_t i = 0; i < n; i++) {
            if (results[i].outcome == TEST_RESULT_PASS) continue;
            if (!results[i].file) break;
            fprintf(f, "%*s%srerun just this one:%s tomo test %s --filter \"%s\"%s\n", indent, "", s.dim, s.reset,
                    results[i].file, results[i].label, s.reset);
            break;
        }
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
        struct timespec t0;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        pid_t pid = fork();
        if (pid == 0) { // ---- child ----
            dup2(pipes[1], STDOUT_FILENO);
            dup2(pipes[1], STDERR_FILENO);
            close(pipes[0]);
            close(pipes[1]);
            if (result_fd >= 0) close(result_fd);
            // Keep the compact failure message rather than the full runtime-error display: the report draws its
            // own source excerpt, and a test's stacktrace is mostly test-runner scaffolding. This also keeps the
            // echoed source (which contains the test's own text) from producing false substring matches below.
            setenv("TOMO_PLAIN_ERRORS", "1", 1);
            // Only `fails "..."` matches its expected message against this output, and ANSI codes in the middle
            // of the message would break that match. Every other test's output is shown to the user verbatim, so
            // leave its syntax highlighting -- colorized `>>` values and types -- intact.
            if (tests[i].expect_failure) USE_COLOR = false;
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
        struct timespec t1;
        clock_gettime(CLOCK_MONOTONIC, &t1);

        // Peel the span trailer off before classifying, so `fails "..."` matching never sees it:
        const char *fail_file = NULL;
        int fail_start = 0, fail_end = 0;
        tomo_test_take_span(output, &fail_file, &fail_start, &fail_end);

        test_result_t r = {
            .outcome = classify(status, output, &tests[i]),
            .fail_file = fail_file,
            .fail_start = fail_start,
            .fail_end = fail_end,
            .seconds = (double)(t1.tv_sec - t0.tv_sec) + 1e-9 * (double)(t1.tv_nsec - t0.tv_nsec),
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
