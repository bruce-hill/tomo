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
    table_t table = Table$from_entries((Array_t){ \
                       .data=memcpy(GC_MALLOC(sizeof(ents)), ents, sizeof(ents)), \
                       .length=sizeof(ents)/sizeof(ents[0]), \
                       .stride=(void*)&ents[1] - (void*)&ents[0], \
                       }, $TableInfo(key_info, value_info)); \
    table.fallback = fb; \
    table; })
#define Set(item_t, item_info, N, ...)  ({ \
    item_t ents[N] = {__VA_ARGS__}; \
    table_t set = Table$from_entries((Array_t){ \
                       .data=memcpy(GC_MALLOC(sizeof(ents)), ents, sizeof(ents)), \
                       .length=sizeof(ents)/sizeof(ents[0]), \
                       .stride=(void*)&ents[1] - (void*)&ents[0], \
                       }, $SetInfo(item_info)); \
    set; })

table_t Table$from_entries(Array_t entries, const TypeInfo *type);
void *Table$get(table_t t, const void *key, const TypeInfo *type);
#define Table$get_value_or_fail(table_expr, key_t, val_t, key_expr, info_expr, start, end) ({ \
    const table_t t = table_expr; key_t k = key_expr; const TypeInfo* info = info_expr; \
    val_t *v = Table$get(t, &k, info); \
    if (__builtin_expect(v == NULL, 0)) \
        fail_source(__SOURCE_FILE__, start, end, "The key %r is not in this table\n", generic_as_text(&k, no, info->TableInfo.key)); \
    *v; })
#define Table$get_value_or_default(table_expr, key_t, val_t, key_expr, default_val, info_expr) ({ \
    const table_t t = table_expr; const key_t k = key_expr; \
    val_t *v = Table$get(t, &k, info_expr); \
    v ? *v : default_val; })
#define Table$has_value(table_expr, key_expr, info_expr) ({ \
    const table_t t = table_expr; __typeof(key_expr) k = key_expr; \
    (Table$get(t, &k, info_expr) != NULL); })
void *Table$get_raw(table_t t, const void *key, const TypeInfo *type);
void *Table$entry(table_t t, int64_t n);
void *Table$reserve(table_t *t, const void *key, const void *value, const TypeInfo *type);
void Table$set(table_t *t, const void *key, const void *value, const TypeInfo *type);
#define Table$set_value(t, key_expr, value_expr, type) ({ __typeof(key_expr) k = key_expr; __typeof(value_expr) v = value_expr; \
                                                        Table$set(t, &k, &v, type); })
#define Table$reserve_value(t, key_expr, type) ({ __typeof(key_expr) k = key_expr; Table$reserve(t, &k, NULL, type); })
#define Table$bump(t_expr, key_expr, amount_expr, type) ({ __typeof(key_expr) key = key_expr; \
                                                         table_t *t = t_expr; \
                                                         __typeof(amount_expr) *val = Table$get_raw(*t, &key, type); \
                                                         if (val) *val += amount_expr; \
                                                         else { __typeof(amount_expr) init = amount_expr; Table$set(t, &key, &init, type); } (void)0; })
                                                    
void Table$remove(table_t *t, const void *key, const TypeInfo *type);
#define Table$remove_value(t, key_expr, type) ({ __typeof(key_expr) k = key_expr; Table$remove(t, &k, type); })

table_t Table$overlap(table_t a, table_t b, const TypeInfo *type);
table_t Table$with(table_t a, table_t b, const TypeInfo *type);
table_t Table$without(table_t a, table_t b, const TypeInfo *type);
bool Table$is_subset_of(table_t a, table_t b, bool strict, const TypeInfo *type);
bool Table$is_superset_of(table_t a, table_t b, bool strict, const TypeInfo *type);

void Table$clear(table_t *t);
table_t Table$sorted(table_t t, const TypeInfo *type);
void Table$mark_copy_on_write(table_t *t);
#define TABLE_INCREF(t) ({ ARRAY_INCREF((t).entries); if ((t).bucket_info) (t).bucket_info->data_refcount += ((t).bucket_info->data_refcount < TABLE_MAX_DATA_REFCOUNT); })
#define TABLE_COPY(t) ({ TABLE_INCREF(t); t; })
int32_t Table$compare(const table_t *x, const table_t *y, const TypeInfo *type);
bool Table$equal(const table_t *x, const table_t *y, const TypeInfo *type);
uint32_t Table$hash(const table_t *t, const TypeInfo *type);
Text_t Table$as_text(const table_t *t, bool colorize, const TypeInfo *type);

void *Table$str_entry(table_t t, int64_t n);
void *Table$str_get(table_t t, const char *key);
void *Table$str_get_raw(table_t t, const char *key);
void Table$str_set(table_t *t, const char *key, const void *value);
void *Table$str_reserve(table_t *t, const char *key, const void *value);
void Table$str_remove(table_t *t, const char *key);

#define Table$length(t) ((t).entries.length)

extern const TypeInfo CStrToVoidStarTable;

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1
