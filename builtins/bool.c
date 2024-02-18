
#include <gc.h>
#include <gc/cord.h>
#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/param.h>
#include <err.h>

#include "../SipHash/halfsiphash.h"
#include "../util.h"
#include "bool.h"
#include "types.h"

extern const void *SSS_HASH_VECTOR;

public CORD Bool__as_str(const bool *b, bool colorize, const TypeInfo *type)
{
    (void)type;
    if (!b) return "Bool";
    if (colorize)
        return *b ? "\x1b[35myes\x1b[m" : "\x1b[35mno\x1b[m";
    else
        return *b ? "yes" : "no";
}

Bool_namespace_t Bool_type = {
    .type={
        .size=sizeof(bool),
        .align=alignof(bool),
        .tag=CustomInfo,
        .CustomInfo={.as_str=(void*)Bool__as_str},
    },
};

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
