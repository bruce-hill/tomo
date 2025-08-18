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

public Text_t Memoryヽas_text(const void *p, bool colorize, const TypeInfo_t *info) {
    (void)info;
    if (!p) return Text("Memory");
    Text_t text = Textヽfrom_str(String("Memory<", *(void**)p, ">"));
    return colorize ? Texts(Text("\x1b[0;34;1m"), text, Text("\x1b[m")) : text;
}

public const TypeInfo_t Memoryヽinfo = {
    .size=0,
    .align=0,
    .metamethods={
        .as_text=Memoryヽas_text,
        .serialize=cannot_serialize,
        .deserialize=cannot_deserialize,
    },
};

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
