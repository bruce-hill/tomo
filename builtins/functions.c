// Built-in functions
#include <errno.h>
#include <execinfo.h>
#include <gc.h>
#include <gc/cord.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/param.h>
#include <uninorm.h>

#include "array.h"
#include "bool.h"
#include "files.h"
#include "functions.h"
#include "halfsiphash.h"
#include "pointer.h"
#include "string.h"
#include "table.h"
#include "text.h"
#include "types.h"
#include "util.h"

extern bool USE_COLOR;

public const char *SSS_HASH_VECTOR = "sss hash vector ----------------------------------------------";;

public void fail(CORD fmt, ...)
{
    if (USE_COLOR) fputs("\x1b[31;7m FAIL: \x1b[m ", stderr);
    else fputs("FAIL: ", stderr);
    va_list args;
    va_start(args, fmt);
    CORD_vfprintf(stderr, fmt, args);
    fputs("\n", stderr);
    va_end(args);

    // Print stack trace:
    fprintf(stderr, "\x1b[34m");
    fflush(stderr);
    void *array[1024];
    size_t size = backtrace(array, sizeof(array)/sizeof(array[0]));
    char **strings = strings = backtrace_symbols(array, size);
    for (size_t i = 1; i < size; i++) {
        char *filename = strings[i];
        const char *cmd = heap_strf("addr2line -e %.*s -fip | sed 's/\\$/./g;s/ at /() at /' >&2", strcspn(filename, "("), filename);
        FILE *fp = popen(cmd, "w");
        if (fp) {
            char *paren = strchrnul(strings[i], '(');
            fprintf(fp, "%.*s\n", strcspn(paren + 1, ")"), paren + 1);
        }
        pclose(fp);
    }
    fprintf(stderr, "\x1b[m");
    fflush(stderr);

    raise(SIGABRT);
}

public void fail_source(const char *filename, int64_t start, int64_t end, CORD fmt, ...)
{
    if (USE_COLOR) fputs("\n\x1b[31;7m FAIL: \x1b[m ", stderr);
    else fputs("\nFAIL: ", stderr);

    va_list args;
    va_start(args, fmt);
    CORD_vfprintf(stderr, fmt, args);
    va_end(args);

    file_t *file = filename ? load_file(filename) : NULL;
    if (filename && file) {
        fputs("\n", stderr);
        fprint_span(stderr, file, file->text+start, file->text+end, "\x1b[31;1m", 2, USE_COLOR);
    }

    raise(SIGABRT);
}

public uint32_t generic_hash(const void *obj, const TypeInfo *type)
{
    switch (type->tag) {
    case PointerInfo: case FunctionInfo: return Pointer$hash(obj, type);
    case TextInfo: return Text$hash(obj);
    case ArrayInfo: return Array$hash(obj, type);
    case TableInfo: return Table$hash(obj, type);
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
    case PointerInfo: case FunctionInfo: return Pointer$compare(x, y, type);
    case TextInfo: return Text$compare(x, y);
    case ArrayInfo: return Array$compare(x, y, type);
    case TableInfo: return Table$compare(x, y, type);
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
    case PointerInfo: case FunctionInfo: return Pointer$equal(x, y, type);
    case TextInfo: return Text$equal(x, y);
    case ArrayInfo: return Array$equal(x, y, type);
    case TableInfo: return Table$equal(x, y, type);
    case CustomInfo:
        if (!type->CustomInfo.equal)
            goto use_generic_compare;
        return type->CustomInfo.equal(x, y, type);
    default:
      use_generic_compare:
        return (generic_compare(x, y, type) == 0);
    }
}

public CORD generic_as_text(const void *obj, bool colorize, const TypeInfo *type)
{
    switch (type->tag) {
    case PointerInfo: return Pointer$as_text(obj, colorize, type);
    case FunctionInfo: return Func$as_text(obj, colorize, type);
    case TextInfo: return Text$as_text(obj, colorize, type);
    case ArrayInfo: return Array$as_text(obj, colorize, type);
    case TableInfo: return Table$as_text(obj, colorize, type);
    case TypeInfoInfo: return Type$as_text(obj, colorize, type);
    case CustomInfo:
        if (!type->CustomInfo.as_text)
            fail("No cord function provided for type!\n");
        return type->CustomInfo.as_text(obj, colorize, type);
    default: errx(1, "Invalid type tag: %d", type->tag);
    }
}


