#pragma once

// Type info and methods for Where datatype (Anywhere, Start, or End enum)
// Mainly used for text methods.

#include <gc/cord.h>
#include <stdbool.h>
#include <stdint.h>

#include "types.h"

typedef struct Where_s Where_t;
extern const TypeInfo Where;
typedef struct Where$Anywhere_s Where$Anywhere_t;
extern const TypeInfo Where$Anywhere;
typedef struct Where$Start_s Where$Start_t;
extern const TypeInfo Where$Start;
typedef struct Where$End_s Where$End_t;
extern const TypeInfo Where$End;

struct Where$Anywhere_s {};
struct Where$Start_s {};
struct Where$End_s {};
struct Where_s {
    enum { $tag$Where$Anywhere = 0, $tag$Where$Start = 1, $tag$Where$End = 2 } tag;
    union {
        Where$Anywhere_t Anywhere;
        Where$Start_t Start;
        Where$End_t End;
    };
};

extern const Where_t Where$tagged$Anywhere;
extern const Where_t Where$tagged$Start;
extern const Where_t Where$tagged$End;

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
