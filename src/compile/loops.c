// This file defines how to compile loops

#include <gmp.h>

#include "../ast.h"
#include "../config.h"
#include "../environment.h"
#include "../stdlib/datatypes.h"
#include "../stdlib/integers.h"
#include "../stdlib/text.h"
#include "../typecheck.h"
#include "../util.h"
#include "compilation.h"

// For loops with an optional `i` index variable (`for i, x in ...`), the index
// is an Int64 counting 1, 2, 3, ... -- an independent counter declared before
// the loop and stepped once per yielded element:
static Text_t index_counter_decl(Text_t index) {
    return index.length > 0 ? Texts("Int64_t ", index, "$counter = 0;\n") : EMPTY_TEXT;
}

static Text_t index_counter_step(Text_t index) {
    return index.length > 0 ? Texts("Int64_t ", index, " = ++", index, "$counter;\n") : EMPTY_TEXT;
}

// Compile `for [i,] &x in xs` -- in-place mutable iteration over a list.
//
// `&x` is a live pointer into the list's buffer, so element updates are plain
// in-place stores with no per-element bounds checks or copy-on-write guards.
// The list itself stays usable inside the body (reads and indexed writes see
// live data through the normal checked paths); the only things it can't do
// while the loop runs are the two that would invalidate the raw pointer or
// break copy-on-write, and both are runtime failures rather than silent bugs:
//   1. If a snapshot shares the buffer at loop entry (data_refcount > 0), we
//      compact once up front -- the same copy CoW would charge on first write.
//   2. List_ref_iter_guard (stdlib/lists.h) re-checks at the top of each
//      iteration AND once after the loop (including `stop` exits), so a
//      resize or copy in any iteration -- even the last -- fails before the
//      loop's results can be used. A mid-loop snapshot can't be *protected*
//      without a per-write CoW check (it would see the remainder of the
//      current iteration's writes), so it fails rather than silently
//      corrupting.
// `&x` itself is a non-escaping stack reference, so it can't outlive the loop.
static Text_t compile_for_reference_loop(env_t *env, ast_t *ast, Text_t naked_body, Text_t stop, type_t *item_t,
                                         Text_t index, ast_t *value_var) {
    DeclareMatch(for_, ast, For);
    ast_t *iter = for_->iters->ast;

    const char *name = Match(Match(value_var, StackReference)->value, Var)->name;
    Text_t value = Texts("_$", name);
    Text_t item_type_code = compile_type(item_t);
    Text_t guard = Texts("List_ref_iter_guard(ptr, data0, n0, stride0, ", quoted_str(ast->file->filename), ", ",
                         (int64_t)(ast->start - ast->file->text), ", ", (int64_t)(ast->end - ast->file->text), ");\n");

    Text_t loop = Texts("for (int64_t i = 1; i <= n0; ++i) {\n", guard, item_type_code, " *", value, " = (",
                        item_type_code, " *)(data0 + (i-1)*stride0);\n",
                        index.length > 0 ? Texts("Int64_t ", index, " = i;\n") : EMPTY_TEXT, naked_body, "}");

    if (for_->empty) loop = Texts("if (n0 > 0) {\n", loop, "\n} else ", compile_statement(env, for_->empty));

    return Texts("{ // for &", name, " in ...\n"
                 "List_t *ptr = ",
                 compile_to_pointer_depth(env, iter, 1, false),
                 ";\n"
                 "if unlikely (ptr->data_refcount > 0) List$compact(ptr, sizeof(",
                 item_type_code,
                 "));\n"
                 "void *data0 = ptr->data;\n"
                 "int64_t n0 = ptr->length;\n"
                 "int64_t stride0 = ptr->stride;\n",
                 loop, stop,
                 // Post-loop check (runs on normal completion and on `stop`,
                 // which lands on the label above): catches a resize/copy made
                 // during the final iteration, which the per-iteration guard
                 // (top-of-body) would otherwise never see.
                 "\nif (n0 > 0) ", guard, "}\n");
}

// Build the C call for one step of an iterator function or closure, e.g.
// `next(&x, &y)` or `((ret(*)(...))next.fn)(&x, &y, next.userdata)`:
static Text_t compile_iterator_call(type_t *iter_value_t, Text_t next_fn, Text_t args_joined) {
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
        return Texts("((", fn_type_code, ")", next_fn, ".fn)(", args_joined, args_joined.length > 0 ? ", " : "",
                     next_fn, ".userdata)");
    } else {
        return Texts(next_fn, "(", args_joined, ")");
    }
}

