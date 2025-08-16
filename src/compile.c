// Compilation logic
#include <ctype.h>
#include <glob.h>
#include <gc.h>
#include <gmp.h>
#include <stdio.h>
#include <uninorm.h>

#include "ast.h"
#include "compile.h"
#include "enums.h"
#include "environment.h"
#include "modules.h"
#include "parse.h"
#include "stdlib/integers.h"
#include "stdlib/nums.h"
#include "stdlib/paths.h"
#include "stdlib/text.h"
#include "stdlib/util.h"
#include "structs.h"
#include "typecheck.h"

typedef ast_t* (*comprehension_body_t)(ast_t*, ast_t*);

static Text_t compile_to_pointer_depth(env_t *env, ast_t *ast, int64_t target_depth, bool needs_incref);
static Text_t compile_text(env_t *env, ast_t *ast, Text_t color);
static Text_t compile_text_literal(Text_t literal);
static Text_t compile_arguments(env_t *env, ast_t *call_ast, arg_t *spec_args, arg_ast_t *call_args);
static Text_t compile_maybe_incref(env_t *env, ast_t *ast, type_t *t);
static Text_t compile_int_to_type(env_t *env, ast_t *ast, type_t *target);
static Text_t compile_unsigned_type(type_t *t);
static Text_t promote_to_optional(type_t *t, Text_t code);
static Text_t compile_none(type_t *t);
static Text_t compile_empty(type_t *t);
static Text_t compile_declared_value(env_t *env, ast_t *declaration_ast);
static Text_t compile_to_type(env_t *env, ast_t *ast, type_t *t);
static Text_t compile_typed_list(env_t *env, ast_t *ast, type_t *list_type);
static Text_t compile_typed_set(env_t *env, ast_t *ast, type_t *set_type);
static Text_t compile_typed_table(env_t *env, ast_t *ast, type_t *table_type);
static Text_t compile_typed_allocation(env_t *env, ast_t *ast, type_t *pointer_type);
static Text_t check_none(type_t *t, Text_t value);
static Text_t optional_into_nonnone(type_t *t, Text_t value);
static ast_t *add_to_list_comprehension(ast_t *item, ast_t *subject);
static ast_t *add_to_table_comprehension(ast_t *entry, ast_t *subject);
static ast_t *add_to_set_comprehension(ast_t *item, ast_t *subject);
static Text_t compile_lvalue(env_t *env, ast_t *ast);

static Text_t quoted_str(const char *str) {
    return Text$quoted(Text$from_str(str), false, Text("\""));
}

static inline Text_t quoted_text(Text_t text) {
    return Text$quoted(text, false, Text("\""));
}

Text_t promote_to_optional(type_t *t, Text_t code)
{
    if (t == PATH_TYPE || t == PATH_TYPE_TYPE) {
        return code;
    } else if (t->tag == IntType) {
        switch (Match(t, IntType)->bits) {
        case TYPE_IBITS8: return Texts("((OptionalInt8_t){.value=", code, "})");
        case TYPE_IBITS16: return Texts("((OptionalInt16_t){.value=", code, "})");
        case TYPE_IBITS32: return Texts("((OptionalInt32_t){.value=", code, "})");
        case TYPE_IBITS64: return Texts("((OptionalInt64_t){.value=", code, "})");
        default: errx(1, "Unsupported in type: ", type_to_str(t));
        }
        return code;
    } else if (t->tag == ByteType) {
        return Texts("((OptionalByte_t){.value=", code, "})");
    } else if (t->tag == StructType) {
        return Texts("({ ", compile_type(Type(OptionalType, .type=t)), " nonnull = {.value=", code, "}; nonnull.is_none = false; nonnull; })");
    } else {
        return code;
    }
}

static Text_t with_source_info(env_t *env, ast_t *ast, Text_t code)
{
    if (code.length == 0 || !ast || !ast->file || !env->do_source_mapping)
        return code;
    int64_t line = get_line_number(ast->file, ast->start);
    return Texts("\n#line ", String(line), "\n", code);
}

static bool promote(env_t *env, ast_t *ast, Text_t *code, type_t *actual, type_t *needed)
{
    if (type_eq(actual, needed))
        return true;

    if (!can_promote(actual, needed))
        return false;

    if (needed->tag == ClosureType && actual->tag == FunctionType) {
        *code = Texts("((Closure_t){", *code, ", NULL})");
        return true;
    }

    // Empty promotion:
    type_t *more_complete = most_complete_type(actual, needed);
    if (more_complete)
        return true;

    // Optional promotion:
    if (needed->tag == OptionalType && type_eq(actual, Match(needed, OptionalType)->type)) {
        *code = promote_to_optional(actual, *code);
        return true;
    }

    // Optional -> Bool promotion
    if (actual->tag == OptionalType && needed->tag == BoolType) {
        *code = Texts("(!", check_none(actual, *code), ")");
        return true;
    }

    // Lang to Text_t:
    if (actual->tag == TextType && needed->tag == TextType && streq(Match(needed, TextType)->lang, "Text"))
        return true;

    // Automatic optional checking for nums:
    if (needed->tag == NumType && actual->tag == OptionalType && Match(actual, OptionalType)->type->tag == NumType) {
        int64_t line = get_line_number(ast->file, ast->start);
        *code = Texts("({ ", compile_declaration(actual, Text("opt")), " = ", *code, "; ",
                         "if unlikely (", check_none(actual, Text("opt")), ")\n",
                         "#line ", String(line), "\n",
                         "fail_source(", quoted_str(ast->file->filename), ", ",
                         String((int64_t)(ast->start - ast->file->text)), ", ",
                         String((int64_t)(ast->end - ast->file->text)), ", ",
                         "\"This was expected to be a value, but it's none\");\n",
                         optional_into_nonnone(actual, Text("opt")), "; })");
        return true;
    }

    // Numeric promotions/demotions
    if ((is_numeric_type(actual) || actual->tag == BoolType) && (is_numeric_type(needed) || needed->tag == BoolType)) {
        arg_ast_t *args = new(arg_ast_t, .value=LiteralCode(*code, .type=actual));
        binding_t *constructor = get_constructor(env, needed, args,
                                                 env->current_type != NULL && type_eq(env->current_type, value_type(needed)));
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
        if (!promote(env, ast, code, actual, Match(b->type, FunctionType)->args->type))
            return false;
        *code = Texts(b->code, "(", *code, ")");
        return true;
    }

    // Text_t to C String
    if (actual->tag == TextType && type_eq(actual, TEXT_TYPE) && needed->tag == CStringType) {
        *code = Texts("Text$as_c_string(", *code, ")");
        return true;
    }

    // Automatic dereferencing:
    if (actual->tag == PointerType
        && can_promote(Match(actual, PointerType)->pointed, needed)) {
        *code = Texts("*(", *code, ")");
        return promote(env, ast, code, Match(actual, PointerType)->pointed, needed);
    }

    // Stack ref promotion:
    if (actual->tag == PointerType && needed->tag == PointerType)
        return true;

    // Cross-promotion between tables with default values and without
    if (needed->tag == TableType && actual->tag == TableType)
        return true;

    if (needed->tag == ClosureType && actual->tag == ClosureType)
        return true;

    if (needed->tag == FunctionType && actual->tag == FunctionType) {
        *code = Texts("(", compile_type(needed), ")", *code);
        return true;
    }

    // Set -> List promotion:
    if (needed->tag == ListType && actual->tag == SetType
        && type_eq(Match(needed, ListType)->item_type, Match(actual, SetType)->item_type)) {
        *code = Texts("(", *code, ").entries");
        return true;
    }

    return false;
}

Text_t compile_maybe_incref(env_t *env, ast_t *ast, type_t *t)
{
    if (is_idempotent(ast) && can_be_mutated(env, ast)) {
        if (t->tag == ListType)
            return Texts("LIST_COPY(", compile_to_type(env, ast, t), ")");
        else if (t->tag == TableType || t->tag == SetType)
            return Texts("TABLE_COPY(", compile_to_type(env, ast, t), ")");
    }
    return compile_to_type(env, ast, t);
}

