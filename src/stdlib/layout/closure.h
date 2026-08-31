// Representation of closures (functions paired with captured data)

#pragma once

typedef struct {
    void *fn, *userdata;
} Closure_t;

typedef Closure_t OptionalClosure_t;
