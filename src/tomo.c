// The main program that runs compilation
#include <ctype.h>
#include <errno.h>
#include <gc.h>
#include <gc/cord.h>
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
#include "compile.h"
#include "cordhelpers.h"
#include "modules.h"
#include "parse.h"
#include "stdlib/bools.h"
#include "stdlib/bytes.h"
#include "stdlib/datatypes.h"
#include "stdlib/integers.h"
#include "stdlib/lists.h"
#include "stdlib/optionals.h"
#include "stdlib/paths.h"
#include "stdlib/print.h"
#include "stdlib/random.h"
#include "stdlib/siphash.h"
#include "stdlib/text.h"
#include "typecheck.h"
#include "types.h"

#define run_cmd(...) ({ const char *_cmd = String(__VA_ARGS__); if (verbose) print("\033[34;1m", _cmd, "\033[m"); popen(_cmd, "w"); })
#define xsystem(...) ({ int _status = system(String(__VA_ARGS__)); if (!WIFEXITED(_status) || WEXITSTATUS(_status) != 0) errx(1, "Failed to run command: %s", String(__VA_ARGS__)); })
#define list_text(list) Text$join(Text(" "), list)

#define whisper(...) print("\033[2m", __VA_ARGS__, "\033[m")

#ifdef __linux__
// Only on Linux is /proc/self/exe available
static struct stat compiler_stat;
#endif

static const char *paths_str(List_t paths) {
    Text_t result = EMPTY_TEXT;
    for (int64_t i = 0; i < paths.length; i++) {
        if (i > 0) result = Texts(result, Text(" "));
        result = Texts(result, Path$as_text((Path_t*)(paths.data + i*paths.stride), false, &Path$info));
    }
    return Text$as_c_string(result);
}

#ifdef __APPLE__
#define SHARED_SUFFIX ".dylib"
#else
#define SHARED_SUFFIX ".so"
#endif

static OptionalList_t files = NONE_LIST,
                       args = NONE_LIST,
                       uninstall = NONE_LIST,
                       libraries = NONE_LIST;
static OptionalBool_t verbose = false,
                      quiet = false,
                      show_parse_tree = false,
                      show_prefix = false,
                      stop_at_transpile = false,
                      stop_at_obj_compilation = false,
                      compile_exe = false,
                      should_install = false,
                      clean_build = false,
                      source_mapping = true;

static OptionalText_t 
            show_codegen = NONE_TEXT,
            cflags = Text("-Werror -fdollars-in-identifiers -std=c2x -Wno-trigraphs "
                          " -ffunction-sections -fdata-sections"
                          " -fno-signed-zeros -fno-finite-math-only "
                          " -D_XOPEN_SOURCE -D_DEFAULT_SOURCE -fPIC -ggdb"
#if defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || defined(__APPLE__)
                          " -D_BSD_SOURCE"
#endif
                          " -DGC_THREADS"
                          " -I'" TOMO_PREFIX "/include' -I'" TOMO_PREFIX "/share/tomo_"TOMO_VERSION"/installed' -I/usr/local/include"),
            ldlibs = Text("-lgc -lm -lgmp -lmpdec -lunistring -ltomo_"TOMO_VERSION),
            ldflags = Text("-Wl,-rpath,'"TOMO_PREFIX"/lib',-rpath,/usr/local/lib"
                           " -L/usr/local/lib"),
            optimization = Text("2"),
            cc = Text(DEFAULT_C_COMPILER);

static Text_t config_summary,
              // This will be either "" or "sudo -u <user>" or "doas -u <user>"
              // to allow a command to put stuff into TOMO_PREFIX as the owner
              // of that directory.
              as_owner = Text("");

static void transpile_header(env_t *base_env, Path_t path);
static void transpile_code(env_t *base_env, Path_t path);
static void compile_object_file(Path_t path);
static Path_t compile_executable(env_t *base_env, Path_t path, Path_t exe_path, List_t object_files, List_t extra_ldlibs);
static void build_file_dependency_graph(Path_t path, Table_t *to_compile, Table_t *to_link);
static void build_library(Path_t lib_dir);
static void install_library(Path_t lib_dir);
static void compile_files(env_t *env, List_t files, List_t *object_files, List_t *ldlibs);
static bool is_stale(Path_t path, Path_t relative_to, bool ignore_missing);
static bool is_stale_for_any(Path_t path, List_t relative_to, bool ignore_missing);
static Path_t build_file(Path_t path, const char *extension);
static void wait_for_child_success(pid_t child);
static bool is_config_outdated(Path_t path);

