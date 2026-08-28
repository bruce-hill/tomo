// This file defines how to compile functions

#include "../ast.h"
#include "../environment.h"
#include "../naming.h"
#include "../stdlib/c_strings.h"
#include "../stdlib/datatypes.h"
#include "../stdlib/integers.h"
#include "../stdlib/floats.h"
#include "../stdlib/optionals.h"
#include "../stdlib/tables.h"
#include "../stdlib/text.h"
#include "../typecheck.h"
#include "../stdlib/files.h"
#include "../types.h"
#include "../util.h"
#include "compilation.h"
#include "text.h"

public
Text_t compile_function_declaration(env_t *env, ast_t *ast) {
    DeclareMatch(fndef, ast, FunctionDef);
    const char *decl_name = Match(fndef->name, Var)->name;
    bool is_private = decl_name[0] == '_';
    if (is_private) return EMPTY_TEXT;
    Text_t arg_signature = Text("(");
    for (arg_ast_t *arg = fndef->args; arg; arg = arg->next) {
        type_t *arg_type = get_arg_ast_type(env, arg);
        arg_signature = Texts(arg_signature, compile_declaration(arg_type, Texts("_$", arg->name)));
        if (arg->next) arg_signature = Texts(arg_signature, ", ");
    }
    arg_signature = Texts(arg_signature, ")");

    type_t *ret_t = fndef->ret_type ? parse_type_ast(env, fndef->ret_type) : Type(VoidType);
    Text_t ret_type_code = compile_type(ret_t);
    if (ret_t->tag == AbortType) ret_type_code = Texts("__attribute__((noreturn)) _Noreturn ", ret_type_code);
    Text_t name =
        namespace_name(env, env->namespace, Text$replace(Text$from_str(decl_name), Text("."), Text("$")));
    if (env->namespace && env->namespace->parent && env->namespace->name && streq(decl_name, env->namespace->name))
        name = namespace_name(env, env->namespace, Texts(get_line_number(ast->file, ast->start)));
    return Texts(ret_type_code, " ", name, arg_signature, ";\n");
}

public
Text_t compile_convert_declaration(env_t *env, ast_t *ast) {
    DeclareMatch(def, ast, ConvertDef);

    Text_t arg_signature = Text("(");
    for (arg_ast_t *arg = def->args; arg; arg = arg->next) {
        type_t *arg_type = get_arg_ast_type(env, arg);
        arg_signature = Texts(arg_signature, compile_declaration(arg_type, Texts("_$", arg->name)));
        if (arg->next) arg_signature = Texts(arg_signature, ", ");
    }
    arg_signature = Texts(arg_signature, ")");

    type_t *ret_t = def->ret_type ? parse_type_ast(env, def->ret_type) : Type(VoidType);
    Text_t ret_type_code = compile_type(ret_t);
    Text_t name = Text$from_str(get_type_name(ret_t));
    if (name.length == 0)
        code_err(ast,
                 "Conversions are only supported for text, struct, and enum "
                 "types, not ",
                 type_to_text(ret_t));
    Text_t name_code = namespace_name(env, env->namespace, Texts(name, "$", get_line_number(ast->file, ast->start)));
    return Texts(ret_type_code, " ", name_code, arg_signature, ";\n");
}

