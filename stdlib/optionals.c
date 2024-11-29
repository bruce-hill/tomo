// Optional types

#include <pthread.h>

#include "bools.h"
#include "bytes.h"
#include "datatypes.h"
#include "integers.h"
#include "metamethods.h"
#include "moments.h"
#include "patterns.h"
#include "text.h"
#include "threads.h"
#include "util.h"

public PUREFUNC bool is_null(const void *obj, const TypeInfo_t *non_optional_type)
{
    if (non_optional_type->metamethods.is_none)
        return non_optional_type->metamethods.is_none(obj, non_optional_type);

    return *(bool*)(obj + non_optional_type->size);
}

PUREFUNC public uint64_t Optional$hash(const void *obj, const TypeInfo_t *type)
{
    return is_null(obj, type->OptionalInfo.type) ? 0 : generic_hash(obj, type->OptionalInfo.type);
}

PUREFUNC public int32_t Optional$compare(const void *x, const void *y, const TypeInfo_t *type)
{
    if (x == y) return 0;
    bool x_is_null = is_null(x, type->OptionalInfo.type);
    bool y_is_null = is_null(y, type->OptionalInfo.type);
    if (x_is_null && y_is_null) return 0;
    else if (x_is_null != y_is_null) return (int32_t)y_is_null - (int32_t)x_is_null;
    else return generic_compare(x, y, type->OptionalInfo.type);
}

PUREFUNC public bool Optional$equal(const void *x, const void *y, const TypeInfo_t *type)
{
    if (x == y) return true;

    bool x_is_null = is_null(x, type->OptionalInfo.type);
    bool y_is_null = is_null(y, type->OptionalInfo.type);
    if (x_is_null && y_is_null) return true;
    else if (x_is_null != y_is_null) return false;
    else return generic_equal(x, y, type->OptionalInfo.type);
}

public Text_t Optional$as_text(const void *obj, bool colorize, const TypeInfo_t *type)
{
    if (!obj)
        return Text$concat(generic_as_text(obj, colorize, type->OptionalInfo.type), Text("?"));

    if (is_null(obj, type->OptionalInfo.type))
        return colorize ? Text("\x1b[31mNONE\x1b[m") : Text("NONE");
    return generic_as_text(obj, colorize, type->OptionalInfo.type);
}

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1
