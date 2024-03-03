#pragma once

#include <gc/cord.h>
#include <stdbool.h>
#include <stdint.h>

#include "types.h"

extern const char *SSS_HASH_VECTOR;

void fail(CORD fmt, ...);
void fail_source(const char *filename, int64_t start, int64_t end, CORD fmt, ...);
CORD builtin_last_err();
public void __doctest(void *expr, const TypeInfo *type, CORD expected, const char *filename, int64_t start, int64_t end);

uint32_t generic_hash(const void *obj, const TypeInfo *type);
int32_t generic_compare(const void *x, const void *y, const TypeInfo *type);
bool generic_equal(const void *x, const void *y, const TypeInfo *type);
CORD generic_as_text(const void *obj, bool colorize, const TypeInfo *type);

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
