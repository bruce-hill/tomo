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
#include "stdlib/arrays.h"
#include "stdlib/datatypes.h"
#include "stdlib/text.h"
#include "stdlib/patterns.h"
#include "compile.h"
#include "cordhelpers.h"
#include "parse.h"
#include "repl.h"
#include "typecheck.h"
#include "types.h"

#define ENV_CORD(name) CORD_from_char_star(getenv(name) ? getenv(name) : "")
#define CORD_RUN(...) ({ CORD _cmd = CORD_all(__VA_ARGS__); if (verbose) CORD_printf("\x1b[33m%r\x1b[m\n", _cmd); popen(CORD_to_const_char_star(_cmd), "w"); })

typedef enum { MODE_TRANSPILE = 0, MODE_COMPILE_OBJ = 1, MODE_COMPILE_SHARED_OBJ = 2, MODE_COMPILE_EXE = 3, MODE_RUN = 4 } mode_e;

static bool verbose = false;
static bool show_codegen = false;
static CORD autofmt = CORD_EMPTY, cconfig = CORD_EMPTY, cflags = CORD_EMPTY, ldlibs = CORD_EMPTY, ldflags = CORD_EMPTY, cc = CORD_EMPTY;

static void transpile_header(env_t *base_env, const char *filename, bool force_retranspile);
static void transpile_code(env_t *base_env, const char *filename, bool force_retranspile);
static void compile_object_file(const char *filename, bool force_recompile);
static void compile_executable(env_t *base_env, const char *filename, CORD object_files, CORD extra_ldlibs);
static void build_file_dependency_graph(const char *filename, Table_t *to_compile, Table_t *to_link);
static const char *escape_lib_name(const char *lib_name);
static void build_library(const char *lib_base_name);
static void compile_files(env_t *env, int filec, const char **filev, bool only_compile_arguments, CORD *object_files, CORD *ldlibs);

