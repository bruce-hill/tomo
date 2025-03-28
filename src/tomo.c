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

#include "ast.h"
#include "compile.h"
#include "cordhelpers.h"
#include "parse.h"
#include "repl.h"
#include "stdlib/arrays.h"
#include "stdlib/bools.h"
#include "stdlib/bytes.h"
#include "stdlib/datatypes.h"
#include "stdlib/integers.h"
#include "stdlib/optionals.h"
#include "stdlib/patterns.h"
#include "stdlib/paths.h"
#include "stdlib/print.h"
#include "stdlib/text.h"
#include "typecheck.h"
#include "types.h"

#define run_cmd(...) ({ const char *_cmd = String(__VA_ARGS__); if (verbose) print("\033[34;1m", _cmd, "\033[m"); popen(_cmd, "w"); })
#define array_text(arr) Text$join(Text(" "), arr)

#ifdef __linux__
// Only on Linux is /proc/self/exe available
static struct stat compiler_stat;
#endif

static const char *paths_str(Array_t paths) {
    Text_t result = EMPTY_TEXT;
    for (int64_t i = 0; i < paths.length; i++) {
        if (i > 0) result = Texts(result, Text(" "));
        result = Texts(result, Path$as_text((Path_t*)(paths.data + i*paths.stride), false, &Path$info));
    }
    return Text$as_c_string(result);
}

static OptionalArray_t files = NONE_ARRAY,
                       args = NONE_ARRAY,
                       uninstall = NONE_ARRAY,
                       libraries = NONE_ARRAY;
static OptionalBool_t verbose = false,
                      quiet = false,
                      stop_at_transpile = false,
                      stop_at_obj_compilation = false,
                      compile_exe = false,
                      should_install = false,
                      run_repl = false,
                      clean_build = false;

static OptionalText_t 
            show_codegen = NONE_TEXT,
            cflags = Text("-Werror -fdollars-in-identifiers -std=c2x -Wno-trigraphs "
                          " -fno-signed-zeros -fno-finite-math-only "
                          " -D_XOPEN_SOURCE=700 -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE -fPIC -ggdb"
                          " -DGC_THREADS"
                          " -I$HOME/.local/include -I$HOME/.local/share/tomo/installed -I/usr/local/include"),
            ldlibs = Text("-lgc -lm -lgmp -lunistring -ltomo"),
            ldflags = Text("-Wl,-rpath='$ORIGIN',-rpath=$HOME/.local/share/tomo/lib,-rpath=$HOME/.local/lib,-rpath=/usr/local/lib "
                           "-L$HOME/.local/lib -L$HOME/.local/share/tomo/lib -L/usr/local/lib"),
            optimization = Text("2"),
            cc = Text("cc");

static void transpile_header(env_t *base_env, Path_t path);
static void transpile_code(env_t *base_env, Path_t path);
static void compile_object_file(Path_t path);
static Path_t compile_executable(env_t *base_env, Path_t path, Path_t exe_path, Array_t object_files, Array_t extra_ldlibs);
static void build_file_dependency_graph(Path_t path, Table_t *to_compile, Table_t *to_link);
static Text_t escape_lib_name(Text_t lib_name);
static void build_library(Text_t lib_dir_name);
static void compile_files(env_t *env, Array_t files, Array_t *object_files, Array_t *ldlibs);
static bool is_stale(Path_t path, Path_t relative_to);
static Path_t build_file(Path_t path, const char *extension);

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
    ldlibs = Texts(ldlibs, Text(" -lexecinfo -lpthread"));
