// Built-in functions

#pragma once

#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h> // IWYU pragma: export

#include "datatypes.h"
#include "print.h"
#include "stacktrace.h" // IWYU pragma: export
#include "types.h"

extern bool USE_COLOR;
extern Text_t TOMO_VERSION_TEXT;

void tomo_init(void);

#define fail(...)                                                                                                      \
    ({                                                                                                                 \
        fflush(stdout);                                                                                                \
        if (USE_COLOR) fputs("\x1b[31;7m ==================== ERROR ==================== \033[m\n\n", stderr);         \
        else fputs("==================== ERROR ====================\n\n", stderr);                                     \
        print_stacktrace(stderr, 1);                                                                                   \
        if (USE_COLOR) fputs("\n\x1b[31;1m", stderr);                                                                  \
        else fputs("\n", stderr);                                                                                      \
        fprint_inline(stderr, "Error: ", __VA_ARGS__);                                                                 \
        if (USE_COLOR) fputs("\x1b[m\n", stderr);                                                                      \
        else fputs("\n", stderr);                                                                                      \
        fflush(stderr);                                                                                                \
        raise(SIGABRT);                                                                                                \
        exit(1);                                                                                                       \
    })

#define fail_source(filename, start, end, ...)                                                                         \
    ({                                                                                                                 \
        fflush(stdout);                                                                                                \
        if (USE_COLOR) fputs("\x1b[31;7m ==================== ERROR ==================== \n\n\x1b[0;1m", stderr);      \
        else fputs("==================== ERROR ====================\n\n", stderr);                                     \
        print_stacktrace(stderr, 0);                                                                                   \
        fputs("\n", stderr);                                                                                           \
        if (USE_COLOR) fputs("\x1b[31;1m", stderr);                                                                    \
        fprint_inline(stderr, __VA_ARGS__);                                                                            \
        file_t *_file = (filename) ? load_file(filename) : NULL;                                                       \
        if ((filename) && _file) {                                                                                     \
            fputs("\n", stderr);                                                                                       \
            highlight_error(_file, _file->text + (start), _file->text + (end), "\x1b[31;1m", 1, USE_COLOR);            \
        }                                                                                                              \
        if (USE_COLOR) fputs("\x1b[m", stderr);                                                                        \
        fflush(stderr);                                                                                                \
        raise(SIGABRT);                                                                                                \
        exit(1);                                                                                                       \
    })

_Noreturn void fail_text(Text_t message);
Text_t builtin_last_err();
__attribute__((nonnull)) void start_inspect(const char *filename, int64_t start, int64_t end);
void end_inspect(const void *expr, const TypeInfo_t *type);
#define inspect(type, expr, typeinfo, start, end)                                                                      \
    {                                                                                                                  \
        start_inspect(__SOURCE_FILE__, start, end);                                                                    \
        type _expr = expr;                                                                                             \
        end_inspect(&_expr, typeinfo);                                                                                 \
    }
#define inspect_void(expr, typeinfo, start, end)                                                                       \
    {                                                                                                                  \
        start_inspect(__SOURCE_FILE__, start, end);                                                                    \
        expr;                                                                                                          \
        end_inspect(NULL, typeinfo);                                                                                   \
    }

void say(Text_t text, bool newline);
Text_t ask(Text_t prompt, bool bold, bool force_tty);
_Noreturn void tomo_exit(Text_t text, int32_t status);

Closure_t spawn(Closure_t fn);
void sleep_num(double seconds);
OptionalText_t getenv_text(Text_t name);
void setenv_text(Text_t name, Text_t value);
