// Implementation of the runtime function profiler (see profiling.h).

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#if defined(__x86_64__) || defined(__i386__)
#include <cpuid.h>
#include <x86intrin.h>
#endif
#include <unistd.h>

#include "print.h"
#include "profiling.h"
#include "stdlib.h" // USE_COLOR
#include "util.h"

public
bool tomo_profiling_enabled = false;

public
const char *tomo_profile_noun = "function";

// The functions the report lists, collected when the call tree is summed up at
// exit (so a function that never ran never appears), and the frame of the call
// currently executing. Plain statics: like the rest of the runtime, this is
// single-threaded.
static tomo_profile_site_t *sites = NULL;
static tomo_profile_frame_t *current_frame = NULL;

// Everything the profiler records. The root is synthetic: its children are the
// outermost instrumented calls (usually just main()). Both the table and the
// graph are derived from this at exit.
static tomo_profile_node_t call_tree_root = {};
// Bounds on the tree, so a program with unbounded call depth (mutual recursion)
// or an enormous number of distinct call paths can't grow it without limit.
// Calls past either limit fold into their caller's node: their time still
// counts, it just isn't split out.
#define MAX_TREE_DEPTH 64
#define MAX_TREE_NODES 8192
static int tree_nodes = 0;

static const char *report_path = NULL; // where the table goes; NULL means stderr
static const char *flame_path = NULL; // where the flame graph goes, if it was asked for
// Both clocks sampled at the start of the run, and the tick rate worked out
// from a second sample when the report is printed. Measuring the rate over the
// whole run is what lets the cycle counter be used without knowing its
// frequency (and makes any constant error in it cancel out).
static double profiling_started = 0.0;
static uint64_t ticks_started = 0;
static double seconds_per_tick = 1e-9;

// Ticks are only meaningful once the report knows how fast they ran:
static double tick_seconds(uint64_t t) {
    return (double)t * seconds_per_tick;
}

static bool reported = false;

static double now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

// Whether the CPU's cycle counter is usable as a clock. Set once at startup;
// when it isn't, ticks are plain nanoseconds and everything downstream is
// unchanged, because the tick rate is measured rather than assumed.
static bool use_cycle_counter = false;

// The timestamp taken on entering and leaving every instrumented call, so this
// is the hottest code in an instrumented build. `rdtsc` is a handful of cycles
// against ~20ns for clock_gettime(), which even through the vDSO is a real
// function call; the branch on `use_cycle_counter` costs nothing measurable
// since it always goes the same way.
static inline uint64_t ticks(void) {
#if defined(__x86_64__) || defined(__i386__)
    if (use_cycle_counter) return __rdtsc();
#elif defined(__aarch64__)
    if (use_cycle_counter) {
        uint64_t counter;
        __asm__ volatile("mrs %0, cntvct_el0" : "=r"(counter));
        return counter;
    }
#endif
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec;
}

// A cycle counter is only a clock if it ticks at a constant rate regardless of
// what the CPU's frequency is doing. On x86 that is the "invariant TSC" feature
// bit; on ARM the architected counter is invariant by definition. Anywhere else
// (or on an x86 too old to promise it), fall back to clock_gettime().
static bool cycle_counter_is_usable(void) {
#if defined(__x86_64__) || defined(__i386__)
    unsigned int eax, ebx, ecx, edx;
    if (__get_cpuid_max(0x80000000u, NULL) < 0x80000007u) return false;
    if (!__get_cpuid(0x80000007u, &eax, &ebx, &ecx, &edx)) return false;
    return (edx & (1u << 8)) != 0; // invariant TSC
#elif defined(__aarch64__)
    return true;
#else
    return false;
#endif
}

// Sums the call tree into the per-function totals (defined below, next to the
// report that is its main caller; a forked child also needs it before handing
// its totals over).
static void aggregate_sites(tomo_profile_node_t *parent);

