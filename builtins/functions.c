// Built-in functions
#include <errno.h>
#include <execinfo.h>
#include <gc.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/param.h>
#include <sys/random.h>
#include <uninorm.h>
#include <unistd.h>

#include "array.h"
#include "bool.h"
#include "channel.h"
#include "files.h"
#include "functions.h"
#include "integers.h"
#include "pointer.h"
#include "string.h"
#include "table.h"
#include "text.h"
#include "types.h"
#include "util.h"

#include "siphash.c"

public uint8_t TOMO_HASH_KEY[16] = {0};

public void tomo_init(void)
{
   GC_INIT();
   USE_COLOR = getenv("COLOR") ? strcmp(getenv("COLOR"), "1") == 0 : isatty(STDOUT_FILENO);
   getrandom(TOMO_HASH_KEY, sizeof(TOMO_HASH_KEY), 0);
   unsigned int seed;
   getrandom(&seed, sizeof(seed), 0);
   srand(seed);
   srand48(seed);
   Int$init_random(seed);

    if (register_printf_specifier('k', printf_text, printf_text_size))
        errx(1, "Couldn't set printf specifier");
}

static void print_stack_trace(FILE *out)
{
    // Print stack trace:
    fprintf(out, "\x1b[34m");
    fflush(out);
    void *array[1024];
    int64_t size = (int64_t)backtrace(array, sizeof(array)/sizeof(array[0]));
    char **strings = strings = backtrace_symbols(array, size);
    for (int64_t i = 2; i < size - 4; i++) {
        char *filename = strings[i];
        const char *cmd = heap_strf("addr2line -e %.*s -fisp | sed 's/\\$/./g;s/ at /() at /' >&2", strcspn(filename, "("), filename);
        FILE *fp = popen(cmd, "w");
        if (fp) {
            char *paren = strchrnul(strings[i], '(');
            fprintf(fp, "%.*s\n", strcspn(paren + 1, ")"), paren + 1);
        }
        pclose(fp);
    }
    fprintf(out, "\x1b[m");
}

public void fail(const char *fmt, ...)
{
    if (USE_COLOR) fputs("\x1b[31;7m ==================== ERROR ==================== \n\n\x1b[0;1m", stderr);
    else fputs("==================== ERROR ====================\n\n", stderr);
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    if (USE_COLOR) fputs("\x1b[m", stderr);
    fputs("\n\n", stderr);
    va_end(args);
    print_stack_trace(stderr);
    fflush(stderr);
    raise(SIGABRT);
}

public void fail_source(const char *filename, int64_t start, int64_t end, const char *fmt, ...)
{
    if (USE_COLOR) fputs("\n\x1b[31;7m ==================== ERROR ==================== \n\n\x1b[0;1m", stderr);
    else fputs("\n==================== ERROR ====================\n\n", stderr);

    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);

    file_t *file = filename ? load_file(filename) : NULL;
    if (filename && file) {
        fputs("\n", stderr);
        highlight_error(file, file->text+start, file->text+end, "\x1b[31;1m", 2, USE_COLOR);
        fputs("\n", stderr);
    }
    if (USE_COLOR) fputs("\x1b[m", stderr);

    print_stack_trace(stderr);
    fflush(stderr);
    raise(SIGABRT);
}

public uint64_t generic_hash(const void *obj, const TypeInfo *type)
{
    switch (type->tag) {
    case TextInfo: return Text$hash((void*)obj);
    case ArrayInfo: return Array$hash(obj, type);
    case ChannelInfo: return Channel$hash((const channel_t**)obj, type);
    case TableInfo: return Table$hash(obj, type);
    case EmptyStruct: return 0;
    case CustomInfo:
        if (!type->CustomInfo.hash)
            goto hash_data;
        return type->CustomInfo.hash(obj, type);
    default: {
      hash_data:;
        return siphash24((void*)obj, type->size, (uint64_t*)TOMO_HASH_KEY);
    }
    }
}

public int32_t generic_compare(const void *x, const void *y, const TypeInfo *type)
{
    switch (type->tag) {
    case PointerInfo: case FunctionInfo: return Pointer$compare(x, y, type);
    case TextInfo: return Text$compare(x, y);
    case ArrayInfo: return Array$compare(x, y, type);
    case ChannelInfo: return Channel$compare((const channel_t**)x, (const channel_t**)y, type);
    case TableInfo: return Table$compare(x, y, type);
    case EmptyStruct: return 0;
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
    case ChannelInfo: return Channel$equal((const channel_t**)x, (const channel_t**)y, type);
    case TableInfo: return Table$equal(x, y, type);
    case EmptyStruct: return true;
    case CustomInfo:
        if (!type->CustomInfo.equal)
            goto use_generic_compare;
        return type->CustomInfo.equal(x, y, type);
    default:
      use_generic_compare:
        return (generic_compare(x, y, type) == 0);
    }
}

