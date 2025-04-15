#pragma once

// This file defines some functions to make it easy to do formatted text
// without using printf style specifiers:
//
//     print(...) - print text
//     fprint(file, ...) - print text to file
//     print_err(...) - print an error message and exit with EXIT_FAILURE
//     String(...) - return an allocated string
//
// If you put `#define PRINT_COLOR 1` before the import, text will be printed
// with terminal colors.

#include <assert.h>
#include <ctype.h>
#include <gc.h>
#include <gc/cord.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <unistd.h>

#include "datatypes.h"
#include "mapmacro.h"

#ifndef PRINT_COLOR
#define PRINT_COLOR 0
#endif

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
#define hex(x, ...) ((hex_format_t){.n=x, __VA_ARGS__})

typedef struct {
    uint64_t n;
    bool no_prefix;
    int digits;
} oct_format_t;
#define oct(x, ...) ((oct_format_t){.n=x, __VA_ARGS__})

typedef struct {
    double n;
    int precision;
} num_format_t;
#define num_fmt(x, ...) ((num_format_t){.n=x, __VA_ARGS__})

typedef struct {
    const char *str;
} quoted_t;
#define quoted(s) ((quoted_t){s})

typedef struct {
    const char *str;
    size_t length;
} string_slice_t;
#define string_slice(...) ((string_slice_t){__VA_ARGS__})

#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
#define FMT64 "ll"
#else
#define FMT64 "l"
#endif

#if PRINT_COLOR
#define hl(s) "\033[35m" s "\033[m"
#else
#define hl(s) s
#endif
PRINT_FN _print_int(FILE *f, int64_t x) { return fprintf(f, hl("%"FMT64"d"), x); }
PRINT_FN _print_uint(FILE *f, uint64_t x) { return fprintf(f, hl("%"FMT64"u"), x); }
PRINT_FN _print_float(FILE *f, float x) { return fprintf(f, hl("%g"), (double)x); }
PRINT_FN _print_double(FILE *f, double x) { return fprintf(f, hl("%g"), x); }
PRINT_FN _print_pointer(FILE *f, void *p) { return fprintf(f, hl("%p"), p); }
PRINT_FN _print_bool(FILE *f, bool b) { return fputs(b ? hl("yes") : hl("no"), f); }
PRINT_FN _print_str(FILE *f, const char *s) { return fputs(s ? s : "(null)", f); }
int _print_char(FILE *f, char c);
int _print_quoted(FILE *f, quoted_t quoted);
PRINT_FN _print_string_slice(FILE *f, string_slice_t slice) { return slice.str ? fwrite(slice.str, 1, slice.length, f) : (size_t)fputs("(null)", f); }
PRINT_FN _print_hex(FILE *f, hex_format_t hex) {
    return fprintf(f, hex.no_prefix ? (hex.uppercase ? hl("%0*"FMT64"X") : hl("%0*"FMT64"x")) : (hex.uppercase ? hl("0x%0*"FMT64"X") : hl("%#0*"FMT64"x")), hex.digits, hex.n);
}
PRINT_FN _print_oct(FILE *f, oct_format_t oct) {
    return fprintf(f, oct.no_prefix ? hl("%0*"FMT64"o") : hl("%#0*"FMT64"o"), oct.digits, oct.n);
}
PRINT_FN _print_num_format(FILE *f, num_format_t num) {
    return fprintf(f, hl("%.*lf"), num.precision, num.n);
}
#undef hl

extern int Text$print(FILE *stream, Text_t text);
extern int Path$print(FILE *stream, Path_t path);
extern int Int$print(FILE *f, Int_t i);
#ifndef _fprint1
#define _fprint1(f, x) _Generic((x), \
    char*: _print_str, \
    const char*: _print_str, \
    char: _print_char, \
    bool: _print_bool, \
    int64_t: _print_int, \
    int32_t: _print_int, \
    int16_t: _print_int, \
    int8_t: _print_int, \
    uint64_t: _print_uint, \
    uint32_t: _print_uint, \
    uint16_t: _print_uint, \
    uint8_t: _print_uint, \
    float: _print_float, \
    double: _print_double, \
    hex_format_t: _print_hex, \
    oct_format_t: _print_oct, \
    num_format_t: _print_num_format, \
    quoted_t: _print_quoted, \
    string_slice_t: _print_string_slice, \
    Text_t: Text$print, \
    Path_t: Path$print, \
    Int_t: Int$print, \
    void*: _print_pointer)(f, x)
#endif

typedef struct {
    char **buffer;
    size_t *size;
    size_t position;
} gc_stream_t;

FILE *gc_memory_stream(char **buf, size_t *size);

#define _print(x) _n += _fprint1(_printing, x)
#define _fprint(f, ...) ({ FILE *_printing = f; int _n = 0; MAP_LIST(_print, __VA_ARGS__); _n; })
#define fprint(f, ...) _fprint(f, __VA_ARGS__, "\n")
#define print(...) fprint(stdout, __VA_ARGS__)
#define fprint_inline(f, ...) _fprint(f, __VA_ARGS__)
#define print_inline(...) fprint_inline(stdout, __VA_ARGS__)
#define String(...) ({ \
    char *_buf = NULL; \
    size_t _size = 0; \
    FILE *_stream = gc_memory_stream(&_buf, &_size); \
    assert(_stream); \
    _fprint(_stream, __VA_ARGS__); \
    fflush(_stream); \
    _buf; })
#define print_err(...) ({ fprint(stderr, __VA_ARGS__); exit(EXIT_FAILURE); })

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
