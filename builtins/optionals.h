#pragma once

// Optional types

#include <gc/cord.h>
#include <stdbool.h>
#include <stdint.h>

#include "types.h"
#include "util.h"

#define Bool_t bool
#define yes (Bool_t)true
#define no (Bool_t)false

extern const Bool_t NULL_BOOL;
extern const Table_t NULL_TABLE;
extern const Array_t NULL_ARRAY;
extern const Int_t NULL_INT;
extern const closure_t NULL_CLOSURE;
extern const Text_t NULL_TEXT;

Text_t Optional$as_text(const void *obj, bool colorize, const TypeInfo *type);

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
