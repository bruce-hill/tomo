
#include <gc.h>
#include <gc/cord.h>
#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/param.h>
#include <err.h>

#include "types.h"
#include "../util.h"
#include "../SipHash/halfsiphash.h"

extern const void *SSS_HASH_VECTOR;

static CORD Bool_cord(const bool *b, bool colorize, const TypeInfo *type)
{
    (void)type;
    if (colorize)
        return *b ? "\x1b[35myes\x1b[m" : "\x1b[35mno\x1b[m";
    else
        return *b ? "yes" : "no";
}

public struct {
    TypeInfo type;
} Bool_type = {
    .type={
        .name="Bool",
        .size=sizeof(bool),
        .align=alignof(bool),
        .tag=CustomInfo,
        .CustomInfo={.cord=(void*)Bool_cord},
    },
};

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
