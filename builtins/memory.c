// Type info and methods for "Memory" opaque type
#include <gc.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/param.h>
#include <err.h>

#include "halfsiphash.h"
#include "memory.h"
#include "text.h"
#include "types.h"
#include "util.h"

public Text_t Memory__as_text(const void *p, bool colorize, const TypeInfo *type) {
    (void)type;
    if (!p) return Text("Memory");
    return Text$format(colorize ? "\x1b[0;34;1mMemory<%p>\x1b[m" : "Memory<%p>", p);
}

public const TypeInfo Memory$info = {
    .size=0,
    .align=0,
    .tag=CustomInfo,
    .CustomInfo={.as_text=(void*)Memory__as_text},
};

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