// Compile `for x, y in xs, ys` -- lockstep iteration over several iterables.
//
// Each iterable contributes a setup step (run once, leftmost first, so the
// iterables are evaluated in source order) and an advance-or-break step at the
// top of a single shared loop, so the loop ends as soon as ANY iterable runs
// out. Each iterable's yielded values bind to its slice of the loop variables
// (`_` discards one); arity was already checked in for_scope(). The `else`
// block (if any) runs when the body never ran at all.
static Text_t compile_lockstep_loop(env_t *env, ast_t *ast, env_t *body_scope, Text_t naked_body, Text_t stop) {
    DeclareMatch(for_, ast, For);

    Text_t setup = EMPTY_TEXT, advance = EMPTY_TEXT, cleanup = EMPTY_TEXT;
    ast_list_t *var = for_->vars;
    int64_t k = 0;
    for (ast_list_t *it = for_->iters; it; it = it->next, k += 1) {
        Text_t temp = Texts("lockstep", k);
        type_t *raw_t = get_type(env, it->ast);
        type_t *iter_value_t = value_type(raw_t);
        switch (iter_value_t->tag) {
        case ListType: {
            type_t *item_t = Match(iter_value_t, ListType)->item_type;
            Text_t list = Texts(temp, "$list"), idx = Texts(temp, "$i");
            if (raw_t->tag == PointerType) {
                Text_t ptr = Texts(temp, "$ptr");
                setup = Texts(setup, "List_t *", ptr, " = ", compile_to_pointer_depth(env, it->ast, 1, false),
                              ";\nLIST_INCREF(*", ptr, ");\nList_t ", list, " = *", ptr, ";\n");
                cleanup = Texts(cleanup, "LIST_DECREF(*", ptr, ");\n");
            } else {
                setup =
                    Texts(setup, "List_t ", list, " = ", compile_to_pointer_depth(env, it->ast, 0, false), ";\n");
            }
            setup = Texts(setup, "int64_t ", idx, " = 0;\n");
            advance = Texts(advance, "if (", idx, " >= ", list, ".length) break;\n", idx, " += 1;\n");
            ast_t *v = var->ast;
            var = var->next;
            if (!streq(Match(v, Var)->name, "_"))
                advance = Texts(advance, compile_declaration(item_t, compile(body_scope, v)), " = *(",
                                compile_type(item_t), "*)(", list, ".data + (", idx, "-1)*", list, ".stride);\n");
            break;
        }
        case TableType: {
            // Only sets ({T}) reach here (direct table iteration is a compile
            // error in iteration_slots()); iterate the entry keys:
            type_t *item_t = Match(iter_value_t, TableType)->key_type;
            Text_t list = Texts(temp, "$entries"), idx = Texts(temp, "$i");
            if (raw_t->tag == PointerType) {
                Text_t ptr = Texts(temp, "$ptr");
                setup = Texts(setup, "Table_t *", ptr, " = ", compile_to_pointer_depth(env, it->ast, 1, false),
                              ";\nLIST_INCREF(", ptr, "->entries);\nList_t ", list, " = ", ptr, "->entries;\n");
                cleanup = Texts(cleanup, "LIST_DECREF(", ptr, "->entries);\n");
            } else {
                setup = Texts(setup, "List_t ", list, " = (", compile_to_pointer_depth(env, it->ast, 0, false),
                              ").entries;\n");
            }
            setup = Texts(setup, "int64_t ", idx, " = 0;\n");
            advance = Texts(advance, "if (", idx, " >= ", list, ".length) break;\n", idx, " += 1;\n");
            ast_t *v = var->ast;
            var = var->next;
            if (!streq(Match(v, Var)->name, "_"))
                advance = Texts(advance, compile_declaration(item_t, compile(body_scope, v)), " = *(",
                                compile_type(item_t), "*)(", list, ".data + (", idx, "-1)*", list, ".stride);\n");
            break;
        }
        case BigIntType: {
            Text_t cur = Texts(temp, "$cur"), max = Texts(temp, "$max");
            setup = Texts(setup, "Int_t ", cur, " = I_small(1), ", max, " = ",
                          compile_to_pointer_depth(env, it->ast, 0, false), ";\n");
            advance = Texts(advance, "if (Int$compare_value(", cur, ", ", max, ") > 0) break;\n");
            ast_t *v = var->ast;
            var = var->next;
            if (!streq(Match(v, Var)->name, "_"))
                advance = Texts(advance, "Int_t ", compile(body_scope, v), " = ", cur, ";\n");
            advance = Texts(advance, cur, " = Int$plus(", cur, ", I_small(1));\n");
            break;
        }
        case IntType: {
            Text_t type_code = compile_type(iter_value_t);
            Text_t cur = Texts(temp, "$cur"), max = Texts(temp, "$max");
            setup = Texts(setup, "Int64_t ", cur, " = 0, ", max, " = (Int64_t)(",
                          compile_to_pointer_depth(env, it->ast, 0, false), ");\n");
            advance = Texts(advance, "if (", cur, " >= ", max, ") break;\n", cur, " += 1;\n");
            ast_t *v = var->ast;
            var = var->next;
            if (!streq(Match(v, Var)->name, "_"))
                advance = Texts(advance, type_code, " ", compile(body_scope, v), " = (", type_code, ")", cur, ";\n");
            break;
        }
        case TextType: {
            Text_t state = Texts(temp, "$state"), idx = Texts(temp, "$i");
            setup = Texts(setup, "TextIter_t ", state, " = NEW_TEXT_ITER_STATE(",
                          compile_to_pointer_depth(env, it->ast, 0, false), ");\nint64_t ", idx, " = 0;\n");
            advance = Texts(advance, "if (", idx, " >= (int64_t)", state, ".stack[0].text.length) break;\n");
            ast_t *v = var->ast;
            var = var->next;
            if (!streq(Match(v, Var)->name, "_"))
                advance = Texts(advance, "Text_t ", compile(body_scope, v), " = Text$from_grapheme(",
                                "Text$get_grapheme_fast(&", state, ", ", idx, "));\n");
            advance = Texts(advance, idx, " += 1;\n");
            break;
        }
        case FunctionType:
        case ClosureType: {
            Text_t next_fn;
            if (is_idempotent(it->ast)) {
                next_fn = compile_to_pointer_depth(env, it->ast, 0, false);
            } else {
                next_fn = Texts(temp, "$next");
                setup = Texts(setup, compile_declaration(iter_value_t, next_fn), " = ",
                              compile_to_pointer_depth(env, it->ast, 0, false), ";\n");
            }

            arg_t *yield_args = iterator_yield_args(iter_value_t);
            if (yield_args) {
                // Multi-value iterator protocol: the loop variables are the
                // `&` out-slots (declared outside the loop, written each call):
                Text_t args_joined = EMPTY_TEXT;
                int64_t pos = 1;
                for (arg_t *arg = yield_args; arg; arg = arg->next, pos += 1) {
                    type_t *val_t = Match(arg->type, PointerType)->pointed;
                    ast_t *v = var->ast;
                    var = var->next;
                    Text_t name = streq(Match(v, Var)->name, "_") ? Texts(temp, "$yield", pos)
                                                                  : compile(body_scope, v);
                    setup = Texts(setup, compile_declaration(val_t, name), ";\n");
                    args_joined = pos == 1 ? Texts("&", name) : Texts(args_joined, ", &", name);
                }
                advance =
                    Texts(advance, "if (!", compile_iterator_call(iter_value_t, next_fn, args_joined), ") break;\n");
            } else {
                __typeof(iter_value_t->__data.FunctionType) *fn =
                    iter_value_t->tag == ClosureType ? Match(Match(iter_value_t, ClosureType)->fn, FunctionType)
                                                     : Match(iter_value_t, FunctionType);
                Text_t call = compile_iterator_call(iter_value_t, next_fn, EMPTY_TEXT);
                ast_t *v = var->ast;
                var = var->next;
                bool discard = streq(Match(v, Var)->name, "_");
                if (fn->ret->tag == OptionalType) {
                    Text_t cur = Texts(temp, "$cur");
                    setup = Texts(setup, compile_declaration(fn->ret, cur), ";\n");
                    advance = Texts(advance, cur, " = ", call, ";\nif (", check_none(fn->ret, cur), ") break;\n");
                    if (!discard)
                        advance = Texts(advance,
                                        compile_declaration(Match(fn->ret, OptionalType)->type,
                                                            compile(body_scope, v)),
                                        " = ", optional_into_nonnone(fn->ret, cur), ";\n");
                } else if (!discard) {
                    advance = Texts(advance, compile_declaration(fn->ret, compile(body_scope, v)), " = ", call, ";\n");
                } else {
                    advance = Texts(advance, call, ";\n");
                }
            }
            break;
        }
        default: code_err(it->ast, "Iteration is not implemented for type: ", type_to_text(raw_t));
        }
    }

    Text_t at_decl = EMPTY_TEXT, at_step = EMPTY_TEXT;
    if (for_->at) {
        Text_t index = compile(body_scope, for_->at);
        at_decl = index_counter_decl(index);
        at_step = index_counter_step(index);
    }

    Text_t code = Texts("{ // lockstep iteration\n", setup, at_decl);
    if (for_->empty) code = Texts(code, "Bool_t lockstep$ran = no;\n");
    code = Texts(code, "for (;;) {\n", advance);
    if (for_->empty) code = Texts(code, "lockstep$ran = yes;\n");
    code = Texts(code, at_step, naked_body, "}\n", stop, "\n", cleanup);
    if (for_->empty) code = Texts(code, "if (!lockstep$ran) ", compile_statement(env, for_->empty), "\n");
    return Texts(code, "}\n");
}

