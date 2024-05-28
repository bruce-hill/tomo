// The main program that runs compilation
#include <ctype.h>
#include <gc.h>
#include <gc/cord.h>
#include <libgen.h>
#include <printf.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "ast.h"
#include "builtins/array.h"
#include "builtins/datatypes.h"
#include "builtins/text.h"
#include "compile.h"
#include "parse.h"
#include "repl.h"
#include "typecheck.h"
#include "types.h"

typedef enum { MODE_TRANSPILE = 0, MODE_COMPILE_OBJ = 1, MODE_COMPILE_SHARED_OBJ = 2, MODE_COMPILE_EXE = 3, MODE_RUN = 4 } mode_e;

static bool verbose = false;
static bool show_codegen = false;
static bool cleanup_files = false;
static const char *autofmt, *cconfig, *cflags, *objfiles, *ldlibs, *ldflags, *cc;

static array_t get_file_dependencies(const char *filename, array_t *object_files);
static int transpile(const char *filename, bool force_retranspile, module_code_t *module_code);
static int compile_object_file(const char *filename, bool force_recompile);
static int compile_executable(const char *filename, array_t object_files, module_code_t *module_code);

int main(int argc, char *argv[])
{
    mode_e mode = MODE_RUN;
    const char *filename = NULL;
    int program_arg_index = argc + 1;
    for (int i = 1; i < argc; i++) {
        if (streq(argv[i], "-t")) {
            mode = MODE_TRANSPILE;
        } else if (streq(argv[i], "-c")) {
            mode = MODE_COMPILE_OBJ;
        } else if (streq(argv[i], "-s")) {
            mode = MODE_COMPILE_SHARED_OBJ;
        } else if (streq(argv[i], "-r")) {
            mode = MODE_RUN;
        } else if (streq(argv[i], "-e")) {
            mode = MODE_COMPILE_EXE;
        } else if (streq(argv[i], "-C")) {
            cleanup_files = true;
        } else if (streq(argv[i], "-h") || streq(argv[i], "--help")) {
            printf("Usage: %s [[-t|-c|-e|-r] [-C] [option=value]* file.tm [args...]]\n", argv[0]);
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
            filename = argv[i];
            program_arg_index = i + 1;
            break;
        }
    }

    // register_printf_modifier(L"p");
    if (register_printf_specifier('T', printf_type, printf_pointer_size))
        errx(1, "Couldn't set printf specifier");
    if (register_printf_specifier('W', printf_ast, printf_pointer_size))
        errx(1, "Couldn't set printf specifier");

    setenv("TOMO_IMPORT_PATH", "~/.local/tomo/src:.", 0);

    autofmt = getenv("AUTOFMT");
    if (!autofmt) autofmt = "indent -kr -l100 -nbbo -nut -sob";
    if (!autofmt[0]) autofmt = "cat";

    verbose = (getenv("VERBOSE") && (streq(getenv("VERBOSE"), "1") || streq(getenv("VERBOSE"), "2")));
    show_codegen = (getenv("VERBOSE") && streq(getenv("VERBOSE"), "2"));

    if (filename == NULL) {
        repl();
        return 0;
    }

    if (strlen(filename) < strlen(".tm") + 1 || strncmp(filename + strlen(filename) - strlen(".tm"), ".tm", strlen(".tm")) != 0)
        errx(1, "Not a valid .tm file: %s", filename);

    cconfig = getenv("CCONFIG");
    if (!cconfig)
        cconfig = "-std=c11 -fdollars-in-identifiers -fsanitize=signed-integer-overflow -fno-sanitize-recover"
            " -D_XOPEN_SOURCE=700 -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE";

    const char *optimization = getenv("O");
    if (!optimization || !optimization[0]) optimization = "-O1";
    else optimization = heap_strf("-O%s", optimization);

    objfiles = getenv("OBJFILES");
    if (!objfiles)
        objfiles = "";

    cflags = getenv("CFLAGS");
    if (!cflags)
        cflags = heap_strf("%s %s -fPIC -ggdb -I./include -I%s/.local/tomo/include -D_DEFAULT_SOURCE", cconfig, optimization, getenv("HOME"));

    ldflags = heap_strf("-Wl,-rpath,'$ORIGIN',-rpath,'%s/.local/tomo/lib' -L. -L%s/.local/tomo/lib", getenv("HOME"), getenv("HOME"));

    ldlibs = "-lgc -lcord -lm -ltomo";
    if (getenv("LDLIBS"))
        ldlibs = heap_strf("%s %s", ldlibs, getenv("LDLIBS"));

    cc = getenv("CC");
    if (!cc) cc = "cc";

    array_t object_files = {};
    const char *my_obj = heap_strf("%s.o", resolve_path(filename, ".", "."));
    Array$insert(&object_files, &my_obj, 0, $ArrayInfo(&$Text));
    array_t file_deps = get_file_dependencies(filename, &object_files);

    module_code_t module_code = {};
    int transpile_status = transpile(filename, true, &module_code);
    if (transpile_status != 0) return transpile_status;

    for (int64_t i = 0; i < file_deps.length; i++) {
        const char *dep = *(char**)(file_deps.data + i*file_deps.stride);
        module_code_t _ = {};
        transpile_status = transpile(dep, false, &_);
        if (transpile_status != 0) return transpile_status;
    }

    int compile_status = compile_object_file(filename, true);
    if (compile_status != 0) return compile_status;

    if (mode == MODE_COMPILE_OBJ)
        return 0;

    for (int64_t i = 0; i < file_deps.length; i++) {
        const char *dep = *(char**)(file_deps.data + i*file_deps.stride);
        compile_status = compile_object_file(dep, false);
        if (compile_status != 0) return compile_status;
    }

    if (mode == MODE_COMPILE_SHARED_OBJ) {
        const char *base = file_base_name(filename);
        const char *outfile = heap_strf("lib%s.so", base);
        const char *cmd = heap_strf("%s %s %s %s %s -Wl,-soname,lib%s.so -shared %s.o -o %s", cc, cflags, ldflags, ldlibs, objfiles, base, filename, outfile);
        if (verbose)
            printf("Running: %s\n", cmd);
        FILE *prog = popen(cmd, "w");
        int status = pclose(prog);
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
            return WEXITSTATUS(status);
        if (verbose)
            printf("Compiled to %s\n", outfile);

        printf("Do you want to install your library? [Y/n] ");
        fflush(stdout);
        switch (getchar()) {
        case 'y': case 'Y': case '\n': {
            const char *name = file_base_name(filename);
            system(heap_strf("mkdir -p ~/.local/tomo/lib ~/.local/tomo/'%s'", name));
            system(heap_strf("cp -v 'lib%s.so' ~/.local/tomo/'%s'/", name, name));
            system(heap_strf("ln -sv '../%s/lib%s.so' ~/.local/tomo/lib/'lib%s.so'", name, name, name));
            for (int64_t i = 0; i < file_deps.length; i++) {
                const char *dep = *(char**)(file_deps.data + i*file_deps.stride);
                system(heap_strf("cp -v %s %s.c %s.h %s.o ~/.local/tomo/%s/", dep, dep, dep, dep, name));
            }
        }
        default: break;
        }
        return 0;
    }

    int executable_status = compile_executable(filename, object_files, &module_code);
    if (mode == MODE_COMPILE_EXE || executable_status != 0)
        return executable_status;

    if (cleanup_files) {
        for (int64_t i = 0; i < file_deps.length; i++) {
            const char *dep = *(char**)(file_deps.data + i*file_deps.stride);
            if (verbose)
                printf("Cleaning up %s files...\n", dep);

            system(heap_strf("rm -f %s.c %s.h %s.o", dep, dep, dep));
        }
    }

    char *exe_name = heap_strn(filename, strlen(filename) - strlen(".tm"));
    int num_args = argc - program_arg_index;
    char *prog_args[num_args + 2];
    prog_args[0] = exe_name;
    for (int i = 0; i < num_args; i++)
        prog_args[i+1] = argv[program_arg_index+i];
    prog_args[num_args+1] = NULL;
    execv(exe_name, prog_args);

    errx(1, "Failed to run compiled program");
}

