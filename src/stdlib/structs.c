// Metamethods for structs

#include <stdint.h>
#include <string.h>

#include "lists.h"
#include "bools.h"
#include "functiontype.h"
#include "metamethods.h"
#include "optionals.h"
#include "pointers.h"
#include "siphash.h"
#include "tables.h"
#include "text.h"
#include "util.h"

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstack-protector"
#endif
PUREFUNC public uint64_t Structヽhash(const void *obj, const TypeInfo_t *type)
{
    if (type->StructInfo.num_fields == 0)
        return 0;

    if (type->StructInfo.num_fields == 1)
        return generic_hash(obj, type->StructInfo.fields[0].type);

    uint64_t field_hashes[type->StructInfo.num_fields];
    ptrdiff_t byte_offset = 0;
    ptrdiff_t bit_offset = 0;
    for (int i = 0; i < type->StructInfo.num_fields; i++) {
        NamedType_t field = type->StructInfo.fields[i];
        if (field.type == &Boolヽinfo) {
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
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

PUREFUNC public uint64_t PackedDataヽhash(const void *obj, const TypeInfo_t *type)
{
    if (type->StructInfo.num_fields == 0)
        return 0;

    return siphash24(obj, (size_t)type->size);
}

PUREFUNC public int32_t Structヽcompare(const void *x, const void *y, const TypeInfo_t *type)
{
    if (x == y)
        return 0;

    ptrdiff_t byte_offset = 0;
    ptrdiff_t bit_offset = 0;
    for (int i = 0; i < type->StructInfo.num_fields; i++) {
        NamedType_t field = type->StructInfo.fields[i];
        if (field.type == &Boolヽinfo) {
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

PUREFUNC public bool Structヽequal(const void *x, const void *y, const TypeInfo_t *type)
{
    if (x == y)
        return true;

    ptrdiff_t byte_offset = 0;
    ptrdiff_t bit_offset = 0;
    for (int i = 0; i < type->StructInfo.num_fields; i++) {
        NamedType_t field = type->StructInfo.fields[i];
        if (field.type == &Boolヽinfo) {
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

PUREFUNC public bool PackedDataヽequal(const void *x, const void *y, const TypeInfo_t *type)
{
    if (x == y) return true;
    return (memcmp(x, y, (size_t)type->size) == 0);
}

PUREFUNC public Text_t Structヽas_text(const void *obj, bool colorize, const TypeInfo_t *type)
{
    if (!obj) return Textヽfrom_str(type->StructInfo.name);

    Text_t name = Textヽfrom_str(type->StructInfo.name);
    if (type->StructInfo.is_secret || type->StructInfo.is_opaque) {
        return colorize ? Texts(Text("\x1b[0;1m"), name, Text("\x1b[m(...)")) : Texts(name, "(...)");
    }

    Text_t text = colorize ? Texts(Text("\x1b[0;1m"), name, Text("\x1b[m(")) : Texts(name, "(");
    ptrdiff_t byte_offset = 0;
    ptrdiff_t bit_offset = 0;
    for (int i = 0; i < type->StructInfo.num_fields; i++) {
        NamedType_t field = type->StructInfo.fields[i];
        if (i > 0)
            text = Textヽconcat(text, Text(", "));

        if (type->StructInfo.num_fields > 1)
            text = Textヽconcat(text, Textヽfrom_str(field.name), Text("="));

        if (field.type == &Boolヽinfo) {
            bool b = ((*(char*)(obj + byte_offset)) >> bit_offset) & 0x1;
            text = Textヽconcat(text, Textヽfrom_str(colorize ? (b ? "\x1b[35myes\x1b[m" : "\x1b[35mno\x1b[m") : (b ? "yes" : "no")));
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
            text = Textヽconcat(text, generic_as_text(obj + byte_offset, colorize, field.type));
            byte_offset += field.type->size;
        }
    }
    return Textヽconcat(text, Text(")"));
}

PUREFUNC public bool Structヽis_none(const void *obj, const TypeInfo_t *type)
{
    return *(bool*)(obj + type->size);
}

public void Structヽserialize(const void *obj, FILE *out, Table_t *pointers, const TypeInfo_t *type)
{
    ptrdiff_t byte_offset = 0;
    ptrdiff_t bit_offset = 0;
    for (int i = 0; i < type->StructInfo.num_fields; i++) {
        NamedType_t field = type->StructInfo.fields[i];
        if (field.type == &Boolヽinfo) {
            bool b = ((*(char*)(obj + byte_offset)) >> bit_offset) & 0x1;
            fputc((int)b, out);
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
            _serialize(obj + byte_offset, out, pointers, field.type);
            byte_offset += field.type->size;
        }
    }
}

public void Structヽdeserialize(FILE *in, void *outval, List_t *pointers, const TypeInfo_t *type)
{
    ptrdiff_t byte_offset = 0;
    ptrdiff_t bit_offset = 0;
    for (int i = 0; i < type->StructInfo.num_fields; i++) {
        NamedType_t field = type->StructInfo.fields[i];
        if (field.type == &Boolヽinfo) {
            bool b = (bool)fgetc(in);
            *(char*)(outval + byte_offset) |= (b << bit_offset);
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
            _deserialize(in, outval + byte_offset, pointers, field.type);
            byte_offset += field.type->size;
        }
    }
}

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
