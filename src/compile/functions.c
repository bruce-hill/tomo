#include "../ast.h"
#include "../compile.h"
#include "../environment.h"
#include "../stdlib/datatypes.h"
#include "../stdlib/integers.h"
#include "../stdlib/nums.h"
#include "../stdlib/tables.h"
#include "../stdlib/text.h"
#include "../stdlib/util.h"
#include "../typecheck.h"
#include "../types.h"
#include "integers.h"
#include "promotion.h"
#include "structs.h"
#include "text.h"

public
Text_t compile_arguments(env_t *env, ast_t *call_ast, arg_t *spec_args, arg_ast_t *call_args) {
    Table_t used_args = {};
    Text_t code = EMPTY_TEXT;
    env_t *default_scope = new (env_t);
    *default_scope = *env;
    default_scope->locals = new (Table_t, .fallback = env->namespace_bindings ? env->namespace_bindings : env->globals);
    for (arg_t *spec_arg = spec_args; spec_arg; spec_arg = spec_arg->next) {
        int64_t i = 1;
        // Find keyword:
        if (spec_arg->name) {
            for (arg_ast_t *call_arg = call_args; call_arg; call_arg = call_arg->next) {
                if (call_arg->name && streq(call_arg->name, spec_arg->name)) {
                    Text_t value;
                    if (spec_arg->type->tag == IntType && call_arg->value->tag == Int) {
                        value = compile_int_to_type(env, call_arg->value, spec_arg->type);
                    } else if (spec_arg->type->tag == NumType && call_arg->value->tag == Int) {
                        OptionalInt_t int_val = Int$from_str(Match(call_arg->value, Int)->str);
                        if (int_val.small == 0) code_err(call_arg->value, "Failed to parse this integer");
                        if (Match(spec_arg->type, NumType)->bits == TYPE_NBITS64)
                            value = Text$from_str(String(hex_double(Num$from_int(int_val, false))));
                        else value = Text$from_str(String(hex_double((double)Num32$from_int(int_val, false)), "f"));
                    } else {
                        env_t *arg_env = with_enum_scope(env, spec_arg->type);
                        value = compile_maybe_incref(arg_env, call_arg->value, spec_arg->type);
                    }
                    Table$str_set(&used_args, call_arg->name, call_arg);
                    if (code.length > 0) code = Texts(code, ", ");
                    code = Texts(code, value);
                    goto found_it;
                }
            }
        }
        // Find positional:
        for (arg_ast_t *call_arg = call_args; call_arg; call_arg = call_arg->next) {
            if (call_arg->name) continue;
            const char *pseudoname = String(i++);
            if (!Table$str_get(used_args, pseudoname)) {
                Text_t value;
                if (spec_arg->type->tag == IntType && call_arg->value->tag == Int) {
                    value = compile_int_to_type(env, call_arg->value, spec_arg->type);
                } else if (spec_arg->type->tag == NumType && call_arg->value->tag == Int) {
                    OptionalInt_t int_val = Int$from_str(Match(call_arg->value, Int)->str);
                    if (int_val.small == 0) code_err(call_arg->value, "Failed to parse this integer");
                    if (Match(spec_arg->type, NumType)->bits == TYPE_NBITS64)
                        value = Text$from_str(String(hex_double(Num$from_int(int_val, false))));
                    else value = Text$from_str(String(hex_double((double)Num32$from_int(int_val, false)), "f"));
                } else {
                    env_t *arg_env = with_enum_scope(env, spec_arg->type);
                    value = compile_maybe_incref(arg_env, call_arg->value, spec_arg->type);
                }

                Table$str_set(&used_args, pseudoname, call_arg);
                if (code.length > 0) code = Texts(code, ", ");
                code = Texts(code, value);
                goto found_it;
            }
        }

        if (spec_arg->default_val) {
            if (code.length > 0) code = Texts(code, ", ");
            code = Texts(code, compile_maybe_incref(default_scope, spec_arg->default_val, get_arg_type(env, spec_arg)));
            goto found_it;
        }

        assert(spec_arg->name);
        code_err(call_ast, "The required argument '", spec_arg->name, "' was not provided");
    found_it:
        continue;
    }

    int64_t i = 1;
    for (arg_ast_t *call_arg = call_args; call_arg; call_arg = call_arg->next) {
        if (call_arg->name) {
            if (!Table$str_get(used_args, call_arg->name))
                code_err(call_arg->value, "There is no argument with the name '", call_arg->name, "'");
        } else {
            const char *pseudoname = String(i++);
            if (!Table$str_get(used_args, pseudoname)) code_err(call_arg->value, "This is one argument too many!");
        }
    }
    return code;
}

