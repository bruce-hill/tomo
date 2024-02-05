#pragma once
#include <gc/cord.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    char *data;
    int64_t length:42;
    uint8_t free:4, cow:1, atomic:1;
    int16_t stride:16;
} Str_t;

#define STRING(s) ((Str_t){.data=s, .length=(int32_t)(sizeof(s)), .stride=1, .free=0})

typedef struct {
    Str_t *data;
    unsigned long int length:42;
    unsigned short int free:4, cow:1, atomic:1;
    short int stride:16;
} Str_Array_t;


// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
