#pragma once

// Type info and methods for Text datatype, which uses the Boehm "cord" library
// and libunistr

#include <gc/cord.h>
#include <stdbool.h>
#include <stdint.h>

#include "datatypes.h"
#include "integers.h"
#include "types.h"
#include "where.h"

#define Text_t CORD

typedef struct {
    enum { FIND_FAILURE, FIND_SUCCESS } status;
    int32_t index;
} find_result_t;

CORD Text$as_text(const void *str, bool colorize, const TypeInfo *info);
CORD Text$quoted(CORD str, bool colorize);
int Text$compare(const CORD *x, const CORD *y);
bool Text$equal(const CORD *x, const CORD *y);
uint32_t Text$hash(const CORD *cord);
CORD Text$upper(CORD str);
CORD Text$lower(CORD str);
CORD Text$title(CORD str);
bool Text$has(CORD str, CORD target, Where_t where);
CORD Text$without(CORD str, CORD target, Where_t where);
CORD Text$trimmed(CORD str, CORD skip, Where_t where);
find_result_t Text$find(CORD str, CORD pat);
CORD Text$replace(CORD text, CORD pat, CORD replacement, Int_t limit);
array_t Text$split(CORD str, CORD split);
CORD Text$join(CORD glue, array_t pieces);
array_t Text$clusters(CORD text);
array_t Text$codepoints(CORD text);
array_t Text$bytes(CORD text);
Int_t Text$num_clusters(CORD text);
Int_t Text$num_codepoints(CORD text);
Int_t Text$num_bytes(CORD text);
array_t Text$character_names(CORD text);
CORD Text$read_line(CORD prompt);

extern const TypeInfo $Text;

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
