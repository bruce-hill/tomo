// This file defines how to compile lists

#include <gc.h>
#include <glob.h>
#include <gmp.h>
#include <math.h>
#include <uninorm.h>

#include "../ast.h"
#include "../config.h"
#include "../environment.h"
#include "../stdlib/tables.h"
#include "../stdlib/text.h"
#include "../util.h"
#include "../typecheck.h"
#include "compilation.h"

// Is `ast` a literal whose value for `item_type` is all-bits-zero? (So a
// comprehension of it can be produced by a zeroed allocation with no fill
// loop.) Only types whose zero value is genuinely all-bits-zero qualify:
// bytes, fixed-width ints (NOT bignum Int, whose 0 is the tagged value 1),
// Nums (+0.0 is all-zero bits; -0.0 is not), and Bools. Unwraps a numeric
// constructor like `Byte(0)` / `Int64(0)` to its literal argument.
static bool is_zero_valued_literal(env_t *env, ast_t *ast, type_t *item_type) {
    if (ast->tag == FunctionCall) {
        DeclareMatch(call, ast, FunctionCall);
        type_t *fn_t = get_type(env, call->fn);
        // Forward through a numeric cast only, since `Byte(0)`/`Int64(0)`/`Num(0.0)`
        // really is all-bits-zero when its argument is. A struct/enum
        // constructor like `Foo(0)` is NOT (its other fields may default to
        // nonzero, and its layout isn't its argument's), so it never qualifies.
        if (fn_t->tag == TypeInfoType && call->args && !call->args->next && !call->args->name) {
            type_t *ctor_t = Match(fn_t, TypeInfoType)->type;
            if (ctor_t->tag == ByteType || ctor_t->tag == IntType || ctor_t->tag == FloatType
                || ctor_t->tag == BoolType)
                return is_zero_valued_literal(env, call->args->value, ctor_t);
        }
        return false;
    }
    if (ast->tag == Int) {
        if (item_type->tag != ByteType && item_type->tag != IntType && item_type->tag != FloatType) return false;
        // Zero always fits the tagged small form (a bignum is never zero):
        Int_t v = Match(ast, Int)->i;
        return (v.small & 1L) && ((v.small >> 2L) == 0);
    }
    if (ast->tag == Num) {
        if (item_type->tag != FloatType) return false;
        double n = num_literal_double(ast);
        return n == 0.0 && !signbit(n);
    }
    if (ast->tag == Bool) return item_type->tag == BoolType && Match(ast, Bool)->b == false;
    return false;
}

static ast_t *add_to_list_comprehension(ast_t *item, ast_t *subject) {
    // Append at the end. `insert` at the default index I(0) hits an inlined
    // fast path in List$insert_value (a bounds-free store, no function call),
    // which GCC selects at compile time here since the index is constant.
    return WrapAST(item, MethodCall, .name = "insert", .self = subject, .args = new (arg_ast_t, .value = item));
}

