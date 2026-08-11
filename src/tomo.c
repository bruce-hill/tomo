// The main program that runs compilation

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <gc.h>
#include <libgen.h>
#include <miniz.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#if defined(__linux__)
#include <sys/random.h>
#endif
#include <time.h>

#include "ast.h"
#include "compile/cli.h"
#include "compile/files.h"
#include "compile/headers.h"
#include "config.h"
#include "formatter/formatter.h"
#include "naming.h"
#include "packages.h"
#include "parse/files.h"
#include "stdlib/bools.h"
#include "stdlib/bytes.h"
#include "stdlib/c_strings.h"
#include "stdlib/cli.h"
#include "stdlib/datatypes.h"
#include "stdlib/enums.h"
#include "stdlib/lists.h"
#include "stdlib/optionals.h"
#include "stdlib/paths.h"
#include "stdlib/print.h"
#include "stdlib/random.h"
#include "stdlib/simpleparse.h"
#include "stdlib/siphash.h"
#include "stdlib/tables.h"
#include "stdlib/text.h"
#include "types.h"
#include "util.h"

#define run_cmd(...)                                                                                                   \
    ({                                                                                                                 \
        const char *_cmd = String(__VA_ARGS__);                                                                        \
        if (verbose) print("\033[94;1m", _cmd, "\033[m");                                                              \
        popen(_cmd, "w");                                                                                              \
    })
