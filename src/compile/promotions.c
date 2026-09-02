// This file defines how to do type promotions during compilation

#include "../ast.h"
#include "../environment.h"
#include "../stdlib/datatypes.h"
#include "../stdlib/table.h"
#include "../stdlib/text.h"
#include "../typecheck.h"
#include "../types.h"
#include "compilation.h"

static Text_t quoted_str(const char *str) {
    return Text$quoted(Text$from_str(str), false, Text("\""));
}

public
bool promote(env_t *env, ast_t *ast, Text_t *code, type_t *actual, type_t *needed) {
    if (type_eq(actual, needed)) return true;

    if (!can_promote(actual, needed)) return false;

    if (needed->tag == ClosureType && actual->tag == FunctionType) {
        // Closure calls go through a function pointer with a trailing
        // `void *userdata` argument. Calling the promoted function through
        // that type directly would be undefined behavior (mismatched
        // function-pointer types), so promotion goes through a shim (one per
        // function type) whose signature matches the closure calling
        // convention exactly; the target function pointer rides in the
        // userdata slot and is called through its own exact type:
        DeclareMatch(fn, actual, FunctionType);
        Text_t fn_type_code = compile_type(actual);
        const char *type_key = Text$as_c_string(fn_type_code);
        const char *existing = Table$str_get(env->code->closure_shims, type_key);
        Text_t shim_name;
        if (existing) {
            shim_name = Text$from_str(existing);
        } else {
            shim_name = Texts("closure$shim$", (int64_t)Table$length(env->code->closure_shims) + 1);
            Text_t params = EMPTY_TEXT, args = EMPTY_TEXT;
            int64_t i = 1;
            for (arg_t *arg = fn->args; arg; arg = arg->next, i++) {
                params = Texts(params, compile_type(arg->type), " a", i, ", ");
                args = i == 1 ? Texts("a", i) : Texts(args, ", a", i);
            }
            Text_t call = Texts("((", fn_type_code, ")fn)(", args, ")");
            bool no_ret = fn->ret->tag == VoidType || fn->ret->tag == AbortType;
            env->code->staticdefs = Texts(env->code->staticdefs, "static ", compile_type(fn->ret), " ", shim_name, "(",
                                          params, "void *fn) { ", no_ret ? EMPTY_TEXT : Text("return "), call, "; }\n");
            Table$str_set(&env->code->closure_shims, type_key, Text$as_c_string(shim_name));
        }
        *code = Texts("((Closure_t){(void*)", shim_name, ", (void*)", *code, "})");
        return true;
    }

    // Empty promotion:
    type_t *more_complete = most_complete_type(actual, needed);
    if (more_complete) return true;

    // Optional promotion:
    if (needed->tag == OptionalType && type_eq(actual, Match(needed, OptionalType)->type)) {
        *code = promote_to_optional(actual, *code);
        return true;
    }

    // Optional -> Bool promotion
    // Note: we explicitly disallow `Bool?` -> `Bool` promotion, because that's a footgun
    if (actual->tag == OptionalType && needed->tag == BoolType && Match(actual, OptionalType)->type->tag != BoolType) {
        *code = Texts("(!", check_none(actual, *code), ")");
        return true;
    }

    // Lang to Text_t:
    if (actual->tag == TextType && needed->tag == TextType && streq(Match(needed, TextType)->lang, "Text")) return true;

    // Automatic optional checking for nums:
    if (needed->tag == FloatType && actual->tag == OptionalType
        && Match(actual, OptionalType)->type->tag == FloatType) {
        int64_t line = get_line_number(ast->file, ast->start);
        *code = Texts("({ ", compile_declaration(actual, Text("opt")), " = ", *code, "; ", "if unlikely (",
                      check_none(actual, Text("opt")), ")\n", "#line ", line, "\n", "fail_source(",
                      quoted_str(ast->file->filename), ", ", (int64_t)(ast->start - ast->file->text), ", ",
                      (int64_t)(ast->end - ast->file->text), ", ",
                      "Text(\"This was expected to be a value, but it's none\"));\n",
                      optional_into_nonnone(actual, Text("opt")), "; })");
        return true;
    }

    // Numeric promotions/demotions
    if ((is_numeric_type(actual) || actual->tag == BoolType) && (is_numeric_type(needed) || needed->tag == BoolType)) {
        arg_ast_t *args = new (arg_ast_t, .value = LiteralCode(*code, .type = actual));
        binding_t *constructor = get_constructor(env, needed, args);
        if (constructor) {
            DeclareMatch(fn, constructor->type, FunctionType);
            if (fn->args->next == NULL) {
                *code = Texts(constructor->code, "(", compile_arguments(env, ast, fn->args, args), ")");
                return true;
            }
        }
    }

    if (needed->tag == EnumType) {
        const char *tag = enum_single_value_tag(needed, actual);
        binding_t *b = get_binding(Match(needed, EnumType)->env, tag);
        assert(b && b->type->tag == FunctionType);
        // Single-value enum constructor:
        if (!promote(env, ast, code, actual, Match(b->type, FunctionType)->args->type)) return false;
        *code = Texts(b->code, "(", *code, ")");
        return true;
    }

    // Text_t to C String
    if (actual->tag == TextType && type_eq(actual, TEXT_TYPE) && needed->tag == CStringType) {
        *code = Texts("Text$as_c_string(", *code, ")");
        return true;
    }

    // C String to Text
    if (actual->tag == CStringType && type_eq(needed, TEXT_TYPE)) {
        *code = Texts("Text$from_str(", *code, ")");
        return true;
    }

    // Automatic dereferencing:
    if (actual->tag == PointerType && can_promote(Match(actual, PointerType)->pointed, needed)) {
        *code = Texts("*(", *code, ")");
        return promote(env, ast, code, Match(actual, PointerType)->pointed, needed);
    }

    // Stack ref promotion:
    if (actual->tag == PointerType && needed->tag == PointerType) return true;

    // Cross-promotion between tables with default values and without
    if (needed->tag == TableType && actual->tag == TableType) return true;

    if (needed->tag == ClosureType && actual->tag == ClosureType) return true;

    if (needed->tag == FunctionType && actual->tag == FunctionType) {
        *code = Texts("(", compile_type(needed), ")", *code);
        return true;
    }

    return false;
}

