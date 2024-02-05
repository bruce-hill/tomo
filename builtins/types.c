// Generic type constructor
#include <err.h>
#include <gc.h>
#include <string.h>
#include <stdalign.h>
#include <stdlib.h>
#include <sys/param.h>

#include "array.h"
#include "table.h"
#include "types.h"
#include "../util.h"
#include "../SipHash/halfsiphash.h"

extern const void *SSS_HASH_VECTOR;

public uint32_t generic_hash(const void *obj, const TypeInfo *type)
{
    switch (type->tag) {
    case ArrayInfo: return Array_hash(obj, type);
    case TableInfo: return Table_hash(obj, type);
    case StructInfo: {
        auto info = type->StructInfo;
        if (info.members.length == 0)
            return 0;

        uint32_t hash_values[info.members.length] = {};
        int64_t offset = 0;
        for (int64_t i = 0; i < info.members.length; i++) {
            typedef struct {const char *name; const TypeInfo *type;} struct_member_t;
            struct_member_t *member = info.members.data + i*info.members.stride;
            if (member->type->align > 1 && (offset % member->type->align))
                offset += member->type->align - (offset % member->type->align);
            hash_values[i] = generic_hash(obj + offset, member->type);
            offset += member->type->size;
        }
        uint32_t hash;
        halfsiphash(hash_values, sizeof(uint32_t)*info.members.length, SSS_HASH_VECTOR, (uint8_t*)&hash, sizeof(hash));
        return hash;
    }
    case TaggedUnionInfo: {
        auto info = type->TaggedUnionInfo;
        int32_t tag = *(int32_t*)obj;
        int64_t offset = 4;
        typedef struct { int64_t tag; const char *name; const TypeInfo *type;} tu_member_t;
        for (int64_t i = 0; i < info.members.length; i++) {
            tu_member_t *member = info.members.data + i*info.members.stride;
            offset = MAX(offset, member->type->align);
        }
        for (int64_t i = 0; i < info.members.length; i++) {
            tu_member_t *member = info.members.data + i*info.members.stride;
            if (member->tag != tag) continue;
            struct {
                int64_t tag;
                uint32_t value_hash;
            } data = {tag, generic_hash(obj + offset, member->type)};
            uint32_t hash;
            halfsiphash(&data, sizeof(data), SSS_HASH_VECTOR, (uint8_t*)&hash, sizeof(hash));
            return hash;
        }
        uint32_t hash;
        halfsiphash(&tag, sizeof(tag), SSS_HASH_VECTOR, (uint8_t*)&hash, sizeof(hash));
        return hash;
    }
    case CustomInfo:
        if (!type->CustomInfo.hash)
            goto hash_data;
        return type->CustomInfo.hash(obj, type);
    case PointerInfo:
    default: {
      hash_data:;
        uint32_t hash;
        halfsiphash((void*)obj, type->size, SSS_HASH_VECTOR, (uint8_t*)&hash, sizeof(hash));
        return hash;
    }
    }
}

public int32_t generic_compare(const void *x, const void *y, const TypeInfo *type)
{
    switch (type->tag) {
    case ArrayInfo: return Array_compare(x, y, type);
    case TableInfo: return Table_compare(x, y, type);
    case StructInfo: {
        auto info = type->StructInfo;
        typedef struct {const char *name; const TypeInfo *type;} struct_member_t;
        auto members = (ARRAY_OF(struct_member_t))&info.members;
        int64_t offset = 0;
        foreach (members, member, last) {
            if (member->type->align > 1 && (offset % member->type->align))
                offset += member->type->align - (offset % member->type->align);
            int32_t cmp = generic_compare(x + offset, y + offset, member->type);
            if (cmp != 0) return cmp;
            offset += member->type->size;
        }
        return 0;
    }
    case TaggedUnionInfo: {
        auto info = type->TaggedUnionInfo;
        int32_t xtag = *(int32_t*)x,
                ytag = *(int32_t*)y;

        if (xtag != ytag)
            return xtag - ytag;

        int64_t offset = 4;
        typedef struct { int64_t tag; const char *name; const TypeInfo *type;} tu_member_t;
        for (int64_t i = 0; i < info.members.length; i++) {
            tu_member_t *member = info.members.data + i*info.members.stride;
            offset = MAX(offset, member->type->align);
        }
        for (int64_t i = 0; i < info.members.length; i++) {
            tu_member_t *member = info.members.data + i*info.members.stride;
            if (member->tag != xtag) continue;
            return generic_compare(x + offset, y + offset, member->type);
        }
        return 0;
    }
    case CustomInfo:
        if (!type->CustomInfo.compare)
            goto compare_data;
        return type->CustomInfo.compare(x, y, type);
    case PointerInfo:
    default:
      compare_data:
        return (int32_t)memcmp((void*)x, (void*)y, type->size);
    }
}

