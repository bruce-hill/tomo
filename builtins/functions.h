#pragma once

// Built-in functions

#include <gc/cord.h>
#include <stdbool.h>
#include <stdint.h>

#include "types.h"

extern uint8_t TOMO_HASH_KEY[8];

void tomo_init(void);

void fail(CORD fmt, ...);
void fail_source(const char *filename, int64_t start, int64_t end, CORD fmt, ...);
CORD builtin_last_err();
void start_test(const char *filename, int64_t start, int64_t end);
void end_test(void *expr, const TypeInfo *type, CORD expected, const char *filename, int64_t start, int64_t end);
#define test(expr, type, expected, filename, start, end) {\
    start_test(filename, start, end); \
    end_test(expr, type, expected, filename, start, end); }
void say(CORD text);

uint32_t generic_hash(const void *obj, const TypeInfo *type);
int32_t generic_compare(const void *x, const void *y, const TypeInfo *type);
bool generic_equal(const void *x, const void *y, const TypeInfo *type);
CORD generic_as_text(const void *obj, bool colorize, const TypeInfo *type);
bool pop_flag(char **argv, int *i, const char *flag, CORD *result);

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
