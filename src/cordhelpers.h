#pragma once
// Some helper functions for the GC Cord library

#include <gc/cord.h>

#define CORD_appendf(cord, fmt, ...) CORD_sprintf(cord, "%r" fmt, *(cord), __VA_ARGS__)
#define CORD_all(...) CORD_catn(sizeof((CORD[]){__VA_ARGS__})/sizeof(CORD), __VA_ARGS__)

__attribute__((format(printf, 1, 2)))
CORD CORD_asprintf(CORD fmt, ...);
CORD CORD_quoted(CORD str);
CORD CORD_replace(CORD c, CORD to_replace, CORD replacement);

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
