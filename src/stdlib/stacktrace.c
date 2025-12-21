// This file defines some code to print stack traces.

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <dlfcn.h>
#include <err.h>
#include <execinfo.h>
#include <gc.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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
                fprint(out, cur_line == lineno ? "\033[31;1m>\033[m " : "  ", "\033[2m",
                       repeated_char(' ', num_width - w), cur_line, "\033(0\x78\033(B",
                       cur_line == lineno ? "\033[0;31;1m" : "\033[0m", line, "\033[m");
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

        fprint_inline(out, USE_COLOR ? "\033[1mIn \033[33m" : "In ", function_display, USE_COLOR ? "()\033[37m" : "()");
        if (install_dir[0] && strncmp(filename, install_dir, strlen(install_dir)) == 0)
            fprint_inline(out, USE_COLOR ? " in library \033[35m" : " in library ", filename, ":", lineno);
        else fprint(out, USE_COLOR ? " in \033[35m" : " in ", filename, ":", lineno);
        fprint(out, USE_COLOR ? "\033[m" : "");
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
    char **strings = backtrace_symbols(stack, size);
    bool main_func_onwards = false;
    for (int64_t i = size - 1; i > offset; i--) {
        Dl_info info;
        void *call_address = stack[i] - 1;
        if (dladdr(call_address, &info) && info.dli_fname) {
            const char *file = info.dli_fname;
            uintptr_t frame_offset = (uintptr_t)call_address - (uintptr_t)info.dli_fbase;
            FILE *fp = popen(String("addr2line -f -e '", file, "' ", (void *)frame_offset, " 2>/dev/null"), "r");
            if (fp) {
                const char *function = NULL, *filename = NULL;
                long line_num = 0;
                if (fparse(fp, &function, "\n", &filename, ":", &line_num) == NULL) {
                    if (starts_with(function, "main$")) main_func_onwards = true;
                    if (main_func_onwards) _print_stack_frame(out, cwd, install_dir, function, filename, line_num);
                } else {
                    if (main_func_onwards) _print_stack_frame(out, cwd, install_dir, NULL, NULL, line_num);
                }
                pclose(fp);
            }
        } else {
            if (main_func_onwards) _print_stack_frame(out, cwd, install_dir, NULL, NULL, 0);
        }
        if (main_func_onwards && i - 1 > offset) fputs("\n", out);
    }
    free(strings);
}
