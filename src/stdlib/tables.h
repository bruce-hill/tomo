// Hash table datastructure with methods and type information

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "datatypes.h"
#include "lists.h"
#include "types.h"
#include "util.h"

#define Table(key_t, val_t, key_info, value_info, fb, N, ...)                                                          \
    ({                                                                                                                 \
        struct {                                                                                                       \
            key_t k;                                                                                                   \
            val_t v;                                                                                                   \
        } ents[N] = {__VA_ARGS__};                                                                                     \
        Table_t table = Table$from_entries(                                                                            \
            (List_t){                                                                                                  \
                .data = memcpy(GC_MALLOC(sizeof(ents)), ents, sizeof(ents)),                                           \
                .length = sizeof(ents) / sizeof(ents[0]),                                                              \
                .stride = (void *)&ents[1] - (void *)&ents[0],                                                         \
            },                                                                                                         \
            Table$info(key_info, value_info));                                                                         \
        table.fallback = fb;                                                                                           \
        table;                                                                                                         \
    })
#define Set(item_t, item_info, N, ...)                                                                                 \
    ({                                                                                                                 \
        item_t ents[N] = {__VA_ARGS__};                                                                                \
        Table_t set = Table$from_entries(                                                                              \
            (List_t){                                                                                                  \
                .data = memcpy(GC_MALLOC(sizeof(ents)), ents, sizeof(ents)),                                           \
                .length = sizeof(ents) / sizeof(ents[0]),                                                              \
                .stride = (void *)&ents[1] - (void *)&ents[0],                                                         \
            },                                                                                                         \
            Set$info(item_info));                                                                                      \
        set;                                                                                                           \
    })

Table_t Table$from_entries(List_t entries, const TypeInfo_t *type);
void *Table$get(Table_t t, const void *key, const TypeInfo_t *type);
#define Table$get_optional(table_expr, key_t, val_t, key_expr, nonnull_var, nonnull_expr, null_expr, info_expr)        \
    ({                                                                                                                 \
        const Table_t t = table_expr;                                                                                  \
        const key_t k = key_expr;                                                                                      \
        val_t *nonnull_var = Table$get(t, &k, info_expr);                                                              \
        nonnull_var ? nonnull_expr : null_expr;                                                                        \
    })
#define Table$get_or_setdefault(table_expr, key_t, val_t, key_expr, default_expr, info_expr)                           \
    ({                                                                                                                 \
        Table_t *t = table_expr;                                                                                       \
        const key_t k = key_expr;                                                                                      \
        if (t->entries.data_refcount > 0)                                                                              \
            List$compact(&t->entries, sizeof(struct {                                                                  \
                key_t k;                                                                                               \
                val_t v;                                                                                               \
            }));                                                                                                       \
        val_t *v = Table$get(*t, &k, info_expr);                                                                       \
        v ? v : (val_t *)Table$reserve(t, &k, (val_t[1]){default_expr}, info_expr);                                    \
    })
#define Table$get_or_default(table_expr, key_t, val_t, key_expr, default_expr, info_expr)                              \
    ({                                                                                                                 \
        const Table_t t = table_expr;                                                                                  \
        const key_t k = key_expr;                                                                                      \
        val_t *v = Table$get(t, &k, info_expr);                                                                        \
        v ? *v : default_expr;                                                                                         \
    })
#define Table$has_value(table_expr, key_expr, info_expr)                                                               \
    ({                                                                                                                 \
        const Table_t t = table_expr;                                                                                  \
        __typeof(key_expr) k = key_expr;                                                                               \
        (Table$get(t, &k, info_expr) != NULL);                                                                         \
    })
PUREFUNC void *Table$get_raw(Table_t t, const void *key, const TypeInfo_t *type);
CONSTFUNC void *Table$entry(Table_t t, int64_t n);
void *Table$reserve(Table_t *t, const void *key, const void *value, const TypeInfo_t *type);
void Table$set(Table_t *t, const void *key, const void *value, const TypeInfo_t *type);
#define Table$set_value(t, key_expr, value_expr, type)                                                                 \
    ({                                                                                                                 \
        __typeof(key_expr) k = key_expr;                                                                               \
        __typeof(value_expr) v = value_expr;                                                                           \
        Table$set(t, &k, &v, type);                                                                                    \
    })
