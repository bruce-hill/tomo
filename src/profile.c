// Implementation of the tomo CLI phase profiler (see profile.h).

#include <gc.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "profile.h"
#include "stdlib/stdlib.h" // USE_COLOR

OptionalBool_t profiling = false;

// Named spans are stored in a fixed-size, insertion-ordered table. The set of
// phases is small and known (a dozen or so), so a linear scan by name keeps the
// code trivial and the report order stable/readable.
#define MAX_SPANS 64
static struct {
    const char *name;
    double total; // accumulated seconds
    long count; // number of times this span was timed
} spans[MAX_SPANS];
static int num_spans = 0;

static double program_start = -1.0; // CLOCK_MONOTONIC seconds at main() entry
static bool reported = false;

static double now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

void profile_mark_start(void) { program_start = now(); }

profile_span_t profile_begin(const char *name) {
    if (!profiling) return (profile_span_t){.name = name, .start = -1.0};
    return (profile_span_t){.name = name, .start = now()};
}

// Look up (or, on first use, append) the accumulator for `name`.
static int span_index(const char *name) {
    for (int i = 0; i < num_spans; i++)
        if (strcmp(spans[i].name, name) == 0) return i;
    if (num_spans >= MAX_SPANS) return -1; // silently drop overflow; report stays bounded
    spans[num_spans] = (typeof(spans[0])){.name = name, .total = 0.0, .count = 0};
    return num_spans++;
}

static void profile_add(const char *name, double seconds, long count) {
    int i = span_index(name);
    if (i < 0) return;
    spans[i].total += seconds;
    spans[i].count += count;
}

void profile_end(profile_span_t span) {
    if (span.start < 0.0) return; // profiling was off when the span began
    profile_add(span.name, now() - span.start, 1);
}

void profile_reset(void) {
    num_spans = 0;
}

void profile_serialize(int fd) {
    if (!profiling) return;
    // One record per line: "<seconds> <count> <name>\n". The name is last so it
    // may contain spaces; seconds/count never do.
    char buf[256];
    for (int i = 0; i < num_spans; i++) {
        int n = snprintf(buf, sizeof(buf), "%.9f %ld %s\n", spans[i].total, spans[i].count, spans[i].name);
        if (n > 0) (void)!write(fd, buf, (size_t)n);
    }
}

void profile_merge(int fd) {
    if (!profiling) return;
    FILE *f = fdopen(fd, "r");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        double seconds;
        long count;
        int offset = 0;
        if (sscanf(line, "%lf %ld %n", &seconds, &count, &offset) < 2) continue;
        char *name = line + offset;
        size_t len = strlen(name);
        if (len > 0 && name[len - 1] == '\n') name[len - 1] = '\0';
        // The merged name comes from a child's buffer, not a literal, so it must
        // be interned to outlive this loop's stack frame.
        profile_add(GC_strdup(name), seconds, count);
    }
    fclose(f); // also closes fd
}

void profile_report(void) {
    if (!profiling || reported) return;
    reported = true;

    double total = program_start >= 0.0 ? now() - program_start : 0.0;
    double accounted = 0.0;
    for (int i = 0; i < num_spans; i++)
        accounted += spans[i].total;

    const char *dim = USE_COLOR ? "\x1b[2m" : "";
    const char *bold = USE_COLOR ? "\x1b[1m" : "";
    const char *reset = USE_COLOR ? "\x1b[m" : "";

    fprintf(stderr, "\n%s┈┈┈ tomo profile ┈┈┈%s\n", dim, reset);
    fprintf(stderr, "%s%-22s %10s %6s %6s%s\n", bold, "phase", "time", "%", "calls", reset);
    for (int i = 0; i < num_spans; i++) {
        double pct = total > 0.0 ? 100.0 * spans[i].total / total : 0.0;
        fprintf(stderr, "%-22s %8.2f ms %5.1f%% %6ld\n", spans[i].name, spans[i].total * 1e3, pct, spans[i].count);
    }
    fprintf(stderr, "%s┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈%s\n", dim, reset);
    if (total > 0.0) {
        double other = total - accounted;
        double other_pct = 100.0 * other / total;
        fprintf(stderr, "%s%-22s %8.2f ms %5.1f%%%s\n", dim, "(unprofiled)", other * 1e3, other_pct, reset);
        fprintf(stderr, "%s%-22s %8.2f ms%s\n", bold, "total", total * 1e3, reset);
    }
    fflush(stderr);
}
