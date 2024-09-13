// Metamethods are methods that all types share for hashing, equality, comparison, and textifying

#include <stdint.h>

#include "array.h"
#include "channel.h"
#include "functiontype.h"
#include "metamethods.h"
#include "optionals.h"
#include "pointer.h"
#include "siphash.h"
#include "table.h"
#include "text.h"
#include "util.h"


PUREFUNC public uint64_t generic_hash(const void *obj, const TypeInfo *type)
{
    switch (type->tag) {
    case TextInfo: return Text$hash((void*)obj);
    case ArrayInfo: return Array$hash(obj, type);
    case ChannelInfo: return Channel$hash((Channel_t**)obj, type);
    case TableInfo: return Table$hash(obj, type);
    case OptionalInfo: return is_null(obj, type->OptionalInfo.type) ? 0 : generic_hash(obj, type->OptionalInfo.type);
    case EmptyStructInfo: return 0;
    case CustomInfo: case StructInfo: case EnumInfo: case CStringInfo: // These all share the same info
        if (!type->CustomInfo.hash)
            goto hash_data;
        return type->CustomInfo.hash(obj, type);
    case PointerInfo: case FunctionInfo: case TypeInfoInfo: case OpaqueInfo: default: {
      hash_data:;
        return siphash24((void*)obj, (size_t)(type->size));
    }
    }
}

PUREFUNC public int32_t generic_compare(const void *x, const void *y, const TypeInfo *type)
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
    case EmptyStructInfo: return 0;
    case CustomInfo: case StructInfo: case EnumInfo: case CStringInfo: // These all share the same info
        if (!type->CustomInfo.compare)
            goto compare_data;
        return type->CustomInfo.compare(x, y, type);
    case TypeInfoInfo: case OpaqueInfo: default:
      compare_data:
        return (int32_t)memcmp((void*)x, (void*)y, (size_t)(type->size));
    }
}

PUREFUNC public bool generic_equal(const void *x, const void *y, const TypeInfo *type)
{
    if (x == y) return true;

    switch (type->tag) {
    case PointerInfo: case FunctionInfo: return Pointer$equal(x, y, type);
    case TextInfo: return Text$equal(x, y);
    case ArrayInfo: return Array$equal(x, y, type);
    case ChannelInfo: return Channel$equal((Channel_t**)x, (Channel_t**)y, type);
    case TableInfo: return Table$equal(x, y, type);
    case EmptyStructInfo: return true;
    case OptionalInfo: {
        bool x_is_null = is_null(x, type->OptionalInfo.type);
        bool y_is_null = is_null(y, type->OptionalInfo.type);
        if (x_is_null && y_is_null) return true;
        else if (x_is_null != y_is_null) return false;
        else return generic_equal(x, y, type->OptionalInfo.type);
    }
    case CustomInfo: case StructInfo: case EnumInfo: case CStringInfo: // These all share the same info
        if (!type->CustomInfo.equal)
            goto use_generic_compare;
        return type->CustomInfo.equal(x, y, type);
    case TypeInfoInfo: case OpaqueInfo: default:
      use_generic_compare:
        return (generic_compare(x, y, type) == 0);
    }
}

public Text_t generic_as_text(const void *obj, bool colorize, const TypeInfo *type)
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
    case EmptyStructInfo: return colorize ?
                      Text$concat(Text("\x1b[0;1m"), Text$from_str(type->EmptyStructInfo.name), Text("\x1b[m()"))
                          : Text$concat(Text$from_str(type->EmptyStructInfo.name), Text("()"));
    case CustomInfo: case StructInfo: case EnumInfo: case CStringInfo: // These all share the same info
        if (!type->CustomInfo.as_text)
            fail("No text function provided for type!\n");
        return type->CustomInfo.as_text(obj, colorize, type);
    case OpaqueInfo: return Text("???");
    default: errx(1, "Invalid type tag: %d", type->tag);
    }
}

public int generic_print(const void *obj, bool colorize, const TypeInfo *type)
{
    Text_t text = generic_as_text(obj, colorize, type);
    return Text$print(stdout, text) + printf("\n");
}


// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
