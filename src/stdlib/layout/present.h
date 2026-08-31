// Representation of the Present type

#pragma once

#include <stdbool.h>

typedef struct Present$$struct {
} Present$$type;

#define PRESENT_STRUCT ((Present$$type){})

typedef struct {
    Present$$type value;
    bool has_value;
} $OptionalPresent$$type;

#define NONE_PRESENT_STRUCT (($OptionalPresent$$type){.has_value = false})
#define OPTIONAL_PRESENT_STRUCT (($OptionalPresent$$type){.has_value = true})