public
Text_t compile_typed_list(env_t *env, ast_t *ast, type_t *list_type) {
    DeclareMatch(list, ast, List);
    if (!list->items) return Text("EMPTY_LIST");

    type_t *item_type = Match(list_type, ListType)->item_type;
    if (item_type == NULL) code_err(ast, "I couldn't figure out what item type goes into this list");

    int64_t n = 0;
    for (ast_list_t *item = list->items; item; item = item->next) {
        ++n;
        if (item->ast->tag == Comprehension) goto list_comprehension;
    }

    {
        env_t *scope = item_type->tag == EnumType ? with_enum_scope(env, item_type) : env;
        if (is_incomplete_type(item_type)) code_err(ast, "This list's type can't be inferred!");
        bool all_const = true;
        for (ast_list_t *item = list->items; item; item = item->next) {
            if (!is_constant(scope, item->ast, item_type)) {
                all_const = false;
                break;
            }
        }
        if (all_const) {
            // Every item is a compile-time constant: hoist the data into a
            // module-level `static const` array (program lifetime, .rodata) and
            // point a `ConstList` at it. The result is a constant List_t
            // *expression* (no allocation, no copy, valid even as a
            // file-scope initializer) that never dangles wherever it escapes to. Mutation
            // is guarded by copy-on-write (see ConstList in stdlib/lists.h).
            static int64_t const_list_num = 0;
            Text_t data_name = Text$from_str(String("list_literal$", ++const_list_num));
            Text_t items = EMPTY_TEXT;
            for (ast_list_t *item = list->items; item; item = item->next)
                items = Texts(items, items.length > 0 ? Text(", ") : EMPTY_TEXT,
                              compile_to_type(scope, item->ast, item_type));
            env->code->constants = Texts(env->code->constants, "static const ", compile_type(item_type), " ",
                                         data_name, "[", n, "] = {", items, "};\n");
            return Texts("ConstList(", compile_type(item_type), ", ", n, ", ", data_name, ")");
        }
        Text_t code = Texts("TypedListN(", compile_type(item_type), ", ", n);
        for (ast_list_t *item = list->items; item; item = item->next) {
            code = Texts(code, ", ", compile_to_type(scope, item->ast, item_type));
        }
        return Texts(code, ")");
    }

list_comprehension: {
    env_t *scope = item_type->tag == EnumType ? with_enum_scope(env, item_type) : fresh_scope(env);
    static int64_t comp_num = 1;
    int64_t this_comp = comp_num++;
    const char *comprehension_name = String("list$", this_comp);
    ast_t *comprehension_var =
        LiteralCode(Texts("&", comprehension_name), .type = Type(PointerType, .pointed = list_type, .is_stack = true));
    Closure_t comp_action = {.fn = add_to_list_comprehension, .userdata = comprehension_var};
    scope->comprehension_action = &comp_action;

    // Fast path: a single comprehension `[expr for x in SOURCE (if cond)]`
    // whose element count is bounded up front. The result can never have more
    // elements than SOURCE yields (a filter only removes), so pre-allocate
    // that capacity and fill it with the inlined List$insert_value append --
    // no growth reallocation, no repeated GC scans of a growing buffer.
    // SOURCE is hoisted into a temp (evaluated once, since it may not be
    // idempotent, e.g. `xs.reversed()` or `a*b`), and the loop iterates that
    // temp. Bounded sources: a list (capacity = its length) or an integer
    // count `for x in n` (capacity = n; `with_capacity` clamps n <= 0 to
    // empty, and a truncated bignum just means a non-pre-sized fallback, never
    // a wrong result).
    if (list->items && !list->items->next && list->items->ast->tag == Comprehension) {
        DeclareMatch(comp, list->items->ast, Comprehension);
        if (comp->iters && !comp->iters->next && comp->expr->tag != Comprehension) {
            type_t *src_t = value_type(get_type(env, comp->iters->ast));
            Text_t capacity = EMPTY_TEXT;
            const char *src_name = String("comp_src$", this_comp);
            if (src_t->tag == ListType) capacity = Texts(src_name, ".length");
            else if (src_t->tag == BigIntType) capacity = Texts("Int64$from_int(", src_name, ", yes)");
            else if (src_t->tag == IntType) capacity = Texts("(int64_t)(", src_name, ")");
            if (capacity.length > 0) {
                Text_t src_code = compile_to_pointer_depth(env, comp->iters->ast, 0, false);
                Text_t zero = Texts("(", compile_type(item_type), "){0}");
                // Even faster: with no filter and a constant all-bits-zero body
                // (`[Byte(0) for _ in n]`), every slot is zero, so skip the loop
                // entirely and just allocate a zeroed block of `capacity` items.
                if (!comp->filter && is_zero_valued_literal(env, comp->expr, item_type))
                    return Texts("({ ", compile_type(src_t), " ", src_name, " = ", src_code, ";\n",
                                 "List$zeroed(", capacity, ", ", zero, "); })");
                ast_t *src_ref = LiteralCode(Texts(src_name), .type = src_t);
                ast_t *body = add_to_list_comprehension(comp->expr, comprehension_var);
                if (comp->filter) body = WrapAST(comp->expr, If, .condition = comp->filter, .body = body);
                ast_t *loop = WrapAST(list->items->ast, For, .vars = comp->vars, .at = comp->at,
                                      .iters = new (ast_list_t, .ast = src_ref), .body = body);
                return Texts("({ ", compile_type(src_t), " ", src_name, " = ", src_code, "; List_t ",
                             comprehension_name, " = List$with_capacity(", capacity, ", ", zero, ");\n",
                             compile_statement(scope, loop), " ", comprehension_name, "; })");
            }
        }
    }

    Text_t code = Texts("({ List_t ", comprehension_name, " = EMPTY_LIST;");
    // set_binding(scope, comprehension_name, list_type, comprehension_name);
    for (ast_list_t *item = list->items; item; item = item->next) {
        if (item->ast->tag == Comprehension) code = Texts(code, "\n", compile_statement(scope, item->ast));
        else code = Texts(code, compile_statement(scope, add_to_list_comprehension(item->ast, comprehension_var)));
    }
    code = Texts(code, " ", comprehension_name, "; })");
    return code;
}
}