#pragma GCC diagnostic ignored "-Wstack-protector"
int main(int argc, char *argv[])
{
    mode_e mode = MODE_RUN;
    int after_flags = 1;
    for (int i = 1; i < argc; i++) {
        if (streq(argv[i], "-t")) {
            mode = MODE_TRANSPILE;
        } else if (streq(argv[i], "-c")) {
            mode = MODE_COMPILE_OBJ;
        } else if (streq(argv[i], "-L")) {
            mode = MODE_COMPILE_SHARED_OBJ;
        } else if (streq(argv[i], "-r")) {
            mode = MODE_RUN;
        } else if (streq(argv[i], "-e")) {
            mode = MODE_COMPILE_EXE;
        } else if (streq(argv[i], "-h") || streq(argv[i], "--help")) {
            printf("Usage: %s | %s [-r] file.tm args... | %s (-t|-c) file1.tm file2.tm... | %s -L [dir]\n",
                   argv[0], argv[0], argv[0], argv[0]);
            return 0;
        } else if (streq(argv[i], "-u")) {
            // Uninstall libraries:
            for (int j = i + 1; j < argc; j++) {
                system(heap_strf("rm -rvf ~/.local/share/tomo/installed/%s ~/.local/share/tomo/lib/lib%s.so",
                                 argv[j], argv[j], argv[j]));
            }
            return 0;
        } else if (strchr(argv[i], '=')) {
            while (argv[i][0] == '-')
                ++argv[i];
            char *eq = strchr(argv[i], '=');
            *eq = '\0';
            for (char *p = argv[i]; *p; p++)
                *p = toupper(*p);
            setenv(argv[i], eq + 1, 1);
        } else {
            after_flags = i;
            break;
        }
        after_flags = i + 1;
    }

    if (register_printf_specifier('T', printf_type, printf_pointer_size))
        errx(1, "Couldn't set printf specifier");
    if (register_printf_specifier('W', printf_ast, printf_pointer_size))
        errx(1, "Couldn't set printf specifier");
    if (register_printf_specifier('k', printf_text, printf_text_size))
        errx(1, "Couldn't set printf specifier");

    CORD home = ENV_CORD("HOME");
    autofmt = ENV_CORD("AUTOFMT");
    if (!autofmt) autofmt = "sed '/^\\s*$/d' | indent -kr -l100 -nbbo -nut -sob";
    if (!autofmt[0]) autofmt = "cat";

    verbose = (getenv("VERBOSE") && (streq(getenv("VERBOSE"), "1") || streq(getenv("VERBOSE"), "2")));
    show_codegen = (getenv("VERBOSE") && streq(getenv("VERBOSE"), "2"));

    if (after_flags >= argc && mode != MODE_COMPILE_SHARED_OBJ) {
        repl();
        return 0;
    }

    cconfig = ENV_CORD("CCONFIG");
    if (!cconfig)
        cconfig = "-fdollars-in-identifiers -std=gnu11 -Wno-trigraphs -fsanitize=signed-integer-overflow -fno-sanitize-recover"
            " -D_XOPEN_SOURCE=700 -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE";

    CORD optimization = ENV_CORD("O");
    if (!optimization) optimization = CORD_EMPTY;
    else optimization = CORD_all("-O", optimization);

    cflags = ENV_CORD("CFLAGS");
    if (!cflags)
        cflags = CORD_all(cconfig, " ", optimization, " -fPIC -ggdb -D_DEFAULT_SOURCE");
    cflags = CORD_all(cflags, " -I'", home, "/.local/share/tomo/installed'");

    ldflags = CORD_all("-Wl,-rpath='$ORIGIN',-rpath='", home, "/.local/share/tomo/lib' -L. -L'", home, "/.local/share/tomo/lib'");

    ldlibs = "-lgc -lgmp -lm -ltomo";

    cc = ENV_CORD("CC");
    if (!cc) cc = "cc";

    if (mode == MODE_COMPILE_EXE || mode == MODE_RUN) {
        const char *filename = argv[after_flags];

        env_t *env = new_compilation_unit(NULL);
        CORD object_files, extra_ldlibs;
        compile_files(env, 1, &filename, false, &object_files, &extra_ldlibs);
        compile_executable(env, filename, object_files, extra_ldlibs);
        if (mode == MODE_COMPILE_EXE)
            return 0;

        char *exe_name = GC_strndup(filename, strlen(filename) - strlen(".tm"));
        int num_args = argc - after_flags - 1;
        char *prog_args[num_args + 2];
        prog_args[0] = exe_name;
        for (int i = 0; i < num_args; i++)
            prog_args[i+1] = argv[after_flags + i + 1];
        prog_args[num_args+1] = NULL;
        execv(exe_name, prog_args);
        errx(1, "Failed to run compiled program");
    } else if (mode == MODE_COMPILE_OBJ) {
        env_t *env = new_compilation_unit(NULL);
        compile_files(env, argc - after_flags, (const char**)&argv[after_flags], true, NULL, NULL);
    } else if (mode == MODE_COMPILE_SHARED_OBJ) {
        char *cwd = get_current_dir_name();
        for (int i = after_flags; i < argc; i++) {
            if (chdir(argv[i]) != 0)
                errx(1, "Could not enter directory: %s", argv[i]);
            char *libdir = get_current_dir_name();
            char *libdirname = basename(libdir);
            build_library(libdirname);
            free(libdir);
            chdir(cwd);
        }
        free(cwd);
    }
    return 0;
}

const char *escape_lib_name(const char *lib_name)
{
    return Text$as_c_string(
        Text$replace(Text$from_str(lib_name), Pattern("{1+ !alphanumeric}"), Text("_"), Pattern(""), false));
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

        if (!Table$str_get(*info->used_imports, use->path)) {
            Table$str_set(info->used_imports, use->path, use->path);
            CORD_put(compile_statement_header(info->env, ast), info->output);
        }
    } else {
        CORD_put(compile_statement_header(info->env, ast), info->output);
    }
}

static void _compile_file_header_for_library(env_t *env, const char *filename, Table_t *visited_files, Table_t *used_imports, FILE *output)
{
    if (Table$str_get(*visited_files, filename))
        return;

    Table$str_set(visited_files, filename, filename);

    ast_t *file_ast = parse_file(filename, NULL);
    if (!file_ast) errx(1, "Could not parse file %s", filename);
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
            const char *used_filename = resolve_path(use->path, filename, ".");
            _compile_file_header_for_library(env, used_filename, visited_files, used_imports, output);
        }
    }

    visit_topologically(
        Match(file_ast, Block)->statements, (Closure_t){.fn=(void*)_compile_statement_header_for_library, &info});

    CORD_fprintf(output, "void %r$initialize(void);\n", namespace_prefix(module_env->libname, module_env->namespace));
}

