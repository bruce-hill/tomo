#include <gc.h>
#include <gc/cord.h>
#include <stdbool.h>
#include <stdint.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/param.h>

#include "../SipHash/halfsiphash.h"
#include "../files.h"
#include "../util.h"
#include "functions.h"
#include "array.h"
#include "table.h"
#include "pointer.h"
#include "string.h"
#include "types.h"

extern bool USE_COLOR;

public const char *SSS_HASH_VECTOR = "sss hash vector ----------------------------------------------";;

public void fail(CORD fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    if (USE_COLOR) fputs("\x1b[31;7m FAIL: \x1b[m ", stderr);
    else fputs("FAIL: ", stderr);
    CORD_vfprintf(stderr, fmt, args);
    va_end(args);
    raise(SIGABRT);
}

public uint32_t generic_hash(const void *obj, const TypeInfo *type)
{
    switch (type->tag) {
    case PointerInfo: case FunctionInfo: return Pointer__hash(obj, type);
    case ArrayInfo: return Array__hash(obj, type);
    case TableInfo: return Table_hash(obj, type);
    case CustomInfo:
        if (!type->CustomInfo.hash)
            goto hash_data;
        return type->CustomInfo.hash(obj, type);
    default: {
      hash_data:;
        uint32_t hash;
        halfsiphash((void*)obj, type->size, SSS_HASH_VECTOR, (uint8_t*)&hash, sizeof(hash));
        return hash;
    }
    }
}

public int32_t generic_compare(const void *x, const void *y, const TypeInfo *type)
{
    switch (type->tag) {
    case PointerInfo: case FunctionInfo: return Pointer__compare(x, y, type);
    case ArrayInfo: return Array__compare(x, y, type);
    case TableInfo: return Table_compare(x, y, type);
    case CustomInfo:
        if (!type->CustomInfo.compare)
            goto compare_data;
        return type->CustomInfo.compare(x, y, type);
    default:
      compare_data:
        return (int32_t)memcmp((void*)x, (void*)y, type->size);
    }
}

public bool generic_equal(const void *x, const void *y, const TypeInfo *type)
{
    switch (type->tag) {
    case PointerInfo: case FunctionInfo: return Pointer__equal(x, y, type);
    case ArrayInfo: return Array__equal(x, y, type);
    case TableInfo: return Table_equal(x, y, type);
    case CustomInfo:
        if (!type->CustomInfo.equal)
            goto use_generic_compare;
        return type->CustomInfo.equal(x, y, type);
    default:
      use_generic_compare:
        return (generic_compare(x, y, type) == 0);
    }
}

public CORD generic_as_str(const void *obj, bool colorize, const TypeInfo *type)
{
    switch (type->tag) {
    case PointerInfo: return Pointer__cord(obj, colorize, type);
    case FunctionInfo: return Func__as_str(obj, colorize, type);
    case ArrayInfo: return Array__as_str(obj, colorize, type);
    case TableInfo: return Table_as_str(obj, colorize, type);
    case TypeInfoInfo: return Type__as_str(obj, colorize, type);
    case CustomInfo:
        if (!type->CustomInfo.as_str)
            fail("No cord function provided for type!\n");
        return type->CustomInfo.as_str(obj, colorize, type);
    default: errx(1, "Invalid type tag: %d", type->tag);
    }
}


public CORD builtin_last_err()
{
    return CORD_from_char_star(strerror(errno));
}

static inline char *without_colors(const char *str)
{
    // Strip out color escape sequences: "\x1b[" ... "m"
    size_t fmt_len = strlen(str);
    char *buf = GC_malloc_atomic(fmt_len+1);
    char *dest = buf;
    for (const char *src = str; *src; ++src) {
        if (src[0] == '\x1b' && src[1] == '[') {
            src += 2;
            while (*src && *src != 'm')
                ++src;
        } else {
            *(dest++) = *src;
        }
    }
    *dest = '\0';
    return buf;
}

public void __doctest(void *expr, TypeInfo *type, CORD expected, const char *filename, int start, int end)
{
    static file_t *file = NULL;
    if (filename && (file == NULL || strcmp(file->filename, filename) != 0))
        file = load_file(filename);

    if (filename && file)
        CORD_fprintf(stderr, USE_COLOR ? "\x1b[33;1m>> \x1b[0m%.*s\x1b[m\n" : ">> %.*s\n", (end - start), file->text + start);

    if (expr) {
        CORD expr_str = generic_as_str(expr, USE_COLOR, type);
        CORD type_name = generic_as_str(NULL, false, type);

        CORD_fprintf(stderr, USE_COLOR ? "\x1b[2m=\x1b[0m %r \x1b[2m: %r\x1b[m\n" : "= %r : %r\n", expr_str, type_name);
        if (expected) {
            CORD expr_plain = USE_COLOR ? generic_as_str(expr, false, type) : expr_str;
            bool success = (CORD_cmp(expr_plain, expected) == 0);
            if (!success && CORD_chr(expected, 0, ':')) {
                success = (CORD_cmp(CORD_catn(3, expr_plain, " : ", type_name), expected) == 0);
            }

            if (!success) {
                if (filename && file)
                    fprint_span(stderr, file, file->text+start, file->text+end, "\x1b[31;1m", 2, USE_COLOR);
                fail(USE_COLOR ? "\x1b[31;1mDoctest failure:\nExpected: \x1b[32;7m%s\x1b[0m\n\x1b[31;1m But got: \x1b[31;7m%s\x1b[0m\n" : "Doctest failure:\nExpected: %s\n But got: %s\n",
                     expected, expr_str);
            }
        }
    }
}

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