// The node for `site` called from `parent`: the existing child if this path has
// been walked before, otherwise a fresh one. Returns `parent` itself when the
// call is directly recursive (so `fib` calling `fib` stays one node instead of
// growing a node per level) or when a limit has been hit; the caller records
// that as a folded frame.
static tomo_profile_node_t *tree_node_for(tomo_profile_node_t *parent, tomo_profile_site_t *site, int depth) {
    if (parent->site == site || depth >= MAX_TREE_DEPTH) return parent;
    for (tomo_profile_node_t *child = parent->children; child; child = child->next_sibling)
        if (child->site == site) return child;
    if (tree_nodes >= MAX_TREE_NODES) return parent;
    tomo_profile_node_t *node = calloc(1, sizeof(tomo_profile_node_t));
    if (!node) return parent;
    tree_nodes += 1;
    node->site = site;
    node->parent = parent;
    // Appended at the end so siblings stay in the order they were first called,
    // which is the order the flame graph lays them out in:
    tomo_profile_node_t **end = &parent->children;
    while (*end)
        end = &(*end)->next_sibling;
    *end = node;
    return node;
}

public
void tomo_profile_begin(tomo_profile_frame_t *frame, tomo_profile_site_t *site) {
    tomo_profile_node_t *parent = current_frame ? current_frame->node : &call_tree_root;
    int depth = current_frame ? current_frame->depth + 1 : 0;
    // The memo turns the usual case, called again from the same place, into
    // one comparison; only a call from somewhere new walks the tree:
    tomo_profile_node_t *node;
    if (site->cached_parent == parent) {
        node = site->cached_node;
    } else {
        node = tree_node_for(parent, site, depth);
        site->cached_parent = parent;
        site->cached_node = node;
    }
    *frame = (tomo_profile_frame_t){.node = node, .parent = current_frame, .folded = (node == parent), .depth = depth};
    current_frame = frame;
    // Taken last, so none of the bookkeeping above lands inside the measurement:
    frame->start = ticks();
}

public
void tomo_profile_end(tomo_profile_frame_t *frame) {
    uint64_t elapsed = ticks() - frame->start;
    frame->node->calls += 1;
    // A folded frame's time is already inside the node it shares with its
    // caller; adding it again would count the same time twice. Everything else
    // the report shows (self time, per-function totals, call counts) is
    // summed out of the tree once, at exit, rather than per call.
    if (!frame->folded) frame->node->total_ticks += elapsed;
    current_frame = frame->parent;
}

// Whether an environment variable is set to something that means "off".
static bool is_off(const char *value) {
    return value != NULL
           && (strcmp(value, "0") == 0 || strcmp(value, "no") == 0 || strcmp(value, "false") == 0
               || strcmp(value, "") == 0);
}

public
void tomo_profile_mark_start(void) {
    if (profiling_started > 0.0) return; // already stamped
    use_cycle_counter = cycle_counter_is_usable();
    profiling_started = now();
    ticks_started = ticks();
}

public
void tomo_profile_enable(void) {
    tomo_profile_mark_start();
    // Where the two outputs go. Both are read here rather than at exit so that a
    // program which changes its own environment can't move them mid-run:
    const char *file = getenv("PROFILE_FILE");
    if (file && file[0] != '\0') report_path = file;
    const char *flame = getenv("FLAME_GRAPH");
    if (flame && flame[0] != '\0') flame_path = flame;
    tomo_profiling_enabled = true;
    atexit(tomo_profile_report);
}

public
void tomo_profile_start(void) {
    // Instrumented programs profile every run; PROFILE=0 is the way to run one
    // without paying for (or printing) a profile:
    if (is_off(getenv("PROFILE"))) return;
    tomo_profile_enable();
}

public
void tomo_profile_reset(void) {
    call_tree_root = (tomo_profile_node_t){};
    current_frame = NULL;
    sites = NULL;
    tree_nodes = 0;
}

// One line per entry: "<ticks> <calls> <name>". The name goes last so it may
// contain spaces, which the counts never do.
public
void tomo_profile_serialize(int fd) {
    if (!tomo_profiling_enabled) return;
    // Nothing else runs in this process afterwards, so summing the tree into
    // the sites here (rather than at exit, which never comes for a forked
    // child) is safe:
    aggregate_sites(&call_tree_root);
    char buf[512];
    for (tomo_profile_site_t *site = sites; site; site = site->next) {
        int n = snprintf(buf, sizeof(buf), "%llu %lld %s\n", (unsigned long long)site->total_ticks,
                         (long long)site->calls, site->name);
        if (n > 0) (void)!write(fd, buf, (size_t)n);
    }
}

