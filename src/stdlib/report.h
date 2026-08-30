// Shared presentation for tomo's multi-file reports (`tomo test`, `tomo fmt
// --check`). One palette and one way to draw a "name ········ value" row, so
// the commands read as the same tool rather than as two lookalikes that drift.
#pragma once

#include <stdbool.h>
#include <stdio.h>

typedef struct {
    const char *green, *red, *dim, *bold, *reset, *hdr;
    const char *pass_mark, *fail_mark, *gutter, *point, *dot, *rule, *under, *sep;
} style_t;

// The palette, or an all-ASCII one with every escape emptied out when color is off:
style_t report_style(void);

// How wide to draw a report. Terminals narrower than the content get the
// content anyway (wrapping beats truncating a diagnostic):
int report_width(void);

void report_repeat(FILE *f, const char *s, int n);

// Columns, not bytes: the marks are multi-byte UTF-8 ("✔" is three bytes wide
// but one column), so measuring with strlen would make lines come up short.
int report_display_width(const char *s);

// "0.42s" / "84ms" / "1m 03s", short enough to sit at the end of a line.
void report_duration(char *buf, size_t n, double seconds);

// Draw "<mark> <name> ···········" and leave the cursor where a right-hand
// field of `right_width` columns should start. Sizing the leader from the
// declared width, rather than letting it absorb whatever the caller prints,
// is what keeps those right-hand fields in a column across rows.
void report_leader(FILE *f, int indent, bool ok, const char *name, int right_width);
