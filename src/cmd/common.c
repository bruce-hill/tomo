// Shared state and helpers for the tomo CLI commands

#include <err.h>
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../ast.h"
#include "../config.h"
#include "../naming.h"
#include "../parse/files.h"
#include "../stdlib/fail.h"
#include "../stdlib/list.h"
#include "../stdlib/optionals.h"
#include "../stdlib/path.h"
#include "../stdlib/print.h"
#include "common.h"

OptionalBool_t verbose = false, quiet = false, clean_build = false, source_mapping = true, install_target = false,
               instrument = false, profiling = false, debugging = false;
bool zig_cache_dir_from_env = false, cross_compiling = false, link_macho = false;
uint32_t enabled_logs = 0;
Text_t target_root = Text(""), lib_root = Text(""), zig_libc_dir = Text(""), cc = Text(""), ar = Text(""),
       optimization = Text("2"), link_optimizations = Text(""), config_summary = Text(""), ldlibs = Text("-lm"),
       ldflags = Text("");
OptionalText_t cflags = Text("-Werror -fdollars-in-identifiers -std=gnu23 -Wno-trigraphs"
                             " -ffunction-sections -fdata-sections"
                             " -fno-signed-zeros"
                             " -fPIC -ggdb"
                             " -DGC_THREADS");
// optimization gets a concrete value from configure_codegen();
// opt_flag stays NONE until the user passes -O:
OptionalText_t opt_flag = NONE_TEXT, target = NONE_TEXT;

#ifdef __linux__
struct stat compiler_stat;
#endif

const char *log_prefix(logtype_t type) {
    switch (type) {
    case LOG_BUILD: return "\033[2m[build]\033[m ";
    case LOG_SKIP: return "\033[2m[skip]\033[m ";
    case LOG_COMMANDS: return "\033[2m[cmd]\033[m ";
    default: return "";
    }
}

void set_default_logs(uint32_t default_logs) {
    enabled_logs = default_logs;
    if (verbose) enabled_logs = ~0u;
    else if (quiet) enabled_logs = 0;
}

void configure_codegen(Text_t opt_level, bool optimize) {
    // A debug build has to be one a debugger can follow: optimized code
    // reorders and folds away the statements the user wants to step through,
    // and without `#line` directives a debugger has no .tm source to show at
    // all. An explicit -O still wins, since sometimes a bug only shows up
    // optimized, but the default becomes -O0 and source mapping is forced
    // on either way.
    if (debugging) {
        if (opt_flag.tag == TEXT_NONE) opt_level = Text("0");
        source_mapping = true;
        // Turns `breakpoint()` from nothing into a call the debugger stops on
        // (see stdlib/debugger.h). It is part of cflags, so it is also part of
        // config_summary and of the precompiled header's fingerprint --
        // toggling --debug rebuilds rather than reusing non-debug objects.
        cflags = Texts(cflags, " -DTOMO_DEBUG_BUILD");
    }
    optimization = opt_level;
    // Debug (-O0) builds get UBSan in trap mode: generated code is kept free
    // of undefined behavior (lambdas take their userdata as `void *` to match
    // the closure calling convention, function-to-closure promotion goes
    // through typed shims, and the tagged small-int fast paths shift through
    // uint64_t), so violations, including in user-written `C_code`, trap
    // with an ILLEGAL INSTRUCTION crash report instead of misbehaving
    // silently. Trap mode needs no UBSan runtime library, which the -nostdlib
    // link couldn't provide anyway. Optimized builds skip the instrumentation
    // for performance:
    if (Text$equal_values(opt_level, Text("0")))
        cflags = Texts(cflags, " -fsanitize=undefined -fsanitize-trap=undefined");
    else cflags = Texts(cflags, " -fno-sanitize=undefined");
    // Dead-code stripping (--gc-sections / -dead_strip) and debug-section
    // compression (zstd) make smaller binaries but slow linking noticeably.
    // Enable them only for optimized (build/install) artifacts; the fast
    // run/eval path skips them since its binary is thrown away after one run.
    if (optimize) {
        // Debug builds skip the zstd debug-section compression: the vendored
        // libbacktrace decompresses it natively for runtime stacktraces, but
        // an external debugger only reads those sections if its own build has
        // zstd support, so a debug build keeps its DWARF uncompressed.
        link_optimizations = link_macho  ? Text(" -Wl,-w,-dead_strip")
                             : debugging ? Text(" -Wl,--gc-sections")
                                         : Text(" -Wl,--gc-sections -Wl,--compress-debug-sections=zstd");
    } else {
        link_optimizations = Text("");
    }
    update_config_summary();
}

void update_config_summary(void) {
    config_summary = Texts("TOMO_VERSION=", TOMO_VERSION, "\n", "COMPILER=", cc, " ", cflags, " -O", optimization, "\n",
                           "SOURCE_MAPPING=", source_mapping ? Text("yes") : Text("no"), "\n",
                           "INSTRUMENTED=", instrument ? Text("yes") : Text("no"), "\n",
                           "DEBUGGING=", debugging ? Text("yes") : Text("no"), "\n");
}

const char *paths_str(List_t paths) {
    Text_t result = EMPTY_TEXT;
    for (int64_t i = 0; i < (int64_t)paths.length; i++) {
        if (i > 0) result = Texts(result, Text(" "));
        result = Texts(result, Path$as_text((Path_t *)(paths.data + i * paths.stride), false, &Path$info));
    }
    return Text$as_c_string(result);
}

