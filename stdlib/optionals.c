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
    if (non_optional_type == &Int$info)
        return ((Int_t*)obj)->small == 0;
    else if (non_optional_type == &Bool$info)
        return *((OptionalBool_t*)obj) == NULL_BOOL;
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
    else if (non_optional_type == &Byte$info)
        return ((OptionalByte_t*)obj)->is_null;
    else if (non_optional_type == &Thread$info)
        return *(pthread_t**)obj == NULL;
    else if (non_optional_type == &Moment$info)
        return ((OptionalMoment_t*)obj)->tv_usec < 0;
    else if (non_optional_type == &Match$info)
        return ((OptionalMatch_t*)obj)->index.small == 0;

    switch (non_optional_type->tag) {
        case ChannelInfo: return *(Channel_t**)obj == NULL;
        case PointerInfo: return *(void**)obj == NULL;
        case TextInfo: return ((Text_t*)obj)->length < 0;
        case ArrayInfo: return ((Array_t*)obj)->length < 0;
        case TableInfo: return ((Table_t*)obj)->entries.length < 0;
        case FunctionInfo: return *(void**)obj == NULL;
        case StructInfo: {
            int64_t offset = 0;
            for (int i = 0; i < non_optional_type->StructInfo.num_fields; i++) {
                NamedType_t field = non_optional_type->StructInfo.fields[i];
                if (offset > 0 && (offset % field.type->align) > 0)
                    offset += field.type->align - (offset % field.type->align);
                offset += field.type->size;
            }
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

public Text_t Optional$as_text(const void *obj, bool colorize, const TypeInfo_t *type)
{
    if (!obj)
        return Text$concat(generic_as_text(obj, colorize, type->OptionalInfo.type), Text("?"));

    if (is_null(obj, type->OptionalInfo.type))
        return colorize ? Text("\x1b[31mNULL\x1b[m") : Text("NULL");
    return generic_as_text(obj, colorize, type->OptionalInfo.type);
}

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1
