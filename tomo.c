// The main program that runs compilation
#include <ctype.h>
#include <errno.h>
#include <gc.h>
#include <gc/cord.h>
#include <libgen.h>
#include <printf.h>
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
#include "stdlib/datatypes.h"
#include "stdlib/integers.h"
#include "stdlib/optionals.h"
#include "stdlib/patterns.h"
#include "stdlib/paths.h"
#include "stdlib/text.h"
#include "typecheck.h"
#include "types.h"

#define run_cmd(...) ({ const char *_cmd = heap_strf(__VA_ARGS__); if (verbose) puts(_cmd); popen(_cmd, "w"); })
#define array_str(arr) Text$as_c_string(Text$join(Text(" "), arr))

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
                      stop_at_transpile = false,
                      stop_at_obj_compilation = false,
                      stop_at_exe_compilation = false,
                      should_install = false,
                      run_repl = false;

static OptionalText_t 
            show_codegen = NONE_TEXT,
            cflags = Text("-Werror -fdollars-in-identifiers -std=gnu11 -Wno-trigraphs -fsanitize=signed-integer-overflow -fno-sanitize-recover"
                          " -fno-signed-zeros -fno-finite-math-only -fno-signaling-nans -fno-trapping-math"
                          " -D_XOPEN_SOURCE=700 -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE -fPIC -ggdb"
                          " -DGC_THREADS"
                          " -I$HOME/.local/share/tomo/installed"),
            ldlibs = Text("-lgc -lgmp -lm -ltomo"),
            ldflags = Text("-Wl,-rpath='$ORIGIN',-rpath=$HOME/.local/share/tomo/lib -L. -L$HOME/.local/share/tomo/lib"),
            optimization = Text("2"),
            cc = Text("gcc");

