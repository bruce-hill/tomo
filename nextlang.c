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
    if (!autofmt) autofmt = "indent -kr -nut | bat --file-name=out.c";

    setenv("SSSPATH", ".", 0);

    sss_file_t *f = sss_load_file(argv[1]);

    ast_t *ast = parse_file(f, NULL);

    if (!ast)
        errx(1, "Could not compile!");

    if (getenv("VERBOSE")) {
        FILE *out = popen(heap_strf("bat --file-name='%s'", argv[1]), "w");
        fputs(f->text, out);
        fclose(out);
    }

    if (getenv("VERBOSE")) {
        FILE *out = popen("bat --file-name=AST", "w");
        fputs(ast_to_str(ast), out);
        fclose(out);
    }

    CORD code = "#include \"nextlang.h\"\n\n";

    // Predeclare types:
    for (ast_list_t *stmt = Match(ast, Block)->statements; stmt; stmt = stmt->next) {
        switch (stmt->ast->tag) {
        case StructDef: case EnumDef: {
            code = CORD_cat(code, compile(stmt->ast));
            break;
        }
        default: break;
        }
    }

    // Predeclare funcs:
    for (ast_list_t *stmt = Match(ast, Block)->statements; stmt; stmt = stmt->next) {
        switch (stmt->ast->tag) {
        case FunctionDef: {
            auto fndef = Match(stmt->ast, FunctionDef);
            CORD_sprintf(&code, "%rstatic %r %r(", code, fndef->ret_type ? compile_type(fndef->ret_type) : "void", compile(fndef->name));
            for (arg_ast_t *arg = fndef->args; arg; arg = arg->next) {
                CORD_sprintf(&code, "%r%r %s", code, compile_type(arg->type), arg->name);
                if (arg->next) code = CORD_cat(code, ", ");
            }
            code = CORD_cat(code, ");\n");
            break;
        }
        default: break;
        }
    }

    // Declare funcs:
    for (ast_list_t *stmt = Match(ast, Block)->statements; stmt; stmt = stmt->next) {
        switch (stmt->ast->tag) {
        case FunctionDef: {
            CORD_sprintf(&code, "%r\n\n%r", code, compile(stmt->ast));
            break;
        }
        default: break;
        }
    }
    
    // Main body:
    code = CORD_cat(code, "\n\nint main(int argc, const char *argv[]) {\n"
                    "(void)argc;\n"
                    "(void)argv;\n"
                    "GC_INIT();\n\n");
    for (ast_list_t *stmt = Match(ast, Block)->statements; stmt; stmt = stmt->next) {
        switch (stmt->ast->tag) {
        case FunctionDef: case StructDef: case EnumDef: break;
        default: {
            code = CORD_cat(code, compile(stmt->ast));
            code = CORD_cat(code, ";\n");
            break;
        }
        }
    }
    code = CORD_cat(code, "\nreturn 0;\n}\n");
    
    if (getenv("VERBOSE")) {
        FILE *out = popen(autofmt, "w");
        CORD_put(code, out);
        fclose(out);
    }

    const char *flags = getenv("CFLAGS");
    if (!flags) flags = "-std=c11 -lm -lgc -lcord";
    const char *run = heap_strf(getenv("VERBOSE") ? "tcc %s -run - | bat --file-name=output.txt" : "tcc %s -run -", flags);
    FILE *cc = popen(run, "w");
    CORD_put(code, cc);
    fclose(cc);

    return 0;
}

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