void Table$remove(Table_t *t, const void *key, const TypeInfo_t *type);
#define Table$remove_value(t, key_expr, type)                                                                          \
    ({                                                                                                                 \
        __typeof(key_expr) k = key_expr;                                                                               \
        Table$remove(t, &k, type);                                                                                     \
    })

Table_t Table$overlap(Table_t a, Table_t b, const TypeInfo_t *type);
Table_t Table$with(Table_t a, Table_t b, const TypeInfo_t *type);
Table_t Table$without(Table_t a, Table_t b, const TypeInfo_t *type);
Table_t Table$xor(Table_t a, Table_t b, const TypeInfo_t *type);
Table_t Table$with_fallback(Table_t t, OptionalTable_t fallback);
PUREFUNC bool Table$is_subset_of(Table_t a, Table_t b, bool strict, const TypeInfo_t *type);
PUREFUNC bool Table$is_superset_of(Table_t a, Table_t b, bool strict, const TypeInfo_t *type);

void Table$clear(Table_t *t);
Table_t Table$sorted(Table_t t, const TypeInfo_t *type);
void Table$mark_copy_on_write(Table_t *t);
#define TABLE_INCREF(t)                                                                                                \
    ({                                                                                                                 \
        LIST_INCREF((t).entries);                                                                                      \
        if ((t).bucket_info)                                                                                           \
            (t).bucket_info->data_refcount += ((t).bucket_info->data_refcount < TABLE_MAX_DATA_REFCOUNT);              \
    })
#define TABLE_COPY(t)                                                                                                  \
    ({                                                                                                                 \
        TABLE_INCREF(t);                                                                                               \
        t;                                                                                                             \
    })
PUREFUNC int32_t Table$compare(const void *x, const void *y, const TypeInfo_t *type);
PUREFUNC bool Table$equal(const void *x, const void *y, const TypeInfo_t *type);
PUREFUNC uint64_t Table$hash(const void *t, const TypeInfo_t *type);
Text_t Table$as_text(const void *t, bool colorize, const TypeInfo_t *type);
PUREFUNC bool Table$is_none(const void *obj, const TypeInfo_t *);

CONSTFUNC void *Table$str_entry(Table_t t, int64_t n);
PUREFUNC void *Table$str_get(Table_t t, const char *key);
PUREFUNC void *Table$str_get_raw(Table_t t, const char *key);
void Table$str_set(Table_t *t, const char *key, const void *value);
void *Table$str_reserve(Table_t *t, const char *key, const void *value);
void Table$str_remove(Table_t *t, const char *key);
void Table$serialize(const void *obj, FILE *out, Table_t *pointers, const TypeInfo_t *type);
void Table$deserialize(FILE *in, void *outval, List_t *pointers, const TypeInfo_t *type);

#define Table$length(t) ((t).entries.length)

extern const TypeInfo_t CStrToVoidStarTable;

#define Table$metamethods                                                                                              \
    {                                                                                                                  \
        .as_text = Table$as_text,                                                                                      \
        .compare = Table$compare,                                                                                      \
        .equal = Table$equal,                                                                                          \
        .hash = Table$hash,                                                                                            \
        .is_none = Table$is_none,                                                                                      \
        .serialize = Table$serialize,                                                                                  \
        .deserialize = Table$deserialize,                                                                              \
    }

#define Table$info(key_expr, value_expr)                                                                               \
    &((TypeInfo_t){.size = sizeof(Table_t),                                                                            \
                   .align = __alignof__(Table_t),                                                                      \
                   .tag = TableInfo,                                                                                   \
                   .TableInfo.key = key_expr,                                                                          \
                   .TableInfo.value = value_expr,                                                                      \
                   .metamethods = Table$metamethods})
#define Set$info(item_info)                                                                                            \
    &((TypeInfo_t){.size = sizeof(Table_t),                                                                            \
                   .align = __alignof__(Table_t),                                                                      \
                   .tag = TableInfo,                                                                                   \
                   .TableInfo.key = item_info,                                                                         \
                   .TableInfo.value = &Void$info,                                                                      \
                   .metamethods = Table$metamethods})