void build_library(const char *lib_base_name)
{
    glob_t tm_files;
    char *library_directory = get_current_dir_name();
    if (glob("[!._0-9]*.tm", 0, NULL, &tm_files) != 0)
        errx(1, "Couldn't get .tm files in directory: %s", library_directory);
    env_t *env = new_compilation_unit(NULL);
    CORD object_files, extra_ldlibs;
    compile_files(env, (int)tm_files.gl_pathc, (const char**)tm_files.gl_pathv, false, &object_files, &extra_ldlibs);

    // Library name replaces all stretchs of non-alphanumeric chars with an underscore
    // So e.g. https://github.com/foo/baz --> https_github_com_foo_baz
    const char *libname = escape_lib_name(lib_base_name);
    env->libname = &libname;

    // Build a "whatever.h" header that loads all the headers:
    FILE *header_prog = CORD_RUN(autofmt ? autofmt : "cat", " 2>/dev/null >", libname, ".h");
    fputs("#pragma once\n", header_prog);
    fputs("#include <tomo/tomo.h>\n", header_prog);
    Table_t visited_files = {};
    Table_t used_imports = {};
    for (size_t i = 0; i < tm_files.gl_pathc; i++) {
        const char *filename = tm_files.gl_pathv[i];
        filename = resolve_path(filename, ".", ".");
        _compile_file_header_for_library(env, filename, &visited_files, &used_imports, header_prog);
    }
    if (pclose(header_prog) == -1)
        errx(1, "Failed to run autoformat program on header file: %s", autofmt);

    // Build up a list of symbol renamings:
    unlink("symbol_renames.txt");
    FILE *prog;
    for (size_t i = 0; i < tm_files.gl_pathc; i++) {
        const char *filename = tm_files.gl_pathv[i];
        prog = CORD_RUN("nm -Ug -fjust-symbols ", filename, ".o | sed 's/.*/\\0 ", libname, "$\\0/' >>symbol_renames.txt");
        int status = pclose(prog);
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
            errx(WEXITSTATUS(status), "Failed to create symbol rename table with `nm` and `sed`");
    }

    globfree(&tm_files);

    prog = CORD_RUN(cc, " ", cflags, " ", ldflags, " ", ldlibs, " ", extra_ldlibs, " -Wl,-soname=lib", libname, ".so -shared ", object_files, " -o lib", libname, ".so");
    int status = pclose(prog);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        errx(WEXITSTATUS(status), "Failed to compile shared library file");
    if (verbose)
        CORD_printf("Compiled to lib%s.so\n", libname);

    prog = CORD_RUN("objcopy --redefine-syms=symbol_renames.txt lib", libname, ".so");
    status = pclose(prog);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        errx(WEXITSTATUS(status), "Failed to run `objcopy` to add library prefix to symbols");

    prog = CORD_RUN("patchelf --rename-dynamic-symbols symbol_renames.txt lib", libname, ".so");
    status = pclose(prog);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        errx(WEXITSTATUS(status), "Failed to run `patchelf` to rename dynamic symbols with library prefix");

    if (verbose)
        CORD_printf("Successfully renamed symbols with library prefix!\n");

    unlink("symbol_renames.txt");

    printf("Do you want to install %s? [Y/n] ", libname);
    fflush(stdout);
    switch (getchar()) {
    case 'y': case 'Y':
        getchar();
        // Fall through
    case '\n': {
        const char *dest = heap_strf("%s/.local/share/tomo/installed/%s", getenv("HOME"), lib_base_name);
        if (!streq(library_directory, dest)) {
            system(heap_strf("rm -rvf '%s'", dest));
            system(heap_strf("mkdir -p '%s'", dest));
            system(heap_strf("cp -rv * '%s/'", dest));
        }
        system("mkdir -p ~/.local/share/tomo/lib/");
        system(heap_strf("ln -fv -s ../installed/'%s'/lib%s.so  ~/.local/share/tomo/lib/lib%s.so", lib_base_name, libname, libname));
    }
    default: break;
    }

    free(library_directory);
}

