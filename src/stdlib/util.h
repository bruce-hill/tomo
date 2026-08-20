// Built-in utility functions

#pragma once

#include <assert.h>
#include <err.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define streq(a, b) (((a) == NULL && (b) == NULL) || (((a) == NULL) == ((b) == NULL) && strcmp(a, b) == 0))
#define starts_with(line, prefix) (strncmp(line, prefix, strlen(prefix)) == 0)
#define ends_with(line, suffix)                                                                                        \
    (strlen(line) >= strlen(suffix) && strcmp(line + strlen(line) - strlen(suffix), suffix) == 0)
#define check_initialized(var, init_var, name)                                                                         \
    *({                                                                                                                \
        if (!init_var) fail_text(Text("The variable " name " is being accessed before it has been initialized!"));     \
        &var;                                                                                                          \
    })

#define MATCH(type, subj, var, body)                                                                                   \
    {                                                                                                                  \
        type var = subj;                                                                                               \
        switch (var.$tag)                                                                                              \
            body                                                                                                       \
    }

#ifndef public
#define public __attribute__((visibility("default")))
#endif

#ifndef PUREFUNC
#define PUREFUNC __attribute__((pure))
#endif

#ifndef CONSTFUNC
#define CONSTFUNC __attribute__((const))
#endif

#ifndef INLINE
#define INLINE inline __attribute__((always_inline))
#endif

#ifndef likely
#define likely(x) (__builtin_expect(!!(x), 1))
#endif

#ifndef unlikely
#define unlikely(x) (__builtin_expect(!!(x), 0))
#endif

// This combination of attributes defines macro-like functions which are always
// inlined and never emitted as standalone compiled functions:
#ifndef MACROLIKE
#define MACROLIKE extern inline __attribute__((gnu_inline, always_inline))
#endif

#ifndef GC_MALLOC
extern void *GC_malloc(size_t);
#define GC_MALLOC GC_malloc
#define heap(x) (__typeof(x) *)memcpy(GC_malloc(sizeof(x)), (__typeof(x)[1]){x}, sizeof(x))
#define stack(x) (__typeof(x) *)((__typeof(x)[1]){x})
#endif

// A single-item copy whose size is a runtime value (e.g. a container's
// per-element `padded_item_size`), not a compile-time constant. libc's
// memcpy() already has its own internal fast paths for small sizes, but none
// of that helps here: since the *compiler* can't see the size, it can't
// inline it away, so every call is a real, non-inlined function call (likely
// through an IFUNC dispatch) -- measured at ~5x the cost of a direct store
// for size 1 or 8. This macro is `always_inline`, so the size switch below
// gets resolved and folded down to one instruction at each call site where
// the compiler CAN see the actual runtime size take one of these common
// values (which it very often can, from a preceding `if`/`switch` on the
// same variable, or profile-guided branch prediction); larger or uncommon
// sizes fall back to plain memcpy(), where the call overhead is amortized
// over more bytes anyway.
MACROLIKE void memcpy_fixed(void *dest, const void *src, int64_t size) {
    switch (size) {
    case 1: *(uint8_t *)dest = *(uint8_t *)src; break;
    case 2: *(uint16_t *)dest = *(uint16_t *)src; break;
    case 4: *(uint32_t *)dest = *(uint32_t *)src; break;
    case 8: *(uint64_t *)dest = *(uint64_t *)src; break;
    default: memcpy(dest, src, (size_t)size); break;
    }
}
