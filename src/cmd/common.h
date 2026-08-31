// Shared state and helpers for the tomo CLI commands

#pragma once

#include <gc.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include "../stdlib/bool.h" // IWYU pragma: export
#include "../stdlib/datatypes.h"
#include "../stdlib/print.h"
#include "../stdlib/text.h"

// Logging categories. Each command turns on a default set (see
// set_default_logs) and its --verbose flag turns them all on. Values are
// powers of two so `enabled_logs` can hold any combination.
typedef enum {
    LOG_BUILD = 1 << 0, // compiler progress: transpiled/compiled/installed steps
    LOG_SKIP = 1 << 1, // "Unchanged: ..." notices for up-to-date artifacts
    LOG_COMMANDS = 1 << 2, // the external toolchain commands being run
} logtype_t;

// Bitmask of the log categories that should currently print (0 = silent,
// ~0 = everything). Set per-command via set_default_logs():
extern uint32_t enabled_logs;

// The "[build] "/"[cmd] "/... tag printed in front of a category's messages:
const char *log_prefix(logtype_t type);

// Print a log message (with its category's prefix) if that category is
// enabled. Args are the usual print()/String() varargs:
#define LOG(type, ...) (((type) & enabled_logs) ? print(log_prefix(type), __VA_ARGS__) : 0)

// Set enabled_logs to a command's default, then apply its --verbose (turn
// everything on) or --quiet (turn everything off) flag. Called at the top of
// each command handler before it does any logging:
void set_default_logs(uint32_t default_logs);

// Reusable spec entries for a command's --verbose (and, for commands with a
// nonzero default, --quiet) flag; drop them into a command's spec array:
#define VERBOSE_FLAG {"verbose", &verbose, &Bool$info, .short_flag = 'v', .description = "print verbose logs"}
#define QUIET_FLAG {"quiet", &quiet, &Bool$info, .short_flag = 'q', .description = "suppress logs"}
// Per-command instrumentation flag: compile the program with function-level
// profiling, which it then prints at exit (see src/stdlib/profiling.h). It
// changes the generated code, so it is part of config_summary and toggling it
// rebuilds:
#define INSTRUMENT_FLAG                                                                                                \
    {"instrument", &instrument, &Bool$info,                                                                            \
     .description = "compile with profiling instrumentation (report printed at exit)"}
// Per-command debug flag: compile the program so a debugger can make sense of
// it: every variable gets a companion holding its TypeInfo (so gdb can print
// Tomo values with Tomo's own formatter) and `breakpoint()` calls are kept.
// Like --instrument, it changes the generated code, so it is part of
// config_summary and toggling it rebuilds. `tomo run --debug` additionally
// runs the program under gdb; see src/cmd/run.c.
#define DEBUG_FLAG                                                                                                     \
    {"debug", &debugging, &Bool$info, .short_flag = 'd',                                                               \
     .description = "compile for debugging (and, for `run`, launch it under a debugger)"}
// Per-command optimization flag. Its dest (opt_flag) stays NONE when unset, so
// each command can fall back to its own default (fast for run/eval, high for
// build) via configure_codegen():
#define OPTIMIZATION_FLAG                                                                                              \
    {"optimization",    &opt_flag,          &Text$info,                                                                \
     .short_flag = 'O', .metavar = "level", .description = "set the optimization level (0-3)"}

#define run_cmd(...)                                                                                                   \
    ({                                                                                                                 \
        const char *_cmd = String(__VA_ARGS__);                                                                        \
        LOG(LOG_COMMANDS, "\033[94;1m", _cmd, "\033[m");                                                               \
        popen(_cmd, "w");                                                                                              \
    })
#define command_output(...)                                                                                            \
    ({                                                                                                                 \
        const char *_cmd = String(__VA_ARGS__);                                                                        \
        LOG(LOG_COMMANDS, "\033[94;1m", _cmd, "\033[m");                                                               \
        FILE *_prog = popen(_cmd, "r");                                                                                \
        char *_output = GC_MALLOC_ATOMIC(1024);                                                                        \
        fgets(_output, 1023, _prog);                                                                                   \
        if (_output[strlen(_output) - 1] == '\n') _output[strlen(_output) - 1] = '\0';                                 \
        int status = pclose(_prog);                                                                                    \
        (!WIFEXITED(status) || WEXITSTATUS(status) != 0) ? NULL : _output;                                             \
    })
