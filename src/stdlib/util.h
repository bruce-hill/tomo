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
#define check_initialized(var, init_var, name) *({ if (!init_var) fail("The variable " name " is being accessed before it has been initialized!"); \
                                       &var; })

#define IF_DECLARE(decl, expr, block) if (({ decl; expr ? ({ block; 1; }) : 0; })) {}

#define WHEN(type, subj, var, body) { type var = subj; switch (var.々tag) body }

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

#ifndef likely
#define likely(x) (__builtin_expect(!!(x), 1))
#endif

#ifndef unlikely
#define unlikely(x) (__builtin_expect(!!(x), 0))
#endif

// GCC lets you define macro-like functions which are always inlined and never
// compiled using this combination of flags. See: https://gcc.gnu.org/onlinedocs/gcc/Inline.html
#ifndef MACROLIKE
#ifdef __TINYC__
#define MACROLIKE static inline __attribute__((gnu_inline, always_inline))
#else
#define MACROLIKE extern inline __attribute__((gnu_inline, always_inline))
#endif
#endif

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
