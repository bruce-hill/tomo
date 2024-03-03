#pragma once
#include <gc/cord.h>
#include <stdbool.h>
#include <stdint.h>

#include "types.h"

#define String_t CORD
#define Str_t CORD

typedef enum { WHERE_ANYWHERE, WHERE_START, WHERE_END } where_e;

typedef struct {
    enum { FIND_FAILURE, FIND_SUCCESS } status;
    int32_t index;
} find_result_t;

CORD Str__as_str(const void *str, bool colorize, const TypeInfo *info);
CORD Str__quoted(CORD str, bool colorize);
int Str__compare(CORD *x, CORD *y);
bool Str__equal(CORD *x, CORD *y);
uint32_t Str__hash(CORD *cord);
CORD Str__upper(CORD str);
CORD Str__lower(CORD str);
CORD Str__title(CORD str);
bool Str__has(CORD str, CORD target, where_e where);
CORD Str__without(CORD str, CORD target, where_e where);
CORD Str__trimmed(CORD str, CORD skip, where_e where);
find_result_t Str__find(CORD str, CORD pat);
CORD Str__replace(CORD text, CORD pat, CORD replacement, int64_t limit);
array_t Str__split(CORD str, CORD split);
CORD Str__join(CORD glue, array_t pieces);

extern const TypeInfo Str;

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
