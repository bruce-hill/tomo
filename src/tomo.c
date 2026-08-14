// The main program for the tomo CLI: global flags, the command table, and
// dispatch (each command's logic lives in src/cmd/)

#include <err.h>
#include <gc.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#if defined(__linux__)
#include <sys/random.h>
#endif
#include <unistd.h>

#include "cmd/commands.h"
#include "cmd/common.h"
#include "config.h"
#include "naming.h"
#include "profile.h"
#include "stdlib/bools.h"
#include "stdlib/cli.h"
#include "stdlib/datatypes.h"
#include "stdlib/paths.h"
#include "stdlib/print.h"
#include "stdlib/siphash.h"
#include "stdlib/stdlib.h"
#include "stdlib/text.h"
#include "util.h"

cli_spec_t tomo_cli = {};

static cli_arg_t global_spec[] = {
    {"force-rebuild", &clean_build, &Bool$info, .short_flag = 'f', .description = "force rebuilding"}, //
    {"source-mapping", &source_mapping, &Bool$info, .short_flag = 'm',
     .description = "toggle source mapping in generated code"}, //
    {"target", &target, &Text$info, .metavar = "platform",
     .description = "cross-compile for another platform; one of: " TOMO_DIST_PLATFORMS}, //
    {"install-target", &install_target, &Bool$info,
     .description = "install the --target platform's libraries without asking"}, //
    {"profile", &profiling, &Bool$info,
     .description = "print a breakdown of where compile time is spent"}, //
};

static cli_command_t *commands[] = {
    &run_command,       &eval_command,           &build_command,     &transpile_command, &parse_command,
    &fmt_command,       &package_command,        &install_command,   &uninstall_command, &vendor_command,
    &info_command,      &version_command,        &uninstall_self_command, &test_command,
};

