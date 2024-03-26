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
#include "typecheck.h"
#include "types.h"

typedef enum { MODE_TRANSPILE = 0, MODE_COMPILE = 1, MODE_RUN = 2 } mode_e;

static bool verbose = false;
static const char *autofmt;
static const char *cconfig;
static const char *cflags;
static const char *ldlibs;
static const char *ldflags = "-Wl,-rpath '-Wl,$ORIGIN' -L/usr/local/lib";
static const char *cc;

static array_t get_file_dependencies(const char *filename);
static int transpile(const char *filename, bool force_retranspile);
static int compile_object_file(const char *filename, bool force_recompile);
static int run_program(const char *filename, const char *object_files);

int main(int argc, char *argv[])
{
    mode_e mode = MODE_RUN;
    const char *filename = NULL;
    for (int i = 1; i < argc; i++) {
        if (streq(argv[i], "-t")) {
            mode = MODE_TRANSPILE;
        } else if (streq(argv[i], "-c")) {
            mode = MODE_COMPILE;
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
            break;
        }
    }

    if (filename == NULL)
        errx(1, "No file provided");

    // register_printf_modifier(L"p");
    if (register_printf_specifier('T', printf_type, printf_pointer_size))
        errx(1, "Couldn't set printf specifier");
    if (register_printf_specifier('W', printf_ast, printf_pointer_size))
        errx(1, "Couldn't set printf specifier");

    autofmt = getenv("AUTOFMT");
    if (!autofmt) autofmt = "indent -kr -l100 -nbbo -nut -sob";
    if (!autofmt[0]) autofmt = "cat";

    verbose = (getenv("VERBOSE") && strcmp(getenv("VERBOSE"), "1") == 0);

    cconfig = getenv("CCONFIG");
    if (!cconfig)
        cconfig = "-std=c11 -fdollars-in-identifiers -fsanitize=signed-integer-overflow -fno-sanitize-recover -D_XOPEN_SOURCE=700 -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE";

    const char *optimization = getenv("O");
    if (!optimization || !optimization[0]) optimization = "-O1";
    else optimization = heap_strf("-O%s", optimization);

    cflags = getenv("CFLAGS");
    if (!cflags)
        cflags = heap_strf("%s %s -ggdb -I. -D_DEFAULT_SOURCE", cconfig, optimization);

    ldlibs = "-lgc -lcord -lm -L. -ltomo";
    if (getenv("LDLIBS"))
        ldlibs = heap_strf("%s %s", ldlibs, getenv("LDLIBS"));

    ldflags = "-Wl,-rpath '-Wl,$ORIGIN' -L/usr/local/lib";

    cc = getenv("CC");
    if (!cc) cc = "tcc";

    int transpile_status = transpile(filename, true);
    if (mode == MODE_TRANSPILE || transpile_status != 0)
        return transpile_status;

    array_t file_deps = get_file_dependencies(filename);
    CORD object_files_cord = CORD_EMPTY;
    for (int64_t i = 0; i < file_deps.length; i++) {
        const char *dep = *(char**)(file_deps.data + i*file_deps.stride);
        int compile_status = compile_object_file(dep, false);
        if (compile_status != 0) return compile_status;
        object_files_cord = object_files_cord ? CORD_all(object_files_cord, " ", dep, ".o") : CORD_cat(dep, ".o");
    }

    if (mode == MODE_RUN) {
        const char *object_files = CORD_to_const_char_star(object_files_cord);
        assert(object_files);
        return run_program(filename, object_files);
    }
    return 0;
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

    if (Table_str_get(*dependencies, base_filename))
        return;

    array_t *deps = new(array_t);
    Array__insert(deps, &base_filename, 0, $ArrayInfo(&Text));
    Table_str_set(dependencies, base_filename, deps);

    transpile(base_filename, false);

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
            const char *line = f->text + f->lines[i].offset;
            const char *prefix = "#include \"";
            if (strncmp(line, prefix, strlen(prefix)) == 0) {
                char *tmp = realpath(heap_strf("%s/%.*s", file_dir, strcspn(line + strlen(prefix), "\"") - 2, line + strlen(prefix)), NULL);
                const char *resolved_file = heap_str(tmp);
                free(tmp);
                Array__insert(deps, &resolved_file, 0, $ArrayInfo(&Text));
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
        .tag=TableInfo, .TableInfo.key=&Text, .TableInfo.value=&unit};

    for (int64_t i = 1; i <= Table_length(file_dependencies); i++) {
        struct { const char *name; array_t *deps; } *entry = Table_entry(file_dependencies, i);
        for (int64_t j = 0; j < entry->deps->length; j++) {
            const char *dep = *(char**)(entry->deps->data + j*entry->deps->stride);
            Table_set(&dependency_set, &dep, &dep, &info);
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

int transpile(const char *filename, bool force_retranspile)
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
        errx(1, "Could not compile!");

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

    module_code_t module = compile_file(ast);

    FILE *h_file = fopen(h_filename, "w");
    if (!h_file)
        errx(1, "Couldn't open file: %s", h_filename);
    CORD_put("#pragma once\n", h_file);
    CORD_put(module.header, h_file);
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
    CORD_put(CORD_all("#include \"", module.module_name, ".tm.h\"\n\n", module.c_file), c_file);
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

int compile_object_file(const char *filename, bool force_recompile)
{
    const char *obj_file = heap_strf("%s.o", filename);
    if (!force_recompile && !stale(obj_file, filename)
        && !stale(obj_file, heap_strf("%s.c", filename)) && !stale(obj_file, heap_strf("%s.h", filename))) {
        return 0;
    }
    const char *cmd = heap_strf("%s %s -c %s.c -o %s.o", cc, cflags, filename, filename);
    if (verbose)
        printf("Running: %s\n", cmd);
    FILE *prog = popen(cmd, "w");
    int status = pclose(prog);
    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        if (verbose)
            printf("Compiled to %s.o\n", filename);
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : EXIT_FAILURE;
}

int run_program(const char *filename, const char *object_files)
{
    const char *bin_name = file_base_name(filename);
    const char *run = streq(cc, "tcc") ? heap_strf("%s | tcc %s %s %s %s -run -", autofmt, cflags, ldflags, ldlibs, object_files)
        : heap_strf("%s | %s %s %s %s %s -x c - -o %s && ./%s", autofmt, cc, cflags, ldflags, ldlibs, object_files, bin_name, bin_name);
    if (verbose)
        printf("%s\n", run);
    FILE *runner = popen(run, "w");

    const char *module_name = file_base_name(filename);
    CORD program = CORD_all(
        "#include <tomo/tomo.h>\n"
        "#include \"", filename, ".h\"\n"
        "\n"
        "int main(int argc, const char *argv[]) {\n"
        "(void)argc;\n"
        "(void)argv;\n"
        "GC_INIT();\n"
        "detect_color();\n",
        module_name, "$use();\n"
        "return 0;\n"
        "}\n"
    );
    if (verbose) {
        FILE *out = popen(heap_strf("%s | bat -P --file-name=run.c", autofmt), "w");
        CORD_put(program, out);
        pclose(out);
    }

    CORD_put(program, runner);
    int status = pclose(runner);
    return WIFEXITED(status) ? WEXITSTATUS(status) : EXIT_FAILURE;
}

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
