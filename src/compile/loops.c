// This file defines how to compile loops

#include <gmp.h>

#include "../ast.h"
#include "../config.h"
#include "../environment.h"
#include "../stdlib/datatypes.h"
#include "../stdlib/integers.h"
#include "../stdlib/text.h"
#include "../stdlib/util.h"
#include "../typecheck.h"
#include "compilation.h"

public
Text_t compile_for_loop(env_t *env, ast_t *ast) {
    DeclareMatch(for_, ast, For);

    // If we're iterating over a comprehension, that's actually just doing
    // one loop, we don't need to compile the comprehension as a list
    // comprehension. This is a common case for reducers like `(+: i*2 for i
    // in 5)` or `(and) x.is_good() for x in xs`
    if (for_->iter->tag == Comprehension) {
        DeclareMatch(comp, for_->iter, Comprehension);
        ast_t *body = for_->body;
        if (for_->vars) {
            if (for_->vars->next) code_err(for_->vars->next->ast, "This is too many variables for iteration");

            body = WrapAST(
                ast, Block,
                .statements =
                    new (ast_list_t, .ast = WrapAST(ast, Declare, .var = for_->vars->ast, .value = comp->expr),
                         .next = body->tag == Block ? Match(body, Block)->statements : new (ast_list_t, .ast = body)));
        }

        if (comp->filter) body = WrapAST(for_->body, If, .condition = comp->filter, .body = body);
        ast_t *loop = WrapAST(ast, For, .vars = comp->vars, .iter = comp->iter, .body = body);
        return compile_statement(env, loop);
    }

    env_t *body_scope = for_scope(env, ast);
    loop_ctx_t loop_ctx = (loop_ctx_t){
        .loop_name = "for",
        .loop_vars = for_->vars,
        .deferred = body_scope->deferred,
        .next = body_scope->loop_ctx,
    };
    body_scope->loop_ctx = &loop_ctx;
    // Naked means no enclosing braces:
    Text_t naked_body = compile_inline_block(body_scope, for_->body);
    if (loop_ctx.skip_label.length > 0) naked_body = Texts(naked_body, "\n", loop_ctx.skip_label, ": continue;");
    Text_t stop = loop_ctx.stop_label.length > 0 ? Texts("\n", loop_ctx.stop_label, ":;") : EMPTY_TEXT;

    // Special case for improving performance for numeric iteration:
    if (for_->iter->tag == MethodCall && streq(Match(for_->iter, MethodCall)->name, "to")
        && is_int_type(get_type(env, Match(for_->iter, MethodCall)->self))) {
        // TODO: support other integer types
        arg_ast_t *args = Match(for_->iter, MethodCall)->args;
        if (!args) code_err(for_->iter, "to() needs at least one argument");

        type_t *int_type = get_type(env, Match(for_->iter, MethodCall)->self);
        type_t *step_type = int_type->tag == ByteType ? Type(IntType, .bits = TYPE_IBITS8) : int_type;

        Text_t last = EMPTY_TEXT, step = EMPTY_TEXT, optional_step = EMPTY_TEXT;
        if (!args->name || streq(args->name, "last")) {
            last = compile_to_type(env, args->value, int_type);
            if (args->next) {
                if (args->next->name && !streq(args->next->name, "step"))
                    code_err(args->next->value, "Invalid argument name: ", args->next->name);
                if (get_type(env, args->next->value)->tag == OptionalType)
                    optional_step = compile_to_type(env, args->next->value, Type(OptionalType, step_type));
                else step = compile_to_type(env, args->next->value, step_type);
            }
        } else if (streq(args->name, "step")) {
            if (get_type(env, args->value)->tag == OptionalType)
                optional_step = compile_to_type(env, args->value, Type(OptionalType, step_type));
            else step = compile_to_type(env, args->value, step_type);
            if (args->next) {
                if (args->next->name && !streq(args->next->name, "last"))
                    code_err(args->next->value, "Invalid argument name: ", args->next->name);
                last = compile_to_type(env, args->next->value, int_type);
            }
        }

        if (last.length == 0) code_err(for_->iter, "No `last` argument was given");

        Text_t type_code = compile_type(int_type);
        Text_t value = for_->vars ? compile(body_scope, for_->vars->ast) : Text("i");
        if (int_type->tag == BigIntType) {
            if (optional_step.length > 0)
                step = Texts("({ OptionalInt_t maybe_step = ", optional_step,
                             "; maybe_step->small == 0 ? "
                             "(Int$compare_value(last, first) >= 0 "
                             "? I_small(1) : I_small(-1)) : (Int_t)maybe_step; "
                             "})");
            else if (step.length == 0)
                step = Text("Int$compare_value(last, first) >= 0 ? "
                            "I_small(1) : I_small(-1)");
            return Texts("for (", type_code, " first = ", compile(env, Match(for_->iter, MethodCall)->self), ", ",
                         value, " = first, last = ", last, ", step = ", step,
                         "; "
                         "Int$compare_value(",
                         value, ", last) != Int$compare_value(step, I_small(0)); ", value, " = Int$plus(", value,
                         ", step)) {\n"
                         "\t",
                         naked_body, "}", stop);
        } else {
            if (optional_step.length > 0)
                step = Texts("({ ", compile_type(Type(OptionalType, step_type)), " maybe_step = ", optional_step,
                             "; "
                             "maybe_step.is_none ? (",
                             type_code, ")(last >= first ? 1 : -1) : maybe_step.value; })");
            else if (step.length == 0) step = Texts("(", type_code, ")(last >= first ? 1 : -1)");
            return Texts("for (", type_code, " first = ", compile(env, Match(for_->iter, MethodCall)->self), ", ",
                         value, " = first, last = ", last, ", step = ", step,
                         "; "
                         "step > 0 ? ",
                         value, " <= last : ", value, " >= last; ", value,
                         " += step) {\n"
                         "\t",
                         naked_body, "}", stop);
        }
    } else if (for_->iter->tag == MethodCall && streq(Match(for_->iter, MethodCall)->name, "onward")
               && get_type(env, Match(for_->iter, MethodCall)->self)->tag == BigIntType) {
        // Special case for Int.onward()
        arg_ast_t *args = Match(for_->iter, MethodCall)->args;
        arg_t *arg_spec =
            new (arg_t, .name = "step", .type = INT_TYPE, .default_val = FakeAST(Int, .str = "1"), .next = NULL);
        Text_t step = compile_arguments(env, for_->iter, arg_spec, args);
        Text_t value = for_->vars ? compile(body_scope, for_->vars->ast) : Text("i");
        return Texts("for (Int_t ", value, " = ", compile(env, Match(for_->iter, MethodCall)->self), ", ",
                     "step = ", step, "; ; ", value, " = Int$plus(", value,
                     ", step)) {\n"
                     "\t",
                     naked_body, "}", stop);
    }

    type_t *iter_t = get_type(env, for_->iter);
    type_t *iter_value_t = value_type(iter_t);

    switch (iter_value_t->tag) {
    case ListType: {
        type_t *item_t = Match(iter_value_t, ListType)->item_type;
        Text_t index = EMPTY_TEXT;
        Text_t value = EMPTY_TEXT;
        if (for_->vars) {
            if (for_->vars->next) {
                if (for_->vars->next->next)
                    code_err(for_->vars->next->next->ast, "This is too many variables for this loop");

                index = compile(body_scope, for_->vars->ast);
                value = compile(body_scope, for_->vars->next->ast);
            } else {
                value = compile(body_scope, for_->vars->ast);
            }
        }

        Text_t loop = EMPTY_TEXT;
        loop = Texts(loop, "for (int64_t i = 1; i <= iterating.length; ++i)");

        if (index.length > 0) naked_body = Texts("Int_t ", index, " = I(i);\n", naked_body);

        if (value.length > 0) {
            loop = Texts(loop, "{\n", compile_declaration(item_t, value), " = *(", compile_type(item_t),
                         "*)(iterating.data + (i-1)*iterating.stride);\n", naked_body, "\n}");
        } else {
            loop = Texts(loop, "{\n", naked_body, "\n}");
        }

        if (for_->empty)
            loop = Texts("if (iterating.length > 0) {\n", loop, "\n} else ", compile_statement(env, for_->empty));

        if (iter_t->tag == PointerType) {
            loop = Texts("{\n"
                         "List_t *ptr = ",
                         compile_to_pointer_depth(env, for_->iter, 1, false),
                         ";\n"
                         "\nLIST_INCREF(*ptr);\n"
                         "List_t iterating = *ptr;\n",
                         loop, stop,
                         "\nLIST_DECREF(*ptr);\n"
                         "}\n");

        } else {
            loop = Texts("{\n"
                         "List_t iterating = ",
                         compile_to_pointer_depth(env, for_->iter, 0, false), ";\n", loop, stop, "}\n");
        }
        return loop;
    }
    case SetType:
    case TableType: {
        Text_t loop = Text("for (int64_t i = 0; i < iterating.length; ++i) {\n");
        if (for_->vars) {
            if (iter_value_t->tag == SetType) {
                if (for_->vars->next) code_err(for_->vars->next->ast, "This is too many variables for this loop");
                Text_t item = compile(body_scope, for_->vars->ast);
                type_t *item_type = Match(iter_value_t, SetType)->item_type;
                loop = Texts(loop, compile_declaration(item_type, item), " = *(", compile_type(item_type), "*)(",
                             "iterating.data + i*iterating.stride);\n");
            } else {
                Text_t key = compile(body_scope, for_->vars->ast);
                type_t *key_t = Match(iter_value_t, TableType)->key_type;
                loop = Texts(loop, compile_declaration(key_t, key), " = *(", compile_type(key_t), "*)(",
                             "iterating.data + i*iterating.stride);\n");

                if (for_->vars->next) {
                    if (for_->vars->next->next)
                        code_err(for_->vars->next->next->ast, "This is too many variables for this loop");

                    type_t *value_t = Match(iter_value_t, TableType)->value_type;
                    Text_t value = compile(body_scope, for_->vars->next->ast);
                    Text_t value_offset = Texts("offsetof(struct { ", compile_declaration(key_t, Text("k")), "; ",
                                                compile_declaration(value_t, Text("v")), "; }, v)");
                    loop = Texts(loop, compile_declaration(value_t, value), " = *(", compile_type(value_t), "*)(",
                                 "iterating.data + i*iterating.stride + ", value_offset, ");\n");
                }
            }
        }

        loop = Texts(loop, naked_body, "\n}");

        if (for_->empty) {
            loop = Texts("if (iterating.length > 0) {\n", loop, "\n} else ", compile_statement(env, for_->empty));
        }

        if (iter_t->tag == PointerType) {
            loop = Texts("{\n", "Table_t *t = ", compile_to_pointer_depth(env, for_->iter, 1, false),
                         ";\n"
                         "LIST_INCREF(t->entries);\n"
                         "List_t iterating = t->entries;\n",
                         loop,
                         "LIST_DECREF(t->entries);\n"
                         "}\n");
        } else {
            loop = Texts("{\n", "List_t iterating = (", compile_to_pointer_depth(env, for_->iter, 0, false),
                         ").entries;\n", loop, "}\n");
        }
        return loop;
    }
    case BigIntType: {
        Text_t n;
        if (for_->iter->tag == Int) {
            const char *str = Match(for_->iter, Int)->str;
            Int_t int_val = Int$from_str(str);
            if (int_val.small == 0) code_err(for_->iter, "Failed to parse this integer");
            mpz_t i;
            mpz_init_set_int(i, int_val);
            if (mpz_cmpabs_ui(i, BIGGEST_SMALL_INT) <= 0) n = Text$from_str(mpz_get_str(NULL, 10, i));
            else goto big_n;

            if (for_->empty && mpz_cmp_si(i, 0) <= 0) {
                return compile_statement(env, for_->empty);
            } else {
                return Texts("for (int64_t i = 1; i <= ", n, "; ++i) {\n",
                             for_->vars ? Texts("\tInt_t ", compile(body_scope, for_->vars->ast), " = I_small(i);\n")
                                        : EMPTY_TEXT,
                             "\t", naked_body, "}\n", stop, "\n");
            }
        }

    big_n:
        n = compile_to_pointer_depth(env, for_->iter, 0, false);
        Text_t i = for_->vars ? compile(body_scope, for_->vars->ast) : Text("i");
        Text_t n_var = for_->vars ? Texts("max", i) : Text("n");
        if (for_->empty) {
            return Texts("{\n"
                         "Int_t ",
                         n_var, " = ", n,
                         ";\n"
                         "if (Int$compare_value(",
                         n_var,
                         ", I(0)) > 0) {\n"
                         "for (Int_t ",
                         i, " = I(1); Int$compare_value(", i, ", ", n_var, ") <= 0; ", i, " = Int$plus(", i,
                         ", I(1))) {\n", "\t", naked_body,
                         "}\n"
                         "} else ",
                         compile_statement(env, for_->empty), stop,
                         "\n"
                         "}\n");
        } else {
            return Texts("for (Int_t ", i, " = I(1), ", n_var, " = ", n, "; Int$compare_value(", i, ", ", n_var,
                         ") <= 0; ", i, " = Int$plus(", i, ", I(1))) {\n", "\t", naked_body, "}\n", stop, "\n");
        }
    }
    case FunctionType:
    case ClosureType: {
        // Iterator function:
        Text_t code = Text("{\n");

        Text_t next_fn;
        if (is_idempotent(for_->iter)) {
            next_fn = compile_to_pointer_depth(env, for_->iter, 0, false);
        } else {
            code = Texts(code, compile_declaration(iter_value_t, Text("next")), " = ",
                         compile_to_pointer_depth(env, for_->iter, 0, false), ";\n");
            next_fn = Text("next");
        }

        __typeof(iter_value_t->__data.FunctionType) *fn =
            iter_value_t->tag == ClosureType ? Match(Match(iter_value_t, ClosureType)->fn, FunctionType)
                                             : Match(iter_value_t, FunctionType);

        Text_t get_next;
        if (iter_value_t->tag == ClosureType) {
            type_t *fn_t = Match(iter_value_t, ClosureType)->fn;
            arg_t *closure_fn_args = NULL;
            for (arg_t *arg = Match(fn_t, FunctionType)->args; arg; arg = arg->next)
                closure_fn_args = new (arg_t, .name = arg->name, .type = arg->type, .default_val = arg->default_val,
                                       .next = closure_fn_args);
            closure_fn_args = new (arg_t, .name = "userdata", .type = Type(PointerType, .pointed = Type(MemoryType)),
                                   .next = closure_fn_args);
            REVERSE_LIST(closure_fn_args);
            Text_t fn_type_code =
                compile_type(Type(FunctionType, .args = closure_fn_args, .ret = Match(fn_t, FunctionType)->ret));
            get_next = Texts("((", fn_type_code, ")", next_fn, ".fn)(", next_fn, ".userdata)");
        } else {
            get_next = Texts(next_fn, "()");
        }

        if (fn->ret->tag == OptionalType) {
            // Use an optional variable `cur` for each iteration step, which
            // will be checked for none
            code = Texts(code, compile_declaration(fn->ret, Text("cur")), ";\n");
            get_next = Texts("(cur=", get_next, ", !", check_none(fn->ret, Text("cur")), ")");
            if (for_->vars) {
                naked_body = Texts(compile_declaration(Match(fn->ret, OptionalType)->type,
                                                       Texts("_$", Match(for_->vars->ast, Var)->name)),
                                   " = ", optional_into_nonnone(fn->ret, Text("cur")), ";\n", naked_body);
            }
            if (for_->empty) {
                code = Texts(code, "if (", get_next,
                             ") {\n"
                             "\tdo{\n\t\t",
                             naked_body, "\t} while(", get_next,
                             ");\n"
                             "} else {\n\t",
                             compile_statement(env, for_->empty), "}", stop, "\n}\n");
            } else {
                code = Texts(code, "while(", get_next, ") {\n\t", naked_body, "}\n", stop, "\n}\n");
            }
        } else {
            if (for_->vars) {
                naked_body = Texts(compile_declaration(fn->ret, Texts("_$", Match(for_->vars->ast, Var)->name)), " = ",
                                   get_next, ";\n", naked_body);
            } else {
                naked_body = Texts(get_next, ";\n", naked_body);
            }
            if (for_->empty)
                code_err(for_->empty, "This iteration loop will always have values, "
                                      "so this block will never run");
            code = Texts(code, "for (;;) {\n\t", naked_body, "}\n", stop, "\n}\n");
        }

        return code;
    }
    default: code_err(for_->iter, "Iteration is not implemented for type: ", type_to_str(iter_t));
    }
}

