// This file defines some of the helper functions used for printing values
#include "print.h"
#include "util.h"

#include <stdio.h>

public int _print_char(FILE *f, char c)
{
#if PRINT_COLOR
#define ESC(e) "\033[35m'\033[34;1m\\" e "\033[0;35m'\033[m"
#else
#define ESC(e) "'\\" e "'"
#endif
    const char *named[256] = {['\n']=ESC("n"), ['\t']=ESC("t"), ['\r']=ESC("r"),
        ['\033']=ESC("e"), ['\v']=ESC("v"), ['\a']=ESC("a"), ['\b']=ESC("b")};
    const char *name = named[(uint8_t)c];
    if (name != NULL)
        return fputs(name, f);
    else if (isprint(c))
#if PRINT_COLOR
        return fprintf(f, "\033[35m'%c'\033[m"), c);
#else
        return fprintf(f, "'%c'", c);
#endif
    else
        return fprintf(f, ESC("x%02X"), (uint8_t)c);
#undef ESC
}

public int _print_quoted(FILE *f, quoted_t quoted)
{
#if PRINT_COLOR
#define ESC(e) "\033[34;1m\\" e "\033[0;35m"
#else
#define ESC(e) "\\" e
#endif
    const char *named[256] = {['"']=ESC("\""), ['\n']=ESC("n"), ['\t']=ESC("t"), ['\r']=ESC("r"),
        ['\033']=ESC("e"), ['\v']=ESC("v"), ['\a']=ESC("a"), ['\b']=ESC("b")};
    int printed =
#if PRINT_COLOR
        fputs("\033[35m\"", f);
#else
        fputc('"', f);
#endif
    for (const char *p = quoted.str; *p; p++) {
        const char *name = named[(uint8_t)*p];
        if (name != NULL) {
            printed += fputs(name, f);
        } else if (isprint(*p) || (uint8_t)*p > 0x7F) {
            printed += fputc(*p, f);
        } else {
            printed += fprintf(f, ESC("x%02X"), (uint8_t)*p);
        }
    }
#if PRINT_COLOR
    printed += fputs("\"\033[m", f);
#else
    printed += fputc('"', f);
#endif
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
