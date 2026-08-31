// This file defines how to compile variable declarations

#include "../ast.h"
#include "../environment.h"
#include "../stdlib/datatypes.h"
#include "../stdlib/text.h"
#include "../typecheck.h"
#include "../util.h"
#include "compilation.h"

public
Text_t compile_declaration(type_t *t, Text_t name) {
    if (t->tag == FunctionType) {
        DeclareMatch(fn, t, FunctionType);
        Text_t code = Texts(compile_type(fn->ret), " (*", name, ")(");
        for (arg_t *arg = fn->args; arg; arg = arg->next) {
            code = Texts(code, compile_type(arg->type));
            if (arg->next) code = Texts(code, ", ");
        }
        if (!fn->args) code = Texts(code, "void");
        return Texts(code, ")");
    } else if (t->tag != ModuleType) {
        return Texts(compile_type(t), " ", name);
    } else {
        return EMPTY_TEXT;
    }
}

// A declared value has an incomplete type: some part of it is an empty list or
// table literal whose element type can't be inferred. Incomplete types aren't
// necessarily containers, and the culprit may be nested (`@[]`, `{"hi": []}`),
// so drill into the AST to point at the specific literal, falling back to a
// general message at `ast` when we can't attribute it to one.
static _Noreturn void report_incomplete_type(env_t *env, ast_t *ast) {
    switch (ast->tag) {
    case List: {
        DeclareMatch(list, ast, List);
        if (list->items == NULL)
            code_err(ast, "I can't tell what type of items this empty list holds. "
                          "Please give it a type annotation, like: `[Int]`");
        for (ast_list_t *item = list->items; item; item = item->next) {
            if (item->ast->tag == Comprehension) continue; // comprehensions have a concrete element type
            if (is_incomplete_type(get_type(env, item->ast))) report_incomplete_type(env, item->ast);
        }
        break;
    }
    case Table: {
        DeclareMatch(table, ast, Table);
        if (table->entries == NULL)
            code_err(ast, "I can't tell what types of keys and values this empty table holds. "
                          "Please give it a type annotation, like: `{Text:Int}`");
        for (ast_list_t *e = table->entries; e; e = e->next) {
            if (e->ast->tag != TableEntry) continue;
            DeclareMatch(entry, e->ast, TableEntry);
            if (entry->key && is_incomplete_type(get_type(env, entry->key))) report_incomplete_type(env, entry->key);
            if (entry->value && is_incomplete_type(get_type(env, entry->value)))
                report_incomplete_type(env, entry->value);
        }
        break;
    }
    case HeapAllocate: report_incomplete_type(env, Match(ast, HeapAllocate)->value); break;
    case StackReference: report_incomplete_type(env, Match(ast, StackReference)->value); break;
    default: break;
    }
    code_err(ast, "I can't determine the type of this value. Please give it a type annotation.");
}

public
Text_t compile_declared_value(env_t *env, ast_t *declare_ast) {
    DeclareMatch(decl, declare_ast, Declare);
    type_t *t = decl->type ? parse_type_ast(env, decl->type) : get_type(env, decl->value);

    if (t->tag == AbortType || t->tag == VoidType || t->tag == ReturnType)
        code_err(declare_ast, "You can't declare a variable with a ", type_to_text(t), " value");

    if (decl->value && is_incomplete_type(t)) report_incomplete_type(env, decl->value);

    if (decl->value) {
        Text_t val_code = compile_maybe_incref(env, decl->value, t);
        if (t->tag == FunctionType) assert(promote(env, decl->value, &val_code, t, Type(ClosureType, t)));
        return val_code;
    } else {
        Text_t val_code = compile_empty(t);
        if (val_code.length == 0)
            code_err(declare_ast, "This type (", type_to_text(t),
                     ") cannot be uninitialized. You must provide a value.");
        return val_code;
    }
}
