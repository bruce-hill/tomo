#pragma once

// Built-in functions

#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "datatypes.h"
#include "files.h"
#include "print.h"
#include "stacktrace.h"
#include "types.h"
#include "util.h"

extern bool USE_COLOR;
extern Text_t TOMO_VERSION_TEXT;

typedef struct {
    const char *name;
    bool required;
    const TypeInfo_t *type;
    void *dest;
} cli_arg_t;

void tomo_init(void);
void _tomo_parse_args(int argc, char *argv[], Text_t usage, Text_t help, int spec_len, cli_arg_t spec[spec_len]);
#define tomo_parse_args(argc, argv, usage, help, ...) \
    _tomo_parse_args(argc, argv, usage, help, sizeof((cli_arg_t[]){__VA_ARGS__})/sizeof(cli_arg_t), (cli_arg_t[]){__VA_ARGS__})

#define fail(...) ({ \
    fflush(stdout); \
    if (USE_COLOR) fputs("\x1b[31;7m ==================== ERROR ==================== \033[m\n\n", stderr); \
    else fputs("==================== ERROR ====================\n\n", stderr); \
    print_stacktrace(stderr, 1); \
    if (USE_COLOR) fputs("\n\x1b[31;1m", stderr); \
    else fputs("\n", stderr); \
    fprint_inline(stderr, "Error: ", __VA_ARGS__); \
    if (USE_COLOR) fputs("\x1b[m\n", stderr); \
    else fputs("\n", stderr); \
    fflush(stderr); \
    raise(SIGABRT); \
    _exit(1); \
})

#define fail_source(filename, start, end, ...) ({ \
    fflush(stdout); \
    if (USE_COLOR) fputs("\x1b[31;7m ==================== ERROR ==================== \n\n\x1b[0;1m", stderr); \
    else fputs("==================== ERROR ====================\n\n", stderr); \
    print_stacktrace(stderr, 0); \
    fputs("\n", stderr); \
    if (USE_COLOR) fputs("\x1b[31;1m", stderr); \
    fprint_inline(stderr, __VA_ARGS__); \
    file_t *_file = (filename) ? load_file(filename) : NULL; \
    if ((filename) && _file) { \
        fputs("\n", stderr); \
        highlight_error(_file, _file->text+(start), _file->text+(end), "\x1b[31;1m", 1, USE_COLOR); \
    } \
    if (USE_COLOR) fputs("\x1b[m", stderr); \
    fflush(stderr); \
    raise(SIGABRT); \
    _exit(1); \
})

_Noreturn void fail_text(Text_t message);
Text_t builtin_last_err();
__attribute__((nonnull))
void start_inspect(const char *filename, int64_t start, int64_t end);
void end_inspect(const void *expr, const TypeInfo_t *type);
#define inspect(type, expr, typeinfo, start, end) {\
    start_inspect(__SOURCE_FILE__, start, end); \
    type _expr = expr; \
    end_inspect(&_expr, typeinfo); \
}
#define inspect_void(expr, typeinfo, start, end) {\
    start_inspect(__SOURCE_FILE__, start, end); \
    expr; \
    end_inspect(NULL, typeinfo); \
}
__attribute__((nonnull))
void test_value(const char *filename, int64_t start, int64_t end, const void *expr, const void *expected, const TypeInfo_t *type);
#define test(type, expr, expected, typeinfo, start, end) {\
    type _expr = expr; \
    type _expected = expected; \
    test_value(__SOURCE_FILE__, start, end, &_expr, &_expected, typeinfo); \
}

void say(Text_t text, bool newline);
Text_t ask(Text_t prompt, bool bold, bool force_tty);
_Noreturn void tomo_exit(Text_t text, int32_t status);

Closure_t spawn(Closure_t fn);
bool pop_flag(char **argv, int *i, const char *flag, Text_t *result);
void sleep_num(double seconds);
OptionalText_t getenv_text(Text_t name);
void setenv_text(Text_t name, Text_t value);

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