public Text_t generic_as_text(const void *obj, bool colorize, const TypeInfo *type)
{
    switch (type->tag) {
    case PointerInfo: return Pointer$as_text(obj, colorize, type);
    case FunctionInfo: return Func$as_text(obj, colorize, type);
    case TextInfo: return Text$as_text(obj, colorize, type);
    case ArrayInfo: return Array$as_text(obj, colorize, type);
    case ChannelInfo: return Channel$as_text((const channel_t**)obj, colorize, type);
    case TableInfo: return Table$as_text(obj, colorize, type);
    case TypeInfoInfo: return Type$as_text(obj, colorize, type);
    case EmptyStruct: return colorize ?
                      Text$concat(Text$from_str("\x1b[0;1m"), Text$from_str(type->EmptyStruct.name), Text$from_str("\x1b[m()"))
                          : Text$concat(Text$from_str(type->EmptyStruct.name), Text$from_str("()"));
    case CustomInfo:
        if (!type->CustomInfo.as_text)
            fail("No text function provided for type!\n");
        return type->CustomInfo.as_text(obj, colorize, type);
    default: errx(1, "Invalid type tag: %d", type->tag);
    }
}


public Text_t builtin_last_err()
{
    return Text$from_str(strerror(errno));
}

static int TEST_DEPTH = 0;
static file_t *file = NULL;

public void start_test(const char *filename, int64_t start, int64_t end)
{
    if (filename && (file == NULL || strcmp(file->filename, filename) != 0))
        file = load_file(filename);

    if (filename && file) {
        for (int i = 0; i < 3*TEST_DEPTH; i++) fputc(' ', stderr);
        fprintf(stderr, USE_COLOR ? "\x1b[33;1m>> \x1b[0m%.*s\x1b[m\n" : ">> %.*s\n", (end - start), file->text + start);
    }
    ++TEST_DEPTH;
}

public void end_test(void *expr, const TypeInfo *type, const char *expected, const char *filename, int64_t start, int64_t end)
{
    (void)filename;
    (void)start;
    (void)end;
    --TEST_DEPTH;
    if (!expr) return;

    Text_t expr_text = generic_as_text(expr, USE_COLOR, type);
    Text_t type_name = generic_as_text(NULL, false, type);

    for (int i = 0; i < 3*TEST_DEPTH; i++) fputc(' ', stderr);
    fprintf(stderr, USE_COLOR ? "\x1b[2m=\x1b[0m %k \x1b[2m: %k\x1b[m\n" : "= %k : %k\n", &expr_text, &type_name);
    if (expected && expected[0]) {
        Text_t expected_text = Text$from_str(expected);
        Text_t expr_plain = USE_COLOR ? generic_as_text(expr, false, type) : expr_text;
        bool success = Text$equal(&expr_plain, &expected_text);
        if (!success) {
            Int_t colon = Text$find(expected_text, Text$from_str(":"), I_small(1), NULL);
            if (colon.small != I_small(0).small) {
                Text_t with_type = Text$concat(expr_plain, Text$from_str(" : "), type_name);
                success = Text$equal(&with_type, &expected_text);
            }
        }

        if (!success) {
            fprintf(stderr, 
                    USE_COLOR
                    ? "\n\x1b[31;7m ==================== TEST FAILED ==================== \x1b[0;1m\n\nExpected: \x1b[1;32m%s\x1b[0m\n\x1b[1m But got:\x1b[m %k\n\n"
                    : "\n==================== TEST FAILED ====================\nExpected: %s\n\n But got: %k\n\n",
                    expected, &expr_text);

            print_stack_trace(stderr);
            fflush(stderr);
            raise(SIGABRT);
        }
    }
}

public void say(Text_t text, bool newline)
{
    Text$print(stdout, text);
    if (newline)
        fputc('\n', stdout);
}

public bool pop_flag(char **argv, int *i, const char *flag, Text_t *result)
{
    if (argv[*i][0] != '-' || argv[*i][1] != '-') {
        return false;
    } else if (streq(argv[*i] + 2, flag)) {
        *result = (Text_t){.length=0};
        argv[*i] = NULL;
        *i += 1;
        return true;
    } else if (strncmp(argv[*i] + 2, "no-", 3) == 0 && streq(argv[*i] + 5, flag)) {
        *result = Text$from_str("no");
        argv[*i] = NULL;
        *i += 1;
        return true;
    } else if (strncmp(argv[*i] + 2, flag, strlen(flag)) == 0 && argv[*i][2 + strlen(flag)] == '=') {
        *result = Text$from_str(argv[*i] + 2 + strlen(flag) + 1);
        argv[*i] = NULL;
        *i += 1;
        return true;
    } else {
        return false;
    }
}

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
