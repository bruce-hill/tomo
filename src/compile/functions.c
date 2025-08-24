// This file defines how to compile functions

#include "functions.h"
#include "../ast.h"
#include "../compile.h"
#include "../environment.h"
#include "../naming.h"
#include "../stdlib/datatypes.h"
#include "../stdlib/integers.h"
#include "../stdlib/nums.h"
#include "../stdlib/tables.h"
#include "../stdlib/text.h"
#include "../stdlib/util.h"
#include "../typecheck.h"
#include "../types.h"
#include "blocks.h"
#include "declarations.h"
#include "integers.h"
#include "lists.h"
#include "promotions.h"
#include "sets.h"
#include "statements.h"
#include "structs.h"
#include "tables.h"
#include "text.h"
#include "types.h"

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
    Text_t name = namespace_name(env, env->namespace, Text$from_str(decl_name));
    if (env->namespace && env->namespace->parent && env->namespace->name && streq(decl_name, env->namespace->name))
        name = namespace_name(env, env->namespace, Text$from_str(String(get_line_number(ast->file, ast->start))));
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
                 type_to_str(ret_t));
    Text_t name_code =
        namespace_name(env, env->namespace, Texts(name, "$", String(get_line_number(ast->file, ast->start))));
    return Texts(ret_type_code, " ", name_code, arg_signature, ";\n");
}

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