public
Text_t compile_repeat(env_t *env, ast_t *ast) {
    ast_t *body = Match(ast, Repeat)->body;
    env_t *scope = fresh_scope(env);
    loop_ctx_t loop_ctx = (loop_ctx_t){
        .loop_name = "repeat",
        .deferred = scope->deferred,
        .next = env->loop_ctx,
    };
    scope->loop_ctx = &loop_ctx;
    Text_t body_code = compile_statement(scope, body);
    if (loop_ctx.skip_label.length > 0) body_code = Texts(body_code, "\n", loop_ctx.skip_label, ": continue;");
    Text_t loop = Texts("for (;;) {\n\t", body_code, "\n}");
    if (loop_ctx.stop_label.length > 0) loop = Texts(loop, "\n", loop_ctx.stop_label, ":;");
    return loop;
}

public
Text_t compile_while(env_t *env, ast_t *ast) {
    DeclareMatch(while_, ast, While);
    env_t *scope = fresh_scope(env);
    loop_ctx_t loop_ctx = (loop_ctx_t){
        .loop_name = "while",
        .deferred = scope->deferred,
        .next = env->loop_ctx,
    };
    scope->loop_ctx = &loop_ctx;
    Text_t body = compile_statement(scope, while_->body);
    if (loop_ctx.skip_label.length > 0) body = Texts(body, "\n", loop_ctx.skip_label, ": continue;");
    Text_t loop =
        Texts("while (", while_->condition ? compile(scope, while_->condition) : Text("yes"), ") {\n\t", body, "\n}");
    if (loop_ctx.stop_label.length > 0) loop = Texts(loop, "\n", loop_ctx.stop_label, ":;");
    return loop;
}

