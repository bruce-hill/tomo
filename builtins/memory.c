// Type info and methods for "Memory" opaque type
#include <gc.h>
#include <gc/cord.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/param.h>
#include <err.h>

#include "util.h"
#include "halfsiphash.h"
#include "memory.h"
#include "types.h"

public CORD Memory__as_text(const void *p, bool colorize, const TypeInfo *type) {
    (void)type;
    if (!p) return "Memory";
    CORD cord;
    CORD_sprintf(&cord, colorize ? "\x1b[0;34;1mMemory<%p>\x1b[m" : "Memory<%p>", p);
    return cord;
}

public const TypeInfo Memory = {
    .size=0,
    .align=0,
    .tag=CustomInfo,
    .CustomInfo={.as_text=(void*)Memory__as_text},
};

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
