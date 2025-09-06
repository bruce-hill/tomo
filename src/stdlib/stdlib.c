// Built-in functions

#include <errno.h>
#include <execinfo.h>
#include <fcntl.h>
#include <gc.h>
#include <locale.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/param.h>
#include <time.h>

#include "../config.h"
#include "files.h"
#include "metamethods.h"
#include "optionals.h"
#include "print.h"
#include "siphash.h"
#include "stacktrace.h"
#include "stdlib.h"
#include "text.h"
#include "util.h"

#if defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || defined(__APPLE__)
static ssize_t getrandom(void *buf, size_t buflen, unsigned int flags) {
    (void)flags;
    arc4random_buf(buf, buflen);
    return buflen;
}
#elif defined(__linux__)
// Use getrandom()
#include <sys/random.h>
#else
#error "Unsupported platform for secure random number generation"
#endif

public
bool USE_COLOR;
public
Text_t TOMO_VERSION_TEXT = Text(TOMO_VERSION);

public
const char *TOMO_PATH = TOMO_INSTALL;

static _Noreturn void signal_handler(int sig, siginfo_t *info, void *userdata) {
    (void)info, (void)userdata;
    assert(sig == SIGILL);
    fflush(stdout);
    if (USE_COLOR) fputs("\x1b[31;7m ===== ILLEGAL INSTRUCTION ===== \n\n\x1b[m", stderr);
    else fputs("===== ILLEGAL INSTRUCTION =====\n\n", stderr);
    print_stacktrace(stderr, 3);
    fflush(stderr);
    raise(SIGABRT);
    _exit(1);
}

public
void tomo_init(void) {
    GC_INIT();
    USE_COLOR = getenv("COLOR") ? strcmp(getenv("COLOR"), "1") == 0 : isatty(STDOUT_FILENO);
    if (getenv("NO_COLOR") && getenv("NO_COLOR")[0] != '\0') USE_COLOR = false;

    setlocale(LC_ALL, "");
    assert(getrandom(TOMO_HASH_KEY, sizeof(TOMO_HASH_KEY), 0) == sizeof(TOMO_HASH_KEY));

    struct sigaction sigact;
    sigact.sa_sigaction = signal_handler;
    sigemptyset(&sigact.sa_mask);
    sigact.sa_flags = 0;
    sigaction(SIGILL, &sigact, (struct sigaction *)NULL);
}

public
_Noreturn void fail_text(Text_t message) { fail(message); }

public
Text_t builtin_last_err() { return Text$from_str(strerror(errno)); }

static int _inspect_depth = 0;
static file_t *file = NULL;

__attribute__((nonnull)) public
void start_inspect(const char *filename, int64_t start, int64_t end) {
    if (file == NULL || strcmp(file->filename, filename) != 0) file = load_file(filename);

    if (file) {
        size_t first_line_len = strcspn(file->text + start, "\r\n");
        const char *slash = strrchr(filename, '/');
        const char *file_base = slash ? slash + 1 : filename;

        int64_t line_num = get_line_number(file, file->text + start);
        if (USE_COLOR) {
            print(repeated_char(' ', 3 * _inspect_depth), "\x1b[33;1m>> \x1b[m",
                  string_slice(file->text + start, first_line_len), "   ",
                  repeated_char(' ', MAX(0, 35 - (int64_t)first_line_len - 3 * _inspect_depth)), "\x1b[32;2m[",
                  file_base, ":", line_num, "]\x1b[m");
        } else {
            print(repeated_char(' ', 3 * _inspect_depth), ">> ", string_slice(file->text + start, first_line_len),
                  "   ", repeated_char(' ', MAX(0, 35 - (int64_t)first_line_len - 3 * _inspect_depth)), "[", file_base,
                  ":", line_num, "]");
        }

        // For multi-line expressions, dedent each and print it on a new line with ".. " in front:
        if (end > start + (int64_t)first_line_len) {
            const char *line_start = get_line(file, line_num);
            int64_t indent_len = (int64_t)strspn(line_start, " \t");
            for (const char *line = file->text + start + first_line_len; line < file->text + end;
                 line += strcspn(line, "\r\n")) {
                line += strspn(line, "\r\n");
                if ((int64_t)strspn(line, " \t") >= indent_len) line += indent_len;
                print(repeated_char(' ', 3 * _inspect_depth), USE_COLOR ? "\x1b[33m..\033[m " : ".. ",
                      string_slice(line, strcspn(line, "\r\n")));
            }
        }
    }
    _inspect_depth += 1;
}

public
void end_inspect(const void *expr, const TypeInfo_t *type) {
    _inspect_depth -= 1;

    if (type && type->metamethods.as_text) {
        Text_t expr_text = generic_as_text(expr, USE_COLOR, type);
        Text_t type_name = generic_as_text(NULL, false, type);
        for (int i = 0; i < 3 * _inspect_depth; i++)
            fputc(' ', stdout);
        fprint(stdout, USE_COLOR ? "\x1b[33;1m=\x1b[0m " : "= ", expr_text, USE_COLOR ? " \x1b[2m: \x1b[36m" : " : ",
               type_name, USE_COLOR ? "\033[m" : "");
    }
}

__attribute__((nonnull)) public
void test_value(const char *filename, int64_t start, int64_t end, const void *expr, const void *expected,
                const TypeInfo_t *type) {
    if (generic_equal(expr, expected, type)) return;

    print_stacktrace(stderr, 2);
    fprint(stderr, "");
    fflush(stderr);

    start_inspect(filename, start, end);
    end_inspect(expr, type);
    fflush(stdout);

    Text_t expr_text = generic_as_text(expr, USE_COLOR, type);
    Text_t expected_text = generic_as_text(expected, USE_COLOR, type);
    if (USE_COLOR) {
        fprint(stderr,
               "\n\x1b[31;7m ==================== TEST FAILED ==================== \x1b[0;1m\n\n"
               "You expected: \x1b[m",
               expected_text,
               "\x1b[0m\n"
               "\x1b[1m   But I got:\x1b[m ",
               expr_text, "\n");
    } else {
        fprint(stderr,
               "\n==================== TEST FAILED ====================\n\n"
               "You expected: ",
               expected_text,
               "\n"
               "   But I got: ",
               expr_text, "\n");
    }

    fflush(stderr);
    raise(SIGABRT);
}

public
void say(Text_t text, bool newline) {
    Text$print(stdout, text);
    if (newline) fputc('\n', stdout);
    fflush(stdout);
}

public
_Noreturn void tomo_exit(Text_t text, int32_t status) {
    if (text.length > 0) print(text);
    _exit(status);
}

public
OptionalText_t ask(Text_t prompt, bool bold, bool force_tty) {
    OptionalText_t ret = NONE_TEXT;
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

    if (length > 0 && line[length - 1] == '\n') {
        line[length - 1] = '\0';
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

public
void sleep_num(double seconds) {
    struct timespec ts;
    ts.tv_sec = (time_t)seconds;
    ts.tv_nsec = (long)((seconds - (double)ts.tv_sec) * 1e9);
    nanosleep(&ts, NULL);
}

public
OptionalText_t getenv_text(Text_t name) {
    const char *val = getenv(Text$as_c_string(name));
    return val ? Text$from_str(val) : NONE_TEXT;
}

public
void setenv_text(Text_t name, Text_t value) { setenv(Text$as_c_string(name), Text$as_c_string(value), 1); }