public
Text_t compile_arguments(env_t *env, ast_t *call_ast, arg_t *spec_args, arg_ast_t *call_args) {
    Table_t used_args = EMPTY_TABLE;
    Text_t code = EMPTY_TEXT;
    env_t *default_scope = new (env_t);
    *default_scope = *env;
    default_scope->locals = new (Table_t, .fallback = env->namespace_bindings ? env->namespace_bindings : env->globals);
    for (arg_t *spec_arg = spec_args; spec_arg; spec_arg = spec_arg->next) {
        int64_t i = 1;
        // Find keyword:
        assert(spec_arg->name);
        for (arg_ast_t *call_arg = call_args; call_arg; call_arg = call_arg->next) {
            if (!call_arg->name) continue;
            if (!(streq(call_arg->name, spec_arg->name) || (spec_arg->alias && streq(call_arg->name, spec_arg->alias))))
                continue;

            Text_t value;
            if (spec_arg->type->tag == IntType && call_arg->value->tag == Int) {
                value = compile_int_to_type(env, call_arg->value, spec_arg->type);
            } else if (spec_arg->type->tag == FloatType && call_arg->value->tag == Int) {
                OptionalInt_t int_val = Int$from_str(Match(call_arg->value, Int)->str);
                if (int_val.small == 0) code_err(call_arg->value, "Failed to parse this integer");
                if (Match(spec_arg->type, FloatType)->bits == TYPE_NBITS64)
                    value = Text$from_str(String(hex_double(Float64$from_int(int_val, false))));
                else value = Text$from_str(String(hex_double((double)Float32$from_int(int_val, false)), "f"));
            } else {
                env_t *arg_env = with_enum_scope(env, spec_arg->type);
                value = compile_maybe_incref(arg_env, call_arg->value, spec_arg->type);
            }
            Table$str_set(&used_args, call_arg->name, call_arg);
            if (code.length > 0) code = Texts(code, ", ");
            code = Texts(code, value);
            goto found_it;
        }

        // Find positional:
        for (arg_ast_t *call_arg = call_args; call_arg; call_arg = call_arg->next) {
            if (call_arg->name) continue;
            const char *pseudoname = String(i++);
            if (!Table$str_get(used_args, pseudoname)) {
                Text_t value;
                if (spec_arg->type->tag == IntType && call_arg->value->tag == Int) {
                    value = compile_int_to_type(env, call_arg->value, spec_arg->type);
                } else if (spec_arg->type->tag == FloatType && call_arg->value->tag == Int) {
                    OptionalInt_t int_val = Int$from_str(Match(call_arg->value, Int)->str);
                    if (int_val.small == 0) code_err(call_arg->value, "Failed to parse this integer");
                    if (Match(spec_arg->type, FloatType)->bits == TYPE_NBITS64)
                        value = Text$from_str(String(hex_double(Float64$from_int(int_val, false))));
                    else value = Text$from_str(String(hex_double((double)Float32$from_int(int_val, false)), "f"));
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
    if (get_variant_constructor(env, call->fn))
        code_err(ast, "Enum variants are built with curly braces, not parentheses. Use ",
                 record_literal_name(call->fn), "{...} instead");
    if (fn_t->tag == FunctionType) {
        Text_t fn = compile(env, call->fn);
        if (!is_valid_call(env, Match(fn_t, FunctionType)->args, call->args, (call_opts_t){.promotion = true})) {
            arg_t *args = NULL;
            for (arg_ast_t *a = call->args; a; a = a->next)
                args = new (arg_t, .name = a->name, .type = get_type(env, a->value), .next = args);
            REVERSE_LIST(args);
            code_err(ast,
                     "This function's signature doesn't match this call site.\n"
                     "\n"
                     "The function takes these args: (",
                     arg_types_to_text(Match(fn_t, FunctionType)->args, ", "),
                     ")\n"
                     "But it's being called with:    (",
                     arg_types_to_text(args, ", "), ")");
        }
        return Texts(fn, "(", compile_arguments(env, ast, Match(fn_t, FunctionType)->args, call->args), ")");
    } else if (fn_t->tag == TypeInfoType) {
        type_t *t = Match(fn_t, TypeInfoType)->type;

        // Literal constructors for numeric types like `Byte(123)` should
        // not go through any conversion, just a cast:
        if (is_numeric_type(t) && call->args && !call->args->next && call->args->value->tag == Int)
            return compile_to_type(env, call->args->value, t);
        // ... and so should a numeric literal wrapped in a float or `Num`
        // constructor. `Num` especially: without this, get_constructor picks
        // the last-registered overload whose parameter the literal can compile
        // to, which is `Num$from_float32` -- so `Num(0.1)` would round the
        // exact tenth through a *single*-precision float, the exact opposite of
        // what the type is for.
        else if ((t->tag == FloatType || t->tag == NumType) && call->args && !call->args->next
                 && call->args->value->tag == Num)
            return compile_to_type(env, call->args->value, t);

        binding_t *constructor = get_constructor(env, t, call->args);
        if (constructor) {
            arg_t *arg_spec = Match(constructor->type, FunctionType)->args;
            return Texts(constructor->code, "(", compile_arguments(env, ast, arg_spec, call->args), ")");
        }

        if (t->tag == TextType) {
            if (!call->args) code_err(ast, "This constructor needs a value");
            if (!type_eq(t, TEXT_TYPE))
                code_err(call->fn, "I don't have a constructor defined for "
                                   "these arguments");
            // Text constructor:
            if (!call->args || call->args->next) code_err(call->fn, "This constructor takes exactly 1 argument");
            type_t *actual = call->args ? get_type(env, call->args->value) : NULL;
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
            type_t *actual = call->args ? get_type(env, call->args->value) : NULL;
            return Texts("Text$as_c_string(", expr_as_text(compile(env, call->args->value), actual, Text("no")), ")");
        } else if (t->tag == StructType) {
            // Parens on a type name mean "call a constructor function"; building a
            // struct from its fields is a record literal instead.
            code_err(ast, "There's no constructor for ", type_to_text(t),
                     " that takes these arguments. If you meant to build one from its fields, use curly braces: ",
                     Match(t, StructType)->name, "{...}");
        }
        code_err(ast,
                 "I could not find a constructor matching these arguments "
                 "for ",
                 type_to_text(t));
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
        code_err(call->fn, "This is not a function, it's a ", type_to_text(fn_t));
    }
}

public
Text_t compile_record_literal(env_t *env, ast_t *ast) {
    DeclareMatch(record, ast, RecordLiteral);
    type_t *type_t_ = get_type(env, record->type);

    binding_t *variant = get_variant_constructor(env, record->type);
    if (type_t_->tag == TypeInfoType) {
        type_t *t = Match(type_t_, TypeInfoType)->type;
        if (t->tag != StructType)
            code_err(record->type, "Curly braces build structs and enum variants, but ", type_to_text(t),
                     " is neither");
        return compile_struct_literal(env, ast, t, record->args);
    } else if (variant) {
        // Each enum variant is a generated constructor function taking the
        // variant's fields and returning the enum:
        arg_t *arg_spec = Match(variant->type, FunctionType)->args;
        call_opts_t constructor_opts = {.promotion = true};
        if (is_valid_call(env, arg_spec, record->args, constructor_opts)) {
            return Texts(variant->code, "(", compile_arguments(env, ast, arg_spec, record->args), ")");
        }
        arg_t *given = NULL;
        for (arg_ast_t *a = record->args; a; a = a->next)
            given = new (arg_t, .name = a->name, .type = get_type(env, a->value), .next = given);
        REVERSE_LIST(given);
        code_err(ast,
                 "This enum variant's fields don't match this value.\n"
                 "\n"
                 "The variant has these fields: (",
                 arg_types_to_text(arg_spec, ", "),
                 ")\n"
                 "But it's being built with:    (",
                 arg_types_to_text(given, ", "), ")");
    } else if (type_t_->tag == EnumType) {
        code_err(ast, "The variant ", record_literal_name(record->type),
                 " doesn't have any fields, so write it without curly braces");
    }
    code_err(record->type, "This isn't a struct or an enum variant, it's a ", type_to_text(type_t_));
}

static void add_closed_vars(Table_t *closed_vars, env_t *enclosing_scope, env_t *env, ast_t *ast) {
    if (ast == NULL) return;

    switch (ast->tag) {
    case Var: {
        binding_t *b = get_binding(enclosing_scope, Match(ast, Var)->name);
        if (b) {
            binding_t *shadow = get_binding(env, Match(ast, Var)->name);
            if (!shadow || shadow == b) Table$str_set(closed_vars, Match(ast, Var)->name, b);
        }
        break;
    }
    case TextJoin: {
        for (ast_list_t *child = Match(ast, TextJoin)->children; child; child = child->next)
            add_closed_vars(closed_vars, enclosing_scope, env, child->ast);
        break;
    }
    case Declare: {
        ast_t *value = Match(ast, Declare)->value;
        add_closed_vars(closed_vars, enclosing_scope, env, value);
        bind_statement(env, ast);
        break;
    }
    case Assign: {
        for (ast_list_t *target = Match(ast, Assign)->targets; target; target = target->next)
            add_closed_vars(closed_vars, enclosing_scope, env, target->ast);
        for (ast_list_t *value = Match(ast, Assign)->values; value; value = value->next)
            add_closed_vars(closed_vars, enclosing_scope, env, value->ast);
        break;
    }
    case BINOP_CASES: {
        binary_operands_t binop = BINARY_OPERANDS(ast);
        add_closed_vars(closed_vars, enclosing_scope, env, binop.lhs);
        add_closed_vars(closed_vars, enclosing_scope, env, binop.rhs);
        break;
    }
    case Not:
    case Negative:
    case HeapAllocate:
    case StackReference: {
        // UNSAFE:
        ast_t *value = ast->__data.Not.value;
        // END UNSAFE
        add_closed_vars(closed_vars, enclosing_scope, env, value);
        break;
    }
    case Min: {
        add_closed_vars(closed_vars, enclosing_scope, env, Match(ast, Min)->lhs);
        add_closed_vars(closed_vars, enclosing_scope, env, Match(ast, Min)->rhs);
        add_closed_vars(closed_vars, enclosing_scope, env, Match(ast, Min)->key);
        break;
    }
    case Max: {
        add_closed_vars(closed_vars, enclosing_scope, env, Match(ast, Max)->lhs);
        add_closed_vars(closed_vars, enclosing_scope, env, Match(ast, Max)->rhs);
        add_closed_vars(closed_vars, enclosing_scope, env, Match(ast, Max)->key);
        break;
    }
    case List: {
        for (ast_list_t *item = Match(ast, List)->items; item; item = item->next)
            add_closed_vars(closed_vars, enclosing_scope, env, item->ast);
        break;
    }
    case Table: {
        add_closed_vars(closed_vars, enclosing_scope, env, Match(ast, Table)->default_value);
        add_closed_vars(closed_vars, enclosing_scope, env, Match(ast, Table)->fallback);
        for (ast_list_t *entry = Match(ast, Table)->entries; entry; entry = entry->next)
            add_closed_vars(closed_vars, enclosing_scope, env, entry->ast);
        break;
    }
    case TableEntry: {
        add_closed_vars(closed_vars, enclosing_scope, env, Match(ast, TableEntry)->key);
        add_closed_vars(closed_vars, enclosing_scope, env, Match(ast, TableEntry)->value);
        break;
    }
    case Comprehension: {
        DeclareMatch(comp, ast, Comprehension);
        if (comp->expr->tag == Comprehension) { // Nested comprehension
            ast_t *body = comp->filter ? WrapAST(ast, If, .condition = comp->filter, .body = comp->expr) : comp->expr;
            ast_t *loop = WrapAST(ast, For, .vars = comp->vars, .at = comp->at, .iters = comp->iters, .body = body);
            return add_closed_vars(closed_vars, enclosing_scope, env, loop);
        }

        // List/Table comprehension:
        ast_t *body = comp->expr;
        if (comp->filter) body = WrapAST(comp->expr, If, .condition = comp->filter, .body = body);
        ast_t *loop = WrapAST(ast, For, .vars = comp->vars, .at = comp->at, .iters = comp->iters, .body = body);
        add_closed_vars(closed_vars, enclosing_scope, env, loop);
        break;
    }
    case Lambda: {
        DeclareMatch(lambda, ast, Lambda);
        env_t *lambda_scope = fresh_scope(env);
        for (arg_ast_t *arg = lambda->args; arg; arg = arg->next)
            set_binding(lambda_scope, arg->name, get_arg_ast_type(env, arg), Texts("_$", arg->name));
        add_closed_vars(closed_vars, enclosing_scope, lambda_scope, lambda->body);
        break;
    }
    case FunctionCall: {
        add_closed_vars(closed_vars, enclosing_scope, env, Match(ast, FunctionCall)->fn);
        for (arg_ast_t *arg = Match(ast, FunctionCall)->args; arg; arg = arg->next)
            add_closed_vars(closed_vars, enclosing_scope, env, arg->value);
        break;
    }
    case RecordLiteral: {
        for (arg_ast_t *arg = Match(ast, RecordLiteral)->args; arg; arg = arg->next)
            add_closed_vars(closed_vars, enclosing_scope, env, arg->value);
        break;
    }
    case MethodCall: {
        add_closed_vars(closed_vars, enclosing_scope, env, Match(ast, MethodCall)->self);
        for (arg_ast_t *arg = Match(ast, MethodCall)->args; arg; arg = arg->next)
            add_closed_vars(closed_vars, enclosing_scope, env, arg->value);
        break;
    }
    case Block: {
        env = fresh_scope(env);
        for (ast_list_t *statement = Match(ast, Block)->statements; statement; statement = statement->next)
            add_closed_vars(closed_vars, enclosing_scope, env, statement->ast);
        break;
    }
    case For: {
        for (ast_list_t *iter = Match(ast, For)->iters; iter; iter = iter->next)
            add_closed_vars(closed_vars, enclosing_scope, env, iter->ast);
        env_t *body_scope = for_scope(env, ast);
        add_closed_vars(closed_vars, enclosing_scope, body_scope, Match(ast, For)->body);
        add_closed_vars(closed_vars, enclosing_scope, env, Match(ast, For)->empty);
        break;
    }
    case While: {
        DeclareMatch(while_, ast, While);
        add_closed_vars(closed_vars, enclosing_scope, env, while_->condition);
        env_t *scope = fresh_scope(env);
        add_closed_vars(closed_vars, enclosing_scope, scope, while_->body);
        break;
    }
    case If: {
        DeclareMatch(if_, ast, If);
        ast_t *condition = if_->condition;
        if (condition->tag == Declare) {
            env_t *truthy_scope = fresh_scope(env);
            bind_statement(truthy_scope, condition);
            if (!Match(condition, Declare)->value)
                code_err(condition, "This declared variable must have an initial value");
            add_closed_vars(closed_vars, enclosing_scope, env, Match(condition, Declare)->value);
            ast_t *var = Match(condition, Declare)->var;
            type_t *cond_t = get_type(truthy_scope, var);
            if (cond_t->tag == OptionalType) {
                set_binding(truthy_scope, Match(var, Var)->name, Match(cond_t, OptionalType)->type, EMPTY_TEXT);
            }
            add_closed_vars(closed_vars, enclosing_scope, truthy_scope, if_->body);
            add_closed_vars(closed_vars, enclosing_scope, env, if_->else_body);
        } else {
            add_closed_vars(closed_vars, enclosing_scope, env, condition);
            env_t *truthy_scope = env;
            type_t *cond_t = get_type(env, condition);
            if (condition->tag == Var && cond_t->tag == OptionalType) {
                truthy_scope = fresh_scope(env);
                set_binding(truthy_scope, Match(condition, Var)->name, Match(cond_t, OptionalType)->type, EMPTY_TEXT);
            }
            add_closed_vars(closed_vars, enclosing_scope, truthy_scope, if_->body);
            add_closed_vars(closed_vars, enclosing_scope, env, if_->else_body);
        }
        break;
    }
    case Match: {
        DeclareMatch(match, ast, Match);
        add_closed_vars(closed_vars, enclosing_scope, env, match->subject);
        type_t *subject_t = get_type(env, match->subject);

        if (subject_t->tag != EnumType) {
            for (match_clause_t *clause = match->clauses; clause; clause = clause->next) {
                add_closed_vars(closed_vars, enclosing_scope, env, clause->pattern);
                add_closed_vars(closed_vars, enclosing_scope, env, clause->body);
            }

            if (match->else_body) add_closed_vars(closed_vars, enclosing_scope, env, match->else_body);
            return;
        }

        for (match_clause_t *clause = match->clauses; clause; clause = clause->next) {
            if (clause->pattern->tag != Var
                && !(clause->pattern->tag == RecordLiteral && Match(clause->pattern, RecordLiteral)->type->tag == Var))
                code_err(clause->pattern, "This is not a valid pattern for a ", type_to_text(subject_t), " enum");

            env_t *scope = match_clause_scope(env, subject_t, clause);
            add_closed_vars(closed_vars, enclosing_scope, scope, clause->body);
        }
        if (match->else_body) add_closed_vars(closed_vars, enclosing_scope, env, match->else_body);
        break;
    }
    case Repeat: {
        add_closed_vars(closed_vars, enclosing_scope, env, Match(ast, Repeat)->body);
        break;
    }
    case Reduction: {
        DeclareMatch(reduction, ast, Reduction);
        add_closed_vars(closed_vars, enclosing_scope, env, reduction->iter);
        static int64_t next_id = 1;
        ast_t *item = FakeAST(Var, String("$it", next_id++));
        ast_t *loop =
            FakeAST(For, .vars = new (ast_list_t, .ast = item), .iters = new (ast_list_t, .ast = reduction->iter), .body = FakeAST(Pass));
        env_t *scope = for_scope(env, loop);
        add_closed_vars(closed_vars, enclosing_scope, scope, reduction->key ? reduction->key : item);
        break;
    }
    case Defer: {
        add_closed_vars(closed_vars, enclosing_scope, env, Match(ast, Defer)->body);
        break;
    }
    case Return: {
        ast_t *ret = Match(ast, Return)->value;
        if (ret) add_closed_vars(closed_vars, enclosing_scope, env, ret);
        break;
    }
    case Index: {
        add_closed_vars(closed_vars, enclosing_scope, env, Match(ast, Index)->indexed);
        add_closed_vars(closed_vars, enclosing_scope, env, Match(ast, Index)->index);
        break;
    }
    case FieldAccess: {
        add_closed_vars(closed_vars, enclosing_scope, env, Match(ast, FieldAccess)->fielded);
        break;
    }
    case NonOptional: {
        add_closed_vars(closed_vars, enclosing_scope, env, Match(ast, NonOptional)->value);
        break;
    }
    case Serialize: {
        add_closed_vars(closed_vars, enclosing_scope, env, Match(ast, Serialize)->value);
        break;
    }
    case Deserialize: {
        add_closed_vars(closed_vars, enclosing_scope, env, Match(ast, Deserialize)->value);
        break;
    }
    case DebugLog: {
        for (ast_list_t *value = Match(ast, DebugLog)->values; value; value = value->next)
            add_closed_vars(closed_vars, enclosing_scope, env, value->ast);
        break;
    }
    case Assert: {
        add_closed_vars(closed_vars, enclosing_scope, env, Match(ast, Assert)->expr);
        add_closed_vars(closed_vars, enclosing_scope, env, Match(ast, Assert)->message);
        break;
    }
    case ExplicitlyTyped: {
        add_closed_vars(closed_vars, enclosing_scope, env, Match(ast, ExplicitlyTyped)->ast);
        break;
    }
    case Use:
    case FunctionDef:
    case ConvertDef:
    case StructDef:
    case EnumDef:
    case LangDef: errx(1, "Definitions should not be reachable in a closure.");
    default: break;
    }
}

public
Table_t get_closed_vars(env_t *env, arg_ast_t *args, ast_t *block) {
    env_t *body_scope = fresh_scope(env);
    for (arg_ast_t *arg = args; arg; arg = arg->next) {
        type_t *arg_type = get_arg_ast_type(env, arg);
        set_binding(body_scope, arg->name, arg_type, Texts("_$", arg->name));
    }

    Table_t closed_vars = EMPTY_TABLE;
    add_closed_vars(&closed_vars, env, body_scope, block);
    return closed_vars;
}

static visit_behavior_t find_used_variables(ast_t *ast, void *userdata) {
    Table_t *vars = (Table_t *)userdata;
    switch (ast->tag) {
    case Var: {
        const char *name = Match(ast, Var)->name;
        Table$str_set(vars, name, ast);
        return VISIT_STOP;
    }
    case Assign: {
        for (ast_list_t *target = Match(ast, Assign)->targets; target; target = target->next) {
            ast_t *var = target->ast;
            for (;;) {
                if (var->tag == Index) {
                    ast_t *index = Match(var, Index)->index;
                    if (index) ast_visit(index, find_used_variables, userdata);
                    var = Match(var, Index)->indexed;
                } else if (var->tag == FieldAccess) {
                    var = Match(var, FieldAccess)->fielded;
                } else {
                    break;
                }
            }
        }
        for (ast_list_t *val = Match(ast, Assign)->values; val; val = val->next) {
            ast_visit(val->ast, find_used_variables, userdata);
        }
        return VISIT_STOP;
    }
    case UPDATE_CASES: {
        binary_operands_t operands = BINARY_OPERANDS(ast);
        ast_t *lhs = operands.lhs;
        for (;;) {
            if (lhs->tag == Index) {
                ast_t *index = Match(lhs, Index)->index;
                if (index) ast_visit(index, find_used_variables, userdata);
                lhs = Match(lhs, Index)->indexed;
            } else if (lhs->tag == FieldAccess) {
                lhs = Match(lhs, FieldAccess)->fielded;
            } else {
                break;
            }
        }
        ast_visit(operands.rhs, find_used_variables, userdata);
        return VISIT_STOP;
    }
    case Declare: {
        ast_visit(Match(ast, Declare)->value, find_used_variables, userdata);
        return VISIT_STOP;
    }
    default: return VISIT_PROCEED;
    }
}

static visit_behavior_t find_assigned_variables(ast_t *ast, void *userdata) {
    Table_t *vars = (Table_t *)userdata;
    switch (ast->tag) {
    case Assign:
        for (ast_list_t *target = Match(ast, Assign)->targets; target; target = target->next) {
            ast_t *var = target->ast;
            for (;;) {
                if (var->tag == Index) var = Match(var, Index)->indexed;
                else if (var->tag == FieldAccess) var = Match(var, FieldAccess)->fielded;
                else break;
            }
            if (var->tag == Var) {
                const char *name = Match(var, Var)->name;
                Table$str_set(vars, name, var);
            }
        }
        return VISIT_STOP;
    case UPDATE_CASES: {
        binary_operands_t operands = BINARY_OPERANDS(ast);
        ast_t *var = operands.lhs;
        for (;;) {
            if (var->tag == Index) var = Match(var, Index)->indexed;
            else if (var->tag == FieldAccess) var = Match(var, FieldAccess)->fielded;
            else break;
        }
        if (var->tag == Var) {
            const char *name = Match(var, Var)->name;
            Table$str_set(vars, name, var);
        }
        return VISIT_STOP;
    }
    case Declare: {
        ast_t *var = Match(ast, Declare)->var;
        const char *name = Match(var, Var)->name;
        Table$str_set(vars, name, var);
        return VISIT_STOP;
    }
    // Don't cross into nested lambdas: they're checked separately with their
    // own argument list, and their arguments (e.g. `&` out-parameters written
    // via `a[] = ...`) shadow the enclosing scope's names.
    case Lambda: return VISIT_STOP;
    default: return VISIT_PROCEED;
    }
}

static void check_unused_vars(env_t *env, arg_ast_t *args, ast_t *body) {
    Table_t used_vars = EMPTY_TABLE;
    ast_visit(body, find_used_variables, &used_vars);
    Table_t assigned_vars = EMPTY_TABLE;
    ast_visit(body, find_assigned_variables, &assigned_vars);

    for (arg_ast_t *arg = args; arg; arg = arg->next) {
        type_t *arg_type = get_arg_ast_type(env, arg);
        if (arg_type->tag == PointerType) {
            Table$str_remove(&assigned_vars, arg->name);
        }
    }

    Table_t unused = Table$without(assigned_vars, used_vars, Table$info(&CString$info, &Present$$info));
    for (int64_t i = 0; i < (int64_t)unused.entries.length; i++) {
        struct {
            const char *name;
        } *entry = unused.entries.data + i * unused.entries.stride;
        if (streq(entry->name, "_")) continue;
        // Global/file scoped vars are okay to mutate without reading
        if (get_binding(env, entry->name) != NULL) continue;
        ast_t *var = Table$str_get(assigned_vars, entry->name);
        if (var) code_err(var, "This variable was assigned to, but never read from.");
    }
}

// The instrumentation `--instrument` adds to a generated function: a
// file-scope site holding the function's Tomo-level name and location (out
// param `site_def`), plus the TOMO_PROFILED() line that opens the body. The
// cleanup attribute inside that macro stops the timer on every way out of the
// function, so `return`s and deferred blocks need no special handling here.
// Returns empty text (and leaves `site_def` alone) when profiling is off.
static Text_t compile_profiling(env_t *env, ast_t *ast, Text_t name_code, Text_t display_name, Text_t *site_def) {
    if (!env->do_profiling) return EMPTY_TEXT;
    Text_t site = Texts("_tomo_profile$", name_code);
    *site_def = Texts("static tomo_profile_site_t ", site, " = {.name=", quoted_text(display_name), ", .file=",
                      quoted_str(ast->file->filename), ", .line=", (int64_t)get_line_number(ast->file, ast->start),
                      "};\n");
    return Texts("TOMO_PROFILED(&", site, ");\n");
}

// A function's name as a Tomo programmer would write it ("Foo.bar"), for the
// profile report -- the C symbol name is an implementation detail.
static Text_t profile_display_name(env_t *env, const char *function_name) {
    Text_t name = Text$from_str(function_name);
    for (namespace_t *ns = env->namespace; ns; ns = ns->parent)
        if (ns->name) name = Texts(ns->name, ".", name);
    return name;
}

// Compile a lambda to a `(Closure_t){fn, userdata}` value. When
// `args_by_pointer` is set, every argument is received as a `void *` and copied
// into a local of its declared type at function entry (`T _$x = *(T*)_$ptr$x;`).
// This lets a lambda match the by-pointer calling convention that the generic
// List runtime uses for predicates/comparisons WITHOUT an adapter shim, while
// the entry copy preserves by-value semantics (mutating the parameter can't
// touch the list element). Only compile_list_method_call() uses this, and only
// for a directly-passed lambda whose declared arg/return types already match.
static Text_t compile_lambda_ex(env_t *env, ast_t *ast, bool args_by_pointer) {
    DeclareMatch(lambda, ast, Lambda);
    Text_t name = namespace_name(env, env->namespace, Texts("lambda$", lambda->id));

    env_t *body_scope = fresh_scope(env);
    body_scope->deferred = NULL;
    for (arg_ast_t *arg = lambda->args; arg; arg = arg->next) {
        type_t *arg_type = get_arg_ast_type(env, arg);
        set_binding(body_scope, arg->name, arg_type, Texts("_$", arg->name));
    }

    body_scope->fn = ast;

    Table_t closed_vars = get_closed_vars(env, lambda->args, ast);
    if (Table$length(closed_vars) > 0) { // Create a typedef for the lambda's closure userdata
        Text_t def = Text("typedef struct {");
        for (int64_t i = 0; i < (int64_t)closed_vars.entries.length; i++) {
            struct {
                const char *name;
                binding_t *b;
            } *entry = closed_vars.entries.data + closed_vars.entries.stride * i;
            if (has_stack_memory(entry->b->type))
                code_err(ast, "This function is holding onto a reference to ", type_to_text(entry->b->type),
                         " stack memory in the variable `", entry->name,
                         "`, but the function may outlive the stack memory");
            if (entry->b->type->tag == ModuleType) continue;
            set_binding(body_scope, entry->name, entry->b->type, Texts("userdata->", entry->name));
            def = Texts(def, compile_declaration(entry->b->type, Text$from_str(entry->name)), "; ");
        }
        def = Texts(def, "} ", name, "$userdata_t;");
        env->code->local_typedefs = Texts(env->code->local_typedefs, def);
    }

    type_t *ret_t = get_function_return_type(env, ast);
    Text_t code = Texts("static ", compile_type(ret_t), " ", name, "(");
    Text_t arg_unpack = EMPTY_TEXT;
    for (arg_ast_t *arg = lambda->args; arg; arg = arg->next) {
        type_t *arg_type = get_arg_ast_type(env, arg);
        if (args_by_pointer) {
            code = Texts(code, "void *_$ptr$", arg->name, ", ");
            arg_unpack = Texts(arg_unpack, compile_type(arg_type), " _$", arg->name, " = *(", compile_type(arg_type),
                               "*)_$ptr$", arg->name, ";\n");
        } else {
            code = Texts(code, compile_type(arg_type), " _$", arg->name, ", ");
        }
    }

    Text_t userdata;
    if (Table$length(closed_vars) == 0) {
        code = Texts(code, "void *_)");
        userdata = Text("NULL");
    } else {
        userdata = Texts("heap(((", name, "$userdata_t){");
        for (int64_t i = 0; i < (int64_t)closed_vars.entries.length; i++) {
            if (i > 0) userdata = Text$concat(userdata, Text(", "));
            struct {
                const char *name;
                binding_t *b;
            } *entry = closed_vars.entries.data + closed_vars.entries.stride * i;
            if (entry->b->type->tag == ModuleType) continue;
            binding_t *b = get_binding(env, entry->name);
            assert(b);
            Text_t binding_code = b->code;
            if (entry->b->type->tag == ListType) userdata = Texts(userdata, "LIST_COPY(", binding_code, ")");
            else if (entry->b->type->tag == TableType) userdata = Texts(userdata, "TABLE_COPY(", binding_code, ")");
            else userdata = Texts(userdata, binding_code);
        }
        userdata = Texts(userdata, "}))");
        // The closure calling convention passes userdata as a `void *` (see the
        // fn_type_code casts at call sites). The lambda's own signature must
        // match that type exactly -- calling through a mismatched
        // function-pointer type is undefined behavior -- so take `void *` and
        // cast to the concrete userdata struct inside:
        code = Texts(code, "void *$userdata)");
    }

    Text_t body = EMPTY_TEXT;
    for (ast_list_t *stmt = Match(lambda->body, Block)->statements; stmt; stmt = stmt->next) {
        if (stmt->next || ret_t->tag == VoidType || ret_t->tag == AbortType
            || get_type(body_scope, stmt->ast)->tag == ReturnType)
            body = Texts(body, compile_statement(body_scope, stmt->ast), "\n");
        else body = Texts(body, compile_statement(body_scope, FakeAST(Return, stmt->ast)), "\n");
        bind_statement(body_scope, stmt->ast);
    }
    if ((ret_t->tag == VoidType || ret_t->tag == AbortType) && body_scope->deferred)
        body = Texts(body, compile_statement(body_scope, FakeAST(Return)), "\n");

    Text_t userdata_cast = Table$length(closed_vars) > 0
                               ? Texts(name, "$userdata_t *userdata = $userdata;\n")
                               : EMPTY_TEXT;
    Text_t site_def = EMPTY_TEXT;
    // Lambdas have no name of their own, so the report identifies them by the
    // source location in the site (`lambda (file.tm:12)`):
    Text_t instrumentation = compile_profiling(env, ast, name, Text("lambda"), &site_def);
    env->code->lambdas = Texts(env->code->lambdas, site_def, code, " {\n", userdata_cast, arg_unpack, instrumentation,
                               body, "\n}\n");

    check_unused_vars(env, lambda->args, lambda->body);

    return Texts("((Closure_t){", name, ", ", userdata, "})");
}

public
Text_t compile_lambda(env_t *env, ast_t *ast) {
    return compile_lambda_ex(env, ast, false);
}

// Compile a lambda whose arguments are received by pointer (see compile_lambda_ex).
public
Text_t compile_lambda_pointer_args(env_t *env, ast_t *ast) {
    return compile_lambda_ex(env, ast, true);
}

public
Text_t compile_function(env_t *env, Text_t name_code, ast_t *ast, Text_t *staticdefs) {
    bool is_private = false;
    const char *function_name;
    arg_ast_t *args;
    type_t *ret_t = get_function_return_type(env, ast);
    ast_t *body;
    ast_t *cache;
    bool is_inline;
    if (ast->tag == FunctionDef) {
        DeclareMatch(fndef, ast, FunctionDef);
        function_name = Match(fndef->name, Var)->name;
        is_private = function_name[0] == '_';
        args = fndef->args;
        body = fndef->body;
        cache = fndef->cache;
        is_inline = fndef->is_inline;
    } else {
        DeclareMatch(convertdef, ast, ConvertDef);
        args = convertdef->args;
        function_name = get_type_name(ret_t);
        if (!function_name)
            code_err(ast,
                     "Conversions are only supported for text, struct, and enum "
                     "types, not ",
                     type_to_text(ret_t));
        body = convertdef->body;
        cache = convertdef->cache;
        is_inline = convertdef->is_inline;
    }

    Text_t arg_signature = Text("(");
    Table_t used_names = EMPTY_TABLE;
    for (arg_ast_t *arg = args; arg; arg = arg->next) {
        type_t *arg_type = get_arg_ast_type(env, arg);
        arg_signature = Texts(arg_signature, compile_declaration(arg_type, Texts("_$", arg->name)));
        if (arg->next) arg_signature = Texts(arg_signature, ", ");
        if (Table$str_get(used_names, arg->name))
            code_err(ast, "The argument name '", arg->name, "' is used more than once");
        Table$str_set(&used_names, arg->name, arg->name);
    }
    arg_signature = Texts(arg_signature, ")");

    Text_t ret_type_code = compile_type(ret_t);
    if (ret_t->tag == AbortType) ret_type_code = Texts("__attribute__((noreturn)) _Noreturn ", ret_type_code);

    if (is_private) *staticdefs = Texts(*staticdefs, "static ", ret_type_code, " ", name_code, arg_signature, ";\n");

    Text_t code;
    if (cache) {
        code = Texts("static ", ret_type_code, " ", name_code, "$uncached", arg_signature);
    } else {
        code = Texts(ret_type_code, " ", name_code, arg_signature);
        if (is_inline) code = Texts("INLINE ", code);
        if (!is_private) code = Texts("public ", code);
    }

    env_t *body_scope = fresh_scope(env);
    while (body_scope->namespace) {
        body_scope->locals->fallback = body_scope->locals->fallback->fallback;
        body_scope->namespace = body_scope->namespace->parent;
    }

    body_scope->deferred = NULL;
    for (arg_ast_t *arg = args; arg; arg = arg->next) {
        type_t *arg_type = get_arg_ast_type(env, arg);
        set_binding(body_scope, arg->name, arg_type, Texts("_$", arg->name));
    }

    body_scope->fn = ast;

    type_t *body_type = get_type(body_scope, body);
    if (ret_t->tag == AbortType) {
        if (body_type->tag != AbortType) code_err(ast, "This function can reach the end without aborting!");
    } else if (ret_t->tag == VoidType) {
        if (body_type->tag == AbortType)
            code_err(ast, "This function will always abort before it reaches the "
                          "end, but it's declared as having a Void return. It should "
                          "be declared as an Abort return instead.");
    } else {
        if (body_type->tag != ReturnType && body_type->tag != AbortType)
            code_err(ast,
                     "This function looks like it can reach the end without "
                     "returning a ",
                     type_to_text(ret_t),
                     " value!\n"
                     "If this is not the case, please add a call to "
                     "`fail(\"Unreachable\")` at the end of the function to "
                     "help the "
                     "compiler out.");
    }

    Text_t site_def = EMPTY_TEXT;
    Text_t instrumentation =
        compile_profiling(env, ast, name_code, profile_display_name(env, function_name), &site_def);
    Text_t body_code = Texts("{\n", instrumentation, compile_inline_block(body_scope, body), "}\n");
    Text_t definition = Texts(site_def, with_source_info(env, ast, Texts(code, " ", body_code, "\n")));

    if (cache && args == NULL) { // no-args cache just uses a static var
        Text_t wrapper =
            Texts(is_private ? EMPTY_TEXT : Text("public "), ret_type_code, " ", name_code,
                  "(void) {\n"
                  "static ",
                  compile_declaration(ret_t, Text("cached_result")), ";\n", "static bool initialized = false;\n",
                  "if (!initialized) {\n"
                  "\tcached_result = ",
                  name_code, "$uncached();\n", "\tinitialized = true;\n", "}\n",
                  "return cached_result;\n"
                  "}\n");
        definition = Texts(definition, wrapper);
    } else if (cache && cache->tag == Int) {
        assert(args);
        OptionalInt64_t cache_size = Int64$parse(Text$from_str(Match(cache, Int)->str), NONE_INT, NULL);
        Text_t pop_code = EMPTY_TEXT;
        if (cache->tag == Int && cache_size.has_value && cache_size.value > 0) {
            // FIXME: this currently just deletes the first entry, but this
            // should be more like a least-recently-used cache eviction policy
            // or least-frequently-used
            pop_code = Texts("if (cache.entries.length > ", cache_size.value,
                             ") Table$remove(&cache, cache.entries.data + "
                             "cache.entries.stride*0, table_type);\n");
        }

        if (!args->next) {
            // Single-argument functions have simplified caching logic
            type_t *arg_type = get_arg_ast_type(env, args);
            Text_t wrapper =
                Texts(is_private ? EMPTY_TEXT : Text("public "), ret_type_code, " ", name_code, arg_signature,
                      "{\n"
                      "static Table_t cache = EMPTY_TABLE;\n",
                      "const TypeInfo_t *table_type = Table$info(", compile_type_info(arg_type), ", ",
                      compile_type_info(ret_t), ");\n",
                      compile_declaration(Type(PointerType, .pointed = ret_t), Text("cached")),
                      " = Table$get_raw(cache, &_$", args->name,
                      ", table_type);\n"
                      "if (cached) return *cached;\n",
                      compile_declaration(ret_t, Text("ret")), " = ", name_code, "$uncached(_$", args->name, ");\n",
                      pop_code, "Table$set(&cache, &_$", args->name,
                      ", &ret, table_type);\n"
                      "return ret;\n"
                      "}\n");
            definition = Texts(definition, wrapper);
        } else {
            // Multi-argument functions use a custom struct type (only defined
            // internally) as a cache key:
            arg_t *fields = NULL;
            for (arg_ast_t *arg = args; arg; arg = arg->next)
                fields = new (arg_t, .name = arg->name, .type = get_arg_ast_type(env, arg), .next = fields);
            REVERSE_LIST(fields);
            type_t *t = Type(StructType, .name = String("func$", get_line_number(ast->file, ast->start), "$args"),
                             .fields = fields, .env = env);

            int64_t num_fields = (int64_t)used_names.entries.length;
            const char *metamethods = is_packed_data(t) ? "PackedData$metamethods" : "Struct$metamethods";
            Text_t args_typeinfo = Texts("((TypeInfo_t[1]){{.size=sizeof(args), "
                                         ".align=__alignof__(args), .metamethods=",
                                         metamethods,
                                         ", .tag=StructInfo, "
                                         ".StructInfo.name=\"FunctionArguments\", "
                                         ".StructInfo.num_fields=",
                                         num_fields, ", .StructInfo.fields=(NamedType_t[", num_fields, "]){");
            Text_t args_type = Text("struct { ");
            for (arg_t *f = fields; f; f = f->next) {
                args_typeinfo = Texts(args_typeinfo, "{\"", f->name, "\", ", compile_type_info(f->type), "}");
                args_type = Texts(args_type, compile_declaration(f->type, Text$from_str(f->name)), "; ");
                if (f->next) args_typeinfo = Texts(args_typeinfo, ", ");
            }
            args_type = Texts(args_type, "}");
            args_typeinfo = Texts(args_typeinfo, "}}})");

            Text_t all_args = EMPTY_TEXT;
            for (arg_ast_t *arg = args; arg; arg = arg->next)
                all_args = Texts(all_args, "_$", arg->name, arg->next ? Text(", ") : EMPTY_TEXT);

            Text_t wrapper = Texts(
                is_private ? EMPTY_TEXT : Text("public "), ret_type_code, " ", name_code, arg_signature,
                "{\n"
                "static Table_t cache = EMPTY_TABLE;\n",
                args_type, " args = {", all_args,
                "};\n"
                "const TypeInfo_t *table_type = Table$info(",
                args_typeinfo, ", ", compile_type_info(ret_t), ");\n",
                compile_declaration(Type(PointerType, .pointed = ret_t), Text("cached")),
                " = Table$get_raw(cache, &args, table_type);\n"
                "if (cached) return *cached;\n",
                compile_declaration(ret_t, Text("ret")), " = ", name_code, "$uncached(", all_args, ");\n", pop_code,
                "Table$set(&cache, &args, &ret, table_type);\n"
                "return ret;\n"
                "}\n");
            definition = Texts(definition, wrapper);
        }
    }

    check_unused_vars(env, args, body);

    return definition;
}

public
Text_t compile_method_call(env_t *env, ast_t *ast) {
    DeclareMatch(call, ast, MethodCall);
    ast_t *as_field = WrapAST(call->self, FieldAccess, .fielded = call->self, .field = call->name);
    if (get_subcommand_binding(env, as_field)) // Subcommand calls: `main.add(...)`
        return compile(env, WrapAST(ast, FunctionCall, .fn = as_field, .args = call->args));
    type_t *self_t = get_type(env, call->self);
    type_t *self_value_t = value_type(self_t);
    if (self_value_t->tag == TypeInfoType || self_value_t->tag == ModuleType) {
        return compile(env, WrapAST(ast, FunctionCall,
                                    .fn = WrapAST(call->self, FieldAccess, .fielded = call->self, .field = call->name),
                                    .args = call->args));
    }

    type_t *field_type = get_field_type(self_value_t, call->name);
    if (field_type && field_type->tag == ClosureType) field_type = Match(field_type, ClosureType)->fn;
    if (field_type && field_type->tag == FunctionType)
        return compile(env, WrapAST(ast, FunctionCall,
                                    .fn = WrapAST(call->self, FieldAccess, .fielded = call->self, .field = call->name),
                                    .args = call->args));

    switch (self_value_t->tag) {
    case ListType: return compile_list_method_call(env, ast);
    case TableType: return compile_table_method_call(env, ast);
    default: {
        DeclareMatch(methodcall, ast, MethodCall);
        type_t *fn_t = get_method_type(env, methodcall->self, methodcall->name);
        arg_ast_t *args = new (arg_ast_t, .value = methodcall->self, .next = methodcall->args);
        binding_t *b = get_namespace_binding(env, methodcall->self, methodcall->name);
        if (!b) {
            OptionalText_t suggestion = suggest_best_name(call->name, get_method_names(env, self_value_t));
            code_err(ast, "No such method!", suggestion);
        }
        return Texts(b->code, "(", compile_arguments(env, ast, Match(fn_t, FunctionType)->args, args), ")");
    }
    }
}
