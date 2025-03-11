#pragma once

// Built-in functions

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "datatypes.h"
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
__attribute__((format(printf, 1, 2)))
_Noreturn void fail(const char *fmt, ...);
_Noreturn void fail_text(Text_t message);
__attribute__((format(printf, 4, 5)))
_Noreturn void fail_source(const char *filename, int64_t start, int64_t end, const char *fmt, ...);
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
void test_value(const void *expr, const TypeInfo_t *type, const char *expected);
#define test(expr, typeinfo, expected, start, end) {\
    auto _expr = expr; \
    test_value(&_expr, typeinfo, expected); \
}

void say(Text_t text, bool newline);
Text_t ask(Text_t prompt, bool bold, bool force_tty);
_Noreturn void tomo_exit(Text_t text, int32_t status);

Closure_t spawn(Closure_t fn);
bool pop_flag(char **argv, int *i, const char *flag, Text_t *result);
void print_stack_trace(FILE *out, int start, int stop);
void sleep_num(double seconds);

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
