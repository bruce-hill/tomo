#include "errors.h"

#include "../stdlib/files.h"
#include "../stdlib/print.h"
#include "../stdlib/stacktrace.h"
#include "../stdlib/stdlib.h"

// The counterpart to parser_err()'s capture path: whoever ends up owning a
// parse error renders it here, so a deferred report looks exactly like an
// immediate one.
public
void print_parse_error(parse_error_t err) {
    if (USE_COLOR) fputs("\x1b[96;1;7m Parser Error \x1b[m\n\n", stderr);
    else fputs("Parser Error\n\n", stderr);
    highlight_error(err.file, err.start, err.end, "\x1b[91;7;1m", 2, USE_COLOR);
    fputs("\n", stderr);
    if (getenv("TOMO_STACKTRACE")) print_stacktrace(stderr, 1);
    if (USE_COLOR) fputs("\x1b[91;1m", stderr);
    fprint(stderr, err.message, "\n"); // fprint adds a newline of its own; this is the blank line after it
    if (USE_COLOR) fputs("\x1b[m", stderr);
}