// The generic List runtime calls predicate/comparison closures with the item(s)
// passed BY POINTER (`fn(void *elem, void *userdata)`), because it's compiled
// once and can't know an arbitrary item type's by-value C ABI. To let users
// write natural by-value closures (`func(i:Int) i == 3`), we emit a tiny
// per-item-type adapter whose own signature is that by-pointer one, and which
// dereferences and calls the user's by-value closure. `*(T*)e` is a value copy,
// so the closure can't mutate the list. Deduped per (shape, item type); mirrors
// the closure-shim mechanism in promote() (compile/promotions.c).
static Text_t byval_adapter_name(env_t *env, type_t *item_t, bool is_compare) {
    Text_t item_type_code = compile_type(item_t);
    const char *key = String(is_compare ? "cmp:" : "pred:", Text$as_c_string(item_type_code));
    const char *existing = Table$str_get(env->code->byval_adapters, key);
    if (existing) return Text$from_str(existing);

    Text_t name = Texts("byval$", is_compare ? Text("cmp") : Text("pred"), "$",
                        (int64_t)Table$length(env->code->byval_adapters) + 1);
    Text_t def;
    if (is_compare)
        def = Texts("static int32_t ", name, "(void *a, void *b, void *ud) { Closure_t *u = ud; return ",
                    "((int32_t(*)(", item_type_code, ", ", item_type_code, ", void*))u->fn)(*(", item_type_code,
                    "*)a, *(", item_type_code, "*)b, u->userdata); }\n");
    else
        def = Texts("static bool ", name, "(void *e, void *ud) { Closure_t *u = ud; return ", "((bool(*)(",
                    item_type_code, ", void*))u->fn)(*(", item_type_code, "*)e, u->userdata); }\n");
    env->code->staticdefs = Texts(env->code->staticdefs, def);
    Table$str_set(&env->code->byval_adapters, key, Text$as_c_string(name));
    return name;
}

