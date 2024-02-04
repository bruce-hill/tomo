#include <stdio.h>
#include <stdlib.h>
#include <gc.h>
#include <gc/cord.h>

#include "ast.h"
#include "parse.h"
#include "compile.h"

int main(int argc, char *argv[])
{
    if (argc < 2) return 1;

    sss_file_t *f = sss_load_file(argv[1]);
    ast_t *ast = parse_file(f, NULL);
    const char *s = ast_to_str(ast);
    puts(s);
    CORD c = compile(ast);
    CORD_put(c, stdout);
    return 0;
}

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
