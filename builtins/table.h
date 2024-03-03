#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "types.h"
#include "datatypes.h"
#include "array.h"

#define $Table(key_t, val_t, key_info, value_info, fb, def, N, ...)  ({ \
    struct { key_t k; val_t v; } $ents[N] = {__VA_ARGS__}; \
    table_t $table = Table_from_entries((array_t){ \
                       .data=memcpy(GC_MALLOC(sizeof($ents)), $ents, sizeof($ents)), \
                       .length=sizeof($ents)/sizeof($ents[0]), \
                       .stride=(void*)&$ents[1] - (void*)&$ents[0], \
                       }, $TableInfo(key_info, value_info)); \
    $table.fallback = fb; \
    $table.default_value = def; \
    $table; })
#define $Table_get(table_expr, key_t, val_t, key_expr, info_expr, filename, start, end) ({ \
    const table_t *$t = table_expr; key_t $k = key_expr; const TypeInfo* $info = info_expr; \
    const val_t *$v = Table_get($t, &$k, $info); \
    if (__builtin_expect($v == NULL, 0)) \
        fail_source(filename, start, end, "The key %r is not in this table\n", generic_as_str(&$k, USE_COLOR, $info->TableInfo.key)); \
    *$v; })
#define $TABLE_FOREACH(table_expr, key_type, k, value_type, v, value_offset, body, else_body) {\
        array_t $entries = (table_expr).entries; \
        if ($entries.length == 0) else_body \
        else { \
            $ARRAY_INCREF($entries); \
            for (int64_t $i = 0; $i < $entries.length; $i++) { \
                key_type k = *(key_type*)($entries.data + $i*$entries.stride); \
                value_type v = *(value_type*)($entries.data + $i*$entries.stride + value_offset); \
                body \
            } \
            $ARRAY_DECREF($entries); \
        } \
    }

table_t Table_from_entries(array_t entries, const TypeInfo *type);
void *Table_get(const table_t *t, const void *key, const TypeInfo *type);
void *Table_get_raw(const table_t *t, const void *key, const TypeInfo *type);
void *Table_entry(const table_t *t, int64_t n);
void *Table_reserve(table_t *t, const void *key, const void *value, const TypeInfo *type);
void Table_set(table_t *t, const void *key, const void *value, const TypeInfo *type);
void Table_remove(table_t *t, const void *key, const TypeInfo *type);
void Table_clear(table_t *t);
void Table_mark_copy_on_write(table_t *t);
int32_t Table_compare(const table_t *x, const table_t *y, const TypeInfo *type);
bool Table_equal(const table_t *x, const table_t *y, const TypeInfo *type);
uint32_t Table_hash(const table_t *t, const TypeInfo *type);
CORD Table_as_str(const table_t *t, bool colorize, const TypeInfo *type);

void *Table_str_entry(const table_t *t, int64_t n);
void *Table_str_get(const table_t *t, const char *key);
void *Table_str_get_raw(const table_t *t, const char *key);
void Table_str_set(table_t *t, const char *key, const void *value);
void *Table_str_reserve(table_t *t, const char *key, const void *value);
void Table_str_remove(table_t *t, const char *key);

#define Table_length(t) ((t)->entries.length)

extern TypeInfo StrToVoidStarTable;

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1
