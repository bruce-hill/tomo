// This file defines some of the helper functions used for printing values
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "fpconv.h"
#include "print.h"
#include "util.h"

public int _print_int(FILE *f, int64_t n)
{
    char buf[21] = {[20]=0}; // Big enough for INT64_MIN + '\0'
    char *p = &buf[19];
    bool negative = n < 0;

    if (n == 0)
        *(p--) = '0';

    for (; n > 0; n /= 10)
        *(p--) = '0' + (n % 10);

    if (negative)
        *(p--) = '-';

    return fwrite(p + 1, sizeof(char), (size_t)(&buf[19] - p), f);
}

public int _print_uint(FILE *f, uint64_t n)
{
    char buf[21] = {[20]=0}; // Big enough for UINT64_MAX + '\0'
    char *p = &buf[19];

    if (n == 0)
        *(p--) = '0';

    for (; n > 0; n /= 10)
        *(p--) = '0' + (n % 10);

    return fwrite(p + 1, sizeof(char), (size_t)(&buf[19] - p), f);
}

public int _print_hex(FILE *f, hex_format_t hex)
{
    int printed = 0;
    if (!hex.no_prefix) printed += fputs("0x", f);
    if (hex.digits > 0) {
        for (uint64_t n = hex.n; n > 0 && hex.digits > 0; n /= 16) {
            hex.digits -= 1;
        }
        for (; hex.digits > 0; hex.digits -= 1) {
            printed += fputc('0', f);
        }
    }
    char buf[9] = {[8]='\0'}; // Enough space for FFFFFFFF + '\0'
    char *p = &buf[7];
    for (uint64_t n = hex.n; n > 0; n /= 16) {
        uint8_t digit = n % 16;
        if (digit <= 9)
            *(p--) = '0' + digit;
        else if (hex.uppercase)
            *(p--) = 'A' + digit - 10;
        else
            *(p--) = 'a' + digit - 10;
    }
    printed += (int)fwrite(p + 1, sizeof(char), (size_t)(&buf[7] - p), f);
    return printed;
}

public int _print_oct(FILE *f, oct_format_t oct)
{
    int printed = 0;
    if (!oct.no_prefix) printed += fputs("0o", f);
    if (oct.digits > 0) {
        for (uint64_t n = oct.n; n > 0 && oct.digits > 0; n /= 8) {
            oct.digits -= 1;
        }
        for (; oct.digits > 0; oct.digits -= 1) {
            printed += fputc('0', f);
        }
    }
    char buf[12] = {[11]='\0'}; // Enough space for octal UINT64_MAX + '\0'
    char *p = &buf[10];
    for (uint64_t n = oct.n; n > 0; n /= 8) {
        *(p--) = '0' + (n % 8);
    }
    printed += (int)fwrite(p + 1, sizeof(char), (size_t)(&buf[10] - p), f);
    return printed;
}

public int _print_double(FILE *f, double n)
{
    static char buf[24];
    int len = fpconv_dtoa(n, buf);
    return (int)fwrite(buf, sizeof(char), (size_t)len, f);
}

public int _print_char(FILE *f, char c)
{
#define ESC(e) "'\\" e "'"
    const char *named[256] = {['\'']=ESC("'"), ['\\']=ESC("\\"),
        ['\n']=ESC("n"), ['\t']=ESC("t"), ['\r']=ESC("r"),
        ['\033']=ESC("e"), ['\v']=ESC("v"), ['\a']=ESC("a"), ['\b']=ESC("b")};
    const char *name = named[(uint8_t)c];
    if (name != NULL)
        return fputs(name, f);
    else if (isprint(c))
        return fputc('\'', f) + fputc(c, f) + fputc('\'', f);
    else
        return (fputs("'\\x", f) + _print_hex(f, hex((uint64_t)c, .digits=2, .no_prefix=true, .uppercase=true))
                + fputs("'", f));
#undef ESC
}

public int _print_quoted(FILE *f, quoted_t quoted)
{
#define ESC(e) "\\" e
    const char *named[256] = {['"']=ESC("\""), ['\\']=ESC("\\"),
        ['\n']=ESC("n"), ['\t']=ESC("t"), ['\r']=ESC("r"),
        ['\033']=ESC("e"), ['\v']=ESC("v"), ['\a']=ESC("a"), ['\b']=ESC("b")};
    int printed = fputc('"', f);
    for (const char *p = quoted.str; *p; p++) {
        const char *name = named[(uint8_t)*p];
        if (name != NULL) {
            printed += fputs(name, f);
        } else if (isprint(*p) || (uint8_t)*p > 0x7F) {
            printed += fputc(*p, f);
        } else {
            printed += fputs("\\x", f) + _print_hex(f, hex((uint64_t)*p, .digits=2, .no_prefix=true, .uppercase=true));
        }
    }
    printed += fputc('"', f);
#undef ESC
    return printed;
}

#if defined(__GLIBC__) && defined(_GNU_SOURCE)
    // GLIBC has fopencookie()
    static ssize_t _gc_stream_write(void *cookie, const char *buf, size_t size) {
        gc_stream_t *stream = (gc_stream_t *)cookie;
        if (stream->position + size + 1 > *stream->size)
            *stream->buffer = GC_REALLOC(*stream->buffer, (*stream->size += MAX(MAX(16UL, *stream->size/2UL), size + 1UL)));
        memcpy(&(*stream->buffer)[stream->position], buf, size);
        stream->position += size;
        (*stream->buffer)[stream->position] = '\0';
        return (ssize_t)size;
    }

    public FILE *gc_memory_stream(char **buf, size_t *size) {
        gc_stream_t *stream = GC_MALLOC(sizeof(gc_stream_t));
        stream->size = size;
        stream->buffer = buf;
        *stream->size = 16;
        *stream->buffer = GC_MALLOC_ATOMIC(*stream->size);
        (*stream->buffer)[0] = '\0';
        stream->position = 0;
        cookie_io_functions_t functions = {.write = _gc_stream_write};
        return fopencookie(stream, "w", functions);
    }
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
    // BSDs have funopen() and fwopen()
    static int _gc_stream_write(void *cookie, const char *buf, int size) {
        gc_stream_t *stream = (gc_stream_t *)cookie;
        if (stream->position + size + 1 > *stream->size)
            *stream->buffer = GC_REALLOC(*stream->buffer, (*stream->size += MAX(MAX(16UL, *stream->size/2UL), size + 1UL)));
        memcpy(&(*stream->buffer)[stream->position], buf, size);
        stream->position += size;
        (*stream->buffer)[stream->position] = '\0';
        return size;
    }

    public FILE *gc_memory_stream(char **buf, size_t *size) {
        gc_stream_t *stream = GC_MALLOC(sizeof(gc_stream_t));
        stream->size = size;
        stream->buffer = buf;
        *stream->size = 16;
        *stream->buffer = GC_MALLOC_ATOMIC(*stream->size);
        (*stream->buffer)[0] = '\0';
        stream->position = 0;
        return fwopen(stream, _gc_stream_write);
    }
#else
#    error "This platform doesn't support fopencookie() or funopen()!"
#endif

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
