#include <stdio.h>
#include <stdlib.h>
#include <gc.h>
#include <gc/cord.h>
#include <printf.h>

#include "ast.h"
#include "builtins/string.h"
#include "compile.h"
#include "parse.h"
#include "typecheck.h"
#include "types.h"

int main(int argc, char *argv[])
{
    if (argc < 2) return 1;

    // register_printf_modifier(L"p");
    if (register_printf_specifier('T', printf_type, printf_pointer_size))
        errx(1, "Couldn't set printf specifier");
    if (register_printf_specifier('W', printf_ast, printf_pointer_size))
        errx(1, "Couldn't set printf specifier");

    const char *autofmt = getenv("AUTOFMT");
    if (!autofmt) autofmt = "indent -kr -l100 -nbbo -nut -sob";

    file_t *f = load_file(argv[1]);

    ast_t *ast = parse_file(f, NULL);

    if (!ast)
        errx(1, "Could not compile!");

    bool verbose = (getenv("VERBOSE") && strcmp(getenv("VERBOSE"), "1") == 0);
    if (verbose) {
        FILE *out = popen(heap_strf("bat -P --file-name='%s'", argv[1]), "w");
        fputs(f->text, out);
        fclose(out);
    }

    if (verbose) {
        FILE *out = popen("bat -P --file-name=AST", "w");
        fputs(ast_to_str(ast), out);
        fclose(out);
    }

    module_code_t module = compile_file(ast);

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
        "$load();\n"
        "return 0;\n"
        "}\n"
    );
    
    if (verbose) {
        FILE *out = popen(heap_strf("%s | bat -P --file-name=program.c", autofmt), "w");
        CORD_put(program, out);
        fclose(out);
    }

    const char *cflags = getenv("CFLAGS");
    if (!cflags)
        cflags = "-std=c11 -fdollars-in-identifiers -fsanitize=signed-integer-overflow -fno-sanitize-recover -D_XOPEN_SOURCE=700 -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE";

    const char *ldlibs = "-lgc -lcord -lm -L. -ltomo";
    if (getenv("LDLIBS"))
        ldlibs = heap_strf("%s %s", ldlibs, getenv("LDLIBS"));

    const char *ldflags = "-Wl,-rpath '-Wl,$ORIGIN'";

    const char *cc = getenv("CC");
    if (!cc) cc = "tcc";
    const char *run = streq(cc, "tcc") ? heap_strf("tcc -run %s %s %s -", cflags, ldflags, ldlibs)
        : heap_strf("gcc -x c %s %s %s - -o program && ./program", cflags, ldflags, ldlibs);
    FILE *runner = popen(run, "w");
    CORD_put(program, runner);
    int status = pclose(runner);
    return WIFEXITED(status) ? WEXITSTATUS(status) : EXIT_FAILURE;
}

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
