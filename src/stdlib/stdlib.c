// Built-in functions

#include <errno.h>
#include <execinfo.h>
#include <fcntl.h>
#include <gc.h>
#include <locale.h>
#include <math.h>
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
    const char *color_env = getenv("COLOR");
    USE_COLOR = color_env ? strcmp(color_env, "1") == 0 : isatty(STDOUT_FILENO);
    const char *no_color_env = getenv("NO_COLOR");
    if (no_color_env && no_color_env[0] != '\0') USE_COLOR = false;

    setlocale(LC_ALL, "");
    assert(getrandom(TOMO_HASH_KEY, sizeof(TOMO_HASH_KEY), 0) == sizeof(TOMO_HASH_KEY));

    struct sigaction sigact;
    sigact.sa_sigaction = signal_handler;
    sigemptyset(&sigact.sa_mask);
    sigact.sa_flags = 0;
    sigaction(SIGILL, &sigact, (struct sigaction *)NULL);
    atexit(tomo_cleanup);
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
        if (first_line_len > (size_t)(end - start)) first_line_len = (size_t)(end - start);
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

public
void say(Text_t text, bool newline) {
    Text$print(stdout, text);
    if (newline) fputc('\n', stdout);
    fflush(stdout);
}

public
_Noreturn void tomo_exit(Text_t text, int32_t status) {
    if (text.length > 0) print(text);
    exit(status);
}

public
OptionalText_t ask(Text_t prompt, bool bold, bool force_tty) {
    OptionalText_t ret = NONE_TEXT;
    FILE *out = stdout;
    FILE *in = stdin;
    bool opened_out = false, opened_in = false;

    char *line = NULL;
    size_t bufsize = 0;
    ssize_t length = 0;
    char *gc_input = NULL;

    if (force_tty && !isatty(STDOUT_FILENO)) {
        out = fopen("/dev/tty", "w");
        if (!out) goto cleanup;
        opened_out = true;
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
        opened_in = true;
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
    if (opened_out) fclose(out);
    if (opened_in) fclose(in);
    if (line != NULL) free(line);
    return ret;
}

public
void sleep_seconds(double seconds) {
    if (seconds < 0) fail("Cannot sleep for a negative amount of time: ", seconds);
    else if (isnan(seconds)) fail("Cannot sleep for a time that is NaN");
    struct timespec ts;
    ts.tv_sec = (time_t)seconds;
    ts.tv_nsec = (long)((seconds - (double)ts.tv_sec) * 1e9);
    while (nanosleep(&ts, NULL) != 0) {
        if (errno == EINTR) continue;
        fail("Failed to sleep for the requested time (", strerror(errno), ")");
    }
}

public
OptionalText_t getenv_text(Text_t name) {
    const char *val = getenv(Text$as_c_string(name));
    return val ? Text$from_str(val) : NONE_TEXT;
}

public
void setenv_text(Text_t name, OptionalText_t value) {
    if (value.tag == TEXT_NONE) unsetenv(Text$as_c_string(name));
    else setenv(Text$as_c_string(name), Text$as_c_string(value), 1);
}

typedef struct cleanup_s {
    Closure_t cleanup_fn;
    struct cleanup_s *next;
} cleanup_t;

static cleanup_t *cleanups = NULL;

public
void tomo_at_cleanup(Closure_t fn) { cleanups = new (cleanup_t, .cleanup_fn = fn, .next = cleanups); }

public
void tomo_cleanup(void) {
    while (cleanups) {
        // NOTE: we *must* remove the cleanup function from the stack before calling it,
        // otherwise it will cause an infinite loop if the cleanup function fails or exits.
        void (*run_cleanup)(void *) = cleanups->cleanup_fn.fn;
        void *userdata = cleanups->cleanup_fn.userdata;
        cleanups = cleanups->next;
        run_cleanup(userdata);
    }
}
