// Metamethods are methods that all types share for hashing, equality, comparison, and textifying

#include <stdint.h>
#include <string.h>

#include "arrays.h"
#include "bools.h"
#include "channels.h"
#include "functiontype.h"
#include "metamethods.h"
#include "optionals.h"
#include "pointers.h"
#include "siphash.h"
#include "tables.h"
#include "text.h"
#include "util.h"

PUREFUNC public uint64_t generic_hash(const void *obj, const TypeInfo_t *type)
{
    if (type->metamethods.hash)
        return type->metamethods.hash(obj, type);

    return siphash24((void*)obj, (size_t)(type->size));
}

PUREFUNC public int32_t generic_compare(const void *x, const void *y, const TypeInfo_t *type)
{
    if (x == y) return 0;

    if (type->metamethods.compare)
        return type->metamethods.compare(x, y, type);

    return (int32_t)memcmp((void*)x, (void*)y, (size_t)(type->size));
}

PUREFUNC public bool generic_equal(const void *x, const void *y, const TypeInfo_t *type)
{
    if (x == y) return true;

    if (type->metamethods.equal)
        return type->metamethods.equal(x, y, type);

    return (generic_compare(x, y, type) == 0);
}

public Text_t generic_as_text(const void *obj, bool colorize, const TypeInfo_t *type)
{
    if (!type->metamethods.as_text)
        fail("No text metamethod provided for type!");

    return type->metamethods.as_text(obj, colorize, type);
}

public int generic_print(const void *obj, bool colorize, const TypeInfo_t *type)
{
    Text_t text = generic_as_text(obj, colorize, type);
    return Text$print(stdout, text) + printf("\n");
}

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
