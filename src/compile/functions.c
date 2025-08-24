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