// Runs after the global flags are popped, before command dispatch: sets up
// the toolchain configuration every command compiles with.
static void after_globals(void) {
    // The compiler is always the bundled `zig cc` (a clang):
    cflags = Texts(cflags, Text(" -Wno-parentheses-equality"));

    Text_t owner = Path$owner(Path$from_str(TOMO_PATH), true);
    Text_t user = Text$from_str(getenv("USER"));
    if (!Text$equal_values(user, owner)) {
        as_owner = Texts(Text(SUDO " -u "), owner, Text(" "));
    }

    // Cross-compilation via --target: compile for another platform using the
    // bundled zig toolchain (which can target every supported platform) and the
    // target platform's libraries (installed on demand from its distribution
    // archive):
    if (install_target && target.length <= 0) print_err("--install-target requires --target <platform>");
    if (target.length > 0 && !Text$equal_values(target, Text(TOMO_PLATFORM))) {
        if (!platform_supported(Text$as_c_string(target)))
            print_err("Unsupported target platform: ", target, "\nSupported platforms: " TOMO_DIST_PLATFORMS);
        cross_compiling = true;
        build_target_platform = target;
        // Target platforms install into the user's XDG data directory (not
        // TOMO_PATH), so installing one never needs root permissions:
        const char *data_home = getenv("XDG_DATA_HOME");
        Path_t data_dir = (data_home && data_home[0] != '\0') ? Path$from_str(data_home)
                                                              : Path$expand_home(Path$from_str("~/.local/share"));
        target_root =
            Texts(Path$as_text(&data_dir, false, &Path$info), "/tomo/tomo@", TOMO_VERSION, "/targets/", target);
        ensure_target_installed();
    }

    // Compile against the headers and libraries of the platform being compiled
    // for: the target's when cross-compiling, this installation's otherwise.
    lib_root = cross_compiling ? target_root : Text$from_str(TOMO_PATH);
    cflags = Texts("-I'", lib_root, "/include/tomo@", TOMO_VERSION, "' -I'", lib_root, "/lib/tomo@", TOMO_VERSION,
                   "' ", cflags);
    if (cross_compiling) {
        // Point the system-header search env vars at the target's too:
        setenv("C_INCLUDE_PATH", String(lib_root, "/include/tomo@", TOMO_VERSION), 1);
        setenv("CPATH", String(lib_root, "/include/tomo@", TOMO_VERSION), 1);
        cflags = Texts("-target ", platform_triple(Text$as_c_string(target)), " ", cflags);
    } else if (ZIG_TARGET[0] != '\0') {
        // ZIG_TARGET (this build's own target triple) is baked in at compile time:
        cflags = Texts("-target ", ZIG_TARGET, " ", cflags);
    }

    ldflags = Texts(ldflags, Text(" -ffunction-sections -fdata-sections"));
    // Link flags depend on the OS being compiled for:
    const char *link_os = cross_compiling ? platform_os(Text$as_c_string(target)) : platform_os(TOMO_PLATFORM);
    // Linux/musl links fully statically. When a debug-stripped copy of zig's C
    // runtime was staged at build time (vendor/zig-libc/, made by
    // scripts/stage_zig_libc.sh), link -nostdlib against it: zig's own libc
    // and compiler-rt unconditionally carry megabytes of DWARF that tomo
    // stacktraces and gdb sessions never read. When the staged copy is
    // missing, fall back to zig's -- the only cost is bigger binaries.
    if (streq(link_os, "linux")) {
        ldflags = Texts(ldflags, Text(" -static"));
        Text_t staged = Texts(lib_root, "/lib/tomo@", TOMO_VERSION, "/vendor/zig-libc");
        if (Path$is_file(Path$from_str(String(staged, "/libc.a")), true)
            && Path$is_file(Path$from_str(String(staged, "/crt1.o")), true)) {
            zig_libc_dir = staged;
            // -nostdlib means supplying the C runtime ourselves: crt1.o here,
            // the libraries after every other archive (in compile_executable()).
            // zig also stops resolving -lm and -lunwind under -nostdlib (and
            // would re-add its own crt1.o if they appeared); musl's libm lives
            // inside libc.a and the staged libunwind.a covers -lunwind:
            ldflags = Texts(ldflags, " -nostdlib '", staged, "/crt1.o'");
            if (Text$equal_values(ldlibs, Text("-lm"))) ldlibs = Text("");
        }
    }
    // The stack unwinder used by libtomo's stacktrace code; zig provides it for
    // every supported target:
    if (zig_libc_dir.length == 0) ldlibs = Texts(ldlibs, Text(" -lunwind"));
    if (streq(link_os, "macos")) {
        link_macho = true;
        // -u _tomo_versions forces the versions.o member (the version info in
        // the __TEXT,__tomo_versions section) out of libtomo.a into every
        // executable. Dead-stripping (-w,-dead_strip) is applied per-command by
        // configure_codegen() via link_optimizations, since it slows linking.
        ldflags = Texts(ldflags, " -Wl,-U,build_info -Wl,-u,_tomo_versions");
    } else {
        // -u forces the build_info/versions sections into every executable.
        // Dead-code stripping (--gc-sections) and debug-section compression
        // (--compress-debug-sections=zstd) are applied per-command by
        // configure_codegen() via link_optimizations: the DWARF that powers
        // runtime stacktraces is most of a binary's file size, and the vendored
        // libbacktrace decompresses zstd sections natively so traces are
        // unaffected -- but both flags slow linking, so the fast run/eval path
        // skips them.
        ldflags = Texts(ldflags, " -Wl,-u,build_info -Wl,-u,tomo_versions");
    }

#ifdef __APPLE__
    if (!cross_compiling) {
        cflags = Texts(cflags, Text(" -I/opt/homebrew/include"));
        ldflags = Texts(ldflags, Text(" -L/opt/homebrew/lib -Wl,-rpath,/opt/homebrew/lib"));
    }
#endif

    // Establish a safe default codegen configuration (highest safe
    // optimization, size-reducing link flags on) for commands that don't set
    // their own; the run/eval/build handlers override it with configure_codegen.
    configure_codegen(Text("2"), /*optimize=*/true);
}

