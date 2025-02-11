// Mutexed data methods/type info
#include <ctype.h>
#include <err.h>
#include <gc.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/param.h>

#include "bools.h"
#include "metamethods.h"
#include "optionals.h"
#include "text.h"
#include "util.h"

static Text_t MutexedData$as_text(const void *m, bool colorize, const TypeInfo_t *type)
{
    auto mutexed = type->MutexedDataInfo;
    Text_t typename = generic_as_text(NULL, false, mutexed.type);
    if (!m) {
        return Texts(colorize ? Text("\x1b[34;1mmutexed\x1b[m(") : Text("mutexed("), typename, Text(")"));
    }
    return Text$format(colorize ? "\x1b[34;1mmutexed %k<%p>\x1b[m" : "mutexed %k<%p>", &typename, *((MutexedData_t*)m));
}

static bool MutexedData$is_none(const void *m, const TypeInfo_t *)
{
    return *((MutexedData_t*)m) == NULL;
}

public const metamethods_t MutexedData$metamethods = {
    .as_text=MutexedData$as_text,
    .is_none=MutexedData$is_none,
    .serialize=cannot_serialize,
    .deserialize=cannot_deserialize,
};

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
