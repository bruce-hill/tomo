// Failure functions
#include <errno.h>
#include <signal.h>
#include <stdio.h>

#include "../util.h"
#include "fail.h"
#include "files.h"
#include "stacktrace.h"
#include "stdlib.h"
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
    if (USE_COLOR) fputs("\x1b[31;7m ==================== ERROR ==================== \n\n\x1b[0;1m", stderr);
    else fputs("==================== ERROR ====================\n\n", stderr);
    print_stacktrace(stderr, 1);
    fputs("\n", stderr);
    if (USE_COLOR) fputs("\x1b[31;1m", stderr);
    Text$print(stderr, message);
    file_t *_file = (filename) ? load_file(filename) : NULL;
    if ((filename) && _file) {
        fputs("\n", stderr);
        highlight_error(_file, _file->text + (start), _file->text + (end), "\x1b[31;1m", 1, USE_COLOR);
    }
    if (USE_COLOR) fputs("\x1b[m", stderr);
    fflush(stderr);
    raise(SIGABRT);
    exit(1);
}
