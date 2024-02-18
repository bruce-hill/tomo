#include <stdio.h>
#include <stdlib.h>
#include <gc.h>
#include <gc/cord.h>
#include <printf.h>

#include "ast.h"
#include "parse.h"
#include "compile.h"
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

    env_t *env = new_compilation_unit();

    CORD_appendf(&env->code->imports, "#include \"nextlang.h\"\n");

    // Main body:
    for (ast_list_t *stmt = Match(ast, Block)->statements; stmt; stmt = stmt->next) {
        CORD code = compile_statement(env, stmt->ast);
        if (code)
            CORD_appendf(&env->code->main, "%r\n", code);
        bind_statement(env, stmt->ast);
    }

    CORD fileinfo = CORD_asprintf("#line 0 \"%s\"\n", f->filename);
    CORD program = CORD_all(
        fileinfo,
        "// Generated code:\n",
        env->code->imports, "\n",
        env->code->typedefs, "\n",
        env->code->typecode, "\n",
        env->code->staticdefs, "\n",
        env->code->funcs, "\n"
        "\n"
        "static void $load(void) {\n",
        env->code->main,
        "}\n"
        "\n"
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
        cflags = "-std=c11 -fdollars-in-identifiers -fsanitize=signed-integer-overflow -fno-sanitize-recover";

    const char *ldlibs = "-lgc -lcord -lm -L. -lnext";
    if (getenv("LDLIBS"))
        ldlibs = heap_strf("%s %s", ldlibs, getenv("LDLIBS"));

    const char *ldflags = "-Wl,-rpath '-Wl,$ORIGIN'";

    // const char *run = heap_strf("tcc -run %s %s %s -", cflags, ldflags, ldlibs);
    const char *run = heap_strf("gcc -x c %s %s %s - -o program && ./program", cflags, ldflags, ldlibs);
    FILE *cc = popen(run, "w");
    CORD_put(program, cc);
    fclose(cc);

    return 0;
}

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