static void build_file_dependency_graph(const char *filename, table_t *dependencies, array_t *object_files)
{
    size_t len = strlen(filename);
    assert(strncmp(filename + len - 3, ".tm", 3) == 0);

    if (Table$str_get(*dependencies, filename))
        return;

    array_t *deps = new(array_t);
    Array$insert(deps, &filename, 0, $ArrayInfo(&$Text));
    Table$str_set(dependencies, filename, deps);

    file_t *f = load_file(filename);
    if (!f)
        errx(1, "No such file: %s", filename);

    ast_t *ast = parse_file(f, NULL);
    if (!ast)
        errx(1, "Could not parse %s", f);

    char *file_dir = realpath(filename, NULL);
    dirname(file_dir);
    for (ast_list_t *stmt = Match(ast, Block)->statements; stmt; stmt = stmt->next) {
        const char *use_path = NULL;
        if (stmt->ast->tag == Use) {
            use_path = Match(stmt->ast, Use)->raw_path;
        } else if (stmt->ast->tag == Declare) {
            auto decl = Match(stmt->ast, Declare);
            if (decl->value->tag == Use)
                use_path = Match(decl->value, Use)->raw_path;
        }
        if (!use_path) continue;
        const char *import, *obj_file;
        if (use_path[0] == '/' || strncmp(use_path, "~/", 2) == 0 || strncmp(use_path, "./", 2) == 0 || strncmp(use_path, "../", 3) == 0) {
            import = resolve_path(use_path, filename, "");
            if (!import) errx(1, "Couldn't resolve path: %s", use_path);
            obj_file = heap_strf("%s.o", resolve_path(use_path, filename, ""));
        } else {
            import = resolve_path(use_path, filename, getenv("TOMO_IMPORT_PATH"));
            obj_file = heap_strf("-l%.*s", strlen(use_path)-3, use_path);
        }
        Array$insert(deps, &import, 0, $ArrayInfo(&$Text));
        Array$insert(object_files, &obj_file, 0, $ArrayInfo(&$Text));
        build_file_dependency_graph(import, dependencies, object_files);
    }
    free(file_dir);
}