public
void tomo_profile_merge(int fd) {
    if (!tomo_profiling_enabled) {
        close(fd);
        return;
    }
    FILE *f = fdopen(fd, "r");
    if (!f) return;
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        unsigned long long total;
        long long calls;
        int offset = 0;
        if (sscanf(line, "%llu %lld %n", &total, &calls, &offset) < 2) continue;
        char *name = line + offset;
        size_t length = strlen(name);
        if (length > 0 && name[length - 1] == '\n') name[length - 1] = '\0';

        // The work happened in another process, so it has no place in this
        // one's call tree: it becomes a top-level entry, which is where a
        // forked child's work belongs anyway. The name lives in a stack buffer
        // here, so both it and its site have to be copied to outlive the loop.
        tomo_profile_node_t *node = NULL;
        for (tomo_profile_node_t *child = call_tree_root.children; child; child = child->next_sibling)
            if (streq(child->site->name, name)) node = child;
        if (node == NULL) {
            tomo_profile_site_t *site = calloc(1, sizeof(tomo_profile_site_t));
            char *copy = calloc(length + 1, 1);
            if (!site || !copy) break;
            memcpy(copy, name, length);
            site->name = copy;
            node = tree_node_for(&call_tree_root, site, 0);
            if (node == &call_tree_root) break; // out of nodes
        }
        node->total_ticks += (uint64_t)total;
        node->calls += (int64_t)calls;
    }
    fclose(f); // also closes fd
}

// The site's file, shortened to a path relative to the working directory when
// it is under it, since the absolute paths the compiler bakes in are long and
// the leading directories are the same for every line.
static const char *short_path(const char *path) {
    if (path == NULL) return NULL;
    static char cwd[4096] = "";
    static bool got_cwd = false;
    if (!got_cwd) {
        got_cwd = true;
        if (!getcwd(cwd, sizeof(cwd))) cwd[0] = '\0';
    }
    size_t len = strlen(cwd);
    if (len > 0 && strncmp(path, cwd, len) == 0 && path[len] == '/') return path + len + 1;
    return path;
}

// A duration in whichever unit keeps it readable: these span
// nanoseconds (a one-line getter called a million times) to seconds (a function
// called once), and a single fixed unit makes one end or the other unreadable.
static const char *format_time(double seconds, char *buf, size_t size) {
    if (seconds < 1e-6) snprintf(buf, size, "%.0fns", 1e9 * seconds);
    else if (seconds < 1e-3) snprintf(buf, size, "%.2fus", 1e6 * seconds);
    else if (seconds < 1.0) snprintf(buf, size, "%.2fms", 1e3 * seconds);
    else snprintf(buf, size, "%.2fs", seconds);
    return buf;
}

static int by_self_time(const void *a, const void *b) {
    uint64_t self_a = (*(tomo_profile_site_t *const *)a)->self_ticks,
             self_b = (*(tomo_profile_site_t *const *)b)->self_ticks;
    if (self_a < self_b) return 1;
    if (self_a > self_b) return -1;
    return 0;
}

// How many characters `text` is, as opposed to how many bytes: Tomo identifiers
// can be non-ASCII, and it is characters that have to be counted against the
// room a label has.
static int utf8_length(const char *text) {
    int length = 0;
    for (const char *c = text; *c; c++)
        if ((*c & 0xC0) != 0x80) length += 1;
    return length;
}

// Copy as much of `src` as fits in `max_chars` characters, never splitting a
// UTF-8 sequence in half.
static void copy_truncated(char *dst, size_t dst_size, const char *src, int max_chars) {
    size_t used = 0;
    int cols = 0;
    for (const char *c = src; *c && cols < max_chars; c++) {
        size_t len = 1;
        while (c[len] && (c[len] & 0xC0) == 0x80)
            len += 1;
        if (used + len + 1 > dst_size) break;
        memcpy(dst + used, c, len);
        used += len;
        cols += 1;
        c += len - 1;
    }
    dst[used] = '\0';
}

// --- Flame graph -------------------------------------------------------------
// The call tree drawn as an image, written when FLAME_GRAPH names a path: one
// box per frame, as wide as its share of the run and stacked on the
// frame that called it. Where the table answers "which function is slow", this
// answers "reached from where", and a canvas has the resolution for it: every
// frame gets a box no matter how thin, carrying its details in a tooltip.

#define SVG_WIDTH 1200.0
#define SVG_ROW_HEIGHT 18.0
#define SVG_MARGIN 12.0
#define SVG_MIN_LABEL 40.0 // narrower boxes than this get no text

