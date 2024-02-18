#pragma once
#include <gc/cord.h>
#include <stdbool.h>
#include <stdint.h>

#include "types.h"

CORD Bool__as_str(const bool *b, bool colorize, const TypeInfo *type);

typedef struct {
    TypeInfo type;
} Bool_namespace_t;

extern Bool_namespace_t Bool_type;

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
