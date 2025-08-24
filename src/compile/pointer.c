// Compilation logic
#include <gc.h>
#include <glob.h>
#include <gmp.h>
#include <uninorm.h>

#include "../ast.h"
#include "../compile.h"
#include "../config.h"
#include "../environment.h"
#include "../stdlib/text.h"
#include "../typecheck.h"
#include "list.h"

Text_t compile_to_pointer_depth(env_t *env, ast_t *ast, int64_t target_depth, bool needs_incref) {
    Text_t val = compile(env, ast);
    type_t *t = get_type(env, ast);
    int64_t depth = 0;
    for (type_t *tt = t; tt->tag == PointerType; tt = Match(tt, PointerType)->pointed)
        ++depth;

    // Passing a literal value won't trigger an incref, because it's ephemeral,
    // e.g. [10, 20].reversed()
    if (t->tag != PointerType && needs_incref && !can_be_mutated(env, ast)) needs_incref = false;

    while (depth != target_depth) {
        if (depth < target_depth) {
            if (ast->tag == Var && target_depth == 1) val = Texts("(&", val, ")");
            else code_err(ast, "This should be a pointer, not ", type_to_str(get_type(env, ast)));
            t = Type(PointerType, .pointed = t, .is_stack = true);
            ++depth;
        } else {
            DeclareMatch(ptr, t, PointerType);
            val = Texts("*(", val, ")");
            t = ptr->pointed;
            --depth;
        }
    }

    while (t->tag == PointerType) {
        DeclareMatch(ptr, t, PointerType);
        t = ptr->pointed;
    }

    if (needs_incref && t->tag == ListType) val = Texts("LIST_COPY(", val, ")");
    else if (needs_incref && (t->tag == TableType || t->tag == SetType)) val = Texts("TABLE_COPY(", val, ")");

    return val;
}
