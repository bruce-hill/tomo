// This file defines some code to print stack traces.

#include <backtrace.h>
#include <dlfcn.h>
#include <err.h>
#include <gc.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// The raw stack addresses are collected with the compiler's stack unwinder,
// which zig provides on every supported platform (including fully static musl
// binaries, where libc-based alternatives like execinfo.h don't exist).
#include <unwind.h>
typedef struct {
    void **frames;
    int count, max;
} unwind_state_t;

static _Unwind_Reason_Code unwind_callback(struct _Unwind_Context *ctx, void *arg) {
    unwind_state_t *state = arg;
    if (state->count >= state->max) return _URC_END_OF_STACK;
    uintptr_t ip = _Unwind_GetIP(ctx);
    if (ip) state->frames[state->count++] = (void *)ip;
    return _URC_NO_REASON;
}

static int collect_backtrace(void **buffer, int size) {
    unwind_state_t state = {.frames = buffer, .count = 0, .max = size};
    _Unwind_Backtrace(unwind_callback, &state);
    return state.count;
}

#include "../config.h"
#include "print.h"
#include "util.h"

extern bool USE_COLOR;

static void fprint_context(FILE *out, const char *filename, int lineno, int context_before, int context_after) {
    FILE *f = fopen(filename, "r");
    if (!f) return;
    char *line = NULL;
    size_t size = 0;
    ssize_t nread;
    int64_t cur_line = 1;

    int num_width = 1;
    for (int n = lineno + context_after; n >= 10; n /= 10)
        num_width += 1;

    while ((nread = getline(&line, &size, f)) != -1) {
        if (line[nread - 1] == '\n') line[nread - 1] = '\0';

        if (cur_line >= lineno - context_before) {
            int w = 1;
            for (int n = cur_line; n >= 10; n /= 10)
                w += 1;

            if (USE_COLOR) {
                fprint(out, cur_line == lineno ? "\033[91;1m>\033[m " : "  ", "\033[2m",
                       repeated_char(' ', num_width - w), cur_line, "\033(0\x78\033(B",
                       cur_line == lineno ? "\033[0;91;1m" : "\033[0m", line, "\033[m");
            } else {
                fprint(out, cur_line == lineno ? "> " : "  ", repeated_char(' ', num_width - w), cur_line, "| ", line);
            }
        }

        cur_line += 1;
        if (cur_line > lineno + context_after) break;
    }
    if (line) free(line);
    fclose(f);
}

static void _print_stack_frame(FILE *out, const char *cwd, const char *install_dir, const char *function,
                               const char *filename, int lineno) {
    if (function == NULL) {
        fprint(out, USE_COLOR ? "\033[2m...unknown function...\033[m" : "...unknown function...");
        return;
    }

    function = String(string_slice(function, strcspn(function, "+")));
    if (function[0] == '\0') function = "???";

    char *function_display = GC_MALLOC_ATOMIC(strlen(function));
    memcpy(function_display, function, strlen(function) + 1);
    char *last_dollar = strrchr(function_display, '$');
    if (last_dollar) *last_dollar = '\0';
    for (char *p = function_display; *p; p++) {
        if (*p == '$') *p = '.';
    }

    if (filename) {
        if (strncmp(filename, cwd, strlen(cwd)) == 0) filename += strlen(cwd);

        fprint(out, USE_COLOR ? "\033[97;1mIn \033[93m" : "In ", function_display, USE_COLOR ? "()\033[97m" : "()");
        if (install_dir[0] && strncmp(filename, install_dir, strlen(install_dir)) == 0) {
            fprint_inline(out, USE_COLOR ? " in package \033[95m" : " in package ", filename, ":", lineno);
        } else {
            fprint(out, USE_COLOR ? "\033[93;4m" : "", filename, USE_COLOR ? "\033[m" : "");
        }
        fprint_context(out, filename, lineno, 3, 1);
    } else {
        fprint(out, "LINE: ", function);
    }
}