static void add_closed_vars(Table_t *closed_vars, env_t *enclosing_scope, env_t *env, ast_t *ast)
{
    if (ast == NULL)
        return;

    switch (ast->tag) {
    case Var: {
        binding_t *b = get_binding(enclosing_scope, Match(ast, Var)->name);
        if (b) {
            binding_t *shadow = get_binding(env, Match(ast, Var)->name);
            if (!shadow || shadow == b)
                Table$str_set(closed_vars, Match(ast, Var)->name, b);
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
    case Not: case Negative: case HeapAllocate: case StackReference: {
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
            ast_t *body = comp->filter ? WrapAST(ast, If, .condition=comp->filter, .body=comp->expr) : comp->expr;
            ast_t *loop = WrapAST(ast, For, .vars=comp->vars, .iter=comp->iter, .body=body);
            return add_closed_vars(closed_vars, enclosing_scope, env, loop);
        }

        // List/Set/Table comprehension:
        ast_t *body = comp->expr;
        if (comp->filter)
            body = WrapAST(comp->expr, If, .condition=comp->filter, .body=body);
        ast_t *loop = WrapAST(ast, For, .vars=comp->vars, .iter=comp->iter, .body=body);
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
                set_binding(truthy_scope, Match(var, Var)->name,
                            Match(cond_t, OptionalType)->type, EMPTY_TEXT);
            }
            add_closed_vars(closed_vars, enclosing_scope, truthy_scope, if_->body);
            add_closed_vars(closed_vars, enclosing_scope, env, if_->else_body);
        } else {
            add_closed_vars(closed_vars, enclosing_scope, env, condition);
            env_t *truthy_scope = env;
            type_t *cond_t = get_type(env, condition);
            if (condition->tag == Var && cond_t->tag == OptionalType) {
                truthy_scope = fresh_scope(env);
                set_binding(truthy_scope, Match(condition, Var)->name,
                            Match(cond_t, OptionalType)->type, EMPTY_TEXT);
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

            if (when->else_body)
                add_closed_vars(closed_vars, enclosing_scope, env, when->else_body);
            return;
        }

        DeclareMatch(enum_t, subject_t, EnumType);
        for (when_clause_t *clause = when->clauses; clause; clause = clause->next) {
            const char *clause_tag_name;
            if (clause->pattern->tag == Var)
                clause_tag_name = Match(clause->pattern, Var)->name;
            else if (clause->pattern->tag == FunctionCall && Match(clause->pattern, FunctionCall)->fn->tag == Var)
                clause_tag_name = Match(Match(clause->pattern, FunctionCall)->fn, Var)->name;
            else
                code_err(clause->pattern, "This is not a valid pattern for a ", type_to_str(subject_t), " enum");

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
        if (when->else_body)
            add_closed_vars(closed_vars, enclosing_scope, env, when->else_body);
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
        ast_t *loop = FakeAST(For, .vars=new(ast_list_t, .ast=item), .iter=reduction->iter, .body=FakeAST(Pass));
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
    case Use: case FunctionDef: case ConvertDef: case StructDef: case EnumDef: case LangDef: case Extend: {
        errx(1, "Definitions should not be reachable in a closure.");
    }
    default:
        break;
    }
}

static Table_t get_closed_vars(env_t *env, arg_ast_t *args, ast_t *block)
{
    env_t *body_scope = fresh_scope(env);
    for (arg_ast_t *arg = args; arg; arg = arg->next) {
        type_t *arg_type = get_arg_ast_type(env, arg);
        set_binding(body_scope, arg->name, arg_type, Texts("_$", arg->name));
    }

    Table_t closed_vars = {};
    add_closed_vars(&closed_vars, env, body_scope, block);
    return closed_vars;
}

Text_t compile_declaration(type_t *t, Text_t name)
{
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

static Text_t compile_update_assignment(env_t *env, ast_t *ast)
{
    if (!is_update_assignment(ast))
        code_err(ast, "This is not an update assignment");

    binary_operands_t update = UPDATE_OPERANDS(ast);

    type_t *lhs_t = get_type(env, update.lhs);

    bool needs_idemotency_fix = !is_idempotent(update.lhs);
    Text_t lhs = needs_idemotency_fix ? Text("(*lhs)") : compile_lvalue(env, update.lhs);

    Text_t update_assignment = EMPTY_TEXT;
    switch (ast->tag) {
    case PlusUpdate: {
        if (lhs_t->tag == IntType || lhs_t->tag == NumType || lhs_t->tag == ByteType)
            update_assignment = Texts(lhs, " += ", compile_to_type(env, update.rhs, lhs_t), ";");
        break;
    }
    case MinusUpdate: {
        if (lhs_t->tag == IntType || lhs_t->tag == NumType || lhs_t->tag == ByteType)
            update_assignment = Texts(lhs, " -= ", compile_to_type(env, update.rhs, lhs_t), ";");
        break;
    }
    case MultiplyUpdate: {
        if (lhs_t->tag == IntType || lhs_t->tag == NumType || lhs_t->tag == ByteType)
            update_assignment = Texts(lhs, " *= ", compile_to_type(env, update.rhs, lhs_t), ";");
        break;
    }
    case DivideUpdate: {
        if (lhs_t->tag == IntType || lhs_t->tag == NumType || lhs_t->tag == ByteType)
            update_assignment = Texts(lhs, " /= ", compile_to_type(env, update.rhs, lhs_t), ";");
        break;
    }
    case LeftShiftUpdate: {
        if (lhs_t->tag == IntType || lhs_t->tag == ByteType)
            update_assignment = Texts(lhs, " <<= ", compile_to_type(env, update.rhs, lhs_t), ";");
        break;
    }
    case RightShiftUpdate: {
        if (lhs_t->tag == IntType || lhs_t->tag == ByteType)
            update_assignment = Texts(lhs, " >>= ", compile_to_type(env, update.rhs, lhs_t), ";");
        break;
    }
    case AndUpdate: {
        if (lhs_t->tag == BoolType)
            update_assignment = Texts("if (", lhs, ") ", lhs, " = ", compile_to_type(env, update.rhs, Type(BoolType)), ";");
        break;
    }
    case OrUpdate: {
        if (lhs_t->tag == BoolType)
            update_assignment = Texts("if (!", lhs, ") ", lhs, " = ", compile_to_type(env, update.rhs, Type(BoolType)), ";");
        break;
    }
    default: break;
    }

    if (update_assignment.length == 0) {
        ast_t *binop = new(ast_t);
        *binop = *ast;
        binop->tag = binop_tag(binop->tag);
        if (needs_idemotency_fix)
            binop->__data.Plus.lhs = LiteralCode(Text("*lhs"), .type=lhs_t);
        update_assignment = Texts(lhs, " = ", compile_to_type(env, binop, lhs_t), ";");
    }
    
    if (needs_idemotency_fix)
        return Texts("{ ", compile_declaration(Type(PointerType, .pointed=lhs_t), Text("lhs")), " = &", compile_lvalue(env, update.lhs), "; ",
                        update_assignment, "; }");
    else
        return update_assignment;
}

static Text_t compile_binary_op(env_t *env, ast_t *ast)
{
    binary_operands_t binop = BINARY_OPERANDS(ast);
    type_t *lhs_t = get_type(env, binop.lhs);
    type_t *rhs_t = get_type(env, binop.rhs);
    type_t *overall_t = get_type(env, ast);

    binding_t *b = get_metamethod_binding(env, ast->tag, binop.lhs, binop.rhs, overall_t);
    if (!b) b = get_metamethod_binding(env, ast->tag, binop.rhs, binop.lhs, overall_t);
    if (b) {
        arg_ast_t *args = new(arg_ast_t, .value=binop.lhs, .next=new(arg_ast_t, .value=binop.rhs));
        DeclareMatch(fn, b->type, FunctionType);
        return Texts(b->code, "(", compile_arguments(env, ast, fn->args, args), ")");
    }

    if (ast->tag == Multiply && is_numeric_type(lhs_t)) {
        b = get_namespace_binding(env, binop.rhs, "scaled_by");
        if (b && b->type->tag == FunctionType) {
            DeclareMatch(fn, b->type, FunctionType);
            if (type_eq(fn->ret, rhs_t)) {
                arg_ast_t *args = new(arg_ast_t, .value=binop.rhs, .next=new(arg_ast_t, .value=binop.lhs));
                if (is_valid_call(env, fn->args, args, true))
                    return Texts(b->code, "(", compile_arguments(env, ast, fn->args, args), ")");
            }
        }
    } else if (ast->tag == Multiply && is_numeric_type(rhs_t)) {
        b = get_namespace_binding(env, binop.lhs, "scaled_by");
        if (b && b->type->tag == FunctionType) {
            DeclareMatch(fn, b->type, FunctionType);
            if (type_eq(fn->ret, lhs_t)) {
                arg_ast_t *args = new(arg_ast_t, .value=binop.lhs, .next=new(arg_ast_t, .value=binop.rhs));
                if (is_valid_call(env, fn->args, args, true))
                    return Texts(b->code, "(", compile_arguments(env, ast, fn->args, args), ")");
            }
        }
    } else if (ast->tag == Divide && is_numeric_type(rhs_t)) {
        b = get_namespace_binding(env, binop.lhs, "divided_by");
        if (b && b->type->tag == FunctionType) {
            DeclareMatch(fn, b->type, FunctionType);
            if (type_eq(fn->ret, lhs_t)) {
                arg_ast_t *args = new(arg_ast_t, .value=binop.lhs, .next=new(arg_ast_t, .value=binop.rhs));
                if (is_valid_call(env, fn->args, args, true))
                    return Texts(b->code, "(", compile_arguments(env, ast, fn->args, args), ")");
            }
        }
    } else if ((ast->tag == Divide || ast->tag == Mod || ast->tag == Mod1) && is_numeric_type(rhs_t)) {
        b = get_namespace_binding(env, binop.lhs, binop_method_name(ast->tag));
        if (b && b->type->tag == FunctionType) {
            DeclareMatch(fn, b->type, FunctionType);
            if (type_eq(fn->ret, lhs_t)) {
                arg_ast_t *args = new(arg_ast_t, .value=binop.lhs, .next=new(arg_ast_t, .value=binop.rhs));
                if (is_valid_call(env, fn->args, args, true))
                    return Texts(b->code, "(", compile_arguments(env, ast, fn->args, args), ")");
            }
        }
    }

    if (ast->tag == Or && lhs_t->tag == OptionalType) {
        if (rhs_t->tag == AbortType || rhs_t->tag == ReturnType) {
            return Texts("({ ", compile_declaration(lhs_t, Text("lhs")), " = ", compile(env, binop.lhs), "; ",
                            "if (", check_none(lhs_t, Text("lhs")), ") ", compile_statement(env, binop.rhs), " ",
                            optional_into_nonnone(lhs_t, Text("lhs")), "; })");
        }

        if (is_incomplete_type(rhs_t)) {
            type_t *complete = most_complete_type(rhs_t, Match(lhs_t, OptionalType)->type);
            if (complete == NULL)
                code_err(binop.rhs, "I don't know how to convert a ", type_to_str(rhs_t), " to a ", type_to_str(Match(lhs_t, OptionalType)->type));
            rhs_t = complete;
        }

        if (rhs_t->tag == OptionalType && type_eq(lhs_t, rhs_t)) {
            return Texts("({ ", compile_declaration(lhs_t, Text("lhs")), " = ", compile(env, binop.lhs), "; ",
                            check_none(lhs_t, Text("lhs")), " ? ", compile(env, binop.rhs), " : lhs; })");
        } else if (rhs_t->tag != OptionalType && type_eq(Match(lhs_t, OptionalType)->type, rhs_t)) {
            return Texts("({ ", compile_declaration(lhs_t, Text("lhs")), " = ", compile(env, binop.lhs), "; ",
                            check_none(lhs_t, Text("lhs")), " ? ", compile(env, binop.rhs), " : ",
                            optional_into_nonnone(lhs_t, Text("lhs")), "; })");
        } else if (rhs_t->tag == BoolType) {
            return Texts("((!", check_none(lhs_t, compile(env, binop.lhs)), ") || ", compile(env, binop.rhs), ")");
        } else {
            code_err(ast, "I don't know how to do an 'or' operation between ", type_to_str(lhs_t), " and ", type_to_str(rhs_t));
        }
    }

    Text_t lhs = compile_to_type(env, binop.lhs, overall_t);
    Text_t rhs = compile_to_type(env, binop.rhs, overall_t);

    switch (ast->tag) {
    case Power: {
        if (overall_t->tag != NumType)
            code_err(ast, "Exponentiation is only supported for Num types, not ", type_to_str(overall_t));
        if (overall_t->tag == NumType && Match(overall_t, NumType)->bits == TYPE_NBITS32)
            return Texts("powf(", lhs, ", ", rhs, ")");
        else
            return Texts("pow(", lhs, ", ", rhs, ")");
    }
    case Multiply: {
        if (overall_t->tag != IntType && overall_t->tag != NumType && overall_t->tag != ByteType)
            code_err(ast, "Math operations are only supported for values of the same numeric type, not ", type_to_str(lhs_t), " and ", type_to_str(rhs_t));
        return Texts("(", lhs, " * ", rhs, ")");
    }
    case Divide: {
        if (overall_t->tag != IntType && overall_t->tag != NumType && overall_t->tag != ByteType)
            code_err(ast, "Math operations are only supported for values of the same numeric type, not ", type_to_str(lhs_t), " and ", type_to_str(rhs_t));
        return Texts("(", lhs, " / ", rhs, ")");
    }
    case Mod: {
        if (overall_t->tag != IntType && overall_t->tag != NumType && overall_t->tag != ByteType)
            code_err(ast, "Math operations are only supported for values of the same numeric type, not ", type_to_str(lhs_t), " and ", type_to_str(rhs_t));
        return Texts("(", lhs, " % ", rhs, ")");
    }
    case Mod1: {
        if (overall_t->tag != IntType && overall_t->tag != NumType && overall_t->tag != ByteType)
            code_err(ast, "Math operations are only supported for values of the same numeric type, not ", type_to_str(lhs_t), " and ", type_to_str(rhs_t));
        return Texts("((((", lhs, ")-1) % (", rhs, ")) + 1)");
    }
    case Plus: {
        if (overall_t->tag != IntType && overall_t->tag != NumType && overall_t->tag != ByteType)
            code_err(ast, "Math operations are only supported for values of the same numeric type, not ", type_to_str(lhs_t), " and ", type_to_str(rhs_t));
        return Texts("(", lhs, " + ", rhs, ")");
    }
    case Minus: {
        if (overall_t->tag == SetType)
            return Texts("Table$without(", lhs, ", ", rhs, ", ", compile_type_info(overall_t), ")");
        if (overall_t->tag != IntType && overall_t->tag != NumType && overall_t->tag != ByteType)
            code_err(ast, "Math operations are only supported for values of the same numeric type, not ", type_to_str(lhs_t), " and ", type_to_str(rhs_t));
        return Texts("(", lhs, " - ", rhs, ")");
    }
    case LeftShift: {
        if (overall_t->tag != IntType && overall_t->tag != NumType && overall_t->tag != ByteType)
            code_err(ast, "Math operations are only supported for values of the same numeric type, not ", type_to_str(lhs_t), " and ", type_to_str(rhs_t));
        return Texts("(", lhs, " << ", rhs, ")");
    }
    case RightShift: {
        if (overall_t->tag != IntType && overall_t->tag != NumType && overall_t->tag != ByteType)
            code_err(ast, "Math operations are only supported for values of the same numeric type, not ", type_to_str(lhs_t), " and ", type_to_str(rhs_t));
        return Texts("(", lhs, " >> ", rhs, ")");
    }
    case UnsignedLeftShift: {
        if (overall_t->tag != IntType && overall_t->tag != NumType && overall_t->tag != ByteType)
            code_err(ast, "Math operations are only supported for values of the same numeric type, not ", type_to_str(lhs_t), " and ", type_to_str(rhs_t));
        return Texts("(", compile_type(overall_t), ")((", compile_unsigned_type(lhs_t), ")", lhs, " << ", rhs, ")");
    }
    case UnsignedRightShift: {
        if (overall_t->tag != IntType && overall_t->tag != NumType && overall_t->tag != ByteType)
            code_err(ast, "Math operations are only supported for values of the same numeric type, not ", type_to_str(lhs_t), " and ", type_to_str(rhs_t));
        return Texts("(", compile_type(overall_t), ")((", compile_unsigned_type(lhs_t), ")", lhs, " >> ", rhs, ")");
    }
    case And: {
        if (overall_t->tag == BoolType)
            return Texts("(", lhs, " && ", rhs, ")");
        else if (overall_t->tag == IntType || overall_t->tag == ByteType)
            return Texts("(", lhs, " & ", rhs, ")");
        else if (overall_t->tag == SetType)
            return Texts("Table$overlap(", lhs, ", ", rhs, ", ", compile_type_info(overall_t), ")");
        else
            code_err(ast, "The 'and' operator isn't supported between ", type_to_str(lhs_t), " and ", type_to_str(rhs_t), " values");
    }
    case Compare: {
        return Texts("generic_compare(stack(", lhs, "), stack(", rhs, "), ", compile_type_info(overall_t), ")");
    }
    case Or: {
        if (overall_t->tag == BoolType) {
            return Texts("(", lhs, " || ", rhs, ")");
        } else if (overall_t->tag == IntType || overall_t->tag == ByteType) {
            return Texts("(", lhs, " | ", rhs, ")");
        } else if (overall_t->tag == SetType) {
            return Texts("Table$with(", lhs, ", ", rhs, ", ", compile_type_info(overall_t), ")");
        } else {
            code_err(ast, "The 'or' operator isn't supported between ", type_to_str(lhs_t), " and ", type_to_str(rhs_t), " values");
        }
    }
    case Xor: {
        // TODO: support optional values in `xor` expressions
        if (overall_t->tag == BoolType || overall_t->tag == IntType || overall_t->tag == ByteType)
            return Texts("(", lhs, " ^ ", rhs, ")");
        else if (overall_t->tag == SetType)
            return Texts("Table$xor(", lhs, ", ", rhs, ", ", compile_type_info(overall_t), ")");
        else
            code_err(ast, "The 'xor' operator isn't supported between ", type_to_str(lhs_t), " and ", type_to_str(rhs_t), " values");
    }
    case Concat: {
        if (overall_t == PATH_TYPE)
            return Texts("Path$concat(", lhs, ", ", rhs, ")");
        switch (overall_t->tag) {
        case TextType: {
            return Texts("Text$concat(", lhs, ", ", rhs, ")");
        }
        case ListType: {
            return Texts("List$concat(", lhs, ", ", rhs, ", sizeof(", compile_type(Match(overall_t, ListType)->item_type), "))");
        }
        default:
            code_err(ast, "Concatenation isn't supported between ", type_to_str(lhs_t), " and ", type_to_str(rhs_t), " values");
        }
    }
    default: errx(1, "Not a valid binary operation: ", ast_to_sexp_str(ast));
    }
    return EMPTY_TEXT;
}

PUREFUNC Text_t compile_unsigned_type(type_t *t)
{
    if (t->tag != IntType)
        errx(1, "Not an int type, so unsigned doesn't make sense!");
    switch (Match(t, IntType)->bits) {
    case TYPE_IBITS8: return Text("uint8_t");
    case TYPE_IBITS16: return Text("uint16_t");
    case TYPE_IBITS32: return Text("uint32_t");
    case TYPE_IBITS64: return Text("uint64_t");
    default: errx(1, "Invalid integer bit size");
    }
    return EMPTY_TEXT;
}

Text_t compile_type(type_t *t)
{
    if (t == PATH_TYPE) return Text("Path_t");
    else if (t == PATH_TYPE_TYPE) return Text("PathType_t");

    switch (t->tag) {
    case ReturnType: errx(1, "Shouldn't be compiling ReturnType to a type");
    case AbortType: return Text("void");
    case VoidType: return Text("void");
    case MemoryType: return Text("void");
    case BoolType: return Text("Bool_t");
    case ByteType: return Text("Byte_t");
    case CStringType: return Text("const char*");
    case BigIntType: return Text("Int_t");
    case IntType: return Texts("Int", String(Match(t, IntType)->bits), "_t");
    case NumType: return Match(t, NumType)->bits == TYPE_NBITS64 ? Text("Num_t") : Texts("Num", String(Match(t, NumType)->bits), "_t");
    case TextType: {
        DeclareMatch(text, t, TextType);
        if (!text->lang || streq(text->lang, "Text"))
            return Text("Text_t");
        else
            return namespace_name(text->env, text->env->namespace, Text("$type"));
    }
    case ListType: return Text("List_t");
    case SetType: return Text("Table_t");
    case TableType: return Text("Table_t");
    case FunctionType: {
        DeclareMatch(fn, t, FunctionType);
        Text_t code = Texts(compile_type(fn->ret), " (*)(");
        for (arg_t *arg = fn->args; arg; arg = arg->next) {
            code = Texts(code, compile_type(arg->type));
            if (arg->next) code = Texts(code, ", ");
        }
        if (!fn->args)
            code = Texts(code, "void");
        return Texts(code, ")");
    }
    case ClosureType: return Text("Closure_t");
    case PointerType: return Texts(compile_type(Match(t, PointerType)->pointed), "*");
    case StructType: {
        DeclareMatch(s, t, StructType);
        if (s->external) return Text$from_str(s->name);
        return Texts("struct ", namespace_name(s->env, s->env->namespace, Text("$struct")));
    }
    case EnumType: {
        DeclareMatch(e, t, EnumType);
        return namespace_name(e->env, e->env->namespace, Text("$type"));
    }
    case OptionalType: {
        type_t *nonnull = Match(t, OptionalType)->type;
        switch (nonnull->tag) {
        case CStringType: case FunctionType: case ClosureType:
        case PointerType: case EnumType:
            return compile_type(nonnull);
        case TextType:
            return Match(nonnull, TextType)->lang ? compile_type(nonnull) : Text("OptionalText_t");
        case IntType: case BigIntType: case NumType: case BoolType: case ByteType:
        case ListType: case TableType: case SetType:
            return Texts("Optional", compile_type(nonnull));
        case StructType: {
            if (nonnull == PATH_TYPE)
                return Text("OptionalPath_t");
            if (nonnull == PATH_TYPE_TYPE)
                return Text("OptionalPathType_t");
            DeclareMatch(s, nonnull, StructType);
            return namespace_name(s->env, s->env->namespace->parent, Texts("$Optional", s->name, "$$type"));
        }
        default:
            compiler_err(NULL, NULL, NULL, "Optional types are not supported for: ", type_to_str(t));
        }
    }
    case TypeInfoType: return Text("TypeInfo_t");
    default: compiler_err(NULL, NULL, NULL, "Compiling type is not implemented for type with tag ", t->tag);
    }
    return EMPTY_TEXT;
}

Text_t compile_lvalue(env_t *env, ast_t *ast)
{
    if (!can_be_mutated(env, ast)) {
        if (ast->tag == Index) {
            ast_t *subject = Match(ast, Index)->indexed;
            code_err(subject, "This is an immutable value, you can't mutate its contents");
        } else if (ast->tag == FieldAccess) {
            ast_t *subject = Match(ast, FieldAccess)->fielded;
            type_t *t = get_type(env, subject);
            code_err(subject, "This is an immutable ", type_to_str(t), " value, you can't assign to its fields");
        } else {
            code_err(ast, "This is a value of type ", type_to_str(get_type(env, ast)), " and can't be used as an assignment target");
        }
    }

    if (ast->tag == Index) {
        DeclareMatch(index, ast, Index);
        type_t *container_t = get_type(env, index->indexed);
        if (container_t->tag == OptionalType)
            code_err(index->indexed, "This value might be none, so it can't be safely used as an assignment target");

        if (!index->index && container_t->tag == PointerType)
            return compile(env, ast);

        container_t = value_type(container_t);
        type_t *index_t = get_type(env, index->index);
        if (container_t->tag == ListType) {
            Text_t target_code = compile_to_pointer_depth(env, index->indexed, 1, false);
            type_t *item_type = Match(container_t, ListType)->item_type;
            Text_t index_code = index->index->tag == Int
                ? compile_int_to_type(env, index->index, Type(IntType, .bits=TYPE_IBITS64))
                : (index_t->tag == BigIntType ? Texts("Int64$from_int(", compile(env, index->index), ", no)")
                   : Texts("(Int64_t)(", compile(env, index->index), ")"));
            if (index->unchecked) {
                return Texts("List_lvalue_unchecked(", compile_type(item_type), ", ", target_code, ", ", 
                                index_code, ")");
            } else {
                return Texts("List_lvalue(", compile_type(item_type), ", ", target_code, ", ", 
                                index_code,
                                ", ", String((int)(ast->start - ast->file->text)),
                                ", ", String((int)(ast->end - ast->file->text)), ")");
            }
        } else if (container_t->tag == TableType) {
            DeclareMatch(table_type, container_t, TableType);
            if (table_type->default_value) {
                type_t *value_type = get_type(env, table_type->default_value);
                return Texts("*Table$get_or_setdefault(",
                                compile_to_pointer_depth(env, index->indexed, 1, false), ", ",
                                compile_type(table_type->key_type), ", ",
                                compile_type(value_type), ", ",
                                compile_to_type(env, index->index, table_type->key_type), ", ",
                                compile_to_type(env, table_type->default_value, table_type->value_type), ", ",
                                compile_type_info(container_t), ")");
            }
            if (index->unchecked)
                code_err(ast, "Table indexes cannot be unchecked");
            return Texts("*(", compile_type(Type(PointerType, table_type->value_type)), ")Table$reserve(",
                            compile_to_pointer_depth(env, index->indexed, 1, false), ", ",
                            compile_to_type(env, index->index, Type(PointerType, table_type->key_type, .is_stack=true)), ", NULL,",
                            compile_type_info(container_t), ")");
        } else {
            code_err(ast, "I don't know how to assign to this target");
        }
    } else if (ast->tag == Var || ast->tag == FieldAccess || ast->tag == InlineCCode) {
        return compile(env, ast);
    } else {
        code_err(ast, "I don't know how to assign to this");
    }
    return EMPTY_TEXT;
}

static Text_t compile_assignment(env_t *env, ast_t *target, Text_t value)
{
    return Texts(compile_lvalue(env, target), " = ", value);
}

static Text_t compile_inline_block(env_t *env, ast_t *ast)
{
    if (ast->tag != Block)
        return compile_statement(env, ast);

    Text_t code = EMPTY_TEXT;
    ast_list_t *stmts = Match(ast, Block)->statements;
    deferral_t *prev_deferred = env->deferred;
    env = fresh_scope(env);
    for (ast_list_t *stmt = stmts; stmt; stmt = stmt->next)
        prebind_statement(env, stmt->ast);
    for (ast_list_t *stmt = stmts; stmt; stmt = stmt->next) {
        code = Texts(code, compile_statement(env, stmt->ast), "\n");
        bind_statement(env, stmt->ast);
    }
    for (deferral_t *deferred = env->deferred; deferred && deferred != prev_deferred; deferred = deferred->next) {
        code = Texts(code, compile_statement(deferred->defer_env, deferred->block));
    }
    return code;
}

Text_t optional_into_nonnone(type_t *t, Text_t value)
{
    if (t->tag == OptionalType) t = Match(t, OptionalType)->type;
    switch (t->tag) {
    case IntType: case ByteType:
        return Texts(value, ".value");
    case StructType:
        if (t == PATH_TYPE || t == PATH_TYPE_TYPE)
            return value;
        return Texts(value, ".value");
    default:
        return value;
    }
}

Text_t check_none(type_t *t, Text_t value)
{
    t = Match(t, OptionalType)->type;
    // NOTE: these use statement expressions ({...;}) because some compilers
    // complain about excessive parens around equality comparisons
    if (t->tag == PointerType || t->tag == FunctionType || t->tag == CStringType)
        return Texts("({", value, " == NULL;})");
    else if (t == PATH_TYPE)
        return Texts("({(", value, ").type.$tag == PATH_NONE;})");
    else if (t == PATH_TYPE_TYPE)
        return Texts("({(", value, ").$tag == PATH_NONE;})");
    else if (t->tag == BigIntType)
        return Texts("({(", value, ").small == 0;})");
    else if (t->tag == ClosureType)
        return Texts("({(", value, ").fn == NULL;})");
    else if (t->tag == NumType)
        return Texts("isnan(", value, ")");
    else if (t->tag == ListType)
        return Texts("({(", value, ").length < 0;})");
    else if (t->tag == TableType || t->tag == SetType)
        return Texts("({(", value, ").entries.length < 0;})");
    else if (t->tag == BoolType)
        return Texts("({(", value, ") == NONE_BOOL;})");
    else if (t->tag == TextType)
        return Texts("({(", value, ").length < 0;})");
    else if (t->tag == IntType || t->tag == ByteType || t->tag == StructType)
        return Texts("(", value, ").is_none");
    else if (t->tag == EnumType) {
        if (enum_has_fields(t))
            return Texts("({(", value, ").$tag == 0;})");
        else
            return Texts("((", value, ") == 0)");
    }
    print_err("Optional check not implemented for: ", type_to_str(t));
    return EMPTY_TEXT;
}

static Text_t compile_condition(env_t *env, ast_t *ast)
{
    type_t *t = get_type(env, ast);
    if (t->tag == BoolType) {
        return compile(env, ast);
    } else if (t->tag == TextType) {
        return Texts("(", compile(env, ast), ").length");
    } else if (t->tag == ListType) {
        return Texts("(", compile(env, ast), ").length");
    } else if (t->tag == TableType || t->tag == SetType) {
        return Texts("(", compile(env, ast), ").entries.length");
    } else if (t->tag == OptionalType) {
        return Texts("!", check_none(t, compile(env, ast)));
    } else if (t->tag == PointerType) {
        code_err(ast, "This pointer will always be non-none, so it should not be used in a conditional.");
    } else {
        code_err(ast, type_to_str(t), " values cannot be used for conditionals");
    }
    return EMPTY_TEXT;
}

static Text_t _compile_statement(env_t *env, ast_t *ast)
{
    switch (ast->tag) {
    case When: {
        // Typecheck to verify exhaustiveness:
        type_t *result_t = get_type(env, ast);
        (void)result_t;

        DeclareMatch(when, ast, When);
        type_t *subject_t = get_type(env, when->subject);

        if (subject_t->tag != EnumType) {
            Text_t prefix = EMPTY_TEXT, suffix = EMPTY_TEXT;
            ast_t *subject = when->subject;
            if (!is_idempotent(when->subject)) {
                prefix = Texts("{\n", compile_declaration(subject_t, Text("_when_subject")), " = ", compile(env, subject), ";\n");
                suffix = Text("}\n");
                subject = LiteralCode(Text("_when_subject"), .type=subject_t);
            }

            Text_t code = EMPTY_TEXT;
            for (when_clause_t *clause = when->clauses; clause; clause = clause->next) {
                ast_t *comparison = WrapAST(clause->pattern, Equals, .lhs=subject, .rhs=clause->pattern);
                (void)get_type(env, comparison);
                if (code.length > 0)
                    code = Texts(code, "else ");
                code = Texts(code, "if (", compile(env, comparison), ")", compile_statement(env, clause->body));
            }
            if (when->else_body)
                code = Texts(code, "else ", compile_statement(env, when->else_body));
            code = Texts(prefix, code, suffix);
            return code;
        }

        DeclareMatch(enum_t, subject_t, EnumType);

        Text_t code;
        if (enum_has_fields(subject_t))
            code = Texts("WHEN(", compile_type(subject_t), ", ", compile(env, when->subject), ", _when_subject, {\n");
        else
            code = Texts("switch(", compile(env, when->subject), ") {\n");

        for (when_clause_t *clause = when->clauses; clause; clause = clause->next) {
            if (clause->pattern->tag == Var) {
                const char *clause_tag_name = Match(clause->pattern, Var)->name;
                type_t *clause_type = clause->body ? get_type(env, clause->body) : Type(VoidType);
                code = Texts(code, "case ", namespace_name(enum_t->env, enum_t->env->namespace, Texts("tag$", clause_tag_name)), ": {\n",
                                compile_inline_block(env, clause->body),
                                (clause_type->tag == ReturnType || clause_type->tag == AbortType) ? EMPTY_TEXT : Text("break;\n"),
                                "}\n");
                continue;
            }

            if (clause->pattern->tag != FunctionCall || Match(clause->pattern, FunctionCall)->fn->tag != Var)
                code_err(clause->pattern, "This is not a valid pattern for a ", type_to_str(subject_t), " enum type");

            const char *clause_tag_name = Match(Match(clause->pattern, FunctionCall)->fn, Var)->name;
            code = Texts(code, "case ", namespace_name(enum_t->env, enum_t->env->namespace, Texts("tag$", clause_tag_name)), ": {\n");
            type_t *tag_type = NULL;
            for (tag_t *tag = enum_t->tags; tag; tag = tag->next) {
                if (streq(tag->name, clause_tag_name)) {
                    tag_type = tag->type;
                    break;
                }
            }
            assert(tag_type);
            env_t *scope = env;

            DeclareMatch(tag_struct, tag_type, StructType);
            arg_ast_t *args = Match(clause->pattern, FunctionCall)->args;
            if (args && !args->next && tag_struct->fields && tag_struct->fields->next) {
                if (args->value->tag != Var)
                    code_err(args->value, "This is not a valid variable to bind to");
                const char *var_name = Match(args->value, Var)->name;
                if (!streq(var_name, "_")) {
                    Text_t var = Texts("_$", var_name);
                    code = Texts(code, compile_declaration(tag_type, var), " = _when_subject.", clause_tag_name, ";\n");
                    scope = fresh_scope(scope);
                    set_binding(scope, Match(args->value, Var)->name, tag_type, EMPTY_TEXT);
                }
            } else if (args) {
                scope = fresh_scope(scope);
                arg_t *field = tag_struct->fields;
                for (arg_ast_t *arg = args; arg || field; arg = arg->next) {
                    if (!arg)
                        code_err(ast, "The field ", type_to_str(subject_t), ".", clause_tag_name, ".", field->name, " wasn't accounted for");
                    if (!field)
                        code_err(arg->value, "This is one more field than ", type_to_str(subject_t), " has");
                    if (arg->name)
                        code_err(arg->value, "Named arguments are not currently supported");

                    const char *var_name = Match(arg->value, Var)->name;
                    if (!streq(var_name, "_")) {
                        Text_t var = Texts("_$", var_name);
                        code = Texts(code, compile_declaration(field->type, var), " = _when_subject.", clause_tag_name, ".", field->name, ";\n");
                        set_binding(scope, Match(arg->value, Var)->name, field->type, var);
                    }
                    field = field->next;
                }
            }
            if (clause->body->tag == Block) {
                ast_list_t *statements = Match(clause->body, Block)->statements;
                if (!statements || (statements->ast->tag == Pass && !statements->next))
                    code = Texts(code, "break;\n}\n");
                else
                    code = Texts(code, compile_inline_block(scope, clause->body), "\nbreak;\n}\n");
            } else {
                code = Texts(code, compile_statement(scope, clause->body), "\nbreak;\n}\n");
            }
        }
        if (when->else_body) {
            if (when->else_body->tag == Block) {
                ast_list_t *statements = Match(when->else_body, Block)->statements;
                if (!statements || (statements->ast->tag == Pass && !statements->next))
                    code = Texts(code, "default: break;");
                else
                    code = Texts(code, "default: {\n", compile_inline_block(env, when->else_body), "\nbreak;\n}\n");
            } else {
                code = Texts(code, "default: {\n", compile_statement(env, when->else_body), "\nbreak;\n}\n");
            }
        } else {
            code = Texts(code, "default: errx(1, \"Invalid tag!\");\n");
        }
        code = Texts(code, "\n}", enum_has_fields(subject_t) ? Text(")") : EMPTY_TEXT, "\n");
        return code;
    }
    case DocTest: {
        DeclareMatch(test, ast, DocTest);
        type_t *expr_t = get_type(env, test->expr);
        if (!expr_t)
            code_err(test->expr, "I couldn't figure out the type of this expression");

        Text_t setup = EMPTY_TEXT;
        Text_t test_code;
        if (test->expr->tag == Declare) {
            DeclareMatch(decl, test->expr, Declare);
            type_t *t = decl->type ? parse_type_ast(env, decl->type) : get_type(env, decl->value);
            if (t->tag == FunctionType) t = Type(ClosureType, t);
            Text_t var = Texts("_$", Match(decl->var, Var)->name);
            Text_t val_code = compile_declared_value(env, test->expr);
            setup = Texts(compile_declaration(t, var), ";\n");
            test_code = Texts("(", var, " = ", val_code, ")");
            expr_t = t;
        } else if (test->expr->tag == Assign) {
            DeclareMatch(assign, test->expr, Assign);
            if (!assign->targets->next && assign->targets->ast->tag == Var && is_idempotent(assign->targets->ast)) {
                // Common case: assigning to one variable:
                type_t *lhs_t = get_type(env, assign->targets->ast);
                if (assign->targets->ast->tag == Index && lhs_t->tag == OptionalType
                    && value_type(get_type(env, Match(assign->targets->ast, Index)->indexed))->tag == TableType)
                    lhs_t = Match(lhs_t, OptionalType)->type;
                if (has_stack_memory(lhs_t))
                    code_err(test->expr, "Stack references cannot be assigned to variables because the variable's scope may outlive the scope of the stack memory.");
                env_t *val_scope = with_enum_scope(env, lhs_t);
                Text_t value = compile_to_type(val_scope, assign->values->ast, lhs_t);
                test_code = Texts("(", compile_assignment(env, assign->targets->ast, value), ")");
                expr_t = lhs_t;
            } else {
                // Multi-assign or assignment to potentially non-idempotent targets
                if (test->expected && assign->targets->next)
                    code_err(ast, "Sorry, but doctesting with '=' is not supported for multi-assignments");

                test_code = Text("({ // Assignment\n");

                int64_t i = 1;
                for (ast_list_t *target = assign->targets, *value = assign->values; target && value; target = target->next, value = value->next) {
                    type_t *lhs_t = get_type(env, target->ast);
                    if (target->ast->tag == Index && lhs_t->tag == OptionalType
                        && value_type(get_type(env, Match(target->ast, Index)->indexed))->tag == TableType)
                        lhs_t = Match(lhs_t, OptionalType)->type;
                    if (has_stack_memory(lhs_t))
                        code_err(ast, "Stack references cannot be assigned to variables because the variable's scope may outlive the scope of the stack memory.");
                    if (target == assign->targets)
                        expr_t = lhs_t;
                    env_t *val_scope = with_enum_scope(env, lhs_t);
                    Text_t val_code = compile_to_type(val_scope, value->ast, lhs_t);
                    test_code = Texts(test_code, compile_type(lhs_t), " $", String(i), " = ", val_code, ";\n");
                    i += 1;
                }
                i = 1;
                for (ast_list_t *target = assign->targets; target; target = target->next) {
                    test_code = Texts(test_code, compile_assignment(env, target->ast, Texts("$", String(i))), ";\n");
                    i += 1;
                }

                test_code = Texts(test_code, "$1; })");
            }
        } else if (is_update_assignment(test->expr)) {
            binary_operands_t update = UPDATE_OPERANDS(test->expr);
            type_t *lhs_t = get_type(env, update.lhs);
            if (update.lhs->tag == Index) {
                type_t *indexed = value_type(get_type(env, Match(update.lhs, Index)->indexed));
                if (indexed->tag == TableType && Match(indexed, TableType)->default_value == NULL)
                    code_err(update.lhs, "Update assignments are not currently supported for tables");
            }

            ast_t *update_var = new(ast_t);
            *update_var = *test->expr;
            update_var->__data.PlusUpdate.lhs = LiteralCode(Text("(*expr)"), .type=lhs_t); // UNSAFE
            test_code = Texts("({", 
                compile_declaration(Type(PointerType, lhs_t), Text("expr")), " = &(", compile_lvalue(env, update.lhs), "); ",
                compile_statement(env, update_var), "; *expr; })");
            expr_t = lhs_t;
        } else if (expr_t->tag == VoidType || expr_t->tag == AbortType || expr_t->tag == ReturnType) {
            test_code = Texts("({", compile_statement(env, test->expr), " NULL;})");
        } else {
            test_code = compile(env, test->expr);
        }
        if (test->expected) {
            return Texts(
                setup,
                "test(", compile_type(expr_t), ", ", test_code, ", ",
                compile_to_type(env, test->expected, expr_t), ", ",
                compile_type_info(expr_t), ", ",
                String((int64_t)(test->expr->start - test->expr->file->text)), ", ",
                String((int64_t)(test->expr->end - test->expr->file->text)), ");");
        } else {
            if (expr_t->tag == VoidType || expr_t->tag == AbortType) {
                return Texts(
                    setup,
                    "inspect_void(", test_code, ", ", compile_type_info(expr_t), ", ",
                    String((int64_t)(test->expr->start - test->expr->file->text)), ", ",
                    String((int64_t)(test->expr->end - test->expr->file->text)), ");");
            }
            return Texts(
                setup,
                "inspect(", compile_type(expr_t), ", ", test_code, ", ",
                compile_type_info(expr_t), ", ",
                String((int64_t)(test->expr->start - test->expr->file->text)), ", ",
                String((int64_t)(test->expr->end - test->expr->file->text)), ");");
        }
    }
    case Assert: {
        ast_t *expr = Match(ast, Assert)->expr;
        ast_t *message = Match(ast, Assert)->message;
        const char *failure = NULL;
        switch (expr->tag) {
        case And: {
            DeclareMatch(and_, ast, And);
            return Texts(
                compile_statement(env, WrapAST(ast, Assert, .expr=and_->lhs, .message=message)),
                compile_statement(env, WrapAST(ast, Assert, .expr=and_->rhs, .message=message)));
        }
        case Equals: failure = "!="; goto assert_comparison;
        case NotEquals: failure = "=="; goto assert_comparison;
        case LessThan: failure = ">="; goto assert_comparison;
        case LessThanOrEquals: failure = ">"; goto assert_comparison;
        case GreaterThan: failure = "<="; goto assert_comparison;
        case GreaterThanOrEquals: failure = "<"; goto assert_comparison; {
          assert_comparison:;
            binary_operands_t cmp = BINARY_OPERANDS(expr);
            type_t *lhs_t = get_type(env, cmp.lhs);
            type_t *rhs_t = get_type(env, cmp.rhs);
            type_t *operand_t;
            if (cmp.lhs->tag == Int && is_numeric_type(rhs_t)) {
                operand_t = rhs_t;
            } else if (cmp.rhs->tag == Int && is_numeric_type(lhs_t)) {
                operand_t = lhs_t;
            } else if (can_compile_to_type(env, cmp.rhs, lhs_t)) {
                operand_t = lhs_t;
            } else if (can_compile_to_type(env, cmp.lhs, rhs_t)) {
                operand_t = rhs_t;
            } else {
                code_err(ast, "I can't do comparisons between ", type_to_str(lhs_t), " and ", type_to_str(rhs_t));
            }

            ast_t *lhs_var = FakeAST(InlineCCode, .chunks=new(ast_list_t, .ast=FakeAST(TextLiteral, Text("_lhs"))), .type=operand_t);
            ast_t *rhs_var = FakeAST(InlineCCode, .chunks=new(ast_list_t, .ast=FakeAST(TextLiteral, Text("_rhs"))), .type=operand_t);
            ast_t *var_comparison = new(ast_t, .file=expr->file, .start=expr->start, .end=expr->end, .tag=expr->tag,
                                        .__data.Equals={.lhs=lhs_var, .rhs=rhs_var});
            int64_t line = get_line_number(ast->file, ast->start);
            return Texts("{ // assertion\n",
                            compile_declaration(operand_t, Text("_lhs")), " = ", compile_to_type(env, cmp.lhs, operand_t), ";\n",
                            "\n#line ", String(line), "\n",
                            compile_declaration(operand_t, Text("_rhs")), " = ", compile_to_type(env, cmp.rhs, operand_t), ";\n",
                            "\n#line ", String(line), "\n",
                            "if (!(", compile_condition(env, var_comparison), "))\n",
                            "#line ", String(line), "\n",
                                Texts(
                                    "fail_source(", quoted_str(ast->file->filename), ", ",
                                    String((int64_t)(expr->start - expr->file->text)), ", ",
                                    String((int64_t)(expr->end - expr->file->text)), ", ", 
                                    message ? Texts("Text$as_c_string(", compile_to_type(env, message, Type(TextType)), ")")
                                        : Text("\"This assertion failed!\""), ", ",
                                    "\" (\", ", expr_as_text(Text("_lhs"), operand_t, Text("no")), ", "
                                    "\" ", failure, " \", ", expr_as_text(Text("_rhs"), operand_t, Text("no")), ", \")\");\n"),
                            "}\n");

        }
        default: {
            int64_t line = get_line_number(ast->file, ast->start);
            return Texts(
                "if (!(", compile_condition(env, expr), "))\n",
                "#line ", String(line), "\n",
                "fail_source(", quoted_str(ast->file->filename), ", ",
                String((int64_t)(expr->start - expr->file->text)), ", ", 
                String((int64_t)(expr->end - expr->file->text)), ", ",
                message ? Texts("Text$as_c_string(", compile_to_type(env, message, Type(TextType)), ")")
                    : Text("\"This assertion failed!\""),
                ");\n");
        }
        }
    }
    case Declare: {
        DeclareMatch(decl, ast, Declare);
        const char *name = Match(decl->var, Var)->name;
        if (streq(name, "_")) { // Explicit discard
            if (decl->value)
                return Texts("(void)", compile(env, decl->value), ";");
            else
                return EMPTY_TEXT;
        } else {
            type_t *t = decl->type ? parse_type_ast(env, decl->type) : get_type(env, decl->value);
            if (t->tag == FunctionType) t = Type(ClosureType, t);
            if (t->tag == AbortType || t->tag == VoidType || t->tag == ReturnType)
                code_err(ast, "You can't declare a variable with a ", type_to_str(t), " value");

            Text_t val_code = compile_declared_value(env, ast);
            return Texts(compile_declaration(t, Texts("_$", name)), " = ", val_code, ";");
        }
    }
    case Assign: {
        DeclareMatch(assign, ast, Assign);
        // Single assignment, no temp vars needed:
        if (assign->targets && !assign->targets->next) {
            type_t *lhs_t = get_type(env, assign->targets->ast);
            if (assign->targets->ast->tag == Index && lhs_t->tag == OptionalType
                && value_type(get_type(env, Match(assign->targets->ast, Index)->indexed))->tag == TableType)
                lhs_t = Match(lhs_t, OptionalType)->type;
            if (has_stack_memory(lhs_t))
                code_err(ast, "Stack references cannot be assigned to variables because the variable's scope may outlive the scope of the stack memory.");
            env_t *val_env = with_enum_scope(env, lhs_t);
            Text_t val = compile_to_type(val_env, assign->values->ast, lhs_t);
            return Texts(compile_assignment(env, assign->targets->ast, val), ";\n");
        }

        Text_t code = Text("{ // Assignment\n");
        int64_t i = 1;
        for (ast_list_t *value = assign->values, *target = assign->targets; value && target; value = value->next, target = target->next) {
            type_t *lhs_t = get_type(env, target->ast);
            if (target->ast->tag == Index && lhs_t->tag == OptionalType
                && value_type(get_type(env, Match(target->ast, Index)->indexed))->tag == TableType)
                lhs_t = Match(lhs_t, OptionalType)->type;
            if (has_stack_memory(lhs_t))
                code_err(ast, "Stack references cannot be assigned to variables because the variable's scope may outlive the scope of the stack memory.");
            env_t *val_env = with_enum_scope(env, lhs_t);
            Text_t val = compile_to_type(val_env, value->ast, lhs_t);
            code = Texts(code, compile_type(lhs_t), " $", String(i), " = ", val, ";\n");
            i += 1;
        }
        i = 1;
        for (ast_list_t *target = assign->targets; target; target = target->next) {
            code = Texts(code, compile_assignment(env, target->ast, Texts("$", String(i))), ";\n");
            i += 1;
        }
        return Texts(code, "\n}");
    }
    case PlusUpdate: {
        DeclareMatch(update, ast, PlusUpdate);
        type_t *lhs_t = get_type(env, update->lhs);
        if (is_idempotent(update->lhs) && (lhs_t->tag == IntType || lhs_t->tag == NumType || lhs_t->tag == ByteType))
            return Texts(compile_lvalue(env, update->lhs), " += ", compile_to_type(env, update->rhs, lhs_t), ";");
        return compile_update_assignment(env, ast);
    }
    case MinusUpdate: {
        DeclareMatch(update, ast, MinusUpdate);
        type_t *lhs_t = get_type(env, update->lhs);
        if (is_idempotent(update->lhs) && (lhs_t->tag == IntType || lhs_t->tag == NumType || lhs_t->tag == ByteType))
            return Texts(compile_lvalue(env, update->lhs), " -= ", compile_to_type(env, update->rhs, lhs_t), ";");
        return compile_update_assignment(env, ast);
    }
    case MultiplyUpdate: {
        DeclareMatch(update, ast, MultiplyUpdate);
        type_t *lhs_t = get_type(env, update->lhs);
        if (is_idempotent(update->lhs) && (lhs_t->tag == IntType || lhs_t->tag == NumType || lhs_t->tag == ByteType))
            return Texts(compile_lvalue(env, update->lhs), " *= ", compile_to_type(env, update->rhs, lhs_t), ";");
        return compile_update_assignment(env, ast);
    }
    case DivideUpdate: {
        DeclareMatch(update, ast, DivideUpdate);
        type_t *lhs_t = get_type(env, update->lhs);
        if (is_idempotent(update->lhs) && (lhs_t->tag == IntType || lhs_t->tag == NumType || lhs_t->tag == ByteType))
            return Texts(compile_lvalue(env, update->lhs), " /= ", compile_to_type(env, update->rhs, lhs_t), ";");
        return compile_update_assignment(env, ast);
    }
    case ModUpdate: {
        DeclareMatch(update, ast, ModUpdate);
        type_t *lhs_t = get_type(env, update->lhs);
        if (is_idempotent(update->lhs) && (lhs_t->tag == IntType || lhs_t->tag == NumType || lhs_t->tag == ByteType))
            return Texts(compile_lvalue(env, update->lhs), " %= ", compile_to_type(env, update->rhs, lhs_t), ";");
        return compile_update_assignment(env, ast);
    }
    case PowerUpdate: case Mod1Update: case ConcatUpdate: case LeftShiftUpdate: case UnsignedLeftShiftUpdate:
    case RightShiftUpdate: case UnsignedRightShiftUpdate: case AndUpdate: case OrUpdate: case XorUpdate: {
        return compile_update_assignment(env, ast);
    }
    case StructDef: case EnumDef: case LangDef: case Extend: case FunctionDef: case ConvertDef: {
        return EMPTY_TEXT;
    }
    case Skip: {
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
                if (code.length > 0)
                    return Texts("{\n", code, "goto ", ctx->skip_label, ";\n}\n");
                else
                    return Texts("goto ", ctx->skip_label, ";");
            }
        }
        if (env->loop_ctx)
            code_err(ast, "This is not inside any loop");
        else if (target)
            code_err(ast, "No loop target named '", target, "' was found");
        else
            return Text("continue;");
    }
    case Stop: {
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
                if (code.length > 0)
                    return Texts("{\n", code, "goto ", ctx->stop_label, ";\n}\n");
                else
                    return Texts("goto ", ctx->stop_label, ";");
            }
        }
        if (env->loop_ctx)
            code_err(ast, "This is not inside any loop");
        else if (target)
            code_err(ast, "No loop target named '", target, "' was found");
        else
            return Text("break;");
    }
    case Pass: return Text(";");
    case Defer: {
        ast_t *body = Match(ast, Defer)->body;
        Table_t closed_vars = get_closed_vars(env, NULL, body);

        static int defer_id = 0;
        env_t *defer_env = fresh_scope(env);
        Text_t code = EMPTY_TEXT;
        for (int64_t i = 0; i < closed_vars.entries.length; i++) {
            struct { const char *name; binding_t *b; } *entry = closed_vars.entries.data + closed_vars.entries.stride*i;
            if (entry->b->type->tag == ModuleType)
                continue;
            if (Text$starts_with(entry->b->code, Text("userdata->"), NULL)) {
                Table$str_set(defer_env->locals, entry->name, entry->b);
            } else {
                Text_t defer_name = Texts("defer$", String(++defer_id), "$", entry->name);
                defer_id += 1;
                code = Texts(
                    code, compile_declaration(entry->b->type, defer_name), " = ", entry->b->code, ";\n");
                set_binding(defer_env, entry->name, entry->b->type, defer_name);
            }
        }
        env->deferred = new(deferral_t, .defer_env=defer_env, .block=body, .next=env->deferred);
        return code;
    }
    case Return: {
        if (!env->fn_ret) code_err(ast, "This return statement is not inside any function");
        ast_t *ret = Match(ast, Return)->value;

        Text_t code = EMPTY_TEXT;
        for (deferral_t *deferred = env->deferred; deferred; deferred = deferred->next) {
            code = Texts(code, compile_statement(deferred->defer_env, deferred->block));
        }

        if (ret) {
            if (env->fn_ret->tag == VoidType || env->fn_ret->tag == AbortType)
                code_err(ast, "This function is not supposed to return any values, according to its type signature");

            env = with_enum_scope(env, env->fn_ret);
            Text_t value = compile_to_type(env, ret, env->fn_ret);
            if (env->deferred) {
                code = Texts(compile_declaration(env->fn_ret, Text("ret")), " = ", value, ";\n", code);
                value = Text("ret");
            }

            return Texts(code, "return ", value, ";");
        } else {
            if (env->fn_ret->tag != VoidType)
                code_err(ast, "This function expects you to return a ", type_to_str(env->fn_ret), " value");
            return Texts(code, "return;");
        }
    }
    case While: {
        DeclareMatch(while_, ast, While);
        env_t *scope = fresh_scope(env);
        loop_ctx_t loop_ctx = (loop_ctx_t){
            .loop_name="while",
            .deferred=scope->deferred,
            .next=env->loop_ctx,
        };
        scope->loop_ctx = &loop_ctx;
        Text_t body = compile_statement(scope, while_->body);
        if (loop_ctx.skip_label.length > 0)
            body = Texts(body, "\n", loop_ctx.skip_label, ": continue;");
        Text_t loop = Texts("while (", while_->condition ? compile(scope, while_->condition) : Text("yes"), ") {\n\t", body, "\n}");
        if (loop_ctx.stop_label.length > 0)
            loop = Texts(loop, "\n", loop_ctx.stop_label, ":;");
        return loop;
    }
    case Repeat: {
        ast_t *body = Match(ast, Repeat)->body;
        env_t *scope = fresh_scope(env);
        loop_ctx_t loop_ctx = (loop_ctx_t){
            .loop_name="repeat",
            .deferred=scope->deferred,
            .next=env->loop_ctx,
        };
        scope->loop_ctx = &loop_ctx;
        Text_t body_code = compile_statement(scope, body);
        if (loop_ctx.skip_label.length > 0)
            body_code = Texts(body_code, "\n", loop_ctx.skip_label, ": continue;");
        Text_t loop = Texts("for (;;) {\n\t", body_code, "\n}");
        if (loop_ctx.stop_label.length > 0)
            loop = Texts(loop, "\n", loop_ctx.stop_label, ":;");
        return loop;
    }
    case For: {
        DeclareMatch(for_, ast, For);

        // If we're iterating over a comprehension, that's actually just doing
        // one loop, we don't need to compile the comprehension as a list
        // comprehension. This is a common case for reducers like `(+: i*2 for i in 5)`
        // or `(and) x.is_good() for x in xs`
        if (for_->iter->tag == Comprehension) {
            DeclareMatch(comp, for_->iter, Comprehension);
            ast_t *body = for_->body;
            if (for_->vars) {
                if (for_->vars->next)
                    code_err(for_->vars->next->ast, "This is too many variables for iteration");

                body = WrapAST(
                    ast, Block,
                    .statements=new(ast_list_t, .ast=WrapAST(ast, Declare, .var=for_->vars->ast, .value=comp->expr),
                                    .next=body->tag == Block ? Match(body, Block)->statements : new(ast_list_t, .ast=body)));
            }

            if (comp->filter)
                body = WrapAST(for_->body, If, .condition=comp->filter, .body=body);
            ast_t *loop = WrapAST(ast, For, .vars=comp->vars, .iter=comp->iter, .body=body);
            return compile_statement(env, loop);
        }

        env_t *body_scope = for_scope(env, ast);
        loop_ctx_t loop_ctx = (loop_ctx_t){
            .loop_name="for",
            .loop_vars=for_->vars,
            .deferred=body_scope->deferred,
            .next=body_scope->loop_ctx,
        };
        body_scope->loop_ctx = &loop_ctx;
        // Naked means no enclosing braces:
        Text_t naked_body = compile_inline_block(body_scope, for_->body);
        if (loop_ctx.skip_label.length > 0)
            naked_body = Texts(naked_body, "\n", loop_ctx.skip_label, ": continue;");
        Text_t stop = loop_ctx.stop_label.length > 0 ? Texts("\n", loop_ctx.stop_label, ":;") : EMPTY_TEXT;

        // Special case for improving performance for numeric iteration:
        if (for_->iter->tag == MethodCall && streq(Match(for_->iter, MethodCall)->name, "to") &&
            is_int_type(get_type(env, Match(for_->iter, MethodCall)->self))) {
            // TODO: support other integer types
            arg_ast_t *args = Match(for_->iter, MethodCall)->args;
            if (!args) code_err(for_->iter, "to() needs at least one argument");

            type_t *int_type = get_type(env, Match(for_->iter, MethodCall)->self);
            type_t *step_type = int_type->tag == ByteType ? Type(IntType, .bits=TYPE_IBITS8) : int_type;

            Text_t last = EMPTY_TEXT, step = EMPTY_TEXT, optional_step = EMPTY_TEXT;
            if (!args->name || streq(args->name, "last")) {
                last = compile_to_type(env, args->value, int_type);
                if (args->next) {
                    if (args->next->name && !streq(args->next->name, "step"))
                        code_err(args->next->value, "Invalid argument name: ", args->next->name);
                    if (get_type(env, args->next->value)->tag == OptionalType)
                        optional_step = compile_to_type(env, args->next->value, Type(OptionalType, step_type));
                    else
                        step = compile_to_type(env, args->next->value, step_type);
                }
            } else if (streq(args->name, "step")) {
                if (get_type(env, args->value)->tag == OptionalType)
                    optional_step = compile_to_type(env, args->value, Type(OptionalType, step_type));
                else
                    step = compile_to_type(env, args->value, step_type);
                if (args->next) {
                    if (args->next->name && !streq(args->next->name, "last"))
                        code_err(args->next->value, "Invalid argument name: ", args->next->name);
                    last = compile_to_type(env, args->next->value, int_type);
                }
            }

            if (last.length == 0)
                code_err(for_->iter, "No `last` argument was given");
            
            Text_t type_code = compile_type(int_type);
            Text_t value = for_->vars ? compile(body_scope, for_->vars->ast) : Text("i");
            if (int_type->tag == BigIntType) {
                if (optional_step.length > 0)
                    step = Texts("({ OptionalInt_t maybe_step = ", optional_step, "; maybe_step->small == 0 ? (Int$compare_value(last, first) >= 0 ? I_small(1) : I_small(-1)) : (Int_t)maybe_step; })");
                else if (step.length == 0)
                    step = Text("Int$compare_value(last, first) >= 0 ? I_small(1) : I_small(-1)");
                return Texts(
                    "for (", type_code, " first = ", compile(env, Match(for_->iter, MethodCall)->self), ", ",
                    value, " = first, last = ", last, ", step = ", step, "; "
                    "Int$compare_value(", value, ", last) != Int$compare_value(step, I_small(0)); ",
                    value, " = Int$plus(", value, ", step)) {\n"
                    "\t", naked_body,
                    "}",
                    stop);
            } else {
                if (optional_step.length > 0)
                    step = Texts("({ ", compile_type(Type(OptionalType, step_type)), " maybe_step = ", optional_step, "; "
                                    "maybe_step.is_none ? (", type_code, ")(last >= first ? 1 : -1) : maybe_step.value; })");
                else if (step.length == 0)
                    step = Texts("(", type_code, ")(last >= first ? 1 : -1)");
                return Texts(
                    "for (", type_code, " first = ", compile(env, Match(for_->iter, MethodCall)->self), ", ",
                    value, " = first, last = ", last, ", step = ", step, "; "
                    "step > 0 ? ", value, " <= last : ", value, " >= last; ",
                    value, " += step) {\n"
                    "\t", naked_body,
                    "}",
                    stop);
            }
        } else if (for_->iter->tag == MethodCall && streq(Match(for_->iter, MethodCall)->name, "onward") &&
            get_type(env, Match(for_->iter, MethodCall)->self)->tag == BigIntType) {
            // Special case for Int.onward()
            arg_ast_t *args = Match(for_->iter, MethodCall)->args;
            arg_t *arg_spec = new(arg_t, .name="step", .type=INT_TYPE, .default_val=FakeAST(Int, .str="1"), .next=NULL);
            Text_t step = compile_arguments(env, for_->iter, arg_spec, args);
            Text_t value = for_->vars ? compile(body_scope, for_->vars->ast) : Text("i");
            return Texts(
                "for (Int_t ", value, " = ", compile(env, Match(for_->iter, MethodCall)->self), ", ",
                "step = ", step, "; ; ", value, " = Int$plus(", value, ", step)) {\n"
                "\t", naked_body,
                "}",
                stop);
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

            if (index.length > 0)
                naked_body = Texts("Int_t ", index, " = I(i);\n", naked_body);

            if (value.length > 0) {
                loop = Texts(loop, "{\n",
                                compile_declaration(item_t, value),
                                " = *(", compile_type(item_t), "*)(iterating.data + (i-1)*iterating.stride);\n",
                                naked_body, "\n}");
            } else {
                loop = Texts(loop, "{\n", naked_body, "\n}");
            }

            if (for_->empty)
                loop = Texts("if (iterating.length > 0) {\n", loop, "\n} else ", compile_statement(env, for_->empty));

            if (iter_t->tag == PointerType) {
                loop = Texts("{\n"
                                "List_t *ptr = ", compile_to_pointer_depth(env, for_->iter, 1, false), ";\n"
                                "\nLIST_INCREF(*ptr);\n"
                                "List_t iterating = *ptr;\n",
                                loop, 
                                stop,
                                "\nLIST_DECREF(*ptr);\n"
                                "}\n");

            } else {
                loop = Texts("{\n"
                                "List_t iterating = ", compile_to_pointer_depth(env, for_->iter, 0, false), ";\n",
                                loop, 
                                stop,
                                "}\n");
            }
            return loop;
        }
        case SetType: case TableType: {
            Text_t loop = Text("for (int64_t i = 0; i < iterating.length; ++i) {\n");
            if (for_->vars) {
                if (iter_value_t->tag == SetType) {
                    if (for_->vars->next)
                        code_err(for_->vars->next->ast, "This is too many variables for this loop");
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
                        Text_t value_offset = Texts("offsetof(struct { ", compile_declaration(key_t, Text("k")), "; ", compile_declaration(value_t, Text("v")), "; }, v)");
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
                loop = Texts(
                    "{\n",
                    "Table_t *t = ", compile_to_pointer_depth(env, for_->iter, 1, false), ";\n"
                    "LIST_INCREF(t->entries);\n"
                    "List_t iterating = t->entries;\n",
                    loop,
                    "LIST_DECREF(t->entries);\n"
                    "}\n");
            } else {
                loop = Texts(
                    "{\n",
                    "List_t iterating = (", compile_to_pointer_depth(env, for_->iter, 0, false), ").entries;\n",
                    loop,
                    "}\n");
            }
            return loop;
        }
        case BigIntType: {
            Text_t n;
            if (for_->iter->tag == Int) {
                const char *str = Match(for_->iter, Int)->str;
                Int_t int_val = Int$from_str(str);
                if (int_val.small == 0)
                    code_err(for_->iter, "Failed to parse this integer");
                mpz_t i;
                mpz_init_set_int(i, int_val);
                if (mpz_cmpabs_ui(i, BIGGEST_SMALL_INT) <= 0)
                    n = Text$from_str(mpz_get_str(NULL, 10, i));
                else
                    goto big_n;


                if (for_->empty && mpz_cmp_si(i, 0) <= 0) {
                    return compile_statement(env, for_->empty);
                } else {
                    return Texts(
                        "for (int64_t i = 1; i <= ", n, "; ++i) {\n",
                        for_->vars ? Texts("\tInt_t ", compile(body_scope, for_->vars->ast), " = I_small(i);\n") : EMPTY_TEXT,
                        "\t", naked_body,
                        "}\n",
                        stop, "\n");
                }
            }

          big_n:
            n = compile_to_pointer_depth(env, for_->iter, 0, false);
            Text_t i = for_->vars ? compile(body_scope, for_->vars->ast) : Text("i");
            Text_t n_var = for_->vars ? Texts("max", i) : Text("n");
            if (for_->empty) {
                return Texts(
                    "{\n"
                    "Int_t ", n_var, " = ", n, ";\n"
                    "if (Int$compare_value(", n_var, ", I(0)) > 0) {\n"
                    "for (Int_t ", i, " = I(1); Int$compare_value(", i, ", ", n_var, ") <= 0; ", i, " = Int$plus(", i, ", I(1))) {\n",
                    "\t", naked_body,
                    "}\n"
                    "} else ", compile_statement(env, for_->empty),
                    stop, "\n"
                    "}\n");
            } else {
                return Texts(
                    "for (Int_t ", i, " = I(1), ", n_var, " = ", n, "; Int$compare_value(", i, ", ", n_var, ") <= 0; ", i, " = Int$plus(", i, ", I(1))) {\n",
                    "\t", naked_body,
                    "}\n",
                    stop, "\n");
            }
        }
        case FunctionType: case ClosureType: {
            // Iterator function:
            Text_t code = Text("{\n");

            Text_t next_fn;
            if (is_idempotent(for_->iter)) {
                next_fn = compile_to_pointer_depth(env, for_->iter, 0, false);
            } else {
                code = Texts(code, compile_declaration(iter_value_t, Text("next")), " = ", compile_to_pointer_depth(env, for_->iter, 0, false), ";\n");
                next_fn = Text("next");
            }

            __typeof(iter_value_t->__data.FunctionType) *fn = iter_value_t->tag == ClosureType ? Match(Match(iter_value_t, ClosureType)->fn, FunctionType) : Match(iter_value_t, FunctionType);

            Text_t get_next;
            if (iter_value_t->tag == ClosureType) {
                type_t *fn_t = Match(iter_value_t, ClosureType)->fn;
                arg_t *closure_fn_args = NULL;
                for (arg_t *arg = Match(fn_t, FunctionType)->args; arg; arg = arg->next)
                    closure_fn_args = new(arg_t, .name=arg->name, .type=arg->type, .default_val=arg->default_val, .next=closure_fn_args);
                closure_fn_args = new(arg_t, .name="userdata", .type=Type(PointerType, .pointed=Type(MemoryType)), .next=closure_fn_args);
                REVERSE_LIST(closure_fn_args);
                Text_t fn_type_code = compile_type(Type(FunctionType, .args=closure_fn_args, .ret=Match(fn_t, FunctionType)->ret));
                get_next = Texts("((", fn_type_code, ")", next_fn, ".fn)(", next_fn, ".userdata)");
            } else {
                get_next = Texts(next_fn, "()");
            }

            if (fn->ret->tag == OptionalType) {
                // Use an optional variable `cur` for each iteration step, which will be checked for none
                code = Texts(code, compile_declaration(fn->ret, Text("cur")), ";\n");
                get_next = Texts("(cur=", get_next, ", !", check_none(fn->ret, Text("cur")), ")");
                if (for_->vars) {
                    naked_body = Texts(
                        compile_declaration(Match(fn->ret, OptionalType)->type, Texts("_$", Match(for_->vars->ast, Var)->name)),
                        " = ", optional_into_nonnone(fn->ret, Text("cur")), ";\n",
                        naked_body);
                }
                if (for_->empty) {
                    code = Texts(code, "if (", get_next, ") {\n"
                                    "\tdo{\n\t\t", naked_body, "\t} while(", get_next, ");\n"
                                    "} else {\n\t", compile_statement(env, for_->empty), "}", stop, "\n}\n");
                } else {
                    code = Texts(code, "while(", get_next, ") {\n\t", naked_body, "}\n", stop, "\n}\n");
                }
            } else {
                if (for_->vars) {
                    naked_body = Texts(
                        compile_declaration(fn->ret, Texts("_$", Match(for_->vars->ast, Var)->name)),
                        " = ", get_next, ";\n", naked_body);
                } else {
                    naked_body = Texts(get_next, ";\n", naked_body);
                }
                if (for_->empty)
                    code_err(for_->empty, "This iteration loop will always have values, so this block will never run");
                code = Texts(code, "for (;;) {\n\t", naked_body, "}\n", stop, "\n}\n");
            }

            return code;
        }
        default: code_err(for_->iter, "Iteration is not implemented for type: ", type_to_str(iter_t));
        }
    }
    case If: {
        DeclareMatch(if_, ast, If);
        ast_t *condition = if_->condition;
        if (condition->tag == Declare) {
            if (Match(condition, Declare)->value == NULL)
                code_err(condition, "This declaration must have a value");
            env_t *truthy_scope = fresh_scope(env);
            Text_t code = Texts("IF_DECLARE(", compile_statement(truthy_scope, condition), ", ");
            bind_statement(truthy_scope, condition);
            ast_t *var = Match(condition, Declare)->var;
            code = Texts(code, compile_condition(truthy_scope, var), ", ");
            type_t *cond_t = get_type(truthy_scope, var);
            if (cond_t->tag == OptionalType) {
                set_binding(truthy_scope, Match(var, Var)->name,
                            Match(cond_t, OptionalType)->type,
                            optional_into_nonnone(cond_t, compile(truthy_scope, var)));
            }
            code = Texts(code, compile_statement(truthy_scope, if_->body), ")");
            if (if_->else_body)
                code = Texts(code, "\nelse ", compile_statement(env, if_->else_body));
            return code;
        } else {
            Text_t code = Texts("if (", compile_condition(env, condition), ")");
            env_t *truthy_scope = env;
            type_t *cond_t = get_type(env, condition);
            if (condition->tag == Var && cond_t->tag == OptionalType) {
                truthy_scope = fresh_scope(env);
                set_binding(truthy_scope, Match(condition, Var)->name,
                            Match(cond_t, OptionalType)->type,
                            optional_into_nonnone(cond_t, compile(truthy_scope, condition)));
            }
            code = Texts(code, compile_statement(truthy_scope, if_->body));
            if (if_->else_body)
                code = Texts(code, "\nelse ", compile_statement(env, if_->else_body));
            return code;
        }
    }
    case Block: {
        return Texts("{\n", compile_inline_block(env, ast), "}\n");
    }
    case Comprehension: {
        if (!env->comprehension_action)
            code_err(ast, "I don't know what to do with this comprehension!");
        DeclareMatch(comp, ast, Comprehension);
        if (comp->expr->tag == Comprehension) { // Nested comprehension
            ast_t *body = comp->filter ? WrapAST(ast, If, .condition=comp->filter, .body=comp->expr) : comp->expr;
            ast_t *loop = WrapAST(ast, For, .vars=comp->vars, .iter=comp->iter, .body=body);
            return compile_statement(env, loop);
        }

        // List/Set/Table comprehension:
        comprehension_body_t get_body = (void*)env->comprehension_action->fn;
        ast_t *body = get_body(comp->expr, env->comprehension_action->userdata);
        if (comp->filter)
            body = WrapAST(comp->expr, If, .condition=comp->filter, .body=body);
        ast_t *loop = WrapAST(ast, For, .vars=comp->vars, .iter=comp->iter, .body=body);
        return compile_statement(env, loop);
    }
    case Extern: return EMPTY_TEXT;
    case InlineCCode: {
        DeclareMatch(inline_code, ast, InlineCCode);
        Text_t code = EMPTY_TEXT;
        for (ast_list_t *chunk = inline_code->chunks; chunk; chunk = chunk->next) {
            if (chunk->ast->tag == TextLiteral) {
                code = Texts(code, Match(chunk->ast, TextLiteral)->text);
            } else {
                code = Texts(code, compile(env, chunk->ast));
            }
        }
        return code;
    }
    case Use: {
        DeclareMatch(use, ast, Use);
        if (use->what == USE_LOCAL) {
            Path_t path = Path$from_str(Match(ast, Use)->path);
            Path_t in_file = Path$from_str(ast->file->filename);
            path = Path$resolved(path, Path$parent(in_file));
            Text_t suffix = get_id_suffix(Path$as_c_string(path));
            return with_source_info(env, ast, Texts("$initialize", suffix, "();\n"));
        } else if (use->what == USE_MODULE) {
            module_info_t mod = get_module_info(ast);
            glob_t tm_files;
            const char *folder = mod.version ? String(mod.name, "_", mod.version) : mod.name;
            if (glob(String(TOMO_PREFIX"/share/tomo_"TOMO_VERSION"/installed/", folder, "/[!._0-9]*.tm"), GLOB_TILDE, NULL, &tm_files) != 0) {
                if (!try_install_module(mod))
                    code_err(ast, "Could not find library");
            }

            Text_t initialization = EMPTY_TEXT;

            for (size_t i = 0; i < tm_files.gl_pathc; i++) {
                const char *filename = tm_files.gl_pathv[i];
                initialization = Texts(
                    initialization,
                    with_source_info(env, ast, Texts("$initialize", get_id_suffix(filename), "();\n")));
            }
            globfree(&tm_files);
            return initialization;
        } else {
            return EMPTY_TEXT;
        }
    }
    default:
        // print("Is discardable: ", ast_to_sexp_str(ast), " ==> ", is_discardable(env, ast));
        if (!is_discardable(env, ast))
            code_err(ast, "The ", type_to_str(get_type(env, ast)), " result of this statement cannot be discarded");
        return Texts("(void)", compile(env, ast), ";");
    }
}

