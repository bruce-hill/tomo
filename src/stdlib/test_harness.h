// The runtime test harness used by `tomo test`.
//
// A test runner binary is a normal Tomo program whose `main` builds a table of
// `tomo_test_t` descriptors (one per `test`/`fails` block) and calls
// `_tomo_run_tests`. Each test runs in a forked child so a panic or infinite
// loop in one test can't take down the rest of the suite; the parent captures
// the child's output and classifies the outcome.
//
// When run under `tomo test`, the driver sets TOMO_TEST_RESULT_FD and the
// runner streams a structured record per test back over that fd (so the driver
// can merge them with compile-failure tests into one summary). Run directly
// (`./mymodule.test-runner`), the runner renders its own summary instead.
#pragma once

#include <stdbool.h>
#include <stdint.h>

// When TOMO_PLAIN_ERRORS is set (i.e. under `tomo test`), a source-located
// failure appends this trailer to its output so the driver can point at the
// exact expression that blew up. It is stripped from the captured output
// before any substring matching happens, so `fails "..."` is unaffected. The
// separator is a raw RS byte, which no sane program prints.
#define TOMO_FAIL_SPAN_TAG "\x1etomo-fail-span\x1e"

// One test case, emitted by codegen:
typedef struct {
    const char *label;
    void (*fn)(void);      // the compiled test body
    bool expect_failure;   // true for `fails "..."`
    const char *expected_msg; // substring the failure must contain, or NULL for "any failure"
    int first_line, last_line; // source line range of the `test` block
} tomo_test_t;

typedef enum {
    TEST_RESULT_PASS,
    TEST_RESULT_UNEXPECTED_FAILURE, // expected to pass, but it failed
    TEST_RESULT_UNEXPECTED_SUCCESS, // expected to fail, but it passed
    TEST_RESULT_WRONG_MESSAGE,      // failed, but the message didn't match
    TEST_RESULT_TIMEOUT,            // exceeded the per-test time limit
} test_outcome_t;

// The classified result of running one test (also produced by the driver for
// `fails_compile` tests), shared by the runner and the `tomo test` driver:
typedef struct {
    test_outcome_t outcome;
    const char *label;
    const char *expected_msg; // for rendering "expected: ..."
    bool expect_failure;
    char *output;      // captured child output (never NULL; may be "")
    const char *file;  // source file this test came from (NULL if unknown), set by the `tomo test` driver
    int first_line, last_line; // source line range of the `test` block (0 if unknown)
    // The exact span that failed, recovered from the TOMO_FAIL_SPAN_TAG trailer (NULL/0 if the failure had no
    // source location, e.g. a bare `fail()` or a timeout):
    const char *fail_file;
    int fail_start, fail_end;
    double seconds; // wall-clock time the test took
} test_result_t;

// Run every test in `tests`, forking per test. Returns 0 if all passed, else 1.
// Streams records to TOMO_TEST_RESULT_FD when set; otherwise renders a summary.
int _tomo_run_tests(tomo_test_t *tests, int64_t n);

// Render a slick pass/fail summary for `results` (used by the standalone runner
// and by the `tomo test` driver over the merged result set):
void tomo_test_render(test_result_t *results, int64_t n, bool verbose);

// Record protocol between a driven runner and the `tomo test` driver:
void tomo_test_write_record(int fd, const test_result_t *r);
bool tomo_test_read_record(int fd, test_result_t *out); // false at EOF

// Strip a TOMO_FAIL_SPAN_TAG trailer off `output` (truncating it in place) and
// report the span it named. Safe to call on output with no trailer.
void tomo_test_take_span(char *output, const char **file, int *start, int *end);
