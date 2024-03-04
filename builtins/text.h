#pragma once
#include <gc/cord.h>
#include <stdbool.h>
#include <stdint.h>

#include "types.h"

#define Text_t CORD

typedef enum { WHERE_ANYWHERE, WHERE_START, WHERE_END } where_e;

typedef struct {
    enum { FIND_FAILURE, FIND_SUCCESS } status;
    int32_t index;
} find_result_t;

CORD Text__as_text(const void *str, bool colorize, const TypeInfo *info);
CORD Text__quoted(CORD str, bool colorize);
int Text__compare(CORD *x, CORD *y);
bool Text__equal(CORD *x, CORD *y);
uint32_t Text__hash(CORD *cord);
CORD Text__upper(CORD str);
CORD Text__lower(CORD str);
CORD Text__title(CORD str);
bool Text__has(CORD str, CORD target, where_e where);
CORD Text__without(CORD str, CORD target, where_e where);
CORD Text__trimmed(CORD str, CORD skip, where_e where);
find_result_t Text__find(CORD str, CORD pat);
CORD Text__replace(CORD text, CORD pat, CORD replacement, int64_t limit);
array_t Text__split(CORD str, CORD split);
CORD Text__join(CORD glue, array_t pieces);
array_t Text__clusters(CORD text);
array_t Text__codepoints(CORD text);
array_t Text__bytes(CORD text);

extern const TypeInfo Text;

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
