//
// files.h - Definitions of an API for loading files.
//
#pragma once

#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

typedef struct {
    int64_t offset;
    int64_t indent:63;
    bool is_empty:1;
} sss_line_t;

typedef struct {
    const char *filename, *relative_filename;
    const char *text;
    int64_t len;
    int64_t num_lines, line_capacity;
    sss_line_t *lines;
} sss_file_t;

char *resolve_path(const char *path, const char *relative_to);
__attribute__((nonnull))
sss_file_t *sss_load_file(const char *filename);
__attribute__((nonnull, returns_nonnull))
sss_file_t *sss_spoof_file(const char *filename, const char *text);
__attribute__((pure, nonnull))
int64_t sss_get_line_number(sss_file_t *f, const char *p);
__attribute__((pure, nonnull))
int64_t sss_get_line_column(sss_file_t *f, const char *p);
__attribute__((pure, nonnull))
int64_t sss_get_indent(sss_file_t *f, const char *p);
__attribute__((pure, nonnull))
const char *sss_get_line(sss_file_t *f, int64_t line_number);
__attribute__((pure, nonnull))
const char *sss_get_file_pos(sss_file_t *f, const char *p);
int fprint_span(FILE *out, sss_file_t *file, const char *start, const char *end, const char *hl_color, int64_t context_lines, bool use_color);

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