typedef struct {
    bool h:1, c:1, o:1;
} staleness_t;

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstack-protector"
#endif
int main(int argc, char *argv[])
{
#ifdef __linux__
    // Get the file modification time of the compiler, so we
    // can recompile files after changing the compiler:
    char compiler_path[PATH_MAX];
    ssize_t count = readlink("/proc/self/exe", compiler_path, PATH_MAX);
    if (count == -1)
        err(1, "Could not find age of compiler");
    compiler_path[count] = '\0';
    if (stat(compiler_path, &compiler_stat) != 0)
        err(1, "Could not find age of compiler");
#endif

#ifdef __OpenBSD__
    ldlibs = Texts(ldlibs, Text(" -lexecinfo"));
#endif

    USE_COLOR = getenv("COLOR") ? strcmp(getenv("COLOR"), "1") == 0 : isatty(STDOUT_FILENO);
    if (getenv("NO_COLOR") && getenv("NO_COLOR")[0] != '\0')
        USE_COLOR = false;

#if defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || defined(__APPLE__)
    arc4random_buf(TOMO_HASH_KEY, sizeof(TOMO_HASH_KEY), 0);
#elif defined(__linux__)
    assert(getrandom(TOMO_HASH_KEY, sizeof(TOMO_HASH_KEY), 0) == sizeof(TOMO_HASH_KEY));
#else
    #error "Unsupported platform for secure random number generation"
#endif

    // Set up environment variables:
    const char *PATH = getenv("PATH");
    setenv("PATH", PATH ? String(TOMO_PREFIX"/bin:", PATH) : TOMO_PREFIX"/bin", 1);
    const char *LD_LIBRARY_PATH = getenv("LD_LIBRARY_PATH");
    setenv("LD_LIBRARY_PATH", LD_LIBRARY_PATH ? String(TOMO_PREFIX"/lib:", LD_LIBRARY_PATH) : TOMO_PREFIX"/lib", 1);
    const char *LIBRARY_PATH = getenv("LIBRARY_PATH");
    setenv("LIBRARY_PATH", LIBRARY_PATH ? String(TOMO_PREFIX"/lib:", LIBRARY_PATH) : TOMO_PREFIX"/lib", 1);
    const char *C_INCLUDE_PATH = getenv("C_INCLUDE_PATH");
    setenv("C_INCLUDE_PATH", C_INCLUDE_PATH ? String(TOMO_PREFIX"/include:", C_INCLUDE_PATH) : TOMO_PREFIX"/include", 1);

    // Run a tool:
    if ((streq(argv[1], "-r") || streq(argv[1], "--run")) && argc >= 3) {
        if (strcspn(argv[2], "/;$") == strlen(argv[2])) {
            const char *program = String("'"TOMO_PREFIX"'/share/tomo_"TOMO_VERSION"/installed/", argv[2], "/", argv[2]);
            execv(program, &argv[2]);
        }
        print_err("This is not an installed tomo program: ", argv[2]);
    }

    Text_t usage = Text("\x1b[33;4;1mUsage:\x1b[m\n"
                        "\x1b[1mRun a program:\x1b[m         tomo file.tm [-- args...]\n"
                        "\x1b[1mTranspile files:\x1b[m       tomo -t file.tm...\n"
                        "\x1b[1mCompile object files:\x1b[m  tomo -c file.tm...\n"
                        "\x1b[1mCompile executables:\x1b[m   tomo -e file.tm...\n"
                        "\x1b[1mBuild libraries:\x1b[m       tomo -L lib...\n"
                        "\x1b[1mUninstall libraries:\x1b[m   tomo -u lib...\n"
                        "\x1b[1mOther flags:\x1b[m\n"
                        "  --verbose|-v: verbose output\n"
                        "  --quiet|-q: quiet output\n"
                        "  --parse|-p: show parse tree\n"
                        "  --install|-I: install the executable or library\n"
                        "  --optimization|-O <level>: set optimization level\n"
                        "  --run|-r: run a program from " TOMO_PREFIX "/share/tomo_"TOMO_VERSION"/installed\n"
                        );
    Text_t help = Texts(Text("\x1b[1mtomo\x1b[m: a compiler for the Tomo programming language"), Text("\n\n"), usage);
    tomo_parse_args(
        argc, argv, usage, help, TOMO_VERSION,
        {"files", true, List$info(&Path$info), &files},
        {"args", true, List$info(&Text$info), &args},
        {"verbose", false, &Bool$info, &verbose},
        {"v", false, &Bool$info, &verbose},
        {"parse", false, &Bool$info, &show_parse_tree},
        {"p", false, &Bool$info, &show_parse_tree},
        {"prefix", false, &Bool$info, &show_prefix},
        {"quiet", false, &Bool$info, &quiet},
        {"q", false, &Bool$info, &quiet},
        {"transpile", false, &Bool$info, &stop_at_transpile},
        {"t", false, &Bool$info, &stop_at_transpile},
        {"compile-obj", false, &Bool$info, &stop_at_obj_compilation},
        {"c", false, &Bool$info, &stop_at_obj_compilation},
        {"compile-exe", false, &Bool$info, &compile_exe},
        {"e", false, &Bool$info, &compile_exe},
        {"uninstall", false, List$info(&Text$info), &uninstall},
        {"u", false, List$info(&Text$info), &uninstall},
        {"library", false, List$info(&Path$info), &libraries},
        {"L", false, List$info(&Path$info), &libraries},
        {"show-codegen", false, &Text$info, &show_codegen},
        {"C", false, &Text$info, &show_codegen},
        {"install", false, &Bool$info, &should_install},
        {"I", false, &Bool$info, &should_install},
        {"optimization", false, &Text$info, &optimization},
        {"O", false, &Text$info, &optimization},
        {"force-rebuild", false, &Bool$info, &clean_build},
        {"f", false, &Bool$info, &clean_build},
        {"source-mapping", false, &Bool$info, &source_mapping},
        {"m", false, &Bool$info, &source_mapping},
    );

    if (show_prefix) {
        print(TOMO_PREFIX);
        return 0;
    }

    bool is_gcc = (system(String(cc, " -v 2>&1 | grep -q 'gcc version'")) == 0);
    if (is_gcc) {
        cflags = Texts(cflags, Text(" -fsanitize=signed-integer-overflow -fno-sanitize-recover"
                                    " -fno-signaling-nans -fno-trapping-math"));
    }

    bool is_clang = (system(String(cc, " -v 2>&1 | grep -q 'clang version'")) == 0);
    if (is_clang) {
        cflags = Texts(cflags, Text(" -Wno-parentheses-equality"));
    }

#ifdef __APPLE__
    cflags = Texts(cflags, Text(" -I/opt/homebrew/include"));
    ldflags = Texts(ldflags, Text(" -L/opt/homebrew/lib -Wl,-rpath,/opt/homebrew/lib"));
#endif

    if (show_codegen.length > 0 && Text$equal_values(show_codegen, Text("pretty")))
        show_codegen = Text("{ sed '/^#line/d;/^$/d' | indent -o /dev/stdout | bat -l c -P; }");

    config_summary = Text$from_str(String(cc, " ", cflags, " -O", optimization));

    Text_t owner = Path$owner(Path$from_str(TOMO_PREFIX), true);
    Text_t user = Text$from_str(getenv("USER"));
    if (!Text$equal_values(user, owner)) {
        as_owner = Texts(Text(SUDO" -u "), owner, Text(" "));
    }

    for (int64_t i = 0; i < uninstall.length; i++) {
        Text_t *u = (Text_t*)(uninstall.data + i*uninstall.stride);
        xsystem(as_owner, "rm -rvf '"TOMO_PREFIX"'/share/tomo_"TOMO_VERSION"/installed/", *u);
        print("Uninstalled ", *u);
    }

    Path_t cwd = Path$current_dir();
    for (int64_t i = 0; i < libraries.length; i++) {
        Path_t *lib = (Path_t*)(libraries.data + i*libraries.stride);
        *lib = Path$resolved(*lib, cwd);
        // Fork a child process to build the library to prevent cross-contamination
        // of side effects when building one library from affecting another library.
        // This *could* be done in parallel, but there may be some dependency issues.
        pid_t child = fork();
        if (child == 0) {
            build_library(*lib);
            _exit(0);
        }
        wait_for_child_success(child);
        if (should_install)
            install_library(*lib);
    }

    if (files.length <= 0 && uninstall.length <= 0 && libraries.length <= 0) {
        fprint(stderr, "No files provided!\n\n", usage);
        return 1;
    }

    if (files.length <= 0 && (uninstall.length > 0 || libraries.length > 0)) {
        return 0;
    }

    // Convert `foo` to `foo/foo.tm` and resolve all paths to absolute paths:
    Path_t cur_dir = Path$current_dir();
    for (int64_t i = 0; i < files.length; i++) {
        Path_t *path = (Path_t*)(files.data + i*files.stride);
        if (Path$is_directory(*path, true))
            *path = Path$child(*path, Texts(Path$base_name(*path), Text(".tm")));

        *path = Path$resolved(*path, cur_dir);
        if (!Path$exists(*path))
            fail("File not found: ", *path);
    }

    if (files.length < 1)
        print_err("No file specified!");

    quiet = !verbose;

    for (int64_t i = 0; i < files.length; i++) {
        Path_t path = *(Path_t*)(files.data + i*files.stride);
        if (show_parse_tree) {
            ast_t *ast = parse_file(Path$as_c_string(path), NULL);
            print(ast_to_sexp_str(ast));
            continue;
        }

        Path_t exe_path = compile_exe ? Path$with_extension(path, Text(""), true)
            : build_file(Path$with_extension(path, Text(""), true), "");

        pid_t child = fork();
        if (child == 0) {
            env_t *env = global_env(source_mapping);
            List_t object_files = {},
                    extra_ldlibs = {};
            compile_files(env, files, &object_files, &extra_ldlibs);
            compile_executable(env, path, exe_path, object_files, extra_ldlibs);

            if (compile_exe)
                _exit(0);

            char *prog_args[1 + args.length + 1];
            prog_args[0] = (char*)Path$as_c_string(exe_path);
            for (int64_t j = 0; j < args.length; j++)
                prog_args[j + 1] = Text$as_c_string(*(Text_t*)(args.data + j*args.stride));
            prog_args[1 + args.length] = NULL;
            execv(prog_args[0], prog_args);
            print_err("Could not execute program: ", prog_args[0]);
        }

        wait_for_child_success(child);
    }

    if (compile_exe && should_install) {
        for (int64_t i = 0; i < files.length; i++) {
            Path_t path = *(Path_t*)(files.data + i*files.stride);
            Path_t exe = Path$with_extension(path, Text(""), true);
            xsystem(as_owner, "cp -v '", exe, "' '"TOMO_PREFIX"'/bin/");
        }
    }
    return 0;
}
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

