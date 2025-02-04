// The main program that runs compilation
#include <ctype.h>
#include <gc.h>
#include <gc/cord.h>
#include <glob.h>
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
#include "stdlib/shell.h"
#include "stdlib/text.h"
#include "typecheck.h"
#include "types.h"

#define run_cmd(...) ({ const char *_cmd = heap_strf(__VA_ARGS__); if (verbose) puts(_cmd); popen(_cmd, "w"); })
#define array_str(arr) Text$as_c_string(Text$join(Text(" "), arr))

static OptionalArray_t files = NONE_ARRAY,
                       args = NONE_ARRAY;
static OptionalBool_t verbose = false,
                      quiet = false,
                      show_codegen = false,
                      stop_at_transpile = false,
                      stop_at_obj_compilation = false,
                      stop_at_exe_compilation = false,
                      should_install = false,
                      library_mode = false,
                      uninstall = false;

static OptionalText_t autofmt = Text("sed '/^\\s*$/d' | indent -kr -l100 -nbbo -nut -sob"),
            cflags = Text("-Werror -fdollars-in-identifiers -std=gnu11 -Wno-trigraphs -fsanitize=signed-integer-overflow -fno-sanitize-recover"
                          " -fno-signed-zeros -fno-finite-math-only -fno-signaling-nans -fno-trapping-math"
                          " -D_XOPEN_SOURCE=700 -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE -fPIC -ggdb"
                          " -DGC_THREADS"
                          " -I$HOME/.local/share/tomo/installed"),
            ldlibs = Text("-lgc -lgmp -lm -ltomo"),
            ldflags = Text("-Wl,-rpath='$ORIGIN',-rpath=$HOME/.local/share/tomo/lib -L. -L$HOME/.local/share/tomo/lib"),
            optimization = Text("2"),
            cc = Text("cc");

static void transpile_header(env_t *base_env, Text_t filename, bool force_retranspile);
static void transpile_code(env_t *base_env, Text_t filename, bool force_retranspile);
static void compile_object_file(Text_t filename, bool force_recompile);
static Text_t compile_executable(env_t *base_env, Text_t filename, Array_t object_files, Array_t extra_ldlibs);
static void build_file_dependency_graph(Text_t filename, Table_t *to_compile, Table_t *to_link);
static Text_t escape_lib_name(Text_t lib_name);
static void build_library(Text_t lib_dir_name);
static void compile_files(env_t *env, Array_t files, bool only_compile_arguments, Array_t *object_files, Array_t *ldlibs);

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
                        "  --quiet|-q: quiet output\n"
                        );
    Text_t help = Texts(Text("\x1b[1mtomo\x1b[m: a compiler for the Tomo programming language"), Text("\n\n"), usage);
    tomo_parse_args(
        argc, argv, usage, help,
        {"files", true, Array$info(&Text$info), &files},
        {"args", true, Array$info(&Text$info), &args},
        {"verbose", false, &Bool$info, &verbose},
        {"v", false, &Bool$info, &verbose},
        {"transpile", false, &Bool$info, &stop_at_transpile},
        {"t", false, &Bool$info, &stop_at_transpile},
        {"compile-obj", false, &Bool$info, &stop_at_obj_compilation},
        {"c", false, &Bool$info, &stop_at_obj_compilation},
        {"compile-exe", false, &Bool$info, &stop_at_exe_compilation},
        {"e", false, &Bool$info, &stop_at_exe_compilation},
        {"uninstall", false, &Bool$info, &uninstall},
        {"u", false, &Bool$info, &uninstall},
        {"library", false, &Bool$info, &library_mode},
        {"L", false, &Bool$info, &library_mode},
        {"show-codegen", false, &Bool$info, &show_codegen},
        {"C", false, &Bool$info, &show_codegen},
        {"install", false, &Bool$info, &should_install},
        {"I", false, &Bool$info, &should_install},
        {"autoformat", false, &Text$info, &autofmt},
        {"f", false, &Text$info, &autofmt},
        {"c-compiler", false, &Text$info, &cc},
        {"optimization", false, &Text$info, &optimization},
        {"O", false, &Text$info, &optimization},
        {"quiet", false, &Bool$info, &quiet},
        {"q", false, &Bool$info, &quiet},
    );

    if (uninstall) {
        for (int64_t i = 0; i < files.length; i++) {
            Text_t arg = *(Text_t*)(files.data + i*files.stride);
            system(heap_strf("rm -rvf ~/.local/share/tomo/installed/%k ~/.local/share/tomo/lib/lib%k.so",
                             &arg, &arg));
        }
        return 0;
    } else if (library_mode) {
        char *cwd = get_current_dir_name();
        if (files.length == 0)
            files = (Array_t){.length=1, .stride=sizeof(Text_t), .data=(Text_t[]){Text(".")}};

        for (int64_t i = 0; i < files.length; i++) {
            Text_t arg = *(Text_t*)(files.data + i*files.stride);
            if (chdir(Text$as_c_string(arg)) != 0)
                errx(1, "Could not enter directory: %k", &arg);
            char *libdir = get_current_dir_name();
            char *libdirname = basename(libdir);
            build_library(Text$from_str(libdirname));
            free(libdir);
            chdir(cwd);
        }
        free(cwd);
        return 0;
    } else if (files.length == 0) {
        repl();
        return 0;
    }

    // Run file directly:
    if (!stop_at_transpile && !stop_at_obj_compilation && !stop_at_exe_compilation) {
        if (files.length < 1)
            errx(1, "No file specified!");
        else if (files.length != 1)
            errx(1, "Too many files specified!");
        Text_t filename = *(Text_t*)files.data;
        env_t *env = new_compilation_unit(NULL);
        Array_t object_files = {},
                extra_ldlibs = {};
        compile_files(env, files, false, &object_files, &extra_ldlibs);
        Text_t exe_name = compile_executable(env, filename, object_files, extra_ldlibs);
        char *prog_args[1 + args.length + 1];
        prog_args[0] = Text$as_c_string(exe_name);
        for (int64_t i = 0; i < args.length; i++)
            prog_args[i + 1] = Text$as_c_string(*(Text_t*)(args.data + i*args.stride));
        prog_args[1 + args.length] = NULL;
        execv(prog_args[0], prog_args);
        errx(1, "Failed to run compiled program");
    } else {
        env_t *env = new_compilation_unit(NULL);
        Array_t object_files = {},
                extra_ldlibs = {};
        compile_files(env, files, stop_at_obj_compilation, &object_files, &extra_ldlibs);
        if (stop_at_obj_compilation)
            return 0;

        for (int64_t i = 0; i < files.length; i++) {
            Text_t filename = *(Text_t*)(files.data + i*files.stride);
            Text_t bin_name = compile_executable(env, filename, object_files, extra_ldlibs);
            if (should_install)
                system(heap_strf("cp -v '%k' ~/.local/bin/", &bin_name));
        }
        return 0;
    }
}
#pragma GCC diagnostic pop

