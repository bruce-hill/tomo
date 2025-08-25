// This code defines functions for transforming ASTs back into Tomo source text

#include "ast.h"
#include "stdlib/datatypes.h"
#include "stdlib/optionals.h"
#include "stdlib/text.h"

Text_t code_format(ast_t *ast, Table_t *comments) {
    (void)comments;
    switch (ast->tag) {
    default: return Text$from_strn(ast->start, (int64_t)(ast->end - ast->start));
    }
}

OptionalText_t code_format_inline(ast_t *ast, Table_t *comments) {
    (void)comments;
    for (const char *p = ast->start; p < ast->end; p++) {
        if (*p == '\n' || *p == '\r') return NONE_TEXT;
    }
    switch (ast->tag) {
    default: return Text$from_strn(ast->start, (int64_t)(ast->end - ast->start));
    }
}
