// Runtime support for `--instrument` builds: a deterministic (exact-count)
// function profiler.
//
// When a program is compiled with `tomo build --instrument`, the compiler emits
// a TOMO_PROFILED() line at the top of every function, conversion, and lambda it
// generates, plus one static tomo_profile_site_t per function holding that
// function's name, source location, and running totals. An instrumented program
// profiles itself every run and prints its report to stderr when it exits; two
// environment variables adjust that (the program's own command-line arguments
// are never touched, so instrumenting can't shadow a flag it defines):
//
//   PROFILE=0           collect nothing and print nothing this run
//   PROFILE_FILE=path   write the table there instead of to stderr (`-` = stdout)
//   FLAME_GRAPH=path    also write the call tree there as an SVG flame graph
//
// Timing is inclusive/exclusive: a function's `self` time is its own work and
// its `total` is the whole subtree. Both are derived at report time by walking
// the call tree, which is what keeps the per-call cost down: a call updates its
// tree node and nothing else. Recursion is likewise a report-time rule (a
// node's time is only counted as inclusive when no caller above it is the same
// function), so a recursive function's total isn't counted once per level.
//
// Timestamps come from the CPU's cycle counter where there is a usable one
// (~4ns a pair, against ~45ns for a pair of clock_gettime() calls). Nothing
// depends on knowing its frequency: the profiler samples CLOCK_MONOTONIC and
// the counter together at both ends of the run and divides, so the counter only
// has to tick at a constant rate, not a known one.
//
// The same machinery profiles the compiler itself: `tomo --profile` times its
// phases with TOMO_PROFILE_SPAN() instead of instrumented functions, and gets
// the same table (and, with FLAME_GRAPH set, the same graph) out of the same
// call tree. See src/tomo.c.
//
// This is single-threaded, matching the rest of the Tomo runtime.

#pragma once

#include <stdbool.h>
#include <stdint.h>

// One instrumented function. The compiler emits these as file-scope statics,
// zero-initialized apart from the name and location; the totals below are
// filled in from the call tree when the report is printed, and a site joins the
// report's list at that point rather than when it first runs.
typedef struct tomo_profile_site_s {
    const char *name; // the Tomo-level name, e.g. "Foo.bar" or "lambda"
    const char *file; // the .tm file it was defined in
    int64_t line;
    // Where this function's last call from `cached_parent` landed in the call
    // tree. A function is overwhelmingly called from the same place it was
    // called from last time, so this one comparison usually replaces the scan
    // over the caller's callees.
    struct tomo_profile_node_s *cached_parent, *cached_node;
    // Totals, summed out of the call tree when the report is printed:
    int64_t calls;
    uint64_t total_ticks; // inclusive (recursion counted once)
    uint64_t self_ticks; // exclusive (callees subtracted)
    bool registered;
    struct tomo_profile_site_s *next; // next in the registration list
} tomo_profile_site_t;

// One node of the call tree: a site reached by one particular path of callers.
// This is the only thing a call updates, and everything reported is summed out
// of it afterwards: the flame graph draws these nodes directly, and the table
// merges the nodes of each function into a single row.
typedef struct tomo_profile_node_s {
    tomo_profile_site_t *site;
    struct tomo_profile_node_s *parent;
    struct tomo_profile_node_s *children, *next_sibling; // children in first-called order
    int64_t calls;
    uint64_t total_ticks; // inclusive time along this path
} tomo_profile_node_t;

// One live call. Lives on the stack of the function being profiled, unwound by
// the cleanup attribute in TOMO_PROFILED().
typedef struct tomo_profile_frame_s {
    // This call's place in the call tree, and the flag for whether there is
    // anything to record at all: NULL means profiling is off this run.
    tomo_profile_node_t *node;
    struct tomo_profile_frame_s *parent;
    uint64_t start; // cycle counter (or nanoseconds) at entry
    int depth; // how many instrumented calls are below this one
    // Whether this frame reuses its caller's node (a recursive call, or one
    // past the depth limit). Its time is already inside that node's total, so
    // it adds calls but not time.
    bool folded;
} tomo_profile_frame_t;

// Whether this run is collecting (true in an instrumented program unless
// PROFILE=0). The check is inlined into the instrumented functions themselves.
extern bool tomo_profiling_enabled;