array_t get_file_dependencies(const char *filename, array_t *object_files)
{
    const char *resolved = resolve_path(filename, ".", ".");
    if (!resolved) errx(1, "Couldn't resolve path: %s", filename);

    table_t file_dependencies = {};
    build_file_dependency_graph(resolved, &file_dependencies, object_files);
    table_t dependency_set = {};

    const TypeInfo unit = {.size=0, .align=0, .tag=CustomInfo};
    const TypeInfo info = {.size=sizeof(table_t), .align=__alignof__(table_t),
        .tag=TableInfo, .TableInfo.key=&$Text, .TableInfo.value=&unit};

    for (int64_t i = 1; i <= Table$length(file_dependencies); i++) {
        struct { const char *name; array_t *deps; } *entry = Table$entry(file_dependencies, i);
        for (int64_t j = 0; j < entry->deps->length; j++) {
            const char *dep = *(char**)(entry->deps->data + j*entry->deps->stride);
            Table$set(&dependency_set, &dep, &dep, &info);
        }
    }
    return dependency_set.entries;
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

int transpile(const char *filename, bool force_retranspile, module_code_t *module_code)
{
    const char *tm_file = filename;
    const char *c_filename = heap_strf("%s.c", tm_file);
    const char *h_filename = heap_strf("%s.h", tm_file);
    if (!force_retranspile && !is_stale(c_filename, tm_file) && !is_stale(h_filename, tm_file)) {
        return 0;
    }

    file_t *f = load_file(filename);
    if (!f)
        errx(1, "No such file: %s", filename);

    ast_t *ast = parse_file(f, NULL);
    if (!ast)
        errx(1, "Could not parse %s", f);

    if (show_codegen) {
        FILE *out = popen(heap_strf("bat -P --file-name='%s'", filename), "w");
        fputs(f->text, out);
        pclose(out);
    }

    if (show_codegen) {
        FILE *out = popen("xmllint --format - | bat -P --file-name=AST", "w");
        CORD_put(ast_to_xml(ast), out);
        pclose(out);
    }

    *module_code = compile_file(ast);

    FILE *h_file = fopen(h_filename, "w");
    if (!h_file)
        errx(1, "Couldn't open file: %s", h_filename);
    CORD_put("#pragma once\n", h_file);
    CORD_put(module_code->header, h_file);
    if (fclose(h_file))
        errx(1, "Failed to close file: %s", h_filename);
    if (verbose)
        printf("Transpiled to %s\n", h_filename);

    if (autofmt && autofmt[0]) {
        FILE *prog = popen(heap_strf("%s %s -o %s >/dev/null 2>/dev/null", autofmt, h_filename, h_filename), "w");
        pclose(prog);
    }

    FILE *c_file = fopen(c_filename, "w");
    if (!c_file)
        errx(1, "Couldn't open file: %s", c_filename);
    CORD_put(CORD_all(
            "#include <tomo/tomo.h>\n"
            "#include \"", module_code->module_name, ".tm.h\"\n\n",
            module_code->c_file), c_file);
    if (fclose(c_file))
        errx(1, "Failed to close file: %s", c_filename);
    if (verbose)
        printf("Transpiled to %s\n", c_filename);

    if (autofmt && autofmt[0]) {
        FILE *prog = popen(heap_strf("%s %s -o %s >/dev/null 2>/dev/null", autofmt, c_filename, c_filename), "w");
        pclose(prog);
    }

    if (show_codegen) {
        FILE *out = popen(heap_strf("bat -P %s %s", h_filename, c_filename), "w");
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
    const char *outfile = heap_strf("%s.o", filename);
    const char *cmd = heap_strf("%s %s -c %s.c -o %s", cc, cflags, filename, outfile);
    if (verbose)
        printf("Running: %s\n", cmd);
    FILE *prog = popen(cmd, "w");
    int status = pclose(prog);
    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        if (verbose)
            printf("Compiled to %s\n", outfile);
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : EXIT_FAILURE;
}

int compile_executable(const char *filename, array_t object_files, module_code_t *module_code)
{
    const char *bin_name = heap_strn(filename, strlen(filename) - strlen(".tm"));
    const char *run = heap_strf("%s | %s %s %s %s %s %s -x c - -o %s",
                                autofmt, cc, cflags, ldflags, ldlibs, objfiles, CORD_to_const_char_star(Text$join(" ", object_files)), bin_name);
    if (verbose)
        printf("%s\n", run);
    FILE *runner = popen(run, "w");

    binding_t *main_binding = get_binding(module_code->env, "main");
    CORD program = CORD_all(
        "#include <tomo/tomo.h>\n"
        "#include \"", filename, ".h\"\n"
        "\n"
        "int main(int argc, char *argv[]) {\n"
        "tomo_init();\n"
        "\n",
        main_binding && main_binding->type->tag == FunctionType ?
            CORD_all(compile_cli_arg_call(module_code->env, main_binding->code, main_binding->type), "return 0;\n")
            : "errx(1, \"No main function is defined!\");\n",
        "}\n"
    );

    if (show_codegen) {
        FILE *out = popen(heap_strf("%s | bat -P --file-name=run.c", autofmt), "w");
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