Text_t escape_lib_name(Text_t lib_name)
{
    return Text$replace(lib_name, Pattern("{1+ !alphanumeric}"), Text("_"), Pattern(""), false);
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

        Text_t path = Text$from_str(use->path);
        if (!Table$get(*info->used_imports, &path, Table$info(&Path$info, &Path$info))) {
            Table$set(info->used_imports, &path, &path, Table$info(&Text$info, &Text$info));
            CORD_put(compile_statement_type_header(info->env, ast), info->output);
            CORD_put(compile_statement_namespace_header(info->env, ast), info->output);
        }
    } else {
        CORD_put(compile_statement_type_header(info->env, ast), info->output);
        CORD_put(compile_statement_namespace_header(info->env, ast), info->output);
    }
}

static void _compile_file_header_for_library(env_t *env, Text_t filename, Table_t *visited_files, Table_t *used_imports, FILE *output)
{
    if (Table$get(*visited_files, &filename, Table$info(&Text$info, &Text$info)))
        return;

    Table$set(visited_files, &filename, &filename, Table$info(&Text$info, &Text$info));

    ast_t *file_ast = parse_file(Text$as_c_string(filename), NULL);
    if (!file_ast) errx(1, "Could not parse file %k", &filename);
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
            Path_t resolved = Path$resolved(Text$from_str(use->path), Path("./"));
            _compile_file_header_for_library(env, resolved, visited_files, used_imports, output);
        }
    }

    visit_topologically(
        Match(file_ast, Block)->statements, (Closure_t){.fn=(void*)_compile_statement_header_for_library, &info});

    CORD_fprintf(output, "void %r$initialize(void);\n", namespace_prefix(module_env, module_env->namespace));
}

