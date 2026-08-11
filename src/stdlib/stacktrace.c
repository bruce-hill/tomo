// This file defines some code to print stack traces.

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <dlfcn.h>
#include <err.h>
#include <gc.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// glibc provides backtrace() via <execinfo.h>, but musl libc does not. When it's
// unavailable (e.g. in a static musl build), fall back to the compiler's stack
// unwinder, which provides the same "collect return addresses" functionality.
#if __has_include(<execinfo.h>)
#include <execinfo.h>
#else
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

static int backtrace(void **buffer, int size) {
    unwind_state_t state = {.frames = buffer, .count = 0, .max = size};
    _Unwind_Backtrace(unwind_callback, &state);
    return state.count;
}
#endif

#include "../config.h"
#include "print.h"
#include "simpleparse.h"
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

__attribute__((noinline)) public
void print_stacktrace(FILE *out, int offset) {
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) == NULL) errx(1, "Path too large!");
    size_t cwd_len = strlen(cwd);
    if (cwd_len + 2 > sizeof(cwd)) errx(1, "Path too large!");
    cwd[cwd_len++] = '/';
    cwd[cwd_len] = '\0';

    const char *install_dir = String(TOMO_PATH, "/lib/tomo@", TOMO_VERSION, "/");

    static void *stack[1024];
    int64_t size = (int64_t)backtrace(stack, sizeof(stack) / sizeof(stack[0]));
    bool main_func_onwards = false;
    for (int64_t i = size - 1; i > offset; i--) {
        Dl_info info;
        void *call_address = stack[i] - 1;
        const char *file = NULL;
        uintptr_t frame_offset = 0;
        if (dladdr(call_address, &info) && info.dli_fname && info.dli_fname[0]) {
            file = info.dli_fname;
            frame_offset = (uintptr_t)call_address - (uintptr_t)info.dli_fbase;
        }
#ifdef __linux__
        else {
            // In a fully static executable there is no dynamic segment, so
            // dladdr() can't resolve anything. Static binaries are linked
            // non-PIE, though, so the backtrace addresses are absolute and can
            // be looked up directly in the executable itself. The executable's
            // path must be resolved *here*: passing "/proc/self/exe" to
            // addr2line would make it inspect its own binary instead of ours.
            static char self_exe[PATH_MAX];
            if (self_exe[0] == '\0') {
                ssize_t n = readlink("/proc/self/exe", self_exe, sizeof(self_exe) - 1);
                if (n > 0) self_exe[n] = '\0';
            }
            if (self_exe[0] != '\0') {
                file = self_exe;
                frame_offset = (uintptr_t)call_address;
            }
        }
#endif
        if (file != NULL) {
            // -i expands inlined call chains: with optimization, several source-
            // level calls can collapse into one physical frame, and without -i
            // only the innermost of them would be visible. addr2line prints the
            // virtual frames innermost-first.
            FILE *fp = popen(String("addr2line -f -i -e '", file, "' ", (void *)frame_offset, " 2>/dev/null"), "r");
            if (fp) {
                // Read all of addr2line's output, then split it into (function,
                // file:line) line pairs, one pair per (possibly inlined) frame:
                char *output = NULL;
                size_t output_capacity = 0;
                ssize_t output_len = getdelim(&output, &output_capacity, '\0', fp);
                pclose(fp);

                enum { MAX_INLINE_FRAMES = 64 };
                const char *functions[MAX_INLINE_FRAMES], *filenames[MAX_INLINE_FRAMES];
                long line_nums[MAX_INLINE_FRAMES];
                int num_frames = 0;
                char *p = output;
                while (output_len > 0 && *p && num_frames < MAX_INLINE_FRAMES) {
                    char *newline = strchr(p, '\n');
                    if (newline == NULL) break;
                    *newline = '\0';
                    functions[num_frames] = p;
                    p = newline + 1;

                    char *location = p;
                    newline = strchr(p, '\n');
                    if (newline != NULL) {
                        *newline = '\0';
                        p = newline + 1;
                    } else {
                        p = location + strlen(location);
                    }
                    // Parse the "file:line" location pair:
                    const char *filename = NULL;
                    long line_num = 0;
                    if (strparse(location, &filename, ":", &line_num) != NULL) {
                        filename = location; // unparseable (e.g. "??:0"): keep as-is
                        line_num = 0;
                    }
                    filenames[num_frames] = filename;
                    line_nums[num_frames] = line_num;
                    num_frames += 1;
                }

                // Print outermost-first to match the overall root-to-crash order:
                for (int j = num_frames - 1; j >= 0; j--) {
                    // Start printing at the program's main function, skipping
                    // libc/startup frames above it. The entry symbol is named
                    // "main$<file id>" (or "parse_and_run$$main$<file id>" when
                    // top-level code is wrapped), so match "main$" anywhere.
                    if (strstr(functions[j], "main$") != NULL) main_func_onwards = true;
                    if (main_func_onwards) {
                        _print_stack_frame(out, cwd, install_dir, functions[j], filenames[j], line_nums[j]);
                        if (j > 0 || i - 1 > offset) fputs("\n", out);
                    }
                }
                if (output) free(output);
            }
        } else {
            if (main_func_onwards) {
                _print_stack_frame(out, cwd, install_dir, NULL, NULL, 0);
                if (i - 1 > offset) fputs("\n", out);
            }
        }
    }
}
