#pragma once

#include <assert.h>
#include <gc.h>
#include <gc/cord.h>
#include <stdio.h>
#include <string.h>
#include <err.h>

#define streq(a, b) (((a) == NULL && (b) == NULL) || (((a) == NULL) == ((b) == NULL) && strcmp(a, b) == 0))
#define new(t, ...) ((t*)memcpy(GC_MALLOC(sizeof(t)), &(t){__VA_ARGS__}, sizeof(t)))
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
CORD CORD_asprintf(const char *fmt, ...);

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