void build_library(Text_t lib_dir_name)
{
    glob_t tm_files;
    char *library_directory = get_current_dir_name();
    if (glob("[!._0-9]*.tm", 0, NULL, &tm_files) != 0)
        errx(1, "Couldn't get .tm files in directory: %s", library_directory);

    Array_t glob_files = {};
    for (size_t i = 0; i < tm_files.gl_pathc; i++)
        Array$insert(&glob_files, (Text_t[1]){Text$from_str(tm_files.gl_pathv[i])}, I(0), sizeof(Text_t));

    env_t *env = new_compilation_unit(NULL);
    Array_t object_files = {},
            extra_ldlibs = {};
    compile_files(env, glob_files, false, &object_files, &extra_ldlibs);

    // Library name replaces all stretchs of non-alphanumeric chars with an underscore
    // So e.g. https://github.com/foo/baz --> https_github_com_foo_baz
    env->libname = Text$as_c_string(escape_lib_name(lib_dir_name));

    // Build a "whatever.h" header that loads all the headers:
    FILE *header_prog = run_cmd("%k 2>/dev/null >'%k.h'", &autofmt, &lib_dir_name);
    fputs("#pragma once\n", header_prog);
    fputs("#include <tomo/tomo.h>\n", header_prog);
    Table_t visited_files = {};
    Table_t used_imports = {};
    for (size_t i = 0; i < tm_files.gl_pathc; i++) {
        const char *filename = tm_files.gl_pathv[i];
        Path_t resolved = Path$resolved(Text$from_str(filename), Path("."));
        _compile_file_header_for_library(env, resolved, &visited_files, &used_imports, header_prog);
    }
    if (pclose(header_prog) == -1)
        errx(1, "Failed to run autoformat program on header file: %k", &autofmt);

    // Build up a list of symbol renamings:
    unlink("symbol_renames.txt");
    FILE *prog;
    for (size_t i = 0; i < tm_files.gl_pathc; i++) {
        const char *filename = tm_files.gl_pathv[i];
        prog = run_cmd("nm -Ug -fjust-symbols '%s.o' | sed 's/.*/\\0 $%s\\0/' >>symbol_renames.txt",
                       filename, CORD_to_const_char_star(env->libname));
        int status = pclose(prog);
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
            errx(WEXITSTATUS(status), "Failed to create symbol rename table with `nm` and `sed`");
    }

    globfree(&tm_files);

    prog = run_cmd("%k -O%k %k %k %k %s -Wl,-soname='lib%k.so' -shared %s -o 'lib%k.so'",
                   &cc, &optimization, &cflags, &ldflags, &ldlibs, array_str(extra_ldlibs), &lib_dir_name,
                   array_str(object_files), &lib_dir_name);
    if (!prog)
        errx(1, "Failed to run C compiler: %k", &cc);
    int status = pclose(prog);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        exit(EXIT_FAILURE);

    if (!quiet || verbose)
        printf("\x1b[2mCompiled to lib%k.so\x1b[m\n", &lib_dir_name);

    prog = run_cmd("objcopy --redefine-syms=symbol_renames.txt 'lib%k.so'", &lib_dir_name);
    status = pclose(prog);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        errx(WEXITSTATUS(status), "Failed to run `objcopy` to add library prefix to symbols");

    prog = run_cmd("patchelf --rename-dynamic-symbols symbol_renames.txt 'lib%k.so'", &lib_dir_name);
    status = pclose(prog);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        errx(WEXITSTATUS(status), "Failed to run `patchelf` to rename dynamic symbols with library prefix");

    if (verbose)
        CORD_printf("Successfully renamed symbols with library prefix!\n");

    unlink("symbol_renames.txt");

    if (should_install) {
        const char *dest = heap_strf("%s/.local/share/tomo/installed/%k", getenv("HOME"), &lib_dir_name);
        if (!streq(library_directory, dest)) {
            system(heap_strf("rm -rvf '%s'", dest));
            system(heap_strf("mkdir -p '%s'", dest));
            system(heap_strf("cp -rv * '%s/'", dest));
        }
        system("mkdir -p ~/.local/share/tomo/lib/");
        system(heap_strf("ln -fv -s ../installed/'%k'/lib'%k'.so  ~/.local/share/tomo/lib/lib'%k'.so",
                         &lib_dir_name, &lib_dir_name, &lib_dir_name));
    }

    free(library_directory);
}

