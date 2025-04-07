// Metamethods for enums

#include <stdint.h>
#include <string.h>

#include "lists.h"
#include "bools.h"
#include "functiontype.h"
#include "integers.h"
#include "metamethods.h"
#include "optionals.h"
#include "pointers.h"
#include "siphash.h"
#include "tables.h"
#include "text.h"
#include "util.h"

PUREFUNC public uint64_t Enum$hash(const void *obj, const TypeInfo_t *type)
{
    int32_t tag = *(int32_t*)obj;
    uint32_t components[2] = {(uint32_t)tag, 0};

    const TypeInfo_t *value = type->EnumInfo.tags[tag-1].type;
    if (value && value->size > 0) {
        ptrdiff_t byte_offset = sizeof(int32_t);
        if (value->align && byte_offset % value->align > 0)
            byte_offset += value->align - (byte_offset % value->align);
        components[1] = generic_hash(obj + byte_offset, value);
    }
    return siphash24((void*)components, sizeof(components));
}

PUREFUNC public int32_t Enum$compare(const void *x, const void *y, const TypeInfo_t *type)
{
    if (x == y) return 0;

    int32_t x_tag = *(int32_t*)x;
    int32_t y_tag = *(int32_t*)y;
    if (x_tag != y_tag)
        return x_tag > y_tag ? 1 : -1;

    const TypeInfo_t *value = type->EnumInfo.tags[x_tag-1].type;
    if (value && value->size > 0) {
        ptrdiff_t byte_offset = sizeof(int32_t);
        if (value->align && byte_offset % value->align > 0)
            byte_offset += value->align - (byte_offset % value->align);
        return generic_compare(x + byte_offset, y + byte_offset, value);
    }
    return 0;
}

PUREFUNC public bool Enum$equal(const void *x, const void *y, const TypeInfo_t *type)
{
    if (x == y) return true;

    int32_t x_tag = *(int32_t*)x;
    int32_t y_tag = *(int32_t*)y;
    if (x_tag != y_tag)
        return false;

    const TypeInfo_t *value = type->EnumInfo.tags[x_tag-1].type;
    if (value && value->size > 0) {
        ptrdiff_t byte_offset = sizeof(int32_t);
        if (value->align && byte_offset % value->align > 0)
            byte_offset += value->align - (byte_offset % value->align);
        return generic_equal(x + byte_offset, y + byte_offset, value);
    }
    return true;
}

public Text_t Enum$as_text(const void *obj, bool colorize, const TypeInfo_t *type)
{
    if (!obj) return Text$from_str(type->EnumInfo.name);

    int32_t tag = *(int32_t*)obj;
    NamedType_t value = type->EnumInfo.tags[tag-1];
    if (!value.type || value.type->size == 0)
        return Text$format(colorize ? "\x1b[1m%s\x1b[m" : "%s", value.name);

    ptrdiff_t byte_offset = sizeof(int32_t);
    if (value.type->align && byte_offset % value.type->align > 0)
        byte_offset += value.type->align - (byte_offset % value.type->align);
    return generic_as_text(obj + byte_offset, colorize, value.type);
}

PUREFUNC public bool Enum$is_none(const void *x, const TypeInfo_t*)
{
    return *(int32_t*)x == 0;
}

public void Enum$serialize(const void *obj, FILE *out, Table_t *pointers, const TypeInfo_t *type)
{
    int32_t tag = *(int32_t*)obj;
    Int32$serialize(&tag, out, pointers, &Int32$info);

    NamedType_t value = type->EnumInfo.tags[tag-1];
    if (value.type && value.type->size > 0) {
        ptrdiff_t byte_offset = sizeof(int32_t);
        if (value.type->align && byte_offset % value.type->align > 0)
            byte_offset += value.type->align - (byte_offset % value.type->align);
        _serialize(obj + byte_offset, out, pointers, value.type);
    }
}

public void Enum$deserialize(FILE *in, void *outval, List_t *pointers, const TypeInfo_t *type)
{
    int32_t tag = 0;
    Int32$deserialize(in, &tag, pointers, &Int32$info);
    *(int32_t*)outval = tag;

    NamedType_t value = type->EnumInfo.tags[tag-1];
    if (value.type && value.type->size > 0) {
        ptrdiff_t byte_offset = sizeof(int32_t);
        if (value.type->align && byte_offset % value.type->align > 0)
            byte_offset += value.type->align - (byte_offset % value.type->align);
        _deserialize(in, outval + byte_offset, pointers, value.type);
    }
}

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
