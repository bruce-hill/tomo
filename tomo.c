#include <stdio.h>
#include <stdlib.h>
#include <gc.h>
#include <gc/cord.h>
#include <printf.h>

#include "ast.h"
#include "builtins/text.h"
#include "compile.h"
#include "parse.h"
#include "typecheck.h"
#include "types.h"

typedef enum { MODE_RUN, MODE_TRANSPILE, MODE_EXPANDED_TRANSPILE } mode_e;

int main(int argc, char *argv[])
{
    mode_e mode = MODE_RUN;
    const char *filename = NULL;
    for (int i = 1; i < argc; i++) {
        if (streq(argv[i], "-t")) {
            mode = MODE_TRANSPILE;
        } else if (streq(argv[i], "-E")) {
            mode = MODE_EXPANDED_TRANSPILE;
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

    const char *autofmt = getenv("AUTOFMT");
    if (!autofmt) autofmt = "indent -kr -l100 -nbbo -nut -sob";

    file_t *f = load_file(filename);
    if (!f)
        errx(1, "No such file: %s", filename);

    ast_t *ast = parse_file(f, NULL);

    if (!ast)
        errx(1, "Could not compile!");

    bool verbose = (getenv("VERBOSE") && strcmp(getenv("VERBOSE"), "1") == 0);
    if (verbose) {
        FILE *out = popen(heap_strf("bat -P --file-name='%s'", filename), "w");
        fputs(f->text, out);
        pclose(out);
    }

    if (verbose) {
        FILE *out = popen("bat -P --file-name=AST", "w");
        fputs(ast_to_str(ast), out);
        pclose(out);
    }

    module_code_t module = compile_file(ast);
    
    if (verbose) {
        FILE *out = popen(heap_strf("%s | bat -P --file-name=%s.h", autofmt, f->filename), "w");
        CORD_put(module.header, out);
        pclose(out);
        out = popen(heap_strf("%s | bat -P --file-name=%s.c", autofmt, f->filename), "w");
        CORD_put(CORD_all("#include \"", f->filename, "\"\n\n", module.c_file), out);
        pclose(out);
    }

    const char *cconfig = getenv("CCONFIG");
    if (!cconfig)
        cconfig = "-std=c11 -fdollars-in-identifiers -fsanitize=signed-integer-overflow -fno-sanitize-recover -D_XOPEN_SOURCE=700 -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE";

    const char *cflags = getenv("CFLAGS");
    if (!cflags)
        cflags = heap_strf("%s -I. -D_DEFAULT_SOURCE", cconfig);

    const char *ldlibs = "-lgc -lcord -lm -L. -ltomo";
    if (getenv("LDLIBS"))
        ldlibs = heap_strf("%s %s", ldlibs, getenv("LDLIBS"));

    const char *ldflags = "-Wl,-rpath '-Wl,$ORIGIN'";

    const char *cc = getenv("CC");
    if (!cc) cc = "tcc";

    switch (mode) {
    case MODE_RUN: {
        const char *run = streq(cc, "tcc") ? heap_strf("tcc -run %s %s %s -", cflags, ldflags, ldlibs)
            : heap_strf("gcc -x c %s %s %s - -o program && ./program", cflags, ldflags, ldlibs);
        FILE *runner = popen(run, "w");

        CORD program = CORD_all(
            "// File: ", f->filename, ".h\n",
            module.header,
            "\n",
            "// File: ", f->filename, ".c\n",
            module.c_file,
            "\n",
            "int main(int argc, const char *argv[]) {\n"
            "(void)argc;\n"
            "(void)argv;\n"
            "GC_INIT();\n"
            "detect_color();\n"
            "use$", module.module_name, "();\n"
            "return 0;\n"
            "}\n"
        );

        CORD_put(program, runner);
        int status = pclose(runner);
        return WIFEXITED(status) ? WEXITSTATUS(status) : EXIT_FAILURE;
    }
    case MODE_TRANSPILE: {
        FILE *prog = popen(heap_strf("%s > %s.h", autofmt, f->filename), "w");
        CORD_put("#pragma once\n", prog);
        CORD_put(module.header, prog);
        int status = pclose(prog);
        if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
            printf("Transpiled to %s.h\n", f->filename);
        else
            return WIFEXITED(status) ? WEXITSTATUS(status) : EXIT_FAILURE;

        prog = popen(heap_strf("%s > %s.c", autofmt, f->filename), "w");
        CORD_put(CORD_all("#include \"", module.module_name, ".tm.h\"\n\n", module.c_file), prog);
        status = pclose(prog);
        if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
            printf("Transpiled to %s.c\n", f->filename);
        return WIFEXITED(status) ? WEXITSTATUS(status) : EXIT_FAILURE;
    }
    case MODE_EXPANDED_TRANSPILE: {
        FILE *prog = popen(heap_strf("%s -x c %s -E - | %s > %s.h", cc, cflags, autofmt, f->filename), "w");
        CORD_put("#pragma once\n", prog);
        CORD_put(module.header, prog);
        int status = pclose(prog);
        if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
            printf("Transpiled to %s.h\n", f->filename);
        else
            return WIFEXITED(status) ? WEXITSTATUS(status) : EXIT_FAILURE;

        prog = popen(heap_strf("%s -x c %s -E - | %s > %s.c", cc, cflags, autofmt, f->filename), "w");
        CORD_put(CORD_all("#include \"", module.module_name, ".tm.h\"\n\n", module.c_file), prog);
        status = pclose(prog);
        if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
            printf("Transpiled to %s.c\n", f->filename);
        return WIFEXITED(status) ? WEXITSTATUS(status) : EXIT_FAILURE;
    }
    }
    return 0;
}

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