// `<` and `&` can't appear in a Tomo identifier, but a file path is under the
// user's control, so escape both anyway rather than emitting broken XML.
static void svg_escape(FILE *out, const char *text) {
    for (const char *c = text; *c; c++) {
        if (*c == '<') fputs("&lt;", out);
        else if (*c == '>') fputs("&gt;", out);
        else if (*c == '&') fputs("&amp;", out);
        else if (*c == '"') fputs("&quot;", out);
        else fputc(*c, out);
    }
}

// The classic flame-graph palette: a warm hue picked from the function's name,
// so the same function keeps its color across runs and neighbouring boxes are
// easy to tell apart.
static void svg_color(const char *name, char *buf, size_t size) {
    uint32_t hash = 2166136261u;
    for (const char *c = name; *c; c++)
        hash = (hash ^ (uint8_t)*c) * 16777619u;
    int red = 205 + (int)(hash % 50);
    int green = 50 + (int)((hash >> 8) % 160);
    int blue = 20 + (int)((hash >> 16) % 45);
    snprintf(buf, size, "#%02x%02x%02x", red, green, blue);
}

static int svg_depth(tomo_profile_node_t *node) {
    int deepest = 0;
    for (tomo_profile_node_t *child = node->children; child; child = child->next_sibling) {
        int depth = 1 + svg_depth(child);
        if (depth > deepest) deepest = depth;
    }
    return deepest;
}

// Frames too narrow to be worth a rect (and their subtrees), counted so the
// image can say so rather than quietly leaving them out.
static int64_t svg_skipped = 0;

static void svg_count_subtree(tomo_profile_node_t *node) {
    for (tomo_profile_node_t *child = node->children; child; child = child->next_sibling) {
        svg_skipped += 1;
        svg_count_subtree(child);
    }
}

static void svg_write_children(FILE *out, tomo_profile_node_t *parent, int row, double x, double width, double total,
                               double root_total, double height) {
    if (width <= 0.0 || total <= 0.0) return;
    double pos = x;
    for (tomo_profile_node_t *child = parent->children; child; child = child->next_sibling) {
        double w = width * (tick_seconds(child->total_ticks) / total);
        if (w < 0.15) { // thinner than a hairline: not drawable, not hoverable
            svg_skipped += 1;
            svg_count_subtree(child);
            continue;
        }
        double y = height - SVG_MARGIN - (double)(row + 1) * SVG_ROW_HEIGHT;
        char color[16], time[32];
        svg_color(child->site->name, color, sizeof(color));
        format_time(tick_seconds(child->total_ticks), time, sizeof(time));
        double pct = root_total > 0.0 ? 100.0 * tick_seconds(child->total_ticks) / root_total : 0.0;

        fputs("<g>", out);
        fputs("<title>", out);
        svg_escape(out, child->site->name);
        fprintf(out, " &#8212; %s, %lld call%s, %.2f%%", time, (long long)child->calls, child->calls == 1 ? "" : "s",
                pct);
        if (child->site->file) {
            fputs(" (", out);
            svg_escape(out, short_path(child->site->file));
            fprintf(out, ":%lld)", (long long)child->site->line);
        }
        fputs("</title>", out);
        // The 1px gap between neighbours only makes sense once a box is wide
        // enough to still be visible without it:
        fprintf(out, "<rect x=\"%.2f\" y=\"%.2f\" width=\"%.2f\" height=\"%.2f\" fill=\"%s\"/>", pos, y,
                w > 1.5 ? w - 1.0 : w, SVG_ROW_HEIGHT - 1.0, color);
        if (w >= SVG_MIN_LABEL) {
            // ~6.2px per character at this font size, minus a little padding:
            int fits = (int)((w - 8.0) / 6.2);
            char label[512];
            snprintf(label, sizeof(label), "%s (%s)", child->site->name, time);
            if (utf8_length(label) > fits) copy_truncated(label, sizeof(label), child->site->name, fits);
            if (utf8_length(label) <= fits) {
                fprintf(out, "<text x=\"%.2f\" y=\"%.2f\">", pos + 4.0, y + SVG_ROW_HEIGHT - 6.0);
                svg_escape(out, label);
                fputs("</text>", out);
            }
        }
        fputs("</g>\n", out);

        svg_write_children(out, child, row + 1, pos, w, tick_seconds(child->total_ticks), root_total, height);
        pos += w;
    }
}

