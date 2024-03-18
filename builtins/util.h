#pragma once

// Built-in utility functions

#include <assert.h>
#include <gc.h>
#include <gc/cord.h>
#include <stdio.h>
#include <string.h>
#include <err.h>

#define streq(a, b) (((a) == NULL && (b) == NULL) || (((a) == NULL) == ((b) == NULL) && strcmp(a, b) == 0))
#define new(t, ...) ((t*)memcpy(GC_MALLOC(sizeof(t)), &(t){__VA_ARGS__}, sizeof(t)))
#define copy(obj_ptr) ((__typeof(obj_ptr))memcpy(GC_MALLOC(sizeof(*(obj_ptr))), obj_ptr, sizeof(*(obj_ptr))))
#define grow(arr, new_size) ((typeof (arr))GC_REALLOC(arr, (sizeof(arr[0]))*(new_size)))
#define Match(x, _tag) ((x)->tag == _tag ? &(x)->__data._tag : (errx(1, __FILE__ ":%d This was supposed to be a " # _tag "\n", __LINE__), &(x)->__data._tag))
#define Tagged(t, _tag, ...) new(t, .tag=_tag, .__data._tag={__VA_ARGS__})

#ifndef auto
#define auto __auto_type
#endif

#ifndef public
#define public __attribute__ ((visibility ("default")))
#endif

char *heap_strn(const char *str, size_t len);
char *heap_str(const char *str);
char *heap_strf(const char *fmt, ...);
CORD CORD_asprintf(CORD fmt, ...);
#define CORD_appendf(cord, fmt, ...) CORD_sprintf(cord, "%r" fmt, *(cord) __VA_OPT__(,) __VA_ARGS__)
#define CORD_all(...) CORD_catn(sizeof((CORD[]){__VA_ARGS__})/sizeof(CORD), __VA_ARGS__)

#define asprintfa(...) ({ char *_buf = alloca(snprintf(0, 0, __VA_ARGS__)); sprintf(_buf, __VA_ARGS__); _buf; })

#define REVERSE_LIST(list) do { \
    __typeof(list) _prev = NULL; \
    __typeof(list) _next = NULL; \
    auto _current = list; \
    while (_current != NULL) { \
        _next = _current->next; \
        _current->next = _prev; \
        _prev = _current; \
        _current = _next; \
    } \
    list = _prev; \
} while(0)

#define LIST_MAP(src, var, ...) ({\
    __typeof(src) __mapped = NULL; \
    __typeof(src) *__next = &__mapped; \
    for (__typeof(src) var = src; var; var = var->next) { \
        *__next = GC_MALLOC(sizeof(__typeof(*(src)))); \
        **__next = *var; \
        **__next = (__typeof(*(src))){__VA_ARGS__}; \
        __next = &((*__next)->next); \
    } \
    __mapped; })


// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
