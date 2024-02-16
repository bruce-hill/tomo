#pragma once
#include <gc/cord.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    CORD *data;
    unsigned long int length:42;
    unsigned short int free:4, cow:1, atomic:1;
    short int stride:16;
} Str_Array_t;


// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
