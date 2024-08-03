#pragma once

// Hash table datastructure with methods and type information

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "array.h"
#include "datatypes.h"
#include "types.h"
#include "util.h"

#define Table(key_t, val_t, key_info, value_info, fb, def, N, ...)  ({ \
    struct { key_t k; val_t v; } ents[N] = {__VA_ARGS__}; \
    table_t table = Table$from_entries((array_t){ \
                       .data=memcpy(GC_MALLOC(sizeof(ents)), ents, sizeof(ents)), \
                       .length=sizeof(ents)/sizeof(ents[0]), \
                       .stride=(void*)&ents[1] - (void*)&ents[0], \
                       }, $TableInfo(key_info, value_info)); \
    table.fallback = fb; \
    table.default_value = def; \
    table; })
#define Table_get(table_expr, key_t, val_t, key_expr, info_expr, filename, start, end) ({ \
    const table_t t = table_expr; key_t k = key_expr; const TypeInfo* info = info_expr; \
    const val_t *v = Table$get(t, &k, info); \
    if (__builtin_expect(v == NULL, 0)) \
        fail_source(filename, start, end, "The key %r is not in this table\n", generic_as_text(&k, no, info->TableInfo.key)); \
    *v; })

table_t Table$from_entries(array_t entries, const TypeInfo *type);
void *Table$get(table_t t, const void *key, const TypeInfo *type);
void *Table$get_raw(table_t t, const void *key, const TypeInfo *type);
void *Table$entry(table_t t, int64_t n);
void *Table$reserve(table_t *t, const void *key, const void *value, const TypeInfo *type);
void Table$set(table_t *t, const void *key, const void *value, const TypeInfo *type);
#define Table$set_value(t, key_expr, value_expr, type) ({ __typeof(key_expr) k = key_expr; __typeof(value_expr) v = value_expr; \
                                                        Table$set(t, &k, &v, type); })
#define Table$reserve_value(t, key_expr, type) ({ __typeof(key_expr) k = key_expr; Table$reserve(t, &k, NULL, type); })
void Table$remove(table_t *t, const void *key, const TypeInfo *type);
void Table$clear(table_t *t);
table_t Table$sorted(table_t t, const TypeInfo *type);
void Table$mark_copy_on_write(table_t *t);
int32_t Table$compare(const table_t *x, const table_t *y, const TypeInfo *type);
bool Table$equal(const table_t *x, const table_t *y, const TypeInfo *type);
uint32_t Table$hash(const table_t *t, const TypeInfo *type);
CORD Table$as_text(const table_t *t, bool colorize, const TypeInfo *type);

void *Table$str_entry(table_t t, int64_t n);
void *Table$str_get(table_t t, const char *key);
void *Table$str_get_raw(table_t t, const char *key);
void Table$str_set(table_t *t, const char *key, const void *value);
void *Table$str_reserve(table_t *t, const char *key, const void *value);
void Table$str_remove(table_t *t, const char *key);

#define Table$length(t) ((t).entries.length)

extern const TypeInfo StrToVoidStarTable;

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1
