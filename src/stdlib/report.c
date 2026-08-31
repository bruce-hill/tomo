// Shared report presentation. See report.h.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "util.h"

#include "report.h"
#include "stdlib.h" // USE_COLOR

#define REPORT_MAX_WIDTH 96

public
style_t report_style(void) {
    if (USE_COLOR)
        return (style_t){
            .green = "\x1b[92m",
            .red = "\x1b[91m",
            .dim = "\x1b[2m",
            .bold = "\x1b[1m",
            .reset = "\x1b[m",
            .hdr = "\x1b[93;1;4m",
            .pass_mark = "✔",
            .fail_mark = "✘",
            .gutter = "│",
            .point = "▸",
            .dot = "·",
            .rule = "─",
            .under = "━",
            .sep = "·",
        };
    return (style_t){
        .green = "",
        .red = "",
        .dim = "",
        .bold = "",
        .reset = "",
        .hdr = "",
        .pass_mark = "ok",
        .fail_mark = "FAIL",
        .gutter = "|",
        .point = ">",
        .dot = ".",
        .rule = "-",
        .under = "^",
        .sep = ",",
    };
}

public
int report_width(void) {
    const char *cols = getenv("COLUMNS");
    int w = cols ? atoi(cols) : 0;
    if (w < 40) {
        struct winsize ws;
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) w = (int)ws.ws_col;
    }
    if (w < 40) w = 80;
    return w > REPORT_MAX_WIDTH ? REPORT_MAX_WIDTH : w;
}

public
void report_repeat(FILE *f, const char *s, int n) {
    for (int i = 0; i < n; i++)
        fputs(s, f);
}

public
int report_display_width(const char *s) {
    int w = 0;
    for (; *s; s++)
        if ((*s & 0xC0) != 0x80) w += 1;
    return w;
}

public
void report_duration(char *buf, size_t n, double seconds) {
    if (seconds >= 60.0) snprintf(buf, n, "%dm %02ds", (int)(seconds / 60), (int)seconds % 60);
    else if (seconds >= 1.0) snprintf(buf, n, "%.2fs", seconds);
    else snprintf(buf, n, "%dms", (int)(seconds * 1000.0 + 0.5));
}

public
void report_leader(FILE *f, int indent, bool ok, const char *name, int right_width) {
    style_t s = report_style();
    const char *mark = ok ? s.pass_mark : s.fail_mark;
    int left = report_display_width(mark) + 1 + report_display_width(name);
    int dots = report_width() - 2 * indent - left - right_width - 2;
    if (dots < 1) dots = 1;
    fprintf(f, "%*s%s%s%s %s %s", indent, "", ok ? s.green : s.red, mark, s.reset, name, s.dim);
    report_repeat(f, s.dot, dots);
    fprintf(f, "%s ", s.reset);
}