// Compile a single user-supplied closure argument (matched from `closure_args`
// against a one-arg spec named `arg_name` with the by-value item type) and wrap
// it in a by-value adapter. Sets *decl to `Closure_t <tmp> = <user closure>;`
// and returns `(Closure_t){(void*)<adapter>, &<tmp>}`. The caller must splice
// *decl and the returned expression into a single statement-expression that
// encloses the whole runtime call, so <tmp> outlives the call that reads it (no
// heap allocation). Safe because no List method retains the closure past return.
// Can this closure argument take the shim-free fast path? Only a single,
// directly-passed lambda whose parameters are all explicitly typed as the item
// type (right arity) and whose return type already matches the expected one. In
// that case the lambda's own C body can be compiled with by-pointer arguments
// (see compile_lambda_pointer_args), so it plugs straight into the runtime's
// by-pointer call with no adapter and no boxed closure. Anything else (an
// inferred-type lambda, a named function, a closure variable, a mismatched
// signature) falls back to the adapter path, which also produces the right type
// error when the signature is actually wrong.
static bool byval_lambda_fastpath(env_t *env, arg_ast_t *closure_args, type_t *item_t, bool is_compare,
                                  const char *arg_name) {
    if (!closure_args || closure_args->next) return false;
    if (closure_args->name && !streq(closure_args->name, arg_name)) return false;
    ast_t *lambda_ast = closure_args->value;
    if (!lambda_ast || lambda_ast->tag != Lambda) return false;
    DeclareMatch(lambda, lambda_ast, Lambda);
    int arity = 0;
    for (arg_ast_t *a = lambda->args; a; a = a->next) {
        if (!a->type) return false; // inferred param type: let the adapter path fill it in
        if (!type_eq(parse_type_ast(env, a->type), item_t)) return false;
        arity += 1;
    }
    if (arity != (is_compare ? 2 : 1)) return false;
    type_t *ret = get_function_return_type(env, lambda_ast);
    return type_eq(ret, is_compare ? Type(IntType, .bits = TYPE_IBITS32) : Type(BoolType));
}

static Text_t compile_byval_closure(env_t *env, ast_t *ast, type_t *item_t, bool is_compare, const char *arg_name,
                                    arg_ast_t *closure_args, Text_t *decl) {
    if (byval_lambda_fastpath(env, closure_args, item_t, is_compare, arg_name)) {
        *decl = EMPTY_TEXT;
        return compile_lambda_pointer_args(env, closure_args->value);
    }
    type_t *fn_t = is_compare ? NewFunctionType(Type(IntType, .bits = TYPE_IBITS32), {.name = "x", .type = item_t},
                                                {.name = "y", .type = item_t})
                              : NewFunctionType(Type(BoolType), {.name = "item", .type = item_t});
    arg_t *arg_spec = new (arg_t, .name = arg_name, .type = Type(ClosureType, .fn = fn_t));
    Text_t user_closure = compile_arguments(env, ast, arg_spec, closure_args);
    Text_t tmp = Texts("_byval$", (int64_t)(ast->start - ast->file->text));
    *decl = Texts("Closure_t ", tmp, " = ", user_closure, "; ");
    // Extra parens: heap_push_value/heap_pop_value are function-like macros, and
    // the braced Closure literal contains a comma that would otherwise be split.
    return Texts("((Closure_t){(void*)", byval_adapter_name(env, item_t, is_compare), ", &", tmp, "})");
}

