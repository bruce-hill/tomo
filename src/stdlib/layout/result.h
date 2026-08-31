// Representation of the Result enum (Success/Failure)

#pragma once

#include <stdbool.h>

#include "text.h"

typedef struct Result$Success$$struct {
} Result$Success$$type;

typedef struct {
    Result$Success$$type value;
    bool has_value;
} $OptionalResult$Success$$type;

typedef struct Result$Failure$$struct {
    Text_t reason;
} Result$Failure$$type;

typedef struct {
    Result$Failure$$type value;
    bool has_value;
} $OptionalResult$Failure$$type;

typedef struct Result$$struct {
    enum { Result$tag$none, Result$tag$Success, Result$tag$Failure } $tag;
    union {
        Result$Success$$type Success;
        Result$Failure$$type Failure;
    };
} Result$$type;

typedef Result$$type Result_t;
