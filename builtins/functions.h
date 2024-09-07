#pragma once

// Built-in functions

#include <gc/cord.h>
#include <stdbool.h>
#include <stdint.h>

#include "datatypes.h"
#include "types.h"

void tomo_init(void);
void fail(const char *fmt, ...);
void fail_source(const char *filename, int64_t start, int64_t end, const char *fmt, ...);
Text_t builtin_last_err();
void start_test(const char *filename, int64_t start, int64_t end);
void end_test(const void *expr, const TypeInfo *type, const char *expected, const char *filename, int64_t start, int64_t end);
#define test(expr, typeinfo, expected, start, end) {\
    start_test(__SOURCE_FILE__, start, end); \
    end_test((__typeof__(expr)[1]){expr}, typeinfo, expected, __SOURCE_FILE__, start, end); }
void say(Text_t text, bool newline);
Text_t ask(Text_t prompt, bool bold, bool force_tty);
_Noreturn void tomo_exit(Text_t text, int32_t status);

uint64_t generic_hash(const void *obj, const TypeInfo *type);
int32_t generic_compare(const void *x, const void *y, const TypeInfo *type);
bool generic_equal(const void *x, const void *y, const TypeInfo *type);
Text_t generic_as_text(const void *obj, bool colorize, const TypeInfo *type);
int generic_print(const void *obj, bool colorize, const TypeInfo *type);
closure_t spawn(closure_t fn);
bool pop_flag(char **argv, int *i, const char *flag, Text_t *result);
void print_stack_trace(FILE *out, int start, int stop);

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