public
Text_t compile_list_method_call(env_t *env, ast_t *ast) {
    DeclareMatch(call, ast, MethodCall);
    type_t *self_t = get_type(env, call->self);

    int64_t pointer_depth = 0;
    type_t *self_value_t = self_t;
    for (; self_value_t->tag == PointerType; self_value_t = Match(self_value_t, PointerType)->pointed)
        pointer_depth += 1;

    Text_t self = compile(env, call->self);
#define EXPECT_POINTER()                                                                                               \
    do {                                                                                                               \
        if (pointer_depth < 1) code_err(call->self, "I expected a list pointer here, not a list value");               \
        else if (pointer_depth > 1) code_err(call->self, "I expected a list pointer here, not a nested list pointer"); \
    } while (0)
    type_t *item_t = Match(self_value_t, ListType)->item_type;
    Text_t padded_item_size = Texts("sizeof(", compile_type(item_t), ")");

    if (streq(call->name, "insert")) {
        EXPECT_POINTER();
        arg_t *arg_spec =
            new (arg_t, .name = "item", .type = item_t,
                 .next = new (arg_t, .name = "at", .type = INT_TYPE, .default_val = FakeAST(Int, .i = I_small(0))));
        return Texts("List$insert_value(", self, ", ", compile_arguments(env, ast, arg_spec, call->args), ", ",
                     padded_item_size, ")");
    } else if (streq(call->name, "insert_all")) {
        EXPECT_POINTER();
        arg_t *arg_spec =
            new (arg_t, .name = "items", .type = self_value_t,
                 .next = new (arg_t, .name = "at", .type = INT_TYPE, .default_val = FakeAST(Int, .i = I_small(0))));
        return Texts("List$insert_all(", self, ", ", compile_arguments(env, ast, arg_spec, call->args), ", ",
                     padded_item_size, ")");
    } else if (streq(call->name, "remove_at")) {
        EXPECT_POINTER();
        arg_t *arg_spec =
            new (arg_t, .name = "index", .type = INT_TYPE, .default_val = FakeAST(Int, .i = I_small(-1)),
                 .next = new (arg_t, .name = "count", .type = INT_TYPE, .default_val = FakeAST(Int, .i = I_small(1))));
        return Texts("List$remove_at(", self, ", ", compile_arguments(env, ast, arg_spec, call->args), ", ",
                     padded_item_size, ")");
    } else if (streq(call->name, "remove_item")) {
        EXPECT_POINTER();
        arg_t *arg_spec =
            new (arg_t, .name = "item", .type = item_t,
                 .next = new (arg_t, .name = "max_count", .type = INT_TYPE, .default_val = FakeAST(Int, .i = I_small(-1))));
        return Texts("List$remove_item_value(", self, ", ", compile_arguments(env, ast, arg_spec, call->args), ", ",
                     compile_type_info(self_value_t), ")");
    } else if (streq(call->name, "has")) {
        self = compile_to_pointer_depth(env, call->self, 0, false);
        arg_t *arg_spec = new (arg_t, .name = "item", .type = item_t);
        return Texts("List$has_value(", self, ", ", compile_arguments(env, ast, arg_spec, call->args), ", ",
                     compile_type_info(self_value_t), ")");
    } else if (streq(call->name, "sample")) {
        type_t *random_num_type = parse_type_string(env, "func(->Num)?");
        self = compile_to_pointer_depth(env, call->self, 0, false);
        arg_t *arg_spec =
            new (arg_t, .name = "count", .type = INT_TYPE,
                 .next = new (
                     arg_t, .name = "weights", .type = Type(ListType, .item_type = Type(NumType)),
                     .default_val = FakeAST(None),
                     .next = new (arg_t, .name = "random", .type = random_num_type, .default_val = FakeAST(None))));
        return Texts("List$sample(", self, ", ", compile_arguments(env, ast, arg_spec, call->args), ", ",
                     padded_item_size, ")");
    } else if (streq(call->name, "shuffle")) {
        type_t *random_int64_type = parse_type_string(env, "func(min,max:Int64->Int64)?");
        EXPECT_POINTER();
        arg_t *arg_spec = new (arg_t, .name = "random", .type = random_int64_type, .default_val = FakeAST(None));
        return Texts("List$shuffle(", self, ", ", compile_arguments(env, ast, arg_spec, call->args), ", ",
                     padded_item_size, ")");
    } else if (streq(call->name, "shuffled")) {
        type_t *random_int64_type = parse_type_string(env, "func(min,max:Int64->Int64)?");
        self = compile_to_pointer_depth(env, call->self, 0, false);
        arg_t *arg_spec = new (arg_t, .name = "random", .type = random_int64_type, .default_val = FakeAST(None));
        return Texts("List$shuffled(", self, ", ", compile_arguments(env, ast, arg_spec, call->args), ", ",
                     padded_item_size, ")");
    } else if (streq(call->name, "random")) {
        type_t *random_int64_type = parse_type_string(env, "func(min,max:Int64->Int64)?");
        self = compile_to_pointer_depth(env, call->self, 0, false);
        arg_t *arg_spec = new (arg_t, .name = "random", .type = random_int64_type, .default_val = FakeAST(None));
        return Texts("List$random_value(", self, ", ", compile_arguments(env, ast, arg_spec, call->args), ", ",
                     compile_type(item_t), ", _, ", promote_to_optional(item_t, Text("_")), ", ", compile_none(item_t),
                     ")");
    } else if (streq(call->name, "swap")) {
        // Compiled inline (List_swap in stdlib/lists.h): two bounds checks
        // and one CoW guard per swap, no function call. Indexes compile to
        // native Int64 exactly like indexed assignment does.
        EXPECT_POINTER();
        arg_ast_t *i_arg = call->args;
        if (i_arg == NULL || i_arg->next == NULL || i_arg->next->next != NULL)
            code_err(ast, "swap() takes exactly two index arguments");
        if ((i_arg->name && !streq(i_arg->name, "i")) || (i_arg->next->name && !streq(i_arg->next->name, "j")))
            code_err(ast, "swap()'s arguments are named `i` and `j`");
        Text_t index_codes[2];
        arg_ast_t *arg = i_arg;
        for (int n = 0; n < 2; n++, arg = arg->next) {
            type_t *arg_t = get_type(env, arg->value);
            if (is_int_literal(arg->value, NULL))
                index_codes[n] = compile_int_to_type(env, arg->value, Type(IntType, .bits = TYPE_IBITS64));
            else if (arg_t->tag == BigIntType) index_codes[n] = Texts("Int64$from_int(", compile(env, arg->value), ", no)");
            else if (is_int_type(arg_t)) index_codes[n] = Texts("(Int64_t)(", compile(env, arg->value), ")");
            else code_err(arg->value, "swap() indexes must be integers, not ", type_to_text(arg_t));
        }
        // If an enclosing loop hoisted this list's header and CoW guard (see
        // cow_hoist_env in loops.c), swap through the hoisted locals: no
        // per-swap CoW check, and the header stays in registers.
        if (is_cow_hoisted(env, call->self))
            return Texts("List_swap_hoisted(", compile_type(item_t), ", ", cow_hoisted_local(call->self, "data"), ", ",
                         cow_hoisted_local(call->self, "stride"), ", ", cow_hoisted_local(call->self, "length"), ", ",
                         index_codes[0], ", ", index_codes[1], ", ", (int64_t)(ast->start - ast->file->text), ", ",
                         (int64_t)(ast->end - ast->file->text), ")");
        return Texts("List_swap(", compile_type(item_t), ", ", self, ", ", index_codes[0], ", ", index_codes[1], ", ",
                     (int64_t)(ast->start - ast->file->text), ", ", (int64_t)(ast->end - ast->file->text), ")");
    } else if (streq(call->name, "sort") || streq(call->name, "sorted")) {
        if (streq(call->name, "sort")) EXPECT_POINTER();
        else self = compile_to_pointer_depth(env, call->self, 0, false);
        if (call->args) {
            Text_t decl, cmp = compile_byval_closure(env, ast, item_t, true, "by", call->args, &decl);
            return Texts("({ ", decl, "List$", call->name, "(", self, ", ", cmp, ", ", padded_item_size, "); })");
        }
        Text_t comparison =
            Texts("((Closure_t){.fn=generic_compare, .userdata=(void*)", compile_type_info(item_t), "})");
        return Texts("List$", call->name, "(", self, ", ", comparison, ", ", padded_item_size, ")");
    } else if (streq(call->name, "heapify")) {
        EXPECT_POINTER();
        if (call->args) {
            Text_t decl, cmp = compile_byval_closure(env, ast, item_t, true, "by", call->args, &decl);
            return Texts("({ ", decl, "List$heapify(", self, ", ", cmp, ", ", padded_item_size, "); })");
        }
        Text_t comparison =
            Texts("((Closure_t){.fn=generic_compare, .userdata=(void*)", compile_type_info(item_t), "})");
        return Texts("List$heapify(", self, ", ", comparison, ", ", padded_item_size, ")");
    } else if (streq(call->name, "heap_push")) {
        EXPECT_POINTER();
        // Split the (required) `item` value from the optional `by` comparison so
        // the comparison can be adapted to by-value independently. Matching
        // mirrors compile_arguments: a keyword arg claims its slot; the rest fill
        // positionally in order.
        arg_ast_t *item_arg = NULL, *by_arg = NULL, *positional[2] = {NULL, NULL};
        int np = 0;
        for (arg_ast_t *a = call->args; a; a = a->next) {
            if (a->name) {
                if (streq(a->name, "item")) item_arg = a;
                else if (streq(a->name, "by")) by_arg = a;
                else code_err(a->value, "heap_push() has no argument named '", a->name, "'");
            } else if (np < 2) {
                positional[np++] = a;
            } else {
                code_err(a->value, "heap_push() takes at most two arguments");
            }
        }
        int p = 0;
        if (!item_arg) item_arg = positional[p++];
        if (!by_arg && p < np) by_arg = positional[p++];
        if (!item_arg) code_err(ast, "heap_push() requires an item to push");
        if (p < np) code_err(positional[p]->value, "This positional argument to heap_push() has no slot to fill "
                                                   "(item and comparison are already provided)");

        arg_t *item_spec = new (arg_t, .name = "item", .type = item_t);
        Text_t item_code = compile_arguments(env, ast, item_spec, new (arg_ast_t, .value = item_arg->value));
        if (by_arg) {
            Text_t decl, cmp = compile_byval_closure(env, ast, item_t, true, "by", new (arg_ast_t, .value = by_arg->value),
                                                     &decl);
            return Texts("({ ", decl, "List$heap_push_value(", self, ", ", item_code, ", ", cmp, ", ", padded_item_size,
                         "); })");
        }
        Text_t default_cmp =
            Texts("((Closure_t){.fn=generic_compare, .userdata=(void*)", compile_type_info(item_t), "})");
        return Texts("List$heap_push_value(", self, ", ", item_code, ", ", default_cmp, ", ", padded_item_size, ")");
    } else if (streq(call->name, "heap_pop")) {
        EXPECT_POINTER();
        if (call->args) {
            Text_t decl, cmp = compile_byval_closure(env, ast, item_t, true, "by", call->args, &decl);
            return Texts("({ ", decl, "List$heap_pop_value(", self, ", ", cmp, ", ", compile_type(item_t), ", _, ",
                         promote_to_optional(item_t, Text("_")), ", ", compile_none(item_t), "); })");
        }
        Text_t cmp = Texts("((Closure_t){.fn=generic_compare, .userdata=(void*)", compile_type_info(item_t), "})");
        return Texts("List$heap_pop_value(", self, ", ", cmp, ", ", compile_type(item_t), ", _, ",
                     promote_to_optional(item_t, Text("_")), ", ", compile_none(item_t), ")");
    } else if (streq(call->name, "binary_search")) {
        self = compile_to_pointer_depth(env, call->self, 0, call->args != NULL);
        Text_t decl, pred = compile_byval_closure(env, ast, item_t, false, "predicate", call->args, &decl);
        return Texts("({ ", decl, "List$binary_search(", self, ", ", pred, "); })");
    } else if (streq(call->name, "clear")) {
        EXPECT_POINTER();
        (void)compile_arguments(env, ast, NULL, call->args);
        return Texts("List$clear(", self, ")");
    } else if (streq(call->name, "find")) {
        self = compile_to_pointer_depth(env, call->self, 0, false);
        arg_t *arg_spec = new (arg_t, .name = "item", .type = item_t);
        return Texts("List$find_value(", self, ", ", compile_arguments(env, ast, arg_spec, call->args), ", ",
                     compile_type_info(self_value_t), ")");
    } else if (streq(call->name, "where")) {
        self = compile_to_pointer_depth(env, call->self, 0, call->args != NULL);
        Text_t decl, pred = compile_byval_closure(env, ast, item_t, false, "predicate", call->args, &decl);
        return Texts("({ ", decl, "List$first(", self, ", ", pred, "); })");
    } else if (streq(call->name, "from")) {
        self = compile_to_pointer_depth(env, call->self, 0, true);
        arg_t *arg_spec = new (arg_t, .name = "first", .type = INT_TYPE);
        return Texts("List$from(", self, ", ", compile_arguments(env, ast, arg_spec, call->args), ")");
    } else if (streq(call->name, "to")) {
        self = compile_to_pointer_depth(env, call->self, 0, true);
        arg_t *arg_spec = new (arg_t, .name = "last", .type = INT_TYPE);
        return Texts("List$to(", self, ", ", compile_arguments(env, ast, arg_spec, call->args), ")");
    } else if (streq(call->name, "slice")) {
        self = compile_to_pointer_depth(env, call->self, 0, true);
        arg_t *arg_spec =
            new (arg_t, .name = "first", .type = INT_TYPE, .next = new (arg_t, .name = "last", .type = INT_TYPE));
        return Texts("List$slice(", self, ", ", compile_arguments(env, ast, arg_spec, call->args), ")");
    } else if (streq(call->name, "by")) {
        self = compile_to_pointer_depth(env, call->self, 0, true);
        arg_t *arg_spec = new (arg_t, .name = "stride", .type = INT_TYPE);
        return Texts("List$by(", self, ", ", compile_arguments(env, ast, arg_spec, call->args), ", ", padded_item_size,
                     ")");
    } else if (streq(call->name, "reversed")) {
        self = compile_to_pointer_depth(env, call->self, 0, true);
        (void)compile_arguments(env, ast, NULL, call->args);
        return Texts("List$reversed(", self, ", ", padded_item_size, ")");
    } else if (streq(call->name, "pairs")) {
        // Iterator over each unordered pair of distinct elements (i < j). The
        // incref gives it snapshot semantics: mutating the list after making
        // the iterator copies first, like other buffer-sharing methods.
        self = compile_to_pointer_depth(env, call->self, 0, true);
        (void)compile_arguments(env, ast, NULL, call->args);
        return Texts("List$pairs(", self, ", ", padded_item_size, ")");
    } else if (streq(call->name, "unique")) {
        self = compile_to_pointer_depth(env, call->self, 0, false);
        (void)compile_arguments(env, ast, NULL, call->args);
        return Texts("Table$from_entries(", self, ", Table$info(", compile_type_info(item_t), ", &Present$$info))");
    } else if (streq(call->name, "pop")) {
        EXPECT_POINTER();
        arg_t *arg_spec = new (arg_t, .name = "index", .type = INT_TYPE, .default_val = FakeAST(Int, I_small(-1)));
        Text_t index = compile_arguments(env, ast, arg_spec, call->args);
        return Texts("List$pop(", self, ", ", index, ", ", compile_type(item_t), ", _, ",
                     promote_to_optional(item_t, Text("_")), ", ", compile_none(item_t), ")");
    } else if (streq(call->name, "counts")) {
        self = compile_to_pointer_depth(env, call->self, 0, false);
        (void)compile_arguments(env, ast, NULL, call->args);
        return Texts("List$counts(", self, ", ", compile_type_info(self_value_t), ")");
    } else {
        code_err(ast, "There is no '", call->name, "' method for lists");
    }
}