Text_t compile_statement(env_t *env, ast_t *ast) {
    Text_t stmt = _compile_statement(env, ast);
    return with_source_info(env, ast, stmt);
}

Text_t expr_as_text(Text_t expr, type_t *t, Text_t color)
{
    switch (t->tag) {
    case MemoryType: return Texts("Memory$as_text(stack(", expr, "), ", color, ", &Memory$info)");
    case BoolType:
         // NOTE: this cannot use stack(), since bools may actually be bit fields:
         return Texts("Bool$as_text((Bool_t[1]){", expr, "}, ", color, ", &Bool$info)");
    case CStringType: return Texts("CString$as_text(stack(", expr, "), ", color, ", &CString$info)");
    case BigIntType: case IntType: case ByteType: case NumType: {
        Text_t name = type_to_text(t);
        return Texts(name, "$as_text(stack(", expr, "), ", color, ", &", name, "$info)");
    }
    case TextType: return Texts("Text$as_text(stack(", expr, "), ", color, ", ", compile_type_info(t), ")");
    case ListType: return Texts("List$as_text(stack(", expr, "), ", color, ", ", compile_type_info(t), ")");
    case SetType: return Texts("Table$as_text(stack(", expr, "), ", color, ", ", compile_type_info(t), ")");
    case TableType: return Texts("Table$as_text(stack(", expr, "), ", color, ", ", compile_type_info(t), ")");
    case FunctionType: case ClosureType: return Texts("Func$as_text(stack(", expr, "), ", color, ", ", compile_type_info(t), ")");
    case PointerType: return Texts("Pointer$as_text(stack(", expr, "), ", color, ", ", compile_type_info(t), ")");
    case OptionalType: return Texts("Optional$as_text(stack(", expr, "), ", color, ", ", compile_type_info(t), ")");
    case StructType: case EnumType:
        return Texts("generic_as_text(stack(", expr, "), ", color, ", ", compile_type_info(t), ")");
    default: compiler_err(NULL, NULL, NULL, "Stringifying is not supported for ", type_to_str(t));
    }
    return EMPTY_TEXT;
}