static void write_svg_flame(FILE *out, double root_total) {
    int rows = svg_depth(&call_tree_root);
    double height = 2.0 * SVG_MARGIN + (double)rows * SVG_ROW_HEIGHT + 24.0;
    char total[32];
    format_time(root_total, total, sizeof(total));
    fprintf(out,
            "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%.0f\" height=\"%.0f\" "
            "viewBox=\"0 0 %.0f %.0f\">\n"
            "<style>text{font:11px ui-monospace,SFMono-Regular,Menlo,monospace;fill:#fff;"
            "pointer-events:none}rect{stroke:#00000022}g:hover rect{stroke:#fff;stroke-width:1}</style>\n"
            "<rect width=\"100%%\" height=\"100%%\" fill=\"#1b1b1b\"/>\n"
            "<text x=\"%.0f\" y=\"%.0f\" style=\"font-size:13px;fill:#ddd\">flame graph &#8212; %s total "
            "(hover a frame for details)</text>\n",
            SVG_WIDTH, height, SVG_WIDTH, height, SVG_MARGIN, SVG_MARGIN + 10.0, total);
    svg_skipped = 0;
    svg_write_children(out, &call_tree_root, 0, SVG_MARGIN, SVG_WIDTH - 2.0 * SVG_MARGIN, root_total, root_total,
                       height);
    if (svg_skipped > 0)
        fprintf(out, "<text x=\"%.0f\" y=\"%.0f\" style=\"fill:#888\">%lld frame%s too narrow to draw</text>\n",
                SVG_WIDTH - SVG_MARGIN - 220.0, SVG_MARGIN + 10.0, (long long)svg_skipped, svg_skipped == 1 ? "" : "s");
    fputs("</svg>\n", out);
}

// Sum the call tree into the per-function totals the table shows. Doing it
// here rather than per call is what keeps tomo_profile_end() down to a few
// stores: a million calls cost a million increments, while this costs one walk
// over the distinct call paths.
static void aggregate_sites(tomo_profile_node_t *parent) {
    for (tomo_profile_node_t *node = parent->children; node; node = node->next_sibling) {
        tomo_profile_site_t *site = node->site;
        if (!site->registered) {
            site->registered = true;
            site->next = sites;
            sites = site;
        }
        site->calls += node->calls;

        // Self time is what this path spent outside the callees it reached:
        uint64_t callees = 0;
        for (tomo_profile_node_t *child = node->children; child; child = child->next_sibling)
            callees += child->total_ticks;
        site->self_ticks += node->total_ticks > callees ? node->total_ticks - callees : 0;

        // Inclusive time is only counted where the function isn't already
        // somewhere above it on the stack: an outer call's time already covers
        // the inner ones, so counting both would count it twice. (Calling
        // itself directly doesn't even reach here, since those frames fold
        // into the one node, but a mutually recursive cycle does.)
        bool nested_in_itself = false;
        for (tomo_profile_node_t *ancestor = node->parent; ancestor; ancestor = ancestor->parent)
            if (ancestor->site == site) {
                nested_in_itself = true;
                break;
            }
        if (!nested_in_itself) site->total_ticks += node->total_ticks;

        aggregate_sites(node);
    }
}