public CORD builtin_last_err()
{
    return CORD_from_char_star(strerror(errno));
}

public void $test(void *expr, const TypeInfo *type, CORD expected, const char *filename, int64_t start, int64_t end)
{
    static file_t *file = NULL;
    if (filename && (file == NULL || strcmp(file->filename, filename) != 0))
        file = load_file(filename);

    if (filename && file)
        CORD_fprintf(stderr, USE_COLOR ? "\x1b[33;1m>> \x1b[0m%.*s\x1b[m\n" : ">> %.*s\n", (end - start), file->text + start);

    if (expr) {
        CORD expr_cord = generic_as_text(expr, USE_COLOR, type);
        CORD type_name = generic_as_text(NULL, false, type);

        uint8_t buf[512] = {0};
        size_t buf_len = sizeof(buf)-1;
        const char *expr_str = CORD_to_const_char_star(expr_cord);
        uint8_t *normalized_str = u8_normalize(UNINORM_NFD, (uint8_t*)expr_str, strlen(expr_str), buf, &buf_len);
        normalized_str[buf_len] = 0;
        if (!normalized_str) errx(1, "Couldn't normalize unicode string!");
        CORD expr_normalized = CORD_from_char_star((char*)normalized_str);
        if (normalized_str != buf)
            free(normalized_str);

        CORD_fprintf(stderr, USE_COLOR ? "\x1b[2m=\x1b[0m %r \x1b[2m: %r\x1b[m\n" : "= %r : %r\n", expr_normalized, type_name);
        if (expected) {
            CORD expr_plain = USE_COLOR ? generic_as_text(expr, false, type) : expr_normalized;
            bool success = Text$equal(&expr_plain, &expected);
            if (!success && CORD_chr(expected, 0, ':')) {
                CORD with_type = CORD_catn(3, expr_plain, " : ", type_name);
                success = Text$equal(&with_type, &expected);
            }

            if (!success) {
                fail_source(filename, start, end, 
                            USE_COLOR ? "\x1b[31;1mDoctest failure:\nExpected: \x1b[32;1m%s\x1b[0m\n\x1b[31;1m But got:\x1b[m %s\n"
                            : "Doctest failure:\nExpected: %s\n But got: %s\n",
                            CORD_to_const_char_star(expected), CORD_to_const_char_star(expr_normalized));
            }
        }
    }
}

public void say(CORD text)
{
    uint8_t buf[512] = {0};
    size_t buf_len = sizeof(buf)-1;
    const char *str = CORD_to_const_char_star(text);
    uint8_t *normalized = u8_normalize(UNINORM_NFD, (uint8_t*)str, strlen(str), buf, &buf_len);
    if (normalized) {
        puts((char*)normalized);
        if (normalized != buf)
            free(normalized);
    }
}

public bool pop_flag(char **argv, int *i, const char *flag, CORD *result)
{
    if (argv[*i][0] != '-' || argv[*i][1] != '-') {
        return false;
    } else if (streq(argv[*i] + 2, flag)) {
        *result = CORD_EMPTY;
        argv[*i] = NULL;
        *i += 1;
        return true;
    } else if (strncmp(argv[*i] + 2, "no-", 3) == 0 && streq(argv[*i] + 5, flag)) {
        *result = "no";
        argv[*i] = NULL;
        *i += 1;
        return true;
    } else if (strncmp(argv[*i] + 2, flag, strlen(flag)) == 0 && argv[*i][2 + strlen(flag)] == '=') {
        *result = CORD_from_char_star(argv[*i] + 2 + strlen(flag) + 1);
        argv[*i] = NULL;
        *i += 1;
        return true;
    } else {
        return false;
    }
}

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