public
Text_t compile_skip(env_t *env, ast_t *ast) {
    const char *target = Match(ast, Skip)->target;
    for (loop_ctx_t *ctx = env->loop_ctx; ctx; ctx = ctx->next) {
        bool matched = !target || strcmp(target, ctx->loop_name) == 0;
        for (ast_list_t *var = ctx->loop_vars; var && !matched; var = var ? var->next : NULL)
            matched = (strcmp(target, Match(var->ast, Var)->name) == 0);

        if (matched) {
            if (ctx->skip_label.length == 0) {
                static int64_t skip_label_count = 1;
                ctx->skip_label = Texts("skip_", String(skip_label_count));
                ++skip_label_count;
            }
            Text_t code = EMPTY_TEXT;
            for (deferral_t *deferred = env->deferred; deferred && deferred != ctx->deferred; deferred = deferred->next)
                code = Texts(code, compile_statement(deferred->defer_env, deferred->block));
            if (code.length > 0) return Texts("{\n", code, "goto ", ctx->skip_label, ";\n}\n");
            else return Texts("goto ", ctx->skip_label, ";");
        }
    }
    if (env->loop_ctx) code_err(ast, "This is not inside any loop");
    else if (target) code_err(ast, "No loop target named '", target, "' was found");
    else return Text("continue;");
}