void compile_files(env_t *env, Array_t to_compile, bool only_compile_arguments, Array_t *object_files, Array_t *extra_ldlibs)
{
    TypeInfo_t *path_table_info = Table$info(&Path$info, &Path$info);
    Table_t to_link = {};
    Table_t argument_files = {};
    Table_t dependency_files = {};
    for (int64_t i = 0; i < to_compile.length; i++) {
        Path_t filename = *(Path_t*)(to_compile.data + i*to_compile.stride);
        if (!Text$ends_with(filename, Text(".tm")))
            errx(1, "Not a valid .tm file: \x1b[31;1m%k\x1b[m", &filename);
        Path_t resolved = Path$resolved(filename, Path("./"));
        if (!Path$is_file(resolved, true))
            errx(1, "Couldn't find file: %k", &resolved);
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
        Path_t filename = *(Path_t*)(dependency_files.entries.data + i*dependency_files.entries.stride);
        bool is_argument_file = (Table$get(argument_files, &filename, path_table_info) != NULL);
        if (!is_argument_file && only_compile_arguments)
            continue;

        pid_t pid = fork();
        if (pid == 0) {
            transpile_code(env, filename, is_argument_file);
            if (!stop_at_transpile)
                compile_object_file(filename, is_argument_file);
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
            Path_t filename = Text$concat(
                *(Path_t*)(dependency_files.entries.data + i*dependency_files.entries.stride),
                Text(".o"));
            Array$insert(object_files, &filename, I(0), sizeof(Path_t));
        }
    }
    if (extra_ldlibs) {
        for (int64_t i = 0; i < to_link.entries.length; i++) {
            Text_t lib = *(Text_t*)(to_link.entries.data + i*to_link.entries.stride);
            Array$insert(extra_ldlibs, &lib, I(0), sizeof(Text_t));
        }
    }
}

void build_file_dependency_graph(Text_t filename, Table_t *to_compile, Table_t *to_link)
{
    if (Table$get(*to_compile, &filename, Table$info(&Text$info, &Text$info)))
        return;

    Table$set(to_compile, &filename, &filename, Table$info(&Text$info, &Text$info));

    assert(Text$ends_with(filename, Text(".tm")));

    ast_t *ast = parse_file(Text$as_c_string(filename), NULL);
    if (!ast)
        errx(1, "Could not parse file %k", &filename);

    for (ast_list_t *stmt = Match(ast, Block)->statements; stmt; stmt = stmt->next) {
        ast_t *stmt_ast = stmt->ast;
        if (stmt_ast->tag == Declare)
            stmt_ast = Match(stmt_ast, Declare)->value;

        if (stmt_ast->tag != Use) continue;
        auto use = Match(stmt_ast, Use);

        switch (use->what) {
        case USE_LOCAL: {
            Text_t resolved = Path$resolved(Text$from_str(use->path), filename);
            if (Table$get(*to_compile, &resolved, Table$info(&Path$info, &Path$info)))
                continue;
            build_file_dependency_graph(resolved, to_compile, to_link);
            Table$set(to_compile, &resolved, &resolved, Table$info(&Text$info, &Text$info));
            break;
        }
        case USE_MODULE: {
            Text_t lib = Text$format("'%s/.local/share/tomo/installed/%s/lib%s.so'", getenv("HOME"), use->path, use->path);
            Table$set(to_link, &lib, &lib, Table$info(&Text$info, &Text$info));
            break;
        }
        case USE_SHARED_OBJECT: {
            Text_t lib = Text$format("-l:%s", use->path);
            Table$set(to_link, &lib, &lib, Table$info(&Text$info, &Text$info));
            break;
        }
        case USE_ASM: {
            Text_t lib = Text$from_str(use->path);
            Table$set(to_link, &lib, &lib, Table$info(&Text$info, &Text$info));
            break;
        }
        default: case USE_HEADER: break;
        }
    }
}

static bool is_stale(Path_t filename, Path_t relative_to)
{
    struct stat target_stat;
    if (stat(Text$as_c_string(filename), &target_stat) != 0)
        return true;
    struct stat relative_to_stat;
    if (stat(Text$as_c_string(relative_to), &relative_to_stat) != 0)
        errx(1, "File doesn't exist: %k", &relative_to);
    return target_stat.st_mtime < relative_to_stat.st_mtime;
}