void wait_for_child_success(pid_t child)
{
    int status;
    while (waitpid(child, &status, 0) < 0 && errno == EINTR) {
        if (WIFEXITED(status) || WIFSIGNALED(status))
            break;
        else if (WIFSTOPPED(status))
            kill(child, SIGCONT);
    }

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        _exit(WIFEXITED(status) ? WEXITSTATUS(status) : EXIT_FAILURE);
    }
}

Path_t build_file(Path_t path, const char *extension)
{
    Path_t build_dir = Path$sibling(path, Text(".build"));
    if (mkdir(Path$as_c_string(build_dir), 0755) != 0) {
        if (!Path$is_directory(build_dir, true))
            err(1, "Could not make .build directory");
    }
    return Path$child(build_dir, Texts(Path$base_name(path), Text$from_str(extension)));
}

static const char *get_version(Path_t lib_dir)
{
    Path_t changes_file = Path$child(lib_dir, Text("CHANGES.md"));
    OptionalText_t changes = Path$read(changes_file);
    if (changes.length <= 0) {
        return "v0.0";
    }
    const char *changes_str = Text$as_c_string(Texts(Text("\n"), changes));
    const char *version_line = strstr(changes_str, "\n## ");
    if (version_line == NULL)
        print_err("CHANGES.md in ", lib_dir, " does not have any valid versions starting with '## '");
    return String(string_slice(version_line + 4, strcspn(version_line + 4, "\r\n")));
}