enum { MAX_INLINE_FRAMES = 64 };
typedef struct {
    const char *functions[MAX_INLINE_FRAMES], *filenames[MAX_INLINE_FRAMES];
    long line_nums[MAX_INLINE_FRAMES];
    int count;
} frame_list_t;

static int bt_frame_callback(void *data, uintptr_t pc, const char *filename, int lineno, const char *function) {
    (void)pc;
    frame_list_t *frames = data;
    if (frames->count >= MAX_INLINE_FRAMES) return 1;
    // Copy the strings: libbacktrace only guarantees them until the callback returns.
    frames->functions[frames->count] = function ? String(function) : NULL;
    frames->filenames[frames->count] = filename ? String(filename) : NULL;
    frames->line_nums[frames->count] = lineno;
    frames->count += 1;
    return 0;
}

static void bt_error_callback(void *data, const char *msg, int errnum) {
    // Quietly leave the frame unresolved:
    (void)data, (void)msg, (void)errnum;
}

__attribute__((noinline)) public
void print_stacktrace(FILE *out, int offset) {
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) == NULL) errx(1, "Path too large!");
    size_t cwd_len = strlen(cwd);
    if (cwd_len + 2 > sizeof(cwd)) errx(1, "Path too large!");
    cwd[cwd_len++] = '/';
    cwd[cwd_len] = '\0';

    const char *install_dir = String(TOMO_PATH, "/lib/tomo@", TOMO_VERSION, "/");

    // Symbolization is done in-process with the vendored libbacktrace (reading
    // the executable's own debug info), so no external tools are needed and it
    // works in fully static binaries. The state is created once and reused.
    static struct backtrace_state *bt_state = NULL;
    if (bt_state == NULL) {
        const char *exe_path = NULL;
#if !defined(__linux__)
        // A NULL filename makes libbacktrace find the executable itself, but it
        // has no way to do that on OpenBSD (which offers no API to query the
        // running executable's path). These platforms link dynamically, though,
        // so dladdr() on one of our own functions yields the executable's path;
        // resolve it in case it's relative to the (current) working directory.
        Dl_info info;
        static char resolved_path[PATH_MAX];
        if (dladdr((void *)(uintptr_t)print_stacktrace, &info) && info.dli_fname && info.dli_fname[0]
            && realpath(info.dli_fname, resolved_path) != NULL)
            exe_path = resolved_path;
#endif
        bt_state = backtrace_create_state(exe_path, 0, bt_error_callback, NULL);
    }

    static void *stack[1024];
    int64_t size = (int64_t)collect_backtrace(stack, sizeof(stack) / sizeof(stack[0]));
    bool main_func_onwards = false;
    for (int64_t i = size - 1; i > offset; i--) {
        uintptr_t call_address = (uintptr_t)stack[i] - 1;

        // backtrace_pcinfo() invokes the callback once per (possibly inlined)
        // frame at this address, innermost-first -- with optimization, several
        // source-level calls can collapse into one physical frame, and this
        // recovers all of them.
        frame_list_t frames = {.count = 0};
        if (bt_state != NULL) backtrace_pcinfo(bt_state, call_address, bt_frame_callback, bt_error_callback, &frames);

        if (frames.count == 0) {
            if (main_func_onwards) {
                _print_stack_frame(out, cwd, install_dir, NULL, NULL, 0);
                if (i - 1 > offset) fputs("\n", out);
            }
            continue;
        }

        // Print outermost-first to match the overall root-to-crash order:
        for (int j = frames.count - 1; j >= 0; j--) {
            // Start printing at the program's main function, skipping
            // libc/startup frames above it. The entry function is named
            // "main$<file id>" (or "parse_and_run$$main$<file id>" when
            // top-level code is wrapped), so match "main$" anywhere.
            if (frames.functions[j] && strstr(frames.functions[j], "main$") != NULL) main_func_onwards = true;
            if (main_func_onwards) {
                _print_stack_frame(out, cwd, install_dir, frames.functions[j], frames.filenames[j],
                                   frames.line_nums[j]);
                if (j > 0 || i - 1 > offset) fputs("\n", out);
            }
        }
    }
}
