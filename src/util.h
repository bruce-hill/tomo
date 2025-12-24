#pragma once

#include <gc.h> // IWYU pragma: export

#include "./stdlib/util.h" // IWYU pragma: export

#define new(t, ...) ((t *)memcpy(GC_MALLOC(sizeof(t)), &(t){__VA_ARGS__}, sizeof(t)))
