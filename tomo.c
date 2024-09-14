// The main program that runs compilation
#include <ctype.h>
#include <gc.h>
#include <gc/cord.h>
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
static CORD autofmt, cconfig, cflags, ldlibs, ldflags, cc;

static int transpile_header(env_t *base_env, const char *filename, bool force_retranspile);
static int transpile_code(env_t *base_env, const char *filename, bool force_retranspile);
static int compile_object_file(const char *filename, bool force_recompile);
static int compile_executable(env_t *base_env, const char *filename, CORD object_files);
static void build_file_dependency_graph(const char *filename, Table_t *to_compile, Table_t *to_link);

#pragma GCC diagnostic ignored "-Wstack-protector"
int main(int argc, char *argv[])
{
    mode_e mode = MODE_RUN;
    int after_flags = 1;
    const char *libname = NULL;
    for (int i = 1; i < argc; i++) {
        if (streq(argv[i], "-t")) {
            mode = MODE_TRANSPILE;
        } else if (streq(argv[i], "-c")) {
            mode = MODE_COMPILE_OBJ;
        } else if (strncmp(argv[i], "-s=", 3) == 0) {
            mode = MODE_COMPILE_SHARED_OBJ;
            libname = argv[i] + strlen("-s=");
        } else if (streq(argv[i], "-s")) {
            mode = MODE_COMPILE_SHARED_OBJ;
            if (i+1 >= argc)
                errx(1, "You must provide at least one file to build a shared library");
            libname = file_base_name(argv[i+1]);
        } else if (streq(argv[i], "-r")) {
            mode = MODE_RUN;
        } else if (streq(argv[i], "-e")) {
            mode = MODE_COMPILE_EXE;
        } else if (streq(argv[i], "-h") || streq(argv[i], "--help")) {
            printf("Usage: %s | %s [-r] file.tm args... | %s (-t|-c|-s) file1.tm file2.tm...\n", argv[0], argv[0], argv[0]);
            return 0;
        } else if (streq(argv[i], "-u")) {
            // Uninstall libraries:
            for (int j = i + 1; j < argc; j++) {
                system(heap_strf("rm -rvf ~/.local/src/tomo/%s ~/.local/include/tomo/lib%s.h ~/.local/lib/tomo/lib%s.so",
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
    }

    if (register_printf_specifier('T', printf_type, printf_pointer_size))
        errx(1, "Couldn't set printf specifier");
    if (register_printf_specifier('W', printf_ast, printf_pointer_size))
        errx(1, "Couldn't set printf specifier");
    if (register_printf_specifier('k', printf_text, printf_text_size))
        errx(1, "Couldn't set printf specifier");

    setenv("TOMO_IMPORT_PATH", "~/.local/src/tomo:.", 0);
    setenv("TOMO_LIB_PATH", "~/.local/lib/tomo:.", 0);

    CORD home = ENV_CORD("HOME");
    autofmt = ENV_CORD("AUTOFMT");
    if (!autofmt) autofmt = "sed '/^\\s*$/d' | indent -kr -l100 -nbbo -nut -sob";
    if (!autofmt[0]) autofmt = "cat";

    verbose = (getenv("VERBOSE") && (streq(getenv("VERBOSE"), "1") || streq(getenv("VERBOSE"), "2")));
    show_codegen = (getenv("VERBOSE") && streq(getenv("VERBOSE"), "2"));

    if (after_flags >= argc) {
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
        cflags = CORD_all(cconfig, " ", optimization, " -fPIC -ggdb -I./include -I'", home, "/.local/include' -D_DEFAULT_SOURCE");

    ldflags = CORD_all("-Wl,-rpath='$ORIGIN',-rpath='", home, "/.local/lib/tomo' -L. -L'", home, "/.local/lib/tomo'");

    ldlibs = "-lgc -lgmp -lm -ltomo";

    cc = ENV_CORD("CC");
    if (!cc) cc = "cc";

    CORD compilation_library_name = CORD_EMPTY;
    env_t *env = new_compilation_unit(&compilation_library_name);
    Table_t dependency_files = {};
    Table_t to_link = {};
    Table_t argument_files = {};

    for (int i = after_flags; i < argc; i++) {
        if (strlen(argv[i]) < 4 || strncmp(argv[i] + strlen(argv[i]) - 3, ".tm", 3) != 0)
            errx(1, "Not a valid .tm file: \x1b[31;1m%s\x1b[m", argv[i]);
        const char *resolved = resolve_path(argv[i], ".", ".");
        if (!resolved) errx(1, "Couldn't resolve path: %s", argv[i]);
        Table$str_set(&argument_files, resolved, argv[i]);
        build_file_dependency_graph(resolved, &dependency_files, &to_link);
        if (mode == MODE_RUN) break;
    }

    int status;
    // (Re)compile header files, eagerly for explicitly passed in files, lazily
    // for downstream dependencies:
    for (int64_t i = 0; i < dependency_files.entries.length; i++) {
        const char *filename = *(char**)(dependency_files.entries.data + i*dependency_files.entries.stride);
        bool is_argument_file = (Table$str_get(argument_files, filename) != NULL);
        status = transpile_header(env, filename, is_argument_file);
        if (status != 0) return status;
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
        if (mode == MODE_COMPILE_OBJ && !is_argument_file)
            continue;

        pid_t pid = fork();
        if (pid == 0) {
            status = transpile_code(env, filename, is_argument_file);
            if (status != 0)
                _exit(status);
            status = compile_object_file(filename, is_argument_file);
            _exit(status);
        }
        child_processes = new(struct child_s, .next=child_processes, .pid=pid);
    }

    for (; child_processes; child_processes = child_processes->next) {
        waitpid(child_processes->pid, &status, 0);
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
            return EXIT_FAILURE;
    }

    if (mode == MODE_COMPILE_OBJ)
        return 0;

    CORD object_files = CORD_EMPTY;
    for (int64_t i = 0; i < dependency_files.entries.length; i++) {
        const char *filename = *(char**)(dependency_files.entries.data + i*dependency_files.entries.stride);
        object_files = CORD_all(object_files, filename, ".o ");
    }
    for (int64_t i = 0; i < to_link.entries.length; i++) {
        const char *lib = *(char**)(to_link.entries.data + i*to_link.entries.stride);
        ldlibs = CORD_all(ldlibs, " ", lib);
    }

    // For shared objects, link up all the object files into one .so file:
    if (mode == MODE_COMPILE_SHARED_OBJ) {
        char *libname_id = GC_strdup(libname);
        for (char *p = libname_id; *p; p++) {
            if (!isalnum(*p) && *p != '_' && *p != '$')
                *p = '_';
        }
        compilation_library_name = libname_id;

        // Build a "libwhatever.h" header that loads all the headers:
        const char *h_filename = heap_strf("lib%s.h", libname);
        FILE *header_prog = CORD_RUN(autofmt ? autofmt : "cat", " 2>/dev/null >", h_filename);
        fputs("#pragma once\n", header_prog);
        for (int i = after_flags; i < argc; i++) {
            const char *filename = argv[i];
            ast_t *ast = parse_file(filename, NULL);
            if (!ast) errx(1, "Could not parse file %s", filename);
            env_t *module_env = load_module_env(env, ast);
            for (ast_list_t *stmt = Match(ast, Block)->statements; stmt; stmt = stmt->next) {
                CORD h = compile_statement_typedefs(module_env, stmt->ast);
                if (h) CORD_put(h, header_prog);
            }
            for (ast_list_t *stmt = Match(ast, Block)->statements; stmt; stmt = stmt->next) {
                CORD h = compile_statement_definitions(module_env, stmt->ast);
                if (h) CORD_put(h, header_prog);
            }
            fprintf(header_prog, "void %s$%s$$initialize(void);\n", libname, file_base_name(filename));
        }
        if (pclose(header_prog) == -1)
            errx(1, "Failed to run autoformat program on header file: %s", autofmt);

        // Also output a "libwhatever.files" file that lists the .tm files it used:
        const char *files_filename = heap_strf("lib%s.files", libname);
        FILE *files_file = fopen(files_filename, "w");
        if (!files_file)
            errx(1, "Couldn't open file: %s", files_filename);
        for (int i = after_flags; i < argc; i++) {
            fprintf(files_file, "%s\n", argv[i]);
        }
        if (fclose(files_file))
            errx(1, "Failed to close file: %s", files_filename);

        // Build up a list of symbol renamings:
        unlink("symbol_renames.txt");
        FILE *prog;
        for (int i = after_flags; i < argc; i++) {
            prog = CORD_RUN("nm -U -fjust-symbols ", argv[i], ".o | sed 's/.*/\\0 ", libname_id, "$\\0/' >>symbol_renames.txt");
            status = pclose(prog);
            if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
                errx(WEXITSTATUS(status), "Failed to create symbol rename table with `nm` and `sed`");
        }

        CORD outfile = CORD_all("lib", libname, ".so");
        prog = CORD_RUN(cc, " ", cflags, " ", ldflags, " ", ldlibs, " -Wl,-soname=", outfile, " -shared ", object_files, " -o ", outfile);
        status = pclose(prog);
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
            errx(WEXITSTATUS(status), "Failed to compile shared library file");
        if (verbose)
            CORD_printf("Compiled to %r\n", outfile);

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

        printf("Do you want to install your library? [Y/n] ");
        fflush(stdout);
        switch (getchar()) {
        case 'y': case 'Y': case '\n': {
            system(heap_strf("mkdir -p ~/.local/lib/tomo ~/.local/include/tomo ~/.local/src/tomo/'%s'", libname));
            system(heap_strf("cp -rv . ~/.local/src/tomo/'%s'", libname));
            system(heap_strf("ln -sfv '../../src/tomo/%s/lib%s.so' ~/.local/lib/tomo/'lib%s.so'", libname, libname, libname));
            system(heap_strf("ln -sfv '../../src/tomo/%s/lib%s.h' ~/.local/include/tomo/'lib%s.h'", libname, libname, libname));
        }
        default: break;
        }
        return 0;
    } else {
        const char *filename = argv[after_flags];
        int executable_status = compile_executable(env, filename, object_files);
        if (mode == MODE_COMPILE_EXE || executable_status != 0)
            return executable_status;

        char *exe_name = GC_strndup(filename, strlen(filename) - strlen(".tm"));
        int num_args = argc - after_flags - 1;
        char *prog_args[num_args + 2];
        prog_args[0] = exe_name;
        for (int i = 0; i < num_args; i++)
            prog_args[i+1] = argv[after_flags+1+i];
        prog_args[num_args+1] = NULL;
        execv(exe_name, prog_args);

        errx(1, "Failed to run compiled program");
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
            const char *base_name = file_base_name(use->path);
            const char *lib_path = heap_strf("%s/lib%s.so", base_name, base_name);
            const char *libfile = resolve_path(lib_path, filename, getenv("TOMO_IMPORT_PATH"));
            if (!libfile) errx(1, "Couldn't resolve path: %s", lib_path);
            const char *lib = heap_strf("-l%s", use->path);
            Table$str_set(to_link, lib, lib);
            break;
        }
        case USE_SHARED_OBJECT: {
            const char *lib = heap_strf("-l:%s", use->path);
            Table$str_set(to_link, lib, lib);
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
    if (stat(filename, &target_stat))
        return true;
    struct stat relative_to_stat;
    if (stat(relative_to, &relative_to_stat))
        errx(1, "File doesn't exist: %s", relative_to);
    return target_stat.st_mtime < relative_to_stat.st_mtime;
}

int transpile_header(env_t *base_env, const char *filename, bool force_retranspile)
{
    const char *h_filename = heap_strf("%s.h", filename);
    if (!force_retranspile && !is_stale(h_filename, filename)) {
        return 0;
    }

    ast_t *ast = parse_file(filename, NULL);
    if (!ast)
        errx(1, "Could not parse file %s", filename);

    env_t *module_env = load_module_env(base_env, ast);

    CORD h_code = compile_header(module_env, ast);

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

    return 0;
}

int transpile_code(env_t *base_env, const char *filename, bool force_retranspile)
{
    const char *c_filename = heap_strf("%s.c", filename);
    if (!force_retranspile && !is_stale(c_filename, filename)) {
        return 0;
    }

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

    return 0;
}

int compile_object_file(const char *filename, bool force_recompile)
{
    const char *obj_file = heap_strf("%s.o", filename);
    if (!force_recompile && !is_stale(obj_file, filename)
        && !is_stale(obj_file, heap_strf("%s.c", filename))
        && !is_stale(obj_file, heap_strf("%s.h", filename))) {
        return 0;
    }
    CORD outfile = CORD_all(filename, ".o");
    FILE *prog = CORD_RUN(cc, " ", cflags, " -c ", filename, ".c -o ", outfile);
    int status = pclose(prog);
    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        if (verbose)
            CORD_printf("Compiled to %r\n", outfile);
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : EXIT_FAILURE;
}

int compile_executable(env_t *base_env, const char *filename, CORD object_files)
{
    ast_t *ast = parse_file(filename, NULL);
    if (!ast)
        errx(1, "Could not parse file %s", filename);
    env_t *env = load_module_env(base_env, ast);
    binding_t *main_binding = get_binding(env, "main");
    if (!main_binding || main_binding->type->tag != FunctionType)
        errx(1, "No main() function has been defined for %s, so it can't be run!", filename);

    const char *bin_name = GC_strndup(filename, strlen(filename) - strlen(".tm"));
    FILE *runner = CORD_RUN(cc, " ", cflags, " ", ldflags, " ", ldlibs, " ", object_files, " -x c - -o ", bin_name);

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
    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        if (verbose)
            printf("Compiled executable: %s\n", bin_name);
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : EXIT_FAILURE;
}

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