static Text_t get_version_suffix(Path_t lib_dir)
{
    return Texts(Text("_"), Text$from_str(get_version(lib_dir)));
}

void build_library(Path_t lib_dir)
{
    lib_dir = Path$resolved(lib_dir, Path$current_dir());
    if (!Path$is_directory(lib_dir, true))
        print_err("Not a valid directory: ", lib_dir);

    Text_t lib_dir_name = Path$base_name(lib_dir);
    List_t tm_files = Path$glob(Path$child(lib_dir, Text("[!._0-9]*.tm")));
    env_t *env = fresh_scope(global_env(source_mapping));
    List_t object_files = {},
           extra_ldlibs = {};

    compile_files(env, tm_files, &object_files, &extra_ldlibs);

    Text_t version_suffix = get_version_suffix(lib_dir);
    Path_t shared_lib = Path$child(lib_dir, Texts(Text("lib"), lib_dir_name, version_suffix, Text(SHARED_SUFFIX)));
    if (!is_stale_for_any(shared_lib, object_files, false)) {
        if (verbose) whisper("Unchanged: ", shared_lib);
        return;
    }

    FILE *prog = run_cmd(cc, " -O", optimization, " ", cflags, " ", ldflags, " ", ldlibs, " ", list_text(extra_ldlibs),
#ifdef __APPLE__
                   " -Wl,-install_name,@rpath/'lib", lib_dir_name, version_suffix, SHARED_SUFFIX, "'"
#else
                   " -Wl,-soname,'lib", lib_dir_name, version_suffix, SHARED_SUFFIX, "'"
#endif
                   " -shared ", paths_str(object_files), " -o '", shared_lib, "'");

    if (!prog)
        print_err("Failed to run C compiler: ", cc);
    int status = pclose(prog);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        exit(EXIT_FAILURE);

    if (!quiet)
        print("Compiled library:\t", shared_lib);
}