// An XDG base directory (e.g. XDG_CACHE_HOME, falling back to ~/.cache),
// with "/tomo" appended:
Path_t xdg_tomo_dir(const char *env_var, const char *fallback) {
    const char *base = getenv(env_var);
    return (base && base[0] == '/') ? Path$from_str(String(base, "/tomo"))
                                    : Path$expand_home(Path$from_str(String(fallback, "/tomo")));
}

// The OS component of a platform key like "x86_64-linux" (the part after the
// last dash; architectures never contain dashes):

const char *platform_os(const char *platform) {
    const char *dash = strrchr(platform, '-');
    return dash ? dash + 1 : platform;
}

// The `zig cc -target` triple for a platform key: Linux targets musl; every
// other OS's platform key is already a valid Zig target:

const char *platform_triple(const char *platform) {
    if (streq(platform_os(platform), "linux")) return String(platform, "-musl");
    return platform;
}

bool platform_supported(const char *platform) {
    return strstr(" " TOMO_DIST_PLATFORMS " ", String(" ", platform, " ")) != NULL;
}

// Cross-compiling needs the *target* platform's libtomo, vendored libraries,
// and headers. They come from the target platform's Tomo distribution archive,
// whose lib/ + include/ trees get extracted into target_root. If they're not
// installed yet, offer to download them (or just do it if --install-target).

void ensure_target_installed(void) {
    Path_t marker = Path$from_str(String(target_root, "/lib/tomo@", TOMO_VERSION, "/libtomo.a"));
    if (Path$is_file(marker, true)) return;

    const char *dist_url = getenv("TOMO_DIST_URL");
    if (!dist_url || dist_url[0] == '\0') dist_url = "https://tomo.bruce-hill.com/dist";
    Text_t archive_url = Texts(Text$from_str(dist_url), "/tomo@", TOMO_VERSION, "-", target, ".tar.xz");

    if (!install_target) {
        fprint(stderr, "The target platform \x1b[1m", target, "\x1b[m is not installed.");
        if (!isatty(STDIN_FILENO))
            print_err("Re-run with --install-target to download and install it from ", archive_url);
        fprint_inline(stderr, "Download and install it from ", archive_url, "? [Y/n] ");
        fflush(stderr);
        char answer[16] = {};
        if (!fgets(answer, sizeof(answer), stdin) || answer[0] == 'n' || answer[0] == 'N')
            print_err("Not installing the target platform ", target);
    }

    print("Installing target platform \x1b[1m", target, "\x1b[m from ", archive_url, " ...");
    Text_t tmp_archive = Texts(target_root, ".tar.xz.tmp");
    xsystem("mkdir -p '", target_root, "'");
    xsystem("curl -#fSL '", archive_url, "' -o '", tmp_archive, "'");
    xsystem("tar -xJf '", tmp_archive, "' -C '", target_root, "' ./lib ./include");
    xsystem("rm -f '", tmp_archive, "'");
    print("Installed target platform \x1b[1m", target, "\x1b[m");
}

List_t normalize_tm_paths(List_t paths) {
    List_t result = EMPTY_LIST;
    for (int64_t i = 0; i < (int64_t)paths.length; i++) {
        Path_t path = *(Path_t *)(paths.data + i * paths.stride);
        // Convert `foo` to `foo/foo.tm` and resolve path to absolute path:
        Path_t cur_dir = Path$current_dir();
        if (Path$is_directory(path, true)) path = Path$child(path, Texts(Path$base_name(path), Text(".tm")));

        path = Path$resolved(path, cur_dir);
        if (!Path$exists(path)) fail("path not found: ", path);
        List$insert(&result, &path, I(0), sizeof(path));
    }
    return result;
}

void wait_for_child_success(pid_t child) {
    int status;
    while (waitpid(child, &status, 0) < 0 && errno == EINTR) {
        if (WIFEXITED(status) || WIFSIGNALED(status)) break;
        else if (WIFSTOPPED(status)) kill(child, SIGCONT);
    }

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        _exit(WIFEXITED(status) ? WEXITSTATUS(status) : EXIT_FAILURE);
    }
}

void require_writable_prefix(const char *prefix) {
    // Check the nearest existing ancestor: if we can write there, we can create
    // and populate the prefix subtree (bin/, man/, ...) even if it doesn't exist
    // yet.
    Path_t p = Path$from_str(prefix);
    for (int i = 0; i < 64 && !Path$exists(p); i++) {
        OptionalPath_t parent = Path$parent(p);
        if (parent == NONE_PATH || strcmp(parent, p) == 0) break;
        p = parent;
    }
    if (access(Path$as_c_string(p), W_OK) != 0)
        print_err("You don't have permission to write to ", prefix,
                  "\nRe-run this command as the owner of that directory (for example with `sudo`).");
}

Path_t get_exe_path(Path_t path) {
    ast_t *ast = parse_file(Path$as_c_string(path), NULL);
    OptionalText_t exe_name = ast_metadata(ast, "EXECUTABLE");
    if (exe_name.tag == TEXT_NONE) exe_name = Path$base_name(Path$with_extension(path, Text(""), true));
    return Path$child(tm_build_dir(path), exe_name);
}

// Cross-compiled artifacts live in a per-target subdirectory
// (.tomo/<platform>/) so they never clobber native builds' artifacts:

Path_t build_file(Path_t path, const char *extension) {
    return Path$child(tm_build_dir(path), Texts(Path$base_name(path), Text$from_str(extension)));
}
