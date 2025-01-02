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
__attribute__((format(printf, 4, 5)))
_Noreturn void fail_source(const char *filename, int64_t start, int64_t end, const char *fmt, ...);
Text_t builtin_last_err();
void start_test(const char *filename, int64_t start, int64_t end);
void end_test(const void *expr, const TypeInfo_t *type, const char *expected);
#define test(expr, typeinfo, expected, start, end) {\
    start_test(__SOURCE_FILE__, start, end); \
    auto _expr = expr; \
    end_test(&_expr, typeinfo, expected); }
void say(Text_t text, bool newline);
Text_t ask(Text_t prompt, bool bold, bool force_tty);
_Noreturn void tomo_exit(Text_t text, int32_t status);

Closure_t spawn(Closure_t fn);
bool pop_flag(char **argv, int *i, const char *flag, Text_t *result);
void print_stack_trace(FILE *out, int start, int stop);
void sleep_num(double seconds);
public Closure_t _mutexed(const void *item, size_t size);
#define mutexed(item) _mutexed((__typeof(item)[1]){item}, sizeof(item))

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