Text_t compile_text(env_t *env, ast_t *ast, Text_t color)
{
    type_t *t = get_type(env, ast);
    Text_t expr = compile(env, ast);
    return expr_as_text(expr, t, color);
}

Text_t compile_to_pointer_depth(env_t *env, ast_t *ast, int64_t target_depth, bool needs_incref)
{
    Text_t val = compile(env, ast);
    type_t *t = get_type(env, ast);
    int64_t depth = 0;
    for (type_t *tt = t; tt->tag == PointerType; tt = Match(tt, PointerType)->pointed)
        ++depth;

    // Passing a literal value won't trigger an incref, because it's ephemeral,
    // e.g. [10, 20].reversed()
    if (t->tag != PointerType && needs_incref && !can_be_mutated(env, ast))
        needs_incref = false;

    while (depth != target_depth) {
        if (depth < target_depth) {
            if (ast->tag == Var && target_depth == 1)
                val = Texts("(&", val, ")");
            else
                code_err(ast, "This should be a pointer, not ", type_to_str(get_type(env, ast)));
            t = Type(PointerType, .pointed=t, .is_stack=true);
            ++depth;
        } else {
            DeclareMatch(ptr, t, PointerType);
            val = Texts("*(", val, ")");
            t = ptr->pointed;
            --depth;
        }
    }

    while (t->tag == PointerType) {
        DeclareMatch(ptr, t, PointerType);
        t = ptr->pointed;
    }

    if (needs_incref && t->tag == ListType)
        val = Texts("LIST_COPY(", val, ")");
    else if (needs_incref && (t->tag == TableType || t->tag == SetType))
        val = Texts("TABLE_COPY(", val, ")");

    return val;
}

Text_t compile_to_type(env_t *env, ast_t *ast, type_t *t)
{
    if (ast->tag == Int && is_numeric_type(non_optional(t))) {
        return compile_int_to_type(env, ast, t);
    } else if (ast->tag == Num && t->tag == NumType) {
        double n = Match(ast, Num)->n;
        switch (Match(t, NumType)->bits) {
        case TYPE_NBITS64: return Text$from_str(String(hex_double(n)));
        case TYPE_NBITS32: return Text$from_str(String(hex_double(n), "f"));
        default: code_err(ast, "This is not a valid number bit width");
        }
    } else if (ast->tag == None) {
        if (t->tag != OptionalType)
            code_err(ast, "This is not supposed to be an optional type");
        else if (Match(t, OptionalType)->type == NULL)
            code_err(ast, "I don't know what kind of `none` this is supposed to be!\nPlease tell me by declaring a variable like `foo : Type = none`");
        return compile_none(t);
    } else if (t->tag == PointerType && (ast->tag == HeapAllocate || ast->tag == StackReference)) {
        return compile_typed_allocation(env, ast, t);
    } else if (t->tag == ListType && ast->tag == List) {
        return compile_typed_list(env, ast, t);
    } else if (t->tag == TableType && ast->tag == Table) {
        return compile_typed_table(env, ast, t);
    } else if (t->tag == SetType && ast->tag == Set) {
        return compile_typed_set(env, ast, t);
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
        // Currently, this is only implemented for cases where you have the return type
        // and the self type equal to each other, because that's the main case I care
        // about with list and set methods (e.g. `List.sorted()`)
        if (is_incomplete_type(self_type) && type_eq(self_type, actual)) {
            type_t *completed_self = most_complete_type(self_type, t);
            if (completed_self) {
                ast_t *explicit_self = WrapAST(methodcall->self, ExplicitlyTyped, .ast=methodcall->self, .type=completed_self);
                ast_t *new_methodcall = WrapAST(ast, MethodCall, .self=explicit_self,
                                                .name=methodcall->name, .args=methodcall->args);
                return compile_to_type(env, new_methodcall, t);
            }
        }
    }

    // Promote values to views-of-values if needed:
    if (t->tag == PointerType && Match(t, PointerType)->is_stack && actual->tag != PointerType)
        return Texts("stack(", compile_to_type(env, ast, Match(t, PointerType)->pointed), ")");

    Text_t code = compile(env, ast);
    if (!promote(env, ast, &code, actual, t))
        code_err(ast, "I expected a ", type_to_str(t), " here, but this is a ", type_to_str(actual));
    return code;
}

Text_t compile_typed_list(env_t *env, ast_t *ast, type_t *list_type)
{
    DeclareMatch(list, ast, List);
    if (!list->items)
        return Text("(List_t){.length=0}");

    type_t *item_type = Match(list_type, ListType)->item_type;

    int64_t n = 0;
    for (ast_list_t *item = list->items; item; item = item->next) {
        ++n;
        if (item->ast->tag == Comprehension)
            goto list_comprehension;
    }

    {
        env_t *scope = item_type->tag == EnumType ? with_enum_scope(env, item_type) : env;
        if (is_incomplete_type(item_type))
            code_err(ast, "This list's type can't be inferred!");
        Text_t code = Texts("TypedListN(", compile_type(item_type), ", ", String(n));
        for (ast_list_t *item = list->items; item; item = item->next) {
            code = Texts(code, ", ", compile_to_type(scope, item->ast, item_type));
        }
        return Texts(code, ")");
    }

  list_comprehension:
    {
        env_t *scope = item_type->tag == EnumType ? with_enum_scope(env, item_type) : fresh_scope(env);
        static int64_t comp_num = 1;
        const char *comprehension_name = String("list$", comp_num++);
        ast_t *comprehension_var = LiteralCode(Texts("&", comprehension_name),
                                               .type=Type(PointerType, .pointed=list_type, .is_stack=true));
        Closure_t comp_action = {.fn=add_to_list_comprehension, .userdata=comprehension_var};
        scope->comprehension_action = &comp_action;
        Text_t code = Texts("({ List_t ", comprehension_name, " = {};");
        // set_binding(scope, comprehension_name, list_type, comprehension_name);
        for (ast_list_t *item = list->items; item; item = item->next) {
            if (item->ast->tag == Comprehension)
                code = Texts(code, "\n", compile_statement(scope, item->ast));
            else
                code = Texts(code, compile_statement(env, add_to_list_comprehension(item->ast, comprehension_var)));
        }
        code = Texts(code, " ", comprehension_name, "; })");
        return code;
    }
}

Text_t compile_typed_set(env_t *env, ast_t *ast, type_t *set_type)
{
    DeclareMatch(set, ast, Set);
    if (!set->items)
        return Text("((Table_t){})");

    type_t *item_type = Match(set_type, SetType)->item_type;

    int64_t n = 0;
    for (ast_list_t *item = set->items; item; item = item->next) {
        ++n;
        if (item->ast->tag == Comprehension)
            goto set_comprehension;
    }
       
    { // No comprehension:
        Text_t code = Texts("Set(",
                             compile_type(item_type), ", ",
                             compile_type_info(item_type), ", ",
                             String(n));
        env_t *scope = item_type->tag == EnumType ? with_enum_scope(env, item_type) : env;
        for (ast_list_t *item = set->items; item; item = item->next) {
            code = Texts(code, ", ", compile_to_type(scope, item->ast, item_type));
        }
        return Texts(code, ")");
    }

  set_comprehension:
    {
        static int64_t comp_num = 1;
        env_t *scope = item_type->tag == EnumType ? with_enum_scope(env, item_type) : fresh_scope(env);
        const char *comprehension_name = String("set$", comp_num++);
        ast_t *comprehension_var = LiteralCode(Texts("&", comprehension_name),
                                               .type=Type(PointerType, .pointed=set_type, .is_stack=true));
        Text_t code = Texts("({ Table_t ", comprehension_name, " = {};");
        Closure_t comp_action = {.fn=add_to_set_comprehension, .userdata=comprehension_var};
        scope->comprehension_action = &comp_action;
        for (ast_list_t *item = set->items; item; item = item->next) {
            if (item->ast->tag == Comprehension)
                code = Texts(code, "\n", compile_statement(scope, item->ast));
            else
                code = Texts(code, compile_statement(env, add_to_set_comprehension(item->ast, comprehension_var)));
        }
        code = Texts(code, " ", comprehension_name, "; })");
        return code;
    }
}

Text_t compile_typed_table(env_t *env, ast_t *ast, type_t *table_type)
{
    DeclareMatch(table, ast, Table);
    if (!table->entries) {
        Text_t code = Text("((Table_t){");
        if (table->fallback)
            code = Texts(code, ".fallback=heap(", compile(env, table->fallback),")");
        return Texts(code, "})");
    }

    type_t *key_t = Match(table_type, TableType)->key_type;
    type_t *value_t = Match(table_type, TableType)->value_type;

    if (value_t->tag == OptionalType)
        code_err(ast, "Tables whose values are optional (", type_to_str(value_t), ") are not currently supported.");

    for (ast_list_t *entry = table->entries; entry; entry = entry->next) {
        if (entry->ast->tag == Comprehension)
            goto table_comprehension;
    }

    { // No comprehension:
        env_t *key_scope = key_t->tag == EnumType ? with_enum_scope(env, key_t) : env;
        env_t *value_scope = value_t->tag == EnumType ? with_enum_scope(env, value_t) : env;
        Text_t code = Texts("Table(",
                             compile_type(key_t), ", ",
                             compile_type(value_t), ", ",
                             compile_type_info(key_t), ", ",
                             compile_type_info(value_t));
        if (table->fallback)
            code = Texts(code, ", /*fallback:*/ heap(", compile(env, table->fallback), ")");
        else
            code = Texts(code, ", /*fallback:*/ NULL");

        size_t n = 0;
        for (ast_list_t *entry = table->entries; entry; entry = entry->next)
            ++n;
        code = Texts(code, ", ", String((int64_t)n));

        for (ast_list_t *entry = table->entries; entry; entry = entry->next) {
            DeclareMatch(e, entry->ast, TableEntry);
            code = Texts(code, ",\n\t{", compile_to_type(key_scope, e->key, key_t), ", ",
                            compile_to_type(value_scope, e->value, value_t), "}");
        }
        return Texts(code, ")");
    }

  table_comprehension:
    {
        static int64_t comp_num = 1;
        env_t *scope = fresh_scope(env);
        const char *comprehension_name = String("table$", comp_num++);
        ast_t *comprehension_var = LiteralCode(Texts("&", comprehension_name),
                                           .type=Type(PointerType, .pointed=table_type, .is_stack=true));

        Text_t code = Texts("({ Table_t ", comprehension_name, " = {");
        if (table->fallback)
            code = Texts(code, ".fallback=heap(", compile(env, table->fallback), "), ");

        code = Texts(code, "};");

        Closure_t comp_action = {.fn=add_to_table_comprehension, .userdata=comprehension_var};
        scope->comprehension_action = &comp_action;
        for (ast_list_t *entry = table->entries; entry; entry = entry->next) {
            if (entry->ast->tag == Comprehension)
                code = Texts(code, "\n", compile_statement(scope, entry->ast));
            else
                code = Texts(code, compile_statement(env, add_to_table_comprehension(entry->ast, comprehension_var)));
        }
        code = Texts(code, " ", comprehension_name, "; })");
        return code;
    }
}

Text_t compile_typed_allocation(env_t *env, ast_t *ast, type_t *pointer_type)
{
    // TODO: for constructors, do new(T, ...) instead of heap((T){...})
    type_t *pointed = Match(pointer_type, PointerType)->pointed;
    switch (ast->tag) {
    case HeapAllocate: {
        return Texts("heap(", compile_to_type(env, Match(ast, HeapAllocate)->value, pointed), ")");
    }
    case StackReference: {
        ast_t *subject = Match(ast, StackReference)->value;
        if (can_be_mutated(env, subject) && type_eq(pointed, get_type(env, subject)))
            return Texts("(&", compile_lvalue(env, subject), ")");
        else
            return Texts("stack(", compile_to_type(env, subject, pointed), ")");
    }
    default: code_err(ast, "Not an allocation!");
    }
    return EMPTY_TEXT;
}

Text_t compile_int_to_type(env_t *env, ast_t *ast, type_t *target)
{
    if (ast->tag != Int) {
        Text_t code = compile(env, ast);
        type_t *actual_type = get_type(env, ast);
        if (!promote(env, ast, &code, actual_type, target))
            code_err(ast, "I couldn't promote this ", type_to_str(actual_type), " to a ", type_to_str(target));
        return code;
    }

    if (target->tag == BigIntType)
        return compile(env, ast);

    if (target->tag == OptionalType && Match(target, OptionalType)->type)
        return compile_int_to_type(env, ast, Match(target, OptionalType)->type);

    const char *literal = Match(ast, Int)->str;
    OptionalInt_t int_val = Int$from_str(literal);
    if (int_val.small == 0)
        code_err(ast, "Failed to parse this integer");

    mpz_t i;
    mpz_init_set_int(i, int_val);

    char *c_literal;
    if (strncmp(literal, "0x", 2) == 0 || strncmp(literal, "0X", 2) == 0 || strncmp(literal, "0b", 2) == 0) {
        gmp_asprintf(&c_literal, "0x%ZX", i);
    } else if (strncmp(literal, "0o", 2) == 0) {
        gmp_asprintf(&c_literal, "%#Zo", i);
    } else {
        gmp_asprintf(&c_literal, "%#Zd", i);
    }

    if (target->tag == ByteType) {
        if (mpz_cmp_si(i, UINT8_MAX) <= 0 && mpz_cmp_si(i, 0) >= 0)
            return Texts("(Byte_t)(", c_literal, ")");
        code_err(ast, "This integer cannot fit in a byte");
    } else if (target->tag == NumType) {
        if (Match(target, NumType)->bits == TYPE_NBITS64) {
            return Texts("N64(", c_literal, ")");
        } else {
            return Texts("N32(", c_literal, ")");
        }
    } else if (target->tag == IntType) {
        int64_t target_bits = (int64_t)Match(target, IntType)->bits;
        switch (target_bits) {
        case TYPE_IBITS64:
            if (mpz_cmp_si(i, INT64_MIN) == 0)
                return Text("I64(INT64_MIN)");
            if (mpz_cmp_si(i, INT64_MAX) <= 0 && mpz_cmp_si(i, INT64_MIN) >= 0)
                return Texts("I64(", c_literal, "L)");
            break;
        case TYPE_IBITS32:
            if (mpz_cmp_si(i, INT32_MAX) <= 0 && mpz_cmp_si(i, INT32_MIN) >= 0)
                return Texts("I32(", c_literal, ")");
            break;
        case TYPE_IBITS16:
            if (mpz_cmp_si(i, INT16_MAX) <= 0 && mpz_cmp_si(i, INT16_MIN) >= 0)
                return Texts("I16(", c_literal, ")");
            break;
        case TYPE_IBITS8:
            if (mpz_cmp_si(i, INT8_MAX) <= 0 && mpz_cmp_si(i, INT8_MIN) >= 0)
                return Texts("I8(", c_literal, ")");
            break;
        default: break;
        }
        code_err(ast, "This integer cannot fit in a ", target_bits, "-bit value");
    } else {
        code_err(ast, "I don't know how to compile this to a ", type_to_str(target));
    }
    return EMPTY_TEXT;
}

Text_t compile_arguments(env_t *env, ast_t *call_ast, arg_t *spec_args, arg_ast_t *call_args)
{
    Table_t used_args = {};
    Text_t code = EMPTY_TEXT;
    env_t *default_scope = new(env_t);
    *default_scope = *env;
    default_scope->locals = new(Table_t, .fallback=env->namespace_bindings ? env->namespace_bindings : env->globals);
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
                        if (int_val.small == 0)
                            code_err(call_arg->value, "Failed to parse this integer");
                        if (Match(spec_arg->type, NumType)->bits == TYPE_NBITS64)
                            value = Text$from_str(String(hex_double(Num$from_int(int_val, false))));
                        else
                            value = Text$from_str(String(hex_double((double)Num32$from_int(int_val, false)), "f"));
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
                    if (int_val.small == 0)
                        code_err(call_arg->value, "Failed to parse this integer");
                    if (Match(spec_arg->type, NumType)->bits == TYPE_NBITS64)
                        value = Text$from_str(String(hex_double(Num$from_int(int_val, false))));
                    else
                        value = Text$from_str(String(hex_double((double)Num32$from_int(int_val, false)), "f"));
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
      found_it: continue;
    }

    int64_t i = 1;
    for (arg_ast_t *call_arg = call_args; call_arg; call_arg = call_arg->next) {
        if (call_arg->name) {
            if (!Table$str_get(used_args, call_arg->name))
                code_err(call_arg->value, "There is no argument with the name '", call_arg->name, "'");
        } else {
            const char *pseudoname = String(i++);
            if (!Table$str_get(used_args, pseudoname))
                code_err(call_arg->value, "This is one argument too many!");
        }
    }
    return code;
}

Text_t compile_text_literal(Text_t literal)
{
    Text_t code = Text("\"");
    const char *utf8 = Text$as_c_string(literal);
    for (const char *p = utf8; *p; p++) {
        switch (*p) {
        case '\\': code = Texts(code, "\\\\"); break;
        case '"': code = Texts(code, "\\\""); break;
        case '\a': code = Texts(code, "\\a"); break;
        case '\b': code = Texts(code, "\\b"); break;
        case '\n': code = Texts(code, "\\n"); break;
        case '\r': code = Texts(code, "\\r"); break;
        case '\t': code = Texts(code, "\\t"); break;
        case '\v': code = Texts(code, "\\v"); break;
        default: {
            if (isprint(*p)) {
                code = Texts(code, Text$from_strn(p, 1));
            } else {
                uint8_t byte = *(uint8_t*)p;
                code = Texts(code, "\\x", String(hex(byte, .no_prefix=true, .uppercase=true, .digits=2)), "\"\"");
            }
            break;
        }
        }
    }
    return Texts(code, "\"");
}

PUREFUNC static bool string_literal_is_all_ascii(Text_t literal)
{
    TextIter_t state = NEW_TEXT_ITER_STATE(literal);
    for (int64_t i = 0; i < literal.length; i++) {
        int32_t g = Text$get_grapheme_fast(&state, i);
        if (g < 0 || g > 127 || !isascii(g))
            return false;
    }
    return true;
}

Text_t compile_none(type_t *t)
{
    if (t == NULL)
        compiler_err(NULL, NULL, NULL, "I can't compile a `none` value with no type");

    if (t->tag == OptionalType)
        t = Match(t, OptionalType)->type;

    if (t == NULL)
        compiler_err(NULL, NULL, NULL, "I can't compile a `none` value with no type");

    if (t == PATH_TYPE) return Text("NONE_PATH");
    else if (t == PATH_TYPE_TYPE) return Text("((OptionalPathType_t){})");

    switch (t->tag) {
    case BigIntType: return Text("NONE_INT");
    case IntType: {
        switch (Match(t, IntType)->bits) {
        case TYPE_IBITS8: return Text("NONE_INT8");
        case TYPE_IBITS16: return Text("NONE_INT16");
        case TYPE_IBITS32: return Text("NONE_INT32");
        case TYPE_IBITS64: return Text("NONE_INT64");
        default: errx(1, "Invalid integer bit size");
        }
        break;
    }
    case BoolType: return Text("NONE_BOOL");
    case ByteType: return Text("NONE_BYTE");
    case ListType: return Text("NONE_LIST");
    case TableType: return Text("NONE_TABLE");
    case SetType: return Text("NONE_TABLE");
    case TextType: return Text("NONE_TEXT");
    case CStringType: return Text("NULL");
    case PointerType: return Texts("((", compile_type(t), ")NULL)");
    case ClosureType: return Text("NONE_CLOSURE");
    case NumType: return Text("nan(\"none\")");
    case StructType: return Texts("((", compile_type(Type(OptionalType, .type=t)), "){.is_none=true})");
    case EnumType: {
        env_t *enum_env = Match(t, EnumType)->env;
        return Texts("((", compile_type(t), "){", namespace_name(enum_env, enum_env->namespace, Text("none")), "})");
    }
    default: compiler_err(NULL, NULL, NULL, "none isn't implemented for this type: ", type_to_str(t));
    }
    return EMPTY_TEXT;
}

