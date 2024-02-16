#include <err.h>
#include <gc/cord.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>

#include "../util.h"
#include "../builtins/datatypes.h"

typedef const struct cord_info_s cord_info_t;
struct cord_info_s {
    union {
        struct {
            char sigil;
            CORD (*func)(void *x, bool use_color, cord_info_t *info);
            cord_info_t *info;
        } pointer;
        int8_t precision;
        struct {
            CORD (*func)(void *x, bool use_color, cord_info_t *info);
            cord_info_t *info; 
        } array;
        struct {
            CORD (*key_func)(void *x, bool use_color, cord_info_t *info);
            cord_info_t *key_info; 
            size_t value_offset;
            CORD (*value_func)(void *x, bool use_color, cord_info_t *info);
            cord_info_t *value_info; 
        } table;
    };
};
#define INFO(type, ...) (&(cord_info_t){.type={__VA_ARGS__}})

// x := @[Str]
// Pointer__as_str(&x, color, INFO(pointer, '@', Array__as_str, INFO(array, Str__quoted, NULL)))

// x := {Int=>Bool}
// Table__as_str(&x, color, INFO(table, Int64__as_str, NULL, Bool__as_str, NULL))

// x := [[Foo]]
// Array__as_str(&x, color, INFO(array, Array__as_str, INFO(array, Foo__as_str, NULL)))

CORD Bool__as_str(void *x, bool color, cord_info_t *info) {
    if (!x) return "Bool";
    return *(bool*)x ? "yes" : "no";
}

CORD Int__as_str(void *x, bool color, cord_info_t *info) {
    if (!x) return "Int";
    return CORD_asprintf("%ld", *(Int_t)x);
}

CORD Int32__as_str(void *x, bool color, cord_info_t *info) {
    if (!x) return "Int32";
    return CORD_asprintf("%d", *(Int32_t)x);
}

CORD Int16__as_str(void *x, bool color, cord_info_t *info) {
    if (!x) return "Int16";
    return CORD_asprintf("%d", *(Int16_t)x);
}

CORD Int8__as_str(void *x, bool color, cord_info_t *info) {
    if (!x) return "Int8";
    return CORD_asprintf("%d", *(Int8_t)x);
}

CORD Num__as_str(void *x, bool color, cord_info_t *info) {
    if (!x) return "Num";
    return CORD_asprintf("%g", *(Num_t)x);
}

CORD Num32__as_str(void *x, bool color, cord_info_t *info) {
    if (!x) return "Num32";
    return CORD_asprintf("%g", *(Num32_t)x);
}

CORD Str__as_str(void *x, bool color, cord_info_t *info) {
    if (!x) return "Str";
    return Str__quoted(*(CORD*)x, color);
}

CORD Pointer__as_str(void *x, bool color, cord_info_t *info)
{
    static const sigils[] = {['@']="@", ['?']="?", ['&']="&", ['!']="!"};
    if (!x)
        return CORD_cat(sigils[info->pointer.sigil], info->pointer.func(NULL, color, info->pointer.info));
    void *ptr = *(void**)x;
    CORD pointed = info->pointer.func(ptr, color, info->pointer.info);
    return CORD_cat(ptr ? sigils[info->pointer.sigil] : "!", pointed);
}

CORD Array__as_str(void *x, bool color, cord_info_t *info)
{
    if (!x)
        return CORD_asprintf("[%r]", info->array.func(NULL, color, info->array.info));
    CORD cord = "[";
    array_t *arr = x;
    for (int64_t i = 0; i < arr->length; i++) {
        if (i > 0) cord = CORD_cat(cord, ", ");
        cord = CORD_cat(cord, info->array.func(arr->data + i*arr->stride, color, info->array.info));
    }
    return CORD_cat(cord, "]");
}

CORD Table__as_str(void *x, bool color, cord_info_t *info)
{
    if (!x) {
        return CORD_asprintf(
            "{%r=>%r}",
            info->table.key_func(NULL, color, info->table.key_info),
            info->table.value_func(NULL, color, info->table.value_info));
    }
    CORD cord = "{";
    table_t *table = x;
    array_t entries = table->entries;
    for (int64_t i = 0; i < entries.length; i++) {
        if (i > 0) cord = CORD_cat(cord, ", ");
        void *key = entries.data + i*entries.stride;
        cord = CORD_cat(cord, info->table.key_func(key, color, info->table.key_info));
        cord = CORD_cat(cord, "=");
        cord = CORD_cat(cord, info->table.value_func(key + info->table.value_offset, color, info->table.value_info));
    }
    if (table->fallback) {
        cord = CORD_cat(cord, "; fallback=");
        cord = CORD_cat(cord, Table__as_str(table->fallback, color, info));
        if (table->default_value) cord = CORD_cat(cord, ", ");
    } else if (table->default_value) {
        if (table->default_value) cord = CORD_cat(cord, "; ");
    }
    if (table->default_value) {
        cord = CORD_cat(cord, "default=");
        cord = CORD_cat(cord, info->table.value_func(table->default_value, color, info->table.value_info));
    }
    return CORD_cat(cord, "}");
}

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
