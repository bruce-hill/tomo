#pragma once

// This file defines some functions to make it easy to parse simply formatted
// strings **correctly** without memory bugs.
//
//     strparse(input_str, format...) - parse a string
//
// Examples:
//
//     const char *filename; long line_num;
//     const char *err = NULL;
//     if ((err=strparse("foo.c:12", &filename, ":", &line_num)))
//         errx(1, "Failed to parse file:line at: ", err);
//
//     const char *item1, *item2;
//     if ((err=strparse("one, two", &item1, ",", PARSE_WHITESPACE, &item2)))
//         errx(1, "Failed to parse items at: ", err);

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "mapmacro.h"

typedef struct { char c; } some_char_t;
#define PARSE_SOME_OF(chars) ((some_char_t*)chars)
#define PARSE_WHITESPACE PARSE_SOME_OF(" \t\r\n\v")
typedef enum {PARSE_LITERAL, PARSE_LONG, PARSE_DOUBLE, PARSE_BOOL, PARSE_STRING, PARSE_SOME_OF} parse_type_e;

typedef struct { parse_type_e type; void *dest; } parse_element_t;

#define _parse_type(dest) _Generic((dest), \
    some_char_t*: PARSE_SOME_OF, \
    const char*: PARSE_LITERAL, \
    const char**: PARSE_STRING, \
    char**: PARSE_STRING, \
    double*: PARSE_DOUBLE, \
    long*: PARSE_LONG, \
    bool*: PARSE_BOOL)

#define as_void_star(x) ((void*)x)
#define strparse(str, ...) simpleparse(str, sizeof((const void*[]){__VA_ARGS__})/sizeof(void*), (parse_type_e[]){MAP_LIST(_parse_type, __VA_ARGS__)}, (void*[]){MAP_LIST(as_void_star, __VA_ARGS__)})
#define fparse(file, ...) ({ char *_file_contents = NULL; size_t _file_len; \
                           (void)getdelim(&_file_contents, &_file_len, '\0', file); \
                           const char *_parse_err = strparse(_file_contents, __VA_ARGS__); \
                           free(_file_contents); \
                           _parse_err; })

const char *simpleparse(const char *str, int n, parse_type_e types[n], void *destinations[n]);

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
