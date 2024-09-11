#pragma once

// Boolean functions/type info

#include <gc/cord.h>
#include <stdbool.h>
#include <stdint.h>

#include "types.h"
#include "optionals.h"
#include "util.h"

#define Bool_t bool
#define yes (Bool_t)true
#define no (Bool_t)false

PUREFUNC Text_t Bool$as_text(const bool *b, bool colorize, const TypeInfo *type);
OptionalBool_t Bool$from_text(Text_t text);
Bool_t Bool$random(double p);

extern const TypeInfo Bool$info;

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