void compile_files(env_t *env, int filec, const char **filev, bool only_compile_arguments, CORD *object_files, CORD *extra_ldlibs)
{
    Table_t to_link = {};
    Table_t argument_files = {};
    Table_t dependency_files = {};
    for (int i = 0; i < filec; i++) {
        if (strlen(filev[i]) < 4 || strncmp(filev[i] + strlen(filev[i]) - 3, ".tm", 3) != 0)
            errx(1, "Not a valid .tm file: \x1b[31;1m%s\x1b[m", filev[i]);
        const char *resolved = resolve_path(filev[i], ".", ".");
        if (!resolved) errx(1, "Couldn't resolve path: %s", filev[i]);
        Table$str_set(&argument_files, resolved, filev[i]);
        build_file_dependency_graph(resolved, &dependency_files, &to_link);
    }

    int status;
    // (Re)compile header files, eagerly for explicitly passed in files, lazily
    // for downstream dependencies:
    for (int64_t i = 0; i < dependency_files.entries.length; i++) {
        const char *filename = *(char**)(dependency_files.entries.data + i*dependency_files.entries.stride);
        bool is_argument_file = (Table$str_get(argument_files, filename) != NULL);
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
        const char *filename = *(char**)(dependency_files.entries.data + i*dependency_files.entries.stride);
        bool is_argument_file = (Table$str_get(argument_files, filename) != NULL);
        if (!is_argument_file && only_compile_arguments)
            continue;

        pid_t pid = fork();
        if (pid == 0) {
            transpile_code(env, filename, is_argument_file);
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
        *object_files = CORD_EMPTY;
        for (int64_t i = 0; i < dependency_files.entries.length; i++) {
            const char *filename = *(char**)(dependency_files.entries.data + i*dependency_files.entries.stride);
            *object_files = CORD_all(*object_files, filename, ".o ");
        }
    }
    if (extra_ldlibs) {
        *extra_ldlibs = CORD_EMPTY;
        for (int64_t i = 0; i < to_link.entries.length; i++) {
            const char *lib = *(char**)(to_link.entries.data + i*to_link.entries.stride);
            *extra_ldlibs = CORD_all(*extra_ldlibs, " ", lib);
        }
    }
}

void build_file_dependency_graph(const char *filename, Table_t *to_compile, Table_t *to_link)
{
    if (Table$str_get(*to_compile, filename))
        return;

    Table$str_set(to_compile, filename, filename);

    size_t len = strlen(filename);
    assert(strncmp(filename + len - 3, ".tm", 3) == 0);

    ast_t *ast = parse_file(filename, NULL);
    if (!ast)
        errx(1, "Could not parse file %s", filename);

    char *file_dir = realpath(filename, NULL);
    dirname(file_dir);
    for (ast_list_t *stmt = Match(ast, Block)->statements; stmt; stmt = stmt->next) {
        ast_t *stmt_ast = stmt->ast;
        if (stmt_ast->tag == Declare)
            stmt_ast = Match(stmt_ast, Declare)->value;

        if (stmt_ast->tag != Use) continue;
        auto use = Match(stmt_ast, Use);

        switch (use->what) {
        case USE_LOCAL: {
            const char *path = use->path;
            path = resolve_path(path, filename, "");
            if (!path) errx(1, "Couldn't resolve import: %s relative to: %s", use->path, filename);
            if (Table$str_get(*to_compile, path))
                continue;
            build_file_dependency_graph(path, to_compile, to_link);
            Table$str_set(to_compile, path, path);
            break;
        }
        case USE_MODULE: {
            const char *lib_name = escape_lib_name(use->path);
            // const char *lib = heap_strf("-l:'lib%s.so'", lib_name);
            const char *lib = heap_strf("'%s/.local/share/tomo/installed/%s/lib%s.so'", getenv("HOME"), lib_name, lib_name);
            Table$str_set(to_link, lib, lib);
            break;
        }
        case USE_SHARED_OBJECT: {
            const char *lib = heap_strf("-l:%s", use->path);
            Table$str_set(to_link, lib, lib);
            break;
        }
        case USE_ASM: {
            Table$str_set(to_link, use->path, use->path);
            break;
        }
        default: case USE_HEADER: break;
        }
    }
    free(file_dir);
}

static bool is_stale(const char *filename, const char *relative_to)
{
    struct stat target_stat;
    if (stat(filename, &target_stat) != 0)
        return true;
    struct stat relative_to_stat;
    if (stat(relative_to, &relative_to_stat) != 0)
        errx(1, "File doesn't exist: %s", relative_to);
    return target_stat.st_mtime < relative_to_stat.st_mtime;
}

void transpile_header(env_t *base_env, const char *filename, bool force_retranspile)
{
    const char *h_filename = heap_strf("%s.h", filename);
    if (!force_retranspile && !is_stale(h_filename, filename))
        return;

    ast_t *ast = parse_file(filename, NULL);
    if (!ast)
        errx(1, "Could not parse file %s", filename);

    env_t *module_env = load_module_env(base_env, ast);

    CORD h_code = compile_file_header(module_env, ast);

    if (autofmt) {
        FILE *prog = CORD_RUN(autofmt, " 2>/dev/null >", h_filename);
        CORD_put(h_code, prog);
        if (pclose(prog) == -1)
            errx(1, "Failed to run autoformat program on header file: %s", autofmt);
    } else {
        FILE *h_file = fopen(h_filename, "w");
        if (!h_file)
            errx(1, "Couldn't open file: %s", h_filename);
        CORD_put(h_code, h_file);
        if (fclose(h_file))
            errx(1, "Failed to close file: %s", h_filename);
    }

    if (verbose)
        printf("Transpiled to %s\n", h_filename);

    if (show_codegen) {
        FILE *out = CORD_RUN("bat -P ", h_filename);
        pclose(out);
    }
}

void transpile_code(env_t *base_env, const char *filename, bool force_retranspile)
{
    const char *c_filename = heap_strf("%s.c", filename);
    if (!force_retranspile && !is_stale(c_filename, filename))
        return;

    ast_t *ast = parse_file(filename, NULL);
    if (!ast)
        errx(1, "Could not parse file %s", filename);

    env_t *module_env = load_module_env(base_env, ast);

    CORD c_code = compile_file(module_env, ast);

    FILE *out;
    bool is_popened = false;
    if (autofmt) {
        out = CORD_RUN(autofmt, " 2>/dev/null >'", c_filename, "'");
        if (!out)
            errx(1, "Failed to run autoformat program: %s", autofmt);
        is_popened = true;
    } else {
        out = fopen(c_filename, "w");
        if (!out)
            errx(1, "Couldn't open file: %s", c_filename);
        is_popened = false;
    }

    CORD_put(c_code, out);

    binding_t *main_binding = get_binding(module_env, "main");
    if (main_binding && main_binding->type->tag == FunctionType) {
        CORD_put(CORD_all(
            "int ", main_binding->code, "$parse_and_run(int argc, char *argv[]) {\n"
            "tomo_init();\n",
            module_env->namespace->name, "$$initialize();\n"
            "\n",
            compile_cli_arg_call(module_env, main_binding->code, main_binding->type),
            "return 0;\n"
            "}\n"), out);
    }

    if (is_popened) {
        if (pclose(out) == -1)
            errx(1, "Failed to output autoformatted C code to %s: %s", c_filename, autofmt);
    } else {
        if (fclose(out))
            errx(1, "Failed to close file: %s", c_filename);
    }

    if (verbose)
        printf("Transpiled to %s\n", c_filename);

    if (show_codegen) {
        out = CORD_RUN("bat -P ", c_filename);
        pclose(out);
    }
}

void compile_object_file(const char *filename, bool force_recompile)
{
    const char *obj_file = heap_strf("%s.o", filename);
    if (!force_recompile && !is_stale(obj_file, filename)
        && !is_stale(obj_file, heap_strf("%s.c", filename))
        && !is_stale(obj_file, heap_strf("%s.h", filename))) {
        return;
    }
    CORD outfile = CORD_all(filename, ".o");
    FILE *prog = CORD_RUN(cc, " ", cflags, " -c ", filename, ".c -o ", outfile);
    int status = pclose(prog);
    if (!WIFEXITED(status) || !WEXITSTATUS(status) == 0)
        exit(EXIT_FAILURE);

    if (verbose)
        CORD_printf("Compiled to %r\n", outfile);
}

void compile_executable(env_t *base_env, const char *filename, CORD object_files, CORD extra_ldlibs)
{
    ast_t *ast = parse_file(filename, NULL);
    if (!ast)
        errx(1, "Could not parse file %s", filename);
    env_t *env = load_module_env(base_env, ast);
    binding_t *main_binding = get_binding(env, "main");
    if (!main_binding || main_binding->type->tag != FunctionType)
        errx(1, "No main() function has been defined for %s, so it can't be run!", filename);

    const char *bin_name = GC_strndup(filename, strlen(filename) - strlen(".tm"));
    FILE *runner = CORD_RUN(cc, " ", cflags, " ", ldflags, " ", ldlibs, " ", extra_ldlibs, " ", object_files, " -x c - -o ", bin_name);

    CORD program = CORD_all(
        "extern int ", main_binding->code, "$parse_and_run(int argc, char *argv[]);\n"
        "int main(int argc, char *argv[]) {\n"
        "\treturn ", main_binding->code, "$parse_and_run(argc, argv);\n"
        "}\n"
    );

    if (show_codegen) {
        FILE *out = CORD_RUN("bat -P --file-name=run.c");
        CORD_put(program, out);
        pclose(out);
    }

    CORD_put(program, runner);
    int status = pclose(runner);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        exit(EXIT_FAILURE);

    if (verbose)
        printf("Compiled executable: %s\n", bin_name);
}

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