Text_t compile_empty(type_t *t)
{
    if (t == NULL)
        compiler_err(NULL, NULL, NULL, "I can't compile a value with no type");

    if (t->tag == OptionalType)
        return compile_none(t);

    if (t == PATH_TYPE) return Text("NONE_PATH");
    else if (t == PATH_TYPE_TYPE) return Text("((OptionalPathType_t){})");

    switch (t->tag) {
    case BigIntType: return Text("I(0)");
    case IntType: {
        switch (Match(t, IntType)->bits) {
        case TYPE_IBITS8: return Text("I8(0)");
        case TYPE_IBITS16: return Text("I16(0)");
        case TYPE_IBITS32: return Text("I32(0)");
        case TYPE_IBITS64: return Text("I64(0)");
        default: errx(1, "Invalid integer bit size");
        }
        break;
    }
    case ByteType: return Text("((Byte_t)0)");
    case BoolType: return Text("((Bool_t)no)");
    case ListType: return Text("((List_t){})");
    case TableType: case SetType: return Text("((Table_t){})");
    case TextType: return Text("Text(\"\")");
    case CStringType: return Text("\"\"");
    case PointerType: {
        DeclareMatch(ptr, t, PointerType);
        Text_t empty_pointed = compile_empty(ptr->pointed);
        return empty_pointed.length == 0 ? EMPTY_TEXT : Texts(ptr->is_stack ? Text("stack(") : Text("heap("), empty_pointed, ")");
    }
    case NumType: {
        return Match(t, NumType)->bits == TYPE_NBITS32 ? Text("N32(0.0f)") : Text("N64(0.0)");
    }
    case StructType: {
        DeclareMatch(struct_, t, StructType);
        Text_t code = Texts("((", compile_type(t), "){");
        for (arg_t *field = struct_->fields; field; field = field->next) {
            Text_t empty_field = field->default_val
                ? compile(struct_->env, field->default_val)
                : compile_empty(field->type);
            if (empty_field.length == 0)
                return EMPTY_TEXT;

            code = Texts(code, empty_field);
            if (field->next)
                code = Texts(code, ", ");
        }
        return Texts(code, "})");
    }
    case EnumType: {
        DeclareMatch(enum_, t, EnumType);
        tag_t *tag = enum_->tags;
        assert(tag);
        assert(tag->type);
        if (Match(tag->type, StructType)->fields)
            return Texts("((", compile_type(t), "){.$tag=", String(tag->tag_value), ", .", tag->name, "=", compile_empty(tag->type), "})");
        else if (enum_has_fields(t))
            return Texts("((", compile_type(t), "){.$tag=", String(tag->tag_value), "})");
        else
            return Texts("((", compile_type(t), ")", String(tag->tag_value), ")");
    }
    default: return EMPTY_TEXT;
    }
    return EMPTY_TEXT;
}

static Text_t compile_declared_value(env_t *env, ast_t *declare_ast)
{
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
            code_err(declare_ast, "This type (", type_to_str(t), ") cannot be uninitialized. You must provide a value.");
        return val_code;
    }
}

ast_t *add_to_table_comprehension(ast_t *entry, ast_t *subject)
{
    DeclareMatch(e, entry, TableEntry);
    return WrapAST(entry, MethodCall, .name="set", .self=subject,
                   .args=new(arg_ast_t, .value=e->key, .next=new(arg_ast_t, .value=e->value)));
}

ast_t *add_to_list_comprehension(ast_t *item, ast_t *subject)
{
    return WrapAST(item, MethodCall, .name="insert", .self=subject, .args=new(arg_ast_t, .value=item));
}

ast_t *add_to_set_comprehension(ast_t *item, ast_t *subject)
{
    return WrapAST(item, MethodCall, .name="add", .self=subject, .args=new(arg_ast_t, .value=item));
}

