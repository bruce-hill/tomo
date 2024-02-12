#include <stdio.h>
#include <stdlib.h>
#include <gc.h>
#include <gc/cord.h>
#include <printf.h>

#include "ast.h"
#include "parse.h"
#include "compile.h"
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
    if (!autofmt) autofmt = "indent -kr -nut";

    file_t *f = load_file(argv[1]);

    ast_t *ast = parse_file(f, NULL);

    if (!ast)
        errx(1, "Could not compile!");

    bool verbose = (getenv("VERBOSE") && strcmp(getenv("VERBOSE"), "1") == 0);
    if (verbose) {
        FILE *out = popen(heap_strf("bat --file-name='%s'", argv[1]), "w");
        fputs(f->text, out);
        fclose(out);
    }

    if (verbose) {
        FILE *out = popen("bat --file-name=AST", "w");
        fputs(ast_to_str(ast), out);
        fclose(out);
    }

    CORD header = "#include \"nextlang.h\"\n";

    // Predeclare types:
    for (ast_list_t *stmt = Match(ast, Block)->statements; stmt; stmt = stmt->next) {
        switch (stmt->ast->tag) {
        case StructDef: case EnumDef: {
            CORD_sprintf(&header, "%r\n%r", header, compile(stmt->ast));
            break;
        }
        default: break;
        }
    }

    CORD program = CORD_cat(header, "\n/////////////////////////////////////////////////////////////////////////\n\n"
                            "bool USE_COLOR = true;\n");

    // Predeclare funcs:
    for (ast_list_t *stmt = Match(ast, Block)->statements; stmt; stmt = stmt->next) {
        switch (stmt->ast->tag) {
        case FunctionDef: {
            auto fndef = Match(stmt->ast, FunctionDef);
            CORD_sprintf(&program, "%rstatic %r %r(", program, fndef->ret_type ? compile_type(fndef->ret_type) : "void", compile(fndef->name));
            for (arg_ast_t *arg = fndef->args; arg; arg = arg->next) {
                CORD_sprintf(&program, "%r%r %s", program, compile_type(arg->type), arg->name);
                if (arg->next) program = CORD_cat(program, ", ");
            }
            program = CORD_cat(program, ");\n");
            break;
        }
        default: break;
        }
    }

    // Declare funcs:
    for (ast_list_t *stmt = Match(ast, Block)->statements; stmt; stmt = stmt->next) {
        switch (stmt->ast->tag) {
        case FunctionDef: {
            CORD_sprintf(&program, "%r\n\n%r", program, compile(stmt->ast));
            break;
        }
        default: break;
        }
    }
    
    // Main body:
    program = CORD_cat(program, "\n\n"
                    "int main(int argc, const char *argv[]) {\n"
                    "(void)argc;\n"
                    "(void)argv;\n"
                    "GC_INIT();\n"
                    "USE_COLOR = getenv(\"COLOR\") ? strcmp(getenv(\"COLOR\"), \"1\") == 0 : isatty(STDOUT_FILENO);\n"
                    "// User code:\n");
    for (ast_list_t *stmt = Match(ast, Block)->statements; stmt; stmt = stmt->next) {
        switch (stmt->ast->tag) {
        case FunctionDef: case StructDef: case EnumDef: break;
        default: {
            program = CORD_cat(program, compile_statement(stmt->ast));
            program = CORD_cat(program, "\n");
            break;
        }
        }
    }
    program = CORD_cat(program, "return 0;\n}\n");
    
    if (verbose) {
        FILE *out = popen(heap_strf("%s | bat --file-name=program.c", autofmt), "w");
        CORD_put(program, out);
        fclose(out);
    }

    const char *cflags = getenv("CFLAGS");
    if (!cflags)
        cflags = "-std=c11";

    const char *ldlibs = "-lgc -lcord -lm -L. -lnext";
    if (getenv("LDLIBS"))
        ldlibs = heap_strf("%s %s", ldlibs, getenv("LDLIBS"));

    const char *run = heap_strf("tcc %s %s -run -", cflags, ldlibs);
    if (verbose)
        run = heap_strf("%s | bat --file-name=STDOUT", run);
    FILE *cc = popen(run, "w");
    CORD_put(program, cc);
    fclose(cc);

    return 0;
}

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