// What the report calls the things it lists: "function" for an instrumented
// program, which the compiler overrides with "phase" for its own spans.
extern const char *tomo_profile_noun;

void tomo_profile_begin(tomo_profile_frame_t *frame, tomo_profile_site_t *site);
void tomo_profile_end(tomo_profile_frame_t *frame);

// Instrument the enclosing function. The cleanup attribute makes the timer stop
// on every path out of the function, whether `return`, a deferred block, or
// falling off the end, without the compiler having to find each exit point.
// Only the one field is assigned, rather than initializing the whole struct:
// `= {.node = NULL}` would zero every other member too, which is pure cost in a
// build that is running with PROFILE=0. Nothing can leave the function between
// the declaration and the assignment, so the cleanup handler never sees it
// uninitialized.
#define TOMO_PROFILED(_site)                                                                                           \
    tomo_profile_frame_t _tomo_profile_frame __attribute__((cleanup(tomo_profile_leave)));                             \
    _tomo_profile_frame.node = NULL;                                                                                   \
    if (tomo_profiling_enabled) tomo_profile_begin(&_tomo_profile_frame, (_site))

// Stops the frame's timer, unless profiling was off when it started (in which
// case leaving an instrumented function costs one predictable branch).
static inline void tomo_profile_leave(tomo_profile_frame_t *frame) {
    if (frame->node) tomo_profile_end(frame);
}

// Time a statement (or a braced block) as a named span. Spans are ordinary
// sites: they nest, they accumulate across repeats, and they appear in the
// report and the graph exactly like instrumented functions do:
//     TOMO_PROFILE_SPAN("parse", ast = parse_file(path));
// (the parameters are `_name`/`_site` so that the designated initializers'
// `.name`/`.site` are not themselves substituted)
#define TOMO_PROFILE_SPAN(_name, ...)                                                                                  \
    do {                                                                                                               \
        static tomo_profile_site_t _span_site = {.name = _name, .file = __FILE__, .line = __LINE__};                   \
        TOMO_PROFILED(&_span_site);                                                                                    \
        __VA_ARGS__;                                                                                                   \
    } while (0)

// The same thing for a region that can't be wrapped in a block, because what it
// declares is used after it. `var` names the span; end it with
// TOMO_PROFILE_SPAN_END(var) on every path out.
#define TOMO_PROFILE_SPAN_BEGIN(var, _name)                                                                            \
    static tomo_profile_site_t var##_site = {.name = _name, .file = __FILE__, .line = __LINE__};                       \
    tomo_profile_frame_t var;                                                                                          \
    var.node = NULL;                                                                                                   \
    if (tomo_profiling_enabled) tomo_profile_begin(&var, &var##_site)
#define TOMO_PROFILE_SPAN_END(var) tomo_profile_leave(&var)

// Stamp the clocks, so a report covers the whole run rather than starting from
// whenever collection was switched on. Cheap and unconditional; call it as
// early in main() as possible.
void tomo_profile_mark_start(void);

// Start collecting unless PROFILE says otherwise, and arrange for the report to
// be printed at exit. Called at the top of the main() of a program compiled
// with --instrument, before anything else runs.
void tomo_profile_start(void);

// Start collecting because something other than the environment asked for it
// (the compiler's own `--profile` flag). PROFILE_FILE and FLAME_GRAPH still
// choose where the output goes.
void tomo_profile_enable(void);

// Forget everything collected so far. Called in a freshly forked child, so that
// it profiles only the work it does rather than the parent history it inherited
// by copy-on-write.
void tomo_profile_reset(void);

// Hand this process's totals to another one: a forked child writes them to a
// pipe before _exit (its own report would never be printed), and the parent
// merges them into its tree as top-level entries. Both are no-ops when this run
// isn't collecting.
void tomo_profile_serialize(int fd);
void tomo_profile_merge(int fd);

// Print the table to PROFILE_FILE (stderr by default), and the flame graph to
// FLAME_GRAPH if one was asked for. Registered with atexit() by
// tomo_profile_start(), so it also runs when the program exits early or dies
// with `fail()`.
void tomo_profile_report(void);
