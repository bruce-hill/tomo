#pragma once

#include <gc.h> // IWYU pragma: export
#include <signal.h> // IWYU pragma: export
#include <stdio.h> // IWYU pragma: export

#include "./stdlib/bools.h" // IWYU pragma: export
#include "./stdlib/print.h" // IWYU pragma: export
#include "./stdlib/stacktrace.h" // IWYU pragma: export
#include "./stdlib/util.h" // IWYU pragma: export
#include "stdlib/stdlib.h" // IWYU pragma: export

#define new(t, ...) ((t *)memcpy(GC_MALLOC(sizeof(t)), &(t){__VA_ARGS__}, sizeof(t)))

#define fail(...)                                                                                                      \
    ({                                                                                                                 \
        tomo_cleanup();                                                                                                \
        fflush(stdout);                                                                                                \
        if (USE_COLOR) fputs("\x1b[91;7;1m Runtime Error \033[m\n\n", stderr);                                         \
        else fputs("Runtime Error\n\n", stderr);                                                                       \
        print_stacktrace(stderr, 1);                                                                                   \
        if (USE_COLOR) fputs("\n\x1b[91;1m", stderr);                                                                  \
        else fputs("\n", stderr);                                                                                      \
        fprint_inline(stderr, "Error: ", __VA_ARGS__);                                                                 \
        if (USE_COLOR) fputs("\x1b[m\n", stderr);                                                                      \
        else fputs("\n", stderr);                                                                                      \
        fflush(stderr);                                                                                                \
        if (Bool$parse(Text$from_str(getenv("TOMO_CORE_DUMP")), NULL) == true) raise(SIGABRT);                         \
        exit(1);                                                                                                       \
    })
