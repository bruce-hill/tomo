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
static const char *autofmt, *cconfig, *cflags, *objfiles, *ldlibs, *ldflags, *cc;

static array_t get_file_dependencies(const char *filename);
static int transpile(const char *filename, bool force_retranspile, module_code_t *module_code);
static int compile_object_file(const char *filename, bool force_recompile, bool shared);
static int compile_executable(const char *filename, const char *object_files, module_code_t *module_code);

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
        } else if (streq(argv[i], "-h") || streq(argv[i], "--help")) {
            printf("Usage: %s [[-t|-c|-e|-r] [option=value]* file.tm [args...]]\n", argv[0]);
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

    autofmt = getenv("AUTOFMT");
    if (!autofmt) autofmt = "indent -kr -l100 -nbbo -nut -sob";
    if (!autofmt[0]) autofmt = "cat";

    verbose = (getenv("VERBOSE") && strcmp(getenv("VERBOSE"), "1") == 0);

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
        cflags = heap_strf("%s %s -fPIC -ggdb -I./include -D_DEFAULT_SOURCE", cconfig, optimization);

    ldflags = "-Wl,-rpath '-Wl,$ORIGIN' -L/usr/local/lib -L.";

    ldlibs = "-lgc -lcord -lm -ltomo";
    if (getenv("LDLIBS"))
        ldlibs = heap_strf("%s %s", ldlibs, getenv("LDLIBS"));

    cc = getenv("CC");
    if (!cc) cc = "tcc";

    module_code_t module_code;
    int transpile_status = transpile(filename, true, &module_code);
    if (mode == MODE_TRANSPILE || transpile_status != 0)
        return transpile_status;

    array_t file_deps = get_file_dependencies(filename);
    CORD object_files_cord = CORD_EMPTY;
    for (int64_t i = 0; i < file_deps.length; i++) {
        const char *dep = *(char**)(file_deps.data + i*file_deps.stride);
        int compile_status = compile_object_file(dep, false, mode == MODE_COMPILE_SHARED_OBJ && streq(dep, resolve_path(filename, ".", ".")));
        if (compile_status != 0) return compile_status;
        object_files_cord = object_files_cord ? CORD_all(object_files_cord, " ", dep, ".o") : CORD_cat(dep, ".o");
    }

    if (mode == MODE_COMPILE_OBJ || mode == MODE_COMPILE_SHARED_OBJ)
        return 0;

    const char *object_files = CORD_to_const_char_star(object_files_cord);
    assert(object_files);
    int executable_status = compile_executable(filename, object_files, &module_code);
    if (mode == MODE_COMPILE_EXE || executable_status != 0)
        return executable_status;

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

static void build_file_dependency_graph(const char *filename, table_t *dependencies)
{
    size_t len = strlen(filename);
    const char *base_filename;
    if (strcmp(filename + (len-2), ".h") == 0 || strcmp(filename + (len-2), ".c") == 0 || strcmp(filename + (len-2), ".o") == 0)
        base_filename = heap_strn(filename, len-2);
    else if (strcmp(filename + (len-3), ".tm") == 0)
        base_filename = filename;
    else
        errx(1, "I don't know how to find object files in: %s", filename);

    if (Table$str_get(*dependencies, base_filename))
        return;

    array_t *deps = new(array_t);
    Array$insert(deps, &base_filename, 0, $ArrayInfo(&$Text));
    Table$str_set(dependencies, base_filename, deps);

    module_code_t _;
    transpile(base_filename, false, &_);

    const char *to_scan[] = {
        heap_strf("%s.h", base_filename),
        heap_strf("%s.c", base_filename),
    };
    char *file_dir = realpath(filename, NULL);
    dirname(file_dir);
    for (size_t s = 0; s < sizeof(to_scan)/sizeof(to_scan[0]); s++) {
        file_t *f = load_file(to_scan[s]);
        if (!f) errx(1, "Couldn't find file: %s", to_scan[s]);
        for (int64_t i = 0; i < f->num_lines; i++) {
            const char *line = f->text + f->line_offsets[i];
            const char *prefix = "#include \"";
            if (strncmp(line, prefix, strlen(prefix)) == 0) {
                char *import = heap_strf("%s/%.*s", file_dir, strcspn(line + strlen(prefix), "\""), line + strlen(prefix));
                if (strstr(import, ".tm.h") != import + strlen(import) - 5)
                    continue;
                import[strlen(import)-2] = '\0';
                char *tmp = realpath(import, NULL);
                if (!tmp) errx(1, "Couldn't find import: %s", import);
                const char *resolved_file = heap_str(tmp);
                free(tmp);
                Array$insert(deps, &resolved_file, 0, $ArrayInfo(&$Text));
                build_file_dependency_graph(resolved_file, dependencies);
            }
        }
    }
    free(file_dir);
}

array_t get_file_dependencies(const char *filename)
{
    const char *resolved = resolve_path(filename, ".", ".");

    table_t file_dependencies = {};
    build_file_dependency_graph(resolved, &file_dependencies);
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

static bool stale(const char *filename, const char *relative_to)
{
    struct stat target_stat;
    if (stat(filename, &target_stat))
        return true;
    struct stat relative_to_stat;
    if (stat(relative_to, &relative_to_stat))
        return true;
    return target_stat.st_mtime < relative_to_stat.st_mtime;
}

int transpile(const char *filename, bool force_retranspile, module_code_t *module_code)
{
    const char *tm_file = filename;
    const char *c_filename = heap_strf("%s.c", tm_file);
    const char *h_filename = heap_strf("%s.h", tm_file);
    if (!force_retranspile && !stale(c_filename, tm_file) && !stale(h_filename, tm_file)) {
        return 0;
    }

    file_t *f = load_file(filename);
    if (!f)
        errx(1, "No such file: %s", filename);

    ast_t *ast = parse_file(f, NULL);

    if (!ast)
        errx(1, "Could not compile %s", f);

    if (verbose) {
        FILE *out = popen(heap_strf("bat -P --file-name='%s'", filename), "w");
        fputs(f->text, out);
        pclose(out);
    }

    if (verbose) {
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

    if (verbose) {
        FILE *out = popen(heap_strf("bat -P %s %s", h_filename, c_filename), "w");
        pclose(out);
    }

    return 0;
}

int compile_object_file(const char *filename, bool force_recompile, bool shared)
{
    const char *obj_file = heap_strf("%s.o", filename);
    if (!force_recompile && !stale(obj_file, filename)
        && !stale(obj_file, heap_strf("%s.c", filename)) && !stale(obj_file, heap_strf("%s.h", filename))) {
        return 0;
    }
    const char *base = strrchr(filename, '/')+1;
    base = heap_strn(base, strlen(base) - strlen(".tm"));
    const char *outfile = shared ? heap_strf("lib%s.so", base) : heap_strf("%s.o", filename);
    const char *cmd = shared ?
        heap_strf("%s %s %s %s %s -Wl,-soname,lib%s.so -shared %s.c -o %s", cc, cflags, ldflags, ldlibs, objfiles, base, filename, outfile)
        : heap_strf("%s %s -c %s.c -o %s", cc, cflags, filename, outfile);
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

int compile_executable(const char *filename, const char *object_files, module_code_t *module_code)
{
    const char *bin_name = heap_strn(filename, strlen(filename) - strlen(".tm"));
    const char *run = heap_strf("%s | %s %s %s %s %s %s -x c - -o %s", autofmt, cc, cflags, ldflags, ldlibs, objfiles, object_files, bin_name);
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

    if (verbose) {
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