public
Text_t compile_lambda(env_t *env, ast_t *ast) {
    DeclareMatch(lambda, ast, Lambda);
    Text_t name = namespace_name(env, env->namespace, Texts("lambda$", String(lambda->id)));

    env_t *body_scope = fresh_scope(env);
    body_scope->deferred = NULL;
    for (arg_ast_t *arg = lambda->args; arg; arg = arg->next) {
        type_t *arg_type = get_arg_ast_type(env, arg);
        set_binding(body_scope, arg->name, arg_type, Texts("_$", arg->name));
    }

    type_t *ret_t = get_type(body_scope, lambda->body);
    if (ret_t->tag == ReturnType) ret_t = Match(ret_t, ReturnType)->ret;

    if (lambda->ret_type) {
        type_t *declared = parse_type_ast(env, lambda->ret_type);
        if (can_promote(ret_t, declared)) ret_t = declared;
        else
            code_err(ast, "This function was declared to return a value of type ", type_to_str(declared),
                     ", but actually returns a value of type ", type_to_str(ret_t));
    }

    body_scope->fn_ret = ret_t;

    Table_t closed_vars = get_closed_vars(env, lambda->args, ast);
    if (Table$length(closed_vars) > 0) { // Create a typedef for the lambda's closure userdata
        Text_t def = Text("typedef struct {");
        for (int64_t i = 0; i < closed_vars.entries.length; i++) {
            struct {
                const char *name;
                binding_t *b;
            } *entry = closed_vars.entries.data + closed_vars.entries.stride * i;
            if (has_stack_memory(entry->b->type))
                code_err(ast, "This function is holding onto a reference to ", type_to_str(entry->b->type),
                         " stack memory in the variable `", entry->name,
                         "`, but the function may outlive the stack memory");
            if (entry->b->type->tag == ModuleType) continue;
            set_binding(body_scope, entry->name, entry->b->type, Texts("userdata->", entry->name));
            def = Texts(def, compile_declaration(entry->b->type, Text$from_str(entry->name)), "; ");
        }
        def = Texts(def, "} ", name, "$userdata_t;");
        env->code->local_typedefs = Texts(env->code->local_typedefs, def);
    }

    Text_t code = Texts("static ", compile_type(ret_t), " ", name, "(");
    for (arg_ast_t *arg = lambda->args; arg; arg = arg->next) {
        type_t *arg_type = get_arg_ast_type(env, arg);
        code = Texts(code, compile_type(arg_type), " _$", arg->name, ", ");
    }

    Text_t userdata;
    if (Table$length(closed_vars) == 0) {
        code = Texts(code, "void *_)");
        userdata = Text("NULL");
    } else {
        userdata = Texts("new(", name, "$userdata_t");
        for (int64_t i = 0; i < closed_vars.entries.length; i++) {
            struct {
                const char *name;
                binding_t *b;
            } *entry = closed_vars.entries.data + closed_vars.entries.stride * i;
            if (entry->b->type->tag == ModuleType) continue;
            binding_t *b = get_binding(env, entry->name);
            assert(b);
            Text_t binding_code = b->code;
            if (entry->b->type->tag == ListType) userdata = Texts(userdata, ", LIST_COPY(", binding_code, ")");
            else if (entry->b->type->tag == TableType || entry->b->type->tag == SetType)
                userdata = Texts(userdata, ", TABLE_COPY(", binding_code, ")");
            else userdata = Texts(userdata, ", ", binding_code);
        }
        userdata = Texts(userdata, ")");
        code = Texts(code, name, "$userdata_t *userdata)");
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

    env->code->lambdas = Texts(env->code->lambdas, code, " {\n", body, "\n}\n");
    return Texts("((Closure_t){", name, ", ", userdata, "})");
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
    case Set: {
        for (ast_list_t *item = Match(ast, Set)->items; item; item = item->next)
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
            ast_t *loop = WrapAST(ast, For, .vars = comp->vars, .iter = comp->iter, .body = body);
            return add_closed_vars(closed_vars, enclosing_scope, env, loop);
        }

        // List/Set/Table comprehension:
        ast_t *body = comp->expr;
        if (comp->filter) body = WrapAST(comp->expr, If, .condition = comp->filter, .body = body);
        ast_t *loop = WrapAST(ast, For, .vars = comp->vars, .iter = comp->iter, .body = body);
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
        add_closed_vars(closed_vars, enclosing_scope, env, Match(ast, For)->iter);
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
    case When: {
        DeclareMatch(when, ast, When);
        add_closed_vars(closed_vars, enclosing_scope, env, when->subject);
        type_t *subject_t = get_type(env, when->subject);

        if (subject_t->tag != EnumType) {
            for (when_clause_t *clause = when->clauses; clause; clause = clause->next) {
                add_closed_vars(closed_vars, enclosing_scope, env, clause->pattern);
                add_closed_vars(closed_vars, enclosing_scope, env, clause->body);
            }

            if (when->else_body) add_closed_vars(closed_vars, enclosing_scope, env, when->else_body);
            return;
        }

        DeclareMatch(enum_t, subject_t, EnumType);
        for (when_clause_t *clause = when->clauses; clause; clause = clause->next) {
            const char *clause_tag_name;
            if (clause->pattern->tag == Var) clause_tag_name = Match(clause->pattern, Var)->name;
            else if (clause->pattern->tag == FunctionCall && Match(clause->pattern, FunctionCall)->fn->tag == Var)
                clause_tag_name = Match(Match(clause->pattern, FunctionCall)->fn, Var)->name;
            else code_err(clause->pattern, "This is not a valid pattern for a ", type_to_str(subject_t), " enum");

            type_t *tag_type = NULL;
            for (tag_t *tag = enum_t->tags; tag; tag = tag->next) {
                if (streq(tag->name, clause_tag_name)) {
                    tag_type = tag->type;
                    break;
                }
            }
            assert(tag_type);
            env_t *scope = when_clause_scope(env, subject_t, clause);
            add_closed_vars(closed_vars, enclosing_scope, scope, clause->body);
        }
        if (when->else_body) add_closed_vars(closed_vars, enclosing_scope, env, when->else_body);
        break;
    }
    case Repeat: {
        add_closed_vars(closed_vars, enclosing_scope, env, Match(ast, Repeat)->body);
        break;
    }
    case Reduction: {
        DeclareMatch(reduction, ast, Reduction);
        static int64_t next_id = 1;
        ast_t *item = FakeAST(Var, String("$it", next_id++));
        ast_t *loop =
            FakeAST(For, .vars = new (ast_list_t, .ast = item), .iter = reduction->iter, .body = FakeAST(Pass));
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
    case Optional: {
        add_closed_vars(closed_vars, enclosing_scope, env, Match(ast, Optional)->value);
        break;
    }
    case NonOptional: {
        add_closed_vars(closed_vars, enclosing_scope, env, Match(ast, NonOptional)->value);
        break;
    }
    case DocTest: {
        add_closed_vars(closed_vars, enclosing_scope, env, Match(ast, DocTest)->expr);
        break;
    }
    case Assert: {
        add_closed_vars(closed_vars, enclosing_scope, env, Match(ast, Assert)->expr);
        add_closed_vars(closed_vars, enclosing_scope, env, Match(ast, Assert)->message);
        break;
    }
    case Deserialize: {
        add_closed_vars(closed_vars, enclosing_scope, env, Match(ast, Deserialize)->value);
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
    case LangDef:
    case Extend: {
        errx(1, "Definitions should not be reachable in a closure.");
    }
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

    Table_t closed_vars = {};
    add_closed_vars(&closed_vars, env, body_scope, block);
    return closed_vars;
}

public
Text_t compile_function(env_t *env, Text_t name_code, ast_t *ast, Text_t *staticdefs) {
    bool is_private = false;
    const char *function_name;
    arg_ast_t *args;
    type_t *ret_t;
    ast_t *body;
    ast_t *cache;
    bool is_inline;
    if (ast->tag == FunctionDef) {
        DeclareMatch(fndef, ast, FunctionDef);
        function_name = Match(fndef->name, Var)->name;
        is_private = function_name[0] == '_';
        args = fndef->args;
        ret_t = fndef->ret_type ? parse_type_ast(env, fndef->ret_type) : Type(VoidType);
        body = fndef->body;
        cache = fndef->cache;
        is_inline = fndef->is_inline;
    } else {
        DeclareMatch(convertdef, ast, ConvertDef);
        args = convertdef->args;
        ret_t = convertdef->ret_type ? parse_type_ast(env, convertdef->ret_type) : Type(VoidType);
        function_name = get_type_name(ret_t);
        if (!function_name)
            code_err(ast,
                     "Conversions are only supported for text, struct, and enum "
                     "types, not ",
                     type_to_str(ret_t));
        body = convertdef->body;
        cache = convertdef->cache;
        is_inline = convertdef->is_inline;
    }

    Text_t arg_signature = Text("(");
    Table_t used_names = {};
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

    body_scope->fn_ret = ret_t;

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
                     type_to_str(ret_t),
                     " value! \n "
                     "If this is not the case, please add a call to "
                     "`fail(\"Unreachable\")` at the end of the function to "
                     "help the "
                     "compiler out.");
    }

    Text_t body_code = Texts("{\n", compile_inline_block(body_scope, body), "}\n");
    Text_t definition = with_source_info(env, ast, Texts(code, " ", body_code, "\n"));

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
        OptionalInt64_t cache_size = Int64$parse(Text$from_str(Match(cache, Int)->str), NULL);
        Text_t pop_code = EMPTY_TEXT;
        if (cache->tag == Int && !cache_size.is_none && cache_size.value > 0) {
            // FIXME: this currently just deletes the first entry, but this
            // should be more like a least-recently-used cache eviction policy
            // or least-frequently-used
            pop_code = Texts("if (cache.entries.length > ", String(cache_size.value),
                             ") Table$remove(&cache, cache.entries.data + "
                             "cache.entries.stride*0, table_type);\n");
        }

        if (!args->next) {
            // Single-argument functions have simplified caching logic
            type_t *arg_type = get_arg_ast_type(env, args);
            Text_t wrapper =
                Texts(is_private ? EMPTY_TEXT : Text("public "), ret_type_code, " ", name_code, arg_signature,
                      "{\n"
                      "static Table_t cache = {};\n",
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

            int64_t num_fields = used_names.entries.length;
            const char *metamethods = is_packed_data(t) ? "PackedData$metamethods" : "Struct$metamethods";
            Text_t args_typeinfo =
                Texts("((TypeInfo_t[1]){{.size=sizeof(args), "
                      ".align=__alignof__(args), .metamethods=",
                      metamethods,
                      ", .tag=StructInfo, "
                      ".StructInfo.name=\"FunctionArguments\", "
                      ".StructInfo.num_fields=",
                      String(num_fields), ", .StructInfo.fields=(NamedType_t[", String(num_fields), "]){");
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
                "static Table_t cache = {};\n",
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

    Text_t qualified_name = Text$from_str(function_name);
    if (env->namespace && env->namespace->parent && env->namespace->name)
        qualified_name = Texts(env->namespace->name, ".", qualified_name);
    Text_t text = Texts("func ", qualified_name, "(");
    for (arg_ast_t *arg = args; arg; arg = arg->next) {
        text = Texts(text, type_to_text(get_arg_ast_type(env, arg)));
        if (arg->next) text = Texts(text, ", ");
    }
    if (ret_t && ret_t->tag != VoidType) text = Texts(text, "->", type_to_text(ret_t));
    text = Texts(text, ")");
    return definition;
}

public
Text_t compile_method_call(env_t *env, ast_t *ast) {
    DeclareMatch(call, ast, MethodCall);
    type_t *self_t = get_type(env, call->self);

    if (streq(call->name, "serialized")) {
        if (call->args) code_err(ast, ".serialized() doesn't take any arguments");
        return Texts("generic_serialize((", compile_declaration(self_t, Text("[1]")), "){", compile(env, call->self),
                     "}, ", compile_type_info(self_t), ")");
    }

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
    case SetType: return compile_set_method_call(env, ast);
    case TableType: return compile_table_method_call(env, ast);
    default: {
        DeclareMatch(methodcall, ast, MethodCall);
        type_t *fn_t = get_method_type(env, methodcall->self, methodcall->name);
        arg_ast_t *args = new (arg_ast_t, .value = methodcall->self, .next = methodcall->args);
        binding_t *b = get_namespace_binding(env, methodcall->self, methodcall->name);
        if (!b) code_err(ast, "No such method");
        return Texts(b->code, "(", compile_arguments(env, ast, Match(fn_t, FunctionType)->args, args), ")");
    }
    }
}