#endif

    USE_COLOR = getenv("COLOR") ? strcmp(getenv("COLOR"), "1") == 0 : isatty(STDOUT_FILENO);
    if (getenv("NO_COLOR") && getenv("NO_COLOR")[0] != '\0')
        USE_COLOR = false;

    // Run a tool:
    if ((streq(argv[1], "-r") || streq(argv[1], "--run")) && argc >= 3) {
        if (strcspn(argv[2], "/;$") == strlen(argv[2])) {
            const char *program = String(getenv("HOME"), "/.local/share/tomo/installed/", argv[2], "/", argv[2]);
            execv(program, &argv[2]);
        }
        print_err("This is not an installed tomo program: \033[31;1m", argv[2], "\033[m");
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
                        "  --install|-I: install the executable or library\n"
                        "  --c-compiler <compiler>: the C compiler to use (default: cc)\n"
                        "  --optimization|-O <level>: set optimization level\n"
                        "  --run|-r: run a program from ~/.local/share/tomo/installed\n"
                        );
    Text_t help = Texts(Text("\x1b[1mtomo\x1b[m: a compiler for the Tomo programming language"), Text("\n\n"), usage);
    tomo_parse_args(
        argc, argv, usage, help,
        {"files", true, Array$info(&Path$info), &files},
        {"args", true, Array$info(&Text$info), &args},
        {"verbose", false, &Bool$info, &verbose},
        {"v", false, &Bool$info, &verbose},
        {"quiet", false, &Bool$info, &quiet},
        {"q", false, &Bool$info, &quiet},
        {"transpile", false, &Bool$info, &stop_at_transpile},
        {"t", false, &Bool$info, &stop_at_transpile},
        {"compile-obj", false, &Bool$info, &stop_at_obj_compilation},
        {"c", false, &Bool$info, &stop_at_obj_compilation},
        {"compile-exe", false, &Bool$info, &compile_exe},
        {"e", false, &Bool$info, &compile_exe},
        {"uninstall", false, Array$info(&Text$info), &uninstall},
        {"u", false, Array$info(&Text$info), &uninstall},
        {"library", false, Array$info(&Path$info), &libraries},
        {"L", false, Array$info(&Path$info), &libraries},
        {"show-codegen", false, &Text$info, &show_codegen},
        {"C", false, &Text$info, &show_codegen},
        {"repl", false, &Bool$info, &run_repl},
        {"R", false, &Bool$info, &run_repl},
        {"install", false, &Bool$info, &should_install},
        {"I", false, &Bool$info, &should_install},
        {"c-compiler", false, &Text$info, &cc},
        {"optimization", false, &Text$info, &optimization},
        {"O", false, &Text$info, &optimization},
        {"force-rebuild", false, &Bool$info, &clean_build},
        {"f", false, &Bool$info, &clean_build},
    );

    bool is_gcc = (system(String(cc, " -v 2>&1 | grep 'gcc version' >/dev/null")) == 0);
    if (is_gcc) {
        cflags = Texts(cflags, Text(" -fsanitize=signed-integer-overflow -fno-sanitize-recover"
                                    " -fno-signaling-nans -fno-trapping-math"));
    }

    if (show_codegen.length > 0 && Text$equal_values(show_codegen, Text("pretty")))
        show_codegen = Text("sed '/^#line/d;/^$/d' | indent -o /dev/stdout | bat -l c -P");

    for (int64_t i = 0; i < uninstall.length; i++) {
        Text_t *u = (Text_t*)(uninstall.data + i*uninstall.stride);
        system(String("rm -rvf ~/.local/share/tomo/installed/", *u, " ~/.local/share/tomo/lib/lib", *u, ".so"));
        print("Uninstalled ", *u);
    }

    for (int64_t i = 0; i < libraries.length; i++) {
        Path_t *lib = (Path_t*)(libraries.data + i*libraries.stride);
        const char *lib_str = Path$as_c_string(*lib);
        char cwd[PATH_MAX];
        getcwd(cwd, sizeof(cwd));
        if (chdir(lib_str) != 0)
            print_err("Could not enter directory: ", lib_str);

        char libdir[PATH_MAX];
        getcwd(libdir, sizeof(libdir));
        char *libdirname = basename(libdir);
        build_library(Text$from_str(libdirname));
        chdir(cwd);
    }

    // TODO: REPL
    if (run_repl) {
        repl();
        return 0;
    }

    if (files.length <= 0 && (uninstall.length > 0 || libraries.length > 0)) {
        return 0;
    }

    // Convert `foo` to `foo/foo.tm`
    for (int64_t i = 0; i < files.length; i++) {
        Path_t *path = (Path_t*)(files.data + i*files.stride);
        if (Path$is_directory(*path, true))
            *path = Path$with_component(*path, Texts(Path$base_name(*path), Text(".tm")));
    }

    if (files.length < 1)
        print_err("No file specified!");
    else if (files.length != 1)
        print_err("Too many files specified!");

    quiet = !verbose;

    for (int64_t i = 0; i < files.length; i++) {
        Path_t path = *(Path_t*)(files.data + i*files.stride);
        Path_t exe_path = compile_exe ? Path$with_extension(path, Text(""), true)
            : build_file(Path$with_extension(path, Text(""), true), "");

        pid_t child = fork();
        if (child == 0) {
            env_t *env = global_env();
            Array_t object_files = {},
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

    if (compile_exe && should_install) {
        for (int64_t i = 0; i < files.length; i++) {
            Path_t path = *(Path_t*)(files.data + i*files.stride);
            Path_t exe = Path$with_extension(path, Text(""), true);
            system(String("cp -v '", exe, "' ~/.local/bin/"));
        }
    }
    return 0;
}
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

Text_t escape_lib_name(Text_t lib_name)
{
    return Text$replace(lib_name, Pattern("{1+ !alphanumeric}"), Text("_"), Pattern(""), false);
}

Path_t build_file(Path_t path, const char *extension)
{
    Path_t build_dir = Path$with_component(Path$parent(path), Text(".build"));
    if (mkdir(Path$as_c_string(build_dir), 0755) != 0) {
        if (!Path$is_directory(build_dir, true))
            err(1, "Could not make .build directory");
    }
    return Path$with_component(build_dir, Texts(Path$base_name(path), Text$from_str(extension)));
}

typedef struct {
    env_t *env;
    Table_t *used_imports;
    FILE *output;
} libheader_info_t;

static void _compile_statement_header_for_library(libheader_info_t *info, ast_t *ast)
{
    if (ast->tag == Declare && Match(ast, Declare)->value->tag == Use)
        ast = Match(ast, Declare)->value;

    if (ast->tag == Use) {
        auto use = Match(ast, Use);
        if (use->what == USE_LOCAL)
            return;

        Path_t path = Path$from_str(use->path);
        if (!Table$get(*info->used_imports, &path, Table$info(&Path$info, &Path$info))) {
            Table$set(info->used_imports, &path, ((Bool_t[1]){1}), Table$info(&Text$info, &Bool$info));
            CORD_put(compile_statement_type_header(info->env, ast), info->output);
            CORD_put(compile_statement_namespace_header(info->env, ast), info->output);
        }
    } else {
        CORD_put(compile_statement_type_header(info->env, ast), info->output);
        CORD_put(compile_statement_namespace_header(info->env, ast), info->output);
    }
}

static void _make_typedefs_for_library(libheader_info_t *info, ast_t *ast)
{
    if (ast->tag == StructDef) {
        auto def = Match(ast, StructDef);
        CORD full_name = CORD_cat(namespace_prefix(info->env, info->env->namespace), def->name);
        CORD_put(CORD_all("typedef struct ", full_name, "$$struct ", full_name, "$$type;\n"), info->output);
    } else if (ast->tag == EnumDef) {
        auto def = Match(ast, EnumDef);
        CORD full_name = CORD_cat(namespace_prefix(info->env, info->env->namespace), def->name);
        CORD_put(CORD_all("typedef struct ", full_name, "$$struct ", full_name, "$$type;\n"), info->output);

        for (tag_ast_t *tag = def->tags; tag; tag = tag->next) {
            if (!tag->fields) continue;
            CORD_put(CORD_all("typedef struct ", full_name, "$", tag->name, "$$struct ", full_name, "$", tag->name, "$$type;\n"), info->output);
        }
    } else if (ast->tag == LangDef) {
        auto def = Match(ast, LangDef);
        CORD_put(CORD_all("typedef Text_t ", namespace_prefix(info->env, info->env->namespace), def->name, "$$type;\n"), info->output);
    }
}

static void _compile_file_header_for_library(env_t *env, Path_t path, Table_t *visited_files, Table_t *used_imports, FILE *output)
{
    if (Table$get(*visited_files, &path, Table$info(&Path$info, &Bool$info)))
        return;

    Table$set(visited_files, &path, ((Bool_t[1]){1}), Table$info(&Path$info, &Bool$info));

    ast_t *file_ast = parse_file(Path$as_c_string(path), NULL);
    if (!file_ast) print_err("Could not parse file ", path);
    env_t *module_env = load_module_env(env, file_ast);

    libheader_info_t info = {
        .env=module_env,
        .used_imports=used_imports,
        .output=output,
    };

    // Visit files in topological order:
    for (ast_list_t *stmt = Match(file_ast, Block)->statements; stmt; stmt = stmt->next) {
        ast_t *ast = stmt->ast;
        if (ast->tag == Declare)
            ast = Match(ast, Declare)->value;
        if (ast->tag != Use) continue;

        auto use = Match(ast, Use);
        if (use->what == USE_LOCAL) {
            Path_t resolved = Path$resolved(Path$from_str(use->path), Path("./"));
            _compile_file_header_for_library(env, resolved, visited_files, used_imports, output);
        }
    }

    visit_topologically(
        Match(file_ast, Block)->statements, (Closure_t){.fn=(void*)_make_typedefs_for_library, &info});
    visit_topologically(
        Match(file_ast, Block)->statements, (Closure_t){.fn=(void*)_compile_statement_header_for_library, &info});

    CORD_fprintf(output, "void %r$initialize(void);\n", namespace_prefix(module_env, module_env->namespace));
}

void build_library(Text_t lib_dir_name)
{
    Array_t tm_files = Path$glob(Path("./[!._0-9]*.tm"));
    env_t *env = fresh_scope(global_env());
    Array_t object_files = {},
            extra_ldlibs = {};
    compile_files(env, tm_files, &object_files, &extra_ldlibs);

    // Library name replaces all stretchs of non-alphanumeric chars with an underscore
    // So e.g. https://github.com/foo/baz --> https_github_com_foo_baz
    env->libname = Text$as_c_string(escape_lib_name(lib_dir_name));

    // Build a "whatever.h" header that loads all the headers:
    FILE *header = fopen(String(lib_dir_name, ".h"), "w");
    fputs("#pragma once\n", header);
    fputs("#include <tomo/tomo.h>\n", header);
    Table_t visited_files = {};
    Table_t used_imports = {};
    for (int64_t i = 0; i < tm_files.length; i++) {
        Path_t f = *(Path_t*)(tm_files.data + i*tm_files.stride);
        Path_t resolved = Path$resolved(f, Path("."));
        _compile_file_header_for_library(env, resolved, &visited_files, &used_imports, header);
    }
    if (fclose(header) == -1)
        print_err("Failed to write header file: ", lib_dir_name, ".h");

    // Build up a list of symbol renamings:
    unlink(".build/symbol_renames.txt");
    FILE *prog;
    for (int64_t i = 0; i < tm_files.length; i++) {
        Path_t f = *(Path_t*)(tm_files.data + i*tm_files.stride);
        prog = run_cmd("nm -Ug -fjust-symbols '", build_file(f, ".o"), "' "
                       "| sed -n 's/_\\$\\(.*\\)/\\0 _$", CORD_to_const_char_star(env->libname),
                       "$\\1/p' >>.build/symbol_renames.txt");
        if (!prog) print_err("Could not find symbols!");
        int status = pclose(prog);
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
            errx(WEXITSTATUS(status), "Failed to create symbol rename table with `nm` and `sed`");
    }

    prog = run_cmd(cc, " -O", optimization, " ", cflags, " ", ldflags, " ", ldlibs, " ", array_text(extra_ldlibs),
                   " -Wl,-soname='lib", lib_dir_name, ".so' -shared ", paths_str(object_files), " -o 'lib", lib_dir_name, ".so'");
    if (!prog)
        print_err("Failed to run C compiler: ", cc);
    int status = pclose(prog);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        exit(EXIT_FAILURE);

    if (!quiet)
        print("Compiled library:\tlib", lib_dir_name, ".so");

    prog = run_cmd("objcopy --redefine-syms=.build/symbol_renames.txt 'lib", lib_dir_name, ".so'");
    status = pclose(prog);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        errx(WEXITSTATUS(status), "Failed to run `objcopy` to add library prefix to symbols");

    prog = run_cmd("patchelf --rename-dynamic-symbols .build/symbol_renames.txt 'lib", lib_dir_name, ".so'");
    status = pclose(prog);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        errx(WEXITSTATUS(status), "Failed to run `patchelf` to rename dynamic symbols with library prefix");

    if (verbose)
        CORD_printf("Successfully renamed symbols with library prefix!\n");

    // unlink(".build/symbol_renames.txt");

    if (should_install) {
        char library_directory[PATH_MAX];
        getcwd(library_directory, sizeof(library_directory));
        const char *dest = String(getenv("HOME"), "/.local/share/tomo/installed/", lib_dir_name);
        if (!streq(library_directory, dest)) {
            system(String("rm -rf '", dest, "'"));
            system(String("mkdir -p '", dest, "'"));
            system(String("cp -r * '", dest, "/'"));
        }
        system("mkdir -p ~/.local/share/tomo/lib/");
        system(String("ln -f -s ../installed/'", lib_dir_name, "'/lib'", lib_dir_name,
                      "'.so  ~/.local/share/tomo/lib/lib'", lib_dir_name, "'.so"));
        print("Installed \033[1m", lib_dir_name, "\033[m to ~/.local/share/tomo/installed");
    }
}

void compile_files(env_t *env, Array_t to_compile, Array_t *object_files, Array_t *extra_ldlibs)
{
    Table_t to_link = {};
    Table_t dependency_files = {};
    for (int64_t i = 0; i < to_compile.length; i++) {
        Path_t filename = *(Path_t*)(to_compile.data + i*to_compile.stride);
        Text_t extension = Path$extension(filename, true);
        if (!Text$equal_values(extension, Text("tm")))
            print_err("Not a valid .tm file: \x1b[31;1m", filename, "\x1b[m");
        Path_t resolved = Path$resolved(filename, Path("./"));
        if (!Path$is_file(resolved, true))
            print_err("Couldn't find file: ", resolved);
        build_file_dependency_graph(resolved, &dependency_files, &to_link);
    }

    int status;
    // (Re)compile header files, eagerly for explicitly passed in files, lazily
    // for downstream dependencies:
    for (int64_t i = 0; i < dependency_files.entries.length; i++) {
        struct {
            Path_t filename;
            staleness_t staleness;
        } *entry = (dependency_files.entries.data + i*dependency_files.entries.stride);
        if (entry->staleness.h) {
            transpile_header(env, entry->filename);
            entry->staleness.o = true;
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
        if (!clean_build && !entry->staleness.c && !entry->staleness.h && !entry->staleness.o)
            continue;

        pid_t pid = fork();
        if (pid == 0) {
            transpile_code(env, entry->filename);
            if (!stop_at_transpile)
                compile_object_file(entry->filename);
            _exit(EXIT_SUCCESS);
        }
        child_processes = new(struct child_s, .next=child_processes, .pid=pid);
    }

    for (; child_processes; child_processes = child_processes->next) {
        waitpid(child_processes->pid, &status, 0);
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
            exit(EXIT_FAILURE);
        else if (WIFSTOPPED(status))
            kill(child_processes->pid, SIGCONT);
    }

    if (object_files) {
        for (int64_t i = 0; i < dependency_files.entries.length; i++) {
            Path_t path = *(Path_t*)(dependency_files.entries.data + i*dependency_files.entries.stride);
            path = build_file(path, ".o");
            Array$insert(object_files, &path, I(0), sizeof(Path_t));
        }
    }
    if (extra_ldlibs) {
        for (int64_t i = 0; i < to_link.entries.length; i++) {
            Text_t lib = *(Text_t*)(to_link.entries.data + i*to_link.entries.stride);
            Array$insert(extra_ldlibs, &lib, I(0), sizeof(Text_t));
        }
    }
}

void build_file_dependency_graph(Path_t path, Table_t *to_compile, Table_t *to_link)
{
    if (Table$get(*to_compile, &path, Table$info(&Path$info, &Byte$info)))
        return;

    staleness_t staleness = {
        .h=is_stale(build_file(path, ".h"), path),
        .c=is_stale(build_file(path, ".c"), path),
    };
    staleness.o = staleness.c || staleness.h
        || is_stale(build_file(path, ".o"), build_file(path, ".c"))
        || is_stale(build_file(path, ".o"), build_file(path, ".h"));
    Table$set(to_compile, &path, &staleness, Table$info(&Path$info, &Byte$info));

    assert(Text$equal_values(Path$extension(path, true), Text("tm")));

    ast_t *ast = parse_file(Path$as_c_string(path), NULL);
    if (!ast)
        print_err("Could not parse file: ", path);

    for (ast_list_t *stmt = Match(ast, Block)->statements; stmt; stmt = stmt->next) {
        ast_t *stmt_ast = stmt->ast;
        if (stmt_ast->tag == Declare)
            stmt_ast = Match(stmt_ast, Declare)->value;

        if (stmt_ast->tag != Use) continue;
        auto use = Match(stmt_ast, Use);

        switch (use->what) {
        case USE_LOCAL: {
            Path_t dep_tm = Path$resolved(Path$from_str(use->path), Path$parent(path));
            if (is_stale(build_file(path, ".h"), dep_tm))
                staleness.h = true;
            if (is_stale(build_file(path, ".c"), dep_tm))
                staleness.c = true;
            if (staleness.c || staleness.h)
                staleness.o = true;
            Table$set(to_compile, &path, &staleness, Table$info(&Path$info, &Byte$info));
            build_file_dependency_graph(dep_tm, to_compile, to_link);
            break;
        }
        case USE_MODULE: {
            Text_t lib = Text$format("'%s/.local/share/tomo/installed/%s/lib%s.so'", getenv("HOME"), use->path, use->path);
            Table$set(to_link, &lib, ((Bool_t[1]){1}), Table$info(&Text$info, &Bool$info));

            Array_t children = Path$glob(Path$from_str(String(getenv("HOME"), "/.local/share/tomo/installed/", use->path, "/*.tm")));
            for (int64_t i = 0; i < children.length; i++) {
                Path_t *child = (Path_t*)(children.data + i*children.stride);
                Table_t discarded = {.fallback=to_compile};
                build_file_dependency_graph(*child, &discarded, to_link);
            }
            break;
        }
        case USE_SHARED_OBJECT: {
            Text_t lib = Text$format("-l:%s", use->path);
            Table$set(to_link, &lib, ((Bool_t[1]){1}), Table$info(&Text$info, &Bool$info));
            break;
        }
        case USE_ASM: {
            Text_t linker_text = Path$as_text(&use->path, false, &Path$info);
            Table$set(to_link, &linker_text, ((Bool_t[1]){1}), Table$info(&Text$info, &Bool$info));
            break;
        }
        default: case USE_HEADER: break;
        }
    }
}

bool is_stale(Path_t path, Path_t relative_to)
{
    struct stat target_stat;
    if (stat(Path$as_c_string(path), &target_stat) != 0)
        return true;

#ifdef __linux__
    // Any file older than the compiler is stale:
    if (target_stat.st_mtime < compiler_stat.st_mtime)
        return true;
#endif

    struct stat relative_to_stat;
    if (stat(Path$as_c_string(relative_to), &relative_to_stat) != 0)
        print_err("File doesn't exist: ", relative_to);
    return target_stat.st_mtime < relative_to_stat.st_mtime;
}

void transpile_header(env_t *base_env, Path_t path)
{
    Path_t h_filename = build_file(path, ".h");
    ast_t *ast = parse_file(Path$as_c_string(path), NULL);
    if (!ast)
        print_err("Could not parse file: ", path);

    env_t *module_env = load_module_env(base_env, ast);

    CORD h_code = compile_file_header(module_env, ast);

    FILE *header = fopen(Path$as_c_string(h_filename), "w");
    if (!header)
        print_err("Failed to open header file: ", h_filename);
    CORD_put(h_code, header);
    if (fclose(header) == -1)
        print_err("Failed to write header file: ", h_filename);

    if (!quiet)
        print("Transpiled header:\t", h_filename);

    if (show_codegen.length > 0)
        system(String(show_codegen, " <", h_filename));
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

    binding_t *main_binding = get_binding(module_env, "main");
    if (main_binding && main_binding->type->tag == FunctionType) {
        type_t *ret = Match(main_binding->type, FunctionType)->ret;
        if (ret->tag != VoidType && ret->tag != AbortType)
            compiler_err(ast->file, ast->start, ast->end,
                         "The main() function in this file has a return type of ", type_to_str(ret),
                         ", but it should not have any return value!");

        CORD_put(CORD_all(
            "int ", main_binding->code, "$parse_and_run(int argc, char *argv[]) {\n"
            "#line 1\n"
            "tomo_init();\n",
            "_$", module_env->namespace->name, "$$initialize();\n"
            "\n",
            compile_cli_arg_call(module_env, main_binding->code, main_binding->type),
            "return 0;\n"
            "}\n"), c_file);
    }

    if (fclose(c_file) == -1)
        print_err("Failed to output C code to ", c_filename);

    if (!quiet)
        print("Transpiled code:\t", c_filename);

    if (show_codegen.length > 0)
        system(String(show_codegen, " <", c_filename));
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

    if (!quiet)
        print("Compiled object:\t", obj_file);
}

Path_t compile_executable(env_t *base_env, Path_t path, Path_t exe_path, Array_t object_files, Array_t extra_ldlibs)
{
    ast_t *ast = parse_file(Path$as_c_string(path), NULL);
    if (!ast)
        print_err("Could not parse file ", path);
    env_t *env = load_module_env(base_env, ast);
    binding_t *main_binding = get_binding(env, "main");
    if (!main_binding || main_binding->type->tag != FunctionType)
        print_err("No main() function has been defined for ", path, ", so it can't be run!");

    FILE *runner = run_cmd(cc, " ", cflags, " -O", optimization, " ", ldflags, " ", ldlibs, " ", array_text(extra_ldlibs), " ",
                           paths_str(object_files), " -x c - -o ", exe_path);
    CORD program = CORD_all(
        "extern int ", main_binding->code, "$parse_and_run(int argc, char *argv[]);\n"
        "int main(int argc, char *argv[]) {\n"
        "\treturn ", main_binding->code, "$parse_and_run(argc, argv);\n"
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