void install_library(Path_t lib_dir)
{
    Text_t lib_dir_name = Path$base_name(lib_dir);
    Text_t version_suffix = get_version_suffix(lib_dir);
    Path_t dest = Path$child(Path$from_str(TOMO_PREFIX"/share/tomo_"TOMO_VERSION"/installed"), Texts(lib_dir_name, version_suffix));
    if (!Path$equal_values(lib_dir, dest)) {
        if (verbose) whisper("Clearing out any pre-existing version of ", lib_dir_name);
        xsystem(as_owner, "rm -rf '", dest, "'");
        if (verbose) whisper("Moving files to ", dest);
        xsystem(as_owner, "mkdir -p '", dest, "'");
        xsystem(as_owner, "cp -r '", lib_dir, "'/* '", dest, "/'");
        xsystem(as_owner, "cp -r '", lib_dir, "'/.build '", dest, "/'");
    }
    // If we have `debugedit` on this system, use it to remap the debugging source information
    // to point to the installed version of the source file. Otherwise, fail silently.
    if (verbose) whisper("Updating debug symbols for ", dest, "/lib", lib_dir_name, SHARED_SUFFIX);
    int result = system(String(as_owner, "debugedit -b ", lib_dir,
                               " -d '", dest, "'"
                               " '", dest, "/lib", lib_dir_name, version_suffix, SHARED_SUFFIX, "'"
));
    (void)result;
    print("Installed \033[1m", lib_dir_name, "\033[m to "TOMO_PREFIX"/share/tomo_"TOMO_VERSION"/installed/", lib_dir_name, version_suffix);
}

