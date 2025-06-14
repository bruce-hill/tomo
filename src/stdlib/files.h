//
// files.h - Definitions of an API for loading files.
//
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>

typedef struct {
    const char *filename, *relative_filename;
    const char *text;
    int64_t len;
    int64_t num_lines, line_capacity;
    int64_t *line_offsets;
} file_t;

char *resolve_path(const char *path, const char *relative_to, const char *system_path);
__attribute__((pure, nonnull))
char *file_base_name(const char *path);
__attribute__((nonnull))
file_t *load_file(const char *filename);
__attribute__((nonnull, returns_nonnull))
file_t *spoof_file(const char *filename, const char *text);
__attribute__((pure, nonnull))
int64_t get_line_number(file_t *f, const char *p);
__attribute__((pure, nonnull))
int64_t get_line_column(file_t *f, const char *p);
__attribute__((pure, nonnull))
const char *get_line(file_t *f, int64_t line_number);
__attribute__((pure, nonnull))
const char *get_file_pos(file_t *f, const char *p);
int highlight_error(file_t *file, const char *start, const char *end, const char *hl_color, int64_t context_lines, bool use_color);

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