Text_t compile(env_t *env, ast_t *ast)
{
    switch (ast->tag) {
    case None: {
        code_err(ast, "I can't figure out what this `none`'s type is!");
    }
    case Bool: return Match(ast, Bool)->b ? Text("yes") : Text("no");
    case Var: {
        binding_t *b = get_binding(env, Match(ast, Var)->name);
        if (b)
            return b->code.length > 0 ? b->code : Texts("_$", Match(ast, Var)->name);
        // return Texts("_$", Match(ast, Var)->name);
        code_err(ast, "I don't know of any variable by this name");
    }
    case Int: {
        const char *str = Match(ast, Int)->str;
        OptionalInt_t int_val = Int$from_str(str);
        if (int_val.small == 0)
            code_err(ast, "Failed to parse this integer");
        mpz_t i;
        mpz_init_set_int(i, int_val);
        if (mpz_cmpabs_ui(i, BIGGEST_SMALL_INT) <= 0) {
            return Texts("I_small(", str, ")");
        } else if (mpz_cmp_si(i, INT64_MAX) <= 0 && mpz_cmp_si(i, INT64_MIN) >= 0) {
            return Texts("Int$from_int64(", str, ")");
        } else {
            return Texts("Int$from_str(\"", str, "\")");
        }
    }
    case Num: {
        return Text$from_str(String(hex_double(Match(ast, Num)->n)));
    }
    case Not: {
        ast_t *value = Match(ast, Not)->value;
        type_t *t = get_type(env, value);

        binding_t *b = get_namespace_binding(env, value, "negated");
        if (b && b->type->tag == FunctionType) {
            DeclareMatch(fn, b->type, FunctionType);
            if (fn->args && can_promote(t, get_arg_type(env, fn->args)))
                return Texts(b->code, "(", compile_arguments(env, ast, fn->args, new(arg_ast_t, .value=value)), ")");
        }

        if (t->tag == BoolType)
            return Texts("!(", compile(env, value), ")");
        else if (t->tag == IntType || t->tag == ByteType)
            return Texts("~(", compile(env, value), ")");
        else if (t->tag == ListType)
            return Texts("((", compile(env, value), ").length == 0)");
        else if (t->tag == SetType || t->tag == TableType)
            return Texts("((", compile(env, value), ").entries.length == 0)");
        else if (t->tag == TextType)
            return Texts("(", compile(env, value), ".length == 0)");
        else if (t->tag == OptionalType)
            return check_none(t, compile(env, value));

        code_err(ast, "I don't know how to negate values of type ", type_to_str(t));
    }
    case Negative: {
        ast_t *value = Match(ast, Negative)->value;
        type_t *t = get_type(env, value);
        binding_t *b = get_namespace_binding(env, value, "negative");
        if (b && b->type->tag == FunctionType) {
            DeclareMatch(fn, b->type, FunctionType);
            if (fn->args && can_promote(t, get_arg_type(env, fn->args)))
                return Texts(b->code, "(", compile_arguments(env, ast, fn->args, new(arg_ast_t, .value=value)), ")");
        }

        if (t->tag == IntType || t->tag == NumType)
            return Texts("-(", compile(env, value), ")");

        code_err(ast, "I don't know how to get the negative value of type ", type_to_str(t));

    }
    case HeapAllocate: case StackReference: {
        return compile_typed_allocation(env, ast, get_type(env, ast));
    }
    case Optional: {
        ast_t *value = Match(ast, Optional)->value;
        Text_t value_code = compile(env, value);
        return promote_to_optional(get_type(env, value), value_code);
    }
    case NonOptional: {
        ast_t *value = Match(ast, NonOptional)->value;
        type_t *t = get_type(env, value);
        Text_t value_code = compile(env, value);
        int64_t line = get_line_number(ast->file, ast->start);
        return Texts("({ ", compile_declaration(t, Text("opt")), " = ", value_code, "; ",
                        "if unlikely (", check_none(t, Text("opt")), ")\n",
                        "#line ", String(line), "\n",
                        "fail_source(", quoted_str(ast->file->filename), ", ",
                        String((int64_t)(value->start - value->file->text)), ", ", 
                        String((int64_t)(value->end - value->file->text)), ", ",
                        "\"This was expected to be a value, but it's none\");\n",
                        optional_into_nonnone(t, Text("opt")), "; })");
    }
    case Power: case Multiply: case Divide: case Mod: case Mod1: case Plus: case Minus: case Concat:
    case LeftShift: case UnsignedLeftShift: case RightShift: case UnsignedRightShift: case And: case Or: case Xor: {
        return compile_binary_op(env, ast);
    }
    case Equals: case NotEquals: {
        binary_operands_t binop = BINARY_OPERANDS(ast);

        type_t *lhs_t = get_type(env, binop.lhs);
        type_t *rhs_t = get_type(env, binop.rhs);
        type_t *operand_t;
        if (binop.lhs->tag == Int && is_numeric_type(rhs_t)) {
            operand_t = rhs_t;
        } else if (binop.rhs->tag == Int && is_numeric_type(lhs_t)) {
            operand_t = lhs_t;
        } else if (can_compile_to_type(env, binop.rhs, lhs_t)) {
            operand_t = lhs_t;
        } else if (can_compile_to_type(env, binop.lhs, rhs_t)) {
            operand_t = rhs_t;
        } else {
            code_err(ast, "I can't do comparisons between ", type_to_str(lhs_t), " and ", type_to_str(rhs_t));
        }

        Text_t lhs, rhs;
        lhs = compile_to_type(env, binop.lhs, operand_t);
        rhs = compile_to_type(env, binop.rhs, operand_t);

        switch (operand_t->tag) {
        case BigIntType:
            return Texts(ast->tag == Equals ? EMPTY_TEXT : Text("!"), "Int$equal_value(", lhs, ", ", rhs, ")");
        case BoolType: case ByteType: case IntType: case NumType: case PointerType: case FunctionType:
            return Texts("(", lhs, ast->tag == Equals ? " == " : " != ", rhs, ")");
        default:
            return Texts(ast->tag == Equals ? EMPTY_TEXT : Text("!"),
                            "generic_equal(stack(", lhs, "), stack(", rhs, "), ", compile_type_info(operand_t), ")");
        }
    }
    case LessThan: case LessThanOrEquals: case GreaterThan: case GreaterThanOrEquals: case Compare: {
        binary_operands_t cmp = BINARY_OPERANDS(ast);

        type_t *lhs_t = get_type(env, cmp.lhs);
        type_t *rhs_t = get_type(env, cmp.rhs);
        type_t *operand_t;
        if (cmp.lhs->tag == Int && is_numeric_type(rhs_t)) {
            operand_t = rhs_t;
        } else if (cmp.rhs->tag == Int && is_numeric_type(lhs_t)) {
            operand_t = lhs_t;
        } else if (can_compile_to_type(env, cmp.rhs, lhs_t)) {
            operand_t = lhs_t;
        } else if (can_compile_to_type(env, cmp.lhs, rhs_t)) {
            operand_t = rhs_t;
        } else {
            code_err(ast, "I can't do comparisons between ", type_to_str(lhs_t), " and ", type_to_str(rhs_t));
        }

        Text_t lhs = compile_to_type(env, cmp.lhs, operand_t);
        Text_t rhs = compile_to_type(env, cmp.rhs, operand_t);

        if (ast->tag == Compare)
            return Texts("generic_compare(stack(", lhs, "), stack(", rhs, "), ",
                            compile_type_info(operand_t), ")");

        const char *op = binop_operator(ast->tag);
        switch (operand_t->tag) {
        case BigIntType:
            return Texts("(Int$compare_value(", lhs, ", ", rhs, ") ", op, " 0)");
        case BoolType: case ByteType: case IntType: case NumType: case PointerType: case FunctionType:
            return Texts("(", lhs, " ", op, " ", rhs, ")");
        default:
            return Texts("(generic_compare(stack(", lhs, "), stack(", rhs, "), ",
                            compile_type_info(operand_t), ") ", op, " 0)");
        }
    }
    case TextLiteral: {
        Text_t literal = Match(ast, TextLiteral)->text; 
        if (literal.length == 0)
            return Text("EMPTY_TEXT");

        if (string_literal_is_all_ascii(literal))
            return Texts("Text(", compile_text_literal(literal), ")");
        else
            return Texts("Text$from_str(", compile_text_literal(literal), ")");
    }
    case TextJoin: {
        const char *lang = Match(ast, TextJoin)->lang;
        Text_t colorize = Match(ast, TextJoin)->colorize ? Text("yes") : Text("no");

        type_t *text_t = lang ? Table$str_get(*env->types, lang) : TEXT_TYPE;
        if (!text_t || text_t->tag != TextType)
            code_err(ast, quoted(lang), " is not a valid text language name");

        Text_t lang_constructor;
        if (!lang || streq(lang, "Text"))
            lang_constructor = Text("Text");
        else
            lang_constructor = namespace_name(Match(text_t, TextType)->env, Match(text_t, TextType)->env->namespace->parent, Text$from_str(lang));

        ast_list_t *chunks = Match(ast, TextJoin)->children;
        if (!chunks) {
            return Texts(lang_constructor, "(\"\")");
        } else if (!chunks->next && chunks->ast->tag == TextLiteral) {
            Text_t literal = Match(chunks->ast, TextLiteral)->text; 
            if (string_literal_is_all_ascii(literal))
                return Texts(lang_constructor, "(", compile_text_literal(literal), ")");
            return Texts("((", compile_type(text_t), ")", compile(env, chunks->ast), ")");
        } else {
            Text_t code = EMPTY_TEXT;
            for (ast_list_t *chunk = chunks; chunk; chunk = chunk->next) {
                Text_t chunk_code;
                type_t *chunk_t = get_type(env, chunk->ast);
                if (chunk->ast->tag == TextLiteral || type_eq(chunk_t, text_t)) {
                    chunk_code = compile(env, chunk->ast);
                } else {
                    binding_t *constructor = get_constructor(env, text_t, new(arg_ast_t, .value=chunk->ast),
                                                             env->current_type != NULL && type_eq(env->current_type, text_t));
                    if (constructor) {
                        arg_t *arg_spec = Match(constructor->type, FunctionType)->args;
                        arg_ast_t *args = new(arg_ast_t, .value=chunk->ast);
                        chunk_code = Texts(constructor->code, "(", compile_arguments(env, ast, arg_spec, args), ")");
                    } else if (type_eq(text_t, TEXT_TYPE)) {
                        if (chunk_t->tag == TextType)
                            chunk_code = compile(env, chunk->ast);
                        else
                            chunk_code = compile_text(env, chunk->ast, colorize);
                    } else {
                        code_err(chunk->ast, "I don't know how to convert ", type_to_str(chunk_t), " to ", type_to_str(text_t));
                    }
                }
                code = Texts(code, chunk_code);
                if (chunk->next) code = Texts(code, ", ");
            }
            if (chunks->next)
                return Texts(lang_constructor, "s(", code, ")");
            else
                return code;
        }
    }
    case Path: {
        return Texts("Path(", compile_text_literal(Text$from_str(Match(ast, Path)->path)), ")");
    }
    case Block: {
        ast_list_t *stmts = Match(ast, Block)->statements;
        if (stmts && !stmts->next)
            return compile(env, stmts->ast);

        Text_t code = Text("({\n");
        deferral_t *prev_deferred = env->deferred;
        env = fresh_scope(env);
        for (ast_list_t *stmt = stmts; stmt; stmt = stmt->next)
            prebind_statement(env, stmt->ast);
        for (ast_list_t *stmt = stmts; stmt; stmt = stmt->next) {
            if (stmt->next) {
                code = Texts(code, compile_statement(env, stmt->ast), "\n");
            } else {
                // TODO: put defer after evaluating block expression
                for (deferral_t *deferred = env->deferred; deferred && deferred != prev_deferred; deferred = deferred->next) {
                    code = Texts(code, compile_statement(deferred->defer_env, deferred->block));
                }
                code = Texts(code, compile(env, stmt->ast), ";\n");
            }
            bind_statement(env, stmt->ast);
        }

        return Texts(code, "})");
    }
    case Min: case Max: {
        type_t *t = get_type(env, ast);
        ast_t *key = ast->tag == Min ? Match(ast, Min)->key : Match(ast, Max)->key;
        ast_t *lhs = ast->tag == Min ? Match(ast, Min)->lhs : Match(ast, Max)->lhs;
        ast_t *rhs = ast->tag == Min ? Match(ast, Min)->rhs : Match(ast, Max)->rhs;
        const char *key_name = "$";
        if (key == NULL) key = FakeAST(Var, key_name);

        env_t *expr_env = fresh_scope(env);
        set_binding(expr_env, key_name, t, Text("ternary$lhs"));
        Text_t lhs_key = compile(expr_env, key);

        set_binding(expr_env, key_name, t, Text("ternary$rhs"));
        Text_t rhs_key = compile(expr_env, key);

        type_t *key_t = get_type(expr_env, key);
        Text_t comparison;
        if (key_t->tag == BigIntType)
            comparison = Texts("(Int$compare_value(", lhs_key, ", ", rhs_key, ")", (ast->tag == Min ? "<=" : ">="), "0)");
        else if (key_t->tag == IntType || key_t->tag == NumType || key_t->tag == BoolType || key_t->tag == PointerType || key_t->tag == ByteType)
            comparison = Texts("((", lhs_key, ")", (ast->tag == Min ? "<=" : ">="), "(", rhs_key, "))");
        else
            comparison = Texts("generic_compare(stack(", lhs_key, "), stack(", rhs_key, "), ", compile_type_info(key_t), ")",
                                  (ast->tag == Min ? "<=" : ">="), "0");

        return Texts(
            "({\n",
            compile_type(t), " ternary$lhs = ", compile(env, lhs), ", ternary$rhs = ", compile(env, rhs), ";\n",
            comparison, " ? ternary$lhs : ternary$rhs;\n"
            "})");
    }
    case List: {
        DeclareMatch(list, ast, List);
        if (!list->items)
            return Text("(List_t){.length=0}");

        type_t *list_type = get_type(env, ast);
        return compile_typed_list(env, ast, list_type);
    }
    case Table: {
        DeclareMatch(table, ast, Table);
        if (!table->entries) {
            Text_t code = Text("((Table_t){");
            if (table->fallback)
                code = Texts(code, ".fallback=heap(", compile(env, table->fallback),")");
            return Texts(code, "})");
        }

        type_t *table_type = get_type(env, ast);
        return compile_typed_table(env, ast, table_type);
    }
    case Set: {
        DeclareMatch(set, ast, Set);
        if (!set->items)
            return Text("((Table_t){})");

        type_t *set_type = get_type(env, ast);
        return compile_typed_set(env, ast, set_type);
    }
    case Comprehension: {
        ast_t *base = Match(ast, Comprehension)->expr;
        while (base->tag == Comprehension)
            base = Match(ast, Comprehension)->expr;
        if (base->tag == TableEntry)
            return compile(env, WrapAST(ast, Table, .entries=new(ast_list_t, .ast=ast)));
        else
            return compile(env, WrapAST(ast, List, .items=new(ast_list_t, .ast=ast)));
    }
    case Lambda: {
        DeclareMatch(lambda, ast, Lambda);
        Text_t name = namespace_name(env, env->namespace, Texts("lambda$", String(lambda->id)));

        env_t *body_scope = fresh_scope(env);
        body_scope->deferred = NULL;
        for (arg_ast_t *arg = lambda->args; arg; arg = arg->next) {
            type_t *arg_type = get_arg_ast_type(env, arg);
            set_binding(body_scope, arg->name, arg_type, Texts("_$", arg->name));
        }

        type_t *ret_t = get_type(body_scope, lambda->body);
        if (ret_t->tag == ReturnType)
            ret_t = Match(ret_t, ReturnType)->ret;

        if (lambda->ret_type) {
            type_t *declared = parse_type_ast(env, lambda->ret_type);
            if (can_promote(ret_t, declared))
                ret_t = declared;
            else
                code_err(ast, "This function was declared to return a value of type ", type_to_str(declared),
                         ", but actually returns a value of type ", type_to_str(ret_t));
        }

        body_scope->fn_ret = ret_t;

        Table_t closed_vars = get_closed_vars(env, lambda->args, ast);
        if (Table$length(closed_vars) > 0) { // Create a typedef for the lambda's closure userdata
            Text_t def = Text("typedef struct {");
            for (int64_t i = 0; i < closed_vars.entries.length; i++) {
                struct { const char *name; binding_t *b; } *entry = closed_vars.entries.data + closed_vars.entries.stride*i;
                if (has_stack_memory(entry->b->type))
                    code_err(ast, "This function is holding onto a reference to ", type_to_str(entry->b->type),
                             " stack memory in the variable `", entry->name, "`, but the function may outlive the stack memory");
                if (entry->b->type->tag == ModuleType)
                    continue;
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
                struct { const char *name; binding_t *b; } *entry = closed_vars.entries.data + closed_vars.entries.stride*i;
                if (entry->b->type->tag == ModuleType)
                    continue;
                binding_t *b = get_binding(env, entry->name);
                assert(b);
                Text_t binding_code = b->code;
                if (entry->b->type->tag == ListType)
                    userdata = Texts(userdata, ", LIST_COPY(", binding_code, ")");
                else if (entry->b->type->tag == TableType || entry->b->type->tag == SetType)
                    userdata = Texts(userdata, ", TABLE_COPY(", binding_code, ")");
                else 
                    userdata = Texts(userdata, ", ", binding_code);
            }
            userdata = Texts(userdata, ")");
            code = Texts(code, name, "$userdata_t *userdata)");
        }

        Text_t body = EMPTY_TEXT;
        for (ast_list_t *stmt = Match(lambda->body, Block)->statements; stmt; stmt = stmt->next) {
            if (stmt->next || ret_t->tag == VoidType || ret_t->tag == AbortType || get_type(body_scope, stmt->ast)->tag == ReturnType)
                body = Texts(body, compile_statement(body_scope, stmt->ast), "\n");
            else
                body = Texts(body, compile_statement(body_scope, FakeAST(Return, stmt->ast)), "\n");
            bind_statement(body_scope, stmt->ast);
        }
        if ((ret_t->tag == VoidType || ret_t->tag == AbortType) && body_scope->deferred)
            body = Texts(body, compile_statement(body_scope, FakeAST(Return)), "\n");

        env->code->lambdas = Texts(env->code->lambdas, code, " {\n", body, "\n}\n");
        return Texts("((Closure_t){", name, ", ", userdata, "})");
    }
    case MethodCall: {
        DeclareMatch(call, ast, MethodCall);
        type_t *self_t = get_type(env, call->self);

        if (streq(call->name, "serialized")) {
            if (call->args)
                code_err(ast, ".serialized() doesn't take any arguments"); 
            return Texts("generic_serialize((", compile_declaration(self_t, Text("[1]")), "){",
                            compile(env, call->self), "}, ", compile_type_info(self_t), ")");
        }

        int64_t pointer_depth = 0;
        type_t *self_value_t = self_t;
        for (; self_value_t->tag == PointerType; self_value_t = Match(self_value_t, PointerType)->pointed)
            pointer_depth += 1;

        if (self_value_t->tag == TypeInfoType || self_value_t->tag == ModuleType) {
            return compile(env, WrapAST(ast, FunctionCall, .fn=WrapAST(call->self, FieldAccess, .fielded=call->self, .field=call->name),
                                        .args=call->args));
        }

        type_t *field_type = get_field_type(self_value_t, call->name);
        if (field_type && field_type->tag == ClosureType)
            field_type = Match(field_type, ClosureType)->fn;
        if (field_type && field_type->tag == FunctionType)
            return compile(env, WrapAST(ast, FunctionCall, .fn=WrapAST(call->self, FieldAccess, .fielded=call->self, .field=call->name),
                                        .args=call->args));

        Text_t self = compile(env, call->self);

#define EXPECT_POINTER(article, name) do { \
    if (pointer_depth < 1) code_err(call->self, "I expected "article" "name" pointer here, not "article" "name" value"); \
    else if (pointer_depth > 1) code_err(call->self, "I expected "article" "name" pointer here, not a nested "name" pointer"); \
} while (0)
        switch (self_value_t->tag) {
        case ListType: {
            type_t *item_t = Match(self_value_t, ListType)->item_type;
            Text_t padded_item_size = Texts("sizeof(", compile_type(item_t), ")");

            if (streq(call->name, "insert")) {
                EXPECT_POINTER("a", "list");
                arg_t *arg_spec = new(arg_t, .name="item", .type=item_t,
                                      .next=new(arg_t, .name="at", .type=INT_TYPE, .default_val=FakeAST(Int, .str="0")));
                return Texts("List$insert_value(", self, ", ", compile_arguments(env, ast, arg_spec, call->args), ", ",
                                padded_item_size, ")");
            } else if (streq(call->name, "insert_all")) {
                EXPECT_POINTER("a", "list");
                arg_t *arg_spec = new(arg_t, .name="items", .type=self_value_t,
                                      .next=new(arg_t, .name="at", .type=INT_TYPE, .default_val=FakeAST(Int, .str="0")));
                return Texts("List$insert_all(", self, ", ", compile_arguments(env, ast, arg_spec, call->args), ", ",
                                padded_item_size, ")");
            } else if (streq(call->name, "remove_at")) {
                EXPECT_POINTER("a", "list");
                arg_t *arg_spec = new(arg_t, .name="index", .type=INT_TYPE, .default_val=FakeAST(Int, .str="-1"),
                                      .next=new(arg_t, .name="count", .type=INT_TYPE, .default_val=FakeAST(Int, .str="1")));
                return Texts("List$remove_at(", self, ", ", compile_arguments(env, ast, arg_spec, call->args), ", ",
                                padded_item_size, ")");
            } else if (streq(call->name, "remove_item")) {
                EXPECT_POINTER("a", "list");
                arg_t *arg_spec = new(arg_t, .name="item", .type=item_t,
                                      .next=new(arg_t, .name="max_count", .type=INT_TYPE, .default_val=FakeAST(Int, .str="-1")));
                return Texts("List$remove_item_value(", self, ", ", compile_arguments(env, ast, arg_spec, call->args), ", ",
                                compile_type_info(self_value_t), ")");
            } else if (streq(call->name, "has")) {
                self = compile_to_pointer_depth(env, call->self, 0, false);
                arg_t *arg_spec = new(arg_t, .name="item", .type=item_t);
                return Texts("List$has_value(", self, ", ", compile_arguments(env, ast, arg_spec, call->args), ", ",
                                compile_type_info(self_value_t), ")");
            } else if (streq(call->name, "sample")) {
                type_t *random_num_type = parse_type_string(env, "func(->Num)?");
                self = compile_to_pointer_depth(env, call->self, 0, false);
                arg_t *arg_spec = new(arg_t, .name="count", .type=INT_TYPE,
                    .next=new(arg_t, .name="weights", .type=Type(ListType, .item_type=Type(NumType, .bits=TYPE_NBITS64)),
                              .default_val=FakeAST(None),
                              .next=new(arg_t, .name="random", .type=random_num_type, .default_val=FakeAST(None))));
                return Texts("List$sample(", self, ", ", compile_arguments(env, ast, arg_spec, call->args), ", ",
                                padded_item_size, ")");
            } else if (streq(call->name, "shuffle")) {
                type_t *random_int64_type = parse_type_string(env, "func(min,max:Int64->Int64)?");
                EXPECT_POINTER("a", "list");
                arg_t *arg_spec = new(arg_t, .name="random", .type=random_int64_type, .default_val=FakeAST(None));
                return Texts("List$shuffle(", self, ", ", compile_arguments(env, ast, arg_spec, call->args), ", ", padded_item_size, ")");
            } else if (streq(call->name, "shuffled")) {
                type_t *random_int64_type = parse_type_string(env, "func(min,max:Int64->Int64)?");
                self = compile_to_pointer_depth(env, call->self, 0, false);
                arg_t *arg_spec = new(arg_t, .name="random", .type=random_int64_type, .default_val=FakeAST(None));
                return Texts("List$shuffled(", self, ", ", compile_arguments(env, ast, arg_spec, call->args), ", ", padded_item_size, ")");
            } else if (streq(call->name, "random")) {
                type_t *random_int64_type = parse_type_string(env, "func(min,max:Int64->Int64)?");
                self = compile_to_pointer_depth(env, call->self, 0, false);
                arg_t *arg_spec = new(arg_t, .name="random", .type=random_int64_type, .default_val=FakeAST(None));
                return Texts("List$random_value(", self, ", ", compile_arguments(env, ast, arg_spec, call->args), ", ", compile_type(item_t), ")");
            } else if (streq(call->name, "sort") || streq(call->name, "sorted")) {
                if (streq(call->name, "sort"))
                    EXPECT_POINTER("a", "list");
                else
                    self = compile_to_pointer_depth(env, call->self, 0, false);
                Text_t comparison;
                if (call->args) {
                    type_t *item_ptr = Type(PointerType, .pointed=item_t, .is_stack=true);
                    type_t *fn_t = NewFunctionType(Type(IntType, .bits=TYPE_IBITS32), {.name="x", .type=item_ptr}, {.name="y", .type=item_ptr});
                    arg_t *arg_spec = new(arg_t, .name="by", .type=Type(ClosureType, .fn=fn_t));
                    comparison = compile_arguments(env, ast, arg_spec, call->args);
                } else {
                    comparison = Texts("((Closure_t){.fn=generic_compare, .userdata=(void*)", compile_type_info(item_t), "})");
                }
                return Texts("List$", call->name, "(", self, ", ", comparison, ", ", padded_item_size, ")");
            } else if (streq(call->name, "heapify")) {
                EXPECT_POINTER("a", "list");
                Text_t comparison;
                if (call->args) {
                    type_t *item_ptr = Type(PointerType, .pointed=item_t, .is_stack=true);
                    type_t *fn_t = NewFunctionType(Type(IntType, .bits=TYPE_IBITS32), {.name="x", .type=item_ptr}, {.name="y", .type=item_ptr});
                    arg_t *arg_spec = new(arg_t, .name="by", .type=Type(ClosureType, .fn=fn_t));
                    comparison = compile_arguments(env, ast, arg_spec, call->args);
                } else {
                    comparison = Texts("((Closure_t){.fn=generic_compare, .userdata=(void*)", compile_type_info(item_t), "})");
                }
                return Texts("List$heapify(", self, ", ", comparison, ", ", padded_item_size, ")");
            } else if (streq(call->name, "heap_push")) {
                EXPECT_POINTER("a", "list");
                type_t *item_ptr = Type(PointerType, .pointed=item_t, .is_stack=true);
                type_t *fn_t = NewFunctionType(Type(IntType, .bits=TYPE_IBITS32), {.name="x", .type=item_ptr}, {.name="y", .type=item_ptr});
                ast_t *default_cmp = LiteralCode(Texts("((Closure_t){.fn=generic_compare, .userdata=(void*)",
                                                          compile_type_info(item_t), "})"),
                                                 .type=Type(ClosureType, .fn=fn_t));
                arg_t *arg_spec = new(arg_t, .name="item", .type=item_t,
                                      .next=new(arg_t, .name="by", .type=Type(ClosureType, .fn=fn_t), .default_val=default_cmp));
                Text_t arg_code = compile_arguments(env, ast, arg_spec, call->args);
                return Texts("List$heap_push_value(", self, ", ", arg_code, ", ", padded_item_size, ")");
            } else if (streq(call->name, "heap_pop")) {
                EXPECT_POINTER("a", "list");
                type_t *item_ptr = Type(PointerType, .pointed=item_t, .is_stack=true);
                type_t *fn_t = NewFunctionType(Type(IntType, .bits=TYPE_IBITS32), {.name="x", .type=item_ptr}, {.name="y", .type=item_ptr});
                ast_t *default_cmp = LiteralCode(Texts("((Closure_t){.fn=generic_compare, .userdata=(void*)",
                                                          compile_type_info(item_t), "})"),
                                                 .type=Type(ClosureType, .fn=fn_t));
                arg_t *arg_spec = new(arg_t, .name="by", .type=Type(ClosureType, .fn=fn_t), .default_val=default_cmp);
                Text_t arg_code = compile_arguments(env, ast, arg_spec, call->args);
                return Texts("List$heap_pop_value(", self, ", ", arg_code, ", ", compile_type(item_t), ", _, ",
                                promote_to_optional(item_t, Text("_")), ", ", compile_none(item_t), ")");
            } else if (streq(call->name, "binary_search")) {
                self = compile_to_pointer_depth(env, call->self, 0, call->args != NULL);
                type_t *item_ptr = Type(PointerType, .pointed=item_t, .is_stack=true);
                type_t *fn_t = NewFunctionType(Type(IntType, .bits=TYPE_IBITS32), {.name="x", .type=item_ptr}, {.name="y", .type=item_ptr});
                ast_t *default_cmp = LiteralCode(
                    Texts("((Closure_t){.fn=generic_compare, .userdata=(void*)",
                             compile_type_info(item_t), "})"),
                    .type=Type(ClosureType, .fn=fn_t));
                arg_t *arg_spec = new(arg_t, .name="target", .type=item_t,
                                      .next=new(arg_t, .name="by", .type=Type(ClosureType, .fn=fn_t), .default_val=default_cmp));
                Text_t arg_code = compile_arguments(env, ast, arg_spec, call->args);
                return Texts("List$binary_search_value(", self, ", ", arg_code, ")");
            } else if (streq(call->name, "clear")) {
                EXPECT_POINTER("a", "list");
                (void)compile_arguments(env, ast, NULL, call->args);
                return Texts("List$clear(", self, ")");
            } else if (streq(call->name, "find")) {
                self = compile_to_pointer_depth(env, call->self, 0, false);
                arg_t *arg_spec = new(arg_t, .name="item", .type=item_t);
                return Texts("List$find_value(", self, ", ", compile_arguments(env, ast, arg_spec, call->args),
                                ", ", compile_type_info(self_value_t), ")");
            } else if (streq(call->name, "where")) {
                self = compile_to_pointer_depth(env, call->self, 0, call->args != NULL);
                type_t *item_ptr = Type(PointerType, .pointed=item_t, .is_stack=true);
                type_t *predicate_type = Type(
                    ClosureType, .fn=NewFunctionType(Type(BoolType), {.name="item", .type=item_ptr}));
                arg_t *arg_spec = new(arg_t, .name="predicate", .type=predicate_type);
                return Texts("List$first(", self, ", ", compile_arguments(env, ast, arg_spec, call->args), ")");
            } else if (streq(call->name, "from")) {
                self = compile_to_pointer_depth(env, call->self, 0, true);
                arg_t *arg_spec = new(arg_t, .name="first", .type=INT_TYPE);
                return Texts("List$from(", self, ", ", compile_arguments(env, ast, arg_spec, call->args), ")");
            } else if (streq(call->name, "to")) {
                self = compile_to_pointer_depth(env, call->self, 0, true);
                arg_t *arg_spec = new(arg_t, .name="last", .type=INT_TYPE);
                return Texts("List$to(", self, ", ", compile_arguments(env, ast, arg_spec, call->args), ")");
            } else if (streq(call->name, "by")) {
                self = compile_to_pointer_depth(env, call->self, 0, true);
                arg_t *arg_spec = new(arg_t, .name="stride", .type=INT_TYPE);
                return Texts("List$by(", self, ", ", compile_arguments(env, ast, arg_spec, call->args), ", ", padded_item_size, ")");
            } else if (streq(call->name, "reversed")) {
                self = compile_to_pointer_depth(env, call->self, 0, true);
                (void)compile_arguments(env, ast, NULL, call->args);
                return Texts("List$reversed(", self, ", ", padded_item_size, ")");
            } else if (streq(call->name, "unique")) {
                self = compile_to_pointer_depth(env, call->self, 0, false);
                (void)compile_arguments(env, ast, NULL, call->args);
                return Texts("Table$from_entries(", self, ", Set$info(", compile_type_info(item_t), "))");
            } else if (streq(call->name, "pop")) {
                EXPECT_POINTER("a", "list");
                arg_t *arg_spec = new(arg_t, .name="index", .type=INT_TYPE, .default_val=FakeAST(Int, "-1"));
                Text_t index = compile_arguments(env, ast, arg_spec, call->args);
                return Texts("List$pop(", self, ", ", index, ", ", compile_type(item_t), ", _, ",
                             promote_to_optional(item_t, Text("_")), ", ", compile_none(item_t), ")");
            } else if (streq(call->name, "counts")) {
                self = compile_to_pointer_depth(env, call->self, 0, false);
                (void)compile_arguments(env, ast, NULL, call->args);
                return Texts("List$counts(", self, ", ", compile_type_info(self_value_t), ")");
            } else code_err(ast, "There is no '", call->name, "' method for lists");
        }
        case SetType: {
            DeclareMatch(set, self_value_t, SetType);
            if (streq(call->name, "has")) {
                self = compile_to_pointer_depth(env, call->self, 0, false);
                arg_t *arg_spec = new(arg_t, .name="key", .type=set->item_type);
                return Texts("Table$has_value(", self, ", ", compile_arguments(env, ast, arg_spec, call->args), ", ",
                                compile_type_info(self_value_t), ")");
            } else if (streq(call->name, "add")) {
                EXPECT_POINTER("a", "set");
                arg_t *arg_spec = new(arg_t, .name="item", .type=set->item_type);
                return Texts("Table$set_value(", self, ", ", compile_arguments(env, ast, arg_spec, call->args), ", NULL, ",
                                compile_type_info(self_value_t), ")");
            } else if (streq(call->name, "add_all")) {
                EXPECT_POINTER("a", "set");
                arg_t *arg_spec = new(arg_t, .name="items", .type=Type(ListType, .item_type=Match(self_value_t, SetType)->item_type));
                return Texts("({ Table_t *set = ", self, "; ",
                                "List_t to_add = ", compile_arguments(env, ast, arg_spec, call->args), "; ",
                                "for (int64_t i = 0; i < to_add.length; i++)\n"
                                "Table$set(set, to_add.data + i*to_add.stride, NULL, ", compile_type_info(self_value_t), ");\n",
                                "(void)0; })");
            } else if (streq(call->name, "remove")) {
                EXPECT_POINTER("a", "set");
                arg_t *arg_spec = new(arg_t, .name="item", .type=set->item_type);
                return Texts("Table$remove_value(", self, ", ", compile_arguments(env, ast, arg_spec, call->args), ", ",
                                compile_type_info(self_value_t), ")");
            } else if (streq(call->name, "remove_all")) {
                EXPECT_POINTER("a", "set");
                arg_t *arg_spec = new(arg_t, .name="items", .type=Type(ListType, .item_type=Match(self_value_t, SetType)->item_type));
                return Texts("({ Table_t *set = ", self, "; ",
                                "List_t to_add = ", compile_arguments(env, ast, arg_spec, call->args), "; ",
                                "for (int64_t i = 0; i < to_add.length; i++)\n"
                                "Table$remove(set, to_add.data + i*to_add.stride, ", compile_type_info(self_value_t), ");\n",
                                "(void)0; })");
            } else if (streq(call->name, "clear")) {
                EXPECT_POINTER("a", "set");
                (void)compile_arguments(env, ast, NULL, call->args);
                return Texts("Table$clear(", self, ")");
            } else if (streq(call->name, "with")) {
                self = compile_to_pointer_depth(env, call->self, 0, false);
                arg_t *arg_spec = new(arg_t, .name="other", .type=self_value_t);
                return Texts("Table$with(", self, ", ", compile_arguments(env, ast, arg_spec, call->args),
                                ", ", compile_type_info(self_value_t), ")");
            } else if (streq(call->name, "overlap")) {
                self = compile_to_pointer_depth(env, call->self, 0, false);
                arg_t *arg_spec = new(arg_t, .name="other", .type=self_value_t);
                return Texts("Table$overlap(", self, ", ", compile_arguments(env, ast, arg_spec, call->args),
                                ", ", compile_type_info(self_value_t), ")");
            } else if (streq(call->name, "without")) {
                self = compile_to_pointer_depth(env, call->self, 0, false);
                arg_t *arg_spec = new(arg_t, .name="other", .type=self_value_t);
                return Texts("Table$without(", self, ", ", compile_arguments(env, ast, arg_spec, call->args),
                                ", ", compile_type_info(self_value_t), ")");
            } else if (streq(call->name, "is_subset_of")) {
                self = compile_to_pointer_depth(env, call->self, 0, false);
                arg_t *arg_spec = new(arg_t, .name="other", .type=self_value_t,
                                      .next=new(arg_t, .name="strict", .type=Type(BoolType), .default_val=FakeAST(Bool, false)));
                return Texts("Table$is_subset_of(", self, ", ", compile_arguments(env, ast, arg_spec, call->args),
                                ", ", compile_type_info(self_value_t), ")");
            } else if (streq(call->name, "is_superset_of")) {
                self = compile_to_pointer_depth(env, call->self, 0, false);
                arg_t *arg_spec = new(arg_t, .name="other", .type=self_value_t,
                                      .next=new(arg_t, .name="strict", .type=Type(BoolType), .default_val=FakeAST(Bool, false)));
                return Texts("Table$is_superset_of(", self, ", ", compile_arguments(env, ast, arg_spec, call->args),
                                ", ", compile_type_info(self_value_t), ")");
            } else code_err(ast, "There is no '", call->name, "' method for tables");
        }
        case TableType: {
            DeclareMatch(table, self_value_t, TableType);
            if (streq(call->name, "get")) {
                self = compile_to_pointer_depth(env, call->self, 0, false);
                arg_t *arg_spec = new(arg_t, .name="key", .type=table->key_type);
                return Texts(
                    "Table$get_optional(", self, ", ", compile_type(table->key_type), ", ",
                    compile_type(table->value_type), ", ", compile_arguments(env, ast, arg_spec, call->args), ", ",
                    "_, ", optional_into_nonnone(table->value_type, Text("(*_)")), ", ", compile_none(table->value_type), ", ",
                    compile_type_info(self_value_t), ")");
            } else if (streq(call->name, "get_or_set")) {
                self = compile_to_pointer_depth(env, call->self, 1, false);
                arg_t *arg_spec = new(arg_t, .name="key", .type=table->key_type,
                                      .next=new(arg_t, .name="default", .type=table->value_type, .default_val=table->default_value));
                return Texts("*Table$get_or_setdefault(",
                                self, ", ", compile_type(table->key_type), ", ",
                                compile_type(table->value_type), ", ",
                                compile_arguments(env, ast, arg_spec, call->args), ", ",
                                compile_type_info(self_value_t), ")");
            } else if (streq(call->name, "has")) {
                self = compile_to_pointer_depth(env, call->self, 0, false);
                arg_t *arg_spec = new(arg_t, .name="key", .type=table->key_type);
                return Texts("Table$has_value(", self, ", ", compile_arguments(env, ast, arg_spec, call->args), ", ",
                                compile_type_info(self_value_t), ")");
            } else if (streq(call->name, "set")) {
                EXPECT_POINTER("a", "table");
                arg_t *arg_spec = new(arg_t, .name="key", .type=table->key_type,
                                      .next=new(arg_t, .name="value", .type=table->value_type));
                return Texts("Table$set_value(", self, ", ", compile_arguments(env, ast, arg_spec, call->args), ", ",
                                compile_type_info(self_value_t), ")");
            } else if (streq(call->name, "remove")) {
                EXPECT_POINTER("a", "table");
                arg_t *arg_spec = new(arg_t, .name="key", .type=table->key_type);
                return Texts("Table$remove_value(", self, ", ", compile_arguments(env, ast, arg_spec, call->args), ", ",
                                compile_type_info(self_value_t), ")");
            } else if (streq(call->name, "clear")) {
                EXPECT_POINTER("a", "table");
                (void)compile_arguments(env, ast, NULL, call->args);
                return Texts("Table$clear(", self, ")");
            } else if (streq(call->name, "sorted")) {
                self = compile_to_pointer_depth(env, call->self, 0, false);
                (void)compile_arguments(env, ast, NULL, call->args);
                return Texts("Table$sorted(", self, ", ", compile_type_info(self_value_t), ")");
            } else if (streq(call->name, "with_fallback")) {
                self = compile_to_pointer_depth(env, call->self, 0, false);
                arg_t *arg_spec = new(arg_t, .name="fallback", .type=Type(OptionalType, self_value_t));
                return Texts("Table$with_fallback(", self, ", ", compile_arguments(env, ast, arg_spec, call->args), ")");
            } else code_err(ast, "There is no '", call->name, "' method for tables");
        }
        default: {
            DeclareMatch(methodcall, ast, MethodCall);
            type_t *fn_t = get_method_type(env, methodcall->self, methodcall->name);
            arg_ast_t *args = new(arg_ast_t, .value=methodcall->self, .next=methodcall->args);
            binding_t *b = get_namespace_binding(env, methodcall->self, methodcall->name);
            if (!b) code_err(ast, "No such method");
            return Texts(b->code, "(", compile_arguments(env, ast, Match(fn_t, FunctionType)->args, args), ")");
        }
        }
#undef EXPECT_POINTER
    }
    case FunctionCall: {
        DeclareMatch(call, ast, FunctionCall);
        type_t *fn_t = get_type(env, call->fn);
        if (fn_t->tag == FunctionType) {
            Text_t fn = compile(env, call->fn);
            return Texts(fn, "(", compile_arguments(env, ast, Match(fn_t, FunctionType)->args, call->args), ")");
        } else if (fn_t->tag == TypeInfoType) {
            type_t *t = Match(fn_t, TypeInfoType)->type;

            // Literal constructors for numeric types like `Byte(123)` should not go through any conversion, just a cast:
            if (is_numeric_type(t) && call->args && !call->args->next && call->args->value->tag == Int)
                return compile_to_type(env, call->args->value, t);
            else if (t->tag == NumType && call->args && !call->args->next && call->args->value->tag == Num)
                return compile_to_type(env, call->args->value, t);

            binding_t *constructor = get_constructor(env, t, call->args,
                                                     env->current_type != NULL && type_eq(env->current_type, t));
            if (constructor) {
                arg_t *arg_spec = Match(constructor->type, FunctionType)->args;
                return Texts(constructor->code, "(", compile_arguments(env, ast, arg_spec, call->args), ")");
            }

            type_t *actual = call->args ? get_type(env, call->args->value) : NULL;
            if (t->tag == TextType) {
                if (!call->args) code_err(ast, "This constructor needs a value");
                if (!type_eq(t, TEXT_TYPE))
                    code_err(call->fn, "I don't have a constructor defined for these arguments");
                // Text constructor:
                if (!call->args || call->args->next)
                    code_err(call->fn, "This constructor takes exactly 1 argument");
                if (type_eq(actual, t))
                    return compile(env, call->args->value);
                return expr_as_text(compile(env, call->args->value), actual, Text("no"));
            } else if (t->tag == CStringType) {
                // C String constructor:
                if (!call->args || call->args->next)
                    code_err(call->fn, "This constructor takes exactly 1 argument");
                if (call->args->value->tag == TextLiteral)
                    return compile_text_literal(Match(call->args->value, TextLiteral)->text);
                else if (call->args->value->tag == TextJoin && Match(call->args->value, TextJoin)->children == NULL)
                    return Text("\"\"");
                else if (call->args->value->tag == TextJoin && Match(call->args->value, TextJoin)->children->next == NULL)
                    return compile_text_literal(Match(Match(call->args->value, TextJoin)->children->ast, TextLiteral)->text);
                return Texts("Text$as_c_string(", expr_as_text(compile(env, call->args->value), actual, Text("no")), ")");
            } else if (t->tag == StructType) {
                DeclareMatch(struct_, t, StructType);
                if (!struct_->opaque && is_valid_call(env, struct_->fields, call->args, true)) {
                    if (env->current_type == NULL || !type_eq(env->current_type, t)) {
                        for (arg_t *field = struct_->fields; field; field = field->next) {
                            if (field->name[0] == '_')
                                code_err(ast, "This struct can't be initialized directly because it has private fields (starting with underscore)");
                        }
                    }
                    return Texts("((", compile_type(t), "){",
                                    compile_arguments(env, ast, struct_->fields, call->args), "})");
                }
            }
            code_err(ast, "I could not find a constructor matching these arguments for ", type_to_str(t));
        } else if (fn_t->tag == ClosureType) {
            fn_t = Match(fn_t, ClosureType)->fn;
            arg_t *type_args = Match(fn_t, FunctionType)->args;

            arg_t *closure_fn_args = NULL;
            for (arg_t *arg = Match(fn_t, FunctionType)->args; arg; arg = arg->next)
                closure_fn_args = new(arg_t, .name=arg->name, .type=arg->type, .default_val=arg->default_val, .next=closure_fn_args);
            closure_fn_args = new(arg_t, .name="userdata", .type=Type(PointerType, .pointed=Type(MemoryType)), .next=closure_fn_args);
            REVERSE_LIST(closure_fn_args);
            Text_t fn_type_code = compile_type(Type(FunctionType, .args=closure_fn_args, .ret=Match(fn_t, FunctionType)->ret));

            Text_t closure = compile(env, call->fn);
            Text_t arg_code = compile_arguments(env, ast, type_args, call->args);
            if (arg_code.length > 0) arg_code = Texts(arg_code, ", ");
            if (call->fn->tag == Var) {
                return Texts("((", fn_type_code, ")", closure, ".fn)(", arg_code, closure, ".userdata)");
            } else {
                return Texts("({ Closure_t closure = ", closure, "; ((", fn_type_code, ")closure.fn)(",
                                arg_code, "closure.userdata); })");
            }
        } else {
            code_err(call->fn, "This is not a function, it's a ", type_to_str(fn_t));
        }
    }
    case Deserialize: {
        ast_t *value = Match(ast, Deserialize)->value;
        type_t *value_type = get_type(env, value);
        if (!type_eq(value_type, Type(ListType, Type(ByteType))))
            code_err(value, "This value should be a list of bytes, not a ", type_to_str(value_type));
        type_t *t = parse_type_ast(env, Match(ast, Deserialize)->type);
        return Texts("({ ", compile_declaration(t, Text("deserialized")), ";\n"
                        "generic_deserialize(", compile(env, value), ", &deserialized, ", compile_type_info(t), ");\n"
                        "deserialized; })");
    }
    case ExplicitlyTyped: {
        return compile_to_type(env, Match(ast, ExplicitlyTyped)->ast, get_type(env, ast));
    }
    case When: {
        DeclareMatch(original, ast, When);
        ast_t *when_var = WrapAST(ast, Var, .name="when"); 
        when_clause_t *new_clauses = NULL;
        type_t *subject_t = get_type(env, original->subject);
        for (when_clause_t *clause = original->clauses; clause; clause = clause->next) {
            type_t *clause_type = get_clause_type(env, subject_t, clause);
            if (clause_type->tag == AbortType || clause_type->tag == ReturnType) {
                new_clauses = new(when_clause_t, .pattern=clause->pattern, .body=clause->body, .next=new_clauses);
            } else {
                ast_t *assign = WrapAST(clause->body, Assign,
                                        .targets=new(ast_list_t, .ast=when_var),
                                        .values=new(ast_list_t, .ast=clause->body));
                new_clauses = new(when_clause_t, .pattern=clause->pattern, .body=assign, .next=new_clauses);
            }
        }
        REVERSE_LIST(new_clauses);
        ast_t *else_body = original->else_body;
        if (else_body) {
            type_t *clause_type = get_type(env, else_body);
            if (clause_type->tag != AbortType && clause_type->tag != ReturnType) {
                else_body = WrapAST(else_body, Assign,
                                    .targets=new(ast_list_t, .ast=when_var),
                                    .values=new(ast_list_t, .ast=else_body));
            }
        }

        type_t *t = get_type(env, ast);
        env_t *when_env = fresh_scope(env);
        set_binding(when_env, "when", t, Text("when"));
        return Texts(
            "({ ", compile_declaration(t, Text("when")), ";\n",
            compile_statement(when_env, WrapAST(ast, When, .subject=original->subject, .clauses=new_clauses, .else_body=else_body)),
            "when; })");
    }
    case If: {
        DeclareMatch(if_, ast, If);
        ast_t *condition = if_->condition;
        Text_t decl_code = EMPTY_TEXT;
        env_t *truthy_scope = env, *falsey_scope = env;

        Text_t condition_code;
        if (condition->tag == Declare) {
            DeclareMatch(decl, condition, Declare);
            if (decl->value == NULL)
                code_err(condition, "This declaration must have a value");
            type_t *condition_type = 
                decl->type ? parse_type_ast(env, decl->type)
                : get_type(env, Match(condition, Declare)->value);
            if (condition_type->tag != OptionalType)
                code_err(condition, "This `if var := ...:` declaration should be an optional type, not ", type_to_str(condition_type));

            if (is_incomplete_type(condition_type))
                code_err(condition, "This type is incomplete!");

            decl_code = compile_statement(env, condition);
            ast_t *var = Match(condition, Declare)->var;
            truthy_scope = fresh_scope(env);
            bind_statement(truthy_scope, condition);
            condition_code = compile_condition(truthy_scope, var);
            set_binding(truthy_scope, Match(var, Var)->name,
                        Match(condition_type, OptionalType)->type,
                        optional_into_nonnone(condition_type, compile(truthy_scope, var)));
        } else if (condition->tag == Var) {
            type_t *condition_type = get_type(env, condition);
            condition_code = compile_condition(env, condition);
            if (condition_type->tag == OptionalType) {
                truthy_scope = fresh_scope(env);
                set_binding(truthy_scope, Match(condition, Var)->name,
                            Match(condition_type, OptionalType)->type,
                            optional_into_nonnone(condition_type, compile(truthy_scope, condition)));
            }
        } else {
            condition_code = compile_condition(env, condition);
        }

        type_t *true_type = get_type(truthy_scope, if_->body);
        type_t *false_type = get_type(falsey_scope, if_->else_body);
        if (true_type->tag == AbortType || true_type->tag == ReturnType)
            return Texts("({ ", decl_code, "if (", condition_code, ") ", compile_statement(truthy_scope, if_->body),
                            "\n", compile(falsey_scope, if_->else_body), "; })");
        else if (false_type->tag == AbortType || false_type->tag == ReturnType)
            return Texts("({ ", decl_code, "if (!(", condition_code, ")) ", compile_statement(falsey_scope, if_->else_body),
                            "\n", compile(truthy_scope, if_->body), "; })");
        else if (decl_code.length > 0)
            return Texts("({ ", decl_code, "(", condition_code, ") ? ", compile(truthy_scope, if_->body), " : ",
                            compile(falsey_scope, if_->else_body), ";})");
        else
            return Texts("((", condition_code, ") ? ",
                            compile(truthy_scope, if_->body), " : ", compile(falsey_scope, if_->else_body), ")");
    }
    case Reduction: {
        DeclareMatch(reduction, ast, Reduction);
        ast_e op = reduction->op;

        type_t *iter_t = get_type(env, reduction->iter);
        type_t *item_t = get_iterated_type(iter_t);
        if (!item_t) code_err(reduction->iter, "I couldn't figure out how to iterate over this type: ", type_to_str(iter_t));

        static int64_t next_id = 1;
        ast_t *item = FakeAST(Var, String("$it", next_id++));
        ast_t *body = LiteralCode(Text("{}")); // placeholder
        ast_t *loop = FakeAST(For, .vars=new(ast_list_t, .ast=item), .iter=reduction->iter, .body=body);
        env_t *body_scope = for_scope(env, loop);
        if (op == Equals || op == NotEquals || op == LessThan || op == LessThanOrEquals || op == GreaterThan || op == GreaterThanOrEquals) {
            // Chained comparisons like ==, <, etc.
            type_t *item_value_type = item_t;
            ast_t *item_value = item;
            if (reduction->key) {
                set_binding(body_scope, "$", item_t, compile(body_scope, item));
                item_value = reduction->key;
                item_value_type = get_type(body_scope, reduction->key);
            }

            Text_t code = Texts(
                "({ // Reduction:\n",
                compile_declaration(item_value_type, Text("prev")), ";\n"
                "OptionalBool_t result = NONE_BOOL;\n"
                );

            ast_t *comparison = new(ast_t, .file=ast->file, .start=ast->start, .end=ast->end,
                                    .tag=op, .__data.Plus.lhs=LiteralCode(Text("prev"), .type=item_value_type), .__data.Plus.rhs=item_value);
            body->__data.InlineCCode.chunks = new(ast_list_t, .ast=FakeAST(TextLiteral, Texts(
                "if (result == NONE_BOOL) {\n"
                "    prev = ", compile(body_scope, item_value), ";\n"
                "    result = yes;\n"
                "} else {\n"
                "    if (", compile(body_scope, comparison), ") {\n",
                "        prev = ", compile(body_scope, item_value), ";\n",
                "    } else {\n"
                "        result = no;\n",
                "        break;\n",
                "    }\n",
                "}\n")));
            code = Texts(code, compile_statement(env, loop), "\nresult;})");
            return code;
        } else if (op == Min || op == Max) {
            // Min/max:
            Text_t superlative = op == Min ? Text("min") : Text("max");
            Text_t code = Texts(
                "({ // Reduction:\n",
                compile_declaration(item_t, superlative), ";\n"
                "Bool_t has_value = no;\n"
                );

            Text_t item_code = compile(body_scope, item);
            ast_e cmp_op = op == Min ? LessThan : GreaterThan;
            if (reduction->key) {
                env_t *key_scope = fresh_scope(env);
                set_binding(key_scope, "$", item_t, item_code);
                type_t *key_type = get_type(key_scope, reduction->key);
                Text_t superlative_key = op == Min ? Text("min_key") : Text("max_key");
                code = Texts(code, compile_declaration(key_type, superlative_key), ";\n");

                ast_t *comparison = new(ast_t, .file=ast->file, .start=ast->start, .end=ast->end,
                                        .tag=cmp_op, .__data.Plus.lhs=LiteralCode(Text("key"), .type=key_type),
                                        .__data.Plus.rhs=LiteralCode(superlative_key, .type=key_type));

                body->__data.InlineCCode.chunks = new(ast_list_t, .ast=FakeAST(TextLiteral, Texts(
                    compile_declaration(key_type, Text("key")), " = ", compile(key_scope, reduction->key), ";\n",
                    "if (!has_value || ", compile(body_scope, comparison), ") {\n"
                    "    ", superlative, " = ", compile(body_scope, item), ";\n"
                    "    ", superlative_key, " = key;\n"
                    "    has_value = yes;\n"
                    "}\n")));
            } else {
                ast_t *comparison = new(ast_t, .file=ast->file, .start=ast->start, .end=ast->end,
                                        .tag=cmp_op, .__data.Plus.lhs=item,
                                        .__data.Plus.rhs=LiteralCode(superlative, .type=item_t));
                body->__data.InlineCCode.chunks = new(ast_list_t, .ast=FakeAST(TextLiteral, Texts(
                    "if (!has_value || ", compile(body_scope, comparison), ") {\n"
                    "    ", superlative, " = ", compile(body_scope, item), ";\n"
                    "    has_value = yes;\n"
                    "}\n")));
            }


            code = Texts(code, compile_statement(env, loop), "\nhas_value ? ", promote_to_optional(item_t, superlative),
                            " : ", compile_none(item_t), ";})");
            return code;
        } else {
            // Accumulator-style reductions like +, ++, *, etc.
            type_t *reduction_type = Match(get_type(env, ast), OptionalType)->type;
            ast_t *item_value = item;
            if (reduction->key) {
                set_binding(body_scope, "$", item_t, compile(body_scope, item));
                item_value = reduction->key;
            }

            Text_t code = Texts(
                "({ // Reduction:\n",
                compile_declaration(reduction_type, Text("reduction")), ";\n"
                "Bool_t has_value = no;\n"
                );

            // For the special case of (or)/(and), we need to early out if we can:
            Text_t early_out = EMPTY_TEXT;
            if (op == Compare) {
                if (reduction_type->tag != IntType || Match(reduction_type, IntType)->bits != TYPE_IBITS32)
                    code_err(ast, "<> reductions are only supported for Int32 values");
            } else if (op == And) {
                if (reduction_type->tag == BoolType)
                    early_out = Text("if (!reduction) break;");
                else if (reduction_type->tag == OptionalType)
                    early_out = Texts("if (", check_none(reduction_type, Text("reduction")), ") break;");
            } else if (op == Or) {
                if (reduction_type->tag == BoolType)
                    early_out = Text("if (reduction) break;");
                else if (reduction_type->tag == OptionalType)
                    early_out = Texts("if (!", check_none(reduction_type, Text("reduction")), ") break;");
            }

            ast_t *combination = new(ast_t, .file=ast->file, .start=ast->start, .end=ast->end,
                                     .tag=op, .__data.Plus.lhs=LiteralCode(Text("reduction"), .type=reduction_type),
                                     .__data.Plus.rhs=item_value);
            body->__data.InlineCCode.chunks = new(ast_list_t, .ast=FakeAST(TextLiteral, Texts(
                "if (!has_value) {\n"
                "    reduction = ", compile(body_scope, item_value), ";\n"
                "    has_value = yes;\n"
                "} else {\n"
                "    reduction = ", compile(body_scope, combination), ";\n",
                early_out,
                "}\n")));

            code = Texts(code, compile_statement(env, loop), "\nhas_value ? ", promote_to_optional(reduction_type, Text("reduction")),
                            " : ", compile_none(reduction_type), ";})");
            return code;
        }
    }
    case FieldAccess: {
        DeclareMatch(f, ast, FieldAccess);
        type_t *fielded_t = get_type(env, f->fielded);
        type_t *value_t = value_type(fielded_t);
        switch (value_t->tag) {
        case TypeInfoType: {
            DeclareMatch(info, value_t, TypeInfoType);
            if (f->field[0] == '_') {
                if (!type_eq(env->current_type, info->type))
                    code_err(ast, "Fields that start with underscores are not accessible on types outside of the type definition.");
            }
            binding_t *b = get_binding(info->env, f->field);
            if (!b) code_err(ast, "I couldn't find the field '", f->field, "' on this type");
            if (b->code.length == 0) code_err(ast, "I couldn't figure out how to compile this field");
            return b->code;
        }
        case TextType: {
            const char *lang = Match(value_t, TextType)->lang; 
            if (lang && streq(f->field, "text")) {
                Text_t text = compile_to_pointer_depth(env, f->fielded, 0, false);
                return Texts("((Text_t)", text, ")");
            } else if (streq(f->field, "length")) {
                return Texts("Int$from_int64((", compile_to_pointer_depth(env, f->fielded, 0, false), ").length)");
            }
            code_err(ast, "There is no '", f->field, "' field on ", type_to_str(value_t), " values");
        }
        case StructType: {
            for (arg_t *field = Match(value_t, StructType)->fields; field; field = field->next) {
                if (streq(field->name, f->field)) {
                    if (fielded_t->tag == PointerType) {
                        Text_t fielded = compile_to_pointer_depth(env, f->fielded, 1, false);
                        return Texts("(", fielded, ")->", f->field);
                    } else {
                        Text_t fielded = compile(env, f->fielded);
                        return Texts("(", fielded, ").", f->field);
                    }
                }
            }
            code_err(ast, "The field '", f->field, "' is not a valid field name of ", type_to_str(value_t));
        }
        case EnumType: {
            DeclareMatch(e, value_t, EnumType);
            for (tag_t *tag = e->tags; tag; tag = tag->next) {
                if (streq(f->field, tag->name)) {
                    Text_t tag_name = namespace_name(e->env, e->env->namespace, Texts("tag$", tag->name));
                    if (fielded_t->tag == PointerType) {
                        Text_t fielded = compile_to_pointer_depth(env, f->fielded, 1, false);
                        return Texts("((", fielded, ")->$tag == ", tag_name, ")");
                    } else if (enum_has_fields(value_t)) {
                        Text_t fielded = compile(env, f->fielded);
                        return Texts("((", fielded, ").$tag == ", tag_name, ")");
                    } else {
                        Text_t fielded = compile(env, f->fielded);
                        return Texts("((", fielded, ") == ", tag_name, ")");
                    }
                }
            }
            code_err(ast, "The field '", f->field, "' is not a valid tag name of ", type_to_str(value_t));
        }
        case ListType: {
            if (streq(f->field, "length"))
                return Texts("Int$from_int64((", compile_to_pointer_depth(env, f->fielded, 0, false), ").length)");
            code_err(ast, "There is no ", f->field, " field on lists");
        }
        case SetType: {
            if (streq(f->field, "items"))
                return Texts("LIST_COPY((", compile_to_pointer_depth(env, f->fielded, 0, false), ").entries)");
            else if (streq(f->field, "length"))
                return Texts("Int$from_int64((", compile_to_pointer_depth(env, f->fielded, 0, false), ").entries.length)");
            code_err(ast, "There is no '", f->field, "' field on sets");
        }
        case TableType: {
            if (streq(f->field, "length")) {
                return Texts("Int$from_int64((", compile_to_pointer_depth(env, f->fielded, 0, false), ").entries.length)");
            } else if (streq(f->field, "keys")) {
                return Texts("LIST_COPY((", compile_to_pointer_depth(env, f->fielded, 0, false), ").entries)");
            } else if (streq(f->field, "values")) {
                DeclareMatch(table, value_t, TableType);
                Text_t offset = Texts("offsetof(struct { ", compile_declaration(table->key_type, Text("k")), "; ", compile_declaration(table->value_type, Text("v")), "; }, v)");
                return Texts("({ List_t *entries = &(", compile_to_pointer_depth(env, f->fielded, 0, false), ").entries;\n"
                                "LIST_INCREF(*entries);\n"
                                "List_t values = *entries;\n"
                                "values.data += ", offset, ";\n"
                                "values; })");
            } else if (streq(f->field, "fallback")) {
                return Texts("({ Table_t *_fallback = (", compile_to_pointer_depth(env, f->fielded, 0, false), ").fallback; _fallback ? *_fallback : NONE_TABLE; })");
            }
            code_err(ast, "There is no '", f->field, "' field on tables");
        }
        case ModuleType: {
            const char *name = Match(value_t, ModuleType)->name;
            env_t *module_env = Table$str_get(*env->imports, name);
            return compile(module_env, WrapAST(ast, Var, f->field));
        }
        default:
            code_err(ast, "Field accesses are not supported on ", type_to_str(fielded_t), " values");
        }
    }
    case Index: {
        DeclareMatch(indexing, ast, Index);
        type_t *indexed_type = get_type(env, indexing->indexed);
        if (!indexing->index) {
            if (indexed_type->tag != PointerType)
                code_err(ast, "Only pointers can use the '[]' operator to dereference the entire value.");
            DeclareMatch(ptr, indexed_type, PointerType);
            if (ptr->pointed->tag == ListType) {
                return Texts("*({ List_t *list = ", compile(env, indexing->indexed), "; LIST_INCREF(*list); list; })");
            } else if (ptr->pointed->tag == TableType || ptr->pointed->tag == SetType) {
                return Texts("*({ Table_t *t = ", compile(env, indexing->indexed), "; TABLE_INCREF(*t); t; })");
            } else {
                return Texts("*(", compile(env, indexing->indexed), ")");
            }
        }

        type_t *container_t = value_type(indexed_type);
        type_t *index_t = get_type(env, indexing->index);
        if (container_t->tag == ListType) {
            if (index_t->tag != IntType && index_t->tag != BigIntType && index_t->tag != ByteType)
                code_err(indexing->index, "Lists can only be indexed by integers, not ", type_to_str(index_t));
            type_t *item_type = Match(container_t, ListType)->item_type;
            Text_t list = compile_to_pointer_depth(env, indexing->indexed, 0, false);
            file_t *f = indexing->index->file;
            Text_t index_code = indexing->index->tag == Int
                ? compile_int_to_type(env, indexing->index, Type(IntType, .bits=TYPE_IBITS64))
                : (index_t->tag == BigIntType ? Texts("Int64$from_int(", compile(env, indexing->index), ", no)")
                   : Texts("(Int64_t)(", compile(env, indexing->index), ")"));
            if (indexing->unchecked)
                return Texts("List_get_unchecked(", compile_type(item_type), ", ", list, ", ", index_code, ")");
            else
                return Texts("List_get(", compile_type(item_type), ", ", list, ", ", index_code, ", ",
                                String((int64_t)(indexing->index->start - f->text)), ", ",
                                String((int64_t)(indexing->index->end - f->text)),
                                ")");
        } else if (container_t->tag == TableType) {
            DeclareMatch(table_type, container_t, TableType);
            if (indexing->unchecked)
                code_err(ast, "Table indexes cannot be unchecked");
            if (table_type->default_value) {
                return Texts("Table$get_or_default(",
                                compile_to_pointer_depth(env, indexing->indexed, 0, false), ", ",
                                compile_type(table_type->key_type), ", ",
                                compile_type(table_type->value_type), ", ",
                                compile(env, indexing->index), ", ",
                                compile_to_type(env, table_type->default_value, table_type->value_type), ", ",
                                compile_type_info(container_t), ")");
            } else {
                return Texts("Table$get_optional(",
                                compile_to_pointer_depth(env, indexing->indexed, 0, false), ", ",
                                compile_type(table_type->key_type), ", ",
                                compile_type(table_type->value_type), ", ",
                                compile(env, indexing->index), ", "
                                "_, ", promote_to_optional(table_type->value_type, Text("(*_)")), ", ",
                                compile_none(table_type->value_type), ", ",
                                compile_type_info(container_t), ")");
            }
        } else if (container_t->tag == TextType) {
            return Texts("Text$cluster(", compile_to_pointer_depth(env, indexing->indexed, 0, false), ", ", compile_to_type(env, indexing->index, Type(BigIntType)), ")");
        } else {
            code_err(ast, "Indexing is not supported for type: ", type_to_str(container_t));
        }
    }
    case InlineCCode: {
        type_t *t = get_type(env, ast);
        if (t->tag == VoidType)
            return Texts("{\n", compile_statement(env, ast), "\n}");
        else
            return compile_statement(env, ast);
    }
    case Use: code_err(ast, "Compiling 'use' as expression!");
    case Defer: code_err(ast, "Compiling 'defer' as expression!");
    case Extern: code_err(ast, "Externs are not supported as expressions");
    case TableEntry: code_err(ast, "Table entries should not be compiled directly");
    case Declare: case Assign: case UPDATE_CASES: case For: case While: case Repeat: case StructDef: case LangDef: case Extend:
    case EnumDef: case FunctionDef: case ConvertDef: case Skip: case Stop: case Pass: case Return: case DocTest: case Assert:
        code_err(ast, "This is not a valid expression");
    default: case Unknown: code_err(ast, "Unknown AST: ", ast_to_sexp_str(ast));
    }
    return EMPTY_TEXT;
}

