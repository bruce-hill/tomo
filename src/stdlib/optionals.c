// Optional types

#include "bools.h"
#include "bytes.h"
#include "datatypes.h"
#include "integers.h"
#include "metamethods.h"
#include "nums.h"
#include "text.h"
#include "util.h"

public PUREFUNC bool is_none(const void *obj, const TypeInfo_t *non_optional_type)
{
    if (non_optional_type->metamethods.is_none)
        return non_optional_type->metamethods.is_none(obj, non_optional_type);

    const void *dest = (obj + non_optional_type->size);
    return *(bool*)dest;
}

PUREFUNC public uint64_t Optionalヽhash(const void *obj, const TypeInfo_t *type)
{
    return is_none(obj, type->OptionalInfo.type) ? 0 : generic_hash(obj, type->OptionalInfo.type);
}

PUREFUNC public int32_t Optionalヽcompare(const void *x, const void *y, const TypeInfo_t *type)
{
    if (x == y) return 0;
    bool x_is_null = is_none(x, type->OptionalInfo.type);
    bool y_is_null = is_none(y, type->OptionalInfo.type);
    if (x_is_null && y_is_null) return 0;
    else if (x_is_null != y_is_null) return (int32_t)y_is_null - (int32_t)x_is_null;
    else return generic_compare(x, y, type->OptionalInfo.type);
}

PUREFUNC public bool Optionalヽequal(const void *x, const void *y, const TypeInfo_t *type)
{
    if (x == y) return true;

    bool x_is_null = is_none(x, type->OptionalInfo.type);
    bool y_is_null = is_none(y, type->OptionalInfo.type);
    if (x_is_null && y_is_null) return true;
    else if (x_is_null != y_is_null) return false;
    else return generic_equal(x, y, type->OptionalInfo.type);
}

public Text_t Optionalヽas_text(const void *obj, bool colorize, const TypeInfo_t *type)
{
    if (!obj)
        return Textヽconcat(generic_as_text(obj, colorize, type->OptionalInfo.type), Text("?"));

    if (is_none(obj, type->OptionalInfo.type))
        return colorize ? Text("\x1b[31mnone\x1b[m") : Text("none");
    return generic_as_text(obj, colorize, type->OptionalInfo.type);
}

public void Optionalヽserialize(const void *obj, FILE *out, Table_t *pointers, const TypeInfo_t *type)
{
    bool has_value = !is_none(obj, type->OptionalInfo.type);
    assert(fputc((int)has_value, out) != EOF);
    if (has_value)
        _serialize(obj, out, pointers, type->OptionalInfo.type);
}

public void Optionalヽdeserialize(FILE *in, void *outval, List_t *pointers, const TypeInfo_t *type)
{
    bool has_value = (bool)fgetc(in);
    const TypeInfo_t *nonnull = type->OptionalInfo.type;
    if (has_value) {
        memset(outval, 0, (size_t)type->size);
        _deserialize(in, outval, pointers, nonnull);
    } else {
        if (nonnull->tag == TextInfo)
            *(Text_t*)outval = NONE_TEXT;
        else if (nonnull->tag == ListInfo)
            *(List_t*)outval = (List_t){.length=-1};
        else if (nonnull->tag == TableInfo)
            *(Table_t*)outval = (Table_t){.entries={.length=-1}};
        else if (nonnull == &Numヽinfo)
            *(double*)outval = (double)NAN;
        else if (nonnull == &Num32ヽinfo)
            *(float*)outval = (float)NAN;
        else if (nonnull->tag == StructInfo || (nonnull->tag == OpaqueInfo && type->size > nonnull->size))
            memset(outval + type->size, -1, (size_t)(type->size - nonnull->size));
        else
            memset(outval, 0, (size_t)type->size);
    }
}

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1
