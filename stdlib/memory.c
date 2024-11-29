// Type info and methods for "Memory" opaque type
#include <gc.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/param.h>
#include <err.h>

#include "memory.h"
#include "metamethods.h"
#include "text.h"
#include "types.h"
#include "util.h"

public Text_t Memory$as_text(const void *p, bool colorize, const TypeInfo_t *) {
    if (!p) return Text("Memory");
    return Text$format(colorize ? "\x1b[0;34;1mMemory<%p>\x1b[m" : "Memory<%p>", p);
}

public const TypeInfo_t Memory$info = {
    .size=0,
    .align=0,
    .metamethods={
        .as_text=Memory$as_text,
        .serialize=cannot_serialize,
        .deserialize=cannot_deserialize,
    },
};

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
