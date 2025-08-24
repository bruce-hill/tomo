// This file defines how to compile pointers and allocated memory

#include <gc.h>
#include <glob.h>
#include <gmp.h>
#include <uninorm.h>

#include "../ast.h"
#include "expressions.h"
#include "../config.h"
#include "../environment.h"
#include "../stdlib/text.h"
#include "../typecheck.h"
#include "assignments.h"
#include "promotions.h"

public
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

public
Text_t compile_typed_allocation(env_t *env, ast_t *ast, type_t *pointer_type) {
    // TODO: for constructors, do new(T, ...) instead of heap((T){...})
    type_t *pointed = Match(pointer_type, PointerType)->pointed;
    switch (ast->tag) {
    case HeapAllocate: {
        return Texts("heap(", compile_to_type(env, Match(ast, HeapAllocate)->value, pointed), ")");
    }
    case StackReference: {
        ast_t *subject = Match(ast, StackReference)->value;
        if (can_be_mutated(env, subject) && type_eq(pointed, get_type(env, subject)))
            return Texts("(&", compile_lvalue(env, subject), ")");
        else return Texts("stack(", compile_to_type(env, subject, pointed), ")");
    }
    default: code_err(ast, "Not an allocation!");
    }
    return EMPTY_TEXT;
}