void compile_files(env_t *env, List_t to_compile, List_t *object_files, List_t *extra_ldlibs)
{
    Table_t to_link = {};
    Table_t dependency_files = {};
    for (int64_t i = 0; i < to_compile.length; i++) {
        Path_t filename = *(Path_t*)(to_compile.data + i*to_compile.stride);
        Text_t extension = Path$extension(filename, true);
        if (!Text$equal_values(extension, Text("tm")))
            print_err("Not a valid .tm file: \x1b[31;1m", filename, "\x1b[m");
        if (!Path$is_file(filename, true))
            print_err("Couldn't find file: ", filename);
        build_file_dependency_graph(filename, &dependency_files, &to_link);
    }

    // Make sure all files and dependencies have a .id file:
    for (int64_t i = 0; i < dependency_files.entries.length; i++) {
        struct {
            Path_t filename;
            staleness_t staleness;
        } *entry = (dependency_files.entries.data + i*dependency_files.entries.stride);

        Path_t id_file = build_file(entry->filename, ".id");
        if (!Path$exists(id_file)) {
            static const char id_chars[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
            char id_str[8]; 
            for (int j = 0; j < (int)sizeof(id_str); j++) {
                id_str[j] = id_chars[random_range(0, sizeof(id_chars)-1)];
            }
            Text_t filename_id = Text("");
            Text_t base = Path$base_name(entry->filename);
            TextIter_t state = NEW_TEXT_ITER_STATE(base);
            for (int64_t j = 0; j < base.length; j++) {
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
    for (int64_t i = 0; i < dependency_files.entries.length; i++) {
        struct {
            Path_t filename;
            staleness_t staleness;
        } *entry = (dependency_files.entries.data + i*dependency_files.entries.stride);

        if (entry->staleness.h || clean_build) {
            transpile_header(env, entry->filename);
            entry->staleness.o = true;
        } else {
            if (verbose) whisper("Unchanged: ", build_file(entry->filename, ".h"));
            if (show_codegen.length > 0)
                xsystem(show_codegen, " <", build_file(entry->filename, ".h"));
        }
    }

    env->imports = new(Table_t);

    struct child_s {
        struct child_s *next;
        pid_t pid;
    } *child_processes = NULL;

    // (Re)transpile and compile object files, eagerly for files explicitly
    // specified and lazily for downstream dependencies:
    for (int64_t i = 0; i < dependency_files.entries.length; i++) {
        struct {
            Path_t filename;
            staleness_t staleness;
        } *entry = (dependency_files.entries.data + i*dependency_files.entries.stride);
        if (!clean_build && !entry->staleness.c && !entry->staleness.h && !entry->staleness.o
            && !is_config_outdated(entry->filename)) {
            if (verbose) whisper("Unchanged: ", build_file(entry->filename, ".c"));
            if (show_codegen.length > 0)
                xsystem(show_codegen, " <", build_file(entry->filename, ".c"));
            if (verbose) whisper("Unchanged: ", build_file(entry->filename, ".o"));
            continue;
        }

        pid_t pid = fork();
        if (pid == 0) {
            if (clean_build || entry->staleness.c)
                transpile_code(env, entry->filename);
            else if (verbose) whisper("Unchanged: ", build_file(entry->filename, ".c"));
            if (!stop_at_transpile)
                compile_object_file(entry->filename);
            _exit(EXIT_SUCCESS);
        }
        child_processes = new(struct child_s, .next=child_processes, .pid=pid);
    }

    for (; child_processes; child_processes = child_processes->next)
        wait_for_child_success(child_processes->pid);

    if (object_files) {
        for (int64_t i = 0; i < dependency_files.entries.length; i++) {
            struct {
                Path_t filename;
                staleness_t staleness;
            } *entry = (dependency_files.entries.data + i*dependency_files.entries.stride);
            Path_t path = entry->filename;
            path = build_file(path, ".o");
            List$insert(object_files, &path, I(0), sizeof(Path_t));
        }
    }
    if (extra_ldlibs) {
        for (int64_t i = 0; i < to_link.entries.length; i++) {
            Text_t lib = *(Text_t*)(to_link.entries.data + i*to_link.entries.stride);
            List$insert(extra_ldlibs, &lib, I(0), sizeof(Text_t));
        }
    }
}

bool is_config_outdated(Path_t path)
{
    OptionalText_t config = Path$read(build_file(path, ".config"));
    if (config.length < 0) return true;
    return !Text$equal_values(config, config_summary);
}

void build_file_dependency_graph(Path_t path, Table_t *to_compile, Table_t *to_link)
{
    if (Table$has_value(*to_compile, path, Table$info(&Path$info, &Byte$info)))
        return;

    staleness_t staleness = {
        .h=is_stale(build_file(path, ".h"), Path$sibling(path, Text("modules.ini")), true)
            || is_stale(build_file(path, ".h"), build_file(path, ":modules.ini"), true)
            || is_stale(build_file(path, ".h"), path, false)
            || is_stale(build_file(path, ".h"), build_file(path, ".id"), false),
        .c=is_stale(build_file(path, ".c"), Path$sibling(path, Text("modules.ini")), true)
            || is_stale(build_file(path, ".c"), build_file(path, ":modules.ini"), true)
            || is_stale(build_file(path, ".c"), path, false)
            || is_stale(build_file(path, ".c"), build_file(path, ".id"), false),
    };
    staleness.o = staleness.c || staleness.h
        || is_stale(build_file(path, ".o"), build_file(path, ".c"), false)
        || is_stale(build_file(path, ".o"), build_file(path, ".h"), false);
    Table$set(to_compile, &path, &staleness, Table$info(&Path$info, &Byte$info));

    assert(Text$equal_values(Path$extension(path, true), Text("tm")));

    ast_t *ast = parse_file(Path$as_c_string(path), NULL);
    if (!ast)
        print_err("Could not parse file: ", path);

    for (ast_list_t *stmt = Match(ast, Block)->statements; stmt; stmt = stmt->next) {
        ast_t *stmt_ast = stmt->ast;
        if (stmt_ast->tag != Use) continue;
        DeclareMatch(use, stmt_ast, Use);

        switch (use->what) {
        case USE_LOCAL: {
            Path_t dep_tm = Path$resolved(Path$from_str(use->path), Path$parent(path));
            if (!Path$is_file(dep_tm, true))
                code_err(stmt_ast, "Not a valid file: ", dep_tm);
            if (is_stale(build_file(path, ".h"), dep_tm, false))
                staleness.h = true;
            if (is_stale(build_file(path, ".c"), dep_tm, false))
                staleness.c = true;
            if (staleness.c || staleness.h)
                staleness.o = true;
            Table$set(to_compile, &path, &staleness, Table$info(&Path$info, &Byte$info));
            build_file_dependency_graph(dep_tm, to_compile, to_link);
            break;
        }
        case USE_MODULE: {
            module_info_t mod = get_module_info(stmt_ast);
            const char *full_name = mod.version ? String(mod.name, "_", mod.version) : mod.name;
            Text_t lib = Texts(Text("-Wl,-rpath,'"),
                               Text(TOMO_PREFIX "/share/tomo_"TOMO_VERSION"/installed/"), Text$from_str(full_name),
                               Text("' '" TOMO_PREFIX "/share/tomo_"TOMO_VERSION"/installed/"),
                               Text$from_str(full_name), Text("/lib"), Text$from_str(full_name), Text(SHARED_SUFFIX "'"));
            Table$set(to_link, &lib, NULL, Table$info(&Text$info, &Void$info));

            List_t children = Path$glob(Path$from_str(String(TOMO_PREFIX"/share/tomo_"TOMO_VERSION"/installed/", full_name, "/[!._0-9]*.tm")));
            for (int64_t i = 0; i < children.length; i++) {
                Path_t *child = (Path_t*)(children.data + i*children.stride);
                Table_t discarded = {.fallback=to_compile};
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
            break;
        }
        default: case USE_HEADER: break;
        }
    }
}

bool is_stale(Path_t path, Path_t relative_to, bool ignore_missing)
{
    struct stat target_stat;
    if (stat(Path$as_c_string(path), &target_stat) != 0) {
        if (ignore_missing) return false;
        return true;
    }

#ifdef __linux__
    // Any file older than the compiler is stale:
    if (target_stat.st_mtime < compiler_stat.st_mtime)
        return true;
#endif

    struct stat relative_to_stat;
    if (stat(Path$as_c_string(relative_to), &relative_to_stat) != 0) {
        if (ignore_missing) return false;
        print_err("File doesn't exist: ", relative_to);
    }
    return target_stat.st_mtime < relative_to_stat.st_mtime;
}

bool is_stale_for_any(Path_t path, List_t relative_to, bool ignore_missing)
{
    for (int64_t i = 0; i < relative_to.length; i++) {
        Path_t r = *(Path_t*)(relative_to.data + i*relative_to.stride);
        if (is_stale(path, r, ignore_missing))
            return true;
    }
    return false;
}

void transpile_header(env_t *base_env, Path_t path)
{
    Path_t h_filename = build_file(path, ".h");
    ast_t *ast = parse_file(Path$as_c_string(path), NULL);
    if (!ast)
        print_err("Could not parse file: ", path);

    env_t *module_env = load_module_env(base_env, ast);

    CORD h_code = compile_file_header(module_env, Path$resolved(h_filename, Path$from_str(".")), ast);

    FILE *header = fopen(Path$as_c_string(h_filename), "w");
    if (!header)
        print_err("Failed to open header file: ", h_filename);
    CORD_put(h_code, header);
    if (fclose(header) == -1)
        print_err("Failed to write header file: ", h_filename);

    if (!quiet)
        print("Transpiled header:\t", h_filename);

    if (show_codegen.length > 0)
        xsystem(show_codegen, " <", h_filename);
}

void transpile_code(env_t *base_env, Path_t path)
{
    Path_t c_filename = build_file(path, ".c");
    ast_t *ast = parse_file(Path$as_c_string(path), NULL);
    if (!ast)
        print_err("Could not parse file: ", path);

    env_t *module_env = load_module_env(base_env, ast);

    CORD c_code = compile_file(module_env, ast);

    FILE *c_file = fopen(Path$as_c_string(c_filename), "w");
    if (!c_file)
        print_err("Failed to write C file: ", c_filename);

    CORD_put(c_code, c_file);

    const char *version = get_version(Path$parent(path));
    binding_t *main_binding = get_binding(module_env, "main");
    if (main_binding && main_binding->type->tag == FunctionType) {
        type_t *ret = Match(main_binding->type, FunctionType)->ret;
        if (ret->tag != VoidType && ret->tag != AbortType)
            compiler_err(ast->file, ast->start, ast->end,
                         "The main() function in this file has a return type of ", type_to_str(ret),
                         ", but it should not have any return value!");

        CORD_put(CORD_all(
            "int parse_and_run$$", main_binding->code, "(int argc, char *argv[]) {\n",
            module_env->do_source_mapping ? "#line 1\n" : CORD_EMPTY,
            "tomo_init();\n",
            namespace_name(module_env, module_env->namespace, "$initialize"), "();\n"
            "\n",
            compile_cli_arg_call(module_env, main_binding->code, main_binding->type, version),
            "return 0;\n"
            "}\n"), c_file);
    }

    if (fclose(c_file) == -1)
        print_err("Failed to output C code to ", c_filename);

    if (!quiet)
        print("Transpiled code:\t", c_filename);

    if (show_codegen.length > 0)
        xsystem(show_codegen, " <", c_filename);
}

void compile_object_file(Path_t path)
{
    Path_t obj_file = build_file(path, ".o");
    Path_t c_file = build_file(path, ".c");

    FILE *prog = run_cmd(cc, " ", cflags, " -O", optimization, " -c ", c_file, " -o ", obj_file);
    if (!prog)
        print_err("Failed to run C compiler: ", cc);
    int status = pclose(prog);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        exit(EXIT_FAILURE);

    Path$write(build_file(path, ".config"), config_summary, 0644);

    if (!quiet)
        print("Compiled object:\t", obj_file);
}

Path_t compile_executable(env_t *base_env, Path_t path, Path_t exe_path, List_t object_files, List_t extra_ldlibs)
{
    ast_t *ast = parse_file(Path$as_c_string(path), NULL);
    if (!ast)
        print_err("Could not parse file ", path);
    env_t *env = load_module_env(base_env, ast);
    binding_t *main_binding = get_binding(env, "main");
    if (!main_binding || main_binding->type->tag != FunctionType)
        print_err("No main() function has been defined for ", path, ", so it can't be run!");

    if (!clean_build && Path$is_file(exe_path, true) && !is_config_outdated(path)
        && !is_stale_for_any(exe_path, object_files, false)
        && !is_stale(exe_path, Path$sibling(path, Text("modules.ini")), true)
        && !is_stale(exe_path, build_file(path, ":modules.ini"), true)) {
        if (verbose) whisper("Unchanged: ", exe_path);
        return exe_path;
    }

    FILE *runner = run_cmd(cc, " ", cflags, " -O", optimization, " ", ldflags, " ", ldlibs, " ", list_text(extra_ldlibs), " ",
                           paths_str(object_files), " -x c - -o ", exe_path);
    CORD program = CORD_all(
        "extern int parse_and_run$$", main_binding->code, "(int argc, char *argv[]);\n"
        "__attribute__ ((noinline))\n"
        "int main(int argc, char *argv[]) {\n"
        "\treturn parse_and_run$$", main_binding->code, "(argc, argv);\n"
        "}\n"
    );

    if (show_codegen.length > 0) {
        FILE *out = run_cmd(show_codegen);
        CORD_put(program, out);
        pclose(out);
    }

    CORD_put(program, runner);
    int status = pclose(runner);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        exit(EXIT_FAILURE);

    if (!quiet)
        print("Compiled executable:\t", exe_path);
    return exe_path;
}

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