#define command_output(...)                                                                                            \
    ({                                                                                                                 \
        const char *_cmd = String(__VA_ARGS__);                                                                        \
        if (verbose) print("\033[94;1m", _cmd, "\033[m");                                                              \
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

#ifdef __linux__
// Only on Linux is /proc/self/exe available
static struct stat compiler_stat;
#endif

static const char *paths_str(List_t paths) {
    Text_t result = EMPTY_TEXT;
    for (int64_t i = 0; i < (int64_t)paths.length; i++) {
        if (i > 0) result = Texts(result, Text(" "));
        result = Texts(result, Path$as_text((Path_t *)(paths.data + i * paths.stride), false, &Path$info));
    }
    return Text$as_c_string(result);
}

static OptionalBool_t verbose = false, quiet = false, show_version = false, show_prefix = false, clean_build = false,
                      source_mapping = true, should_install = false, install_target = false;

// Cross-compilation state, set up in main() when --target names a platform
// other than the one this Tomo build is for:
static OptionalText_t target = NONE_TEXT;
static bool cross_compiling = false;
// The directory holding the target platform's lib/ + include/ trees (extracted
// from that platform's Tomo distribution archive). Lives in the user's XDG data
// directory rather than TOMO_PATH so installing one never needs root:
static Text_t target_root = Text("");
// The prefix whose lib/ + include/ user programs compile and link against
// (TOMO_PATH normally, target_root when cross-compiling):
static Text_t lib_root = Text("");
// Whether the platform being compiled for uses Mach-O linking (macOS):
static bool link_macho = false;

static List_t format_files = EMPTY_LIST, format_files_inplace = EMPTY_LIST, parse_files = EMPTY_LIST,
              transpile_files = EMPTY_LIST, compile_objects = EMPTY_LIST, compile_executables = EMPTY_LIST,
              run_files = EMPTY_LIST, uninstall_packages = EMPTY_LIST, packages = EMPTY_LIST, args = EMPTY_LIST,
              show_build_info = EMPTY_LIST, extract_source_files = EMPTY_LIST;

static OptionalText_t show_codegen = NONE_TEXT,
                      cflags = Text("-Werror -fdollars-in-identifiers -std=gnu23 -Wno-trigraphs"
                                    " -ffunction-sections -fdata-sections"
                                    " -fno-signed-zeros"
                                    " -fPIC -ggdb"
                                    " -DGC_THREADS"),
                      ldlibs = Text("-lm"), ldflags = Text(""), optimization = Text("2"),
                      // The toolchain is not configurable; these are set in main() to the
                      // `zig cc`/`zig ar` bundled inside the Tomo installation:
    cc = Text(""), ar = Text("");

static Text_t config_summary,
    // This will be either "" or "sudo -u <user>" or "doas -u <user>"
    // to allow a command to put stuff into TOMO_PATH as the owner
    // of that directory.
    as_owner = Text("");

typedef enum { COMPILE_C_FILES, COMPILE_OBJ, COMPILE_EXE } compile_mode_t;

static void print_build_info(Path_t p);
static void write_source_blob(env_t *env, Path_t main_file, Path_t blob_path);
static void extract_embedded_source(Path_t binary);
static void transpile_header(env_t *base_env, Path_t path);
static void transpile_code(env_t *base_env, Path_t path);
static void compile_object_file(Path_t path);
static Path_t compile_executable(env_t *base_env, Path_t path, Path_t exe_path, List_t object_files,
                                 List_t extra_ldlibs);
static void build_file_dependency_graph(Table_t *build_info, Path_t path, Table_t *to_compile, Table_t *to_link);
static void build_package(Path_t pkg_dir);
static void install_package(Path_t pkg_dir);
static void compile_files(env_t *env, List_t files, List_t *object_files, List_t *ldlibs, compile_mode_t mode);
static bool is_stale(Path_t path, Path_t relative_to, bool ignore_missing);
static bool is_stale_for_any(Path_t path, List_t relative_to, bool ignore_missing);
static Path_t build_file(Path_t path, const char *extension);
static void wait_for_child_success(pid_t child);
static bool is_config_outdated(Path_t path);
static Path_t get_exe_path(Path_t path);

typedef struct {
    bool h : 1, c : 1, o : 1;
} staleness_t;

// The OS component of a platform key like "x86_64-linux" (the part after the
// last dash; architectures never contain dashes):
static const char *platform_os(const char *platform) {
    const char *dash = strrchr(platform, '-');
    return dash ? dash + 1 : platform;
}

// The `zig cc -target` triple for a platform key: Linux targets musl; every
// other OS's platform key is already a valid Zig target:
static const char *platform_triple(const char *platform) {
    if (streq(platform_os(platform), "linux")) return String(platform, "-musl");
    return platform;
}

static bool platform_supported(const char *platform) {
    return strstr(" " TOMO_DIST_PLATFORMS " ", String(" ", platform, " ")) != NULL;
}

// Cross-compiling needs the *target* platform's libtomo, vendored libraries,
// and headers. They come from the target platform's Tomo distribution archive,
// whose lib/ + include/ trees get extracted into target_root. If they're not
// installed yet, offer to download them (or just do it if --install-target).
static void ensure_target_installed(void) {
    Path_t marker = Path$from_str(String(target_root, "/lib/libtomo@", TOMO_VERSION, ".a"));
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

static List_t normalize_tm_paths(List_t paths) {
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

int main(int argc, char *argv[]) {
    GC_INIT();
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

    // Set up environment variables:
    const char *PATH = getenv("PATH");
    setenv("PATH", PATH ? String(TOMO_PATH, "/bin:", PATH) : String(TOMO_PATH, "/bin"), 1);
    const char *LD_LIBRARY_PATH = getenv("LD_LIBRARY_PATH");
    setenv("LD_LIBRARY_PATH", LD_LIBRARY_PATH ? String(TOMO_PATH, "/lib:", LD_LIBRARY_PATH) : String(TOMO_PATH, "/lib"),
           1);
    const char *LIBRARY_PATH = getenv("LIBRARY_PATH");
    setenv("LIBRARY_PATH", LIBRARY_PATH ? String(TOMO_PATH, "/lib:", LIBRARY_PATH) : String(TOMO_PATH, "/lib"), 1);
    const char *C_INCLUDE_PATH = getenv("C_INCLUDE_PATH");
    setenv("C_INCLUDE_PATH",
           C_INCLUDE_PATH ? String(TOMO_PATH, "/include:", C_INCLUDE_PATH) : String(TOMO_PATH, "/include"), 1);
    const char *CPATH = getenv("CPATH");
    setenv("CPATH", CPATH ? String(TOMO_PATH, "/include:", CPATH) : String(TOMO_PATH, "/include"), 1);

    Text_t usage = Texts("\x1b[93;4;1mUsage:\x1b[m\n"
                         "\x1b[1mRun a program:\x1b[m         tomo file.tm [-- args...]\n"
                         "\x1b[1mTranspile files:\x1b[m       tomo -t file.tm\n"
                         "\x1b[1mCompile object file:\x1b[m   tomo -c file.tm\n"
                         "\x1b[1mCompile executable:\x1b[m    tomo -e file.tm\n"
                         "\x1b[1mBuild packages:\x1b[m       tomo -p package...\n"
                         "\x1b[1mUninstall packages:\x1b[m   tomo -u package...\n"
                         "\x1b[1mOther flags:\x1b[m\n"
                         "  --verbose|-v: verbose output\n"
                         "  --prefix: print the Tomo prefix directory\n"
                         "  --quiet|-q: quiet output\n"
                         "  --parse|-P: show parse tree\n"
                         "  --transpile|-t: transpile C code without compiling\n"
                         "  --show-codegen|-c <pager>: show generated code\n"
                         "  --compile-obj|-c: compile C code for object file\n"
                         "  --compile-exe|-e: compile to standalone executable without running\n"
                         "  --format|F: print formatted code\n"
                         "  --format-inplace: format the code in a file (in place)\n"
                         "  --package|p: build a folder as a package\n"
                         "  --install|-I: install the executable or package\n"
                         "  --uninstall|-u: uninstall an executable or package\n"
                         "  --optimization|-O <level>: set optimization level\n"
                         "  --force-rebuild|-f: force rebuilding\n"
                         "  --build-info|-b <file>: print the build info embedded in a compiled file\n"
                         "  --extract-source|-x <file>: extract the source files embedded in a compiled program\n"
                         "  --source-mapping|-m <yes|no>: toggle source mapping in generated code\n"
                         "  --target <platform>: cross-compile for another platform; one of:\n"
                         "      " TOMO_DIST_PLATFORMS "\n"
                         "  --install-target: install the --target platform's libraries without asking\n"
                         "  --changelog: show the Tomo changelog\n");
    Text_t help = Texts(Text("\x1b[1mtomo\x1b[m: a compiler for the Tomo programming language"), Text("\n\n"), usage);
    cli_arg_t tomo_args[] = {
        {"run", &run_files, List$info(&Path$info), .short_flag = 'r'}, //
        {"args", &args, List$info(&CString$info)}, //
        {"format", &format_files, List$info(&Path$info), .short_flag = 'F'}, //
        {"parse", &parse_files, List$info(&Path$info), .short_flag = 'P'}, //
        {"format-inplace", &format_files_inplace, List$info(&Path$info)}, //
        {"transpile", &transpile_files, List$info(&Path$info), .short_flag = 't'}, //
        {"compile-obj", &compile_objects, List$info(&Path$info), .short_flag = 'c'}, //
        {"compile-exe", &compile_executables, List$info(&Path$info), .short_flag = 'e'}, //
        {"package", &packages, List$info(&Path$info), .short_flag = 'p'}, //
        {"uninstall", &uninstall_packages, List$info(&Text$info), .short_flag = 'u'}, //
        {"verbose", &verbose, &Bool$info, .short_flag = 'v'}, //
        {"install", &should_install, &Bool$info, .short_flag = 'I'}, //
        {"prefix", &show_prefix, &Bool$info}, //
        {"quiet", &quiet, &Bool$info, .short_flag = 'q'}, //
        {"version", &show_version, &Bool$info, .short_flag = 'V'}, //
        {"show-codegen", &show_codegen, &Text$info, .short_flag = 'C'}, //
        {"build-info", &show_build_info, List$info(&Path$info), .short_flag = 'b'},
        {"extract-source", &extract_source_files, List$info(&Path$info), .short_flag = 'x'}, //
        {"optimization", &optimization, &Text$info, .short_flag = 'O'}, //
        {"force-rebuild", &clean_build, &Bool$info, .short_flag = 'f'}, //
        {"source-mapping", &source_mapping, &Bool$info, .short_flag = 'm'},
        {"target", &target, &Text$info}, //
        {"install-target", &install_target, &Bool$info}, //
    };

    tomo_parse_args(argc, argv, usage, help, TOMO_VERSION, sizeof(tomo_args) / sizeof(tomo_args[0]), tomo_args);
    if (show_prefix) {
        print(TOMO_PATH);
        return 0;
    }

    if (show_version) {
        if (verbose) print(TOMO_VERSION, " ", GIT_VERSION);
        else print(TOMO_VERSION);
        return 0;
    }

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
        if (should_install) print_err("--install can't be combined with --target: the binary wouldn't run here");
        // `tomo --target <platform> --install-target` with nothing else to do:
        if (install_target && run_files.length == 0 && format_files.length == 0 && format_files_inplace.length == 0
            && parse_files.length == 0 && transpile_files.length == 0 && compile_objects.length == 0
            && compile_executables.length == 0 && uninstall_packages.length == 0 && packages.length == 0
            && show_build_info.length == 0 && extract_source_files.length == 0)
            return 0;
    }

    // Compile against the headers and libraries of the platform being compiled
    // for: the target's when cross-compiling, this installation's otherwise.
    lib_root = cross_compiling ? target_root : Text$from_str(TOMO_PATH);
    cflags = Texts("-I'", lib_root, "/include' -I'", lib_root, "/lib/tomo@", TOMO_VERSION, "' ", cflags);
    if (cross_compiling) {
        // Point the system-header/library search env vars at the target's too:
        setenv("C_INCLUDE_PATH", String(lib_root, "/include"), 1);
        setenv("CPATH", String(lib_root, "/include"), 1);
        setenv("LIBRARY_PATH", String(lib_root, "/lib"), 1);
        cflags = Texts("-target ", platform_triple(Text$as_c_string(target)), " ", cflags);
    } else if (ZIG_TARGET[0] != '\0') {
        // ZIG_TARGET (this build's own target triple) is baked in at compile time:
        cflags = Texts("-target ", ZIG_TARGET, " ", cflags);
    }

    ldflags = Texts(ldflags, Text(" -ffunction-sections -fdata-sections"));
    // The stack unwinder used by libtomo's stacktrace code; zig provides it for
    // every supported target:
    ldlibs = Texts(ldlibs, Text(" -lunwind"));
    // Link flags depend on the OS being compiled for:
    const char *link_os = cross_compiling ? platform_os(Text$as_c_string(target)) : platform_os(TOMO_PLATFORM);
    // Linux/musl links fully statically:
    if (streq(link_os, "linux")) ldflags = Texts(ldflags, Text(" -static"));
    if (streq(link_os, "macos")) {
        link_macho = true;
        // -u _tomo_versions forces the versions.o member (the version info in
        // the __TEXT,__tomo_versions section) out of libtomo.a into every
        // executable:
        ldflags = Texts(ldflags, " -Wl,-w,-dead_strip -Wl,-U,build_info -Wl,-u,_tomo_versions");
    } else {
        ldflags = Texts(ldflags, " -Wl,--gc-sections -Wl,-u,build_info -Wl,-u,tomo_versions");
    }

#ifdef __APPLE__
    if (!cross_compiling) {
        cflags = Texts(cflags, Text(" -I/opt/homebrew/include"));
        ldflags = Texts(ldflags, Text(" -L/opt/homebrew/lib -Wl,-rpath,/opt/homebrew/lib"));
    }
#endif

    if (show_codegen.length > 0 && Text$equal_values(show_codegen, Text("pretty")))
        show_codegen = Text("{ sed '/^#line/d;/^$/d' | clang-format | bat -l c -P; }");

    config_summary = Texts("TOMO_VERSION=", TOMO_VERSION, "\n", "COMPILER=", cc, " ", cflags, " -O", optimization, "\n",
                           "SOURCE_MAPPING=", source_mapping ? Text("yes") : Text("no"), "\n");

    // Uninstall packages:
    for (int64_t i = 0; i < (int64_t)uninstall_packages.length; i++) {
        Text_t *u = (Text_t *)(uninstall_packages.data + i * uninstall_packages.stride);
        xsystem(as_owner, "rm -rvf '", TOMO_PATH, "'/lib/tomo@", TOMO_VERSION, "/", *u, " '", TOMO_PATH, "'/bin/", *u,
                " '", TOMO_PATH, "'/man/man1/", *u, ".1");
        print("Uninstalled ", *u);
    }

    // Build info:
    for (int64_t i = 0; i < (int64_t)show_build_info.length; i++) {
        Path_t p = *(Path_t *)(show_build_info.data + i * show_build_info.stride);
        print_build_info(p);
    }

    // Extract embedded source zips:
    for (int64_t i = 0; i < (int64_t)extract_source_files.length; i++) {
        Path_t p = *(Path_t *)(extract_source_files.data + i * extract_source_files.stride);
        extract_embedded_source(p);
    }

    // Build (and install) packages
    Path_t cwd = Path$current_dir();
    for (int64_t i = 0; i < (int64_t)packages.length; i++) {
        Path_t *lib = (Path_t *)(packages.data + i * packages.stride);
        *lib = Path$resolved(*lib, cwd);
        // Fork a child process to build the package to prevent cross-contamination
        // of side effects when building one package from affecting another package.
        // This *could* be done in parallel, but there may be some dependency issues.
        pid_t child = fork();
        if (child == 0) {
            build_package(*lib);
            if (should_install) install_package(*lib);
            _exit(0);
        }
        wait_for_child_success(child);
    }

    parse_files = normalize_tm_paths(parse_files);
    for (int64_t i = 0; i < (int64_t)parse_files.length; i++) {
        Path_t path = *(Path_t *)(parse_files.data + i * parse_files.stride);
        ast_t *ast = parse_file(Path$as_c_string(path), NULL);
        print(ast_to_sexp_str(ast));
    }

    format_files = normalize_tm_paths(format_files);
    for (int64_t i = 0; i < (int64_t)format_files.length; i++) {
        Path_t path = *(Path_t *)(format_files.data + i * format_files.stride);
        Text_t formatted = format_file(Path$as_c_string(path));
        print_inline(formatted);
    }

    format_files_inplace = normalize_tm_paths(format_files_inplace);
    for (int64_t i = 0; i < (int64_t)format_files_inplace.length; i++) {
        Path_t path = *(Path_t *)(format_files_inplace.data + i * format_files_inplace.stride);
        Text_t formatted = format_file(Path$as_c_string(path));
        print("Formatted ", path);
        Path$write(path, formatted, 0644);
    }

    if (transpile_files.length > 0) {
        transpile_files = normalize_tm_paths(transpile_files);
        env_t *env = global_env(source_mapping);
        List_t object_files = EMPTY_LIST, extra_ldlibs = EMPTY_LIST;
        compile_files(env, transpile_files, &object_files, &extra_ldlibs, COMPILE_C_FILES);
    }

    if (compile_objects.length > 0) {
        compile_objects = normalize_tm_paths(compile_objects);
        env_t *env = global_env(source_mapping);
        List_t object_files = EMPTY_LIST, extra_ldlibs = EMPTY_LIST;
        compile_files(env, transpile_files, &object_files, &extra_ldlibs, COMPILE_OBJ);
    }

    struct child_s {
        struct child_s *next;
        pid_t pid;
    } *child_processes = NULL;

    if (compile_executables.length > 0) {
        compile_executables = normalize_tm_paths(compile_executables);

        // Compile and install in parallel:
        for (int64_t i = 0; i < (int64_t)compile_executables.length; i++) {
            Path_t path = *(Path_t *)(compile_executables.data + i * compile_executables.stride);

            Path_t exe_path = get_exe_path(path);
            // Put executable as a sibling to the .tm file instead of in the .build
            // directory. Cross-compiled executables get the target platform as a
            // suffix (foo.aarch64-macos) so they don't collide with the native
            // executable or each other:
            Text_t exe_name = Path$base_name(exe_path);
            if (cross_compiling) exe_name = Texts(exe_name, ".", target);
            exe_path = Path$sibling(path, exe_name);
            pid_t child = fork();
            if (child == 0) {
                env_t *env = global_env(source_mapping);
                List_t object_files = EMPTY_LIST, extra_ldlibs = EMPTY_LIST;
                compile_files(env, List(path), &object_files, &extra_ldlibs, COMPILE_EXE);
                compile_executable(env, path, exe_path, object_files, extra_ldlibs);
                if (should_install) {
                    xsystem(as_owner, "mkdir -p '", TOMO_PATH, "/bin' '", TOMO_PATH, "/man/man1'");
                    xsystem(as_owner, "cp -v '", exe_path, "' '", TOMO_PATH, "/bin/'");
                    Path_t manpage_file = build_file(Path$with_extension(path, Text(".1"), true), "");
                    xsystem(as_owner, "cp -v '", manpage_file, "' '", TOMO_PATH, "/man/man1/'");
                }
                _exit(0);
            }

            child_processes = new (struct child_s, .next = child_processes, .pid = child);
        }

        for (; child_processes; child_processes = child_processes->next)
            wait_for_child_success(child_processes->pid);
    }

    // When running files, if `--verbose` is not set, then don't print "compiled to ..." messages
    if (!verbose) quiet = true;

    run_files = normalize_tm_paths(run_files);

    if (run_files.length == 0 && format_files.length == 0 && format_files_inplace.length == 0 && parse_files.length == 0
        && transpile_files.length == 0 && compile_objects.length == 0 && compile_executables.length == 0
        && uninstall_packages.length == 0 && packages.length == 0 && show_build_info.length == 0
        && extract_source_files.length == 0) {

        // Piping a program into Tomo
        if (!isatty(STDIN_FILENO)) {
            Path_t parent = Path$expand_home(Path$from_str(String("~/.local/state/tomo/tomo@", TOMO_VERSION)));
            Path$create_directory(parent, 0644, true);
            Path_t path = Path$child(parent, Text("stdin.tm"));

            int fd = open(path, O_CREAT | O_TRUNC | O_WRONLY, 0644);
            if (fd < 0) {
                fail("Could not open temporary file for writing at ", path);
            }
            char buf[256];
            for (ssize_t len; (len = read(STDIN_FILENO, buf, sizeof(buf))) > 0;) {
                write(fd, buf, (size_t)len);
            }
            if (close(fd) != 0) fail("Could not close file: ", path);
            List$insert(&run_files, &path, I(0), sizeof(path));
            goto run_files;
        }

        // If not on a TTY, then just print version and exit
        if (!isatty(STDOUT_FILENO)) {
            print(help);
            return 0;
        }

        Path_t path = Path$from_str(String("~/.local/state/tomo/tomo@", TOMO_VERSION, "/run.tm"));
        path = Path$expand_home(path);
        Path$create_directory(Path$parent(path), 0755, true);
        if (!Path$exists(path)) {
            Path$write(path,
                       Text("# This is a handy Tomo REPL-like runner\n" //
                            "# Normally you would run `tomo ./file.tm` to run a script\n" //
                            "# See `tomo --help` for full usage\n" //
                            "\n" //
                            "func main()\n" //
                            "    # Put your code here:\n" //
                            "    say(\"Hello world!\")\n" //
                            "\n" //
                            "# Save and exit to run\n"),
                       0644);
        }
        List$insert(&run_files, &path, I(0), sizeof(path));
        const char *editor = getenv("EDITOR");
        if (!editor || editor[0] == '\0') editor = "vim";
        int status = system(String(editor, " ", path));
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) return 1;
    }

run_files:;

    if (cross_compiling && run_files.length > 0)
        print_err("Programs cross-compiled with --target can't run on this machine; "
                  "use --compile-exe to build them instead");

    // Compile runnable files in parallel, then execute in serial:
    for (int64_t i = 0; i < (int64_t)run_files.length; i++) {
        Path_t path = *(Path_t *)(run_files.data + i * run_files.stride);
        Path_t exe_path = get_exe_path(path);
        pid_t child = fork();
        if (child == 0) {
            env_t *env = global_env(source_mapping);
            List_t object_files = EMPTY_LIST, extra_ldlibs = EMPTY_LIST;
            compile_files(env, List(path), &object_files, &extra_ldlibs, COMPILE_EXE);
            compile_executable(env, path, exe_path, object_files, extra_ldlibs);
            _exit(0);
        }

        child_processes = new (struct child_s, .next = child_processes, .pid = child);
    }

    for (; child_processes; child_processes = child_processes->next)
        wait_for_child_success(child_processes->pid);

    // After parallel compilation, do serial execution:
    for (int64_t i = 0; i < (int64_t)run_files.length; i++) {
        Path_t path = *(Path_t *)(run_files.data + i * run_files.stride);
        Path_t exe_path = get_exe_path(path);
        // Don't fork for the last program
        pid_t child = i == (int64_t)run_files.length - 1 ? 0 : fork();
        if (child == 0) {
            const char *prog_args[1 + args.length + 1];
            Path_t relative_exe = Path$relative_to(exe_path, Path$current_dir());
            prog_args[0] = (char *)Path$as_c_string(relative_exe);
            for (int64_t j = 0; j < (int64_t)args.length; j++)
                prog_args[j + 1] = *(const char **)(args.data + j * args.stride);
            prog_args[1 + args.length] = NULL;
            execv(prog_args[0], (char **)prog_args);
            print_err("Could not execute program: ", prog_args[0]);
        }
        wait_for_child_success(child);
    }

    return 0;
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

// The build-info blob lives in a named section (see compile_build_info()
// below), but rather than maintaining ELF/Mach-O/archive parsers just to find
// that section, the blob brackets itself with sentinel strings and this scans
// the raw bytes for them -- which works uniformly on executables for any
// platform and on `ar` archives like package.a. Entries are NUL-separated on
// ELF (where the section is a string table) and newline-separated on Mach-O,
// so NULs print as newlines.
void print_build_info(Path_t p) {
    p = Path$expand_home(p);
    char *contents = NULL;
    struct stat sb;
    int fd = open(p, O_RDONLY);
    if (fd != -1 && fstat(fd, &sb) == 0) {
        contents = mmap(NULL, (size_t)sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    }
    if (contents == NULL) {
        fprint(stderr, "Could not open file: ", p);
        exit(1);
    }
    const char *contents_end = contents + sb.st_size;
    static const char *start_header = "===== Begin Tomo Build Info =====";
    static const char *end_header = "===== End Tomo Build Info =====";
    bool found = false;
    for (const char *match = contents;
         (match = memmem(match, (size_t)(contents_end - match), start_header, strlen(start_header)));) {
        const char *info = match + strlen(start_header);
        if (info < contents_end && (*info == '\n' || *info == '\0')) info += 1;
        const char *info_end = memmem(info, (size_t)(contents_end - info), end_header, strlen(end_header));
        if (info_end == NULL) break;
        for (const char *c = info; c < info_end; c++)
            fputc(*c == '\0' ? '\n' : *c, stdout);
        match = info_end + strlen(end_header);
        found = true;
    }
    if (!found) fprint(stderr, "No Tomo build info found in: ", p);
    munmap(contents, (size_t)sb.st_size);
}

// --- Embedded source zips ---------------------------------------------------
// Every compiled executable embeds a zip of the sources needed to rebuild it:
// the program's .tm file, its transitive local imports, and each source
// directory's packages.ini (which pins the versions of any used packages).
// The zip is deterministic (sorted entries, no timestamps: miniz is built with
// MINIZ_NO_TIME) and is bracketed by magic header/footer strings so it can be
// located with a raw scan, the same way build info is. It lives in a retained
// section (.tomo.source / __TEXT,__tomo_source), so it can also be pulled out
// with `objcopy -O binary --only-section=.tomo.source` or `segedit`.
static const char *SOURCE_ZIP_HEADER = "===== Begin Tomo Source Zip =====\n";
static const char *SOURCE_ZIP_FOOTER = "\n===== End Tomo Source Zip =====\n";

static char *slurp_file(Path_t path, size_t *size) {
    int fd = open(path, O_RDONLY);
    struct stat sb;
    if (fd < 0) return NULL;
    if (fstat(fd, &sb) != 0) {
        close(fd);
        return NULL;
    }
    char *buf = GC_MALLOC_ATOMIC((size_t)sb.st_size + 1);
    for (ssize_t off = 0; off < (ssize_t)sb.st_size;) {
        ssize_t got = read(fd, buf + off, (size_t)(sb.st_size - off));
        if (got <= 0) {
            close(fd);
            return NULL;
        }
        off += got;
    }
    close(fd);
    *size = (size_t)sb.st_size;
    return buf;
}

static void write_all(int fd, const void *data, size_t size, Path_t path) {
    for (size_t off = 0; off < size;) {
        ssize_t wrote = write(fd, (const char *)data + off, size - off);
        if (wrote <= 0) print_err("Could not write to file: ", path);
        off += (size_t)wrote;
    }
}

static int compare_source_entries(const void *a, const void *b) {
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

static bool is_build_artifact(Text_t filename) {
    return Text$ends_with(filename, Text(".a"), NULL) || Text$ends_with(filename, Text(".o"), NULL)
           || Text$ends_with(filename, Text(".so"), NULL) || Text$ends_with(filename, Text(".dylib"), NULL);
}

// Recursively add every file in `dir` to the zip's name->path table under
// `prefix`, skipping hidden files (like .build/), compiled artifacts, and
// symlinks (package binding links -- each linked package is embedded once,
// under its own packages/ entry, via the dependency graph):
static void add_dir_files(Table_t *files, Path_t dir, const char *prefix) {
    List_t children = Path$glob(Path$child(dir, Text("[!.]*")));
    for (int64_t i = 0; i < (int64_t)children.length; i++) {
        Path_t child = *(Path_t *)(children.data + i * children.stride);
        struct stat child_stat;
        if (lstat(child, &child_stat) != 0 || S_ISLNK(child_stat.st_mode)) continue;
        Text_t base = Path$base_name(child);
        const char *name = String(prefix, "/", Text$as_c_string(base));
        if (Path$is_directory(child, true)) add_dir_files(files, child, name);
        else if (!is_build_artifact(base)) Table$str_set(files, name, Path$as_c_string(child));
    }
}

// The zip entry recording which store entry each consumer's `use NAME`
// actually bound: one "<consumer>\t<name>\t<store dir>" line per direct use,
// where <consumer> is "" for the program's own files or the store-directory
// name of the package making the use. Extraction recreates the
// packages/<name> binding links from these lines (only for actually-used
// packages -- the packages.ini pins may cover transitive dependencies too):
static const char *SOURCE_LINKS_ENTRY = "packages.links";

// Record the packages that `consumer_file`'s use statements directly bind:
static void add_package_bindings(env_t *env, Table_t *bindings, Path_t consumer_file, const char *consumer) {
    ast_t *ast = parse_file(Path$as_c_string(consumer_file), NULL);
    if (!ast) return;
    for (ast_list_t *stmt = Match(ast, Block)->statements; stmt; stmt = stmt->next) {
        if (stmt->ast->tag != Use) continue;
        DeclareMatch(use, stmt->ast, Use);
        if (use->what != USE_PACKAGE) continue;
        OptionalPath_t installed = find_installed_package(env->build_info, stmt->ast);
        if (installed == NULL) continue;
        Table$str_set(bindings, String(consumer, "\t", use->path, "\t", Text$as_c_string(Path$base_name(installed))),
                      "");
    }
}

// Write the header + source zip + footer blob for `main_file` to `blob_path`:
void write_source_blob(env_t *env, Path_t main_file, Path_t blob_path) {
    Table_t dep_files = EMPTY_TABLE, to_link = EMPTY_TABLE;
    build_file_dependency_graph(env->build_info, main_file, &dep_files, &to_link);
    Path_t root = Path$parent(main_file);

    // Zip entry name -> source file path, deduplicated (several sources can
    // share a directory and thus a packages.ini). Files outside the project
    // directory are package sources (cross builds compile installed packages'
    // sources directly); their whole package is embedded below instead.
    Table_t files = EMPTY_TABLE;
    Table_t package_dirs = EMPTY_TABLE;
    Table_t bindings = EMPTY_TABLE;
    const char *lib_prefix = String(TOMO_PATH, "/lib/tomo@", TOMO_VERSION, "/store/");
    for (int64_t i = 0; i < (int64_t)dep_files.entries.length; i++) {
        struct {
            Path_t filename;
            staleness_t staleness;
        } *entry = dep_files.entries.data + i * dep_files.entries.stride;
        Path_t rel = Path$relative_to(entry->filename, root);
        if (strncmp(rel, "..", 2) == 0) {
            const char *rest = strncmp(entry->filename, lib_prefix, strlen(lib_prefix)) == 0
                                   ? strchr(entry->filename + strlen(lib_prefix), '/')
                                   : NULL;
            if (rest) {
                size_t dir_len = (size_t)(rest - entry->filename);
                char *pkg_dir = GC_MALLOC_ATOMIC(dir_len + 1);
                memcpy(pkg_dir, entry->filename, dir_len);
                pkg_dir[dir_len] = '\0';
                Table$str_set(&package_dirs, pkg_dir, pkg_dir);
            } else {
                fprint(stderr, "Warning: not embedding source file outside the project: ", entry->filename);
            }
            continue;
        }
        Table$str_set(&files, rel, Path$as_c_string(entry->filename));
        add_package_bindings(env, &bindings, entry->filename, "");
        Path_t ini = Path$sibling(entry->filename, Text("packages.ini"));
        if (Path$is_file(ini, true))
            Table$str_set(&files, Path$as_c_string(Path$relative_to(ini, root)), Path$as_c_string(ini));
    }

    // Every installed package that gets linked in (to_link holds each one's
    // package.a, including packages used transitively by other packages):
    for (int64_t i = 0; i < (int64_t)to_link.entries.length; i++) {
        Text_t lib = *(Text_t *)(to_link.entries.data + i * to_link.entries.stride);
        if (!Text$ends_with(lib, Text("/package.a"), NULL)) continue;
        Path_t pkg_dir = Path$parent(Path$from_text(lib));
        Table$str_set(&package_dirs, Path$as_c_string(pkg_dir), Path$as_c_string(pkg_dir));
    }

    // Embed each package's full sources (and license files) under store/,
    // mirroring the installed content-addressed layout. `tomo -x` recreates
    // the packages/<name> binding links from the extracted packages.ini pins:
    for (int64_t i = 0; i < (int64_t)package_dirs.entries.length; i++) {
        struct {
            const char *key, *value;
        } *entry = package_dirs.entries.data + i * package_dirs.entries.stride;
        Path_t pkg_dir = Path$from_str(entry->key);
        const char *store_name = Text$as_c_string(Path$base_name(pkg_dir));
        add_dir_files(&files, pkg_dir, String("store/", store_name));
        List_t pkg_files = Path$glob(Path$child(pkg_dir, Text("[!._0-9]*.tm")));
        for (int64_t j = 0; j < (int64_t)pkg_files.length; j++)
            add_package_bindings(env, &bindings, *(Path_t *)(pkg_files.data + j * pkg_files.stride), store_name);
    }

    // Also include the license texts shipped with the Tomo install (Tomo's own
    // license plus every statically linked library's) under licenses/:
    List_t licenses = Path$glob(Path$from_str(String(TOMO_PATH, "/share/licenses/tomo@", TOMO_VERSION, "/*")));
    for (int64_t i = 0; i < (int64_t)licenses.length; i++) {
        Path_t license = *(Path_t *)(licenses.data + i * licenses.stride);
        Table$str_set(&files, String("licenses/", Text$as_c_string(Path$base_name(license))),
                      Path$as_c_string(license));
    }

    // Sort by entry name so the zip is deterministic:
    int64_t num_files = files.entries.length;
    struct {
        const char *name, *path;
    } *entries = GC_MALLOC((size_t)num_files * sizeof(*entries));
    for (int64_t i = 0; i < num_files; i++)
        memcpy(&entries[i], files.entries.data + i * files.entries.stride, sizeof(entries[i]));
    qsort(entries, (size_t)num_files, sizeof(*entries), compare_source_entries);

    mz_zip_archive zip = {};
    if (!mz_zip_writer_init_heap(&zip, 0, 0)) print_err("Could not create source zip for ", main_file);
    for (int64_t i = 0; i < num_files; i++) {
        size_t size;
        char *contents = slurp_file(Path$from_str(entries[i].path), &size);
        if (contents == NULL) print_err("Could not read source file: ", entries[i].path);
        if (!mz_zip_writer_add_mem(&zip, entries[i].name, contents, size, MZ_BEST_COMPRESSION))
            print_err("Could not add ", entries[i].name, " to the source zip for ", main_file);
    }
    // The binding-links manifest (see SOURCE_LINKS_ENTRY):
    Text_t links = EMPTY_TEXT;
    for (int64_t i = 0; i < (int64_t)bindings.entries.length; i++) {
        struct {
            const char *key, *value;
        } *entry = bindings.entries.data + i * bindings.entries.stride;
        links = Texts(links, entry->key, "\n");
    }
    if (links.length > 0) {
        const char *links_str = Text$as_c_string(links);
        if (!mz_zip_writer_add_mem(&zip, SOURCE_LINKS_ENTRY, links_str, strlen(links_str), MZ_BEST_COMPRESSION))
            print_err("Could not add ", SOURCE_LINKS_ENTRY, " to the source zip for ", main_file);
    }

    void *zip_data;
    size_t zip_size;
    if (!mz_zip_writer_finalize_heap_archive(&zip, &zip_data, &zip_size))
        print_err("Could not finalize the source zip for ", main_file);
    mz_zip_writer_end(&zip);

    int fd = open(blob_path, O_CREAT | O_TRUNC | O_WRONLY, 0644);
    if (fd < 0) print_err("Could not write source zip: ", blob_path);
    write_all(fd, SOURCE_ZIP_HEADER, strlen(SOURCE_ZIP_HEADER), blob_path);
    write_all(fd, zip_data, zip_size, blob_path);
    write_all(fd, SOURCE_ZIP_FOOTER, strlen(SOURCE_ZIP_FOOTER), blob_path);
    close(fd);
    mz_free(zip_data);
}

// Recreate the packages/<name> binding links for an extracted source tree
// from its packages.links manifest. Only links tomo itself synthesizes get
// created (validated names, targets constrained to extracted store entries):
static void create_extracted_links(Path_t outdir, char *manifest) {
    for (char *line = manifest; line && *line;) {
        char *end = strchr(line, '\n');
        if (end) *end = '\0';
        char *tab1 = strchr(line, '\t');
        char *tab2 = tab1 ? strchr(tab1 + 1, '\t') : NULL;
        if (tab1 && tab2) {
            *tab1 = *tab2 = '\0';
            const char *consumer = line, *name = tab1 + 1, *dep = tab2 + 1;
            if (*name && *dep && !strchr(consumer, '/') && !strchr(name, '/') && !strchr(dep, '/')
                && !streq(consumer, "..") && !streq(name, "..") && !streq(dep, "..")) {
                Path_t base =
                    *consumer ? Path$from_str(String(Path$as_c_string(outdir), "/store/", consumer)) : outdir;
                Path_t dep_dir = Path$from_str(String(Path$as_c_string(outdir), "/store/", dep));
                if (Path$is_directory(base, true) && Path$is_directory(dep_dir, true)) {
                    Path_t link_dir = Path$child(base, Text("packages"));
                    Result_t result = Path$create_directory(link_dir, 0755, true);
                    if (result.Failure.reason.tag == TEXT_NONE) {
                        Path_t link = Path$child(link_dir, Text$from_str(name));
                        const char *target = *consumer ? String("../../", dep) : String("../store/", dep);
                        unlink(link);
                        if (symlink(target, link) == 0)
                            print("Linked    ", Path$relative_to(link, Path$current_dir()), " -> ", target);
                    }
                }
            }
        }
        line = end ? end + 1 : NULL;
    }
}

// Extract the embedded source zip from a compiled binary into a
// "<binary name>-source" directory:
void extract_embedded_source(Path_t binary) {
    binary = Path$resolved(Path$expand_home(binary), Path$current_dir());
    char *contents = NULL;
    struct stat sb;
    int fd = open(binary, O_RDONLY);
    if (fd != -1 && fstat(fd, &sb) == 0) {
        contents = mmap(NULL, (size_t)sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    }
    if (contents == NULL) print_err("Could not open file: ", binary);
    const char *zip_start = memmem(contents, (size_t)sb.st_size, SOURCE_ZIP_HEADER, strlen(SOURCE_ZIP_HEADER));
    if (zip_start == NULL) print_err("No embedded Tomo source found in: ", binary);
    zip_start += strlen(SOURCE_ZIP_HEADER);
    const char *zip_end = memmem(zip_start, (size_t)(contents + sb.st_size - zip_start), SOURCE_ZIP_FOOTER,
                                 strlen(SOURCE_ZIP_FOOTER));
    if (zip_end == NULL) print_err("The embedded Tomo source in ", binary, " is truncated");

    mz_zip_archive zip = {};
    if (!mz_zip_reader_init_mem(&zip, zip_start, (size_t)(zip_end - zip_start), 0))
        print_err("The embedded Tomo source in ", binary, " is not a valid zip file");
    Path_t outdir = Path$sibling(binary, Texts(Path$base_name(binary), Text("-source")));
    bool extracted_packages = false;
    char *links_manifest = NULL;
    for (mz_uint i = 0; i < mz_zip_reader_get_num_files(&zip); i++) {
        mz_zip_archive_file_stat stat;
        if (!mz_zip_reader_file_stat(&zip, i, &stat))
            print_err("The embedded Tomo source in ", binary, " is not a valid zip file");
        if (stat.m_is_directory) continue;
        if (stat.m_filename[0] == '/' || strstr(stat.m_filename, "..")) {
            fprint(stderr, "Skipping unsafe path in embedded source: ", stat.m_filename);
            continue;
        }
        size_t size;
        void *data = mz_zip_reader_extract_to_heap(&zip, i, &size, 0);
        if (data == NULL) print_err("Could not extract ", stat.m_filename, " from the source zip in ", binary);
        if (streq(stat.m_filename, SOURCE_LINKS_ENTRY)) {
            // Not a source file: consumed below to recreate the binding links.
            links_manifest = GC_MALLOC_ATOMIC(size + 1);
            memcpy(links_manifest, data, size);
            links_manifest[size] = '\0';
            mz_free(data);
            continue;
        }
        Path_t out = Path$from_str(String(Path$as_c_string(outdir), "/", stat.m_filename));
        Path$create_directory(Path$parent(out), 0755, true);
        int out_fd = open(out, O_CREAT | O_TRUNC | O_WRONLY, 0644);
        if (out_fd < 0) print_err("Could not write extracted file: ", out);
        write_all(out_fd, data, size, out);
        close(out_fd);
        mz_free(data);
        print("Extracted ", Path$relative_to(out, Path$current_dir()));
        if (strncmp(stat.m_filename, "store/", strlen("store/")) == 0) extracted_packages = true;
    }
    mz_zip_reader_end(&zip);
    munmap(contents, (size_t)sb.st_size);

    if (links_manifest) create_extracted_links(outdir, links_manifest);

    if (extracted_packages)
        print("\nNote: the sources of the packages this program uses were extracted into store/,\n"
              "with packages/<name> links resolving each `use`. To rebuild using those copies\n"
              "instead of fetching the pinned sources, point the packages.ini entries at them,\n"
              "e.g.: source=./store/<digest> (and remove that package's digest= line).");
}

// The C code embedding the source blob in a retained section on ELF targets
// (`.incbin` splices the blob file in verbatim). On Mach-O the blob is
// embedded at link time instead, via -sectcreate (see compile_executable):
static Text_t compile_source_asm(Path_t blob_path) {
    Text_t asm_text = Texts(".pushsection .tomo.source,\"aR\",%progbits\n"
                            ".globl tomo_source\ntomo_source:\n"
                            ".incbin ",
                            Text$quoted(Text$from_str(Path$as_c_string(blob_path)), false, Text("\"")),
                            "\n.popsection\n");
    return Texts("__asm__(", Text$quoted(asm_text, false, Text("\"")), ");\n");
}

Path_t get_exe_path(Path_t path) {
    ast_t *ast = parse_file(Path$as_c_string(path), NULL);
    OptionalText_t exe_name = ast_metadata(ast, "EXECUTABLE");
    if (exe_name.tag == TEXT_NONE) exe_name = Path$base_name(Path$with_extension(path, Text(""), true));

    return Path$child(tm_build_dir(path), exe_name);
}

// Cross-compiled artifacts live in a per-target subdirectory
// (.build/<platform>/) so they never clobber native builds' artifacts:
Path_t build_file(Path_t path, const char *extension) {
    return Path$child(tm_build_dir(path), Texts(Path$base_name(path), Text$from_str(extension)));
}

// The C code defining the build-info blob, which lives in a named section so
// it can be retrieved with standard tools (readelf -p .tomo.build_info /
// otool -s __TEXT __tomo_build) in addition to `tomo --build-info`'s
// sentinel-based scan. On ELF targets the section is emitted with module-level
// asm so it can carry the SHF_STRINGS + SHF_GNU_RETAIN flags ("aRS"): one
// NUL-terminated string per entry, which `readelf -p` prints one line at a
// time, retained through the linker's default --gc-sections. Mach-O has no
// equivalent flags, so there the blob is a plain newline-separated char array.
static Text_t compile_build_info(env_t *env, const char *symbol) {
    if (link_macho) {
        Text_t blob = Text("===== Begin Tomo Build Info =====\n");
        for (int64_t i = 0; i < (int64_t)env->build_info->entries.length; i++) {
            struct {
                const char *key, *value;
            } *entry = env->build_info->entries.data + i * env->build_info->entries.stride;
            blob = Texts(blob, entry->key, ": ", entry->value, "\n");
        }
        blob = Texts(blob, "===== End Tomo Build Info =====\n");
        return Texts("const char ", symbol,
                     "[] __attribute__((used, visibility(\"default\"), section(\"__TEXT,__tomo_build\"))) = ",
                     Text$quoted(blob, false, Text("\"")), ";\n");
    }
    Text_t asm_text = Texts(".pushsection .tomo.build_info,\"aRS\",%progbits\n"
                            ".globl ",
                            symbol, "\n", symbol,
                            ":\n"
                            ".asciz \"===== Begin Tomo Build Info =====\"\n");
    for (int64_t i = 0; i < (int64_t)env->build_info->entries.length; i++) {
        struct {
            const char *key, *value;
        } *entry = env->build_info->entries.data + i * env->build_info->entries.stride;
        asm_text = Texts(asm_text, ".asciz ", Text$quoted(Texts(entry->key, ": ", entry->value), false, Text("\"")),
                         "\n");
    }
    asm_text = Texts(asm_text, ".asciz \"===== End Tomo Build Info =====\"\n.popsection\n");
    return Texts("__asm__(", Text$quoted(asm_text, false, Text("\"")), ");\n");
}

static void add_git_info(env_t *env, Path_t dir) {
    const char *commit = command_output("git -C '", dir, "' rev-parse HEAD 2>/dev/null");
    if (commit) {
        Table$str_set(env->build_info, "Git commit", commit);
        const char *commit_time = command_output("git -C '", dir, "' log -1 --format=%cI 2>/dev/null");
        if (commit_time) Table$str_set(env->build_info, "Git commit time", commit_time);
        const char *dirty = command_output("git -C '", dir, "' diff --quiet  2>/dev/null && echo false || echo true");
        if (dirty) Table$str_set(env->build_info, "Git local changes", dirty);
    }
}

void build_package(Path_t pkg_dir) {
    pkg_dir = Path$resolved(pkg_dir, Path$current_dir());
    if (!Path$is_directory(pkg_dir, true)) print_err("Not a valid directory: ", pkg_dir);

    List_t tm_files = Path$glob(Path$child(pkg_dir, Text("[!._0-9]*.tm")));
    env_t *env = fresh_scope(global_env(source_mapping));
    List_t object_files = EMPTY_LIST, extra_ldlibs = EMPTY_LIST;

    compile_files(env, tm_files, &object_files, &extra_ldlibs, COMPILE_OBJ);
    // Cross-compiled package archives go in the per-target .build directory so
    // they don't clobber the native package.a:
    Path_t archive = cross_compiling ? build_file(Path$child(pkg_dir, Text("package.a")), "")
                                     : Path$child(pkg_dir, Text("package.a"));
    if (is_stale_for_any(archive, object_files, false)) {
        add_git_info(env, pkg_dir);

        // Store metadata about the package's build information:
        Path_t build_info_obj = build_file("./__build_info", ".o");
        {
            FILE *prog = run_cmd(cc, " ", cflags, " -x c -c - -o ", build_info_obj);
            if (!prog) print_err("Failed to run C compiler: ", cc);
            Text_t build_info = compile_build_info(env, "package_build_info");
            fputs(Text$as_c_string(build_info), prog);
            int status = pclose(prog);
            if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) exit(EXIT_FAILURE);
        }

        FILE *prog = run_cmd(ar, " rcs '", archive, "' ", paths_str(object_files), " '", build_info_obj, "'");
        if (!prog) print_err("Failed to run `ar`");
        int status = pclose(prog);
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) exit(EXIT_FAILURE);
        if (!quiet) print("Compiled static package:\t", Path$relative_to(archive, Path$current_dir()));
    } else {
        if (verbose) whisper("Unchanged: ", archive);
    }
}

void install_package(Path_t pkg_dir) {
    Text_t pkg_name = get_package_name(pkg_dir);
    Path_t dest = Path$child(Path$from_str(String(TOMO_PATH, "/lib/tomo@", TOMO_VERSION)), pkg_name);
    print("Installing ", pkg_dir, " into ", dest);
    if (!Enum$equal(&pkg_dir, &dest, &Path$info)) {
        if (verbose) whisper("Clearing out any pre-existing version of ", pkg_name);
        xsystem(as_owner, "rm -rf '", dest, "'");
        if (verbose) whisper("Moving files to ", dest);
        xsystem(as_owner, "mkdir -p '", dest, "'");
        xsystem(as_owner, "cp -r '", pkg_dir, "'/* '", dest, "/'");
        xsystem(as_owner, "cp -r '", pkg_dir, "'/.build '", dest, "/'");
    }
    print("Installed \033[1m", pkg_dir, "\033[m to ", TOMO_PATH, "/lib/tomo@", TOMO_VERSION, "/", pkg_name);
}

void compile_files(env_t *env, List_t to_compile, List_t *object_files, List_t *extra_ldlibs, compile_mode_t mode) {
    Table_t to_link = EMPTY_TABLE;
    Table_t dependency_files = EMPTY_TABLE;
    for (int64_t i = 0; i < (int64_t)to_compile.length; i++) {

        Path_t filename = *(Path_t *)(to_compile.data + i * to_compile.stride);
        if (!Path$has_extension(filename, Text("tm")))
            print_err("Not a valid .tm file: \x1b[91;1m", filename, "\x1b[m");
        if (!Path$is_file(filename, true)) print_err("Couldn't find file: ", filename);
        build_file_dependency_graph(env->build_info, filename, &dependency_files, &to_link);
    }

    // Make sure all files and dependencies have a .id file:
    for (int64_t i = 0; i < (int64_t)dependency_files.entries.length; i++) {
        struct {
            Path_t filename;
            staleness_t staleness;
        } *entry = (dependency_files.entries.data + i * dependency_files.entries.stride);

        Path_t id_file = build_file(entry->filename, ".id");
        if (!Path$exists(id_file)) {
            static const char id_chars[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
            int64_t num_id_chars = (int64_t)strlen(id_chars);
            char id_str[8];
            for (int j = 0; j < (int)sizeof(id_str); j++) {
                id_str[j] = id_chars[random_range(0, num_id_chars - 1)];
            }
            Text_t filename_id = Text("");
            Text_t base = Path$base_name(entry->filename);
            TextIter_t state = NEW_TEXT_ITER_STATE(base);
            for (int64_t j = 0; j < (int64_t)base.length; j++) {
                uint32_t c = Text$get_main_grapheme_fast(&state, j);
                if (c == '.') break;
                if (isalpha(c) || isdigit(c) || c == '_')
                    filename_id = Texts(filename_id, Text$from_strn((char[]){(char)c}, 1));
            }
            Path$write(id_file, Texts(filename_id, Text("_"), Text$from_strn(id_str, sizeof(id_str))), 0644);
        }
    }

    // (Re)compile header files, eagerly for explicitly passed in files, lazily
    // for downstream dependencies:
    for (int64_t i = 0; i < (int64_t)dependency_files.entries.length; i++) {
        struct {
            Path_t filename;
            staleness_t staleness;
        } *entry = (dependency_files.entries.data + i * dependency_files.entries.stride);

        if (entry->staleness.h || clean_build) {
            transpile_header(env, entry->filename);
            entry->staleness.o = true;
        } else {
            if (verbose) whisper("Unchanged: ", build_file(entry->filename, ".h"));
            if (show_codegen.length > 0) xsystem(show_codegen, " <", build_file(entry->filename, ".h"));
        }
    }

    env->imports = new (Table_t);

    struct child_s {
        struct child_s *next;
        pid_t pid;
    } *child_processes = NULL;

    // (Re)transpile and compile object files, eagerly for files explicitly
    // specified and lazily for downstream dependencies:
    for (int64_t i = 0; i < (int64_t)dependency_files.entries.length; i++) {
        struct {
            Path_t filename;
            staleness_t staleness;
        } *entry = (dependency_files.entries.data + i * dependency_files.entries.stride);
        if (!clean_build && !entry->staleness.c && !entry->staleness.h && !entry->staleness.o
            && !is_config_outdated(entry->filename)) {
            if (verbose) whisper("Unchanged: ", build_file(entry->filename, ".c"));
            if (show_codegen.length > 0) xsystem(show_codegen, " <", build_file(entry->filename, ".c"));
            if (verbose) whisper("Unchanged: ", build_file(entry->filename, ".o"));
            continue;
        }

        pid_t pid = fork();
        if (pid == 0) {
            if (clean_build || entry->staleness.c) transpile_code(env, entry->filename);
            else if (verbose) whisper("Unchanged: ", build_file(entry->filename, ".c"));
            if (mode != COMPILE_C_FILES) compile_object_file(entry->filename);
            _exit(EXIT_SUCCESS);
        }
        child_processes = new (struct child_s, .next = child_processes, .pid = pid);
    }

    for (; child_processes; child_processes = child_processes->next)
        wait_for_child_success(child_processes->pid);

    if (object_files) {
        for (int64_t i = 0; i < (int64_t)dependency_files.entries.length; i++) {
            struct {
                Path_t filename;
                staleness_t staleness;
            } *entry = (dependency_files.entries.data + i * dependency_files.entries.stride);
            Path_t path = entry->filename;
            path = build_file(path, ".o");
            List$insert(object_files, &path, I(0), sizeof(Path_t));
        }
    }
    if (extra_ldlibs) {
        for (int64_t i = 0; i < (int64_t)to_link.entries.length; i++) {
            Text_t lib = *(Text_t *)(to_link.entries.data + i * to_link.entries.stride);
            List$insert(extra_ldlibs, &lib, I(0), sizeof(Text_t));
        }
    }
}

bool is_config_outdated(Path_t path) {
    OptionalText_t config = Path$read(build_file(path, ".config"));
    if (config.tag == TEXT_NONE) return true;
    return !Text$equal_values(config, config_summary);
}

void build_file_dependency_graph(Table_t *build_info, Path_t path, Table_t *to_compile, Table_t *to_link) {
    if (Table$has_value(*to_compile, path, Table$info(&Path$info, &Byte$info))) return;

    staleness_t staleness = {
        .h = is_stale(build_file(path, ".h"), Path$sibling(path, Text("packages.ini")), true)
             || is_stale(build_file(path, ".h"), build_file(path, ":packages.ini"), true)
             || is_stale(build_file(path, ".h"), path, false)
             || is_stale(build_file(path, ".h"), build_file(path, ".id"), false),
        .c = is_stale(build_file(path, ".c"), Path$sibling(path, Text("packages.ini")), true)
             || is_stale(build_file(path, ".c"), build_file(path, ":packages.ini"), true)
             || is_stale(build_file(path, ".c"), path, false)
             || is_stale(build_file(path, ".c"), build_file(path, ".id"), false),
    };
    staleness.o = staleness.c || staleness.h || is_stale(build_file(path, ".o"), build_file(path, ".c"), false)
                  || is_stale(build_file(path, ".o"), build_file(path, ".h"), false);
    Table$set(to_compile, &path, &staleness, Table$info(&Path$info, &Byte$info));

    assert(Text$equal_values(Path$extension(path, true), Text("tm")));

    ast_t *ast = parse_file(Path$as_c_string(path), NULL);
    if (!ast) print_err("Could not parse file: ", path);

    for (ast_list_t *stmt = Match(ast, Block)->statements; stmt; stmt = stmt->next) {
        ast_t *stmt_ast = stmt->ast;
        if (stmt_ast->tag != Use) continue;
        DeclareMatch(use, stmt_ast, Use);

        switch (use->what) {
        case USE_LOCAL: {
            Path_t dep_tm = Path$resolved(Path$from_str(use->path), Path$parent(path));
            if (!Path$is_file(dep_tm, true)) code_err(stmt_ast, "Not a valid file: ", dep_tm);
            if (is_stale(build_file(path, ".h"), dep_tm, false)) staleness.h = true;
            if (is_stale(build_file(path, ".c"), dep_tm, false)) staleness.c = true;
            if (staleness.c || staleness.h) staleness.o = true;
            Table$set(to_compile, &path, &staleness, Table$info(&Path$info, &Byte$info));
            build_file_dependency_graph(build_info, dep_tm, to_compile, to_link);
            break;
        }
        case USE_PACKAGE: {
            OptionalPath_t installed = find_installed_package(build_info, stmt_ast);
            if (!installed) code_err(stmt_ast, "I don't know where to find this package.");

            List_t children = Path$glob(Path$child(installed, Text("/[!._0-9]*.tm")));
            if (cross_compiling) {
                // Installed package archives were compiled for the native
                // platform, so cross builds recompile the package's modules from
                // their installed sources (into per-target .build directories)
                // and link those objects instead of package.a:
                for (int64_t i = 0; i < (int64_t)children.length; i++) {
                    Path_t *child = (Path_t *)(children.data + i * children.stride);
                    build_file_dependency_graph(build_info, *child, to_compile, to_link);
                }
                break;
            }

            Text_t lib = Texts(installed, "/package.a");
            Table$set(to_link, &lib, NULL, Table$info(&Text$info, &Void$info));

            for (int64_t i = 0; i < (int64_t)children.length; i++) {
                Path_t *child = (Path_t *)(children.data + i * children.stride);
                Table_t discarded = {.entries = EMPTY_LIST, .fallback = to_compile};
                build_file_dependency_graph(build_info, *child, &discarded, to_link);
            }
            break;
        }
        case USE_SHARED_OBJECT: {
            Text_t lib = Text$from_str(use->path);
            Table$set(to_link, &lib, NULL, Table$info(&Text$info, &Void$info));
            break;
        }
        case USE_ASM: {
            Path_t asm_path = Path$from_str(use->path);
            asm_path = Path$concat(Path$parent(path), asm_path);
            Text_t linker_text = Path$as_text(&asm_path, NULL, &Path$info);
            Table$set(to_link, &linker_text, NULL, Table$info(&Text$info, &Void$info));
            if (is_stale(build_file(path, ".o"), asm_path, false)) {
                staleness.o = true;
                Table$set(to_compile, &path, &staleness, Table$info(&Path$info, &Byte$info));
            }
            break;
        }
        case USE_HEADER:
        case USE_C_CODE: {
            if (use->path[0] == '<') break;

            Path_t dep_path = Path$resolved(Path$from_str(use->path), Path$parent(path));
            if (is_stale(build_file(path, ".o"), dep_path, false)) {
                staleness.o = true;
                Table$set(to_compile, &path, &staleness, Table$info(&Path$info, &Byte$info));
            }
            break;
        }
        default: break;
        }
    }
}

time_t latest_included_modification_time(Path_t path) {
    static Table_t c_modification_times = EMPTY_TABLE;
    const TypeInfo_t time_info = {.size = sizeof(time_t), .align = __alignof__(time_t), .tag = OpaqueInfo};
    time_t *cached_latest = Table$get(c_modification_times, &path, Table$info(&Path$info, &time_info));
    if (cached_latest) return *cached_latest;

    struct stat s;
    time_t latest = 0;
    if (stat(Path$as_c_string(path), &s) == 0) latest = s.st_mtime;
    Table$set(&c_modification_times, &path, &latest, Table$info(&Path$info, &time_info));

    OptionalClosure_t by_line = Path$by_line(path);
    if (by_line.fn == NULL) return 0;
    OptionalText_t (*next_line)(void *) = by_line.fn;
    Path_t parent = Path$parent(path);
    bool allow_dot_include = Path$has_extension(path, Text("s")) || Path$has_extension(path, Text("S"));
    for (OptionalText_t line; (line = next_line(by_line.userdata)).tag != TEXT_NONE;) {
        line = Text$trim(line, Text(" \t"), true, false);
        if (!Text$starts_with(line, Text("#include"), NULL)
            && !(allow_dot_include && Text$starts_with(line, Text(".include"), NULL)))
            continue;

        // Check for `"` after `#include` or `.include` and some spaces:
        if (!Text$starts_with(Text$trim(Text$from(line, I(9)), Text(" \t"), true, false), Text("\""), NULL)) continue;

        List_t chunks = Text$split(line, Text("\""));
        if (chunks.length < 3) // Should be `#include "foo" ...` -> ["#include ", "foo", "..."]
            continue;

        Text_t included = *(Text_t *)(chunks.data + 1 * chunks.stride);
        Path_t included_path = Path$resolved(Path$from_text(included), parent);
        time_t included_time = latest_included_modification_time(included_path);
        if (included_time > latest) {
            latest = included_time;
            Table$set(&c_modification_times, &path, &latest, Table$info(&Path$info, &time_info));
        }
    }
    return latest;
}

bool is_stale(Path_t path, Path_t relative_to, bool ignore_missing) {
    struct stat target_stat;
    if (stat(Path$as_c_string(path), &target_stat) != 0) {
        if (ignore_missing) return false;
        return true;
    }

#ifdef __linux__
    // Any file older than the compiler is stale:
    if (target_stat.st_mtime < compiler_stat.st_mtime) return true;
#endif

    if (Path$has_extension(relative_to, Text("c")) || Path$has_extension(relative_to, Text("h"))
        || Path$has_extension(relative_to, Text("s")) || Path$has_extension(relative_to, Text("S"))) {
        time_t mtime = latest_included_modification_time(relative_to);
        return target_stat.st_mtime < mtime;
    }

    struct stat relative_to_stat;
    if (stat(Path$as_c_string(relative_to), &relative_to_stat) != 0) {
        if (ignore_missing) return false;
        print_err("File doesn't exist: ", relative_to);
    }
    return target_stat.st_mtime < relative_to_stat.st_mtime;
}

bool is_stale_for_any(Path_t path, List_t relative_to, bool ignore_missing) {
    for (int64_t i = 0; i < (int64_t)relative_to.length; i++) {
        Path_t r = *(Path_t *)(relative_to.data + i * relative_to.stride);
        if (is_stale(path, r, ignore_missing)) return true;
    }
    return false;
}

void transpile_header(env_t *base_env, Path_t path) {
    Path_t h_filename = build_file(path, ".h");
    ast_t *ast = parse_file(Path$as_c_string(path), NULL);
    if (!ast) print_err("Could not parse file: ", path);

    env_t *module_env = load_module_env(base_env, ast);

    Text_t h_code = compile_file_header(module_env, Path$resolved(h_filename, Path$from_str(".")), ast);

    FILE *header = fopen(Path$as_c_string(h_filename), "w");
    if (!header) print_err("Failed to open header file: ", h_filename);
    Text$print(header, h_code);
    if (fclose(header) == -1) print_err("Failed to write header file: ", h_filename);

    if (!quiet) print("Transpiled header:\t", Path$relative_to(h_filename, Path$current_dir()));

    if (show_codegen.length > 0) xsystem(show_codegen, " <", h_filename);
}

void transpile_code(env_t *base_env, Path_t path) {
    Path_t c_filename = build_file(path, ".c");
    ast_t *ast = parse_file(Path$as_c_string(path), NULL);
    if (!ast) print_err("Could not parse file: ", path);

    env_t *module_env = load_module_env(base_env, ast);

    Text_t c_code = compile_file(module_env, ast);

    FILE *c_file = fopen(Path$as_c_string(c_filename), "w");
    if (!c_file) print_err("Failed to write C file: ", c_filename);

    Text$print(c_file, c_code);

    binding_t *main_binding = get_binding(module_env, "main");
    if (main_binding && main_binding->type->tag == FunctionType) {
        type_t *ret = Match(main_binding->type, FunctionType)->ret;
        if (ret->tag != VoidType && ret->tag != AbortType)
            compiler_err(ast->file, ast->start, ast->end, "The main() function in this file has a return type of ",
                         type_to_text(ret), ", but it should not have any return value!");

        Text$print(c_file, Texts("int parse_and_run$$", main_binding->code, "(int argc, char *argv[]) {\n",
                                 module_env->do_source_mapping ? Text("#line 1\n") : EMPTY_TEXT, "tomo_init();\n",
                                 namespace_name(module_env, module_env->namespace, Text("$initialize")),
                                 "();\n"
                                 "\n",
                                 compile_cli_arg_call(module_env, ast, main_binding->code, main_binding->type),
                                 "return 0;\n"
                                 "}\n"));
    }

    if (fclose(c_file) == -1) print_err("Failed to output C code to ", c_filename);

    if (!quiet) print("Transpiled code:\t", Path$relative_to(c_filename, Path$current_dir()));

    if (show_codegen.length > 0) xsystem(show_codegen, " <", c_filename);
}

// The first time the bundled Zig toolchain compiles anything on a machine, it
// builds its libc (musl, compiler-rt, etc.) from source and stores it in its
// global cache; that one-time setup takes tens of seconds and can look like a
// hang. Detect a missing cache (using the same resolution order as zig itself:
// $ZIG_GLOBAL_CACHE_DIR, then $XDG_CACHE_HOME/zig, then $HOME/.cache/zig) and
// print a notice so users know what's happening.
static void warn_if_first_compile(void) {
    static bool already_checked = false;
    if (already_checked) return;
    already_checked = true;

    const char *dir = getenv("ZIG_GLOBAL_CACHE_DIR");
    if (dir == NULL || dir[0] == '\0') {
        const char *xdg_cache = getenv("XDG_CACHE_HOME");
        const char *home = getenv("HOME");
        if (xdg_cache && xdg_cache[0]) dir = String(xdg_cache, "/zig");
        else if (home && home[0]) dir = String(home, "/.cache/zig");
        else return;
    }

    struct stat st;
    if (stat(dir, &st) != 0) {
        fprint(stderr, USE_COLOR ? "\x1b[93;1m" : "",
               "First compile on this machine: the bundled Zig toolchain is building its libc cache "
               "(a one-time setup that takes a little while)...",
               USE_COLOR ? "\x1b[m" : "");
        fflush(stderr);
    }
}

void compile_object_file(Path_t path) {
    warn_if_first_compile();
    Path_t obj_file = build_file(path, ".o");
    Path_t c_file = build_file(path, ".c");

    FILE *prog = run_cmd(cc, " ", cflags, " -O", optimization, " -c ", c_file, " -o ", obj_file);
    if (!prog) print_err("Failed to run C compiler: ", cc);
    int status = pclose(prog);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) exit(EXIT_FAILURE);

    Path$write(build_file(path, ".config"), config_summary, 0644);

    if (!quiet) print("Compiled object:\t", Path$relative_to(obj_file, Path$current_dir()));
}

Path_t compile_executable(env_t *base_env, Path_t path, Path_t exe_path, List_t object_files, List_t extra_ldlibs) {
    warn_if_first_compile();
    ast_t *ast = parse_file(Path$as_c_string(path), NULL);
    if (!ast) print_err("Could not parse file ", path);
    env_t *env = load_module_env(base_env, ast);
    binding_t *main_binding = get_binding(env, "main");
    if (main_binding && main_binding->type->tag == FunctionType) {
        Path_t manpage_file = build_file(Path$with_extension(path, Text(".1"), true), "");
        if (clean_build || !Path$is_file(manpage_file, true) || is_stale(manpage_file, path, true)) {
            Text_t manpage =
                compile_manpage(Path$base_name(exe_path), ast, Match(main_binding->type, FunctionType)->args);
            Path$write(manpage_file, manpage, 0644);
            if (!quiet) print("Wrote manpage:\t", Path$relative_to(manpage_file, Path$current_dir()));
        } else {
            if (verbose) whisper("Unchanged: ", manpage_file);
        }
    }

    if (!clean_build && Path$is_file(exe_path, true) && !is_config_outdated(path)
        && !is_stale_for_any(exe_path, object_files, false)
        && !is_stale(exe_path, Path$sibling(path, Text("packages.ini")), true)
        && !is_stale(exe_path, build_file(path, ":packages.ini"), true)) {
        if (verbose) whisper("Unchanged: ", exe_path);
        return exe_path;
    }

    add_git_info(env, Path$parent(path));

    // Zip up the program's sources for embedding into the executable:
    Path_t source_blob = build_file(path, ".source.zip");
    write_source_blob(env, path, source_blob);

    Text_t program;
    if (main_binding && main_binding->type->tag == FunctionType) {
        program = Texts("extern int parse_and_run$$", main_binding->code,
                        "(int argc, char *argv[]);\n"
                        "__attribute__ ((noinline))\n"
                        "int main(int argc, char *argv[]) {\n"
                        "\treturn parse_and_run$$",
                        main_binding->code,
                        "(argc, argv);\n"
                        "}\n",
                        compile_build_info(env, "build_info"),
                        link_macho ? EMPTY_TEXT : compile_source_asm(source_blob));
    } else {
        program = Texts("extern void ", namespace_name(env, env->namespace, Text("$initialize")),
                        "(void);\n"
                        "extern void tomo_init(void);\n"
                        "__attribute__ ((noinline))\n"
                        "int main(int argc, char *argv[]) {\n"
                        "tomo_init();\n",
                        namespace_name(env, env->namespace, Text("$initialize")),
                        "();\n"
                        "\n",
                        "return 0;\n"
                        "}\n",
                        compile_build_info(env, "build_info"),
                        link_macho ? EMPTY_TEXT : compile_source_asm(source_blob));
    }
    Path_t runner_file = build_file(path, ".runner.c");
    Path$write(runner_file, program, 0644);

    // Libraries bundled with the Tomo toolchain: every program links the full
    // vendored archives (below), so a package's `use -lgmp` etc. must not
    // become a -l flag -- no system copies exist (the toolchain uses its own
    // static musl builds):
    static const char *bundled_libs[] = {"-lgc", "-lgmp", "-lunistring", "-lbacktrace", "-lm", "-lunwind"};

    // .a archive files need to go later in the positional order:
    List_t archives = EMPTY_LIST;
    for (int64_t i = 0; i < (int64_t)extra_ldlibs.length;) {
        Text_t *lib = (Text_t *)(extra_ldlibs.data + i * extra_ldlibs.stride);
        bool bundled = false;
        for (size_t j = 0; j < sizeof(bundled_libs) / sizeof(bundled_libs[0]); j++)
            bundled = bundled || Text$equal_values(*lib, Text$from_str(bundled_libs[j]));
        if (bundled) {
            List$remove_at(&extra_ldlibs, I(i + 1), I(1), sizeof(Text_t));
        } else if (Text$ends_with(*lib, Text(".a"), NULL)) {
            List$insert(&archives, lib, I(0), sizeof(Text_t));
            List$remove_at(&extra_ldlibs, I(i + 1), I(1), sizeof(Text_t));
        } else {
            i += 1;
        }
    }

    // The vendored static libraries that libtomo (and any package using their
    // headers) is linked against:
    Text_t vendor_dir = Texts(lib_root, "/lib/tomo@", TOMO_VERSION, "/vendor");
    Text_t vendor_archives = Texts(" ", vendor_dir, "/libgc.a ", vendor_dir, "/libgmp.a ", vendor_dir,
                                   "/libunistring.a ", vendor_dir, "/libbacktrace.a");

    // On Mach-O the source blob is embedded by the linker rather than asm:
    Text_t source_section_flag =
        link_macho ? Texts(" '-Wl,-sectcreate,__TEXT,__tomo_source,", Text$from_str(Path$as_c_string(source_blob)), "'")
                   : EMPTY_TEXT;

    FILE *runner = run_cmd( // Invoke C compiler
        cc,
        // C flags:
        " ", cflags, " -O", optimization,
        // Linker flags and dynamically linked shared packages:
        " ", ldflags, source_section_flag, " ", ldlibs, " ", list_text(extra_ldlibs),
        // Object files:
        " ", paths_str(object_files),
        // Input file:
        " ", runner_file,
        // Statically linked archive files (must come after runner). No archive
        // grouping is needed for circular dependencies among packages: zig links
        // with lld, which resolves archive members iteratively.
        " ", list_text(archives),
        // Tomo static library (Mach-O linking has no --no-whole-archive):
        link_macho ? "" : " -Wl,--no-whole-archive", " ", lib_root, "/lib/libtomo@", TOMO_VERSION, ".a",
        vendor_archives,
        // Output file:
        " -o ", exe_path);

    if (show_codegen.length > 0) {
        FILE *out = run_cmd(show_codegen);
        Text$print(out, program);
        pclose(out);
    }

    Text$print(runner, program);
    int status = pclose(runner);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) exit(EXIT_FAILURE);

    if (!quiet) print("Compiled executable:\t", Path$relative_to(exe_path, Path$current_dir()));
    return exe_path;
}
