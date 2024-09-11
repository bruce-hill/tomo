// Optional types

#include "bool.h"
#include "datatypes.h"
#include "integers.h"
#include "text.h"
#include "util.h"

public const Array_t NULL_ARRAY = {.length=-1};
public const Bool_t NULL_BOOL = -1;
public const Int_t NULL_INT = {.small=0};
public const Table_t NULL_TABLE = {.entries.length=-1};
public const closure_t NULL_CLOSURE = {.fn=NULL};
public const Text_t NULL_TEXT = {.length=-1};

static inline bool is_null(const void *obj, const TypeInfo *non_optional_type)
{
    if (non_optional_type == &Int$info)
        return ((Int_t*)obj)->small == 0;
    else if (non_optional_type == &Bool$info)
        return *((Bool_t*)obj) == NULL_BOOL;
    else if (non_optional_type == &Num$info)
        return isnan(*((Num_t*)obj));
    else if (non_optional_type == &Int64$info)
        return ((OptionalInt64_t*)obj)->is_null;
    else if (non_optional_type == &Int32$info)
        return ((OptionalInt32_t*)obj)->is_null;
    else if (non_optional_type == &Int16$info)
        return ((OptionalInt16_t*)obj)->is_null;
    else if (non_optional_type == &Int8$info)
        return ((OptionalInt8_t*)obj)->is_null;

    switch (non_optional_type->tag) {
        case PointerInfo: return *(void**)obj == NULL;
        case TextInfo: return ((Text_t*)obj)->length < 0;
        case ArrayInfo: return ((Array_t*)obj)->length < 0;
        case TableInfo: return ((Table_t*)obj)->entries.length < 0;
        case FunctionInfo: return *(void**)obj == NULL;
        case StructInfo: {
            int64_t offset = non_optional_type->size;
            if (offset % non_optional_type->align)
                offset += non_optional_type->align - (offset % non_optional_type->align);
            return *(bool*)(obj + offset);
        }
        case EnumInfo: return (*(int*)obj) == 0; // NULL tag
        case CStringInfo: return (*(char**)obj) == NULL;
        default: {
            Text_t t = generic_as_text(NULL, false, non_optional_type);
            errx(1, "is_null() not implemented for: %k", &t);
        }
    }
}

#pragma GCC diagnostic ignored "-Wstack-protector"
public Text_t Optional$as_text(const void *obj, bool colorize, const TypeInfo *type)
{
    if (!obj)
        return Text$concat(generic_as_text(obj, colorize, type->OptionalInfo.type), Text("?"));

    if (is_null(obj, type->OptionalInfo.type))
        return Text$concat(colorize ? Text("\x1b[31m!") : Text("!"), generic_as_text(NULL, false, type->OptionalInfo.type),
                           colorize ? Text("\x1b[m") : Text(""));
    return Text$concat(generic_as_text(obj, colorize, type->OptionalInfo.type), colorize ? Text("\x1b[33m?\x1b[m") : Text("?"));
}

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1
