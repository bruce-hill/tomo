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


#pragma GCC diagnostic ignored "-Wstack-protector"
PUREFUNC public uint64_t generic_hash(const void *obj, const TypeInfo_t *type)
{
    switch (type->tag) {
    case TextInfo: return Text$hash((void*)obj);
    case ArrayInfo: return Array$hash(obj, type);
    case ChannelInfo: return Channel$hash((Channel_t**)obj, type);
    case TableInfo: return Table$hash(obj, type);
    case OptionalInfo: return is_null(obj, type->OptionalInfo.type) ? 0 : generic_hash(obj, type->OptionalInfo.type);
    case StructInfo: {
        if (type->StructInfo.num_fields == 0) {
            return 0;
        } else if (type->StructInfo.num_fields == 1) {
            return generic_hash(obj, type->StructInfo.fields[0].type);
        } else {
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
    }
    case EnumInfo: {
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
    case CustomInfo: case CStringInfo: // These all share the same info
        if (!type->CustomInfo.hash)
            goto hash_data;
        return type->CustomInfo.hash(obj, type);
    case PointerInfo: case FunctionInfo: case TypeInfoInfo: case OpaqueInfo: default: {
      hash_data:;
        return siphash24((void*)obj, (size_t)(type->size));
    }
    }
}

PUREFUNC public int32_t generic_compare(const void *x, const void *y, const TypeInfo_t *type)
{
    if (x == y) return 0;

    switch (type->tag) {
    case PointerInfo: case FunctionInfo: return Pointer$compare(x, y, type);
    case TextInfo: return Text$compare(x, y);
    case ArrayInfo: return Array$compare(x, y, type);
    case ChannelInfo: return Channel$compare((Channel_t**)x, (Channel_t**)y, type);
    case TableInfo: return Table$compare(x, y, type);
    case OptionalInfo: {
        bool x_is_null = is_null(x, type->OptionalInfo.type);
        bool y_is_null = is_null(y, type->OptionalInfo.type);
        if (x_is_null && y_is_null) return 0;
        else if (x_is_null != y_is_null) return (int32_t)y_is_null - (int32_t)x_is_null;
        else return generic_compare(x, y, type->OptionalInfo.type);
    }
    case StructInfo: {
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
    case EnumInfo: {
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
    case CustomInfo: case CStringInfo: // These all share the same info
        if (!type->CustomInfo.compare)
            goto compare_data;
        return type->CustomInfo.compare(x, y, type);
    case TypeInfoInfo: case OpaqueInfo: default:
      compare_data:
        return (int32_t)memcmp((void*)x, (void*)y, (size_t)(type->size));
    }
}

PUREFUNC public bool generic_equal(const void *x, const void *y, const TypeInfo_t *type)
{
    if (x == y) return true;

    switch (type->tag) {
    case PointerInfo: case FunctionInfo: return Pointer$equal(x, y, type);
    case TextInfo: return Text$equal(x, y);
    case ArrayInfo: return Array$equal(x, y, type);
    case ChannelInfo: return Channel$equal((Channel_t**)x, (Channel_t**)y, type);
    case TableInfo: return Table$equal(x, y, type);
    case OptionalInfo: {
        bool x_is_null = is_null(x, type->OptionalInfo.type);
        bool y_is_null = is_null(y, type->OptionalInfo.type);
        if (x_is_null && y_is_null) return true;
        else if (x_is_null != y_is_null) return false;
        else return generic_equal(x, y, type->OptionalInfo.type);
    }
    case StructInfo: case EnumInfo: 
        return (generic_compare(x, y, type) == 0);
    case CustomInfo: case CStringInfo: // These all share the same info
        if (!type->CustomInfo.equal)
            goto use_generic_compare;
        return type->CustomInfo.equal(x, y, type);
    case TypeInfoInfo: case OpaqueInfo: default:
      use_generic_compare:
        return (generic_compare(x, y, type) == 0);
    }
}

public Text_t generic_as_text(const void *obj, bool colorize, const TypeInfo_t *type)
{
    switch (type->tag) {
    case PointerInfo: return Pointer$as_text(obj, colorize, type);
    case FunctionInfo: return Func$as_text(obj, colorize, type);
    case TextInfo: return Text$as_text(obj, colorize, type);
    case ArrayInfo: return Array$as_text(obj, colorize, type);
    case ChannelInfo: return Channel$as_text((Channel_t**)obj, colorize, type);
    case TableInfo: return Table$as_text(obj, colorize, type);
    case TypeInfoInfo: return Type$as_text(obj, colorize, type);
    case OptionalInfo: return Optional$as_text(obj, colorize, type);
    case StructInfo: {
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
    case EnumInfo: {
        if (!obj) return Text$from_str(type->EnumInfo.name);

        int32_t tag = *(int32_t*)obj;
        NamedType_t value = type->EnumInfo.tags[tag-1];
        if (!value.type || value.type->size == 0)
            return Text$format(colorize ? "\x1b[36;1m%s\x1b[m.\x1b[1m%s\x1b[m" : "%s.%s", type->EnumInfo.name, value.name);

        ptrdiff_t byte_offset = sizeof(int32_t);
        if (value.type->align && byte_offset % value.type->align > 0)
            byte_offset += value.type->align - (byte_offset % value.type->align);
        return Text$concat(Text$format(colorize ? "\x1b[36;1m%s\x1b[m." : "%s.", type->EnumInfo.name),
                           generic_as_text(obj + byte_offset, colorize, value.type));
    }
    case CustomInfo: case CStringInfo: // These all share the same info
        if (!type->CustomInfo.as_text)
            fail("No text function provided for type!\n");
        return type->CustomInfo.as_text(obj, colorize, type);
    case OpaqueInfo: return Text("???");
    default: fail("Invalid type tag: %d", type->tag);
    }
}

public int generic_print(const void *obj, bool colorize, const TypeInfo_t *type)
{
    Text_t text = generic_as_text(obj, colorize, type);
    return Text$print(stdout, text) + printf("\n");
}


// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
