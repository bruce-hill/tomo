// Failure functions
#include <errno.h>
#include <signal.h>
#include <stdio.h>

#include "../util.h"
#include "bools.h"
#include "fail.h"
#include "files.h"
#include "stacktrace.h"
#include "stdlib.h"
#include "test_harness.h"
#include "text.h"
#include "util.h"

public
_Noreturn void fail_text(Text_t message) {
    fail(message);
}

public
Text_t builtin_last_err() {
    return Text$from_str(strerror(errno));
}

public
_Noreturn void fail_source(const char *filename, int start, int end, Text_t message) {
    tomo_cleanup();
    fflush(stdout);
    // Plain mode (set by `tomo test`): emit only the message, no header, stacktrace, or source echo, so a captured
    // failure can be substring-matched cleanly (the echoed source would otherwise include the test's own source text).
    if (getenv("TOMO_PLAIN_ERRORS")) {
        Text$print(stderr, message);
        fputs("\n", stderr);
        // Hand the failing span to the test driver so it can show the offending line (see TOMO_FAIL_SPAN_TAG):
        if (filename) fprintf(stderr, TOMO_FAIL_SPAN_TAG "%s\x1e%d\x1e%d\x1e\n", filename, start, end);
        fflush(stderr);
        exit(1);
    }
    if (USE_COLOR) fputs("\x1b[91;7;1m Runtime Error \x1b[m\n\n\x1b[0;1m", stderr);
    else fputs("Runtime Error\n\n", stderr);
    print_stacktrace(stderr, 1);
    fputs("\n", stderr);
    // Source first, then the message, the same order parser_err and compiler_err use:
    file_t *_file = (filename) ? load_file(filename) : NULL;
    if ((filename) && _file) {
        highlight_error(_file, _file->text + (start), _file->text + (end), "\x1b[91;7;1m", 2, USE_COLOR);
        fputs("\n", stderr);
    }
    if (USE_COLOR) fputs("\x1b[91;1m", stderr);
    Text$print(stderr, message);
    fputs("\n", stderr);
    if (USE_COLOR) fputs("\x1b[m", stderr);
    fflush(stderr);
    if (Bool$parse(Text$from_str(getenv("TOMO_CORE_DUMP")), NULL) == true) raise(SIGABRT);
    exit(1);
}
