#include <backtrace.h>
#include <err.h>
#include <gc.h>
#include <signal.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "util.h"

extern bool USE_COLOR;

static struct backtrace_state *bt_state = NULL;

static void fprint_context(FILE *out, const char *filename, int lineno, int context_before, int context_after)
{
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
        if (line[strlen(line)-1] == '\n')
            line[strlen(line)-1] = '\0';

        if (cur_line >= lineno - context_before)
            fprintf(out, USE_COLOR ? "%s\033[2m%*d\033(0\x78\x1b(B%s%s\033[m\n" : "%s%*d| %s%s\n",
                    cur_line == lineno ? (USE_COLOR ? "\033[31;1m>\033[m " : "> ") : "  ",
                    num_width, cur_line, USE_COLOR ? (cur_line == lineno ? "\033[0;31;1m" : "\033[0m") : "", line);

        cur_line += 1;
        if (cur_line > lineno + context_after)
            break;
    }
    if (line) free(line);
    fclose(f);
}

typedef struct stack_info_s {
    const char *function, *filename;
    int lineno;
    struct stack_info_s *next;
} stack_info_t;

// Simple callback to print each frame
static int print_callback(void *data, uintptr_t pc, const char *filename, int lineno, const char *function)
{
    (void)pc;
    stack_info_t *info = GC_MALLOC(sizeof(stack_info_t));
    info->next = *(stack_info_t**)data;
    info->function = function;
    info->filename = filename;
    info->lineno = lineno;
    *(stack_info_t**)data = info;
    return 0;
}

public void print_stacktrace(FILE *out, int offset)
{
    stack_info_t *backwards = NULL;

    backtrace_full(bt_state, offset, print_callback, NULL, &backwards);

    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) == NULL)
        errx(1, "Path too large!");
    size_t cwd_len = strlen(cwd);
    if (cwd_len + 2 > sizeof(cwd))
        errx(1, "Path too large!");
    cwd[cwd_len++] = '/';
    cwd[cwd_len] = '\0';

    // Skip C entrypoint stuff:
    while (backwards && backwards->function == NULL)
        backwards = backwards->next;

    if (backwards && strstr(backwards->function, "$parse_and_run"))
        backwards = backwards->next;

    for (stack_info_t *frame = backwards; frame; frame = frame->next) {
        while (frame && frame->function == NULL) {
            fprintf(out, USE_COLOR ? "\033[2m... unknown function ...\033[m\n" : "... unknown function ...\n");
            if (frame->next)
                fprintf(out, "\n");
            frame = frame->next;
        }
        if (frame == NULL) break;

        char *function_display = GC_MALLOC_ATOMIC(strlen(frame->function));
        memcpy(function_display, frame->function, strlen(frame->function)+1);
        function_display += strcspn(function_display, "$");
        function_display += strspn(function_display, "$");
        function_display += strcspn(function_display, "$");
        function_display += strspn(function_display, "$");
        for (char *p = function_display; *p; p++) {
            if (*p == '$') *p = '.';
        }

        if (strncmp(frame->filename, cwd, cwd_len) == 0)
            frame->filename += cwd_len;

        fprintf(out, USE_COLOR ? "\033[1mIn \033[33m%s()\033[37m" : "In %s()", function_display);
        if (frame->filename)
            fprintf(out, USE_COLOR ? " in \033[35m%s:%d" : " in %s:%d", frame->filename, frame->lineno);
        fprintf(out, USE_COLOR ? "\033[m\n" : "\n");
        if (frame->filename)
            fprint_context(out, frame->filename, frame->lineno, 3, 1);
        if (frame->next)
            fprintf(out, "\n");
    }
}

public void initialize_stacktrace(const char *program)
{
    bt_state = backtrace_create_state(program, 1, NULL, NULL);
    if (!bt_state)
        errx(1, "Failed to create stacktrace state");
}
