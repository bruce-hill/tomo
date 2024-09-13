//
// files.c - Implementation of some file loading functionality.
//

#include <ctype.h>
#include <err.h>
#include <fcntl.h>
#include <gc.h>
#include <libgen.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>

#include "files.h"
#include "util.h"

static const int tabstop = 4;

public char *resolve_path(const char *path, const char *relative_to, const char *system_path)
{
    if (!relative_to || streq(relative_to, "/dev/stdin")) relative_to = ".";
    if (!path || strlen(path) == 0) return NULL;

    // Resolve the path to an absolute path, assuming it's relative to the file
    // it was found in:
    char buf[PATH_MAX] = {0};
    if (streq(path, "~") || starts_with(path, "~/")) {
        char *resolved = realpath(heap_strf("%s%s", getenv("HOME"), path+1), buf);
        if (resolved) return GC_strdup(resolved);
    } else if (streq(path, ".") || starts_with(path, "./") || starts_with(path, "../")) {
        char *relative_dir = dirname(GC_strdup(relative_to));
        char *resolved = realpath(heap_strf("%s/%s", relative_dir, path), buf);
        if (resolved) return GC_strdup(resolved);
    } else if (path[0] == '/') {
        // Absolute path:
        char *resolved = realpath(path, buf);
        if (resolved) return GC_strdup(resolved);
    } else {
        // Relative path:
        char *relative_dir = dirname(GC_strdup(relative_to));
        if (!system_path) system_path = ".";
        char *copy = GC_strdup(system_path);
        for (char *dir, *pos = copy; (dir = strsep(&pos, ":")); ) {
            if (dir[0] == '/') {
                char *resolved = realpath(heap_strf("%s/%s", dir, path), buf);
                if (resolved) return GC_strdup(resolved);
            } else if (dir[0] == '~' && (dir[1] == '\0' || dir[1] == '/')) {
                char *resolved = realpath(heap_strf("%s%s/%s", getenv("HOME"), dir+1, path), buf);
                if (resolved) return GC_strdup(resolved);
            } else if (streq(dir, ".") || strncmp(dir, "./", 2) == 0) {
                char *resolved = realpath(heap_strf("%s/%s", relative_dir, path), buf);
                if (resolved) return GC_strdup(resolved);
            } else if (streq(dir, ".") || streq(dir, "..") || strncmp(dir, "./", 2) == 0 || strncmp(dir, "../", 3) == 0) {
                char *resolved = realpath(heap_strf("%s/%s/%s", relative_dir, dir, path), buf);
                if (resolved) return GC_strdup(resolved);
            } else {
                char *resolved = realpath(heap_strf("%s/%s", dir, path), buf);
                if (resolved) return GC_strdup(resolved);
            }
        }
    }
    return NULL;
}

public char *file_base_name(const char *path)
{
    const char *slash = strrchr(path, '/');
    if (slash) path = slash + 1;
    assert(!isdigit(*path));
    const char *end = strchrnul(path, '.');
    size_t len = (size_t)(end - path);
    char *buf = GC_MALLOC_ATOMIC(len+1);
    strncpy(buf, path, len);
    buf[len] = '\0';
    for (char *p = buf; *p; p++) {
        if (!isalnum(*p))
            *p = '_';
    }
    return buf;
}

static file_t *_load_file(const char* filename, FILE *file)
{
    if (!file) return NULL;

    file_t *ret = new(file_t, .filename=filename);

    size_t file_size = 0, line_cap = 0;
    char *file_buf = NULL, *line_buf = NULL;
    FILE *mem = open_memstream(&file_buf, &file_size);
    int64_t line_len = 0;
    while ((line_len = getline(&line_buf, &line_cap, file)) >= 0) {
        if (ret->line_capacity <= ret->num_lines)
            ret->line_offsets = GC_REALLOC(ret->line_offsets, sizeof(int64_t[ret->line_capacity += 32]));
        ret->line_offsets[ret->num_lines++] = (int64_t)file_size;
        fwrite(line_buf, sizeof(char), (size_t)line_len, mem);
        fflush(mem);
    }
    fclose(file);

    char *copy = GC_MALLOC_ATOMIC(file_size+1);
    memcpy(copy, file_buf, file_size);
    copy[file_size] = '\0';
    ret->text = copy;
    ret->len = (int64_t)file_size;
    fclose(mem);

    free(file_buf);
    ret->relative_filename = filename;
    if (filename && filename[0] != '<' && !streq(filename, "/dev/stdin")) {
        filename = resolve_path(filename, ".", ".");
        // Convert to relative path (if applicable)
        char buf[PATH_MAX];
        char *cwd = getcwd(buf, sizeof(buf));
        size_t cwd_len = strlen(cwd);
        if (strncmp(cwd, filename, cwd_len) == 0 && filename[cwd_len] == '/')
            ret->relative_filename = &filename[cwd_len+1];
    }
    return ret;
}

//
// Read an entire file into memory.
//
public file_t *load_file(const char* filename)
{
    FILE *file = filename[0] ? fopen(filename, "r") : stdin;
    return _load_file(filename, file);
}

//
// Create a virtual file from a string.
//
public file_t *spoof_file(const char* filename, const char *text)
{
    FILE *file = fmemopen((char*)text, strlen(text)+1, "r");
    return _load_file(filename, file);
}