public
void tomo_profile_report(void) {
    if (!tomo_profiling_enabled || reported) return;
    reported = true;
    double elapsed = now() - profiling_started;
    // Calibrate: the counter ran `ticks` times over `elapsed` seconds of real
    // time. A run too short to measure keeps the nanosecond default, which is
    // what the fallback path counts in anyway.
    uint64_t ticks_elapsed = ticks() - ticks_started;
    if (ticks_elapsed > 0 && elapsed > 0.0) seconds_per_tick = elapsed / (double)ticks_elapsed;

    // The program can exit from inside a call, whether `fail()`, an explicit
    // exit, or a fatal signal, leaving frames on the stack whose timers never
    // stopped. Stop them now, so the functions the program died inside are
    // reported with the time they had used, instead of showing up with zero
    // calls. This has to happen before the totals are summed, since it is what
    // puts those frames' time into the tree:
    while (current_frame)
        tomo_profile_end(current_frame);

    aggregate_sites(&call_tree_root);

    // Nothing to report: the program exited before any instrumented function
    // ran (`--help`, a rejected argument). Instrumented programs profile every
    // run, so those runs would otherwise each print an empty report and,
    // with PROFILE_FILE or FLAME_GRAPH set, write an empty file.
    if (sites == NULL) return;

    // The root of the call tree accounts for every outermost call, which is
    // what the flame graph's full width represents:
    double root_total = 0.0;
    for (tomo_profile_node_t *child = call_tree_root.children; child; child = child->next_sibling)
        root_total += tick_seconds(child->total_ticks);

    // FLAME_GRAPH asks for the graph as well as the table, so both come out of
    // one run. A line on stderr says where it went, since a program that seems
    // to print nothing has otherwise quietly written a file:
    if (flame_path) {
        bool to_stdout = streq(flame_path, "-");
        FILE *svg = to_stdout ? stdout : fopen(flame_path, "w");
        if (svg) {
            write_svg_flame(svg, root_total);
            if (to_stdout) fflush(svg);
            else {
                fclose(svg);
                fprint(stderr, USE_COLOR ? "\x1b[2m" : "", "Wrote flame graph to ", flame_path,
                       USE_COLOR ? "\x1b[m" : "");
            }
        } else {
            fprint(stderr, "\x1b[31;1mCouldn't open the flame graph file: ", flame_path, "\x1b[m");
        }
    }

    FILE *out = stderr;
    bool close_out = false;
    if (report_path && strcmp(report_path, "-") == 0) {
        out = stdout;
    } else if (report_path) {
        out = fopen(report_path, "w");
        if (!out) {
            fprint(stderr, "\x1b[31;1mCouldn't open the profile file: ", report_path, "\x1b[m");
            return;
        }
        close_out = true;
    }
    // Color only makes sense on a terminal we already decided to colorize:
    bool color = USE_COLOR && !close_out;
    const char *dim = color ? "\x1b[2m" : "", *bold = color ? "\x1b[1m" : "", *reset = color ? "\x1b[m" : "";

    size_t count = 0;
    for (tomo_profile_site_t *site = sites; site; site = site->next)
        count += 1;
    tomo_profile_site_t **sorted = calloc(count, sizeof(tomo_profile_site_t *));
    if (!sorted) {
        if (close_out) fclose(out);
        return;
    }
    size_t i = 0;
    double instrumented = 0.0;
    int64_t total_calls = 0;
    for (tomo_profile_site_t *site = sites; site; site = site->next) {
        sorted[i++] = site;
        instrumented += tick_seconds(site->self_ticks);
        total_calls += site->calls;
    }
    qsort(sorted, count, sizeof(sorted[0]), by_self_time);

    fprintf(out, "%s───── profile ─────%s\n", dim, reset);
    // Column widths match the row format below: the value fields print
    // their own units, so the widths are 8/9/9/10/10 plus one space each:
    fprintf(out, "%s%8s %9s %9s %10s %10s  %s%s\n", bold, "self%", "self", "total", "calls", "avg self",
            tomo_profile_noun, reset);
    for (i = 0; i < count; i++) {
        tomo_profile_site_t *site = sorted[i];
        double self = tick_seconds(site->self_ticks);
        double pct = elapsed > 0.0 ? 100.0 * self / elapsed : 0.0;
        char avg[32];
        double avg_seconds = site->calls > 0 ? self / (double)site->calls : 0.0;
        fprintf(out, "%7.2f%% %7.2fms %7.2fms %10lld %10s  %s", pct, 1e3 * self, 1e3 * tick_seconds(site->total_ticks),
                (long long)site->calls, format_time(avg_seconds, avg, sizeof(avg)), site->name);
        // Entries merged from another process (see tomo_profile_merge) carry a
        // name but no source location:
        if (site->file) fprintf(out, " %s(%s:%lld)%s", dim, short_path(site->file), (long long)site->line, reset);
        fputc('\n', out);
    }
    fprintf(out, "%s───────────────────%s\n", dim, reset);
    fprintf(out, "%s%d %s%s, %lld call%s, %.2fms in instrumented code, %.2fms total runtime%s\n", dim, (int)count,
            tomo_profile_noun, count == 1 ? "" : "s", (long long)total_calls, total_calls == 1 ? "" : "s",
            1e3 * instrumented, 1e3 * elapsed, reset);
    free(sorted);
    if (close_out) fclose(out);
    else fflush(out);
}
