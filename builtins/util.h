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
#define copy(obj_ptr) ((__typeof(obj_ptr))memcpy(GC_MALLOC(sizeof(*(obj_ptr))), obj_ptr, sizeof(*(obj_ptr))))
#define Match(x, _tag) ((x)->tag == _tag ? &(x)->__data._tag : (errx(1, __FILE__ ":%d This was supposed to be a " # _tag "\n", __LINE__), &(x)->__data._tag))
#define Tagged(t, _tag, ...) new(t, .tag=_tag, .__data._tag={__VA_ARGS__})
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

extern bool USE_COLOR;

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
    __typeof(src) mapped = NULL; \
    __typeof(src) *next = &mapped; \
    for (__typeof(src) var = src; var; var = var->next) { \
        *next = GC_MALLOC(sizeof(__typeof(*(src)))); \
        **next = *var; \
        **next = (__typeof(*(src))){__VA_ARGS__}; \
        next = &((*next)->next); \
    } \
    mapped; })

__attribute__((format(printf, 1, 2)))
char *heap_strf(const char *fmt, ...);

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