static void transpile_header(env_t *base_env, Path_t path, bool force_retranspile);
static void transpile_code(env_t *base_env, Path_t path, bool force_retranspile);
static void compile_object_file(Path_t path, bool force_recompile);
static Path_t compile_executable(env_t *base_env, Path_t path, Array_t object_files, Array_t extra_ldlibs);
static void build_file_dependency_graph(Path_t path, Table_t *to_compile, Table_t *to_link);
static Text_t escape_lib_name(Text_t lib_name);
static void build_library(Text_t lib_dir_name);
static void compile_files(env_t *env, Array_t files, bool only_compile_arguments, Array_t *object_files, Array_t *ldlibs);
static bool is_stale(Path_t path, Path_t relative_to);

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstack-protector"
int main(int argc, char *argv[])
{
    if (register_printf_specifier('T', printf_type, printf_pointer_size))
        errx(1, "Couldn't set printf specifier");
    if (register_printf_specifier('W', printf_ast, printf_pointer_size))
        errx(1, "Couldn't set printf specifier");
    if (register_printf_specifier('k', printf_text, printf_text_size))
        errx(1, "Couldn't set printf specifier");
    
    // Run a tool:
    if ((streq(argv[1], "-r") || streq(argv[1], "--run")) && argc >= 3) {
        if (strcspn(argv[2], "/;$") == strlen(argv[2])) {
            const char *program = heap_strf("%s/.local/share/tomo/installed/%s/%s", getenv("HOME"), argv[2], argv[2]);
            execv(program, &argv[2]);
        }
        errx(1, "This is not an installed tomo program: \033[31;1m%s\033[m", argv[2]);
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
        {"transpile", false, &Bool$info, &stop_at_transpile},
        {"t", false, &Bool$info, &stop_at_transpile},
        {"compile-obj", false, &Bool$info, &stop_at_obj_compilation},
        {"c", false, &Bool$info, &stop_at_obj_compilation},
        {"compile-exe", false, &Bool$info, &stop_at_exe_compilation},
        {"e", false, &Bool$info, &stop_at_exe_compilation},
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
    );

    if (show_codegen.length > 0 && Text$equal_values(show_codegen, Text("pretty")))
        show_codegen = Text("sed '/^#line/d;/^$/d' | indent -o /dev/stdout | bat -l c -P");

    for (int64_t i = 0; i < uninstall.length; i++) {
        Text_t *u = (Text_t*)(uninstall.data + i*uninstall.stride);
        system(heap_strf("rm -rvf ~/.local/share/tomo/installed/%k ~/.local/share/tomo/lib/lib%k.so", u, u));
        printf("Uninstalled %k\n", u);
    }

    for (int64_t i = 0; i < libraries.length; i++) {
        Path_t *lib = (Path_t*)(libraries.data + i*libraries.stride);
        const char *lib_str = Path$as_c_string(*lib);
        char *cwd = get_current_dir_name();
        if (chdir(lib_str) != 0)
            errx(1, "Could not enter directory: %s", lib_str);

        char *libdir = get_current_dir_name();
        char *libdirname = basename(libdir);
        build_library(Text$from_str(libdirname));
        free(libdir);
        chdir(cwd);
        free(cwd);
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

    // Run file directly:
    if (!stop_at_transpile && !stop_at_obj_compilation && !stop_at_exe_compilation) {
        if (files.length < 1)
            errx(1, "No file specified!");
        else if (files.length != 1)
            errx(1, "Too many files specified!");
        Path_t path = *(Path_t*)files.data;
        env_t *env = global_env();
        Array_t object_files = {},
                extra_ldlibs = {};
        compile_files(env, files, false, &object_files, &extra_ldlibs);
        Path_t exe_name = compile_executable(env, path, object_files, extra_ldlibs);
        char *prog_args[1 + args.length + 1];
        prog_args[0] = (char*)Path$as_c_string(exe_name);
        for (int64_t i = 0; i < args.length; i++)
            prog_args[i + 1] = Text$as_c_string(*(Text_t*)(args.data + i*args.stride));
        prog_args[1 + args.length] = NULL;
        execv(prog_args[0], prog_args);
        errx(1, "Failed to run compiled program");
    } else {
        env_t *env = global_env();
        Array_t object_files = {},
                extra_ldlibs = {};
        compile_files(env, files, stop_at_obj_compilation, &object_files, &extra_ldlibs);
        if (stop_at_obj_compilation)
            return 0;

        for (int64_t i = 0; i < files.length; i++) {
            Path_t path = *(Path_t*)(files.data + i*files.stride);
            Path_t bin_name = compile_executable(env, path, object_files, extra_ldlibs);
            if (should_install)
                system(heap_strf("cp -v '%s' ~/.local/bin/", Path$as_c_string(bin_name)));
        }
        return 0;
    }
}
#pragma GCC diagnostic pop

Text_t escape_lib_name(Text_t lib_name)
{
    return Text$replace(lib_name, Pattern("{1+ !alphanumeric}"), Text("_"), Pattern(""), false);
}

static Path_t build_file(Path_t path, const char *extension)
{
    Path_t build_dir = Path$with_component(Path$parent(path), Text(".build"));
    if (mkdir(Path$as_c_string(build_dir), 0777) != 0) {
        if (errno != EEXIST)
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
    if (!file_ast) errx(1, "Could not parse file %s", Path$as_c_string(path));
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
    compile_files(env, tm_files, false, &object_files, &extra_ldlibs);

    // Library name replaces all stretchs of non-alphanumeric chars with an underscore
    // So e.g. https://github.com/foo/baz --> https_github_com_foo_baz
    env->libname = Text$as_c_string(escape_lib_name(lib_dir_name));

    // Build a "whatever.h" header that loads all the headers:
    FILE *header = fopen(heap_strf("%k.h", &lib_dir_name), "w");
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
        errx(1, "Failed to write header file: %k.h", &lib_dir_name);

    // Build up a list of symbol renamings:
    unlink(".build/symbol_renames.txt");
    FILE *prog;
    for (int64_t i = 0; i < tm_files.length; i++) {
        Path_t f = *(Path_t*)(tm_files.data + i*tm_files.stride);
        prog = run_cmd("nm -Ug -fjust-symbols '%s' | sed -n 's/_\\$\\(.*\\)/\\0 _$%s$\\1/p' >>.build/symbol_renames.txt",
                       Path$as_c_string(build_file(f, ".o")), CORD_to_const_char_star(env->libname));
        if (!prog) errx(1, "Could not find symbols!");
        int status = pclose(prog);
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
            errx(WEXITSTATUS(status), "Failed to create symbol rename table with `nm` and `sed`");
    }

    prog = run_cmd("%k -O%k %k %k %k %s -Wl,-soname='lib%k.so' -shared %s -o 'lib%k.so'",
                   &cc, &optimization, &cflags, &ldflags, &ldlibs, array_str(extra_ldlibs), &lib_dir_name,
                   paths_str(object_files), &lib_dir_name);
    if (!prog)
        errx(1, "Failed to run C compiler: %k", &cc);
    int status = pclose(prog);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        exit(EXIT_FAILURE);

    if (verbose)
        printf("\x1b[2mCompiled to lib%k.so\x1b[m\n", &lib_dir_name);

    prog = run_cmd("objcopy --redefine-syms=.build/symbol_renames.txt 'lib%k.so'", &lib_dir_name);
    status = pclose(prog);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        errx(WEXITSTATUS(status), "Failed to run `objcopy` to add library prefix to symbols");

    prog = run_cmd("patchelf --rename-dynamic-symbols .build/symbol_renames.txt 'lib%k.so'", &lib_dir_name);
    status = pclose(prog);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        errx(WEXITSTATUS(status), "Failed to run `patchelf` to rename dynamic symbols with library prefix");

    if (verbose)
        CORD_printf("Successfully renamed symbols with library prefix!\n");

    // unlink(".build/symbol_renames.txt");

    if (should_install) {
        char *library_directory = get_current_dir_name();
        const char *dest = heap_strf("%s/.local/share/tomo/installed/%k", getenv("HOME"), &lib_dir_name);
        if (!streq(library_directory, dest)) {
            system(heap_strf("rm -rvf '%s'", dest));
            system(heap_strf("mkdir -p '%s'", dest));
            system(heap_strf("cp -rv * '%s/'", dest));
        }
        system("mkdir -p ~/.local/share/tomo/lib/");
        system(heap_strf("ln -fv -s ../installed/'%k'/lib'%k'.so  ~/.local/share/tomo/lib/lib'%k'.so",
                         &lib_dir_name, &lib_dir_name, &lib_dir_name));
        free(library_directory);
    }
}

void compile_files(env_t *env, Array_t to_compile, bool only_compile_arguments, Array_t *object_files, Array_t *extra_ldlibs)
{
    TypeInfo_t *path_table_info = Table$info(&Path$info, &Path$info);
    Table_t to_link = {};
    Table_t argument_files = {};
    Table_t dependency_files = {};
    for (int64_t i = 0; i < to_compile.length; i++) {
        Path_t filename = *(Path_t*)(to_compile.data + i*to_compile.stride);
        Text_t extension = Path$extension(filename, true);
        if (!Text$equal_values(extension, Text("tm")))
            errx(1, "Not a valid .tm file: \x1b[31;1m%s\x1b[m", Path$as_c_string(filename));
        Path_t resolved = Path$resolved(filename, Path("./"));
        if (!Path$is_file(resolved, true))
            errx(1, "Couldn't find file: %s", Path$as_c_string(resolved));
        Table$set(&argument_files, &resolved, &filename, path_table_info);
        build_file_dependency_graph(resolved, &dependency_files, &to_link);
    }

    int status;
    // (Re)compile header files, eagerly for explicitly passed in files, lazily
    // for downstream dependencies:
    for (int64_t i = 0; i < dependency_files.entries.length; i++) {
        Path_t filename = *(Path_t*)(dependency_files.entries.data + i*dependency_files.entries.stride);
        bool is_argument_file = (Table$get(argument_files, &filename, path_table_info) != NULL);
        transpile_header(env, filename, is_argument_file);
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
            bool stale;
        } *entry = (dependency_files.entries.data + i*dependency_files.entries.stride);
        bool is_argument_file = (Table$get(argument_files, &entry->filename, path_table_info) != NULL);
        if (!is_argument_file && (!entry->stale || only_compile_arguments))
            continue;

        pid_t pid = fork();
        if (pid == 0) {
            transpile_code(env, entry->filename, true);
            if (!stop_at_transpile)
                compile_object_file(entry->filename, true);
            _exit(EXIT_SUCCESS);
        }
        child_processes = new(struct child_s, .next=child_processes, .pid=pid);
    }

    for (; child_processes; child_processes = child_processes->next) {
        waitpid(child_processes->pid, &status, 0);
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
            exit(EXIT_FAILURE);
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
    if (Table$get(*to_compile, &path, Table$info(&Path$info, &Bool$info)))
        return;

    bool stale = is_stale(build_file(path, ".o"), path);
    Table$set(to_compile, &path, &stale, Table$info(&Path$info, &Bool$info));

    assert(Text$equal_values(Path$extension(path, true), Text("tm")));

    ast_t *ast = parse_file(Path$as_c_string(path), NULL);
    if (!ast)
        errx(1, "Could not parse file %s", Path$as_c_string(path));

    for (ast_list_t *stmt = Match(ast, Block)->statements; stmt; stmt = stmt->next) {
        ast_t *stmt_ast = stmt->ast;
        if (stmt_ast->tag == Declare)
            stmt_ast = Match(stmt_ast, Declare)->value;

        if (stmt_ast->tag != Use) continue;
        auto use = Match(stmt_ast, Use);

        switch (use->what) {
        case USE_LOCAL: {
            Path_t resolved = Path$resolved(Path$from_str(use->path), Path$parent(path));
            if (!stale && is_stale(build_file(path, ".o"), resolved)) {
                stale = true;
                Table$set(to_compile, &path, &stale, Table$info(&Path$info, &Bool$info));
            }
            if (Table$get(*to_compile, &resolved, Table$info(&Path$info, &Path$info)))
                continue;
            build_file_dependency_graph(resolved, to_compile, to_link);
            break;
        }
        case USE_MODULE: {
            Text_t lib = Text$format("'%s/.local/share/tomo/installed/%s/lib%s.so'", getenv("HOME"), use->path, use->path);
            Table$set(to_link, &lib, ((Bool_t[1]){1}), Table$info(&Text$info, &Bool$info));

            Array_t children = Path$glob(Path$from_str(heap_strf("%s/.local/share/tomo/installed/%s/*.tm", getenv("HOME"), use->path)));
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
    struct stat relative_to_stat;
    if (stat(Path$as_c_string(relative_to), &relative_to_stat) != 0)
        errx(1, "File doesn't exist: %s", Path$as_c_string(relative_to));
    return target_stat.st_mtime < relative_to_stat.st_mtime;
}

void transpile_header(env_t *base_env, Path_t path, bool force_retranspile)
{
    Path_t h_filename = build_file(path, ".h");
    if (!force_retranspile && !is_stale(h_filename, path))
        return;

    ast_t *ast = parse_file(Path$as_c_string(path), NULL);
    if (!ast)
        errx(1, "Could not parse file %s", Path$as_c_string(path));

    env_t *module_env = load_module_env(base_env, ast);

    CORD h_code = compile_file_header(module_env, ast);

    FILE *header = fopen(Path$as_c_string(h_filename), "w");
    if (!header)
        errx(1, "Failed to open header file: %s", Path$as_c_string(h_filename));
    CORD_put(h_code, header);
    if (fclose(header) == -1)
        errx(1, "Failed to write header file: %s", Path$as_c_string(h_filename));

    if (verbose)
        printf("\x1b[2mTranspiled to %s\x1b[m\n", Path$as_c_string(h_filename));

    if (show_codegen.length > 0)
        system(heap_strf("<%s %k", Path$as_c_string(h_filename), &show_codegen));
}

void transpile_code(env_t *base_env, Path_t path, bool force_retranspile)
{
    Path_t c_filename = build_file(path, ".c");
    if (!force_retranspile && !is_stale(c_filename, path))
        return;

    ast_t *ast = parse_file(Path$as_c_string(path), NULL);
    if (!ast)
        errx(1, "Could not parse file %s", Path$as_c_string(path));

    env_t *module_env = load_module_env(base_env, ast);

    CORD c_code = compile_file(module_env, ast);

    FILE *c_file = fopen(Path$as_c_string(c_filename), "w");
    if (!c_file)
        errx(1, "Failed to write C file: %s", Path$as_c_string(c_filename));

    CORD_put(c_code, c_file);

    binding_t *main_binding = get_binding(module_env, "main");
    if (main_binding && main_binding->type->tag == FunctionType) {
        type_t *ret = Match(main_binding->type, FunctionType)->ret;
        if (ret->tag != VoidType && ret->tag != AbortType)
            compiler_err(ast->file, ast->start, ast->end, "The main() function in this file has a return type of %T, but it should not have any return value!", ret);

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
        errx(1, "Failed to output C code to %s", Path$as_c_string(c_filename));

    if (verbose)
        printf("\x1b[2mTranspiled to %s\x1b[m\n", Path$as_c_string(c_filename));

    if (show_codegen.length > 0)
        system(heap_strf("<%s %k", Path$as_c_string(c_filename), &show_codegen));
}

void compile_object_file(Path_t path, bool force_recompile)
{
    Path_t obj_file = build_file(path, ".o");
    Path_t c_file = build_file(path, ".c");
    Path_t h_file = build_file(path, ".h");
    if (!force_recompile && !is_stale(obj_file, path)
        && !is_stale(obj_file, c_file)
        && !is_stale(obj_file, h_file)) {
        return;
    }

    FILE *prog = run_cmd("%k %k -O%k -c %s -o %s",
                         &cc, &cflags, &optimization, Path$as_c_string(c_file), Path$as_c_string(obj_file));
    if (!prog)
        errx(1, "Failed to run C compiler: %k", &cc);
    int status = pclose(prog);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        exit(EXIT_FAILURE);

    if (verbose)
        printf("\x1b[2mCompiled to %s\x1b[m\n", Path$as_c_string(obj_file));
}

Path_t compile_executable(env_t *base_env, Path_t path, Array_t object_files, Array_t extra_ldlibs)
{
    ast_t *ast = parse_file(Path$as_c_string(path), NULL);
    if (!ast)
        errx(1, "Could not parse file %s", Path$as_c_string(path));
    env_t *env = load_module_env(base_env, ast);
    binding_t *main_binding = get_binding(env, "main");
    if (!main_binding || main_binding->type->tag != FunctionType)
        errx(1, "No main() function has been defined for %s, so it can't be run!", Path$as_c_string(path));

    Path_t bin_name = Path$with_extension(path, Text(""), true);
    FILE *runner = run_cmd("%k %k -O%k %k %k %s %s -x c - -o %s",
                           &cc, &cflags, &optimization, &ldflags, &ldlibs,
                           array_str(extra_ldlibs), paths_str(object_files), Path$as_c_string(bin_name));
    CORD program = CORD_all(
        "extern int ", main_binding->code, "$parse_and_run(int argc, char *argv[]);\n"
        "int main(int argc, char *argv[]) {\n"
        "\treturn ", main_binding->code, "$parse_and_run(argc, argv);\n"
        "}\n"
    );

    if (show_codegen.length > 0) {
        FILE *out = run_cmd("%k", &show_codegen);
        CORD_put(program, out);
        pclose(out);
    }

    CORD_put(program, runner);
    int status = pclose(runner);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        exit(EXIT_FAILURE);

    if (verbose)
        printf("\x1b[2mCompiled executable: %s\x1b[m\n", Path$as_c_string(bin_name));
    return bin_name;
}

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
