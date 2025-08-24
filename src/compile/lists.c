// Compilation logic for lists

#include <gc.h>
#include <glob.h>
#include <gmp.h>
#include <uninorm.h>

#include "../ast.h"
#include "../compile.h"
#include "../config.h"
#include "../environment.h"
#include "../stdlib/text.h"
#include "../stdlib/util.h"
#include "../typecheck.h"
#include "functions.h"
#include "optionals.h"
#include "pointers.h"
#include "promotions.h"
#include "statements.h"
#include "types.h"

static ast_t *add_to_list_comprehension(ast_t *item, ast_t *subject) {
    return WrapAST(item, MethodCall, .name = "insert", .self = subject, .args = new (arg_ast_t, .value = item));
}

public
Text_t compile_typed_list(env_t *env, ast_t *ast, type_t *list_type) {
    DeclareMatch(list, ast, List);
    if (!list->items) return Text("(List_t){.length=0}");

    type_t *item_type = Match(list_type, ListType)->item_type;

    int64_t n = 0;
    for (ast_list_t *item = list->items; item; item = item->next) {
        ++n;
        if (item->ast->tag == Comprehension) goto list_comprehension;
    }

    {
        env_t *scope = item_type->tag == EnumType ? with_enum_scope(env, item_type) : env;
        if (is_incomplete_type(item_type)) code_err(ast, "This list's type can't be inferred!");
        Text_t code = Texts("TypedListN(", compile_type(item_type), ", ", String(n));
        for (ast_list_t *item = list->items; item; item = item->next) {
            code = Texts(code, ", ", compile_to_type(scope, item->ast, item_type));
        }
        return Texts(code, ")");
    }

list_comprehension: {
    env_t *scope = item_type->tag == EnumType ? with_enum_scope(env, item_type) : fresh_scope(env);
    static int64_t comp_num = 1;
    const char *comprehension_name = String("list$", comp_num++);
    ast_t *comprehension_var =
        LiteralCode(Texts("&", comprehension_name), .type = Type(PointerType, .pointed = list_type, .is_stack = true));
    Closure_t comp_action = {.fn = add_to_list_comprehension, .userdata = comprehension_var};
    scope->comprehension_action = &comp_action;
    Text_t code = Texts("({ List_t ", comprehension_name, " = {};");
    // set_binding(scope, comprehension_name, list_type, comprehension_name);
    for (ast_list_t *item = list->items; item; item = item->next) {
        if (item->ast->tag == Comprehension) code = Texts(code, "\n", compile_statement(scope, item->ast));
        else code = Texts(code, compile_statement(env, add_to_list_comprehension(item->ast, comprehension_var)));
    }
    code = Texts(code, " ", comprehension_name, "; })");
    return code;
}
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
                 .next = new (arg_t, .name = "at", .type = INT_TYPE, .default_val = FakeAST(Int, .str = "0")));
        return Texts("List$insert_value(", self, ", ", compile_arguments(env, ast, arg_spec, call->args), ", ",
                     padded_item_size, ")");
    } else if (streq(call->name, "insert_all")) {
        EXPECT_POINTER();
        arg_t *arg_spec =
            new (arg_t, .name = "items", .type = self_value_t,
                 .next = new (arg_t, .name = "at", .type = INT_TYPE, .default_val = FakeAST(Int, .str = "0")));
        return Texts("List$insert_all(", self, ", ", compile_arguments(env, ast, arg_spec, call->args), ", ",
                     padded_item_size, ")");
    } else if (streq(call->name, "remove_at")) {
        EXPECT_POINTER();
        arg_t *arg_spec =
            new (arg_t, .name = "index", .type = INT_TYPE, .default_val = FakeAST(Int, .str = "-1"),
                 .next = new (arg_t, .name = "count", .type = INT_TYPE, .default_val = FakeAST(Int, .str = "1")));
        return Texts("List$remove_at(", self, ", ", compile_arguments(env, ast, arg_spec, call->args), ", ",
                     padded_item_size, ")");
    } else if (streq(call->name, "remove_item")) {
        EXPECT_POINTER();
        arg_t *arg_spec =
            new (arg_t, .name = "item", .type = item_t,
                 .next = new (arg_t, .name = "max_count", .type = INT_TYPE, .default_val = FakeAST(Int, .str = "-1")));
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
                     arg_t, .name = "weights", .type = Type(ListType, .item_type = Type(NumType, .bits = TYPE_NBITS64)),
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
                     compile_type(item_t), ")");
    } else if (streq(call->name, "sort") || streq(call->name, "sorted")) {
        if (streq(call->name, "sort")) EXPECT_POINTER();
        else self = compile_to_pointer_depth(env, call->self, 0, false);
        Text_t comparison;
        if (call->args) {
            type_t *item_ptr = Type(PointerType, .pointed = item_t, .is_stack = true);
            type_t *fn_t = NewFunctionType(Type(IntType, .bits = TYPE_IBITS32), {.name = "x", .type = item_ptr},
                                           {.name = "y", .type = item_ptr});
            arg_t *arg_spec = new (arg_t, .name = "by", .type = Type(ClosureType, .fn = fn_t));
            comparison = compile_arguments(env, ast, arg_spec, call->args);
        } else {
            comparison = Texts("((Closure_t){.fn=generic_compare, "
                               ".userdata=(void*)",
                               compile_type_info(item_t), "})");
        }
        return Texts("List$", call->name, "(", self, ", ", comparison, ", ", padded_item_size, ")");
    } else if (streq(call->name, "heapify")) {
        EXPECT_POINTER();
        Text_t comparison;
        if (call->args) {
            type_t *item_ptr = Type(PointerType, .pointed = item_t, .is_stack = true);
            type_t *fn_t = NewFunctionType(Type(IntType, .bits = TYPE_IBITS32), {.name = "x", .type = item_ptr},
                                           {.name = "y", .type = item_ptr});
            arg_t *arg_spec = new (arg_t, .name = "by", .type = Type(ClosureType, .fn = fn_t));
            comparison = compile_arguments(env, ast, arg_spec, call->args);
        } else {
            comparison = Texts("((Closure_t){.fn=generic_compare, "
                               ".userdata=(void*)",
                               compile_type_info(item_t), "})");
        }
        return Texts("List$heapify(", self, ", ", comparison, ", ", padded_item_size, ")");
    } else if (streq(call->name, "heap_push")) {
        EXPECT_POINTER();
        type_t *item_ptr = Type(PointerType, .pointed = item_t, .is_stack = true);
        type_t *fn_t = NewFunctionType(Type(IntType, .bits = TYPE_IBITS32), {.name = "x", .type = item_ptr},
                                       {.name = "y", .type = item_ptr});
        ast_t *default_cmp = LiteralCode(Texts("((Closure_t){.fn=generic_compare, "
                                               ".userdata=(void*)",
                                               compile_type_info(item_t), "})"),
                                         .type = Type(ClosureType, .fn = fn_t));
        arg_t *arg_spec =
            new (arg_t, .name = "item", .type = item_t,
                 .next = new (arg_t, .name = "by", .type = Type(ClosureType, .fn = fn_t), .default_val = default_cmp));
        Text_t arg_code = compile_arguments(env, ast, arg_spec, call->args);
        return Texts("List$heap_push_value(", self, ", ", arg_code, ", ", padded_item_size, ")");
    } else if (streq(call->name, "heap_pop")) {
        EXPECT_POINTER();
        type_t *item_ptr = Type(PointerType, .pointed = item_t, .is_stack = true);
        type_t *fn_t = NewFunctionType(Type(IntType, .bits = TYPE_IBITS32), {.name = "x", .type = item_ptr},
                                       {.name = "y", .type = item_ptr});
        ast_t *default_cmp = LiteralCode(Texts("((Closure_t){.fn=generic_compare, "
                                               ".userdata=(void*)",
                                               compile_type_info(item_t), "})"),
                                         .type = Type(ClosureType, .fn = fn_t));
        arg_t *arg_spec = new (arg_t, .name = "by", .type = Type(ClosureType, .fn = fn_t), .default_val = default_cmp);
        Text_t arg_code = compile_arguments(env, ast, arg_spec, call->args);
        return Texts("List$heap_pop_value(", self, ", ", arg_code, ", ", compile_type(item_t), ", _, ",
                     promote_to_optional(item_t, Text("_")), ", ", compile_none(item_t), ")");
    } else if (streq(call->name, "binary_search")) {
        self = compile_to_pointer_depth(env, call->self, 0, call->args != NULL);
        type_t *item_ptr = Type(PointerType, .pointed = item_t, .is_stack = true);
        type_t *fn_t = NewFunctionType(Type(IntType, .bits = TYPE_IBITS32), {.name = "x", .type = item_ptr},
                                       {.name = "y", .type = item_ptr});
        ast_t *default_cmp = LiteralCode(Texts("((Closure_t){.fn=generic_compare, "
                                               ".userdata=(void*)",
                                               compile_type_info(item_t), "})"),
                                         .type = Type(ClosureType, .fn = fn_t));
        arg_t *arg_spec =
            new (arg_t, .name = "target", .type = item_t,
                 .next = new (arg_t, .name = "by", .type = Type(ClosureType, .fn = fn_t), .default_val = default_cmp));
        Text_t arg_code = compile_arguments(env, ast, arg_spec, call->args);
        return Texts("List$binary_search_value(", self, ", ", arg_code, ")");
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
        type_t *item_ptr = Type(PointerType, .pointed = item_t, .is_stack = true);
        type_t *predicate_type =
            Type(ClosureType, .fn = NewFunctionType(Type(BoolType), {.name = "item", .type = item_ptr}));
        arg_t *arg_spec = new (arg_t, .name = "predicate", .type = predicate_type);
        return Texts("List$first(", self, ", ", compile_arguments(env, ast, arg_spec, call->args), ")");
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
    } else if (streq(call->name, "unique")) {
        self = compile_to_pointer_depth(env, call->self, 0, false);
        (void)compile_arguments(env, ast, NULL, call->args);
        return Texts("Table$from_entries(", self, ", Set$info(", compile_type_info(item_t), "))");
    } else if (streq(call->name, "pop")) {
        EXPECT_POINTER();
        arg_t *arg_spec = new (arg_t, .name = "index", .type = INT_TYPE, .default_val = FakeAST(Int, "-1"));
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

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
