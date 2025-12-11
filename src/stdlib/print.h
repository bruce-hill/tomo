// This file defines some functions to make it easy to do formatted text
// without using printf style specifiers:
//
//     print(...) - print text
//     fprint(file, ...) - print text to file
//     print_err(...) - print an error message and exit with EXIT_FAILURE
//     String(...) - return an allocated string

#pragma once

#include <assert.h>
#include <gc.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <unistd.h>

#include "datatypes.h"
#include "mapmacro.h"

// GCC lets you define macro-like functions which are always inlined and never
// compiled using this combination of flags. See: https://gcc.gnu.org/onlinedocs/gcc/Inline.html
#ifndef PRINT_FN
#ifdef __TINYC__
#define PRINT_FN static inline __attribute__((gnu_inline, always_inline)) int
#else
#define PRINT_FN extern inline __attribute__((gnu_inline, always_inline)) int
#endif
#endif

typedef struct {
    uint64_t n;
    bool no_prefix;
    bool uppercase;
    int digits;
} hex_format_t;
#define hex(x, ...) ((hex_format_t){.n = x, __VA_ARGS__})

typedef struct {
    double d;
} hex_double_t;
#define hex_double(x, ...) ((hex_double_t){.d = x, __VA_ARGS__})

typedef struct {
    uint64_t n;
    bool no_prefix;
    int digits;
} oct_format_t;
#define oct(x, ...) ((oct_format_t){.n = x, __VA_ARGS__})

typedef struct {
    const char *str;
} quoted_t;
#define quoted(s) ((quoted_t){s})

typedef struct {
    const char *str;
    size_t length;
} string_slice_t;
#define string_slice(...) ((string_slice_t){__VA_ARGS__})

typedef struct {
    char c;
    int length;
} repeated_char_t;
#define repeated_char(ch, len) ((repeated_char_t){.c = ch, .length = len})

#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
#define FMT64 "ll"
#else
#define FMT64 "l"
#endif

int _print_int(FILE *f, int64_t x);
int _print_uint(FILE *f, uint64_t x);
int _print_double(FILE *f, double x);
int _print_real(FILE *f, Real_t x);
int _print_hex(FILE *f, hex_format_t hex);
int _print_hex_double(FILE *f, hex_double_t hex);
int _print_oct(FILE *f, oct_format_t oct);
PRINT_FN _print_float(FILE *f, float x) { return _print_double(f, (double)x); }
PRINT_FN _print_pointer(FILE *f, void *p) { return _print_hex(f, hex((uint64_t)p)); }
PRINT_FN _print_bool(FILE *f, bool b) { return fputs(b ? "yes" : "no", f); }
PRINT_FN _print_str(FILE *f, const char *s) { return fputs(s ? s : "(null)", f); }
int _print_char(FILE *f, char c);
int _print_quoted(FILE *f, quoted_t quoted);
PRINT_FN _print_string_slice(FILE *f, string_slice_t slice) {
    return slice.str ? fwrite(slice.str, 1, slice.length, f) : (size_t)fputs("(null)", f);
}
PRINT_FN _print_repeated_char(FILE *f, repeated_char_t repeated) {
    int len = 0;
    for (int n = 0; n < repeated.length; n++)
        len += fputc(repeated.c, f);
    return len;
}

extern int Text$print(FILE *stream, Text_t text);
extern int Path$print(FILE *stream, Path_t path);
extern int Int$print(FILE *f, Int_t i);
#ifndef _fprint1
#define _fprint1(f, x)                                                                                                 \
    _Generic((x),                                                                                                      \
        char *: _print_str,                                                                                            \
        const char *: _print_str,                                                                                      \
        char: _print_char,                                                                                             \
        bool: _print_bool,                                                                                             \
        int64_t: _print_int,                                                                                           \
        int32_t: _print_int,                                                                                           \
        int16_t: _print_int,                                                                                           \
        int8_t: _print_int,                                                                                            \
        uint64_t: _print_uint,                                                                                         \
        uint32_t: _print_uint,                                                                                         \
        uint16_t: _print_uint,                                                                                         \
        uint8_t: _print_uint,                                                                                          \
        float: _print_float,                                                                                           \
        double: _print_double,                                                                                         \
        Real_t: _print_real,                                                                                           \
        hex_format_t: _print_hex,                                                                                      \
        hex_double_t: _print_hex_double,                                                                               \
        oct_format_t: _print_oct,                                                                                      \
        quoted_t: _print_quoted,                                                                                       \
        string_slice_t: _print_string_slice,                                                                           \
        repeated_char_t: _print_repeated_char,                                                                         \
        Text_t: Text$print,                                                                                            \
        Path_t: Path$print,                                                                                            \
        Int_t: Int$print,                                                                                              \
        void *: _print_pointer)(f, x)
#endif

typedef struct {
    char **buffer;
    size_t *size;
    size_t position;
} gc_stream_t;

FILE *gc_memory_stream(char **buf, size_t *size);

#define _print(x) _n += _fprint1(_printing, x)
#define _fprint(f, ...)                                                                                                \
    ({                                                                                                                 \
        FILE *_printing = f;                                                                                           \
        int _n = 0;                                                                                                    \
        MAP_LIST(_print, __VA_ARGS__);                                                                                 \
        _n;                                                                                                            \
    })
#define fprint(f, ...) _fprint(f, __VA_ARGS__, "\n")
#define print(...) fprint(stdout, __VA_ARGS__)
#define fprint_inline(f, ...) _fprint(f, __VA_ARGS__)
#define print_inline(...) fprint_inline(stdout, __VA_ARGS__)
#define String(...)                                                                                                    \
    ({                                                                                                                 \
        char *_buf = NULL;                                                                                             \
        size_t _size = 0;                                                                                              \
        FILE *_stream = gc_memory_stream(&_buf, &_size);                                                               \
        assert(_stream);                                                                                               \
        _fprint(_stream, __VA_ARGS__);                                                                                 \
        fflush(_stream);                                                                                               \
        _buf;                                                                                                          \
    })
#define print_err(...)                                                                                                 \
    ({                                                                                                                 \
        fprint(stderr, "\033[31;1m", __VA_ARGS__, "\033[m");                                                           \
        exit(EXIT_FAILURE);                                                                                            \
    })
