#pragma once
#include <gc/cord.h>
#include <stdbool.h>
#include <stdint.h>

#include "types.h"

#define String_t CORD
#define Str_t CORD

typedef struct {
    CORD *data;
    unsigned long int length:42;
    unsigned short int free:4, cow:1, atomic:1;
    short int stride:16;
} Str_Array_t;

typedef enum { WHERE_ANYWHERE, WHERE_START, WHERE_END } where_e;

typedef struct {
    enum { FIND_FAILURE, FIND_SUCCESS } status;
    int32_t index;
} find_result_t;

typedef struct {
    TypeInfo type;
    CORD (*uppercased)(CORD s);
    CORD (*lowercased)(CORD s);
    CORD (*titlecased)(CORD s);
    bool (*has)(CORD s, CORD prefix, where_e where);
    bool (*ends_with)(CORD s, CORD suffix);
    CORD (*without)(CORD s, CORD target, where_e where);
    CORD (*without_suffix)(CORD s, CORD suffix);
    CORD (*trimmed)(CORD s, CORD trim_chars, where_e where);
    CORD (*slice)(CORD s, int64_t first, int64_t stride, int64_t length);
    char *(*c_string)(CORD str);
    CORD (*from_c_string)(char *str);
    find_result_t (*find)(CORD str, CORD pat);
    CORD (*replace)(CORD text, CORD pat, CORD replacement, int64_t limit);
    CORD (*quoted)(CORD text, bool colorize);
    Str_Array_t (*split)(CORD str, CORD split_chars);
    CORD (*join)(CORD glue, Str_Array_t pieces);
    bool (*equal)(CORD *x, CORD *y);
    int32_t (*compare)(CORD *x, CORD *y);
    int (*hash)(CORD *s, TypeInfo *type);
    CORD (*cord)(CORD *s, bool colorize, TypeInfo *type);
} Str_namespace_t;
extern Str_namespace_t Str;

CORD Str__as_str(const void *str, bool colorize, const TypeInfo *info);
CORD Str__quoted(CORD str, bool colorize);
int Str__compare(CORD *x, CORD *y);
bool Str__equal(CORD *x, CORD *y);
uint32_t Str__hash(CORD *cord);
CORD Str__uppercased(CORD str);
CORD Str__lowercased(CORD str);
CORD Str__titlecased(CORD str);
bool Str__has(CORD str, CORD target, where_e where);
CORD Str__without(CORD str, CORD target, where_e where);
CORD Str__trimmed(CORD str, CORD skip, where_e where);
CORD Str__slice(CORD str, int64_t first, int64_t stride, int64_t length);
find_result_t Str__find(CORD str, CORD pat);
CORD Str__replace(CORD text, CORD pat, CORD replacement, int64_t limit);
Str_Array_t Str__split(CORD str, CORD split);
CORD Str__join(CORD glue, Str_Array_t pieces);

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
