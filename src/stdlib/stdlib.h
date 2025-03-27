#pragma once

// Built-in functions

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "datatypes.h"
#include "files.h"
#include "print.h"
#include "types.h"
#include "util.h"

extern bool USE_COLOR;

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
    if (USE_COLOR) fputs("\x1b[31;7m ==================== ERROR ==================== \n\n\x1b[0;1m", stderr); \
    else fputs("==================== ERROR ====================\n\n", stderr); \
    fprint_inline(stderr, __VA_ARGS__); \
    if (USE_COLOR) fputs("\x1b[m", stderr); \
    fputs("\n\n", stderr); \
    print_stack_trace(stderr, 2, 4); \
    fflush(stderr); \
    raise(SIGABRT); \
    _exit(1); \
})

#define fail_source(filename, start, end, ...) ({ \
    fflush(stdout); \
    if (USE_COLOR) fputs("\x1b[31;7m ==================== ERROR ==================== \n\n\x1b[0;1m", stderr); \
    else fputs("==================== ERROR ====================\n\n", stderr); \
    fprint_inline(stderr, __VA_ARGS__); \
    if (USE_COLOR) fputs("\x1b[m", stderr); \
    file_t *_file = (filename) ? load_file(filename) : NULL; \
    if ((filename) && _file) { \
        fputs("\n", stderr); \
        highlight_error(_file, _file->text+(start), _file->text+(end), "\x1b[31;1m", 2, USE_COLOR); \
        fputs("\n", stderr); \
    } \
    if (USE_COLOR) fputs("\x1b[m", stderr); \
    print_stack_trace(stderr, 2, 4); \
    fputs("\n\n", stderr); \
    print_stack_trace(stderr, 2, 4); \
    fflush(stderr); \
    raise(SIGABRT); \
    _exit(1); \
})

_Noreturn void fail_text(Text_t message);
Text_t builtin_last_err();
__attribute__((nonnull))
void start_inspect(const char *filename, int64_t start, int64_t end);
__attribute__((nonnull))
void end_inspect(const void *expr, const TypeInfo_t *type);
#define inspect(expr, typeinfo, start, end) {\
    start_inspect(__SOURCE_FILE__, start, end); \
    auto _expr = expr; \
    end_inspect(&_expr, typeinfo); \
}
__attribute__((nonnull))
void test_value(const void *expr, const void *expected, const TypeInfo_t *type);
#define test(expr, expected, typeinfo, start, end) {\
    auto _expr = expr; \
    auto _expected = expected; \
    test_value(&_expr, &_expected, typeinfo); \
}

void say(Text_t text, bool newline);
Text_t ask(Text_t prompt, bool bold, bool force_tty);
_Noreturn void tomo_exit(Text_t text, int32_t status);

Closure_t spawn(Closure_t fn);
bool pop_flag(char **argv, int *i, const char *flag, Text_t *result);
void print_stack_trace(FILE *out, int start, int stop);
void sleep_num(double seconds);

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