public
Text_t compile_to_type(env_t *env, ast_t *ast, type_t *t) {
    if (is_incomplete_type(t)) code_err(ast, "I can't determine the complete type of this value.");

    if (t->tag == EnumType) {
        env = with_enum_scope(env, t);
    }

    if (ast->tag == Block && Match(ast, Block)->statements && !Match(ast, Block)->statements->next) {
        ast = Match(ast, Block)->statements->ast;
    }

    if (is_int_literal(ast, NULL) && is_numeric_type(non_optional(t))) {
        return compile_int_to_type(env, ast, t);
    } else if (ast->tag == Num && is_numeric_type(non_optional(t))) {
        return compile_num_to_type(env, ast, t);
    } else if (ast->tag == None) {
        if (t->tag != OptionalType) code_err(ast, "This is not supposed to be an optional type");
        else if (Match(t, OptionalType)->type == NULL)
            code_err(ast, "I don't know what kind of `none` this is supposed to be!\n"
                          "Please tell me by declaring a variable like `foo : Type = none`");
        return compile_none(t);
    } else if (t->tag == PointerType && (ast->tag == HeapAllocate || ast->tag == StackReference)) {
        return compile_typed_allocation(env, ast, t);
    } else if (t->tag == ListType && ast->tag == List) {
        return compile_typed_list(env, ast, t);
    } else if (t->tag == TableType && ast->tag == Table) {
        return compile_typed_table(env, ast, t);
    } else if (ast->tag == Embed && t->tag == TextType) {
        return compile_embed_as_text(ast, get_embed_bytes(ast));
    } else if (ast->tag == Embed && t->tag == CStringType) {
        return compile_embed_as_cstring(ast, get_embed_bytes(ast));
    } else if (is_pushdown_arithmetic(ast, non_optional(t)) && get_type(env, ast)->tag == BigIntType) {
        // Untyped-int-literal arithmetic (`1 + 2`, always inferred as bignum
        // `Int`) flowing into a fixed-width numeric type: if both operands can
        // themselves compile to that target, do the arithmetic natively in it
        // (e.g. `(I64(1) + I64(2))`) instead of computing a bignum and
        // converting. This is faster and, for the ops with a native C form, a
        // compile-time constant (see is_constant).
        type_t *target = non_optional(t);
        if (is_numeric_type(target) && target->tag != BigIntType) {
            binary_operands_t binop = BINARY_OPERANDS(ast);
            if (can_compile_to_type(env, binop.lhs, target) && can_compile_to_type(env, binop.rhs, target)) {
                Text_t code = compile_binary_op_to_type(env, ast, target);
                return t->tag == OptionalType ? promote_to_optional(target, code) : code;
            }
        }
    }

    type_t *actual = get_type(env, ast);

    // Edge case: there are some situations where a method call needs to have
    // the `self` value get compiled to a specific type that can't be fully
    // inferred from the expression itself. We can infer what the specific type
    // should be from what we know the specific type of the return value is,
    // but it requires a bit of special logic.
    // For example:
    //    x : [Int?] = [none].sorted()
    // Here, we know that `[none]` is `[Int?]`, but we need to thread that
    // information through the compiler using an `ExplicitlyTyped` node.
    if (ast->tag == MethodCall) {
        DeclareMatch(methodcall, ast, MethodCall);
        type_t *self_type = get_type(env, methodcall->self);
        // Currently, this is only implemented for cases where you have the
        // return type and the self type equal to each other, because that's the
        // main case I care about with list and set methods (e.g.
        // `List.sorted()`)
        if (is_incomplete_type(self_type) && type_eq(self_type, actual)) {
            type_t *completed_self = most_complete_type(self_type, t);
            if (completed_self) {
                ast_t *explicit_self =
                    WrapAST(methodcall->self, ExplicitlyTyped, .ast = methodcall->self, .type = completed_self);
                ast_t *new_methodcall =
                    WrapAST(ast, MethodCall, .self = explicit_self, .name = methodcall->name, .args = methodcall->args);
                return compile_to_type(env, new_methodcall, t);
            }
        }
    }

    // Promote values to views-of-values if needed:
    if (t->tag == PointerType && Match(t, PointerType)->is_stack && actual->tag != PointerType) {
        if (is_bit_packed_field(env, ast))
            code_err(ast, "This is a ", type_to_text(actual),
                     " field of a `packed_bools` struct, so it lives in a bit or two inside a byte and has no "
                     "address of its own, but a ",
                     type_to_text(t), " is needed here.");
        if (type_eq(actual, Match(t, PointerType)->pointed) && can_be_mutated(env, ast))
            return Texts("&(", compile_lvalue(env, ast), ")");
    }

    if (!is_incomplete_type(actual)) {
        Text_t code = compile(env, ast);
        if (promote(env, ast, &code, actual, t)) return code;
    }

    arg_ast_t *constructor_args = new (arg_ast_t, .value = ast);
    binding_t *constructor = get_constructor(env, t, constructor_args);
    if (constructor) {
        arg_t *arg_spec = Match(constructor->type, FunctionType)->args;
        return Texts(constructor->code, "(", compile_arguments(env, ast, arg_spec, constructor_args), ")");
    }

    code_err(ast, "I expected a ", type_to_text(t), " here, but this is a ", type_to_text(actual));
}
