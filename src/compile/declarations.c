// This file defines how to compile variable declarations
#include "../ast.h"
#include "../compile.h"
#include "../environment.h"
#include "../stdlib/datatypes.h"
#include "../stdlib/text.h"
#include "../stdlib/util.h"
#include "../typecheck.h"
#include "promotions.h"
#include "types.h"

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

public
Text_t compile_declared_value(env_t *env, ast_t *declare_ast) {
    DeclareMatch(decl, declare_ast, Declare);
    type_t *t = decl->type ? parse_type_ast(env, decl->type) : get_type(env, decl->value);

    if (t->tag == AbortType || t->tag == VoidType || t->tag == ReturnType)
        code_err(declare_ast, "You can't declare a variable with a ", type_to_str(t), " value");

    if (decl->value) {
        Text_t val_code = compile_maybe_incref(env, decl->value, t);
        if (t->tag == FunctionType) {
            assert(promote(env, decl->value, &val_code, t, Type(ClosureType, t)));
            t = Type(ClosureType, t);
        }
        return val_code;
    } else {
        Text_t val_code = compile_empty(t);
        if (val_code.length == 0)
            code_err(declare_ast, "This type (", type_to_str(t),
                     ") cannot be uninitialized. You must provide a value.");
        return val_code;
    }
}