//
// Given a pointer, determine which line number it points to (1-indexed)
//
public int64_t get_line_number(file_t *f, const char *p)
{
    // Binary search:
    int64_t lo = 0, hi = (int64_t)f->num_lines-1;
    if (p < f->text) return 0;
    int64_t offset = (int64_t)(p - f->text);
    while (lo <= hi) {
        int64_t mid = (lo + hi) / 2;
        int64_t line_offset = f->line_offsets[mid];
        if (line_offset == offset)
            return mid + 1;
        else if (line_offset < offset)
            lo = mid + 1;    
        else if (line_offset > offset)
            hi = mid - 1;
    }
    return lo; // Return the line number whose line starts closest before p
}

//
// Given a pointer, determine which line column it points to.
//
public int64_t get_line_column(file_t *f, const char *p)
{
    int64_t line_no = get_line_number(f, p);
    int64_t line_offset = f->line_offsets[line_no-1];
    return 1 + (int64_t)(p - (f->text + line_offset));
}

//
// Return a pointer to the line with the specified line number (1-indexed)
//
public const char *get_line(file_t *f, int64_t line_number)
{
    if (line_number == 0 || line_number > (int64_t)f->num_lines) return NULL;
    int64_t line_offset = f->line_offsets[line_number-1];
    return f->text + line_offset;
}

//
// Return a value like /foo:line:col
//
public const char *get_file_pos(file_t *f, const char *p)
{
    return heap_strf("%s:%ld:%ld", f->filename, get_line_number(f, p), get_line_column(f, p));
}

static int fputc_column(FILE *out, char c, char print_char, int *column)
{
    int printed = 0;
    if (print_char == '\t') print_char = ' ';
    if (c == '\t') {
        for (int to_fill = tabstop - (*column % tabstop); to_fill > 0; --to_fill) {
            printed += fputc(print_char, out);
            ++*column;
        }
    } else {
        printed += fputc(print_char, out);
        ++*column;
    }
    return printed;
}

//
// Print a span from a file
//
public int highlight_error(file_t *file, const char *start, const char *end, const char *hl_color, int64_t context_lines, bool use_color)
{
    if (!file) return 0;

    // Handle spans that come from multiple files:
    if (start < file->text || start > file->text + file->len)
        start = end;
    if (end < file->text || end > file->text + file->len)
        end = start;
    // Just in case neither end of the span came from this file:
    if (end < file->text || end > file->text + file->len)
        start = end = file->text;

    const char *lineno_fmt, *normal_color, *empty_marker;
    bool print_carets = false;
    int printed = 0;
    if (use_color) {
        lineno_fmt = "\x1b[0;2m%*lu\x1b(0\x78\x1b(B\x1b[m ";
        normal_color = "\x1b[m";
        empty_marker = "\x1b(0\x61\x1b(B";
        printed += fprintf(stderr, "\x1b[33;4;1m%s\x1b[m\n", file->relative_filename);
    } else {
        lineno_fmt = "%*lu| ";
        hl_color = "";
        normal_color = "";
        empty_marker = " ";
        print_carets = true;
        printed += fprintf(stderr, "%s\n", file->relative_filename);
    }

    if (context_lines == 0)
        return fprintf(stderr, "%s%.*s%s", hl_color, (int)(end - start), start, normal_color);

    int64_t start_line = get_line_number(file, start),
            end_line = get_line_number(file, end);

    int64_t first_line = start_line - (context_lines - 1),
            last_line = end_line + (context_lines - 1);

    if (first_line < 1) first_line = 1;
    if (last_line > file->num_lines) last_line = file->num_lines;

    int digits = 1;
    for (int64_t i = last_line; i > 0; i /= 10) ++digits;

    for (int64_t line_no = first_line; line_no <= last_line; ++line_no) {
        if (line_no > first_line + 5 && line_no < last_line - 5) {
            if (use_color)
                printed += fprintf(stderr, "\x1b[0;2;3;4m     ... %ld lines omitted ...     \x1b[m\n", (last_line - first_line) - 11);
            else
                printed += fprintf(stderr, "     ... %ld lines omitted ...\n", (last_line - first_line) - 11);
            line_no = last_line - 6;
            continue;
        }

        printed += fprintf(stderr, lineno_fmt, digits, line_no);
        const char *line = get_line(file, line_no);
        if (!line) break;

        int column = 0;
        const char *p = line;
        // Before match
        for (; *p && *p != '\r' && *p != '\n' && p < start; ++p)
            printed += fputc_column(stderr, *p, *p, &column);

        // Zero-width matches
        if (p == start && start == end) {
            printed += fprintf(stderr, "%s%s%s", hl_color, empty_marker, normal_color);
            column += 1;
        }

        // Inside match
        if (start <= p && p < end) {
            printed += fputs(hl_color, stderr);
            for (; *p && *p != '\r' && *p != '\n' && p < end; ++p)
                printed += fputc_column(stderr, *p, *p, &column);
            printed += fputs(normal_color, stderr);
        }

        // After match
        for (; *p && *p != '\r' && *p != '\n'; ++p)
            printed += fputc_column(stderr, *p, *p, &column);

        printed += fprintf(stderr, "\n");

        const char *eol = strchrnul(line, '\n');
        if (print_carets && start >= line && start < eol && line <= start) {
            for (int num = 0; num < digits; num++)
                printed += fputc(' ', stderr);
            printed += fputs(": ", stderr);
            int col = 0;
            for (const char *sp = line; *sp && *sp != '\n'; ++sp) {
                char print_char;
                if (sp < start)
                    print_char = ' ';
                else if (sp == start && sp == end)
                    print_char = '^';
                else if (sp >= start && sp < end)
                    print_char = '-';
                else
                    print_char = ' ';
                printed += fputc_column(stderr, *sp, print_char, &col);
            }
            printed += fputs("\n", stderr);
        }
    }
    fflush(stderr);
    return printed;
}

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
