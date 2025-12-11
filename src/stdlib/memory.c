// Type info and methods for "Memory" opaque type

#include <err.h>
#include <gc.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/param.h>

#include "memory.h"
#include "metamethods.h"
#include "print.h"
#include "text.h"
#include "types.h"
#include "util.h"

public
Text_t Memory$as_text(const void *p, bool colorize, const TypeInfo_t *info) {
    (void)info;
    if (!p) return Text("Memory");
    Text_t text = Text$from_str(String("Memory<", (void *)p, ">"));
    return colorize ? Texts(Text("\x1b[0;34;1m"), text, Text("\x1b[m")) : text;
}

public
const TypeInfo_t Memory$info = {
    .size = 0,
    .align = 0,
    .metamethods =
        {
            .as_text = Memory$as_text,
            .serialize = cannot_serialize,
            .deserialize = cannot_deserialize,
        },
};