public
Text_t compile_stop(env_t *env, ast_t *ast) {
    const char *target = Match(ast, Stop)->target;
    for (loop_ctx_t *ctx = env->loop_ctx; ctx; ctx = ctx->next) {
        bool matched = !target || strcmp(target, ctx->loop_name) == 0;
        for (ast_list_t *var = ctx->loop_vars; var && !matched; var = var ? var->next : var)
            matched = (strcmp(target, Match(var->ast, Var)->name) == 0);

        if (matched) {
            if (ctx->stop_label.length == 0) {
                static int64_t stop_label_count = 1;
                ctx->stop_label = Texts("stop_", String(stop_label_count));
                ++stop_label_count;
            }
            Text_t code = EMPTY_TEXT;
            for (deferral_t *deferred = env->deferred; deferred && deferred != ctx->deferred; deferred = deferred->next)
                code = Texts(code, compile_statement(deferred->defer_env, deferred->block));
            if (code.length > 0) return Texts("{\n", code, "goto ", ctx->stop_label, ";\n}\n");
            else return Texts("goto ", ctx->stop_label, ";");
        }
    }
    if (env->loop_ctx) code_err(ast, "This is not inside any loop");
    else if (target) code_err(ast, "No loop target named '", target, "' was found");
    else return Text("break;");
}