Text_t compile_type_info(type_t *t)
{
    if (t == NULL) compiler_err(NULL, NULL, NULL, "Attempt to compile a NULL type");
    if (t == PATH_TYPE) return Text("&Path$info");
    else if (t == PATH_TYPE_TYPE) return Text("&PathType$info");

    switch (t->tag) {
    case BoolType: case ByteType: case IntType: case BigIntType: case NumType: case CStringType:
        return Texts("&", type_to_text(t), "$info");
    case TextType: {
        DeclareMatch(text, t, TextType);
        if (!text->lang || streq(text->lang, "Text"))
            return Text("&Text$info");
        return Texts("(&", namespace_name(text->env, text->env->namespace, Text("$info")), ")");
    }
    case StructType: {
        DeclareMatch(s, t, StructType);
        return Texts("(&", namespace_name(s->env, s->env->namespace, Text("$info")), ")");
    }
    case EnumType: {
        DeclareMatch(e, t, EnumType);
        return Texts("(&", namespace_name(e->env, e->env->namespace, Text("$info")), ")");
    }
    case ListType: {
        type_t *item_t = Match(t, ListType)->item_type;
        return Texts("List$info(", compile_type_info(item_t), ")");
    }
    case SetType: {
        type_t *item_type = Match(t, SetType)->item_type;
        return Texts("Set$info(", compile_type_info(item_type), ")");
    }
    case TableType: {
        DeclareMatch(table, t, TableType);
        type_t *key_type = table->key_type;
        type_t *value_type = table->value_type;
        return Texts("Table$info(", compile_type_info(key_type), ", ", compile_type_info(value_type), ")");
    }
    case PointerType: {
        DeclareMatch(ptr, t, PointerType);
        const char *sigil = ptr->is_stack ? "&" : "@";
        return Texts("Pointer$info(", quoted_str(sigil), ", ", compile_type_info(ptr->pointed), ")");
    }
    case FunctionType: {
        return Texts("Function$info(", quoted_text(type_to_text(t)), ")");
    }
    case ClosureType: {
        return Texts("Closure$info(", quoted_text(type_to_text(t)), ")");
    }
    case OptionalType: {
        type_t *non_optional = Match(t, OptionalType)->type;
        return Texts("Optional$info(sizeof(", compile_type(non_optional),
                        "), __alignof__(", compile_type(non_optional), "), ", compile_type_info(non_optional), ")");
    }
    case TypeInfoType: return Texts("Type$info(", quoted_text(type_to_text(Match(t, TypeInfoType)->type)), ")");
    case MemoryType: return Text("&Memory$info");
    case VoidType: return Text("&Void$info");
    default:
        compiler_err(NULL, 0, 0, "I couldn't convert to a type info: ", type_to_str(t));
    }
    return EMPTY_TEXT;
}

static Text_t get_flag_options(type_t *t, const char *separator)
{
    if (t->tag == BoolType) {
        return Text("yes|no");
    } else if (t->tag == EnumType) {
        Text_t options = EMPTY_TEXT;
        for (tag_t *tag = Match(t, EnumType)->tags; tag; tag = tag->next) {
            options = Texts(options, tag->name);
            if (tag->next) options = Texts(options, separator);
        }
        return options;
    } else if (t->tag == IntType || t->tag == NumType || t->tag == BigIntType) {
        return Text("N");
    } else {
        return Text("...");
    }
}

Text_t compile_cli_arg_call(env_t *env, Text_t fn_name, type_t *fn_type, const char *version)
{
    DeclareMatch(fn_info, fn_type, FunctionType);

    env_t *main_env = fresh_scope(env);

    Text_t code = EMPTY_TEXT;
    binding_t *usage_binding = get_binding(env, "_USAGE");
    Text_t usage_code = usage_binding ? usage_binding->code : Text("usage");
    binding_t *help_binding = get_binding(env, "_HELP");
    Text_t help_code = help_binding ? help_binding->code : usage_code;
    if (!usage_binding) {
        bool explicit_help_flag = false;
        for (arg_t *arg = fn_info->args; arg; arg = arg->next) {
            if (streq(arg->name, "help")) {
                explicit_help_flag = true;
                break;
            }
        }

        Text_t usage = explicit_help_flag ? EMPTY_TEXT : Text(" [--help]");
        for (arg_t *arg = fn_info->args; arg; arg = arg->next) {
            usage = Texts(usage, " ");
            type_t *t = get_arg_type(main_env, arg);
            Text_t flag = Text$replace(Text$from_str(arg->name), Text("_"), Text("-"));
            if (arg->default_val || arg->type->tag == OptionalType) {
                if (strlen(arg->name) == 1) {
                    if (t->tag == BoolType || (t->tag == OptionalType && Match(t, OptionalType)->type->tag == BoolType))
                        usage = Texts(usage, "[-", flag, "]");
                    else
                        usage = Texts(usage, "[-", flag, " ", get_flag_options(t, "|"), "]");
                } else {
                    if (t->tag == BoolType || (t->tag == OptionalType && Match(t, OptionalType)->type->tag == BoolType))
                        usage = Texts(usage, "[--", flag, "]");
                    else if (t->tag == ListType)
                        usage = Texts(usage, "[--", flag, " ", get_flag_options(t, "|"), "]");
                    else
                        usage = Texts(usage, "[--", flag, "=", get_flag_options(t, "|"), "]");
                }
            } else {
                if (t->tag == BoolType)
                    usage = Texts(usage, "<--", flag, "|--no-", flag, ">");
                else if (t->tag == EnumType)
                    usage = Texts(usage, get_flag_options(t, "|"));
                else if (t->tag == ListType)
                    usage = Texts(usage, "[", flag, "...]");
                else
                    usage = Texts(usage, "<", flag, ">");
            }
        }
        code = Texts(code, "Text_t usage = Texts(Text(\"Usage: \"), Text$from_str(argv[0])",
                        usage.length == 0 ? EMPTY_TEXT : Texts(", Text(", quoted_text(usage), ")"), ");\n");
    }


    for (arg_t *arg = fn_info->args; arg; arg = arg->next) {
        type_t *opt_type = arg->type->tag == OptionalType ? arg->type : Type(OptionalType, .type=arg->type);
        code = Texts(code, compile_declaration(opt_type, Texts("_$", arg->name)));
        if (arg->default_val) {
            Text_t default_val = compile(env, arg->default_val);
            if (arg->type->tag != OptionalType)
                default_val = promote_to_optional(arg->type, default_val);
            code = Texts(code, " = ", default_val);
        } else {
            code = Texts(code, " = ", compile_none(arg->type));
        }
        code = Texts(code, ";\n");
    }

    Text_t version_code = quoted_str(version);
    code = Texts(code, "tomo_parse_args(argc, argv, ", usage_code, ", ", help_code, ", ", version_code);
    for (arg_t *arg = fn_info->args; arg; arg = arg->next) {
        code = Texts(code, ",\n{", quoted_text(Text$replace(Text$from_str(arg->name), Text("_"), Text("-"))), ", ",
                        (arg->default_val || arg->type->tag == OptionalType) ? "false" : "true", ", ",
                        compile_type_info(arg->type),
                        ", &", Texts("_$", arg->name), "}");
    }
    code = Texts(code, ");\n");

    code = Texts(code, fn_name, "(");
    for (arg_t *arg = fn_info->args; arg; arg = arg->next) {
        Text_t arg_code = Texts("_$", arg->name);
        if (arg->type->tag != OptionalType)
            arg_code = optional_into_nonnone(arg->type, arg_code);

        code = Texts(code, arg_code);
        if (arg->next) code = Texts(code, ", ");
    }
    code = Texts(code, ");\n");
    return code;
}

