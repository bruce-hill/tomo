// Lightweight phase profiler for the tomo CLI, enabled by the global
// `--profile` flag. It accumulates wall-clock time under named spans and, at
// the end of a command, prints a breakdown of where the time went.
//
// Spans accumulate by name, so timing the same phase repeatedly (e.g. once per
// compiled file) sums into a single line. Recording is near-free when
// profiling is off: every entry point early-returns on the `profiling` flag.
//
// Fork awareness: parts of the build (transpiling + `zig cc -c`) run in forked
// children whose accumulated spans never reach the parent's address space. A
// child serializes its spans with profile_serialize() before _exit and the
// parent folds them in with profile_merge(); see compile_files().

#pragma once

#include <stdbool.h>

#include "stdlib/bools.h"

// Destination of the `--profile` global flag (see tomo.c). When false, every
// function here is a cheap no-op.
extern OptionalBool_t profiling;

// A single running span. Returned by profile_begin() and passed back to
// profile_end(); treat it as opaque.
typedef struct {
    const char *name;
    double start; // seconds, CLOCK_MONOTONIC; negative when profiling is off
} profile_span_t;

// Stamp the program start time, so the final report can show total elapsed
// time and how much of it the profiled phases account for. Call once, early in
// main(), before the flag is even parsed (it's cheap and unconditional).
void profile_mark_start(void);

// Begin timing a named span. `name` must outlive the report (use a string
// literal). Returns a handle to pass to profile_end().
profile_span_t profile_begin(const char *name);

// End a span begun by profile_begin(), adding its elapsed time to `name`'s
// running total (creating the entry on first use).
void profile_end(profile_span_t span);

// Time a single statement or block, accumulating under `name`. The braces keep
// the temporary span scoped:
//   PROFILE("parse", ast = parse_file(...));
#define PROFILE(name, ...)                                                                                              \
    do {                                                                                                               \
        profile_span_t _span = profile_begin(name);                                                                    \
        __VA_ARGS__;                                                                                                   \
        profile_end(_span);                                                                                            \
    } while (0)

// Discard every span accumulated so far. Called in a freshly forked child so it
// only reports the work it does, not the parent history it inherited by COW.
void profile_reset(void);

// Serialize this process's spans to `fd` as newline-delimited records (used by
// forked children to hand their timings back to the parent).
void profile_serialize(int fd);

// Read serialized spans from `fd` and add them into this process's totals.
void profile_merge(int fd);

// Print the profile breakdown to stderr (once; repeat calls are no-ops). Safe
// to call on every command-exit path, including right before an execv() that
// would otherwise skip atexit() handlers.
void profile_report(void);
