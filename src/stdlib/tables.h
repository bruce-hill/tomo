#pragma once

// Hash table datastructure with methods and type information

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "lists.h"
#include "datatypes.h"
#include "types.h"
#include "util.h"

#define Table(key_t, val_t, key_info, value_info, fb, N, ...)  ({ \
    struct { key_t k; val_t v; } ents[N] = {__VA_ARGS__}; \
    Table_t table = Tableヽfrom_entries((List_t){ \
                       .data=memcpy(GC_MALLOC(sizeof(ents)), ents, sizeof(ents)), \
                       .length=sizeof(ents)/sizeof(ents[0]), \
                       .stride=(void*)&ents[1] - (void*)&ents[0], \
                       }, Tableヽinfo(key_info, value_info)); \
    table.fallback = fb; \
    table; })
#define Set(item_t, item_info, N, ...)  ({ \
    item_t ents[N] = {__VA_ARGS__}; \
    Table_t set = Tableヽfrom_entries((List_t){ \
                       .data=memcpy(GC_MALLOC(sizeof(ents)), ents, sizeof(ents)), \
                       .length=sizeof(ents)/sizeof(ents[0]), \
                       .stride=(void*)&ents[1] - (void*)&ents[0], \
                       }, Setヽinfo(item_info)); \
    set; })

Table_t Tableヽfrom_entries(List_t entries, const TypeInfo_t *type);
void *Tableヽget(Table_t t, const void *key, const TypeInfo_t *type);
#define Tableヽget_optional(table_expr, key_t, val_t, key_expr, nonnull_var, nonnull_expr, null_expr, info_expr) ({ \
    const Table_t t = table_expr; const key_t k = key_expr; \
    val_t *nonnull_var = Tableヽget(t, &k, info_expr); \
    nonnull_var ? nonnull_expr : null_expr; })
#define Tableヽget_or_setdefault(table_expr, key_t, val_t, key_expr, default_expr, info_expr) ({ \
    Table_t *t = table_expr; const key_t k = key_expr; \
    if (t->entries.data_refcount > 0) Listヽcompact(&t->entries, sizeof(struct {key_t k; val_t v;})); \
    val_t *v = Tableヽget(*t, &k, info_expr); \
    v ? v : (val_t*)Tableヽreserve(t, &k, (val_t[1]){default_expr}, info_expr); })
#define Tableヽget_or_default(table_expr, key_t, val_t, key_expr, default_expr, info_expr) ({ \
    const Table_t t = table_expr; const key_t k = key_expr; \
    val_t *v = Tableヽget(t, &k, info_expr); \
    v ? *v : default_expr; })
#define Tableヽhas_value(table_expr, key_expr, info_expr) ({ \
    const Table_t t = table_expr; __typeof(key_expr) k = key_expr; \
    (Tableヽget(t, &k, info_expr) != NULL); })
PUREFUNC void *Tableヽget_raw(Table_t t, const void *key, const TypeInfo_t *type);
CONSTFUNC void *Tableヽentry(Table_t t, int64_t n);
void *Tableヽreserve(Table_t *t, const void *key, const void *value, const TypeInfo_t *type);
void Tableヽset(Table_t *t, const void *key, const void *value, const TypeInfo_t *type);
#define Tableヽset_value(t, key_expr, value_expr, type) ({ __typeof(key_expr) k = key_expr; __typeof(value_expr) v = value_expr; \
                                                        Tableヽset(t, &k, &v, type); })
void Tableヽremove(Table_t *t, const void *key, const TypeInfo_t *type);
#define Tableヽremove_value(t, key_expr, type) ({ __typeof(key_expr) k = key_expr; Tableヽremove(t, &k, type); })

Table_t Tableヽoverlap(Table_t a, Table_t b, const TypeInfo_t *type);
Table_t Tableヽwith(Table_t a, Table_t b, const TypeInfo_t *type);
Table_t Tableヽwithout(Table_t a, Table_t b, const TypeInfo_t *type);
Table_t Tableヽxor(Table_t a, Table_t b, const TypeInfo_t *type);
Table_t Tableヽwith_fallback(Table_t t, OptionalTable_t fallback);
PUREFUNC bool Tableヽis_subset_of(Table_t a, Table_t b, bool strict, const TypeInfo_t *type);
PUREFUNC bool Tableヽis_superset_of(Table_t a, Table_t b, bool strict, const TypeInfo_t *type);

void Tableヽclear(Table_t *t);
Table_t Tableヽsorted(Table_t t, const TypeInfo_t *type);
void Tableヽmark_copy_on_write(Table_t *t);
#define TABLE_INCREF(t) ({ LIST_INCREF((t).entries); if ((t).bucket_info) (t).bucket_info->data_refcount += ((t).bucket_info->data_refcount < TABLE_MAX_DATA_REFCOUNT); })
#define TABLE_COPY(t) ({ TABLE_INCREF(t); t; })
PUREFUNC int32_t Tableヽcompare(const void *x, const void *y, const TypeInfo_t *type);
PUREFUNC bool Tableヽequal(const void *x, const void *y, const TypeInfo_t *type);
PUREFUNC uint64_t Tableヽhash(const void *t, const TypeInfo_t *type);
Text_t Tableヽas_text(const void *t, bool colorize, const TypeInfo_t *type);
PUREFUNC bool Tableヽis_none(const void *obj, const TypeInfo_t*);

CONSTFUNC void *Tableヽstr_entry(Table_t t, int64_t n);
PUREFUNC void *Tableヽstr_get(Table_t t, const char *key);
PUREFUNC void *Tableヽstr_get_raw(Table_t t, const char *key);
void Tableヽstr_set(Table_t *t, const char *key, const void *value);
void *Tableヽstr_reserve(Table_t *t, const char *key, const void *value);
void Tableヽstr_remove(Table_t *t, const char *key);
void Tableヽserialize(const void *obj, FILE *out, Table_t *pointers, const TypeInfo_t *type);
void Tableヽdeserialize(FILE *in, void *outval, List_t *pointers, const TypeInfo_t *type);

#define Tableヽlength(t) ((t).entries.length)

extern const TypeInfo_t CStrToVoidStarTable;

#define Tableヽmetamethods { \
    .as_text=Tableヽas_text, \
    .compare=Tableヽcompare, \
    .equal=Tableヽequal, \
    .hash=Tableヽhash, \
    .is_none=Tableヽis_none, \
    .serialize=Tableヽserialize, \
    .deserialize=Tableヽdeserialize, \
}

#define Tableヽinfo(key_expr, value_expr) &((TypeInfo_t){.size=sizeof(Table_t), .align=__alignof__(Table_t), \
                                           .tag=TableInfo, .TableInfo.key=key_expr, .TableInfo.value=value_expr, .metamethods=Tableヽmetamethods})
#define Setヽinfo(item_info) &((TypeInfo_t){.size=sizeof(Table_t), .align=__alignof__(Table_t), \
                              .tag=TableInfo, .TableInfo.key=item_info, .TableInfo.value=&Voidヽinfo, .metamethods=Tableヽmetamethods})

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1