public
Text_t compile_function_call(env_t *env, ast_t *ast) {
    DeclareMatch(call, ast, FunctionCall);
    type_t *fn_t = get_type(env, call->fn);
    if (fn_t->tag == FunctionType) {
        Text_t fn = compile(env, call->fn);
        if (!is_valid_call(env, Match(fn_t, FunctionType)->args, call->args, (call_opts_t){.promotion = true})) {
            if (is_valid_call(env, Match(fn_t, FunctionType)->args, call->args,
                              (call_opts_t){.promotion = true, .underscores = true})) {
                code_err(ast, "You can't pass underscore arguments to this function (those are private)");
            } else {
                arg_t *args = NULL;
                for (arg_ast_t *a = call->args; a; a = a->next)
                    args = new (arg_t, .name = a->name, .type = get_type(env, a->value), .next = args);
                REVERSE_LIST(args);
                code_err(ast,
                         "This function's public signature doesn't match this call site.\n"
                         "The signature is: ",
                         type_to_text(fn_t),
                         "\n"
                         "But it's being called with: ",
                         type_to_text(Type(FunctionType, .args = args)));
            }
        }
        return Texts(fn, "(", compile_arguments(env, ast, Match(fn_t, FunctionType)->args, call->args), ")");
    } else if (fn_t->tag == TypeInfoType) {
        type_t *t = Match(fn_t, TypeInfoType)->type;

        // Literal constructors for numeric types like `Byte(123)` should
        // not go through any conversion, just a cast:
        if (is_numeric_type(t) && call->args && !call->args->next && call->args->value->tag == Int)
            return compile_to_type(env, call->args->value, t);
        else if (t->tag == NumType && call->args && !call->args->next && call->args->value->tag == Num)
            return compile_to_type(env, call->args->value, t);

        binding_t *constructor =
            get_constructor(env, t, call->args, env->current_type != NULL && type_eq(env->current_type, t));
        if (constructor) {
            arg_t *arg_spec = Match(constructor->type, FunctionType)->args;
            return Texts(constructor->code, "(", compile_arguments(env, ast, arg_spec, call->args), ")");
        }

        type_t *actual = call->args ? get_type(env, call->args->value) : NULL;
        if (t->tag == TextType) {
            if (!call->args) code_err(ast, "This constructor needs a value");
            if (!type_eq(t, TEXT_TYPE))
                code_err(call->fn, "I don't have a constructor defined for "
                                   "these arguments");
            // Text constructor:
            if (!call->args || call->args->next) code_err(call->fn, "This constructor takes exactly 1 argument");
            if (type_eq(actual, t)) return compile(env, call->args->value);
            return expr_as_text(compile(env, call->args->value), actual, Text("no"));
        } else if (t->tag == CStringType) {
            // C String constructor:
            if (!call->args || call->args->next) code_err(call->fn, "This constructor takes exactly 1 argument");
            if (call->args->value->tag == TextLiteral)
                return compile_text_literal(Match(call->args->value, TextLiteral)->text);
            else if (call->args->value->tag == TextJoin && Match(call->args->value, TextJoin)->children == NULL)
                return Text("\"\"");
            else if (call->args->value->tag == TextJoin && Match(call->args->value, TextJoin)->children->next == NULL)
                return compile_text_literal(
                    Match(Match(call->args->value, TextJoin)->children->ast, TextLiteral)->text);
            return Texts("Text$as_c_string(", expr_as_text(compile(env, call->args->value), actual, Text("no")), ")");
        } else if (t->tag == StructType) {
            return compile_struct_literal(env, ast, t, call->args);
        }
        code_err(ast,
                 "I could not find a constructor matching these arguments "
                 "for ",
                 type_to_str(t));
    } else if (fn_t->tag == ClosureType) {
        fn_t = Match(fn_t, ClosureType)->fn;
        arg_t *type_args = Match(fn_t, FunctionType)->args;

        arg_t *closure_fn_args = NULL;
        for (arg_t *arg = Match(fn_t, FunctionType)->args; arg; arg = arg->next)
            closure_fn_args = new (arg_t, .name = arg->name, .type = arg->type, .default_val = arg->default_val,
                                   .next = closure_fn_args);
        closure_fn_args = new (arg_t, .name = "userdata", .type = Type(PointerType, .pointed = Type(MemoryType)),
                               .next = closure_fn_args);
        REVERSE_LIST(closure_fn_args);
        Text_t fn_type_code =
            compile_type(Type(FunctionType, .args = closure_fn_args, .ret = Match(fn_t, FunctionType)->ret));

        Text_t closure = compile(env, call->fn);
        Text_t arg_code = compile_arguments(env, ast, type_args, call->args);
        if (arg_code.length > 0) arg_code = Texts(arg_code, ", ");
        if (call->fn->tag == Var) {
            return Texts("((", fn_type_code, ")", closure, ".fn)(", arg_code, closure, ".userdata)");
        } else {
            return Texts("({ Closure_t closure = ", closure, "; ((", fn_type_code, ")closure.fn)(", arg_code,
                         "closure.userdata); })");
        }
    } else {
        code_err(call->fn, "This is not a function, it's a ", type_to_str(fn_t));
    }
}