int main(int argc, char *argv[]) {
    GC_INIT();
    profile_mark_start();
    tomo_configure();

#ifdef __linux__
    // Get the file modification time of the compiler, so we
    // can recompile files after changing the compiler:
    char compiler_path[PATH_MAX];
    ssize_t count = readlink("/proc/self/exe", compiler_path, PATH_MAX);
    if (count == -1) err(1, "Could not find age of compiler");
    compiler_path[count] = '\0';
    if (stat(compiler_path, &compiler_stat) != 0) err(1, "Could not find age of compiler");
#endif

    const char *color_env = getenv("COLOR");
    USE_COLOR = color_env ? strcmp(color_env, "1") == 0 : isatty(STDOUT_FILENO);
    const char *no_color_env = getenv("NO_COLOR");
    if (no_color_env && no_color_env[0] != '\0') USE_COLOR = false;

#if defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || defined(__APPLE__)
    arc4random_buf(TOMO_HASH_KEY, sizeof(TOMO_HASH_KEY));
#elif defined(__linux__)
    assert(getrandom(TOMO_HASH_KEY, sizeof(TOMO_HASH_KEY), 0) == sizeof(TOMO_HASH_KEY));
#else
#error "Unsupported platform for secure random number generation"
#endif

    if (getenv("TOMO_PATH")) TOMO_PATH = getenv("TOMO_PATH");

    // Always use the Zig toolchain bundled inside the Tomo installation as the C
    // compiler/linker for user programs. It lives in a fixed location relative to
    // TOMO_PATH (see the Makefile's libexec layout) and is a native build for
    // this platform, invoked with `-target <ZIG_TARGET>` (plus `-static` on
    // Linux/musl) to produce statically-linked binaries.
    cc = Texts(Text$from_str(TOMO_PATH), "/libexec/tomo@", TOMO_VERSION, "/zig/zig cc");
    // zig's llvm-based ar, so no system binutils is needed at runtime:
    ar = Texts(Text$from_str(TOMO_PATH), "/libexec/tomo@", TOMO_VERSION, "/zig/zig ar");

    // Keep the bundled zig's global cache (its libc/compiler-rt builds, ~GBs)
    // inside Tomo's own cache directory rather than the zig default
    // (~/.cache/zig, which may belong to a zig the user runs themselves), so
    // `tomo uninstall-self` can clear it. An explicit ZIG_GLOBAL_CACHE_DIR in
    // the environment is respected (and remembered, so `tomo run` knows
    // whether to strip the variable before exec'ing the user's program):
    zig_cache_dir_from_env = getenv("ZIG_GLOBAL_CACHE_DIR") != NULL;
    setenv("ZIG_GLOBAL_CACHE_DIR", Path$child(xdg_tomo_dir("XDG_CACHE_HOME", "~/.cache"), Text("zig")), 0);

    // Nested tomo invocations (e.g. compiling packages) must run THIS tomo,
    // not whatever `tomo` is on PATH (which may be an older installed
    // version); record where we live (see tomo_exe() in packages.c):
    Path_t self = strchr(argv[0], '/') ? Path$resolved(Path$from_str(argv[0]), Path$current_dir())
                                       : Path$from_str(String(TOMO_PATH, "/bin/tomo"));
    setenv("TOMO_EXE", self, 1);

    // Set up environment variables:
    const char *PATH = getenv("PATH");
    setenv("PATH", PATH ? String(TOMO_PATH, "/bin:", PATH) : String(TOMO_PATH, "/bin"), 1);
    const char *include_dir = String(TOMO_PATH, "/include/tomo@", TOMO_VERSION);
    const char *C_INCLUDE_PATH = getenv("C_INCLUDE_PATH");
    setenv("C_INCLUDE_PATH", C_INCLUDE_PATH ? String(include_dir, ":", C_INCLUDE_PATH) : include_dir, 1);
    const char *CPATH = getenv("CPATH");
    setenv("CPATH", CPATH ? String(include_dir, ":", CPATH) : include_dir, 1);

    tomo_cli = (cli_spec_t){
        .name = "tomo",
        .summary = "a compiler for the Tomo programming language",
        .description = "Running \x1b[1mtomo file.tm\x1b[m without a command runs the file, and bare\n"
                       "\x1b[1mtomo\x1b[m opens a scratch file to edit and run.",
        .global_len = (int)(sizeof(global_spec) / sizeof(global_spec[0])),
        .global_spec = global_spec,
        .num_commands = (int)(sizeof(commands) / sizeof(commands[0])),
        .commands = commands,
        .after_globals = after_globals,
        // Bare `tomo file.tm` (and `tomo` with no file) shims to `tomo run`:
        .default_command = &run_command,
    };
    // Print the profile (if --profile was given) on any normal exit; the
    // run/eval paths that exec a program call profile_report() themselves right
    // before execv, since that replaces the process and skips atexit handlers.
    atexit(profile_report);
    return tomo_dispatch_command(argc, argv, &tomo_cli);
}