#define xsystem(...)                                                                                                   \
    ({                                                                                                                 \
        int _status = system(String(__VA_ARGS__));                                                                     \
        if (!WIFEXITED(_status) || WEXITSTATUS(_status) != 0)                                                          \
            errx(1, "Failed to run command: %s", String(__VA_ARGS__));                                                 \
    })
#define list_text(list) Text$join(Text(" "), list)

#define whisper(...) print("\033[2m", __VA_ARGS__, "\033[m")

// Configuration shared by every command. clean_build/source_mapping/
// install_target come from global CLI flags; verbose/quiet are the dests of
// each command's own --verbose/--quiet flags (see VERBOSE_FLAG/QUIET_FLAG and
// set_default_logs), no longer global flags:
extern OptionalBool_t verbose, quiet, clean_build, source_mapping, install_target,
    // The dest of the global --profile flag: time the compiler's own phases
    // (see tomo_profile_enable() in stdlib/profiling.h):
    profiling,
    // The dest of each compiling command's --instrument flag (see INSTRUMENT_FLAG):
    instrument,
    // The dest of each compiling command's --debug flag (see DEBUG_FLAG):
    debugging;

// Whether ZIG_GLOBAL_CACHE_DIR came from the user's environment (true) or was
// set by main() to point the bundled zig at Tomo's own cache directory
// (false). In the latter case it must not leak into programs `tomo run`
// execs, where it would misdirect a user-run zig's cache:
extern bool zig_cache_dir_from_env;

// Cross-compilation state, set up in main() when --target names a platform
// other than the one this Tomo build is for:
extern OptionalText_t target;
extern bool cross_compiling;
// The directory holding the target platform's lib/ + include/ trees (extracted
// from that platform's Tomo distribution archive). Lives in the user's XDG data
// directory rather than TOMO_PATH so installing one never needs root:
extern Text_t target_root;
// The prefix whose lib/ + include/ user programs compile and link against
// (TOMO_PATH normally, target_root when cross-compiling):
extern Text_t lib_root;
// Whether the platform being compiled for uses Mach-O linking (macOS):
extern bool link_macho;
// The staged debug-stripped copy of zig's C runtime that executables link
// -nostdlib against, or empty to let zig supply its own (unstripped) copy:
extern Text_t zig_libc_dir;

extern OptionalText_t cflags, ldlibs, ldflags,
    // The effective `-O` level compiled with (always a concrete value like
    // "1"). Set via configure_codegen(); see optimization vs opt_flag below.
    optimization,
    // The dest of each compiling command's --optimization/-O flag. NONE until
    // the user passes -O, letting the command pick its own default level.
    opt_flag,
    // The toolchain is not configurable; these are set in main() to the
    // `zig cc`/`zig ar` bundled inside the Tomo installation:
    cc, ar;

// Extra link-time flags that shrink the binary at the cost of link speed
// (dead-code stripping + debug-section compression). configure_codegen()
// enables them for optimized builds and clears them for the fast run/eval
// path, where the executable is ephemeral so its size doesn't matter.
extern Text_t link_optimizations;

// Set the optimization level (`-O<opt_level>`) and, when `optimize` is true,
// the size-reducing link flags; then rebuild config_summary. Called once per
// command before compiling: after_globals() applies a safe default, and the
// run/eval/build handlers override it with their own level + speed tradeoff.
void configure_codegen(Text_t opt_level, bool optimize);

// Rebuild config_summary from the current flags. configure_codegen() does this
// itself; commands that don't call it (`tomo transpile`) call this after their
// own flags are parsed, so a config-affecting flag like --instrument still
// invalidates artifacts built without it.
void update_config_summary(void);

extern Text_t config_summary;

#ifdef __linux__
// The file modification time of the compiler itself, so files can be
// recompiled after the compiler changes (only on Linux is /proc/self/exe
// available); set up in main():
extern struct stat compiler_stat;
#endif

const char *paths_str(List_t paths);
Path_t xdg_tomo_dir(const char *env_var, const char *fallback);
const char *platform_os(const char *platform);
const char *platform_triple(const char *platform);
bool platform_supported(const char *platform);
void ensure_target_installed(void);
List_t normalize_tm_paths(List_t paths);
void wait_for_child_success(pid_t child);
// Abort with a "re-run with more permissions" message if this user can't write
// into `prefix` (checked at its nearest existing ancestor, so a prefix that
// doesn't exist yet is fine as long as it can be created). Tomo never elevates
// itself (no sudo/doas); the user re-runs the command with the permissions they
// need.
void require_writable_prefix(const char *prefix);
Path_t get_exe_path(Path_t path);
Path_t build_file(Path_t path, const char *extension);
