#pragma once

// Built-in utility functions

#include <assert.h>
#include <gc.h>
#include <stdbool.h>
#include <string.h>
#include <err.h>

#define streq(a, b) (((a) == NULL && (b) == NULL) || (((a) == NULL) == ((b) == NULL) && strcmp(a, b) == 0))
#define starts_with(line, prefix) (strncmp(line, prefix, strlen(prefix)) == 0)
#define ends_with(line, suffix) (strlen(line) >= strlen(suffix) && strcmp(line + strlen(line) - strlen(suffix), suffix) == 0)
#define new(t, ...) ((t*)memcpy(GC_MALLOC(sizeof(t)), &(t){__VA_ARGS__}, sizeof(t)))
#define heap(x) (__typeof(x)*)memcpy(GC_MALLOC(sizeof(x)), (__typeof(x)[1]){x}, sizeof(x))
#define stack(x) (__typeof(x)*)((__typeof(x)[1]){x})
#define check_initialized(var, name) *({ if (!var ## $initialized) fail("The variable " name " is being accessed before it has been initialized!"); \
                                       &var; })

#ifndef auto
#define auto __auto_type
#endif

#ifndef public
#define public __attribute__ ((visibility ("default")))
#endif

#ifndef PUREFUNC
#define PUREFUNC __attribute__ ((pure))
#endif

#ifndef CONSTFUNC
#define CONSTFUNC __attribute__ ((const))
#endif

#ifndef INLINE
#define INLINE inline __attribute__ ((always_inline))
#endif

__attribute__((format(printf, 1, 2)))
char *heap_strf(const char *fmt, ...);

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