public
Text_t compile_for_loop(env_t *env, ast_t *ast) {
    DeclareMatch(for_, ast, For);
    ast_t *iter = for_->iters->ast;

    // If we're iterating over a comprehension, that's actually just doing
    // one loop, we don't need to compile the comprehension as a list
    // comprehension. This is a common case for reducers like `(+: i*2 for i
    // in 5)` or `(and) x.is_good() for x in xs`
    if (!for_->iters->next && iter->tag == Comprehension) {
        DeclareMatch(comp, iter, Comprehension);
        ast_t *body = for_->body;
        if (for_->at)
            code_err(for_->at, "An `at` counter isn't supported when iterating over a comprehension. "
                               "Put the `at` inside the comprehension instead.");
        if (for_->vars) {
            if (for_->vars->ast->tag == StackReference)
                code_err(for_->vars->ast, "You can't iterate by reference over a comprehension");
            if (for_->vars->next) code_err(for_->vars->next->ast, "This is too many variables for iteration");

            body = WrapAST(
                ast, Block,
                .statements =
                    new (ast_list_t, .ast = WrapAST(ast, Declare, .var = for_->vars->ast, .value = comp->expr),
                         .next = body->tag == Block ? Match(body, Block)->statements : new (ast_list_t, .ast = body)));
        }

        if (comp->filter) body = WrapAST(for_->body, If, .condition = comp->filter, .body = body);
        ast_t *loop = WrapAST(ast, For, .vars = comp->vars, .at = comp->at, .iters = comp->iters, .body = body);
        return compile_statement(env, loop);
    }

    env_t *body_scope = for_scope(env, ast);
    // `skip`/`stop` can target any loop variable by name, including the `at` counter:
    ast_list_t *ctx_vars = for_->at ? new (ast_list_t, .ast = for_->at, .next = for_->vars) : for_->vars;
    loop_ctx_t loop_ctx = (loop_ctx_t){
        .loop_name = "for",
        .loop_vars = ctx_vars,
        .deferred = body_scope->deferred,
        .next = body_scope->loop_ctx,
    };
    body_scope->loop_ctx = &loop_ctx;
    // Naked means no enclosing braces:
    Text_t naked_body = compile_inline_block(body_scope, for_->body);
    if (loop_ctx.skip_label.length > 0) naked_body = Texts(naked_body, "\n", loop_ctx.skip_label, ": continue;");
    Text_t stop = loop_ctx.stop_label.length > 0 ? Texts("\n", loop_ctx.stop_label, ":;") : EMPTY_TEXT;

    if (for_->iters->next) return compile_lockstep_loop(env, ast, body_scope, naked_body, stop);

    // Special case for improving performance for numeric iteration:
    if (iter->tag == MethodCall && streq(Match(iter, MethodCall)->name, "to")
        && is_int_type(get_type(env, Match(iter, MethodCall)->self))) {
        // TODO: support other integer types
        arg_ast_t *args = Match(iter, MethodCall)->args;
        if (!args) code_err(iter, "to() needs at least one argument");

        type_t *int_type = get_type(env, Match(iter, MethodCall)->self);
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

        if (last.length == 0) code_err(iter, "No `last` argument was given");

        Text_t type_code = compile_type(int_type);
        // `for i, x in a.to(b)`: `i` is an Int64 iteration counter (1, 2, 3, ...)
        // and `x` is the range value, typed like the range's endpoints.
        ast_t *index_var = for_->at;
        ast_t *value_var = single_loop_var(for_->vars);
        Text_t index = index_var ? compile(body_scope, index_var) : EMPTY_TEXT;
        Text_t value = value_var ? compile(body_scope, value_var) : Text("i");
        Text_t counter_decl = index_counter_decl(index);
        Text_t index_decl = index_counter_step(index);
        Text_t open = index.length > 0 ? Text("{\n") : EMPTY_TEXT;
        Text_t close = index.length > 0 ? Text("\n}\n") : EMPTY_TEXT;
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
            return Texts(open, counter_decl, "for (", type_code,
                         " first = ", compile(env, Match(iter, MethodCall)->self), ", ", value,
                         " = first, last = ", last, ", step = ", step,
                         "; "
                         "Int$compare_value(",
                         value, ", last) != Int$compare_value(step, I_small(0)); ", value, " = Int$plus(", value,
                         ", step)) {\n", index_decl,
                         "\t",
                         naked_body, "}", stop, close);
        } else {
            if (optional_step.length > 0)
                step = Texts("({ ", compile_type(Type(OptionalType, step_type)), " maybe_step = ", optional_step,
                             "; "
                             "maybe_step.is_none ? (",
                             type_code, ")(last >= first ? 1 : -1) : maybe_step.value; })");
            else if (step.length == 0) step = Texts("(", type_code, ")(last >= first ? 1 : -1)");
            return Texts(open, counter_decl, "for (", type_code,
                         " first = ", compile(env, Match(iter, MethodCall)->self), ", ", value,
                         " = first, last = ", last, ", step = ", step, "; (", compile_type(step_type),
                         ")step > 0 ? ", value, " <= last : ", value, " >= last; ", value,
                         " += step) {\n", index_decl,
                         "\t",
                         naked_body, "}", stop, close);
        }
    } else if (iter->tag == MethodCall && streq(Match(iter, MethodCall)->name, "onward")
               && get_type(env, Match(iter, MethodCall)->self)->tag == BigIntType) {
        // Special case for Int.onward()
        arg_ast_t *args = Match(iter, MethodCall)->args;
        arg_t *arg_spec =
            new (arg_t, .name = "step", .type = INT_TYPE, .default_val = FakeAST(Int, .str = "1"), .next = NULL);
        Text_t step = compile_arguments(env, iter, arg_spec, args);
        ast_t *index_var = for_->at;
        ast_t *value_var = single_loop_var(for_->vars);
        Text_t index = index_var ? compile(body_scope, index_var) : EMPTY_TEXT;
        Text_t value = value_var ? compile(body_scope, value_var) : Text("i");
        Text_t open = index.length > 0 ? Text("{\n") : EMPTY_TEXT;
        Text_t close = index.length > 0 ? Text("\n}\n") : EMPTY_TEXT;
        return Texts(open, index_counter_decl(index), "for (Int_t ", value, " = ",
                     compile(env, Match(iter, MethodCall)->self), ", ", "step = ", step, "; ; ", value,
                     " = Int$plus(", value,
                     ", step)) {\n", index_counter_step(index),
                     "\t",
                     naked_body, "}", stop, close);
    }

    type_t *iter_t = get_type(env, iter);
    type_t *iter_value_t = value_type(iter_t);

    switch (iter_value_t->tag) {
    case ListType: {
        type_t *item_t = Match(iter_value_t, ListType)->item_type;
        ast_t *index_var = for_->at;
        ast_t *value_var = single_loop_var(for_->vars);
        Text_t index = index_var ? compile(body_scope, index_var) : EMPTY_TEXT;
        Text_t value = EMPTY_TEXT;

        if (value_var && value_var->tag == StackReference)
            return compile_for_reference_loop(env, ast, naked_body, stop, item_t, index, value_var);

        if (value_var) value = compile(body_scope, value_var);

        Text_t loop = EMPTY_TEXT;
        loop = Texts(loop, "for (int64_t i = 1; i <= iterating.length; ++i)");

        if (index.length > 0) naked_body = Texts("Int64_t ", index, " = i;\n", naked_body);

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
                         compile_to_pointer_depth(env, iter, 1, false),
                         ";\n"
                         "\nLIST_INCREF(*ptr);\n"
                         "List_t iterating = *ptr;\n",
                         loop, stop,
                         "\nLIST_DECREF(*ptr);\n"
                         "}\n");

        } else {
            loop = Texts("{\n"
                         "List_t iterating = ",
                         compile_to_pointer_depth(env, iter, 0, false), ";\n", loop, stop, "}\n");
        }
        return loop;
    }
    case TableType: {
        // Only sets ({T}) reach here (direct table iteration is a compile
        // error in for_scope()); iterate the entry keys:
        Text_t loop = Text("for (int64_t i = 0; i < (int64_t)iterating.length; ++i) {\n");
        if (for_->at) loop = Texts(loop, "Int64_t ", compile(body_scope, for_->at), " = i + 1;\n");
        ast_t *value_var = single_loop_var(for_->vars);
        if (value_var) {
            Text_t item = compile(body_scope, value_var);
            type_t *item_t = Match(iter_value_t, TableType)->key_type;
            loop = Texts(loop, compile_declaration(item_t, item), " = *(", compile_type(item_t), "*)(",
                         "iterating.data + i*iterating.stride);\n");
        }

        loop = Texts(loop, naked_body, "\n}");

        if (for_->empty) {
            loop = Texts("if (iterating.length > 0) {\n", loop, "\n} else ", compile_statement(env, for_->empty));
        }

        // NOTE: the `stop` label goes before the DECREF (like the ListType
        // case) so that stopping out of the loop still releases the refcount.
        if (iter_t->tag == PointerType) {
            loop = Texts("{\n", "Table_t *t = ", compile_to_pointer_depth(env, iter, 1, false),
                         ";\n"
                         "LIST_INCREF(t->entries);\n"
                         "List_t iterating = t->entries;\n",
                         loop, stop,
                         "\nLIST_DECREF(t->entries);\n"
                         "}\n");
        } else {
            loop = Texts("{\n", "List_t iterating = (", compile_to_pointer_depth(env, iter, 0, false),
                         ").entries;\n", loop, stop, "}\n");
        }
        return loop;
    }
    case BigIntType: {
        // `for x in n` counts with the count's own type (`Int`); the optional
        // index form `for i, x in n` adds an Int64 index. The index is just a
        // counter starting at 1 -- it can't plausibly overflow within any
        // physically executable loop, so it needs no relation to the bound.
        ast_t *index_var = for_->at;
        ast_t *value_var = single_loop_var(for_->vars);
        Text_t index = index_var ? compile(body_scope, index_var) : EMPTY_TEXT;
        Text_t value = value_var ? compile(body_scope, value_var) : EMPTY_TEXT;

        Text_t n;
        if (iter->tag == Int) {
            const char *str = Match(iter, Int)->str;
            Int_t int_val = Int$from_str(str);
            if (int_val.small == 0) code_err(iter, "Failed to parse this integer");
            mpz_t i;
            if likely (int_val.small & 1L) {
                mpz_init_set_si(i, int_val.small >> 2L);
            } else {
                mpz_init_set(i, int_val.big);
            }
            if (mpz_cmpabs_ui(i, BIGGEST_SMALL_INT) <= 0) n = Text$from_str(mpz_get_str(NULL, 10, i));
            else goto big_n;

            if (for_->empty && mpz_cmp_si(i, 0) <= 0) {
                return compile_statement(env, for_->empty);
            } else {
                return Texts("for (int64_t i = 1; i <= ", n, "; ++i) {\n",
                             index.length > 0 ? Texts("\tInt64_t ", index, " = i;\n") : EMPTY_TEXT,
                             value.length > 0 ? Texts("\tInt_t ", value, " = I_small(i);\n") : EMPTY_TEXT, "\t",
                             naked_body, "}\n", stop, "\n");
            }
        }

    big_n:
        n = compile_to_pointer_depth(env, iter, 0, false);
        Text_t i = value.length > 0 ? value : Text("i");
        Text_t n_var = value.length > 0 ? Texts("max", i) : Text("n");
        Text_t counter_decl = index_counter_decl(index);
        Text_t index_decl = index_counter_step(index);
        if (for_->empty) {
            return Texts("{\n", counter_decl,
                         "Int_t ",
                         n_var, " = ", n,
                         ";\n"
                         "if (Int$compare_value(",
                         n_var,
                         ", I(0)) > 0) {\n"
                         "for (Int_t ",
                         i, " = I(1); Int$compare_value(", i, ", ", n_var, ") <= 0; ", i, " = Int$plus(", i,
                         ", I(1))) {\n", index_decl, "\t", naked_body,
                         "}\n"
                         "} else ",
                         compile_statement(env, for_->empty), stop,
                         "\n"
                         "}\n");
        } else {
            Text_t open = index.length > 0 ? Text("{\n") : EMPTY_TEXT;
            Text_t close = index.length > 0 ? Text("\n}\n") : EMPTY_TEXT;
            return Texts(open, counter_decl, "for (Int_t ", i, " = I(1), ", n_var, " = ", n,
                         "; Int$compare_value(", i, ", ", n_var, ") <= 0; ", i, " = Int$plus(", i, ", I(1))) {\n",
                         index_decl, "\t", naked_body, "}\n", stop, close);
        }
    }
    case IntType: {
        // Native-int counts (`for [i,] x in n` where n is Int64/Int32/...): a
        // native loop whose value variable has the count's own type and whose
        // optional index is an Int64. The internal counter is Int64 so that
        // counting to a smaller type's maximum can't overflow.
        ast_t *index_var = for_->at;
        ast_t *value_var = single_loop_var(for_->vars);
        Text_t index = index_var ? compile(body_scope, index_var) : EMPTY_TEXT;
        Text_t value = value_var ? compile(body_scope, value_var) : EMPTY_TEXT;
        Text_t type_code = compile_type(iter_value_t);
        Text_t n = compile_to_pointer_depth(env, iter, 0, false);
        Text_t decls = Texts(index.length > 0 ? Texts("Int64_t ", index, " = i$;\n") : EMPTY_TEXT,
                             value.length > 0 ? Texts(type_code, " ", value, " = (", type_code, ")i$;\n")
                                              : EMPTY_TEXT);
        Text_t n_var = value.length > 0 ? Texts("max", value) : Text("n");
        if (for_->empty) {
            return Texts("{\n"
                         "Int64_t ",
                         n_var, " = (Int64_t)(", n,
                         ");\n"
                         "if (",
                         n_var,
                         " > 0) {\n"
                         "for (Int64_t i$ = 1; i$ <= ", n_var, "; ++i$) {\n", decls, "\t", naked_body,
                         "}\n"
                         "} else ",
                         compile_statement(env, for_->empty), stop,
                         "\n"
                         "}\n");
        } else {
            return Texts("for (Int64_t i$ = 1, ", n_var, " = (Int64_t)(", n, "); i$ <= ", n_var, "; ++i$) {\n",
                         decls, "\t", naked_body, "}\n", stop, "\n");
        }
    }
    case FunctionType:
    case ClosureType: {
        // Iterator function. `for x at i in iterfn` gives an Int64 iteration
        // counter (1, 2, 3, ...) alongside each yielded value.
        ast_t *index_var = for_->at;
        Text_t code = Text("{\n");
        if (index_var) {
            Text_t index = compile(body_scope, index_var);
            code = Texts(code, index_counter_decl(index));
            naked_body = Texts(index_counter_step(index), naked_body);
        }

        Text_t next_fn;
        if (is_idempotent(iter)) {
            next_fn = compile_to_pointer_depth(env, iter, 0, false);
        } else {
            code = Texts(code, compile_declaration(iter_value_t, Text("next")), " = ",
                         compile_to_pointer_depth(env, iter, 0, false), ";\n");
            next_fn = Text("next");
        }

        __typeof(iter_value_t->__data.FunctionType) *fn =
            iter_value_t->tag == ClosureType ? Match(Match(iter_value_t, ClosureType)->fn, FunctionType)
                                             : Match(iter_value_t, FunctionType);

        // Multi-value iterator protocol: every argument is a `&` out-parameter
        // and the return is a Bool saying whether values were produced. The
        // loop variables themselves are the out-slots -- each call writes the
        // next values directly into them, no tuple or wrapper value involved.
        arg_t *yield_args = iterator_yield_args(iter_value_t);
        if (yield_args) {
            Text_t args_joined = EMPTY_TEXT;
            int64_t pos = 1;
            ast_list_t *v = for_->vars;
            for (arg_t *arg = yield_args; arg; arg = arg->next, pos += 1) {
                type_t *val_t = Match(arg->type, PointerType)->pointed;
                Text_t name = (v && !streq(Match(v->ast, Var)->name, "_")) ? Texts("_$", Match(v->ast, Var)->name)
                                                                           : Texts("yield$", pos);
                code = Texts(code, compile_declaration(val_t, name), ";\n");
                args_joined = pos == 1 ? Texts("&", name) : Texts(args_joined, ", &", name);
                if (v) v = v->next;
            }

            Text_t get_next = compile_iterator_call(iter_value_t, next_fn, args_joined);

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
            return code;
        }

        ast_t *value_var = single_loop_var(for_->vars);

        Text_t get_next = compile_iterator_call(iter_value_t, next_fn, EMPTY_TEXT);

        if (fn->ret->tag == OptionalType) {
            // Use an optional variable `cur` for each iteration step, which
            // will be checked for none
            code = Texts(code, compile_declaration(fn->ret, Text("cur")), ";\n");
            get_next = Texts("(cur=", get_next, ", !", check_none(fn->ret, Text("cur")), ")");
            if (value_var) {
                naked_body = Texts(compile_declaration(Match(fn->ret, OptionalType)->type,
                                                       Texts("_$", Match(value_var, Var)->name)),
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
            if (value_var) {
                naked_body = Texts(compile_declaration(fn->ret, Texts("_$", Match(value_var, Var)->name)), " = ",
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
    case TextType: {
        ast_t *index_var = for_->at;
        ast_t *value_var = single_loop_var(for_->vars);
        Text_t index = index_var ? compile(body_scope, index_var) : EMPTY_TEXT;
        Text_t value = value_var ? compile(body_scope, value_var) : EMPTY_TEXT;

        Text_t code =
            Texts("{\n"
                  "TextIter_t ",
                  value, "$state = NEW_TEXT_ITER_STATE(", compile_to_pointer_depth(env, iter, 0, false), ");\n");

        Text_t loop = Texts("for (int64_t ", value, "$i = 0; ", value, "$i < (int64_t)", value,
                            "$state.stack[0].text.length; ", value, "$i += 1) {\n");

        if (index.length > 0) {
            // 1-indexed, to match text cluster indexing:
            loop = Texts(loop, "Int64_t ", index, " = ", value, "$i + 1;\n");
        }

        if (value.length > 0) {
            loop = Texts(loop, "int32_t g = Text$get_grapheme_fast(&", value, "$state, ", value,
                         "$i);\n"
                         "Text_t ",
                         value, " = Text$from_grapheme(g);\n", naked_body, "}\n");
        } else {
            loop = Texts(loop, naked_body, "}\n");
        }

        if (for_->empty)
            loop = Texts("if (", value, "$state.stack[0].text.length > 0) {\n", loop, "\n} else ",
                         compile_statement(env, for_->empty));

        code = Texts(code, loop, stop, "}\n");
        return code;
    }
    default: code_err(iter, "Iteration is not implemented for type: ", type_to_text(iter_t));
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
        for (ast_list_t *var = ctx->loop_vars; var && !matched; var = var ? var->next : NULL) {
            ast_t *var_ast = var->ast->tag == StackReference ? Match(var->ast, StackReference)->value : var->ast;
            matched = (strcmp(target, Match(var_ast, Var)->name) == 0);
        }

        if (matched) {
            if (ctx->skip_label.length == 0) {
                static int64_t skip_label_count = 1;
                ctx->skip_label = Texts("skip_", skip_label_count);
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
        for (ast_list_t *var = ctx->loop_vars; var && !matched; var = var ? var->next : var) {
            ast_t *var_ast = var->ast->tag == StackReference ? Match(var->ast, StackReference)->value : var->ast;
            matched = (strcmp(target, Match(var_ast, Var)->name) == 0);
        }

        if (matched) {
            if (ctx->stop_label.length == 0) {
                static int64_t stop_label_count = 1;
                ctx->stop_label = Texts("stop_", stop_label_count);
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
