// Metamethods for structs

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

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstack-protector"
PUREFUNC public uint64_t Struct$hash(const void *obj, const TypeInfo_t *type)
{
    if (type->StructInfo.num_fields == 0)
        return 0;

    if (type->StructInfo.num_fields == 1)
        return generic_hash(obj, type->StructInfo.fields[0].type);

    uint32_t field_hashes[type->StructInfo.num_fields] = {};
    ptrdiff_t byte_offset = 0;
    ptrdiff_t bit_offset = 0;
    for (int i = 0; i < type->StructInfo.num_fields; i++) {
        NamedType_t field = type->StructInfo.fields[i];
        if (field.type == &Bool$info) {
            bool b = ((*(char*)(obj + byte_offset)) >> bit_offset) & 0x1;
            field_hashes[i] = (uint32_t)b;
            bit_offset += 1;
            if (bit_offset >= 8) {
                byte_offset += 1;
                bit_offset = 0;
            }
        } else {
            if (bit_offset > 0) {
                byte_offset += 1;
                bit_offset = 0;
            }
            if (field.type->align && byte_offset % field.type->align > 0)
                byte_offset += field.type->align - (byte_offset % field.type->align);
            field_hashes[i] = generic_hash(obj + byte_offset, field.type);
            byte_offset += field.type->size;
        }
    }
    return siphash24((void*)field_hashes, sizeof(field_hashes));
}
#pragma GCC diagnostic pop

PUREFUNC public int32_t Struct$compare(const void *x, const void *y, const TypeInfo_t *type)
{
    if (x == y)
        return 0;

    ptrdiff_t byte_offset = 0;
    ptrdiff_t bit_offset = 0;
    for (int i = 0; i < type->StructInfo.num_fields; i++) {
        NamedType_t field = type->StructInfo.fields[i];
        if (field.type == &Bool$info) {
            bool bx = ((*(char*)(x + byte_offset)) >> bit_offset) & 0x1;
            bool by = ((*(char*)(y + byte_offset)) >> bit_offset) & 0x1;
            if (bx != by)
                return (int32_t)bx - (int32_t)by;
            bit_offset += 1;
            if (bit_offset >= 8) {
                byte_offset += 1;
                bit_offset = 0;
            }
        } else {
            if (bit_offset > 0) {
                byte_offset += 1;
                bit_offset = 0;
            }
            if (field.type->align && byte_offset % field.type->align > 0)
                byte_offset += field.type->align - (byte_offset % field.type->align);
            int32_t cmp = generic_compare(x + byte_offset, y + byte_offset, field.type);
            if (cmp != 0)
                return cmp;
            byte_offset += field.type->size;
        }
    }
    return 0;
}

PUREFUNC public bool Struct$equal(const void *x, const void *y, const TypeInfo_t *type)
{
    if (x == y)
        return true;

    ptrdiff_t byte_offset = 0;
    ptrdiff_t bit_offset = 0;
    for (int i = 0; i < type->StructInfo.num_fields; i++) {
        NamedType_t field = type->StructInfo.fields[i];
        if (field.type == &Bool$info) {
            bool bx = ((*(char*)(x + byte_offset)) >> bit_offset) & 0x1;
            bool by = ((*(char*)(y + byte_offset)) >> bit_offset) & 0x1;
            if (bx != by)
                return false;
            bit_offset += 1;
            if (bit_offset >= 8) {
                byte_offset += 1;
                bit_offset = 0;
            }
        } else {
            if (bit_offset > 0) {
                byte_offset += 1;
                bit_offset = 0;
            }
            if (field.type->align && byte_offset % field.type->align > 0)
                byte_offset += field.type->align - (byte_offset % field.type->align);
            if (!generic_equal(x + byte_offset, y + byte_offset, field.type))
                return false;
            byte_offset += field.type->size;
        }
    }
    return true;
}

PUREFUNC public Text_t Struct$as_text(const void *obj, bool colorize, const TypeInfo_t *type)
{
    if (!obj) return Text$from_str(type->StructInfo.name);

    if (type->StructInfo.is_secret)
        return Text$format(colorize ? "\x1b[0;1m%s\x1b[m(...)" : "%s(...)", type->StructInfo.name);

    Text_t text = Text$format(colorize ? "\x1b[0;1m%s\x1b[m(" : "%s(", type->StructInfo.name);
    ptrdiff_t byte_offset = 0;
    ptrdiff_t bit_offset = 0;
    for (int i = 0; i < type->StructInfo.num_fields; i++) {
        NamedType_t field = type->StructInfo.fields[i];
        if (i > 0)
            text = Text$concat(text, Text(", "));

        if (type->StructInfo.num_fields > 1)
            text = Text$concat(text, Text$from_str(field.name), Text("="));

        if (field.type == &Bool$info) {
            bool b = ((*(char*)(obj + byte_offset)) >> bit_offset) & 0x1;
            text = Text$concat(text, Text$from_str(colorize ? (b ? "\x1b[35myes\x1b[m" : "\x1b[35mno\x1b[m") : (b ? "yes" : "no")));
            bit_offset += 1;
            if (bit_offset >= 8) {
                byte_offset += 1;
                bit_offset = 0;
            }
        } else {
            if (bit_offset > 0) {
                byte_offset += 1;
                bit_offset = 0;
            }
            if (field.type->align && byte_offset % field.type->align > 0)
                byte_offset += field.type->align - (byte_offset % field.type->align);
            text = Text$concat(text, generic_as_text(obj + byte_offset, colorize, field.type));
            byte_offset += field.type->size;
        }
    }
    return Text$concat(text, Text(")"));
}

PUREFUNC public bool Struct$is_none(const void *obj, const TypeInfo_t *type)
{
    return *(bool*)(obj + type->size);
}

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
