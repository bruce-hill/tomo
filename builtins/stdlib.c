// Built-in functions

#include <errno.h>
#include <execinfo.h>
#include <gc.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/param.h>
#include <sys/random.h>
#include <time.h>

#include "files.h"
#include "integers.h"
#include "metamethods.h"
#include "pattern.h"
#include "siphash.h"
#include "table.h"
#include "text.h"
#include "util.h"

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

void print_stack_trace(FILE *out, int start, int stop)
{
    // Print stack trace:
    fprintf(out, "\x1b[34m");
    fflush(out);
    void *array[1024];
    int64_t size = (int64_t)backtrace(array, sizeof(array)/sizeof(array[0]));
    char **strings = strings = backtrace_symbols(array, size);
    for (int64_t i = start; i < size - stop; i++) {
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
    fflush(out);
}

__attribute__((format(printf, 1, 2)))
public _Noreturn void fail(const char *fmt, ...)
{
    fflush(stdout);
    if (USE_COLOR) fputs("\x1b[31;7m ==================== ERROR ==================== \n\n\x1b[0;1m", stderr);
    else fputs("==================== ERROR ====================\n\n", stderr);
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    if (USE_COLOR) fputs("\x1b[m", stderr);
    fputs("\n\n", stderr);
    va_end(args);
    print_stack_trace(stderr, 2, 4);
    fflush(stderr);
    raise(SIGABRT);
    _exit(1);
}

__attribute__((format(printf, 4, 5)))
public _Noreturn void fail_source(const char *filename, int64_t start, int64_t end, const char *fmt, ...)
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

    print_stack_trace(stderr, 2, 4);
    fflush(stderr);
    raise(SIGABRT);
    _exit(1);
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

        int64_t first_line_len = (int64_t)strcspn(file->text + start, "\r\n");
        fprintf(stderr, USE_COLOR ? "\x1b[33;1m>> \x1b[m%.*s\n" : ">> %.*s\n", first_line_len, file->text + start);

        // For multi-line expressions, dedent each and print it on a new line with ".. " in front:
        if (end > start + first_line_len) {
            int64_t line_num = get_line_number(file, file->text + start);
            const char *line_start = get_line(file, line_num);
            int64_t indent_len = (int64_t)strspn(line_start, " \t");
            for (const char *line = file->text + start + first_line_len; line < file->text + end; line += strcspn(line, "\r\n")) {
                line += strspn(line, "\r\n");
                if ((int64_t)strspn(line, " \t") >= indent_len)
                    line += indent_len;
                fprintf(stderr, USE_COLOR ? "\x1b[33m.. \x1b[m%.*s\n" : ".. %.*s\n", strcspn(line, "\r\n"), line);
            }
        }
    }
    ++TEST_DEPTH;
}

public void end_test(const void *expr, const TypeInfo *type, const char *expected, const char *filename, int64_t start, int64_t end)
{
    (void)filename;
    (void)start;
    (void)end;
    --TEST_DEPTH;
    if (!expr || !type) return;

    Text_t expr_text = generic_as_text(expr, USE_COLOR, type);
    Text_t type_name = generic_as_text(NULL, false, type);

    for (int i = 0; i < 3*TEST_DEPTH; i++) fputc(' ', stderr);
    fprintf(stderr, USE_COLOR ? "\x1b[2m=\x1b[0m %k \x1b[2m: %k\x1b[m\n" : "= %k : %k\n", &expr_text, &type_name);
    if (expected && expected[0]) {
        Text_t expected_text = Text$from_str(expected);
        Text_t expr_plain = USE_COLOR ? generic_as_text(expr, false, type) : expr_text;
        bool success = Text$equal(&expr_plain, &expected_text);
        if (!success) {
            Int_t colon = Text$find(expected_text, Text(":"), I_small(1), NULL);
            if (colon.small != I_small(0).small) {
                Text_t with_type = Text$concat(expr_plain, Text(" : "), type_name);
                success = Text$equal(&with_type, &expected_text);
            }
        }

        if (!success) {
            fprintf(stderr, 
                    USE_COLOR
                    ? "\n\x1b[31;7m ==================== TEST FAILED ==================== \x1b[0;1m\n\nExpected: \x1b[1;32m%s\x1b[0m\n\x1b[1m But got:\x1b[m %k\n\n"
                    : "\n==================== TEST FAILED ====================\nExpected: %s\n\n But got: %k\n\n",
                    expected, &expr_text);

            print_stack_trace(stderr, 2, 4);
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
    fflush(stdout);
}

public _Noreturn void tomo_exit(Text_t text, int32_t status)
{
    if (text.length > 0)
        say(text, true);
    _exit(status);
}

public Text_t ask(Text_t prompt, bool bold, bool force_tty)
{
    Text_t ret = Text("");
    FILE *out = stdout;
    FILE *in = stdin;

    char *line = NULL;
    size_t bufsize = 0;
    ssize_t length = 0;
    char *gc_input = NULL;

    if (force_tty && !isatty(STDOUT_FILENO)) {
        out = fopen("/dev/tty", "w");
        if (!out) goto cleanup;
    }

    if (bold) fputs("\x1b[1m", out);
    Text$print(out, prompt);
    if (bold) fputs("\x1b[m", out);
    fflush(out);

    if (force_tty && !isatty(STDIN_FILENO)) {
        in = fopen("/dev/tty", "r");
        if (!in) {
            fputs("\n", out); // finish the line, since the user can't
            goto cleanup;
        }
    }

    length = getline(&line, &bufsize, in);
    if (length == -1) {
        fputs("\n", out); // finish the line, since we didn't get any input
        goto cleanup;
    }

    if (length > 0 && line[length-1] == '\n') {
        line[length-1] = '\0';
        --length;
    }

    gc_input = GC_MALLOC_ATOMIC((size_t)(length + 1));
    memcpy(gc_input, line, (size_t)(length + 1));

    ret = Text$from_strn(gc_input, (size_t)(length));

  cleanup:
    if (out && out != stdout) fclose(out);
    if (in && in != stdin) fclose(in);
    return ret;
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
        *result = Text("no");
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

public void sleep_num(double seconds)
{
    struct timespec ts;
    ts.tv_sec = (time_t)seconds;
    ts.tv_nsec = (long)((seconds - (double)ts.tv_sec) * 1e9);
    nanosleep(&ts, NULL);
}

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