public bool generic_equal(const void *x, const void *y, const TypeInfo *type)
{
    switch (type->tag) {
    case ArrayInfo: return Array_equal(x, y, type);
    case TableInfo: return Table_equal(x, y, type);
    case StructInfo: {
        auto info = type->StructInfo;
        typedef struct {const char *name; const TypeInfo *type;} struct_member_t;
        auto members = (ARRAY_OF(struct_member_t))&info.members;
        int64_t offset = 0;
        foreach (members, member, last) {
            if (member->type->align > 1 && (offset % member->type->align))
                offset += member->type->align - (offset % member->type->align);
            bool equal = generic_equal(x + offset, y + offset, member->type);
            if (!equal) return false;
            offset += member->type->size;
        }
        return true;
    }
    case TaggedUnionInfo: {
        auto info = type->TaggedUnionInfo;
        int32_t xtag = *(int32_t*)x,
                ytag = *(int32_t*)y;

        if (xtag != ytag)
            return false;

        int64_t offset = 4;
        typedef struct { int64_t tag; const char *name; const TypeInfo *type;} tu_member_t;
        for (int64_t i = 0; i < info.members.length; i++) {
            tu_member_t *member = info.members.data + i*info.members.stride;
            offset = MAX(offset, member->type->align);
        }
        for (int64_t i = 0; i < info.members.length; i++) {
            tu_member_t *member = info.members.data + i*info.members.stride;
            if (member->tag != xtag) continue;
            return generic_equal(x + offset, y + offset, member->type);
        }
        return true;
    }
    case CustomInfo:
        if (!type->CustomInfo.equal)
            goto use_generic_compare;
        return type->CustomInfo.equal(x, y, type);
    case PointerInfo:
    default:
      use_generic_compare:
        return (generic_compare(x, y, type) == 0);
    }
}

