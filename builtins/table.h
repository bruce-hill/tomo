#pragma once

// Hash table datastructure with methods and type information

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "array.h"
#include "datatypes.h"
#include "types.h"
#include "util.h"

#define Table(key_t, val_t, key_info, value_info, fb, N, ...)  ({ \
    struct { key_t k; val_t v; } ents[N] = {__VA_ARGS__}; \
    Table_t table = Table$from_entries((Array_t){ \
                       .data=memcpy(GC_MALLOC(sizeof(ents)), ents, sizeof(ents)), \
                       .length=sizeof(ents)/sizeof(ents[0]), \
                       .stride=(void*)&ents[1] - (void*)&ents[0], \
                       }, Table$info(key_info, value_info)); \
    table.fallback = fb; \
    table; })
#define Set(item_t, item_info, N, ...)  ({ \
    item_t ents[N] = {__VA_ARGS__}; \
    Table_t set = Table$from_entries((Array_t){ \
                       .data=memcpy(GC_MALLOC(sizeof(ents)), ents, sizeof(ents)), \
                       .length=sizeof(ents)/sizeof(ents[0]), \
                       .stride=(void*)&ents[1] - (void*)&ents[0], \
                       }, Set$info(item_info)); \
    set; })

Table_t Table$from_entries(Array_t entries, const TypeInfo *type);
void *Table$get(Table_t t, const void *key, const TypeInfo *type);
#define Table$get_optional(table_expr, key_t, val_t, key_expr, nonnull_var, nonnull_expr, null_expr, info_expr) ({ \
    const Table_t t = table_expr; const key_t k = key_expr; \
    val_t *nonnull_var = Table$get(t, &k, info_expr); \
    nonnull_var ? nonnull_expr : null_expr; })
#define Table$has_value(table_expr, key_expr, info_expr) ({ \
    const Table_t t = table_expr; __typeof(key_expr) k = key_expr; \
    (Table$get(t, &k, info_expr) != NULL); })
PUREFUNC void *Table$get_raw(Table_t t, const void *key, const TypeInfo *type);
CONSTFUNC void *Table$entry(Table_t t, int64_t n);
void *Table$reserve(Table_t *t, const void *key, const void *value, const TypeInfo *type);
void Table$set(Table_t *t, const void *key, const void *value, const TypeInfo *type);
#define Table$set_value(t, key_expr, value_expr, type) ({ __typeof(key_expr) k = key_expr; __typeof(value_expr) v = value_expr; \
                                                        Table$set(t, &k, &v, type); })
#define Table$reserve_value(t, key_expr, type) ({ __typeof(key_expr) k = key_expr; Table$reserve(t, &k, NULL, type); })
#define Table$bump(t_expr, key_expr, amount_expr, type) ({ __typeof(key_expr) key = key_expr; \
                                                         Table_t *t = t_expr; \
                                                         __typeof(amount_expr) *val = Table$get_raw(*t, &key, type); \
                                                         if (val) *val += amount_expr; \
                                                         else { __typeof(amount_expr) init = amount_expr; Table$set(t, &key, &init, type); } (void)0; })
                                                    
void Table$remove(Table_t *t, const void *key, const TypeInfo *type);
#define Table$remove_value(t, key_expr, type) ({ __typeof(key_expr) k = key_expr; Table$remove(t, &k, type); })

Table_t Table$overlap(Table_t a, Table_t b, const TypeInfo *type);
Table_t Table$with(Table_t a, Table_t b, const TypeInfo *type);
Table_t Table$without(Table_t a, Table_t b, const TypeInfo *type);
PUREFUNC bool Table$is_subset_of(Table_t a, Table_t b, bool strict, const TypeInfo *type);
PUREFUNC bool Table$is_superset_of(Table_t a, Table_t b, bool strict, const TypeInfo *type);

void Table$clear(Table_t *t);
Table_t Table$sorted(Table_t t, const TypeInfo *type);
void Table$mark_copy_on_write(Table_t *t);
#define TABLE_INCREF(t) ({ ARRAY_INCREF((t).entries); if ((t).bucket_info) (t).bucket_info->data_refcount += ((t).bucket_info->data_refcount < TABLE_MAX_DATA_REFCOUNT); })
#define TABLE_COPY(t) ({ TABLE_INCREF(t); t; })
PUREFUNC int32_t Table$compare(const Table_t *x, const Table_t *y, const TypeInfo *type);
PUREFUNC bool Table$equal(const Table_t *x, const Table_t *y, const TypeInfo *type);
PUREFUNC uint64_t Table$hash(const Table_t *t, const TypeInfo *type);
Text_t Table$as_text(const Table_t *t, bool colorize, const TypeInfo *type);

CONSTFUNC void *Table$str_entry(Table_t t, int64_t n);
PUREFUNC void *Table$str_get(Table_t t, const char *key);
PUREFUNC void *Table$str_get_raw(Table_t t, const char *key);
void Table$str_set(Table_t *t, const char *key, const void *value);
void *Table$str_reserve(Table_t *t, const char *key, const void *value);
void Table$str_remove(Table_t *t, const char *key);

#define Table$length(t) ((t).entries.length)

extern const TypeInfo CStrToVoidStarTable;

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1
