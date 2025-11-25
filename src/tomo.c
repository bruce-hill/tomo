// The main program that runs compilation

#include <ctype.h>
#include <errno.h>
#include <gc.h>
#include <libgen.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/wait.h>
#if defined(__linux__)
#include <sys/random.h>
#endif

#include "ast.h"
#include "changes.md.h"
#include "compile/cli.h"
#include "compile/files.h"
#include "compile/headers.h"
#include "config.h"
#include "formatter/formatter.h"
#include "modules.h"
#include "naming.h"
#include "parse/files.h"
#include "stdlib/bools.h"
#include "stdlib/bytes.h"
#include "stdlib/c_strings.h"
#include "stdlib/cli.h"
#include "stdlib/datatypes.h"
#include "stdlib/lists.h"
#include "stdlib/optionals.h"
#include "stdlib/paths.h"
#include "stdlib/print.h"
#include "stdlib/random.h"
#include "stdlib/siphash.h"
#include "stdlib/tables.h"
#include "stdlib/text.h"
#include "stdlib/util.h"
#include "types.h"

#define run_cmd(...)                                                                                                   \
    ({                                                                                                                 \
        const char *_cmd = String(__VA_ARGS__);                                                                        \
        if (verbose) print("\033[34;1m", _cmd, "\033[m");                                                              \
        popen(_cmd, "w");                                                                                              \
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

#ifdef __APPLE__
#define SHARED_SUFFIX ".dylib"
#else
#define SHARED_SUFFIX ".so"
#endif

static OptionalBool_t verbose = false, quiet = false, show_version = false, show_prefix = false, clean_build = false,
                      source_mapping = true, show_changelog = false, should_install = false;

static List_t format_files = EMPTY_LIST, format_files_inplace = EMPTY_LIST, parse_files = EMPTY_LIST,
              transpile_files = EMPTY_LIST, compile_objects = EMPTY_LIST, compile_executables = EMPTY_LIST,
              run_files = EMPTY_LIST, uninstall_libraries = EMPTY_LIST, libraries = EMPTY_LIST, args = EMPTY_LIST;

static OptionalText_t show_codegen = NONE_TEXT,
                      cflags = Text("-Werror -fdollars-in-identifiers -std=c2x -Wno-trigraphs "
                                    " -ffunction-sections -fdata-sections"
                                    " -fno-signed-zeros "
                                    " -D_XOPEN_SOURCE -D_DEFAULT_SOURCE -fPIC -ggdb"
#if defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || defined(__APPLE__)
                                    " -D_BSD_SOURCE"
#endif
                                    " -DGC_THREADS"),
                      ldlibs = Text("-lgc -lm -lgmp -lunistring -ltomo_" TOMO_VERSION), ldflags = Text(""),
                      optimization = Text("2"), cc = Text(DEFAULT_C_COMPILER);

static Text_t config_summary,
    // This will be either "" or "sudo -u <user>" or "doas -u <user>"
    // to allow a command to put stuff into TOMO_PATH as the owner
    // of that directory.
    as_owner = Text("");

typedef enum { COMPILE_C_FILES, COMPILE_OBJ, COMPILE_EXE } compile_mode_t;

static void transpile_header(env_t *base_env, Path_t path);
static void transpile_code(env_t *base_env, Path_t path);
static void compile_object_file(Path_t path);
static Path_t compile_executable(env_t *base_env, Path_t path, Path_t exe_path, List_t object_files,
                                 List_t extra_ldlibs);
static void build_file_dependency_graph(Path_t path, Table_t *to_compile, Table_t *to_link);
static void build_library(Path_t lib_dir);
static void install_library(Path_t lib_dir);
static void compile_files(env_t *env, List_t files, List_t *object_files, List_t *ldlibs, compile_mode_t mode);
static bool is_stale(Path_t path, Path_t relative_to, bool ignore_missing);
static bool is_stale_for_any(Path_t path, List_t relative_to, bool ignore_missing);
static Path_t build_file(Path_t path, const char *extension);
static void wait_for_child_success(pid_t child);
static bool is_config_outdated(Path_t path);

typedef struct {
    bool h : 1, c : 1, o : 1;
} staleness_t;

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
#ifdef __linux__
    // Get the file modification time of the compiler, so we
    // can recompile files after changing the compiler:
    char compiler_path[PATH_MAX];
    ssize_t count = readlink("/proc/self/exe", compiler_path, PATH_MAX);
    if (count == -1) err(1, "Could not find age of compiler");
    compiler_path[count] = '\0';
    if (stat(compiler_path, &compiler_stat) != 0) err(1, "Could not find age of compiler");
#endif

#ifdef __OpenBSD__
    ldlibs = Texts(ldlibs, Text(" -lexecinfo"));
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

    cflags = Texts("-I'", TOMO_PATH, "/include' -I'", TOMO_PATH, "/lib/tomo_" TOMO_VERSION "' ", cflags);

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

    // Run a tool:
    if ((streq(argv[1], "-r") || streq(argv[1], "--run")) && argc >= 3) {
        if (strcspn(argv[2], "/;$") == strlen(argv[2])) {
            const char *program = String("'", TOMO_PATH, "'/lib/tomo_" TOMO_VERSION "/", argv[2], "/", argv[2]);
            execv(program, &argv[2]);
        }
        print_err("This is not an installed tomo program: ", argv[2]);
    }

    Text_t usage = Texts("\x1b[33;4;1mUsage:\x1b[m\n"
                         "\x1b[1mRun a program:\x1b[m         tomo file.tm [-- args...]\n"
                         "\x1b[1mTranspile files:\x1b[m       tomo -t file.tm\n"
                         "\x1b[1mCompile object file:\x1b[m  tomo -c file.tm\n"
                         "\x1b[1mCompile executable:\x1b[m   tomo -e file.tm\n"
                         "\x1b[1mBuild libraries:\x1b[m       tomo -L lib...\n"
                         "\x1b[1mUninstall libraries:\x1b[m   tomo -u lib...\n"
                         "\x1b[1mOther flags:\x1b[m\n"
                         "  --verbose|-v: verbose output\n"
                         "  --prefix: print the Tomo prefix directory\n"
                         "  --quiet|-q: quiet output\n"
                         "  --parse|-p: show parse tree\n"
                         "  --transpile|-t: transpile C code without compiling\n"
                         "  --show-codegen|-c <pager>: show generated code\n"
                         "  --compile-obj|-c: compile C code for object file\n"
                         "  --compile-exe|-e: compile to standalone executable without running\n"
                         "  --format: print formatted code\n"
                         "  --format-inplace: format the code in a file (in place)\n"
                         "  --library|-L: build a folder as a library\n"
                         "  --install|-I: install the executable or library\n"
                         "  --uninstall|-u: uninstall an executable or library\n"
                         "  --optimization|-O <level>: set optimization level\n"
                         "  --force-rebuild|-f: force rebuilding\n"
                         "  --source-mapping|-m <yes|no>: toggle source mapping in generated code\n"
                         "  --changelog: show the Tomo changelog\n"
                         "  --run|-r: run a program from ",
                         TOMO_PATH, "/share/tomo_" TOMO_VERSION "/installed\n");
    Text_t help = Texts(Text("\x1b[1mtomo\x1b[m: a compiler for the Tomo programming language"), Text("\n\n"), usage);
    cli_arg_t tomo_args[] = {
        {"run", &run_files, List$info(&Path$info), .short_flag = 'r'}, //
        {"args", &args, List$info(&CString$info)}, //
        {"format", &format_files, List$info(&Path$info), .short_flag = 'F'}, //
        {"format-inplace", &format_files_inplace, List$info(&Path$info)}, //
        {"transpile", &transpile_files, List$info(&Path$info), .short_flag = 't'}, //
        {"compile-obj", &compile_objects, List$info(&Path$info), .short_flag = 'c'}, //
        {"compile-exe", &compile_executables, List$info(&Path$info), .short_flag = 'e'}, //
        {"library", &libraries, List$info(&Path$info), .short_flag = 'L'}, //
        {"uninstall", &uninstall_libraries, List$info(&Text$info), .short_flag = 'u'}, //
        {"verbose", &verbose, &Bool$info, .short_flag = 'v'}, //
        {"install", &should_install, &Bool$info, .short_flag = 'I'}, //
        {"prefix", &show_prefix, &Bool$info}, //
        {"quiet", &quiet, &Bool$info, .short_flag = 'q'}, //
        {"version", &show_version, &Bool$info, .short_flag = 'V'}, //
        {"show-codegen", &show_codegen, &Text$info, .short_flag = 'C'}, //
        {"optimization", &optimization, &Text$info, .short_flag = 'O'}, //
        {"force-rebuild", &clean_build, &Bool$info, .short_flag = 'f'}, //
        {"source-mapping", &source_mapping, &Bool$info, .short_flag = 'm'},
        {"changelog", &show_changelog, &Bool$info}, //
    };

    tomo_parse_args(argc, argv, usage, help, TOMO_VERSION, sizeof(tomo_args) / sizeof(tomo_args[0]), tomo_args);
    if (show_prefix) {
        print(TOMO_PATH);
        return 0;
    }

    if (show_changelog) {
        print_inline(string_slice((const char *)CHANGES_md, (size_t)CHANGES_md_len));
        return 0;
    }

    if (show_version) {
        if (verbose) print(TOMO_VERSION, " ", GIT_VERSION);
        else print(TOMO_VERSION);
        return 0;
    }

    bool is_gcc = (system(String(cc, " -v 2>&1 | grep -q 'gcc version'")) == 0);
    if (is_gcc) {
        cflags = Texts(cflags, Text(" -fsanitize=signed-integer-overflow -fno-sanitize-recover"
                                    " -fno-signaling-nans -fno-trapping-math -fno-finite-math-only"));
    }

    bool is_clang = (system(String(cc, " -v 2>&1 | grep -q 'clang version'")) == 0);
    if (is_clang) {
        cflags = Texts(cflags, Text(" -Wno-parentheses-equality"));
    }

    ldflags = Texts("-Wl,-rpath,'", TOMO_PATH, "/lib' ", ldflags);

#ifdef __APPLE__
    cflags = Texts(cflags, Text(" -I/opt/homebrew/include"));
    ldflags = Texts(ldflags, Text(" -L/opt/homebrew/lib -Wl,-rpath,/opt/homebrew/lib"));
#endif

    if (show_codegen.length > 0 && Text$equal_values(show_codegen, Text("pretty")))
        show_codegen = Text("{ sed '/^#line/d;/^$/d' | clang-format | bat -l c -P; }");

    config_summary = Texts("TOMO_VERSION=", TOMO_VERSION, "\n", "COMPILER=", cc, " ", cflags, " -O", optimization, "\n",
                           "SOURCE_MAPPING=", source_mapping ? Text("yes") : Text("no"), "\n");

    Text_t owner = Path$owner(Path$from_str(TOMO_PATH), true);
    Text_t user = Text$from_str(getenv("USER"));
    if (!Text$equal_values(user, owner)) {
        as_owner = Texts(Text(SUDO " -u "), owner, Text(" "));
    }

    // Uninstall libraries:
    for (int64_t i = 0; i < (int64_t)uninstall_libraries.length; i++) {
        Text_t *u = (Text_t *)(uninstall_libraries.data + i * uninstall_libraries.stride);
        xsystem(as_owner, "rm -rvf '", TOMO_PATH, "'/lib/tomo_" TOMO_VERSION "/", *u, " '", TOMO_PATH, "'/bin/", *u,
                " '", TOMO_PATH, "'/man/man1/", *u, ".1");
        print("Uninstalled ", *u);
    }

    // Build (and install) libraries
    Path_t cwd = Path$current_dir();
    for (int64_t i = 0; i < (int64_t)libraries.length; i++) {
        Path_t *lib = (Path_t *)(libraries.data + i * libraries.stride);
        *lib = Path$resolved(*lib, cwd);
        // Fork a child process to build the library to prevent cross-contamination
        // of side effects when building one library from affecting another library.
        // This *could* be done in parallel, but there may be some dependency issues.
        pid_t child = fork();
        if (child == 0) {
            if (Text$equal_values(Path$extension(*lib, false), Text("ini"))) {
                if (!install_from_modules_ini(*lib, false)) {
                    print("Failed to install modules from file: ", *lib);
                    _exit(1);
                }
            } else {
                build_library(*lib);
                if (should_install) install_library(*lib);
            }
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
        print(formatted);
    }

    format_files_inplace = normalize_tm_paths(format_files_inplace);
    for (int64_t i = 0; i < (int64_t)format_files.length; i++) {
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

            Path_t exe_path = Path$with_extension(path, Text(""), true);
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

    // Compile runnable files in parallel, then execute in serial:
    for (int64_t i = 0; i < (int64_t)run_files.length; i++) {
        Path_t path = *(Path_t *)(run_files.data + i * run_files.stride);
        Path_t exe_path = build_file(Path$with_extension(path, Text(""), true), "");
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
        Path_t exe_path = build_file(Path$with_extension(path, Text(""), true), "");
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

Path_t build_file(Path_t path, const char *extension) {
    Path_t build_dir = Path$sibling(path, Text(".build"));
    if (mkdir(Path$as_c_string(build_dir), 0755) != 0) {
        if (!Path$is_directory(build_dir, true)) err(1, "Could not make .build directory");
    }
    return Path$child(build_dir, Texts(Path$base_name(path), Text$from_str(extension)));
}

void build_library(Path_t lib_dir) {
    lib_dir = Path$resolved(lib_dir, Path$current_dir());
    if (!Path$is_directory(lib_dir, true)) print_err("Not a valid directory: ", lib_dir);

    List_t tm_files = Path$glob(Path$child(lib_dir, Text("[!._0-9]*.tm")));
    env_t *env = fresh_scope(global_env(source_mapping));
    List_t object_files = EMPTY_LIST, extra_ldlibs = EMPTY_LIST;

    compile_files(env, tm_files, &object_files, &extra_ldlibs, COMPILE_OBJ);

    Text_t lib_name = get_library_name(lib_dir);
    Path_t shared_lib = Path$child(lib_dir, Texts(Text("lib"), lib_name, Text(SHARED_SUFFIX)));
    if (!is_stale_for_any(shared_lib, object_files, false)) {
        if (verbose) whisper("Unchanged: ", shared_lib);
        return;
    }

    FILE *prog = run_cmd(cc, " -O", optimization, " ", cflags, " ", ldflags, " ", ldlibs, " ", list_text(extra_ldlibs),
#ifdef __APPLE__
                         " -Wl,-install_name,@rpath/'lib", lib_name, SHARED_SUFFIX,
                         "'"
#else
                         " -Wl,-soname,'lib", lib_name, SHARED_SUFFIX,
                         "'"
#endif
                         " -shared ",
                         paths_str(object_files), " -o '", shared_lib, "'");

    if (!prog) print_err("Failed to run C compiler: ", cc);
    int status = pclose(prog);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) exit(EXIT_FAILURE);

    if (!quiet) print("Compiled library:\t", Path$relative_to(shared_lib, Path$current_dir()));
}

void install_library(Path_t lib_dir) {
    Text_t lib_name = get_library_name(lib_dir);
    Path_t dest = Path$child(Path$from_str(String(TOMO_PATH, "/lib/tomo_" TOMO_VERSION)), lib_name);
    print("Installing ", lib_dir, " into ", dest);
    if (!Path$equal_values(lib_dir, dest)) {
        if (verbose) whisper("Clearing out any pre-existing version of ", lib_name);
        xsystem(as_owner, "rm -rf '", dest, "'");
        if (verbose) whisper("Moving files to ", dest);
        xsystem(as_owner, "mkdir -p '", dest, "'");
        xsystem(as_owner, "cp -r '", lib_dir, "'/* '", dest, "/'");
        xsystem(as_owner, "cp -r '", lib_dir, "'/.build '", dest, "/'");
    }
    // If we have `debugedit` on this system, use it to remap the debugging source information
    // to point to the installed version of the source file. Otherwise, fail silently.
    if (verbose) whisper("Updating debug symbols for ", dest, "/lib", lib_name, SHARED_SUFFIX);
    int result = system(String(as_owner, "debugedit -b ", lib_dir, " -d '", dest,
                               "'"
                               " '",
                               dest, "/lib", lib_name, SHARED_SUFFIX,
                               "' "
                               ">/dev/null 2>/dev/null"));
    (void)result;
    print("Installed \033[1m", lib_dir, "\033[m to ", TOMO_PATH, "/lib/tomo_" TOMO_VERSION "/", lib_name);
}

void compile_files(env_t *env, List_t to_compile, List_t *object_files, List_t *extra_ldlibs, compile_mode_t mode) {
    Table_t to_link = EMPTY_TABLE;
    Table_t dependency_files = EMPTY_TABLE;
    for (int64_t i = 0; i < (int64_t)to_compile.length; i++) {

        Path_t filename = *(Path_t *)(to_compile.data + i * to_compile.stride);
        Text_t extension = Path$extension(filename, true);
        if (!Text$equal_values(extension, Text("tm")))
            print_err("Not a valid .tm file: \x1b[31;1m", filename, "\x1b[m");
        if (!Path$is_file(filename, true)) print_err("Couldn't find file: ", filename);
        build_file_dependency_graph(filename, &dependency_files, &to_link);
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

void build_file_dependency_graph(Path_t path, Table_t *to_compile, Table_t *to_link) {
    if (Table$has_value(*to_compile, path, Table$info(&Path$info, &Byte$info))) return;

    staleness_t staleness = {
        .h = is_stale(build_file(path, ".h"), Path$sibling(path, Text("modules.ini")), true)
             || is_stale(build_file(path, ".h"), build_file(path, ":modules.ini"), true)
             || is_stale(build_file(path, ".h"), path, false)
             || is_stale(build_file(path, ".h"), build_file(path, ".id"), false),
        .c = is_stale(build_file(path, ".c"), Path$sibling(path, Text("modules.ini")), true)
             || is_stale(build_file(path, ".c"), build_file(path, ":modules.ini"), true)
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
            build_file_dependency_graph(dep_tm, to_compile, to_link);
            break;
        }
        case USE_MODULE: {
            module_info_t mod = get_used_module_info(stmt_ast);
            const char *full_name = mod.version ? String(mod.name, "_", mod.version) : mod.name;
            Text_t lib = Texts("-Wl,-rpath,'", TOMO_PATH, "/lib/tomo_" TOMO_VERSION "/", Text$from_str(full_name),
                               "' '", TOMO_PATH, "/lib/tomo_" TOMO_VERSION "/", Text$from_str(full_name), "/lib",
                               Text$from_str(full_name), SHARED_SUFFIX "'");
            Table$set(to_link, &lib, NULL, Table$info(&Text$info, &Void$info));

            List_t children =
                Path$glob(Path$from_str(String(TOMO_PATH, "/lib/tomo_" TOMO_VERSION "/", full_name, "/[!._0-9]*.tm")));
            for (int64_t i = 0; i < (int64_t)children.length; i++) {
                Path_t *child = (Path_t *)(children.data + i * children.stride);
                Table_t discarded = {.entries = EMPTY_LIST, .fallback = to_compile};
                build_file_dependency_graph(*child, &discarded, to_link);
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

    const char *version = get_library_version(Path$parent(path));
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
                                 compile_cli_arg_call(module_env, ast, main_binding->code, main_binding->type, version),
                                 "return 0;\n"
                                 "}\n"));
    }

    if (fclose(c_file) == -1) print_err("Failed to output C code to ", c_filename);

    if (!quiet) print("Transpiled code:\t", Path$relative_to(c_filename, Path$current_dir()));

    if (show_codegen.length > 0) xsystem(show_codegen, " <", c_filename);
}

void compile_object_file(Path_t path) {
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
    ast_t *ast = parse_file(Path$as_c_string(path), NULL);
    if (!ast) print_err("Could not parse file ", path);
    env_t *env = load_module_env(base_env, ast);
    binding_t *main_binding = get_binding(env, "main");
    if (!main_binding || main_binding->type->tag != FunctionType)
        print_err("No main() function has been defined for ", path, ", so it can't be run!");

    Path_t manpage_file = build_file(Path$with_extension(path, Text(".1"), true), "");
    if (clean_build || !Path$is_file(manpage_file, true) || is_stale(manpage_file, path, true)) {
        Text_t manpage = compile_manpage(Path$base_name(exe_path), ast, Match(main_binding->type, FunctionType)->args);
        Path$write(manpage_file, manpage, 0644);
        if (!quiet) print("Wrote manpage:\t", Path$relative_to(manpage_file, Path$current_dir()));
    } else {
        if (verbose) whisper("Unchanged: ", manpage_file);
    }

    if (!clean_build && Path$is_file(exe_path, true) && !is_config_outdated(path)
        && !is_stale_for_any(exe_path, object_files, false)
        && !is_stale(exe_path, Path$sibling(path, Text("modules.ini")), true)
        && !is_stale(exe_path, build_file(path, ":modules.ini"), true)) {
        if (verbose) whisper("Unchanged: ", exe_path);
        return exe_path;
    }

    Text_t program = Texts("extern int parse_and_run$$", main_binding->code,
                           "(int argc, char *argv[]);\n"
                           "__attribute__ ((noinline))\n"
                           "int main(int argc, char *argv[]) {\n"
                           "\treturn parse_and_run$$",
                           main_binding->code,
                           "(argc, argv);\n"
                           "}\n");
    Path_t runner_file = build_file(path, ".runner.c");
    Path$write(runner_file, program, 0644);

    FILE *runner = run_cmd(cc, " ", cflags, " -O", optimization, " ", ldflags, " ", ldlibs, " ",
                           list_text(extra_ldlibs), " ", paths_str(object_files), " ", runner_file, " -o ", exe_path);

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