public CORD generic_cord(const void *obj, bool colorize, const TypeInfo *type)
{
    switch (type->tag) {
    case PointerInfo: {
        auto ptr_info = type->PointerInfo;
        static TypeInfo ptr_type = {.name="@Memory", .size=sizeof(void*), .align=alignof(void*)};
        static TypeInfo int_type = {.name="Int", .size=sizeof(int64_t), .align=alignof(int64_t)};
        static TypeInfo ptr_to_int_type = {
            .name="{@Memory=>Int}",
            .size=sizeof(table_t),
            .align=alignof(table_t),
            .tag=TableInfo,
            .TableInfo={.key=&ptr_type, .value=&int_type, .entry_size=16, .value_offset=8},
        };

        static int64_t zero = 0;
        static table_t recursion = {.default_value=&zero};
        static table_t *current_recursion = NULL;

        CORD c;
        bool first_ptr_call = (current_recursion == NULL);
        if (first_ptr_call && ptr_info.cyclic) {
            current_recursion = &recursion;
        }

        if (ptr_info.cyclic) {
            int64_t *found = Table_reserve(current_recursion, obj, NULL, &ptr_to_int_type);
            if (*found) {
                CORD_sprintf(&c, colorize ? "\x1b[34;1m%s%s#%ld\x1b[m" : "%s%s#%ld",
                             ptr_info.sigil, ptr_info.pointed ? ptr_info.pointed->name : "", *found);
                goto done;
            } else {
                *found = Table_length(current_recursion);
            }
        }

        void *ptr = *(void**)obj;
        if (ptr == NULL) {
            CORD_sprintf(&c, colorize ? "\x1b[34;1m!%s\x1b[m" : "!%s", ptr_info.pointed ? ptr_info.pointed->name : "");
        } else {
            CORD_sprintf(&c, colorize ? "\x1b[34;1m%s\x1b[m%r" : "%s%r",
                         ptr_info.sigil, ptr_info.pointed ? generic_cord(ptr, colorize, ptr_info.pointed) : "???");
        }

      done:;
        if (first_ptr_call && ptr_info.cyclic) {
            recursion = (table_t){.default_value=&zero};
            current_recursion = NULL;
        }

        return c;
    }
    case StructInfo: {
        auto info = type->StructInfo;
        CORD c = "{";
        int64_t offset = 0;
        for (int64_t i = 0; i < info.members.length; i++) {
            if (i > 0) c = CORD_cat(c, ", ");
            typedef struct {const char *name; const TypeInfo *type;} struct_member_t;
            struct_member_t *member = info.members.data + i*info.members.stride;
            if (member->type->align > 1 && (offset % member->type->align))
                offset += member->type->align - (offset % member->type->align);

            // Print the field name only if it's not an anonymous field ("_%ld" format)
            char *end;
            if (member->name[0] != '_' || strtol(member->name+1, &end, 10) != i+1 || *end) {
                c = CORD_cat(c, member->name);
                c = CORD_cat(c, "=");
            }

            c = CORD_cat(c, generic_cord(obj + offset, colorize, member->type));
            offset += member->type->size;
        }
        c = CORD_cat(c, "}");
        if (strncmp(type->name, "struct(", strlen("struct(")) != 0)
            CORD_sprintf(&c, colorize ? "\x1b[1m%s\x1b[m%r" : "%s%r", type->name, c);
        return c;
    }
    case TaggedUnionInfo: {
        auto info = type->TaggedUnionInfo;
        int32_t tag = *(int32_t*)obj;
        int64_t offset = 4;
        typedef struct { int64_t tag; const char *name; const TypeInfo *type;} tu_member_t;
        for (int64_t i = 0; i < info.members.length; i++) {
            tu_member_t *member = info.members.data + i*info.members.stride;
            offset = MAX(offset, member->type->align);
        }
        for (int64_t i = 0; i < info.members.length; i++) {
            tu_member_t *member = info.members.data + i*info.members.stride;
            if (member->tag == tag) {
                CORD c;
                CORD_sprintf(&c, colorize ? "\x1b[36m%s\x1b[m" : "%s", member->name);
                if (member->type && member->type->size > 0)
                    c = CORD_cat(c, generic_cord(obj + offset, colorize, member->type));
                return c;
            }
        }

        CORD c = colorize ? "\x1b[36m" : "";
        int32_t tag_remainder = tag;
        for (int64_t i = 0; tag_remainder && i < info.members.length; i++) {
            tu_member_t *member = info.members.data + i*info.members.stride;
            if (tag_remainder & member->tag) {
                if (tag_remainder != tag)
                    c = CORD_cat(c, "+");
                c = CORD_cat(c, member->name);
                tag_remainder &= ~member->tag;
            }
        }
        if (tag_remainder) {
            if (tag_remainder != tag)
                c = CORD_cat(c, "+");
            c = CORD_cat(c, "???");
        }

        if (colorize)
            c = CORD_cat(c, "\x1b[m");

        return c;
    }
    case ArrayInfo: return Array_cord(obj, colorize, type);
    case TableInfo: return Table_cord(obj, colorize, type);
    case CustomInfo:
        if (!type->CustomInfo.cord)
            builtin_fail("No cord function provided for type: %s!\n", type->name);
        return type->CustomInfo.cord(obj, colorize, type);
    default: errx(1, "Invalid type tag: %d", type->tag);
    }
}

public CORD Type__cord(void *type_namespace, bool colorize, const TypeInfo *type)
{
    (void)type_namespace;
    if (!colorize)
        return CORD_from_char_star(type->name);
    CORD c;
    CORD_sprintf(&c, "\x1b[36;1m%s\x1b[m", type->name);
    return c;
}

public struct {
    TypeInfo type;
} TypeInfo_type = {
    .type={
        .name="TypeInfo",
        .size=sizeof(TypeInfo),
        .align=alignof(TypeInfo),
        .tag=CustomInfo,
        .CustomInfo={.cord=(void*)Type__cord},
    },
};

public struct {
    TypeInfo type;
} Void_type = {.type={.name="Void", .size=0, .align=0}};
public struct {
    TypeInfo type;
} Abort_type = {.type={.name="Abort", .size=0, .align=0}};

public CORD Func__cord(const void *fn, bool colorize, const TypeInfo *type)
{
    (void)fn;
    CORD c = type->name;
    if (colorize)
        CORD_sprintf(&c, "\x1b[32;1m%r\x1b[m", c);
    return c;
}

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
