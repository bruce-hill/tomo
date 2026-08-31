// Representation of closures (functions paired with captured data)

#pragma once

#include <stddef.h>

typedef struct {
    void *fn, *userdata;
} Closure_t;

typedef Closure_t OptionalClosure_t;

#define NONE_CLOSURE ((OptionalClosure_t){.fn = NULL})