Text_t compile_function(env_t *env, Text_t name_code, ast_t *ast, Text_t *staticdefs)
{
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
            code_err(ast, "Conversions are only supported for text, struct, and enum types, not ", type_to_str(ret_t));
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
    if (ret_t->tag == AbortType)
        ret_type_code = Texts("__attribute__((noreturn)) _Noreturn ", ret_type_code);

    if (is_private)
        *staticdefs = Texts(*staticdefs, "static ", ret_type_code, " ", name_code, arg_signature, ";\n");

    Text_t code;
    if (cache) {
        code = Texts("static ", ret_type_code, " ", name_code, "$uncached", arg_signature);
    } else {
        code = Texts(ret_type_code, " ", name_code, arg_signature);
        if (is_inline)
            code = Texts("INLINE ", code);
        if (!is_private)
            code = Texts("public ", code);
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
        if (body_type->tag != AbortType)
            code_err(ast, "This function can reach the end without aborting!");
    } else if (ret_t->tag == VoidType) {
        if (body_type->tag == AbortType)
            code_err(ast, "This function will always abort before it reaches the end, but it's declared as having a Void return. It should be declared as an Abort return instead.");
    } else {
        if (body_type->tag != ReturnType && body_type->tag != AbortType)
            code_err(ast, "This function looks like it can reach the end without returning a ", type_to_str(ret_t), " value! \n "
                     "If this is not the case, please add a call to `fail(\"Unreachable\")` at the end of the function to help the compiler out.");
    }

    Text_t body_code = Texts("{\n", compile_inline_block(body_scope, body), "}\n");
    Text_t definition = with_source_info(env, ast, Texts(code, " ", body_code, "\n"));

    if (cache && args == NULL) { // no-args cache just uses a static var
        Text_t wrapper = Texts(
            is_private ? EMPTY_TEXT : Text("public "), ret_type_code, " ", name_code, "(void) {\n"
            "static ", compile_declaration(ret_t, Text("cached_result")), ";\n",
            "static bool initialized = false;\n",
            "if (!initialized) {\n"
            "\tcached_result = ", name_code, "$uncached();\n",
            "\tinitialized = true;\n",
            "}\n",
            "return cached_result;\n"
            "}\n");
        definition = Texts(definition, wrapper);
    } else if (cache && cache->tag == Int) {
        assert(args);
        OptionalInt64_t cache_size = Int64$parse(Text$from_str(Match(cache, Int)->str), NULL);
        Text_t pop_code = EMPTY_TEXT;
        if (cache->tag == Int && !cache_size.is_none && cache_size.value > 0) {
            // FIXME: this currently just deletes the first entry, but this should be more like a
            // least-recently-used cache eviction policy or least-frequently-used
            pop_code = Texts("if (cache.entries.length > ", String(cache_size.value),
                                ") Table$remove(&cache, cache.entries.data + cache.entries.stride*0, table_type);\n");
        }

        if (!args->next) {
            // Single-argument functions have simplified caching logic
            type_t *arg_type = get_arg_ast_type(env, args);
            Text_t wrapper = Texts(
                is_private ? EMPTY_TEXT : Text("public "), ret_type_code, " ", name_code, arg_signature, "{\n"
                "static Table_t cache = {};\n",
                "const TypeInfo_t *table_type = Table$info(", compile_type_info(arg_type), ", ", compile_type_info(ret_t), ");\n",
                compile_declaration(Type(PointerType, .pointed=ret_t), Text("cached")), " = Table$get_raw(cache, &_$", args->name, ", table_type);\n"
                "if (cached) return *cached;\n",
                compile_declaration(ret_t, Text("ret")), " = ", name_code, "$uncached(_$", args->name, ");\n",
                pop_code,
                "Table$set(&cache, &_$", args->name, ", &ret, table_type);\n"
                "return ret;\n"
                "}\n");
            definition = Texts(definition, wrapper);
        } else {
            // Multi-argument functions use a custom struct type (only defined internally) as a cache key:
            arg_t *fields = NULL;
            for (arg_ast_t *arg = args; arg; arg = arg->next)
                fields = new(arg_t, .name=arg->name, .type=get_arg_ast_type(env, arg), .next=fields);
            REVERSE_LIST(fields);
            type_t *t = Type(StructType, .name=String("func$", get_line_number(ast->file, ast->start), "$args"), .fields=fields, .env=env);

            int64_t num_fields = used_names.entries.length;
            const char *metamethods = is_packed_data(t) ? "PackedData$metamethods" : "Struct$metamethods";
            Text_t args_typeinfo = Texts("((TypeInfo_t[1]){{.size=sizeof(args), .align=__alignof__(args), .metamethods=", metamethods,
                                          ", .tag=StructInfo, .StructInfo.name=\"FunctionArguments\", "
                                          ".StructInfo.num_fields=", String(num_fields),
                                          ", .StructInfo.fields=(NamedType_t[", String(num_fields), "]){");
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
                is_private ? EMPTY_TEXT : Text("public "), ret_type_code, " ", name_code, arg_signature, "{\n"
                "static Table_t cache = {};\n",
                args_type, " args = {", all_args, "};\n"
                "const TypeInfo_t *table_type = Table$info(", args_typeinfo, ", ", compile_type_info(ret_t), ");\n",
                compile_declaration(Type(PointerType, .pointed=ret_t), Text("cached")), " = Table$get_raw(cache, &args, table_type);\n"
                "if (cached) return *cached;\n",
                compile_declaration(ret_t, Text("ret")), " = ", name_code, "$uncached(", all_args, ");\n",
                pop_code,
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
    if (ret_t && ret_t->tag != VoidType)
        text = Texts(text, "->", type_to_text(ret_t));
    text = Texts(text, ")");
    return definition;
}

Text_t compile_top_level_code(env_t *env, ast_t *ast)
{
    if (!ast) return EMPTY_TEXT;

    switch (ast->tag) {
    case Use: {
        // DeclareMatch(use, ast, Use);
        // if (use->what == USE_C_CODE) {
        //     Path_t path = Path$relative_to(Path$from_str(use->path), Path(".build"));
        //     return Texts("#include \"", Path$as_c_string(path), "\"\n");
        // }
        return EMPTY_TEXT;
    }
    case Declare: {
        DeclareMatch(decl, ast, Declare);
        const char *decl_name = Match(decl->var, Var)->name;
        Text_t full_name = namespace_name(env, env->namespace, Text$from_str(decl_name));
        type_t *t = decl->type ? parse_type_ast(env, decl->type) : get_type(env, decl->value);
        if (t->tag == FunctionType) t = Type(ClosureType, t);
        Text_t val_code = compile_declared_value(env, ast);
        bool is_private = decl_name[0] == '_';
        if ((decl->value && is_constant(env, decl->value)) || (!decl->value && !has_heap_memory(t))) {
            set_binding(env, decl_name, t, full_name);
            return Texts(
                is_private ? "static " : "public ",
                compile_declaration(t, full_name), " = ", val_code, ";\n");
        } else {
            Text_t init_var = namespace_name(env, env->namespace, Texts(decl_name, "$$initialized"));
            Text_t checked_access = Texts("check_initialized(", full_name, ", ", init_var, ", \"", decl_name, "\")");
            set_binding(env, decl_name, t, checked_access);

            Text_t initialized_name = namespace_name(env, env->namespace, Texts(decl_name, "$$initialized"));
            return Texts(
                "static bool ", initialized_name, " = false;\n",
                is_private ? "static " : "public ",
                compile_declaration(t, full_name), ";\n");
        }
    }
    case FunctionDef: {
        Text_t name_code = namespace_name(env, env->namespace, Text$from_str(Match(Match(ast, FunctionDef)->name, Var)->name));
        return compile_function(env, name_code, ast, &env->code->staticdefs);
    }
    case ConvertDef: {
        type_t *type = get_function_def_type(env, ast);
        const char *name = get_type_name(Match(type, FunctionType)->ret);
        if (!name)
            code_err(ast, "Conversions are only supported for text, struct, and enum types, not ", type_to_str(Match(type, FunctionType)->ret));
        Text_t name_code = namespace_name(env, env->namespace, Texts(name, "$", String(get_line_number(ast->file, ast->start))));
        return compile_function(env, name_code, ast, &env->code->staticdefs);
    }
    case StructDef: {
        DeclareMatch(def, ast, StructDef);
        type_t *t = Table$str_get(*env->types, def->name);
        assert(t && t->tag == StructType);
        Text_t code = compile_struct_typeinfo(env, t, def->name, def->fields, def->secret, def->opaque);
        env_t *ns_env = namespace_env(env, def->name);
        return Texts(code, def->namespace ? compile_top_level_code(ns_env, def->namespace) : EMPTY_TEXT);
    }
    case EnumDef: {
        DeclareMatch(def, ast, EnumDef);
        Text_t code = compile_enum_typeinfo(env, ast);
        code = Texts(code, compile_enum_constructors(env, ast));
        env_t *ns_env = namespace_env(env, def->name);
        return Texts(code, def->namespace ? compile_top_level_code(ns_env, def->namespace) : EMPTY_TEXT);
    }
    case LangDef: {
        DeclareMatch(def, ast, LangDef);
        Text_t code = Texts("public const TypeInfo_t ", namespace_name(env, env->namespace, Texts(def->name, "$$info")),
                             " = {", String((int64_t)sizeof(Text_t)), ", ", String((int64_t)__alignof__(Text_t)),
                             ", .metamethods=Text$metamethods, .tag=TextInfo, .TextInfo={", quoted_str(def->name), "}};\n");
        env_t *ns_env = namespace_env(env, def->name);
        return Texts(code, def->namespace ? compile_top_level_code(ns_env, def->namespace) : EMPTY_TEXT);
    }
    case Extend: {
        DeclareMatch(extend, ast, Extend);
        binding_t *b = get_binding(env, extend->name);
        if (!b || b->type->tag != TypeInfoType)
            code_err(ast, "'", extend->name, "' is not the name of any type I recognize.");
        env_t *ns_env = Match(b->type, TypeInfoType)->env;
        env_t *extended = new(env_t);
        *extended = *ns_env;
        extended->locals = new(Table_t, .fallback=env->locals);
        extended->namespace_bindings = new(Table_t, .fallback=env->namespace_bindings);
        extended->id_suffix = env->id_suffix;
        return compile_top_level_code(extended, extend->body);
    }
    case Extern: return EMPTY_TEXT;
    case Block: {
        Text_t code = EMPTY_TEXT;
        for (ast_list_t *stmt = Match(ast, Block)->statements; stmt; stmt = stmt->next) {
            code = Texts(code, compile_top_level_code(env, stmt->ast));
        }
        return code;
    }
    default: return EMPTY_TEXT;
    }
}

static void initialize_vars_and_statics(env_t *env, ast_t *ast)
{
    if (!ast) return;

    for (ast_list_t *stmt = Match(ast, Block)->statements; stmt; stmt = stmt->next) {
        if (stmt->ast->tag == InlineCCode) {
            Text_t code = compile_statement(env, stmt->ast);
            env->code->staticdefs = Texts(env->code->staticdefs, code, "\n");
        } else if (stmt->ast->tag == Declare) {
            DeclareMatch(decl, stmt->ast, Declare);
            const char *decl_name = Match(decl->var, Var)->name;
            Text_t full_name = namespace_name(env, env->namespace, Text$from_str(decl_name));
            type_t *t = decl->type ? parse_type_ast(env, decl->type) : get_type(env, decl->value);
            if (t->tag == FunctionType) t = Type(ClosureType, t);
            Text_t val_code = compile_declared_value(env, stmt->ast);
            if ((decl->value && !is_constant(env, decl->value)) || (!decl->value && has_heap_memory(t))) {
                Text_t initialized_name = namespace_name(env, env->namespace, Texts(decl_name, "$$initialized"));
                env->code->variable_initializers = Texts(
                    env->code->variable_initializers,
                    with_source_info(
                        env, stmt->ast,
                        Texts(
                            full_name, " = ", val_code, ",\n",
                            initialized_name, " = true;\n")));
            }
        } else if (stmt->ast->tag == StructDef) {
            initialize_vars_and_statics(namespace_env(env, Match(stmt->ast, StructDef)->name),
                                        Match(stmt->ast, StructDef)->namespace);
        } else if (stmt->ast->tag == EnumDef) {
            initialize_vars_and_statics(namespace_env(env, Match(stmt->ast, EnumDef)->name),
                                        Match(stmt->ast, EnumDef)->namespace);
        } else if (stmt->ast->tag == LangDef) {
            initialize_vars_and_statics(namespace_env(env, Match(stmt->ast, LangDef)->name),
                                        Match(stmt->ast, LangDef)->namespace);
        } else if (stmt->ast->tag == Extend) {
            initialize_vars_and_statics(namespace_env(env, Match(stmt->ast, Extend)->name),
                                        Match(stmt->ast, Extend)->body);
        } else if (stmt->ast->tag == Use) {
            continue;
        } else {
            Text_t code = compile_statement(env, stmt->ast);
            if (code.length > 0) code_err(stmt->ast, "I did not expect this to generate code");
        }
    }
}

Text_t compile_file(env_t *env, ast_t *ast)
{
    Text_t top_level_code = compile_top_level_code(env, ast);
    Text_t includes = EMPTY_TEXT;
    Text_t use_imports = EMPTY_TEXT;

    // First prepare variable initializers to prevent unitialized access:
    for (ast_list_t *stmt = Match(ast, Block)->statements; stmt; stmt = stmt->next) {
        if (stmt->ast->tag == Use) {
            use_imports = Texts(use_imports, compile_statement(env, stmt->ast));

            DeclareMatch(use, stmt->ast, Use);
            if (use->what == USE_C_CODE) {
                Path_t path = Path$relative_to(Path$from_str(use->path), Path(".build"));
                includes = Texts(includes, "#include \"", Path$as_c_string(path), "\"\n");
            }
        }
    }

    initialize_vars_and_statics(env, ast);

    const char *name = file_base_name(ast->file->filename);
    return Texts(
        env->do_source_mapping ? Texts("#line 1 ", quoted_str(ast->file->filename), "\n") : EMPTY_TEXT,
        "#define __SOURCE_FILE__ ", quoted_str(ast->file->filename), "\n",
        "#include <tomo_"TOMO_VERSION"/tomo.h>\n"
        "#include \"", name, ".tm.h\"\n\n",
        includes,
        env->code->local_typedefs, "\n",
        env->code->lambdas, "\n",
        env->code->staticdefs, "\n",
        top_level_code,
        "public void ", namespace_name(env, env->namespace, Text("$initialize")), "(void) {\n",
        "static bool initialized = false;\n",
        "if (initialized) return;\n",
        "initialized = true;\n",
        use_imports,
        env->code->variable_initializers,
        "}\n");
}

Text_t compile_statement_type_header(env_t *env, Path_t header_path, ast_t *ast)
{
    switch (ast->tag) {
    case Use: {
        DeclareMatch(use, ast, Use);
        Path_t source_path = Path$from_str(ast->file->filename);
        Path_t source_dir = Path$parent(source_path);
        Path_t build_dir = Path$resolved(Path$parent(header_path), Path$current_dir());
        switch (use->what) {
        case USE_MODULE: {
            module_info_t mod = get_module_info(ast);
            glob_t tm_files;
            const char *folder = mod.version ? String(mod.name, "_", mod.version) : mod.name;
            if (glob(String(TOMO_PREFIX"/share/tomo_"TOMO_VERSION"/installed/", folder, "/[!._0-9]*.tm"), GLOB_TILDE, NULL, &tm_files) != 0) {
                if (!try_install_module(mod))
                    code_err(ast, "Could not find library");
            }

            Text_t includes = EMPTY_TEXT;
            for (size_t i = 0; i < tm_files.gl_pathc; i++) {
                const char *filename = tm_files.gl_pathv[i];
                Path_t tm_file = Path$from_str(filename);
                Path_t lib_build_dir = Path$sibling(tm_file, Text(".build"));
                Path_t header = Path$child(lib_build_dir, Texts(Path$base_name(tm_file), Text(".h")));
                includes = Texts(includes, "#include \"", Path$as_c_string(header), "\"\n");
            }
            globfree(&tm_files);
            return with_source_info(env, ast, includes);
        }
        case USE_LOCAL: {
            Path_t used_path = Path$resolved(Path$from_str(use->path), source_dir);
            Path_t used_build_dir = Path$sibling(used_path, Text(".build"));
            Path_t used_header_path = Path$child(used_build_dir, Texts(Path$base_name(used_path), Text(".h")));
            return Texts("#include \"", Path$as_c_string(Path$relative_to(used_header_path, build_dir)), "\"\n");
        }
        case USE_HEADER:
            if (use->path[0] == '<') {
                return Texts("#include ", use->path, "\n");
            } else {
                Path_t used_path = Path$resolved(Path$from_str(use->path), source_dir);
                return Texts("#include \"", Path$as_c_string(Path$relative_to(used_path, build_dir)), "\"\n");
            }
        default:
            return EMPTY_TEXT;
        }
    }
    case StructDef: {
        return compile_struct_header(env, ast);
    }
    case EnumDef: {
        return compile_enum_header(env, ast);
    }
    case LangDef: {
        DeclareMatch(def, ast, LangDef);
        return Texts(
            // Constructor macro:
            "#define ", namespace_name(env, env->namespace, Text$from_str(def->name)),
                "(text) ((", namespace_name(env, env->namespace, Texts(def->name, "$$type")), "){.length=sizeof(text)-1, .tag=TEXT_ASCII, .ascii=\"\" text})\n"
            "#define ", namespace_name(env, env->namespace, Text$from_str(def->name)),
                "s(...) ((", namespace_name(env, env->namespace, Texts(def->name, "$$type")), ")Texts(__VA_ARGS__))\n"
            "extern const TypeInfo_t ", namespace_name(env, env->namespace, Texts(def->name, Text("$$info"))), ";\n"
        );
    }
    case Extend: {
        return EMPTY_TEXT;
    }
    default:
        return EMPTY_TEXT;
    }
}

Text_t compile_statement_namespace_header(env_t *env, Path_t header_path, ast_t *ast)
{
    env_t *ns_env = NULL;
    ast_t *block = NULL;
    switch (ast->tag) {
    case LangDef: {
        DeclareMatch(def, ast, LangDef);
        ns_env = namespace_env(env, def->name);
        block = def->namespace;
        break;
    }
    case Extend: {
        DeclareMatch(extend, ast, Extend);
        ns_env = namespace_env(env, extend->name);

        env_t *extended = new(env_t);
        *extended = *ns_env;
        extended->locals = new(Table_t, .fallback=env->locals);
        extended->namespace_bindings = new(Table_t, .fallback=env->namespace_bindings);
        extended->id_suffix = env->id_suffix;
        ns_env = extended;

        block = extend->body;
        break;
    }
    case StructDef: {
        DeclareMatch(def, ast, StructDef);
        ns_env = namespace_env(env, def->name);
        block = def->namespace;
        break;
    }
    case EnumDef: {
        DeclareMatch(def, ast, EnumDef);
        ns_env = namespace_env(env, def->name);
        block = def->namespace;
        break;
    }
    case Extern: {
        DeclareMatch(ext, ast, Extern);
        type_t *t = parse_type_ast(env, ext->type);
        Text_t decl;
        if (t->tag == ClosureType) {
            t = Match(t, ClosureType)->fn;
            DeclareMatch(fn, t, FunctionType);
            decl = Texts(compile_type(fn->ret), " ", ext->name, "(");
            for (arg_t *arg = fn->args; arg; arg = arg->next) {
                decl = Texts(decl, compile_type(arg->type));
                if (arg->next) decl = Texts(decl, ", ");
            }
            decl = Texts(decl, ")");
        } else {
            decl = compile_declaration(t, Text$from_str(ext->name));
        }
        return Texts("extern ", decl, ";\n");
    }
    case Declare: {
        DeclareMatch(decl, ast, Declare);
        const char *decl_name = Match(decl->var, Var)->name;
        bool is_private = (decl_name[0] == '_');
        if (is_private)
            return EMPTY_TEXT;

        type_t *t = decl->type ? parse_type_ast(env, decl->type) : get_type(env, decl->value);
        if (t->tag == FunctionType)
            t = Type(ClosureType, t);
        assert(t->tag != ModuleType);
        if (t->tag == AbortType || t->tag == VoidType || t->tag == ReturnType)
            code_err(ast, "You can't declare a variable with a ", type_to_str(t), " value");

        return Texts(
            decl->value ? compile_statement_type_header(env, header_path, decl->value) : EMPTY_TEXT,
            "extern ", compile_declaration(t, namespace_name(env, env->namespace, Text$from_str(decl_name))), ";\n");
    }
    case FunctionDef: {
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
        if (ret_t->tag == AbortType)
            ret_type_code = Texts("__attribute__((noreturn)) _Noreturn ", ret_type_code);
        Text_t name = namespace_name(env, env->namespace, Text$from_str(decl_name));
        if (env->namespace && env->namespace->parent && env->namespace->name && streq(decl_name, env->namespace->name))
            name = namespace_name(env, env->namespace, Text$from_str(String(get_line_number(ast->file, ast->start))));
        return Texts(ret_type_code, " ", name, arg_signature, ";\n");
    }
    case ConvertDef: {
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
            code_err(ast, "Conversions are only supported for text, struct, and enum types, not ", type_to_str(ret_t));
        Text_t name_code = namespace_name(env, env->namespace, Texts(name, "$", String(get_line_number(ast->file, ast->start))));
        return Texts(ret_type_code, " ", name_code, arg_signature, ";\n");
    }
    default: return EMPTY_TEXT;
    }
    assert(ns_env);
    Text_t header = EMPTY_TEXT;
    for (ast_list_t *stmt = block ? Match(block, Block)->statements : NULL; stmt; stmt = stmt->next) {
        header = Texts(header, compile_statement_namespace_header(ns_env, header_path, stmt->ast));
    }
    return header;
}

typedef struct {
    env_t *env;
    Text_t *header;
    Path_t header_path;
} compile_typedef_info_t;

static void _make_typedefs(compile_typedef_info_t *info, ast_t *ast)
{
    if (ast->tag == StructDef) {
        DeclareMatch(def, ast, StructDef);
        if (def->external) return;
        Text_t struct_name = namespace_name(info->env, info->env->namespace, Texts(def->name, "$$struct"));
        Text_t type_name = namespace_name(info->env, info->env->namespace, Texts(def->name, "$$type"));
        *info->header = Texts(*info->header, "typedef struct ", struct_name, " ", type_name, ";\n");
    } else if (ast->tag == EnumDef) {
        DeclareMatch(def, ast, EnumDef);
        bool has_any_tags_with_fields = false;
        for (tag_ast_t *tag = def->tags; tag; tag = tag->next) {
            has_any_tags_with_fields = has_any_tags_with_fields || (tag->fields != NULL);
        }

        if (has_any_tags_with_fields) {
            Text_t struct_name = namespace_name(info->env, info->env->namespace, Texts(def->name, "$$struct"));
            Text_t type_name = namespace_name(info->env, info->env->namespace, Texts(def->name, "$$type"));
            *info->header = Texts(*info->header, "typedef struct ", struct_name, " ", type_name, ";\n");

            for (tag_ast_t *tag = def->tags; tag; tag = tag->next) {
                if (!tag->fields) continue;
                Text_t tag_struct = namespace_name(info->env, info->env->namespace, Texts(def->name, "$", tag->name, "$$struct"));
                Text_t tag_type = namespace_name(info->env, info->env->namespace, Texts(def->name, "$", tag->name, "$$type"));
                *info->header = Texts(*info->header, "typedef struct ", tag_struct, " ", tag_type, ";\n");
            }
        } else {
            Text_t enum_name = namespace_name(info->env, info->env->namespace, Texts(def->name, "$$enum"));
            Text_t type_name = namespace_name(info->env, info->env->namespace, Texts(def->name, "$$type"));
            *info->header = Texts(*info->header, "typedef enum ", enum_name, " ", type_name, ";\n");
        }
    } else if (ast->tag == LangDef) {
        DeclareMatch(def, ast, LangDef);
        *info->header = Texts(*info->header, "typedef Text_t ", namespace_name(info->env, info->env->namespace, Texts(def->name, "$$type")), ";\n");
    }
}

static void _define_types_and_funcs(compile_typedef_info_t *info, ast_t *ast)
{
    *info->header = Texts(*info->header,
                             compile_statement_type_header(info->env, info->header_path, ast),
                             compile_statement_namespace_header(info->env, info->header_path, ast));
}

Text_t compile_file_header(env_t *env, Path_t header_path, ast_t *ast)
{
    Text_t header = Texts(
        "#pragma once\n",
        env->do_source_mapping ? Texts("#line 1 ", quoted_str(ast->file->filename), "\n") : EMPTY_TEXT,
        "#include <tomo_"TOMO_VERSION"/tomo.h>\n");

    compile_typedef_info_t info = {.env=env, .header=&header, .header_path=header_path};
    visit_topologically(Match(ast, Block)->statements, (Closure_t){.fn=(void*)_make_typedefs, &info});
    visit_topologically(Match(ast, Block)->statements, (Closure_t){.fn=(void*)_define_types_and_funcs, &info});

    header = Texts(header, "void ", namespace_name(env, env->namespace, Text("$initialize")), "(void);\n");
    return header;
}

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