void transpile_header(env_t *base_env, Text_t filename, bool force_retranspile)
{
    Text_t h_filename = Text$concat(filename, Text(".h"));
    if (!force_retranspile && !is_stale(h_filename, filename))
        return;

    ast_t *ast = parse_file(Text$as_c_string(filename), NULL);
    if (!ast)
        errx(1, "Could not parse file %k", &filename);

    env_t *module_env = load_module_env(base_env, ast);

    CORD h_code = compile_file_header(module_env, ast);

    FILE *prog = run_cmd("%k 2>/dev/null >'%k'", &autofmt, &h_filename);
    CORD_put(h_code, prog);
    if (pclose(prog) == -1)
        errx(1, "Failed to run autoformat program on header file: %k", &autofmt);

    if (!quiet || verbose)
        printf("\x1b[2mTranspiled to %k\x1b[m\n", &h_filename);

    if (show_codegen)
        system(heap_strf("bat -P %k", &h_filename));
}

void transpile_code(env_t *base_env, Text_t filename, bool force_retranspile)
{
    Text_t c_filename = Text$concat(filename, Text(".c"));
    if (!force_retranspile && !is_stale(c_filename, filename))
        return;

    ast_t *ast = parse_file(Text$as_c_string(filename), NULL);
    if (!ast)
        errx(1, "Could not parse file %k", &filename);

    env_t *module_env = load_module_env(base_env, ast);

    CORD c_code = compile_file(module_env, ast);

    FILE *out = run_cmd("%k 2>/dev/null >'%k'", &autofmt, &c_filename);
    if (!out)
        errx(1, "Failed to run autoformat program: %k", &autofmt);

    CORD_put(c_code, out);

    binding_t *main_binding = get_binding(module_env, "main");
    if (main_binding && main_binding->type->tag == FunctionType) {
        CORD_put(CORD_all(
            "int ", main_binding->code, "$parse_and_run(int argc, char *argv[]) {\n"
            "tomo_init();\n",
            "_$", module_env->namespace->name, "$$initialize();\n"
            "\n",
            compile_cli_arg_call(module_env, main_binding->code, main_binding->type),
            "return 0;\n"
            "}\n"), out);
    }

    if (pclose(out) == -1)
        errx(1, "Failed to output autoformatted C code to %k: %k", &c_filename, &autofmt);

    if (!quiet || verbose)
        printf("\x1b[2mTranspiled to %k\x1b[m\n", &c_filename);

    if (show_codegen)
        system(heap_strf("bat -P %k", &c_filename));
}

void compile_object_file(Text_t filename, bool force_recompile)
{
    Text_t obj_file = Text$concat(filename, Text(".o"));
    Text_t c_file = Text$concat(filename, Text(".c"));
    Text_t h_file = Text$concat(filename, Text(".h"));
    if (!force_recompile && !is_stale(obj_file, filename)
        && !is_stale(obj_file, c_file)
        && !is_stale(obj_file, h_file)) {
        return;
    }

    FILE *prog = run_cmd("%k %k -O%k -c %k -o %k",
                         &cc, &cflags, &optimization, &c_file, &obj_file);
    if (!prog)
        errx(1, "Failed to run C compiler: %k", &cc);
    int status = pclose(prog);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        exit(EXIT_FAILURE);

    if (!quiet || verbose)
        printf("\x1b[2mCompiled to %k\x1b[m\n", &obj_file);
}

Text_t compile_executable(env_t *base_env, Text_t filename, Array_t object_files, Array_t extra_ldlibs)
{
    ast_t *ast = parse_file(Text$as_c_string(filename), NULL);
    if (!ast)
        errx(1, "Could not parse file %k", &filename);
    env_t *env = load_module_env(base_env, ast);
    binding_t *main_binding = get_binding(env, "main");
    if (!main_binding || main_binding->type->tag != FunctionType)
        errx(1, "No main() function has been defined for %k, so it can't be run!", &filename);

    Text_t bin_name = Text$trim(filename, Text(".tm"), false, true);
    FILE *runner = run_cmd("%k %k -O%k %k %k %s %s -x c - -o %k",
                           &cc, &cflags, &optimization, &ldflags, &ldlibs,
                           array_str(extra_ldlibs), array_str(object_files), &bin_name);
    CORD program = CORD_all(
        "extern int ", main_binding->code, "$parse_and_run(int argc, char *argv[]);\n"
        "int main(int argc, char *argv[]) {\n"
        "\treturn ", main_binding->code, "$parse_and_run(argc, argv);\n"
        "}\n"
    );

    if (show_codegen) {
        FILE *out = run_cmd("bat -P --file-name=run.c");
        CORD_put(program, out);
        pclose(out);
    }

    CORD_put(program, runner);
    int status = pclose(runner);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        exit(EXIT_FAILURE);

    if (!quiet || verbose)
        printf("\x1b[2mCompiled executable: %k\x1b[m\n", &bin_name);
    return bin_name;
}

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
