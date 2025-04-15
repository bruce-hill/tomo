// Compilation logic
#include <ctype.h>
#include <glob.h>
#include <gc.h>
#include <gc/cord.h>
#include <gmp.h>
#include <stdio.h>
#include <uninorm.h>

#include "ast.h"
#include "compile.h"
#include "cordhelpers.h"
#include "enums.h"
#include "environment.h"
#include "parse.h"
#include "stdlib/integers.h"
#include "stdlib/nums.h"
#include "stdlib/paths.h"
#include "stdlib/text.h"
#include "stdlib/util.h"
#include "structs.h"
#include "typecheck.h"

typedef ast_t* (*comprehension_body_t)(ast_t*, ast_t*);

static CORD compile_to_pointer_depth(env_t *env, ast_t *ast, int64_t target_depth, bool needs_incref);
static CORD compile_string(env_t *env, ast_t *ast, CORD color);
static CORD compile_arguments(env_t *env, ast_t *call_ast, arg_t *spec_args, arg_ast_t *call_args);
static CORD compile_maybe_incref(env_t *env, ast_t *ast, type_t *t);
static CORD compile_int_to_type(env_t *env, ast_t *ast, type_t *target);
static CORD compile_unsigned_type(type_t *t);
static CORD promote_to_optional(type_t *t, CORD code);
static CORD compile_none(type_t *t);
static CORD compile_empty(type_t *t);
static CORD compile_declared_value(env_t *env, ast_t *declaration_ast);
static CORD compile_to_type(env_t *env, ast_t *ast, type_t *t);
static CORD compile_typed_list(env_t *env, ast_t *ast, type_t *list_type);
static CORD compile_typed_set(env_t *env, ast_t *ast, type_t *set_type);
static CORD compile_typed_table(env_t *env, ast_t *ast, type_t *table_type);
static CORD compile_typed_allocation(env_t *env, ast_t *ast, type_t *pointer_type);
static CORD check_none(type_t *t, CORD value);
static CORD optional_into_nonnone(type_t *t, CORD value);
static CORD compile_string_literal(CORD literal);
static ast_t *add_to_list_comprehension(ast_t *item, ast_t *subject);
static ast_t *add_to_table_comprehension(ast_t *entry, ast_t *subject);
static ast_t *add_to_set_comprehension(ast_t *item, ast_t *subject);
static CORD compile_lvalue(env_t *env, ast_t *ast);

CORD promote_to_optional(type_t *t, CORD code)
{
    if (t == PATH_TYPE || t == PATH_TYPE_TYPE) {
        return code;
    } else if (t->tag == IntType) {
        switch (Match(t, IntType)->bits) {
        case TYPE_IBITS8: return CORD_all("((OptionalInt8_t){.value=", code, "})");
        case TYPE_IBITS16: return CORD_all("((OptionalInt16_t){.value=", code, "})");
        case TYPE_IBITS32: return CORD_all("((OptionalInt32_t){.value=", code, "})");
        case TYPE_IBITS64: return CORD_all("((OptionalInt64_t){.value=", code, "})");
        default: errx(1, "Unsupported in type: ", type_to_str(t));
        }
    } else if (t->tag == ByteType) {
        return CORD_all("((OptionalByte_t){.value=", code, "})");
    } else if (t->tag == StructType) {
        return CORD_all("({ ", compile_type(Type(OptionalType, .type=t)), " nonnull = {.value=", code, "}; nonnull.is_none = false; nonnull; })");
    } else {
        return code;
    }
}

static CORD with_source_info(env_t *env, ast_t *ast, CORD code)
{
    if (code == CORD_EMPTY || !ast || !ast->file || !env->do_source_mapping)
        return code;
    int64_t line = get_line_number(ast->file, ast->start);
    return CORD_asprintf("\n#line %ld\n%r", line, code);
}

static bool promote(env_t *env, ast_t *ast, CORD *code, type_t *actual, type_t *needed)
{
    if (type_eq(actual, needed))
        return true;

    if (!can_promote(actual, needed))
        return false;

    if (needed->tag == ClosureType && actual->tag == FunctionType) {
        *code = CORD_all("((Closure_t){", *code, ", NULL})");
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
        *code = CORD_all("(!", check_none(actual, *code), ")");
        return true;
    }

    // Lang to Text:
    if (actual->tag == TextType && needed->tag == TextType && streq(Match(needed, TextType)->lang, "Text"))
        return true;

    // Automatic optional checking for nums:
    if (needed->tag == NumType && actual->tag == OptionalType && Match(actual, OptionalType)->type->tag == NumType) {
        *code = CORD_all("({ ", compile_declaration(actual, "opt"), " = ", *code, "; ",
                         "if unlikely (", check_none(actual, "opt"), ")\n",
                        CORD_asprintf("fail_source(%r, %ld, %ld, \"This was expected to be a value, but it's none\");\n",
                                      CORD_quoted(ast->file->filename),
                                      (long)(ast->start - ast->file->text),
                                      (long)(ast->end - ast->file->text)),
                         optional_into_nonnone(actual, "opt"), "; })");
        return true;
    }

    // Numeric promotions/demotions
    if ((is_numeric_type(actual) || actual->tag == BoolType) && (is_numeric_type(needed) || needed->tag == BoolType)) {
        arg_ast_t *args = new(arg_ast_t, .value=LiteralCode(*code, .type=actual));
        binding_t *constructor = get_constructor(env, needed, args);
        if (constructor) {
            DeclareMatch(fn, constructor->type, FunctionType);
            if (fn->args->next == NULL) {
                *code = CORD_all(constructor->code, "(", compile_arguments(env, ast, fn->args, args), ")");
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
        *code = CORD_all(b->code, "(", *code, ")");
        return true;
    }

    // Text to C String
    if (actual->tag == TextType && type_eq(actual, TEXT_TYPE) && needed->tag == CStringType) {
        *code = CORD_all("Text$as_c_string(", *code, ")");
        return true;
    }

    // Automatic dereferencing:
    if (actual->tag == PointerType
        && can_promote(Match(actual, PointerType)->pointed, needed)) {
        *code = CORD_all("*(", *code, ")");
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
        *code = CORD_all("(", compile_type(needed), ")", *code);
        return true;
    }

    // Set -> List promotion:
    if (needed->tag == ListType && actual->tag == SetType
        && type_eq(Match(needed, ListType)->item_type, Match(actual, SetType)->item_type)) {
        *code = CORD_all("(", *code, ").entries");
        return true;
    }

    return false;
}

CORD compile_maybe_incref(env_t *env, ast_t *ast, type_t *t)
{
    if (is_idempotent(ast) && can_be_mutated(env, ast)) {
        if (t->tag == ListType)
            return CORD_all("LIST_COPY(", compile_to_type(env, ast, t), ")");
        else if (t->tag == TableType || t->tag == SetType)
            return CORD_all("TABLE_COPY(", compile_to_type(env, ast, t), ")");
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
            set_binding(lambda_scope, arg->name, get_arg_ast_type(env, arg), CORD_all("_$", arg->name));
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
                            Match(cond_t, OptionalType)->type, CORD_EMPTY);
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
                            Match(cond_t, OptionalType)->type, CORD_EMPTY);
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
        set_binding(body_scope, arg->name, arg_type, CORD_cat("_$", arg->name));
    }

    Table_t closed_vars = {};
    add_closed_vars(&closed_vars, env, body_scope, block);
    return closed_vars;
}

CORD compile_declaration(type_t *t, CORD name)
{
    if (t->tag == FunctionType) {
        DeclareMatch(fn, t, FunctionType);
        CORD code = CORD_all(compile_type(fn->ret), " (*", name, ")(");
        for (arg_t *arg = fn->args; arg; arg = arg->next) {
            code = CORD_all(code, compile_type(arg->type));
            if (arg->next) code = CORD_cat(code, ", ");
        }
        if (!fn->args) code = CORD_all(code, "void");
        return CORD_all(code, ")");
    } else if (t->tag != ModuleType) {
        return CORD_all(compile_type(t), " ", name);
    } else {
        return CORD_EMPTY;
    }
}

static CORD compile_update_assignment(env_t *env, ast_t *ast)
{
    if (!is_update_assignment(ast))
        code_err(ast, "This is not an update assignment");

    binary_operands_t update = BINARY_OPERANDS(ast);

    type_t *lhs_t = get_type(env, update.lhs);

    bool needs_idemotency_fix = !is_idempotent(update.lhs);
    CORD lhs = needs_idemotency_fix ? "(*lhs)" : compile_lvalue(env, update.lhs);

    CORD update_assignment = CORD_EMPTY;
    switch (ast->tag) {
    case PlusUpdate: {
        if (lhs_t->tag == IntType || lhs_t->tag == NumType || lhs_t->tag == ByteType)
            update_assignment = CORD_all(lhs, " += ", compile_to_type(env, update.rhs, lhs_t), ";");
        break;
    }
    case MinusUpdate: {
        if (lhs_t->tag == IntType || lhs_t->tag == NumType || lhs_t->tag == ByteType)
            update_assignment = CORD_all(lhs, " -= ", compile_to_type(env, update.rhs, lhs_t), ";");
        break;
    }
    case MultiplyUpdate: {
        if (lhs_t->tag == IntType || lhs_t->tag == NumType || lhs_t->tag == ByteType)
            update_assignment = CORD_all(lhs, " *= ", compile_to_type(env, update.rhs, lhs_t), ";");
        break;
    }
    case DivideUpdate: {
        if (lhs_t->tag == IntType || lhs_t->tag == NumType || lhs_t->tag == ByteType)
            update_assignment = CORD_all(lhs, " /= ", compile_to_type(env, update.rhs, lhs_t), ";");
        break;
    }
    case LeftShiftUpdate: {
        if (lhs_t->tag == IntType || lhs_t->tag == ByteType)
            update_assignment = CORD_all(lhs, " <<= ", compile_to_type(env, update.rhs, lhs_t), ";");
        break;
    }
    case RightShiftUpdate: {
        if (lhs_t->tag == IntType || lhs_t->tag == ByteType)
            update_assignment = CORD_all(lhs, " >>= ", compile_to_type(env, update.rhs, lhs_t), ";");
        break;
    }
    case AndUpdate: {
        if (lhs_t->tag == BoolType)
            update_assignment = CORD_all("if (", lhs, ") ", lhs, " = ", compile_to_type(env, update.rhs, Type(BoolType)), ";");
        break;
    }
    case OrUpdate: {
        if (lhs_t->tag == BoolType)
            update_assignment = CORD_all("if (!", lhs, ") ", lhs, " = ", compile_to_type(env, update.rhs, Type(BoolType)), ";");
        break;
    }
    default: break;
    }

    if (update_assignment == CORD_EMPTY) {
        ast_t *binop = new(ast_t);
        *binop = *ast;
        binop->tag = binop_tag(binop->tag);
        if (needs_idemotency_fix)
            binop->__data.Plus.lhs = LiteralCode("*lhs", .type=lhs_t);
        update_assignment = CORD_all(lhs, " = ", compile_to_type(env, binop, lhs_t), ";");
    }
    
    if (needs_idemotency_fix)
        return CORD_all("{ ", compile_declaration(Type(PointerType, .pointed=lhs_t), "lhs"), " = &", compile_lvalue(env, update.lhs), "; ",
                        update_assignment, "; }");
    else
        return update_assignment;
}

static CORD compile_binary_op(env_t *env, ast_t *ast)
{
    binary_operands_t binop = BINARY_OPERANDS(ast);
    type_t *lhs_t = get_type(env, binop.lhs);
    type_t *rhs_t = get_type(env, binop.rhs);
    type_t *overall_t = get_type(env, ast);

    binding_t *b = get_metamethod_binding(env, ast->tag, binop.lhs, binop.rhs, overall_t);
    if (b) {
        arg_ast_t *args = new(arg_ast_t, .value=binop.lhs, .next=new(arg_ast_t, .value=binop.rhs));
        DeclareMatch(fn, b->type, FunctionType);
        return CORD_all(b->code, "(", compile_arguments(env, ast, fn->args, args), ")");
    }

    if (ast->tag == Multiply && is_numeric_type(lhs_t)) {
        b = get_namespace_binding(env, binop.rhs, "scaled_by");
        if (b && b->type->tag == FunctionType) {
            DeclareMatch(fn, b->type, FunctionType);
            if (type_eq(fn->ret, rhs_t)) {
                arg_ast_t *args = new(arg_ast_t, .value=binop.rhs, .next=new(arg_ast_t, .value=binop.lhs));
                if (is_valid_call(env, fn->args, args, true))
                    return CORD_all(b->code, "(", compile_arguments(env, ast, fn->args, args), ")");
            }
        }
    } else if (ast->tag == Multiply && is_numeric_type(rhs_t)) {
        b = get_namespace_binding(env, binop.lhs, "scaled_by");
        if (b && b->type->tag == FunctionType) {
            DeclareMatch(fn, b->type, FunctionType);
            if (type_eq(fn->ret, lhs_t)) {
                arg_ast_t *args = new(arg_ast_t, .value=binop.lhs, .next=new(arg_ast_t, .value=binop.rhs));
                if (is_valid_call(env, fn->args, args, true))
                    return CORD_all(b->code, "(", compile_arguments(env, ast, fn->args, args), ")");
            }
        }
    } else if (ast->tag == Divide && is_numeric_type(rhs_t)) {
        b = get_namespace_binding(env, binop.lhs, "divided_by");
        if (b && b->type->tag == FunctionType) {
            DeclareMatch(fn, b->type, FunctionType);
            if (type_eq(fn->ret, lhs_t)) {
                arg_ast_t *args = new(arg_ast_t, .value=binop.lhs, .next=new(arg_ast_t, .value=binop.rhs));
                if (is_valid_call(env, fn->args, args, true))
                    return CORD_all(b->code, "(", compile_arguments(env, ast, fn->args, args), ")");
            }
        }
    } else if ((ast->tag == Divide || ast->tag == Mod || ast->tag == Mod1) && is_numeric_type(rhs_t)) {
        b = get_namespace_binding(env, binop.lhs, binop_method_name(ast->tag));
        if (b && b->type->tag == FunctionType) {
            DeclareMatch(fn, b->type, FunctionType);
            if (type_eq(fn->ret, lhs_t)) {
                arg_ast_t *args = new(arg_ast_t, .value=binop.lhs, .next=new(arg_ast_t, .value=binop.rhs));
                if (is_valid_call(env, fn->args, args, true))
                    return CORD_all(b->code, "(", compile_arguments(env, ast, fn->args, args), ")");
            }
        }
    }

    if (ast->tag == Or && lhs_t->tag == OptionalType) {
        if (rhs_t->tag == AbortType || rhs_t->tag == ReturnType) {
            return CORD_all("({ ", compile_declaration(lhs_t, "lhs"), " = ", compile(env, binop.lhs), "; ",
                            "if (", check_none(lhs_t, "lhs"), ") ", compile_statement(env, binop.rhs), " ",
                            optional_into_nonnone(lhs_t, "lhs"), "; })");
        }

        if (is_incomplete_type(rhs_t)) {
            type_t *complete = most_complete_type(rhs_t, Match(lhs_t, OptionalType)->type);
            if (complete == NULL)
                code_err(binop.rhs, "I don't know how to convert a ", type_to_str(rhs_t), " to a ", type_to_str(Match(lhs_t, OptionalType)->type));
            rhs_t = complete;
        }

        if (rhs_t->tag == OptionalType && type_eq(lhs_t, rhs_t)) {
            return CORD_all("({ ", compile_declaration(lhs_t, "lhs"), " = ", compile(env, binop.lhs), "; ",
                            check_none(lhs_t, "lhs"), " ? ", compile(env, binop.rhs), " : lhs; })");
        } else if (rhs_t->tag != OptionalType && type_eq(Match(lhs_t, OptionalType)->type, rhs_t)) {
            return CORD_all("({ ", compile_declaration(lhs_t, "lhs"), " = ", compile(env, binop.lhs), "; ",
                            check_none(lhs_t, "lhs"), " ? ", compile(env, binop.rhs), " : ",
                            optional_into_nonnone(lhs_t, "lhs"), "; })");
        } else if (rhs_t->tag == BoolType) {
            return CORD_all("((!", check_none(lhs_t, compile(env, binop.lhs)), ") || ", compile(env, binop.rhs), ")");
        } else {
            code_err(ast, "I don't know how to do an 'or' operation between ", type_to_str(lhs_t), " and ", type_to_str(rhs_t));
        }
    }

    CORD lhs = compile_to_type(env, binop.lhs, overall_t);
    CORD rhs = compile_to_type(env, binop.rhs, overall_t);

    switch (ast->tag) {
    case Power: {
        if (overall_t->tag != NumType)
            code_err(ast, "Exponentiation is only supported for Num types, not ", type_to_str(overall_t));
        if (overall_t->tag == NumType && Match(overall_t, NumType)->bits == TYPE_NBITS32)
            return CORD_all("powf(", lhs, ", ", rhs, ")");
        else
            return CORD_all("pow(", lhs, ", ", rhs, ")");
    }
    case Multiply: {
        if (overall_t->tag != IntType && overall_t->tag != NumType && overall_t->tag != ByteType)
            code_err(ast, "Math operations are only supported for values of the same numeric type, not ", type_to_str(lhs_t), " and ", type_to_str(rhs_t));
        return CORD_all("(", lhs, " * ", rhs, ")");
    }
    case Divide: {
        if (overall_t->tag != IntType && overall_t->tag != NumType && overall_t->tag != ByteType)
            code_err(ast, "Math operations are only supported for values of the same numeric type, not ", type_to_str(lhs_t), " and ", type_to_str(rhs_t));
        return CORD_all("(", lhs, " / ", rhs, ")");
    }
    case Mod: {
        if (overall_t->tag != IntType && overall_t->tag != NumType && overall_t->tag != ByteType)
            code_err(ast, "Math operations are only supported for values of the same numeric type, not ", type_to_str(lhs_t), " and ", type_to_str(rhs_t));
        return CORD_all("(", lhs, " % ", rhs, ")");
    }
    case Mod1: {
        if (overall_t->tag != IntType && overall_t->tag != NumType && overall_t->tag != ByteType)
            code_err(ast, "Math operations are only supported for values of the same numeric type, not ", type_to_str(lhs_t), " and ", type_to_str(rhs_t));
        return CORD_all("((((", lhs, ")-1) % (", rhs, ")) + 1)");
    }
    case Plus: {
        if (overall_t->tag != IntType && overall_t->tag != NumType && overall_t->tag != ByteType)
            code_err(ast, "Math operations are only supported for values of the same numeric type, not ", type_to_str(lhs_t), " and ", type_to_str(rhs_t));
        return CORD_all("(", lhs, " + ", rhs, ")");
    }
    case Minus: {
        if (overall_t->tag != IntType && overall_t->tag != NumType && overall_t->tag != ByteType)
            code_err(ast, "Math operations are only supported for values of the same numeric type, not ", type_to_str(lhs_t), " and ", type_to_str(rhs_t));
        return CORD_all("(", lhs, " - ", rhs, ")");
    }
    case LeftShift: {
        if (overall_t->tag != IntType && overall_t->tag != NumType && overall_t->tag != ByteType)
            code_err(ast, "Math operations are only supported for values of the same numeric type, not ", type_to_str(lhs_t), " and ", type_to_str(rhs_t));
        return CORD_all("(", lhs, " << ", rhs, ")");
    }
    case RightShift: {
        if (overall_t->tag != IntType && overall_t->tag != NumType && overall_t->tag != ByteType)
            code_err(ast, "Math operations are only supported for values of the same numeric type, not ", type_to_str(lhs_t), " and ", type_to_str(rhs_t));
        return CORD_all("(", lhs, " >> ", rhs, ")");
    }
    case UnsignedLeftShift: {
        if (overall_t->tag != IntType && overall_t->tag != NumType && overall_t->tag != ByteType)
            code_err(ast, "Math operations are only supported for values of the same numeric type, not ", type_to_str(lhs_t), " and ", type_to_str(rhs_t));
        return CORD_all("(", compile_type(overall_t), ")((", compile_unsigned_type(lhs_t), ")", lhs, " << ", rhs, ")");
    }
    case UnsignedRightShift: {
        if (overall_t->tag != IntType && overall_t->tag != NumType && overall_t->tag != ByteType)
            code_err(ast, "Math operations are only supported for values of the same numeric type, not ", type_to_str(lhs_t), " and ", type_to_str(rhs_t));
        return CORD_all("(", compile_type(overall_t), ")((", compile_unsigned_type(lhs_t), ")", lhs, " >> ", rhs, ")");
    }
    case And: {
        if (overall_t->tag == BoolType)
            return CORD_all("(", lhs, " && ", rhs, ")");
        else if (overall_t->tag == IntType || overall_t->tag == ByteType)
            return CORD_all("(", lhs, " & ", rhs, ")");
        else
            code_err(ast, "The 'and' operator isn't supported between ", type_to_str(lhs_t), " and ", type_to_str(rhs_t), " values");
    }
    case Compare: {
        return CORD_all("generic_compare(stack(", lhs, "), stack(", rhs, "), ", compile_type_info(overall_t), ")");
    }
    case Or: {
        if (overall_t->tag == BoolType) {
            return CORD_all("(", lhs, " || ", rhs, ")");
        } else if (overall_t->tag == IntType || overall_t->tag == ByteType) {
            return CORD_all("(", lhs, " | ", rhs, ")");
        } else {
            code_err(ast, "The 'or' operator isn't supported between ", type_to_str(lhs_t), " and ", type_to_str(rhs_t), " values");
        }
    }
    case Xor: {
        // TODO: support optional values in `xor` expressions
        if (overall_t->tag == BoolType || overall_t->tag == IntType || overall_t->tag == ByteType)
            return CORD_all("(", lhs, " ^ ", rhs, ")");
        else
            code_err(ast, "The 'xor' operator isn't supported between ", type_to_str(lhs_t), " and ", type_to_str(rhs_t), " values");
    }
    case Concat: {
        if (overall_t == PATH_TYPE)
            return CORD_all("Path$concat(", lhs, ", ", rhs, ")");
        switch (overall_t->tag) {
        case TextType: {
            return CORD_all("Text$concat(", lhs, ", ", rhs, ")");
        }
        case ListType: {
            return CORD_all("List$concat(", lhs, ", ", rhs, ", sizeof(", compile_type(Match(overall_t, ListType)->item_type), "))");
        }
        default:
            code_err(ast, "Concatenation isn't supported between ", type_to_str(lhs_t), " and ", type_to_str(rhs_t), " values");
        }
    }
    default: errx(1, "Not a valid binary operation: ", ast_to_xml_str(ast));
    }
}

PUREFUNC CORD compile_unsigned_type(type_t *t)
{
    if (t->tag != IntType)
        errx(1, "Not an int type, so unsigned doesn't make sense!");
    switch (Match(t, IntType)->bits) {
    case TYPE_IBITS8: return "uint8_t";
    case TYPE_IBITS16: return "uint16_t";
    case TYPE_IBITS32: return "uint32_t";
    case TYPE_IBITS64: return "uint64_t";
    default: errx(1, "Invalid integer bit size");
    }
}

CORD compile_type(type_t *t)
{
    if (t == PATH_TYPE) return "Path_t";
    else if (t == PATH_TYPE_TYPE) return "PathType_t";

    switch (t->tag) {
    case ReturnType: errx(1, "Shouldn't be compiling ReturnType to a type");
    case AbortType: return "void";
    case VoidType: return "void";
    case MemoryType: return "void";
    case BoolType: return "Bool_t";
    case ByteType: return "Byte_t";
    case CStringType: return "const char*";
    case BigIntType: return "Int_t";
    case IntType: return CORD_asprintf("Int%ld_t", Match(t, IntType)->bits);
    case NumType: return Match(t, NumType)->bits == TYPE_NBITS64 ? "Num_t" : CORD_asprintf("Num%ld_t", Match(t, NumType)->bits);
    case TextType: {
        DeclareMatch(text, t, TextType);
        if (!text->lang || streq(text->lang, "Text"))
            return "Text_t";
        else
            return CORD_all(namespace_prefix(text->env, text->env->namespace->parent), text->lang, "$$type");
    }
    case ListType: return "List_t";
    case SetType: return "Table_t";
    case TableType: return "Table_t";
    case FunctionType: {
        DeclareMatch(fn, t, FunctionType);
        CORD code = CORD_all(compile_type(fn->ret), " (*)(");
        for (arg_t *arg = fn->args; arg; arg = arg->next) {
            code = CORD_all(code, compile_type(arg->type));
            if (arg->next) code = CORD_cat(code, ", ");
        }
        if (!fn->args)
            code = CORD_all(code, "void");
        return CORD_all(code, ")");
    }
    case ClosureType: return "Closure_t";
    case PointerType: return CORD_cat(compile_type(Match(t, PointerType)->pointed), "*");
    case StructType: {
        DeclareMatch(s, t, StructType);
        if (s->external) return s->name;
        return CORD_all("struct ", namespace_prefix(s->env, s->env->namespace->parent), s->name, "$$struct");
    }
    case EnumType: {
        DeclareMatch(e, t, EnumType);
        return CORD_all(namespace_prefix(e->env, e->env->namespace->parent), e->name, "$$type");
    }
    case OptionalType: {
        type_t *nonnull = Match(t, OptionalType)->type;
        switch (nonnull->tag) {
        case CStringType: case FunctionType: case ClosureType:
        case PointerType: case EnumType:
            return compile_type(nonnull);
        case TextType:
            return Match(nonnull, TextType)->lang ? compile_type(nonnull) : "OptionalText_t";
        case IntType: case BigIntType: case NumType: case BoolType: case ByteType:
        case ListType: case TableType: case SetType:
            return CORD_all("Optional", compile_type(nonnull));
        case StructType: {
            if (nonnull == PATH_TYPE)
                return "OptionalPath_t";
            if (nonnull == PATH_TYPE_TYPE)
                return "OptionalPathType_t";
            DeclareMatch(s, nonnull, StructType);
            return CORD_all(namespace_prefix(s->env, s->env->namespace->parent), "$Optional", s->name, "$$type");
        }
        default:
            compiler_err(NULL, NULL, NULL, "Optional types are not supported for: ", type_to_str(t));
        }
    }
    case TypeInfoType: return "TypeInfo_t";
    default: compiler_err(NULL, NULL, NULL, "Compiling type is not implemented for type with tag ", t->tag);
    }
}

CORD compile_lvalue(env_t *env, ast_t *ast)
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
            code_err(index->indexed, "This value might be null, so it can't be safely used as an assignment target");

        if (!index->index && container_t->tag == PointerType)
            return compile(env, ast);

        container_t = value_type(container_t);
        type_t *index_t = get_type(env, index->index);
        if (container_t->tag == ListType) {
            CORD target_code = compile_to_pointer_depth(env, index->indexed, 1, false);
            type_t *item_type = Match(container_t, ListType)->item_type;
            CORD index_code = index->index->tag == Int
                ? compile_int_to_type(env, index->index, Type(IntType, .bits=TYPE_IBITS64))
                : (index_t->tag == BigIntType ? CORD_all("Int64$from_int(", compile(env, index->index), ", no)")
                   : CORD_all("(Int64_t)(", compile(env, index->index), ")"));
            if (index->unchecked) {
                return CORD_all("List_lvalue_unchecked(", compile_type(item_type), ", ", target_code, ", ", 
                                index_code, ")");
            } else {
                return CORD_all("List_lvalue(", compile_type(item_type), ", ", target_code, ", ", 
                                index_code,
                                ", ", String((int)(ast->start - ast->file->text)),
                                ", ", String((int)(ast->end - ast->file->text)), ")");
            }
        } else if (container_t->tag == TableType) {
            DeclareMatch(table_type, container_t, TableType);
            if (table_type->default_value) {
                type_t *value_type = get_type(env, table_type->default_value);
                return CORD_all("*Table$get_or_setdefault(",
                                compile_to_pointer_depth(env, index->indexed, 1, false), ", ",
                                compile_type(table_type->key_type), ", ",
                                compile_type(value_type), ", ",
                                compile_to_type(env, index->index, table_type->key_type), ", ",
                                compile_to_type(env, table_type->default_value, table_type->value_type), ", ",
                                compile_type_info(container_t), ")");
            }
            if (index->unchecked)
                code_err(ast, "Table indexes cannot be unchecked");
            return CORD_all("*(", compile_type(Type(PointerType, table_type->value_type)), ")Table$reserve(",
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
}

static CORD compile_assignment(env_t *env, ast_t *target, CORD value)
{
    return CORD_all(compile_lvalue(env, target), " = ", value);
}

static CORD compile_inline_block(env_t *env, ast_t *ast)
{
    if (ast->tag != Block)
        return compile_statement(env, ast);

    CORD code = CORD_EMPTY;
    ast_list_t *stmts = Match(ast, Block)->statements;
    deferral_t *prev_deferred = env->deferred;
    env = fresh_scope(env);
    for (ast_list_t *stmt = stmts; stmt; stmt = stmt->next)
        prebind_statement(env, stmt->ast);
    for (ast_list_t *stmt = stmts; stmt; stmt = stmt->next) {
        code = CORD_all(code, compile_statement(env, stmt->ast), "\n");
        bind_statement(env, stmt->ast);
    }
    for (deferral_t *deferred = env->deferred; deferred && deferred != prev_deferred; deferred = deferred->next) {
        code = CORD_all(code, compile_statement(deferred->defer_env, deferred->block));
    }
    return code;
}

CORD optional_into_nonnone(type_t *t, CORD value)
{
    if (t->tag == OptionalType) t = Match(t, OptionalType)->type;
    switch (t->tag) {
    case IntType: case ByteType:
        return CORD_all(value, ".value");
    case StructType:
        if (t == PATH_TYPE || t == PATH_TYPE_TYPE)
            return value;
        return CORD_all(value, ".value");
    default:
        return value;
    }
}

CORD check_none(type_t *t, CORD value)
{
    t = Match(t, OptionalType)->type;
    // NOTE: these use statement expressions ({...;}) because some compilers
    // complain about excessive parens around equality comparisons
    if (t->tag == PointerType || t->tag == FunctionType || t->tag == CStringType)
        return CORD_all("({", value, " == NULL;})");
    else if (t == PATH_TYPE)
        return CORD_all("({(", value, ").type.$tag == PATH_NONE;})");
    else if (t == PATH_TYPE_TYPE)
        return CORD_all("({(", value, ").$tag == PATH_NONE;})");
    else if (t->tag == BigIntType)
        return CORD_all("({(", value, ").small == 0;})");
    else if (t->tag == ClosureType)
        return CORD_all("({(", value, ").fn == NULL;})");
    else if (t->tag == NumType)
        return CORD_all("isnan(", value, ")");
    else if (t->tag == ListType)
        return CORD_all("({(", value, ").length < 0;})");
    else if (t->tag == TableType || t->tag == SetType)
        return CORD_all("({(", value, ").entries.length < 0;})");
    else if (t->tag == BoolType)
        return CORD_all("({(", value, ") == NONE_BOOL;})");
    else if (t->tag == TextType)
        return CORD_all("({(", value, ").length < 0;})");
    else if (t->tag == IntType || t->tag == ByteType || t->tag == StructType)
        return CORD_all("(", value, ").is_none");
    else if (t->tag == EnumType)
        return CORD_all("({(", value, ").$tag == 0;})");
    print_err("Optional check not implemented for: ", type_to_str(t));
}

static CORD compile_condition(env_t *env, ast_t *ast)
{
    type_t *t = get_type(env, ast);
    if (t->tag == BoolType) {
        return compile(env, ast);
    } else if (t->tag == TextType) {
        return CORD_all("(", compile(env, ast), ").length");
    } else if (t->tag == ListType) {
        return CORD_all("(", compile(env, ast), ").length");
    } else if (t->tag == TableType || t->tag == SetType) {
        return CORD_all("(", compile(env, ast), ").entries.length");
    } else if (t->tag == OptionalType) {
        return CORD_all("!", check_none(t, compile(env, ast)));
    } else if (t->tag == PointerType) {
        code_err(ast, "This pointer will always be non-none, so it should not be used in a conditional.");
    } else {
        code_err(ast, type_to_str(t), " values cannot be used for conditionals");
    }
}

static CORD _compile_statement(env_t *env, ast_t *ast)
{
    switch (ast->tag) {
    case When: {
        // Typecheck to verify exhaustiveness:
        type_t *result_t = get_type(env, ast);
        (void)result_t;

        DeclareMatch(when, ast, When);
        type_t *subject_t = get_type(env, when->subject);

        if (subject_t->tag != EnumType) {
            CORD prefix = CORD_EMPTY, suffix = CORD_EMPTY;
            ast_t *subject = when->subject;
            if (!is_idempotent(when->subject)) {
                prefix = CORD_all("{\n", compile_declaration(subject_t, "_when_subject"), " = ", compile(env, subject), ";\n");
                suffix = "}\n";
                subject = LiteralCode("_when_subject", .type=subject_t);
            }

            CORD code = CORD_EMPTY;
            for (when_clause_t *clause = when->clauses; clause; clause = clause->next) {
                ast_t *comparison = WrapAST(clause->pattern, Equals, .lhs=subject, .rhs=clause->pattern);
                (void)get_type(env, comparison);
                if (code != CORD_EMPTY)
                    code = CORD_all(code, "else ");
                code = CORD_all(code, "if (", compile(env, comparison), ")", compile_statement(env, clause->body));
            }
            if (when->else_body)
                code = CORD_all(code, "else ", compile_statement(env, when->else_body));
            code = CORD_all(prefix, code, suffix);
            return code;
        }

        DeclareMatch(enum_t, subject_t, EnumType);
        CORD code = CORD_all("WHEN(", compile_type(subject_t), ", ", compile(env, when->subject), ", _when_subject, {\n");
        for (when_clause_t *clause = when->clauses; clause; clause = clause->next) {
            if (clause->pattern->tag == Var) {
                const char *clause_tag_name = Match(clause->pattern, Var)->name;
                code = CORD_all(code, "case ", namespace_prefix(enum_t->env, enum_t->env->namespace), "tag$", clause_tag_name, ": {\n",
                                compile_statement(env, clause->body),
                                "break;\n"
                                "}\n");
                continue;
            }

            if (clause->pattern->tag != FunctionCall || Match(clause->pattern, FunctionCall)->fn->tag != Var)
                code_err(clause->pattern, "This is not a valid pattern for a ", type_to_str(subject_t), " enum type");

            const char *clause_tag_name = Match(Match(clause->pattern, FunctionCall)->fn, Var)->name;
            code = CORD_all(code, "case ", namespace_prefix(enum_t->env, enum_t->env->namespace), "tag$", clause_tag_name, ": {\n");
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
                    code = CORD_all(code, compile_declaration(tag_type, compile(env, args->value)), " = _when_subject.", clause_tag_name, ";\n");
                    scope = fresh_scope(scope);
                    set_binding(scope, Match(args->value, Var)->name, tag_type, CORD_EMPTY);
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
                        code = CORD_all(code, compile_declaration(field->type, compile(env, arg->value)), " = _when_subject.", clause_tag_name, ".", field->name, ";\n");
                        set_binding(scope, Match(arg->value, Var)->name, field->type, CORD_EMPTY);
                    }
                    field = field->next;
                }
            }
            if (clause->body->tag == Block) {
                ast_list_t *statements = Match(clause->body, Block)->statements;
                if (!statements || (statements->ast->tag == Pass && !statements->next))
                    code = CORD_all(code, "break;\n}\n");
                else
                    code = CORD_all(code, compile_inline_block(scope, clause->body), "\nbreak;\n}\n");
            } else {
                code = CORD_all(code, compile_statement(scope, clause->body), "\nbreak;\n}\n");
            }
        }
        if (when->else_body) {
            if (when->else_body->tag == Block) {
                ast_list_t *statements = Match(when->else_body, Block)->statements;
                if (!statements || (statements->ast->tag == Pass && !statements->next))
                    code = CORD_all(code, "default: break;");
                else
                    code = CORD_all(code, "default: {\n", compile_inline_block(env, when->else_body), "\nbreak;\n}\n");
            } else {
                code = CORD_all(code, "default: {\n", compile_statement(env, when->else_body), "\nbreak;\n}\n");
            }
        } else {
            code = CORD_all(code, "default: errx(1, \"Invalid tag!\");\n");
        }
        code = CORD_all(code, "\n})\n");
        return code;
    }
    case DocTest: {
        DeclareMatch(test, ast, DocTest);
        type_t *expr_t = get_type(env, test->expr);
        if (!expr_t)
            code_err(test->expr, "I couldn't figure out the type of this expression");

        CORD setup = CORD_EMPTY;
        CORD test_code;
        if (test->expr->tag == Declare) {
            DeclareMatch(decl, test->expr, Declare);
            type_t *t = decl->type ? parse_type_ast(env, decl->type) : get_type(env, decl->value);
            if (t->tag == FunctionType) t = Type(ClosureType, t);
            CORD var = CORD_all("_$", Match(decl->var, Var)->name);
            CORD val_code = compile_declared_value(env, test->expr);
            setup = CORD_all(compile_declaration(t, var), ";\n");
            test_code = CORD_all("(", var, " = ", val_code, ")");
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
                CORD value = compile_to_type(val_scope, assign->values->ast, lhs_t);
                test_code = CORD_all("(", compile_assignment(env, assign->targets->ast, value), ")");
                expr_t = lhs_t;
            } else {
                // Multi-assign or assignment to potentially non-idempotent targets
                if (test->expected && assign->targets->next)
                    code_err(ast, "Sorry, but doctesting with '=' is not supported for multi-assignments");

                test_code = "({ // Assignment\n";

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
                    CORD val_code = compile_to_type(val_scope, value->ast, lhs_t);
                    CORD_appendf(&test_code, "%r $%ld = %r;\n", compile_type(lhs_t), i++, val_code);
                }
                i = 1;
                for (ast_list_t *target = assign->targets; target; target = target->next)
                    test_code = CORD_all(test_code, compile_assignment(env, target->ast, CORD_asprintf("$%ld", i++)), ";\n");

                test_code = CORD_all(test_code, "$1; })");
            }
        } else if (is_update_assignment(test->expr)) {
            binary_operands_t update = BINARY_OPERANDS(test->expr);
            type_t *lhs_t = get_type(env, update.lhs);
            if (update.lhs->tag == Index) {
                type_t *indexed = value_type(get_type(env, Match(update.lhs, Index)->indexed));
                if (indexed->tag == TableType && Match(indexed, TableType)->default_value == NULL)
                    code_err(update.lhs, "Update assignments are not currently supported for tables");
            }

            ast_t *update_var = new(ast_t);
            *update_var = *test->expr;
            update_var->__data.PlusUpdate.lhs = LiteralCode("(*expr)", .type=lhs_t); // UNSAFE
            test_code = CORD_all("({", 
                compile_declaration(Type(PointerType, lhs_t), "expr"), " = &(", compile_lvalue(env, update.lhs), "); ",
                compile_statement(env, update_var), "; *expr; })");
            expr_t = lhs_t;
        } else if (expr_t->tag == VoidType || expr_t->tag == AbortType || expr_t->tag == ReturnType) {
            test_code = CORD_all("({", compile_statement(env, test->expr), " NULL;})");
        } else {
            test_code = compile(env, test->expr);
        }
        if (test->expected) {
            return CORD_asprintf(
                "%rtest(%r, %r, %r, %r, %ld, %ld);",
                setup, compile_type(expr_t), test_code,
                compile_to_type(env, test->expected, expr_t),
                compile_type_info(expr_t),
                (int64_t)(test->expr->start - test->expr->file->text),
                (int64_t)(test->expr->end - test->expr->file->text));
        } else {
            if (expr_t->tag == VoidType || expr_t->tag == AbortType) {
                return CORD_asprintf("%rinspect_void(%r, %r, %ld, %ld);",
                setup, test_code,
                compile_type_info(expr_t),
                (int64_t)(test->expr->start - test->expr->file->text),
                (int64_t)(test->expr->end - test->expr->file->text));
            }
            return CORD_asprintf(
                "%rinspect(%r, %r, %r, %ld, %ld);",
                setup, compile_type(expr_t), test_code,
                compile_type_info(expr_t),
                (int64_t)(test->expr->start - test->expr->file->text),
                (int64_t)(test->expr->end - test->expr->file->text));
        }
    }
    case Declare: {
        DeclareMatch(decl, ast, Declare);
        const char *name = Match(decl->var, Var)->name;
        if (streq(name, "_")) { // Explicit discard
            if (decl->value)
                return CORD_all("(void)", compile(env, decl->value), ";");
            else
                return CORD_EMPTY;
        } else {
            type_t *t = decl->type ? parse_type_ast(env, decl->type) : get_type(env, decl->value);
            if (t->tag == FunctionType) t = Type(ClosureType, t);
            if (t->tag == AbortType || t->tag == VoidType || t->tag == ReturnType)
                code_err(ast, "You can't declare a variable with a ", type_to_str(t), " value");

            CORD val_code = compile_declared_value(env, ast);
            return CORD_all(compile_declaration(t, CORD_cat("_$", name)), " = ", val_code, ";");
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
            CORD val = compile_to_type(val_env, assign->values->ast, lhs_t);
            return CORD_all(compile_assignment(env, assign->targets->ast, val), ";\n");
        }

        CORD code = "{ // Assignment\n";
        int64_t i = 1;
        for (ast_list_t *value = assign->values, *target = assign->targets; value && target; value = value->next, target = target->next) {
            type_t *lhs_t = get_type(env, target->ast);
            if (target->ast->tag == Index && lhs_t->tag == OptionalType
                && value_type(get_type(env, Match(target->ast, Index)->indexed))->tag == TableType)
                lhs_t = Match(lhs_t, OptionalType)->type;
            if (has_stack_memory(lhs_t))
                code_err(ast, "Stack references cannot be assigned to variables because the variable's scope may outlive the scope of the stack memory.");
            env_t *val_env = with_enum_scope(env, lhs_t);
            CORD val = compile_to_type(val_env, value->ast, lhs_t);
            CORD_appendf(&code, "%r $%ld = %r;\n", compile_type(lhs_t), i++, val);
        }
        i = 1;
        for (ast_list_t *target = assign->targets; target; target = target->next) {
            code = CORD_all(code, compile_assignment(env, target->ast, CORD_asprintf("$%ld", i++)), ";\n");
        }
        return CORD_cat(code, "\n}");
    }
    case PlusUpdate: {
        DeclareMatch(update, ast, PlusUpdate);
        type_t *lhs_t = get_type(env, update->lhs);
        if (is_idempotent(update->lhs) && (lhs_t->tag == IntType || lhs_t->tag == NumType || lhs_t->tag == ByteType))
            return CORD_all(compile_lvalue(env, update->lhs), " += ", compile_to_type(env, update->rhs, lhs_t), ";");
        return compile_update_assignment(env, ast);
    }
    case MinusUpdate: {
        DeclareMatch(update, ast, MinusUpdate);
        type_t *lhs_t = get_type(env, update->lhs);
        if (is_idempotent(update->lhs) && (lhs_t->tag == IntType || lhs_t->tag == NumType || lhs_t->tag == ByteType))
            return CORD_all(compile_lvalue(env, update->lhs), " -= ", compile_to_type(env, update->rhs, lhs_t), ";");
        return compile_update_assignment(env, ast);
    }
    case MultiplyUpdate: {
        DeclareMatch(update, ast, MultiplyUpdate);
        type_t *lhs_t = get_type(env, update->lhs);
        if (is_idempotent(update->lhs) && (lhs_t->tag == IntType || lhs_t->tag == NumType || lhs_t->tag == ByteType))
            return CORD_all(compile_lvalue(env, update->lhs), " *= ", compile_to_type(env, update->rhs, lhs_t), ";");
        return compile_update_assignment(env, ast);
    }
    case DivideUpdate: {
        DeclareMatch(update, ast, DivideUpdate);
        type_t *lhs_t = get_type(env, update->lhs);
        if (is_idempotent(update->lhs) && (lhs_t->tag == IntType || lhs_t->tag == NumType || lhs_t->tag == ByteType))
            return CORD_all(compile_lvalue(env, update->lhs), " /= ", compile_to_type(env, update->rhs, lhs_t), ";");
        return compile_update_assignment(env, ast);
    }
    case ModUpdate: {
        DeclareMatch(update, ast, ModUpdate);
        type_t *lhs_t = get_type(env, update->lhs);
        if (is_idempotent(update->lhs) && (lhs_t->tag == IntType || lhs_t->tag == NumType || lhs_t->tag == ByteType))
            return CORD_all(compile_lvalue(env, update->lhs), " %= ", compile_to_type(env, update->rhs, lhs_t), ";");
        return compile_update_assignment(env, ast);
    }
    case PowerUpdate: case Mod1Update: case ConcatUpdate: case LeftShiftUpdate: case UnsignedLeftShiftUpdate:
    case RightShiftUpdate: case UnsignedRightShiftUpdate: case AndUpdate: case OrUpdate: case XorUpdate: {
        return compile_update_assignment(env, ast);
    }
    case StructDef: case EnumDef: case LangDef: case Extend: case FunctionDef: case ConvertDef: {
        return CORD_EMPTY;
    }
    case Skip: {
        const char *target = Match(ast, Skip)->target;
        for (loop_ctx_t *ctx = env->loop_ctx; ctx; ctx = ctx->next) {
            bool matched = !target || CORD_cmp(target, ctx->loop_name) == 0;
            for (ast_list_t *var = ctx->loop_vars; var && !matched; var = var ? var->next : NULL)
                matched = (CORD_cmp(target, Match(var->ast, Var)->name) == 0);

            if (matched) {
                if (!ctx->skip_label) {
                    static int64_t skip_label_count = 1;
                    CORD_sprintf(&ctx->skip_label, "skip_%ld", skip_label_count);
                    ++skip_label_count;
                }
                CORD code = CORD_EMPTY;
                for (deferral_t *deferred = env->deferred; deferred && deferred != ctx->deferred; deferred = deferred->next)
                    code = CORD_all(code, compile_statement(deferred->defer_env, deferred->block));
                if (code)
                    return CORD_all("{\n", code, "goto ", ctx->skip_label, ";\n}\n");
                else
                    return CORD_all("goto ", ctx->skip_label, ";");
            }
        }
        if (env->loop_ctx)
            code_err(ast, "This is not inside any loop");
        else if (target)
            code_err(ast, "No loop target named '", target, "' was found");
        else
            return "continue;";
    }
    case Stop: {
        const char *target = Match(ast, Stop)->target;
        for (loop_ctx_t *ctx = env->loop_ctx; ctx; ctx = ctx->next) {
            bool matched = !target || CORD_cmp(target, ctx->loop_name) == 0;
            for (ast_list_t *var = ctx->loop_vars; var && !matched; var = var ? var->next : var)
                matched = (CORD_cmp(target, Match(var->ast, Var)->name) == 0);

            if (matched) {
                if (!ctx->stop_label) {
                    static int64_t stop_label_count = 1;
                    CORD_sprintf(&ctx->stop_label, "stop_%ld", stop_label_count);
                    ++stop_label_count;
                }
                CORD code = CORD_EMPTY;
                for (deferral_t *deferred = env->deferred; deferred && deferred != ctx->deferred; deferred = deferred->next)
                    code = CORD_all(code, compile_statement(deferred->defer_env, deferred->block));
                if (code)
                    return CORD_all("{\n", code, "goto ", ctx->stop_label, ";\n}\n");
                else
                    return CORD_all("goto ", ctx->stop_label, ";");
            }
        }
        if (env->loop_ctx)
            code_err(ast, "This is not inside any loop");
        else if (target)
            code_err(ast, "No loop target named '", target, "' was found");
        else
            return "break;";
    }
    case Pass: return ";";
    case Defer: {
        ast_t *body = Match(ast, Defer)->body;
        Table_t closed_vars = get_closed_vars(env, NULL, body);

        static int defer_id = 0;
        env_t *defer_env = fresh_scope(env);
        CORD code = CORD_EMPTY;
        for (int64_t i = 1; i <= Table$length(closed_vars); i++) {
            struct { const char *name; binding_t *b; } *entry = Table$entry(closed_vars, i);
            if (entry->b->type->tag == ModuleType)
                continue;
            if (CORD_ncmp(entry->b->code, 0, "userdata->", 0, strlen("userdata->")) == 0) {
                Table$str_set(defer_env->locals, entry->name, entry->b);
            } else {
                CORD defer_name = CORD_asprintf("defer$%d$%s", ++defer_id, entry->name);
                code = CORD_all(
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

        CORD code = CORD_EMPTY;
        for (deferral_t *deferred = env->deferred; deferred; deferred = deferred->next) {
            code = CORD_all(code, compile_statement(deferred->defer_env, deferred->block));
        }

        if (ret) {
            if (env->fn_ret->tag == VoidType || env->fn_ret->tag == AbortType)
                code_err(ast, "This function is not supposed to return any values, according to its type signature");

            env = with_enum_scope(env, env->fn_ret);
            CORD value = compile_to_type(env, ret, env->fn_ret);
            if (env->deferred) {
                code = CORD_all(compile_declaration(env->fn_ret, "ret"), " = ", value, ";\n", code);
                value = "ret";
            }

            return CORD_all(code, "return ", value, ";");
        } else {
            if (env->fn_ret->tag != VoidType)
                code_err(ast, "This function expects you to return a ", type_to_str(env->fn_ret), " value");
            return CORD_all(code, "return;");
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
        CORD body = compile_statement(scope, while_->body);
        if (loop_ctx.skip_label)
            body = CORD_all(body, "\n", loop_ctx.skip_label, ": continue;");
        CORD loop = CORD_all("while (", while_->condition ? compile(scope, while_->condition) : "yes", ") {\n\t", body, "\n}");
        if (loop_ctx.stop_label)
            loop = CORD_all(loop, "\n", loop_ctx.stop_label, ":;");
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
        CORD body_code = compile_statement(scope, body);
        if (loop_ctx.skip_label)
            body_code = CORD_all(body_code, "\n", loop_ctx.skip_label, ": continue;");
        CORD loop = CORD_all("for (;;) {\n\t", body_code, "\n}");
        if (loop_ctx.stop_label)
            loop = CORD_all(loop, "\n", loop_ctx.stop_label, ":;");
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
        CORD naked_body = compile_inline_block(body_scope, for_->body);
        if (loop_ctx.skip_label)
            naked_body = CORD_all(naked_body, "\n", loop_ctx.skip_label, ": continue;");
        CORD stop = loop_ctx.stop_label ? CORD_all("\n", loop_ctx.stop_label, ":;") : CORD_EMPTY;

        // Special case for improving performance for numeric iteration:
        if (for_->iter->tag == MethodCall && streq(Match(for_->iter, MethodCall)->name, "to") &&
            is_int_type(get_type(env, Match(for_->iter, MethodCall)->self))) {
            // TODO: support other integer types
            arg_ast_t *args = Match(for_->iter, MethodCall)->args;
            if (!args) code_err(for_->iter, "to() needs at least one argument");

            type_t *int_type = get_type(env, Match(for_->iter, MethodCall)->self);
            type_t *step_type = int_type->tag == ByteType ? Type(IntType, .bits=TYPE_IBITS8) : int_type;

            CORD last = CORD_EMPTY, step = CORD_EMPTY, optional_step = CORD_EMPTY;
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

            if (!last)
                code_err(for_->iter, "No `last` argument was given");
            
            CORD type_code = compile_type(int_type);
            CORD value = for_->vars ? compile(body_scope, for_->vars->ast) : "i";
            if (int_type->tag == BigIntType) {
                if (optional_step)
                    step = CORD_all("({ OptionalInt_t maybe_step = ", optional_step, "; maybe_step->small == 0 ? (Int$compare_value(last, first) >= 0 ? I_small(1) : I_small(-1)) : (Int_t)maybe_step; })");
                else if (!step)
                    step = "Int$compare_value(last, first) >= 0 ? I_small(1) : I_small(-1)";
                return CORD_all(
                    "for (", type_code, " first = ", compile(env, Match(for_->iter, MethodCall)->self), ", ",
                    value, " = first, last = ", last, ", step = ", step, "; "
                    "Int$compare_value(", value, ", last) != Int$compare_value(step, I_small(0)); ",
                    value, " = Int$plus(", value, ", step)) {\n"
                    "\t", naked_body,
                    "}",
                    stop);
            } else {
                if (optional_step)
                    step = CORD_all("({ ", compile_type(Type(OptionalType, step_type)), " maybe_step = ", optional_step, "; "
                                    "maybe_step.is_none ? (", type_code, ")(last >= first ? 1 : -1) : maybe_step.value; })");
                else if (!step)
                    step = CORD_all("(", type_code, ")(last >= first ? 1 : -1)");
                return CORD_all(
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
            CORD step = compile_arguments(env, for_->iter, arg_spec, args);
            CORD value = for_->vars ? compile(body_scope, for_->vars->ast) : "i";
            return CORD_all(
                "for (Int_t ", value, " = ", compile(env, Match(for_->iter, MethodCall)->self), ", ",
                "step = ", step, "; ; ", value, " = Int$plus(", value, ", step)) {\n"
                "\t", naked_body,
                "}",
                stop);
        }

        type_t *iter_t = value_type(get_type(env, for_->iter));
        type_t *iter_value_t = value_type(iter_t);

        switch (iter_value_t->tag) {
        case ListType: {
            type_t *item_t = Match(iter_value_t, ListType)->item_type;
            CORD index = CORD_EMPTY;
            CORD value = CORD_EMPTY;
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

            CORD loop = CORD_EMPTY;
            loop = CORD_all(loop, "for (int64_t i = 1; i <= iterating.length; ++i)");

            if (index != CORD_EMPTY)
                naked_body = CORD_all("Int_t ", index, " = I(i);\n", naked_body);

            if (value != CORD_EMPTY) {
                loop = CORD_all(loop, "{\n",
                                compile_declaration(item_t, value),
                                " = *(", compile_type(item_t), "*)(iterating.data + (i-1)*iterating.stride);\n",
                                naked_body, "\n}");
            } else {
                loop = CORD_all(loop, "{\n", naked_body, "\n}");
            }

            if (for_->empty)
                loop = CORD_all("if (iterating.length > 0) {\n", loop, "\n} else ", compile_statement(env, for_->empty));

            if (iter_t->tag == PointerType) {
                loop = CORD_all("{\n"
                                "List_t *ptr = ", compile_to_pointer_depth(env, for_->iter, 1, false), ";\n"
                                "\nLIST_INCREF(*ptr);\n"
                                "List_t iterating = *ptr;\n",
                                loop, 
                                stop,
                                "\nLIST_DECREF(*ptr);\n"
                                "}\n");

            } else {
                loop = CORD_all("{\n"
                                "List_t iterating = ", compile_to_pointer_depth(env, for_->iter, 0, false), ";\n",
                                loop, 
                                stop,
                                "}\n");
            }
            return loop;
        }
        case SetType: case TableType: {
            CORD loop = "for (int64_t i = 0; i < iterating.length; ++i) {\n";
            if (for_->vars) {
                if (iter_value_t->tag == SetType) {
                    if (for_->vars->next)
                        code_err(for_->vars->next->ast, "This is too many variables for this loop");
                    CORD item = compile(body_scope, for_->vars->ast);
                    type_t *item_type = Match(iter_value_t, SetType)->item_type;
                    loop = CORD_all(loop, compile_declaration(item_type, item), " = *(", compile_type(item_type), "*)(",
                                    "iterating.data + i*iterating.stride);\n");
                } else {
                    CORD key = compile(body_scope, for_->vars->ast);
                    type_t *key_t = Match(iter_value_t, TableType)->key_type;
                    loop = CORD_all(loop, compile_declaration(key_t, key), " = *(", compile_type(key_t), "*)(",
                                    "iterating.data + i*iterating.stride);\n");

                    if (for_->vars->next) {
                        if (for_->vars->next->next)
                            code_err(for_->vars->next->next->ast, "This is too many variables for this loop");

                        type_t *value_t = Match(iter_value_t, TableType)->value_type;
                        CORD value = compile(body_scope, for_->vars->next->ast);
                        CORD value_offset = CORD_all("offsetof(struct { ", compile_declaration(key_t, "k"), "; ", compile_declaration(value_t, "v"), "; }, v)");
                        loop = CORD_all(loop, compile_declaration(value_t, value), " = *(", compile_type(value_t), "*)(",
                                        "iterating.data + i*iterating.stride + ", value_offset, ");\n");
                    }
                }
            }

            loop = CORD_all(loop, naked_body, "\n}");

            if (for_->empty) {
                loop = CORD_all("if (iterating.length > 0) {\n", loop, "\n} else ", compile_statement(env, for_->empty));
            }

            if (iter_t->tag == PointerType) {
                loop = CORD_all(
                    "{\n",
                    "Table_t *t = ", compile_to_pointer_depth(env, for_->iter, 1, false), ";\n"
                    "LIST_INCREF(t->entries);\n"
                    "List_t iterating = t->entries;\n",
                    loop,
                    "LIST_DECREF(t->entries);\n"
                    "}\n");
            } else {
                loop = CORD_all(
                    "{\n",
                    "List_t iterating = (", compile_to_pointer_depth(env, for_->iter, 0, false), ").entries;\n",
                    loop,
                    "}\n");
            }
            return loop;
        }
        case BigIntType: {
            CORD n;
            if (for_->iter->tag == Int) {
                const char *str = Match(for_->iter, Int)->str;
                Int_t int_val = Int$from_str(str);
                if (int_val.small == 0)
                    code_err(for_->iter, "Failed to parse this integer");
                mpz_t i;
                mpz_init_set_int(i, int_val);
                if (mpz_cmpabs_ui(i, BIGGEST_SMALL_INT) <= 0)
                    n = mpz_get_str(NULL, 10, i);
                else
                    goto big_n;


                if (for_->empty && mpz_cmp_si(i, 0) <= 0) {
                    return compile_statement(env, for_->empty);
                } else {
                    return CORD_all(
                        "for (int64_t i = 1; i <= ", n, "; ++i) {\n",
                        for_->vars ? CORD_all("\tInt_t ", compile(body_scope, for_->vars->ast), " = I_small(i);\n") : CORD_EMPTY,
                        "\t", naked_body,
                        "}\n",
                        stop, "\n");
                }
            }

          big_n:
            n = compile_to_pointer_depth(env, for_->iter, 0, false);
            CORD i = for_->vars ? compile(body_scope, for_->vars->ast) : "i";
            CORD n_var = for_->vars ? CORD_all("max", i) : "n";
            if (for_->empty) {
                return CORD_all(
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
                return CORD_all(
                    "for (Int_t ", i, " = I(1), ", n_var, " = ", n, "; Int$compare_value(", i, ", ", n_var, ") <= 0; ", i, " = Int$plus(", i, ", I(1))) {\n",
                    "\t", naked_body,
                    "}\n",
                    stop, "\n");
            }
        }
        case FunctionType: case ClosureType: {
            // Iterator function:
            CORD code = "{\n";

            CORD next_fn;
            if (is_idempotent(for_->iter)) {
                next_fn = compile_to_pointer_depth(env, for_->iter, 0, false);
            } else {
                code = CORD_all(code, compile_declaration(iter_value_t, "next"), " = ", compile_to_pointer_depth(env, for_->iter, 0, false), ";\n");
                next_fn = "next";
            }

            __typeof(iter_value_t->__data.FunctionType) *fn = iter_value_t->tag == ClosureType ? Match(Match(iter_value_t, ClosureType)->fn, FunctionType) : Match(iter_value_t, FunctionType);

            CORD get_next;
            if (iter_value_t->tag == ClosureType) {
                type_t *fn_t = Match(iter_value_t, ClosureType)->fn;
                arg_t *closure_fn_args = NULL;
                for (arg_t *arg = Match(fn_t, FunctionType)->args; arg; arg = arg->next)
                    closure_fn_args = new(arg_t, .name=arg->name, .type=arg->type, .default_val=arg->default_val, .next=closure_fn_args);
                closure_fn_args = new(arg_t, .name="userdata", .type=Type(PointerType, .pointed=Type(MemoryType)), .next=closure_fn_args);
                REVERSE_LIST(closure_fn_args);
                CORD fn_type_code = compile_type(Type(FunctionType, .args=closure_fn_args, .ret=Match(fn_t, FunctionType)->ret));
                get_next = CORD_all("((", fn_type_code, ")", next_fn, ".fn)(", next_fn, ".userdata)");
            } else {
                get_next = CORD_all(next_fn, "()");
            }

            if (fn->ret->tag == OptionalType) {
                // Use an optional variable `cur` for each iteration step, which will be checked for null
                code = CORD_all(code, compile_declaration(fn->ret, "cur"), ";\n");
                get_next = CORD_all("(cur=", get_next, ", !", check_none(fn->ret, "cur"), ")");
                if (for_->vars) {
                    naked_body = CORD_all(
                        compile_declaration(Match(fn->ret, OptionalType)->type, CORD_all("_$", Match(for_->vars->ast, Var)->name)),
                        " = ", optional_into_nonnone(fn->ret, "cur"), ";\n",
                        naked_body);
                }
                if (for_->empty) {
                    code = CORD_all(code, "if (", get_next, ") {\n"
                                    "\tdo{\n\t\t", naked_body, "\t} while(", get_next, ");\n"
                                    "} else {\n\t", compile_statement(env, for_->empty), "}", stop, "\n}\n");
                } else {
                    code = CORD_all(code, "while(", get_next, ") {\n\t", naked_body, "}\n", stop, "\n}\n");
                }
            } else {
                if (for_->vars) {
                    naked_body = CORD_all(
                        compile_declaration(fn->ret, CORD_all("_$", Match(for_->vars->ast, Var)->name)),
                        " = ", get_next, ";\n", naked_body);
                } else {
                    naked_body = CORD_all(get_next, ";\n", naked_body);
                }
                if (for_->empty)
                    code_err(for_->empty, "This iteration loop will always have values, so this block will never run");
                code = CORD_all(code, "for (;;) {\n\t", naked_body, "}\n", stop, "\n}\n");
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
            CORD code = CORD_all("IF_DECLARE(", compile_statement(truthy_scope, condition), ", ");
            bind_statement(truthy_scope, condition);
            ast_t *var = Match(condition, Declare)->var;
            code = CORD_all(code, compile_condition(truthy_scope, var), ", ");
            type_t *cond_t = get_type(truthy_scope, var);
            if (cond_t->tag == OptionalType) {
                set_binding(truthy_scope, Match(var, Var)->name,
                            Match(cond_t, OptionalType)->type,
                            optional_into_nonnone(cond_t, compile(truthy_scope, var)));
            }
            code = CORD_all(code, compile_statement(truthy_scope, if_->body), ")");
            if (if_->else_body)
                code = CORD_all(code, "\nelse ", compile_statement(env, if_->else_body));
            return code;
        } else {
            CORD code = CORD_all("if (", compile_condition(env, condition), ")");
            env_t *truthy_scope = env;
            type_t *cond_t = get_type(env, condition);
            if (condition->tag == Var && cond_t->tag == OptionalType) {
                truthy_scope = fresh_scope(env);
                set_binding(truthy_scope, Match(condition, Var)->name,
                            Match(cond_t, OptionalType)->type,
                            optional_into_nonnone(cond_t, compile(truthy_scope, condition)));
            }
            code = CORD_all(code, compile_statement(truthy_scope, if_->body));
            if (if_->else_body)
                code = CORD_all(code, "\nelse ", compile_statement(env, if_->else_body));
            return code;
        }
    }
    case Block: {
        return CORD_all("{\n", compile_inline_block(env, ast), "}\n");
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
    case Extern: return CORD_EMPTY;
    case InlineCCode: {
        DeclareMatch(inline_code, ast, InlineCCode);
        CORD code = CORD_EMPTY;
        for (ast_list_t *chunk = inline_code->chunks; chunk; chunk = chunk->next) {
            if (chunk->ast->tag == TextLiteral) {
                code = CORD_all(code, Match(chunk->ast, TextLiteral)->cord);
            } else {
                code = CORD_all(code, compile(env, chunk->ast));
            }
        }
        return code;
    }
    case Use: {
        DeclareMatch(use, ast, Use);
        if (use->what == USE_LOCAL) {
            CORD name = file_base_id(Match(ast, Use)->path);
            return with_source_info(env, ast, CORD_all("_$", name, "$$initialize();\n"));
        } else if (use->what == USE_MODULE) {
            glob_t tm_files;
            if (glob(String(TOMO_HOME"/installed/", use->path, "/[!._0-9]*.tm"), GLOB_TILDE, NULL, &tm_files) != 0)
                code_err(ast, "Could not find library");

            CORD initialization = CORD_EMPTY;

            char *lib_id = String(use->path);
            for (char *p = lib_id; *p; p++) {
                if (!isalnum(*p) && *p != '_')
                    *p = '_';
            }

            for (size_t i = 0; i < tm_files.gl_pathc; i++) {
                const char *filename = tm_files.gl_pathv[i];
                initialization = CORD_all(
                    initialization,
                    with_source_info(env, ast, CORD_all("_$", lib_id, "$", file_base_id(filename), "$$initialize();\n")));
            }
            globfree(&tm_files);
            return initialization;
        } else {
            return CORD_EMPTY;
        }
    }
    default:
        // print("Is discardable: ", ast_to_xml_str(ast), " ==> ", is_discardable(env, ast));
        if (!is_discardable(env, ast))
            code_err(ast, "The ", type_to_str(get_type(env, ast)), " result of this statement cannot be discarded");
        return CORD_asprintf("(void)%r;", compile(env, ast));
    }
}

CORD compile_statement(env_t *env, ast_t *ast) {
    CORD stmt = _compile_statement(env, ast);
    return with_source_info(env, ast, stmt);
}

CORD expr_as_text(CORD expr, type_t *t, CORD color)
{
    switch (t->tag) {
    case MemoryType: return CORD_asprintf("Memory$as_text(stack(%r), %r, &Memory$info)", expr, color);
    case BoolType:
         // NOTE: this cannot use stack(), since bools may actually be bit fields:
         return CORD_asprintf("Bool$as_text((Bool_t[1]){%r}, %r, &Bool$info)", expr, color);
    case CStringType: return CORD_asprintf("CString$as_text(stack(%r), %r, &CString$info)", expr, color);
    case BigIntType: case IntType: case ByteType: case NumType: {
        CORD name = type_to_cord(t);
        return CORD_asprintf("%r$as_text(stack(%r), %r, &%r$info)", name, expr, color, name);
    }
    case TextType: return CORD_asprintf("Text$as_text(stack(%r), %r, %r)", expr, color, compile_type_info(t));
    case ListType: return CORD_asprintf("List$as_text(stack(%r), %r, %r)", expr, color, compile_type_info(t));
    case SetType: return CORD_asprintf("Table$as_text(stack(%r), %r, %r)", expr, color, compile_type_info(t));
    case TableType: return CORD_asprintf("Table$as_text(stack(%r), %r, %r)", expr, color, compile_type_info(t));
    case FunctionType: case ClosureType: return CORD_asprintf("Func$as_text(stack(%r), %r, %r)", expr, color, compile_type_info(t));
    case PointerType: return CORD_asprintf("Pointer$as_text(stack(%r), %r, %r)", expr, color, compile_type_info(t));
    case OptionalType: return CORD_asprintf("Optional$as_text(stack(%r), %r, %r)", expr, color, compile_type_info(t));
    case StructType: case EnumType:
        return CORD_asprintf("generic_as_text(stack(%r), %r, %r)", expr, color, compile_type_info(t));
    default: compiler_err(NULL, NULL, NULL, "Stringifying is not supported for ", type_to_str(t));
    }
}

CORD compile_string(env_t *env, ast_t *ast, CORD color)
{
    type_t *t = get_type(env, ast);
    CORD expr = compile(env, ast);
    return expr_as_text(expr, t, color);
}

CORD compile_to_pointer_depth(env_t *env, ast_t *ast, int64_t target_depth, bool needs_incref)
{
    CORD val = compile(env, ast);
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
                val = CORD_all("(&", val, ")");
            else
                code_err(ast, "This should be a pointer, not ", type_to_str(get_type(env, ast)));
            t = Type(PointerType, .pointed=t, .is_stack=true);
            ++depth;
        } else {
            DeclareMatch(ptr, t, PointerType);
            val = CORD_all("*(", val, ")");
            t = ptr->pointed;
            --depth;
        }
    }

    while (t->tag == PointerType) {
        DeclareMatch(ptr, t, PointerType);
        t = ptr->pointed;
    }

    if (needs_incref && t->tag == ListType)
        val = CORD_all("LIST_COPY(", val, ")");
    else if (needs_incref && (t->tag == TableType || t->tag == SetType))
        val = CORD_all("TABLE_COPY(", val, ")");

    return val;
}

CORD compile_to_type(env_t *env, ast_t *ast, type_t *t)
{
    if (ast->tag == Int && is_numeric_type(non_optional(t))) {
        return compile_int_to_type(env, ast, t);
    } else if (ast->tag == Num && t->tag == NumType) {
        double n = Match(ast, Num)->n;
        switch (Match(t, NumType)->bits) {
        case TYPE_NBITS64: return CORD_asprintf("N64(%.20g)", n);
        case TYPE_NBITS32: return CORD_asprintf("N32(%.10g)", n);
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
        return CORD_all("stack(", compile_to_type(env, ast, Match(t, PointerType)->pointed), ")");

    CORD code = compile(env, ast);
    if (!promote(env, ast, &code, actual, t))
        code_err(ast, "I expected a ", type_to_str(t), " here, but this is a ", type_to_str(actual));
    return code;
}

CORD compile_typed_list(env_t *env, ast_t *ast, type_t *list_type)
{
    DeclareMatch(list, ast, List);
    if (!list->items)
        return "(List_t){.length=0}";

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
        CORD code = CORD_all("TypedListN(", compile_type(item_type), CORD_asprintf(", %ld", n));
        for (ast_list_t *item = list->items; item; item = item->next) {
            code = CORD_all(code, ", ", compile_to_type(scope, item->ast, item_type));
        }
        return CORD_cat(code, ")");
    }

  list_comprehension:
    {
        env_t *scope = item_type->tag == EnumType ? with_enum_scope(env, item_type) : fresh_scope(env);
        static int64_t comp_num = 1;
        const char *comprehension_name = String("list$", comp_num++);
        ast_t *comprehension_var = LiteralCode(CORD_all("&", comprehension_name),
                                               .type=Type(PointerType, .pointed=list_type, .is_stack=true));
        Closure_t comp_action = {.fn=add_to_list_comprehension, .userdata=comprehension_var};
        scope->comprehension_action = &comp_action;
        CORD code = CORD_all("({ List_t ", comprehension_name, " = {};");
        // set_binding(scope, comprehension_name, list_type, comprehension_name);
        for (ast_list_t *item = list->items; item; item = item->next) {
            if (item->ast->tag == Comprehension)
                code = CORD_all(code, "\n", compile_statement(scope, item->ast));
            else
                code = CORD_all(code, compile_statement(env, add_to_list_comprehension(item->ast, comprehension_var)));
        }
        code = CORD_all(code, " ", comprehension_name, "; })");
        return code;
    }
}

CORD compile_typed_set(env_t *env, ast_t *ast, type_t *set_type)
{
    DeclareMatch(set, ast, Set);
    if (!set->items)
        return "((Table_t){})";

    type_t *item_type = Match(set_type, SetType)->item_type;

    size_t n = 0;
    for (ast_list_t *item = set->items; item; item = item->next) {
        ++n;
        if (item->ast->tag == Comprehension)
            goto set_comprehension;
    }
       
    { // No comprehension:
        CORD code = CORD_all("Set(",
                             compile_type(item_type), ", ",
                             compile_type_info(item_type));
        CORD_appendf(&code, ", %zu", n);
        env_t *scope = item_type->tag == EnumType ? with_enum_scope(env, item_type) : env;
        for (ast_list_t *item = set->items; item; item = item->next) {
            code = CORD_all(code, ", ", compile_to_type(scope, item->ast, item_type));
        }
        return CORD_cat(code, ")");
    }

  set_comprehension:
    {
        static int64_t comp_num = 1;
        env_t *scope = item_type->tag == EnumType ? with_enum_scope(env, item_type) : fresh_scope(env);
        const char *comprehension_name = String("set$", comp_num++);
        ast_t *comprehension_var = LiteralCode(CORD_all("&", comprehension_name),
                                               .type=Type(PointerType, .pointed=set_type, .is_stack=true));
        CORD code = CORD_all("({ Table_t ", comprehension_name, " = {};");
        Closure_t comp_action = {.fn=add_to_set_comprehension, .userdata=comprehension_var};
        scope->comprehension_action = &comp_action;
        for (ast_list_t *item = set->items; item; item = item->next) {
            if (item->ast->tag == Comprehension)
                code = CORD_all(code, "\n", compile_statement(scope, item->ast));
            else
                code = CORD_all(code, compile_statement(env, add_to_set_comprehension(item->ast, comprehension_var)));
        }
        code = CORD_all(code, " ", comprehension_name, "; })");
        return code;
    }
}

CORD compile_typed_table(env_t *env, ast_t *ast, type_t *table_type)
{
    DeclareMatch(table, ast, Table);
    if (!table->entries) {
        CORD code = "((Table_t){";
        if (table->fallback)
            code = CORD_all(code, ".fallback=heap(", compile(env, table->fallback),")");
        return CORD_cat(code, "})");
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
        CORD code = CORD_all("Table(",
                             compile_type(key_t), ", ",
                             compile_type(value_t), ", ",
                             compile_type_info(key_t), ", ",
                             compile_type_info(value_t));
        if (table->fallback)
            code = CORD_all(code, ", /*fallback:*/ heap(", compile(env, table->fallback), ")");
        else
            code = CORD_all(code, ", /*fallback:*/ NULL");

        size_t n = 0;
        for (ast_list_t *entry = table->entries; entry; entry = entry->next)
            ++n;
        CORD_appendf(&code, ", %zu", n);

        for (ast_list_t *entry = table->entries; entry; entry = entry->next) {
            DeclareMatch(e, entry->ast, TableEntry);
            code = CORD_all(code, ",\n\t{", compile_to_type(key_scope, e->key, key_t), ", ",
                            compile_to_type(value_scope, e->value, value_t), "}");
        }
        return CORD_cat(code, ")");
    }

  table_comprehension:
    {
        static int64_t comp_num = 1;
        env_t *scope = fresh_scope(env);
        const char *comprehension_name = String("table$", comp_num++);
        ast_t *comprehension_var = LiteralCode(CORD_all("&", comprehension_name),
                                           .type=Type(PointerType, .pointed=table_type, .is_stack=true));

        CORD code = CORD_all("({ Table_t ", comprehension_name, " = {");
        if (table->fallback)
            code = CORD_all(code, ".fallback=heap(", compile(env, table->fallback), "), ");

        code = CORD_cat(code, "};");

        Closure_t comp_action = {.fn=add_to_table_comprehension, .userdata=comprehension_var};
        scope->comprehension_action = &comp_action;
        for (ast_list_t *entry = table->entries; entry; entry = entry->next) {
            if (entry->ast->tag == Comprehension)
                code = CORD_all(code, "\n", compile_statement(scope, entry->ast));
            else
                code = CORD_all(code, compile_statement(env, add_to_table_comprehension(entry->ast, comprehension_var)));
        }
        code = CORD_all(code, " ", comprehension_name, "; })");
        return code;
    }
}

CORD compile_typed_allocation(env_t *env, ast_t *ast, type_t *pointer_type)
{
    // TODO: for constructors, do new(T, ...) instead of heap((T){...})
    type_t *pointed = Match(pointer_type, PointerType)->pointed;
    switch (ast->tag) {
    case HeapAllocate: {
        return CORD_asprintf("heap(%r)", compile_to_type(env, Match(ast, HeapAllocate)->value, pointed));
    }
    case StackReference: {
        ast_t *subject = Match(ast, StackReference)->value;
        if (can_be_mutated(env, subject) && type_eq(pointed, get_type(env, subject)))
            return CORD_all("(&", compile_lvalue(env, subject), ")");
        else
            return CORD_all("stack(", compile_to_type(env, subject, pointed), ")");
    }
    default: code_err(ast, "Not an allocation!");
    }
}

CORD compile_int_to_type(env_t *env, ast_t *ast, type_t *target)
{
    if (ast->tag != Int) {
        CORD code = compile(env, ast);
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
            return CORD_asprintf("(Byte_t)(%s)", c_literal);
        code_err(ast, "This integer cannot fit in a byte");
    } else if (target->tag == NumType) {
        if (Match(target, NumType)->bits == TYPE_NBITS64) {
            return CORD_asprintf("N64(%s)", c_literal);
        } else {
            return CORD_asprintf("N32(%s)", c_literal);
        }
    } else if (target->tag == IntType) {
        int64_t target_bits = (int64_t)Match(target, IntType)->bits;
        switch (target_bits) {
        case TYPE_IBITS64:
            if (mpz_cmp_si(i, INT64_MIN) == 0)
                return "I64(INT64_MIN)";
            if (mpz_cmp_si(i, INT64_MAX) <= 0 && mpz_cmp_si(i, INT64_MIN) >= 0)
                return CORD_asprintf("I64(%sL)", c_literal);
            break;
        case TYPE_IBITS32:
            if (mpz_cmp_si(i, INT32_MAX) <= 0 && mpz_cmp_si(i, INT32_MIN) >= 0)
                return CORD_asprintf("I32(%s)", c_literal);
            break;
        case TYPE_IBITS16:
            if (mpz_cmp_si(i, INT16_MAX) <= 0 && mpz_cmp_si(i, INT16_MIN) >= 0)
                return CORD_asprintf("I16(%s)", c_literal);
            break;
        case TYPE_IBITS8:
            if (mpz_cmp_si(i, INT8_MAX) <= 0 && mpz_cmp_si(i, INT8_MIN) >= 0)
                return CORD_asprintf("I8(%s)", c_literal);
            break;
        default: break;
        }
        code_err(ast, "This integer cannot fit in a ", target_bits, "-bit value");
    } else {
        code_err(ast, "I don't know how to compile this to a ", type_to_str(target));
    }
}

CORD compile_arguments(env_t *env, ast_t *call_ast, arg_t *spec_args, arg_ast_t *call_args)
{
    Table_t used_args = {};
    CORD code = CORD_EMPTY;
    env_t *default_scope = namespace_scope(env);
    for (arg_t *spec_arg = spec_args; spec_arg; spec_arg = spec_arg->next) {
        int64_t i = 1;
        // Find keyword:
        if (spec_arg->name) {
            for (arg_ast_t *call_arg = call_args; call_arg; call_arg = call_arg->next) {
                if (call_arg->name && streq(call_arg->name, spec_arg->name)) {
                    CORD value;
                    if (spec_arg->type->tag == IntType && call_arg->value->tag == Int) {
                        value = compile_int_to_type(env, call_arg->value, spec_arg->type);
                    } else if (spec_arg->type->tag == NumType && call_arg->value->tag == Int) {
                        OptionalInt_t int_val = Int$from_str(Match(call_arg->value, Int)->str);
                        if (int_val.small == 0)
                            code_err(call_arg->value, "Failed to parse this integer");
                        if (Match(spec_arg->type, NumType)->bits == TYPE_NBITS64)
                            value = CORD_asprintf("N64(%.20g)", Num$from_int(int_val, false));
                        else
                            value = CORD_asprintf("N32(%.10g)", (double)Num32$from_int(int_val, false));
                    } else {
                        env_t *arg_env = with_enum_scope(env, spec_arg->type);
                        value = compile_maybe_incref(arg_env, call_arg->value, spec_arg->type);
                    }
                    Table$str_set(&used_args, call_arg->name, call_arg);
                    if (code) code = CORD_cat(code, ", ");
                    code = CORD_cat(code, value);
                    goto found_it;
                }
            }
        }
        // Find positional:
        for (arg_ast_t *call_arg = call_args; call_arg; call_arg = call_arg->next) {
            if (call_arg->name) continue;
            const char *pseudoname = String(i++);
            if (!Table$str_get(used_args, pseudoname)) {
                CORD value;
                if (spec_arg->type->tag == IntType && call_arg->value->tag == Int) {
                    value = compile_int_to_type(env, call_arg->value, spec_arg->type);
                } else if (spec_arg->type->tag == NumType && call_arg->value->tag == Int) {
                    OptionalInt_t int_val = Int$from_str(Match(call_arg->value, Int)->str);
                    if (int_val.small == 0)
                        code_err(call_arg->value, "Failed to parse this integer");
                    if (Match(spec_arg->type, NumType)->bits == TYPE_NBITS64)
                        value = CORD_asprintf("N64(%.20g)", Num$from_int(int_val, false));
                    else
                        value = CORD_asprintf("N32(%.10g)", (double)Num32$from_int(int_val, false));
                } else {
                    env_t *arg_env = with_enum_scope(env, spec_arg->type);
                    value = compile_maybe_incref(arg_env, call_arg->value, spec_arg->type);
                }

                Table$str_set(&used_args, pseudoname, call_arg);
                if (code) code = CORD_cat(code, ", ");
                code = CORD_cat(code, value);
                goto found_it;
            }
        }

        if (spec_arg->default_val) {
            if (code) code = CORD_cat(code, ", ");
            code = CORD_cat(code, compile_maybe_incref(default_scope, spec_arg->default_val, get_arg_type(env, spec_arg)));
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

CORD compile_string_literal(CORD literal)
{
    CORD code = "\"";
    CORD_pos i;
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-conversion"
#endif
    CORD_FOR(i, literal) {
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
        char c = CORD_pos_fetch(i);
        switch (c) {
        case '\\': code = CORD_cat(code, "\\\\"); break;
        case '"': code = CORD_cat(code, "\\\""); break;
        case '\a': code = CORD_cat(code, "\\a"); break;
        case '\b': code = CORD_cat(code, "\\b"); break;
        case '\n': code = CORD_cat(code, "\\n"); break;
        case '\r': code = CORD_cat(code, "\\r"); break;
        case '\t': code = CORD_cat(code, "\\t"); break;
        case '\v': code = CORD_cat(code, "\\v"); break;
        default: {
            if (isprint(c))
                code = CORD_cat_char(code, c);
            else
                CORD_sprintf(&code, "%r\\x%02X\"\"", code, (uint8_t)c);
            break;
        }
        }
    }
    return CORD_cat(code, "\"");
}

static bool string_literal_is_all_ascii(CORD literal)
{
    CORD_pos i;
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-conversion"
#endif
    CORD_FOR(i, literal) {
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
        if (!isascii(CORD_pos_fetch(i)))
            return false;
    }
    return true;
}

CORD compile_none(type_t *t)
{
    if (t == NULL)
        compiler_err(NULL, NULL, NULL, "I can't compile a `none` value with no type");

    if (t->tag == OptionalType)
        t = Match(t, OptionalType)->type;

    if (t == NULL)
        compiler_err(NULL, NULL, NULL, "I can't compile a `none` value with no type");

    if (t == PATH_TYPE) return "NONE_PATH";
    else if (t == PATH_TYPE_TYPE) return "((OptionalPathType_t){})";

    switch (t->tag) {
    case BigIntType: return "NONE_INT";
    case IntType: {
        switch (Match(t, IntType)->bits) {
        case TYPE_IBITS8: return "NONE_INT8";
        case TYPE_IBITS16: return "NONE_INT16";
        case TYPE_IBITS32: return "NONE_INT32";
        case TYPE_IBITS64: return "NONE_INT64";
        default: errx(1, "Invalid integer bit size");
        }
        break;
    }
    case BoolType: return "NONE_BOOL";
    case ByteType: return "NONE_BYTE";
    case ListType: return "NONE_LIST";
    case TableType: return "NONE_TABLE";
    case SetType: return "NONE_TABLE";
    case TextType: return "NONE_TEXT";
    case CStringType: return "NULL";
    case PointerType: return CORD_all("((", compile_type(t), ")NULL)");
    case ClosureType: return "NONE_CLOSURE";
    case NumType: return "nan(\"null\")";
    case StructType: return CORD_all("((", compile_type(Type(OptionalType, .type=t)), "){.is_none=true})");
    case EnumType: {
        env_t *enum_env = Match(t, EnumType)->env;
        return CORD_all("((", compile_type(t), "){", namespace_prefix(enum_env, enum_env->namespace), "null})");
    }
    default: compiler_err(NULL, NULL, NULL, "none isn't implemented for this type: ", type_to_str(t));
    }
}

CORD compile_empty(type_t *t)
{
    if (t == NULL)
        compiler_err(NULL, NULL, NULL, "I can't compile a value with no type");

    if (t->tag == OptionalType)
        return compile_none(t);

    if (t == PATH_TYPE) return "NONE_PATH";
    else if (t == PATH_TYPE_TYPE) return "((OptionalPathType_t){})";

    switch (t->tag) {
    case BigIntType: return "I(0)";
    case IntType: {
        switch (Match(t, IntType)->bits) {
        case TYPE_IBITS8: return "I8(0)";
        case TYPE_IBITS16: return "I16(0)";
        case TYPE_IBITS32: return "I32(0)";
        case TYPE_IBITS64: return "I64(0)";
        default: errx(1, "Invalid integer bit size");
        }
        break;
    }
    case ByteType: return "((Byte_t)0)";
    case BoolType: return "((Bool_t)no)";
    case ListType: return "((List_t){})";
    case TableType: case SetType: return "((Table_t){})";
    case TextType: return "Text(\"\")";
    case CStringType: return "\"\"";
    case PointerType: {
        DeclareMatch(ptr, t, PointerType);
        CORD empty_pointed = compile_empty(ptr->pointed);
        return empty_pointed == CORD_EMPTY ? CORD_EMPTY : CORD_all(ptr->is_stack ? "stack(" : "heap(", empty_pointed, ")");
    }
    case NumType: {
        return Match(t, NumType)->bits == TYPE_NBITS32 ? "N32(0.0f)" : "N64(0.0)" ;
    }
    case StructType: {
        DeclareMatch(struct_, t, StructType);
        CORD code = CORD_all("((", compile_type(t), "){");
        for (arg_t *field = struct_->fields; field; field = field->next) {
            CORD empty_field = field->default_val
                ? compile(struct_->env, field->default_val)
                : compile_empty(field->type);
            if (empty_field == CORD_EMPTY)
                return CORD_EMPTY;

            code = CORD_all(code, empty_field);
            if (field->next)
                code = CORD_all(code, ", ");
        }
        return CORD_all(code, "})");
    }
    case EnumType: {
        DeclareMatch(enum_, t, EnumType);
        tag_t *tag = enum_->tags;
        assert(tag);
        assert(tag->type);
        if (Match(tag->type, StructType)->fields)
            return CORD_all("((", compile_type(t), "){.$tag=", String(tag->tag_value), ", .", tag->name, "=", compile_empty(tag->type), "})");
        else
            return CORD_all("((", compile_type(t), "){.$tag=", String(tag->tag_value), "})");
    }
    default: return CORD_EMPTY;
    }
}

static CORD compile_declared_value(env_t *env, ast_t *declare_ast)
{
    DeclareMatch(decl, declare_ast, Declare);
    type_t *t = decl->type ? parse_type_ast(env, decl->type) : get_type(env, decl->value);

    if (t->tag == AbortType || t->tag == VoidType || t->tag == ReturnType)
        code_err(declare_ast, "You can't declare a variable with a ", type_to_str(t), " value");

    if (decl->value) {
        CORD val_code = compile_maybe_incref(env, decl->value, t);
        if (t->tag == FunctionType) {
            assert(promote(env, decl->value, &val_code, t, Type(ClosureType, t)));
            t = Type(ClosureType, t);
        }
        return val_code;
    } else {
        CORD val_code = compile_empty(t);
        if (val_code == CORD_EMPTY)
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

CORD compile(env_t *env, ast_t *ast)
{
    switch (ast->tag) {
    case None: {
        code_err(ast, "I can't figure out what this `none`'s type is!");
    }
    case Bool: return Match(ast, Bool)->b ? "yes" : "no";
    case Var: {
        binding_t *b = get_binding(env, Match(ast, Var)->name);
        if (b)
            return b->code ? b->code : CORD_cat("_$", Match(ast, Var)->name);
        return CORD_cat("_$", Match(ast, Var)->name);
        // code_err(ast, "I don't know of any variable by this name");
    }
    case Int: {
        const char *str = Match(ast, Int)->str;
        OptionalInt_t int_val = Int$from_str(str);
        if (int_val.small == 0)
            code_err(ast, "Failed to parse this integer");
        mpz_t i;
        mpz_init_set_int(i, int_val);
        if (mpz_cmpabs_ui(i, BIGGEST_SMALL_INT) <= 0) {
            return CORD_asprintf("I_small(%s)", str);
        } else if (mpz_cmp_si(i, INT64_MAX) <= 0 && mpz_cmp_si(i, INT64_MIN) >= 0) {
            return CORD_asprintf("Int$from_int64(%s)", str);
        } else {
            return CORD_asprintf("Int$from_str(\"%s\")", str);
        }
    }
    case Num: {
        return CORD_asprintf("N64(%.20g)", Match(ast, Num)->n);
    }
    case Not: {
        ast_t *value = Match(ast, Not)->value;
        type_t *t = get_type(env, value);

        binding_t *b = get_namespace_binding(env, value, "negated");
        if (b && b->type->tag == FunctionType) {
            DeclareMatch(fn, b->type, FunctionType);
            if (fn->args && can_promote(t, get_arg_type(env, fn->args)))
                return CORD_all(b->code, "(", compile_arguments(env, ast, fn->args, new(arg_ast_t, .value=value)), ")");
        }

        if (t->tag == BoolType)
            return CORD_all("!(", compile(env, value), ")");
        else if (t->tag == IntType || t->tag == ByteType)
            return CORD_all("~(", compile(env, value), ")");
        else if (t->tag == ListType)
            return CORD_all("((", compile(env, value), ").length == 0)");
        else if (t->tag == SetType || t->tag == TableType)
            return CORD_all("((", compile(env, value), ").entries.length == 0)");
        else if (t->tag == TextType)
            return CORD_all("(", compile(env, value), " == CORD_EMPTY)");
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
                return CORD_all(b->code, "(", compile_arguments(env, ast, fn->args, new(arg_ast_t, .value=value)), ")");
        }

        if (t->tag == IntType || t->tag == NumType)
            return CORD_all("-(", compile(env, value), ")");

        code_err(ast, "I don't know how to get the negative value of type ", type_to_str(t));

    }
    case HeapAllocate: case StackReference: {
        return compile_typed_allocation(env, ast, get_type(env, ast));
    }
    case Optional: {
        ast_t *value = Match(ast, Optional)->value;
        CORD value_code = compile(env, value);
        return promote_to_optional(get_type(env, value), value_code);
    }
    case NonOptional: {
        ast_t *value = Match(ast, NonOptional)->value;
        type_t *t = get_type(env, value);
        CORD value_code = compile(env, value);
        return CORD_all("({ ", compile_declaration(t, "opt"), " = ", value_code, "; ",
                        "if unlikely (", check_none(t, "opt"), ")\n",
                        CORD_asprintf("fail_source(%r, %ld, %ld, \"This was expected to be a value, but it's none\");\n",
                                      CORD_quoted(ast->file->filename),
                                      (long)(value->start - value->file->text),
                                      (long)(value->end - value->file->text)),
                        optional_into_nonnone(t, "opt"), "; })");
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
        if (is_numeric_type(lhs_t) && binop.rhs->tag == Int) {
            operand_t = lhs_t;
        } else if (is_numeric_type(rhs_t) && binop.lhs->tag == Int) {
            operand_t = rhs_t;
        } else {
            switch (compare_precision(lhs_t, rhs_t)) {
            case NUM_PRECISION_LESS: operand_t = rhs_t; break;
            case NUM_PRECISION_MORE: operand_t = lhs_t; break;
            case NUM_PRECISION_EQUAL: operand_t = lhs_t; break;
            default: {
                if (can_compile_to_type(env, binop.rhs, lhs_t)) {
                    operand_t = lhs_t;
                } else if (can_compile_to_type(env, binop.lhs, rhs_t)) {
                    operand_t = rhs_t;
                } else {
                    code_err(ast, "I can't do comparisons between ", type_to_str(lhs_t), " and ", type_to_str(rhs_t));
                }
                break;
            }
            }
        }

        CORD lhs, rhs;
        lhs = compile_to_type(env, binop.lhs, operand_t);
        rhs = compile_to_type(env, binop.rhs, operand_t);

        switch (operand_t->tag) {
        case BigIntType:
            return CORD_all(ast->tag == Equals ? CORD_EMPTY : "!", "Int$equal_value(", lhs, ", ", rhs, ")");
        case BoolType: case ByteType: case IntType: case NumType: case PointerType: case FunctionType:
            return CORD_all("(", lhs, ast->tag == Equals ? " == " : " != ", rhs, ")");
        default:
            return CORD_all(ast->tag == Equals ? CORD_EMPTY : "!",
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

        CORD lhs = compile_to_type(env, cmp.lhs, operand_t);
        CORD rhs = compile_to_type(env, cmp.rhs, operand_t);

        if (ast->tag == Compare)
            return CORD_all("generic_compare(stack(", lhs, "), stack(", rhs, "), ",
                            compile_type_info(operand_t), ")");

        const char *op = binop_operator(ast->tag);
        switch (operand_t->tag) {
        case BigIntType:
            return CORD_all("(Int$compare_value(", lhs, ", ", rhs, ") ", op, " 0)");
        case BoolType: case ByteType: case IntType: case NumType: case PointerType: case FunctionType:
            return CORD_all("(", lhs, " ", op, " ", rhs, ")");
        default:
            return CORD_all("(generic_compare(stack(", lhs, "), stack(", rhs, "), ",
                            compile_type_info(operand_t), ") ", op, " 0)");
        }
    }
    case TextLiteral: {
        CORD literal = Match(ast, TextLiteral)->cord; 
        if (literal == CORD_EMPTY)
            return "EMPTY_TEXT";

        if (string_literal_is_all_ascii(literal))
            return CORD_all("Text(", compile_string_literal(literal), ")");
        else
            return CORD_all("Text$from_str(", compile_string_literal(literal), ")");
    }
    case TextJoin: {
        const char *lang = Match(ast, TextJoin)->lang;
        const char *colorize = Match(ast, TextJoin)->colorize ? "yes" : "no";

        type_t *text_t = lang ? Table$str_get(*env->types, lang) : TEXT_TYPE;
        if (!text_t || text_t->tag != TextType)
            code_err(ast, quoted(lang), " is not a valid text language name");

        CORD lang_constructor;
        if (!lang || streq(lang, "Text"))
            lang_constructor = "Text";
        else
            lang_constructor = CORD_all(namespace_prefix(Match(text_t, TextType)->env, Match(text_t, TextType)->env->namespace->parent), lang);

        ast_list_t *chunks = Match(ast, TextJoin)->children;
        if (!chunks) {
            return CORD_all(lang_constructor, "(\"\")");
        } else if (!chunks->next && chunks->ast->tag == TextLiteral) {
            CORD literal = Match(chunks->ast, TextLiteral)->cord; 
            if (string_literal_is_all_ascii(literal))
                return CORD_all(lang_constructor, "(", compile_string_literal(literal), ")");
            return CORD_all("((", compile_type(text_t), ")", compile(env, chunks->ast), ")");
        } else {
            CORD code = CORD_EMPTY;
            for (ast_list_t *chunk = chunks; chunk; chunk = chunk->next) {
                CORD chunk_code;
                type_t *chunk_t = get_type(env, chunk->ast);
                if (chunk->ast->tag == TextLiteral || type_eq(chunk_t, text_t)) {
                    chunk_code = compile(env, chunk->ast);
                } else {
                    binding_t *constructor = get_constructor(env, text_t, new(arg_ast_t, .value=chunk->ast));
                    if (constructor) {
                        arg_t *arg_spec = Match(constructor->type, FunctionType)->args;
                        arg_ast_t *args = new(arg_ast_t, .value=chunk->ast);
                        chunk_code = CORD_all(constructor->code, "(", compile_arguments(env, ast, arg_spec, args), ")");
                    } else if (type_eq(text_t, TEXT_TYPE)) {
                        if (chunk_t->tag == TextType)
                            chunk_code = compile(env, chunk->ast);
                        else
                            chunk_code = compile_string(env, chunk->ast, colorize);
                    } else {
                        code_err(chunk->ast, "I don't know how to convert ", type_to_str(chunk_t), " to ", type_to_str(text_t));
                    }
                }
                code = CORD_cat(code, chunk_code);
                if (chunk->next) code = CORD_cat(code, ", ");
            }
            if (chunks->next)
                return CORD_all(lang_constructor, "s(", code, ")");
            else
                return code;
        }
    }
    case Path: {
        return CORD_all("Path(", compile_string_literal(Match(ast, Path)->path), ")");
    }
    case Block: {
        ast_list_t *stmts = Match(ast, Block)->statements;
        if (stmts && !stmts->next)
            return compile(env, stmts->ast);

        CORD code = "({\n";
        deferral_t *prev_deferred = env->deferred;
        env = fresh_scope(env);
        for (ast_list_t *stmt = stmts; stmt; stmt = stmt->next)
            prebind_statement(env, stmt->ast);
        for (ast_list_t *stmt = stmts; stmt; stmt = stmt->next) {
            if (stmt->next) {
                code = CORD_all(code, compile_statement(env, stmt->ast), "\n");
            } else {
                // TODO: put defer after evaluating block expression
                for (deferral_t *deferred = env->deferred; deferred && deferred != prev_deferred; deferred = deferred->next) {
                    code = CORD_all(code, compile_statement(deferred->defer_env, deferred->block));
                }
                code = CORD_all(code, compile(env, stmt->ast), ";\n");
            }
            bind_statement(env, stmt->ast);
        }

        return CORD_cat(code, "})");
    }
    case Min: case Max: {
        type_t *t = get_type(env, ast);
        ast_t *key = ast->tag == Min ? Match(ast, Min)->key : Match(ast, Max)->key;
        ast_t *lhs = ast->tag == Min ? Match(ast, Min)->lhs : Match(ast, Max)->lhs;
        ast_t *rhs = ast->tag == Min ? Match(ast, Min)->rhs : Match(ast, Max)->rhs;
        const char *key_name = "$";
        if (key == NULL) key = FakeAST(Var, key_name);

        env_t *expr_env = fresh_scope(env);
        set_binding(expr_env, key_name, t, "ternary$lhs");
        CORD lhs_key = compile(expr_env, key);

        set_binding(expr_env, key_name, t, "ternary$rhs");
        CORD rhs_key = compile(expr_env, key);

        type_t *key_t = get_type(expr_env, key);
        CORD comparison;
        if (key_t->tag == BigIntType)
            comparison = CORD_all("(Int$compare_value(", lhs_key, ", ", rhs_key, ")", (ast->tag == Min ? "<=" : ">="), "0)");
        else if (key_t->tag == IntType || key_t->tag == NumType || key_t->tag == BoolType || key_t->tag == PointerType || key_t->tag == ByteType)
            comparison = CORD_all("((", lhs_key, ")", (ast->tag == Min ? "<=" : ">="), "(", rhs_key, "))");
        else
            comparison = CORD_all("generic_compare(stack(", lhs_key, "), stack(", rhs_key, "), ", compile_type_info(key_t), ")",
                                  (ast->tag == Min ? "<=" : ">="), "0");

        return CORD_all(
            "({\n",
            compile_type(t), " ternary$lhs = ", compile(env, lhs), ", ternary$rhs = ", compile(env, rhs), ";\n",
            comparison, " ? ternary$lhs : ternary$rhs;\n"
            "})");
    }
    case List: {
        DeclareMatch(list, ast, List);
        if (!list->items)
            return "(List_t){.length=0}";

        type_t *list_type = get_type(env, ast);
        return compile_typed_list(env, ast, list_type);
    }
    case Table: {
        DeclareMatch(table, ast, Table);
        if (!table->entries) {
            CORD code = "((Table_t){";
            if (table->fallback)
                code = CORD_all(code, ".fallback=heap(", compile(env, table->fallback),")");
            return CORD_cat(code, "})");
        }

        type_t *table_type = get_type(env, ast);
        return compile_typed_table(env, ast, table_type);
    }
    case Set: {
        DeclareMatch(set, ast, Set);
        if (!set->items)
            return "((Table_t){})";

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
        CORD name = CORD_asprintf("%rlambda$%ld", namespace_prefix(env, env->namespace), lambda->id);

        env->code->function_naming = CORD_all(
            env->code->function_naming,
            CORD_asprintf("register_function(%r, Text(\"%s.tm\"), %ld, Text(%r));\n",
                          name, file_base_name(ast->file->filename), get_line_number(ast->file, ast->start),
                          CORD_quoted(type_to_cord(get_type(env, ast)))));

        env_t *body_scope = fresh_scope(env);
        body_scope->deferred = NULL;
        for (arg_ast_t *arg = lambda->args; arg; arg = arg->next) {
            type_t *arg_type = get_arg_ast_type(env, arg);
            set_binding(body_scope, arg->name, arg_type, CORD_all("_$", arg->name));
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
            CORD def = "typedef struct {";
            for (int64_t i = 1; i <= Table$length(closed_vars); i++) {
                struct { const char *name; binding_t *b; } *entry = Table$entry(closed_vars, i);
                if (has_stack_memory(entry->b->type))
                    code_err(ast, "This function is holding onto a reference to ", type_to_str(entry->b->type),
                             " stack memory in the variable `", entry->name, "`, but the function may outlive the stack memory");
                if (entry->b->type->tag == ModuleType)
                    continue;
                set_binding(body_scope, entry->name, entry->b->type, CORD_cat("userdata->", entry->name));
                def = CORD_all(def, compile_declaration(entry->b->type, entry->name), "; ");
            }
            def = CORD_all(def, "} ", name, "$userdata_t;");
            env->code->local_typedefs = CORD_all(env->code->local_typedefs, def);
        }

        CORD code = CORD_all("static ", compile_type(ret_t), " ", name, "(");
        for (arg_ast_t *arg = lambda->args; arg; arg = arg->next) {
            type_t *arg_type = get_arg_ast_type(env, arg);
            code = CORD_all(code, compile_type(arg_type), " _$", arg->name, ", ");
        }

        CORD userdata;
        if (Table$length(closed_vars) == 0) {
            code = CORD_cat(code, "void *_)");
            userdata = "NULL";
        } else {
            userdata = CORD_all("new(", name, "$userdata_t");
            for (int64_t i = 1; i <= Table$length(closed_vars); i++) {
                struct { const char *name; binding_t *b; } *entry = Table$entry(closed_vars, i);
                if (entry->b->type->tag == ModuleType)
                    continue;
                binding_t *b = get_binding(env, entry->name);
                assert(b);
                CORD binding_code = b->code;
                if (entry->b->type->tag == ListType)
                    userdata = CORD_all(userdata, ", LIST_COPY(", binding_code, ")");
                else if (entry->b->type->tag == TableType || entry->b->type->tag == SetType)
                    userdata = CORD_all(userdata, ", TABLE_COPY(", binding_code, ")");
                else 
                    userdata = CORD_all(userdata, ", ", binding_code);
            }
            userdata = CORD_all(userdata, ")");
            code = CORD_all(code, name, "$userdata_t *userdata)");
        }

        CORD body = CORD_EMPTY;
        for (ast_list_t *stmt = Match(lambda->body, Block)->statements; stmt; stmt = stmt->next) {
            if (stmt->next || ret_t->tag == VoidType || ret_t->tag == AbortType || get_type(body_scope, stmt->ast)->tag == ReturnType)
                body = CORD_all(body, compile_statement(body_scope, stmt->ast), "\n");
            else
                body = CORD_all(body, compile_statement(body_scope, FakeAST(Return, stmt->ast)), "\n");
            bind_statement(body_scope, stmt->ast);
        }
        if ((ret_t->tag == VoidType || ret_t->tag == AbortType) && body_scope->deferred)
            body = CORD_all(body, compile_statement(body_scope, FakeAST(Return)), "\n");

        env->code->lambdas = CORD_all(env->code->lambdas, code, " {\n", body, "\n}\n");
        return CORD_all("((Closure_t){", name, ", ", userdata, "})");
    }
    case MethodCall: {
        DeclareMatch(call, ast, MethodCall);
        type_t *self_t = get_type(env, call->self);

        if (streq(call->name, "serialized")) {
            if (call->args)
                code_err(ast, ".serialized() doesn't take any arguments"); 
            return CORD_all("generic_serialize((", compile_declaration(self_t, "[1]"), "){",
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

        CORD self = compile(env, call->self);

#define EXPECT_POINTER(article, name) do { \
    if (pointer_depth < 1) code_err(call->self, "I expected "article" "name" pointer here, not "article" "name" value"); \
    else if (pointer_depth > 1) code_err(call->self, "I expected "article" "name" pointer here, not a nested "name" pointer"); \
} while (0)
        switch (self_value_t->tag) {
        case ListType: {
            type_t *item_t = Match(self_value_t, ListType)->item_type;
            CORD padded_item_size = CORD_all("sizeof(", compile_type(item_t), ")");

            if (streq(call->name, "insert")) {
                EXPECT_POINTER("a", "list");
                arg_t *arg_spec = new(arg_t, .name="item", .type=item_t,
                                      .next=new(arg_t, .name="at", .type=INT_TYPE, .default_val=FakeAST(Int, .str="0")));
                return CORD_all("List$insert_value(", self, ", ", compile_arguments(env, ast, arg_spec, call->args), ", ",
                                padded_item_size, ")");
            } else if (streq(call->name, "insert_all")) {
                EXPECT_POINTER("a", "list");
                arg_t *arg_spec = new(arg_t, .name="items", .type=self_value_t,
                                      .next=new(arg_t, .name="at", .type=INT_TYPE, .default_val=FakeAST(Int, .str="0")));
                return CORD_all("List$insert_all(", self, ", ", compile_arguments(env, ast, arg_spec, call->args), ", ",
                                padded_item_size, ")");
            } else if (streq(call->name, "remove_at")) {
                EXPECT_POINTER("a", "list");
                arg_t *arg_spec = new(arg_t, .name="index", .type=INT_TYPE, .default_val=FakeAST(Int, .str="-1"),
                                      .next=new(arg_t, .name="count", .type=INT_TYPE, .default_val=FakeAST(Int, .str="1")));
                return CORD_all("List$remove_at(", self, ", ", compile_arguments(env, ast, arg_spec, call->args), ", ",
                                padded_item_size, ")");
            } else if (streq(call->name, "remove_item")) {
                EXPECT_POINTER("a", "list");
                arg_t *arg_spec = new(arg_t, .name="item", .type=item_t,
                                      .next=new(arg_t, .name="max_count", .type=INT_TYPE, .default_val=FakeAST(Int, .str="-1")));
                return CORD_all("List$remove_item_value(", self, ", ", compile_arguments(env, ast, arg_spec, call->args), ", ",
                                compile_type_info(self_value_t), ")");
            } else if (streq(call->name, "has")) {
                self = compile_to_pointer_depth(env, call->self, 0, false);
                arg_t *arg_spec = new(arg_t, .name="item", .type=item_t);
                return CORD_all("List$has_value(", self, ", ", compile_arguments(env, ast, arg_spec, call->args), ", ",
                                compile_type_info(self_value_t), ")");
            } else if (streq(call->name, "sample")) {
                type_t *random_num_type = parse_type_string(env, "func(->Num)?");
                self = compile_to_pointer_depth(env, call->self, 0, false);
                arg_t *arg_spec = new(arg_t, .name="count", .type=INT_TYPE,
                    .next=new(arg_t, .name="weights", .type=Type(ListType, .item_type=Type(NumType, .bits=TYPE_NBITS64)),
                              .default_val=FakeAST(None),
                              .next=new(arg_t, .name="random", .type=random_num_type, .default_val=FakeAST(None))));
                return CORD_all("List$sample(", self, ", ", compile_arguments(env, ast, arg_spec, call->args), ", ",
                                padded_item_size, ")");
            } else if (streq(call->name, "shuffle")) {
                type_t *random_int64_type = parse_type_string(env, "func(min,max:Int64->Int64)?");
                EXPECT_POINTER("a", "list");
                arg_t *arg_spec = new(arg_t, .name="random", .type=random_int64_type, .default_val=FakeAST(None));
                return CORD_all("List$shuffle(", self, ", ", compile_arguments(env, ast, arg_spec, call->args), ", ", padded_item_size, ")");
            } else if (streq(call->name, "shuffled")) {
                type_t *random_int64_type = parse_type_string(env, "func(min,max:Int64->Int64)?");
                self = compile_to_pointer_depth(env, call->self, 0, false);
                arg_t *arg_spec = new(arg_t, .name="random", .type=random_int64_type, .default_val=FakeAST(None));
                return CORD_all("List$shuffled(", self, ", ", compile_arguments(env, ast, arg_spec, call->args), ", ", padded_item_size, ")");
            } else if (streq(call->name, "random")) {
                type_t *random_int64_type = parse_type_string(env, "func(min,max:Int64->Int64)?");
                self = compile_to_pointer_depth(env, call->self, 0, false);
                arg_t *arg_spec = new(arg_t, .name="random", .type=random_int64_type, .default_val=FakeAST(None));
                return CORD_all("List$random_value(", self, ", ", compile_arguments(env, ast, arg_spec, call->args), ", ", compile_type(item_t), ")");
            } else if (streq(call->name, "sort") || streq(call->name, "sorted")) {
                if (streq(call->name, "sort"))
                    EXPECT_POINTER("a", "list");
                else
                    self = compile_to_pointer_depth(env, call->self, 0, false);
                CORD comparison;
                if (call->args) {
                    type_t *item_ptr = Type(PointerType, .pointed=item_t, .is_stack=true);
                    type_t *fn_t = NewFunctionType(Type(IntType, .bits=TYPE_IBITS32), {.name="x", .type=item_ptr}, {.name="y", .type=item_ptr});
                    arg_t *arg_spec = new(arg_t, .name="by", .type=Type(ClosureType, .fn=fn_t));
                    comparison = compile_arguments(env, ast, arg_spec, call->args);
                } else {
                    comparison = CORD_all("((Closure_t){.fn=generic_compare, .userdata=(void*)", compile_type_info(item_t), "})");
                }
                return CORD_all("List$", call->name, "(", self, ", ", comparison, ", ", padded_item_size, ")");
            } else if (streq(call->name, "heapify")) {
                EXPECT_POINTER("a", "list");
                CORD comparison;
                if (call->args) {
                    type_t *item_ptr = Type(PointerType, .pointed=item_t, .is_stack=true);
                    type_t *fn_t = NewFunctionType(Type(IntType, .bits=TYPE_IBITS32), {.name="x", .type=item_ptr}, {.name="y", .type=item_ptr});
                    arg_t *arg_spec = new(arg_t, .name="by", .type=Type(ClosureType, .fn=fn_t));
                    comparison = compile_arguments(env, ast, arg_spec, call->args);
                } else {
                    comparison = CORD_all("((Closure_t){.fn=generic_compare, .userdata=(void*)", compile_type_info(item_t), "})");
                }
                return CORD_all("List$heapify(", self, ", ", comparison, ", ", padded_item_size, ")");
            } else if (streq(call->name, "heap_push")) {
                EXPECT_POINTER("a", "list");
                type_t *item_ptr = Type(PointerType, .pointed=item_t, .is_stack=true);
                type_t *fn_t = NewFunctionType(Type(IntType, .bits=TYPE_IBITS32), {.name="x", .type=item_ptr}, {.name="y", .type=item_ptr});
                ast_t *default_cmp = LiteralCode(CORD_all("((Closure_t){.fn=generic_compare, .userdata=(void*)",
                                                          compile_type_info(item_t), "})"),
                                                 .type=Type(ClosureType, .fn=fn_t));
                arg_t *arg_spec = new(arg_t, .name="item", .type=item_t,
                                      .next=new(arg_t, .name="by", .type=Type(ClosureType, .fn=fn_t), .default_val=default_cmp));
                CORD arg_code = compile_arguments(env, ast, arg_spec, call->args);
                return CORD_all("List$heap_push_value(", self, ", ", arg_code, ", ", padded_item_size, ")");
            } else if (streq(call->name, "heap_pop")) {
                EXPECT_POINTER("a", "list");
                type_t *item_ptr = Type(PointerType, .pointed=item_t, .is_stack=true);
                type_t *fn_t = NewFunctionType(Type(IntType, .bits=TYPE_IBITS32), {.name="x", .type=item_ptr}, {.name="y", .type=item_ptr});
                ast_t *default_cmp = LiteralCode(CORD_all("((Closure_t){.fn=generic_compare, .userdata=(void*)",
                                                          compile_type_info(item_t), "})"),
                                                 .type=Type(ClosureType, .fn=fn_t));
                arg_t *arg_spec = new(arg_t, .name="by", .type=Type(ClosureType, .fn=fn_t), .default_val=default_cmp);
                CORD arg_code = compile_arguments(env, ast, arg_spec, call->args);
                return CORD_all("List$heap_pop_value(", self, ", ", arg_code, ", ", compile_type(item_t), ", _, ",
                                promote_to_optional(item_t, "_"), ", ", compile_none(item_t), ")");
            } else if (streq(call->name, "binary_search")) {
                self = compile_to_pointer_depth(env, call->self, 0, call->args != NULL);
                type_t *item_ptr = Type(PointerType, .pointed=item_t, .is_stack=true);
                type_t *fn_t = NewFunctionType(Type(IntType, .bits=TYPE_IBITS32), {.name="x", .type=item_ptr}, {.name="y", .type=item_ptr});
                ast_t *default_cmp = LiteralCode(
                    CORD_all("((Closure_t){.fn=generic_compare, .userdata=(void*)",
                             compile_type_info(item_t), "})"),
                    .type=Type(ClosureType, .fn=fn_t));
                arg_t *arg_spec = new(arg_t, .name="target", .type=item_t,
                                      .next=new(arg_t, .name="by", .type=Type(ClosureType, .fn=fn_t), .default_val=default_cmp));
                CORD arg_code = compile_arguments(env, ast, arg_spec, call->args);
                return CORD_all("List$binary_search_value(", self, ", ", arg_code, ")");
            } else if (streq(call->name, "clear")) {
                EXPECT_POINTER("a", "list");
                (void)compile_arguments(env, ast, NULL, call->args);
                return CORD_all("List$clear(", self, ")");
            } else if (streq(call->name, "find")) {
                self = compile_to_pointer_depth(env, call->self, 0, false);
                arg_t *arg_spec = new(arg_t, .name="item", .type=item_t);
                return CORD_all("List$find_value(", self, ", ", compile_arguments(env, ast, arg_spec, call->args),
                                ", ", compile_type_info(self_value_t), ")");
            } else if (streq(call->name, "first")) {
                self = compile_to_pointer_depth(env, call->self, 0, call->args != NULL);
                type_t *item_ptr = Type(PointerType, .pointed=item_t, .is_stack=true);
                type_t *predicate_type = Type(
                    ClosureType, .fn=NewFunctionType(Type(BoolType), {.name="item", .type=item_ptr}));
                arg_t *arg_spec = new(arg_t, .name="predicate", .type=predicate_type);
                return CORD_all("List$first(", self, ", ", compile_arguments(env, ast, arg_spec, call->args), ")");
            } else if (streq(call->name, "from")) {
                self = compile_to_pointer_depth(env, call->self, 0, true);
                arg_t *arg_spec = new(arg_t, .name="first", .type=INT_TYPE);
                return CORD_all("List$from(", self, ", ", compile_arguments(env, ast, arg_spec, call->args), ")");
            } else if (streq(call->name, "to")) {
                self = compile_to_pointer_depth(env, call->self, 0, true);
                arg_t *arg_spec = new(arg_t, .name="last", .type=INT_TYPE);
                return CORD_all("List$to(", self, ", ", compile_arguments(env, ast, arg_spec, call->args), ")");
            } else if (streq(call->name, "by")) {
                self = compile_to_pointer_depth(env, call->self, 0, true);
                arg_t *arg_spec = new(arg_t, .name="stride", .type=INT_TYPE);
                return CORD_all("List$by(", self, ", ", compile_arguments(env, ast, arg_spec, call->args), ", ", padded_item_size, ")");
            } else if (streq(call->name, "reversed")) {
                self = compile_to_pointer_depth(env, call->self, 0, true);
                (void)compile_arguments(env, ast, NULL, call->args);
                return CORD_all("List$reversed(", self, ", ", padded_item_size, ")");
            } else if (streq(call->name, "unique")) {
                self = compile_to_pointer_depth(env, call->self, 0, false);
                (void)compile_arguments(env, ast, NULL, call->args);
                return CORD_all("Table$from_entries(", self, ", Set$info(", compile_type_info(item_t), "))");
            } else if (streq(call->name, "pop")) {
                EXPECT_POINTER("a", "list");
                arg_t *arg_spec = new(arg_t, .name="index", .type=INT_TYPE, .default_val=FakeAST(Int, "-1"));
                CORD index = compile_arguments(env, ast, arg_spec, call->args);
                return CORD_all("List$pop(", self, ", ", index, ", ", compile_type(item_t), ", _, ",
                                promote_to_optional(item_t, "_"), ", ", compile_none(item_t), ")");
            } else if (streq(call->name, "counts")) {
                self = compile_to_pointer_depth(env, call->self, 0, false);
                (void)compile_arguments(env, ast, NULL, call->args);
                return CORD_all("List$counts(", self, ", ", compile_type_info(self_value_t), ")");
            } else code_err(ast, "There is no '", call->name, "' method for lists");
        }
        case SetType: {
            DeclareMatch(set, self_value_t, SetType);
            if (streq(call->name, "has")) {
                self = compile_to_pointer_depth(env, call->self, 0, false);
                arg_t *arg_spec = new(arg_t, .name="key", .type=set->item_type);
                return CORD_all("Table$has_value(", self, ", ", compile_arguments(env, ast, arg_spec, call->args), ", ",
                                compile_type_info(self_value_t), ")");
            } else if (streq(call->name, "add")) {
                EXPECT_POINTER("a", "set");
                arg_t *arg_spec = new(arg_t, .name="item", .type=set->item_type);
                return CORD_all("Table$set_value(", self, ", ", compile_arguments(env, ast, arg_spec, call->args), ", NULL, ",
                                compile_type_info(self_value_t), ")");
            } else if (streq(call->name, "add_all")) {
                EXPECT_POINTER("a", "set");
                arg_t *arg_spec = new(arg_t, .name="items", .type=Type(ListType, .item_type=Match(self_value_t, SetType)->item_type));
                return CORD_all("({ Table_t *set = ", self, "; ",
                                "List_t to_add = ", compile_arguments(env, ast, arg_spec, call->args), "; ",
                                "for (int64_t i = 0; i < to_add.length; i++)\n"
                                "Table$set(set, to_add.data + i*to_add.stride, NULL, ", compile_type_info(self_value_t), ");\n",
                                "(void)0; })");
            } else if (streq(call->name, "remove")) {
                EXPECT_POINTER("a", "set");
                arg_t *arg_spec = new(arg_t, .name="item", .type=set->item_type);
                return CORD_all("Table$remove_value(", self, ", ", compile_arguments(env, ast, arg_spec, call->args), ", ",
                                compile_type_info(self_value_t), ")");
            } else if (streq(call->name, "remove_all")) {
                EXPECT_POINTER("a", "set");
                arg_t *arg_spec = new(arg_t, .name="items", .type=Type(ListType, .item_type=Match(self_value_t, SetType)->item_type));
                return CORD_all("({ Table_t *set = ", self, "; ",
                                "List_t to_add = ", compile_arguments(env, ast, arg_spec, call->args), "; ",
                                "for (int64_t i = 0; i < to_add.length; i++)\n"
                                "Table$remove(set, to_add.data + i*to_add.stride, ", compile_type_info(self_value_t), ");\n",
                                "(void)0; })");
            } else if (streq(call->name, "clear")) {
                EXPECT_POINTER("a", "set");
                (void)compile_arguments(env, ast, NULL, call->args);
                return CORD_all("Table$clear(", self, ")");
            } else if (streq(call->name, "with")) {
                self = compile_to_pointer_depth(env, call->self, 0, false);
                arg_t *arg_spec = new(arg_t, .name="other", .type=self_value_t);
                return CORD_all("Table$with(", self, ", ", compile_arguments(env, ast, arg_spec, call->args),
                                ", ", compile_type_info(self_value_t), ")");
            } else if (streq(call->name, "overlap")) {
                self = compile_to_pointer_depth(env, call->self, 0, false);
                arg_t *arg_spec = new(arg_t, .name="other", .type=self_value_t);
                return CORD_all("Table$overlap(", self, ", ", compile_arguments(env, ast, arg_spec, call->args),
                                ", ", compile_type_info(self_value_t), ")");
            } else if (streq(call->name, "without")) {
                self = compile_to_pointer_depth(env, call->self, 0, false);
                arg_t *arg_spec = new(arg_t, .name="other", .type=self_value_t);
                return CORD_all("Table$without(", self, ", ", compile_arguments(env, ast, arg_spec, call->args),
                                ", ", compile_type_info(self_value_t), ")");
            } else if (streq(call->name, "is_subset_of")) {
                self = compile_to_pointer_depth(env, call->self, 0, false);
                arg_t *arg_spec = new(arg_t, .name="other", .type=self_value_t,
                                      .next=new(arg_t, .name="strict", .type=Type(BoolType), .default_val=FakeAST(Bool, false)));
                return CORD_all("Table$is_subset_of(", self, ", ", compile_arguments(env, ast, arg_spec, call->args),
                                ", ", compile_type_info(self_value_t), ")");
            } else if (streq(call->name, "is_superset_of")) {
                self = compile_to_pointer_depth(env, call->self, 0, false);
                arg_t *arg_spec = new(arg_t, .name="other", .type=self_value_t,
                                      .next=new(arg_t, .name="strict", .type=Type(BoolType), .default_val=FakeAST(Bool, false)));
                return CORD_all("Table$is_superset_of(", self, ", ", compile_arguments(env, ast, arg_spec, call->args),
                                ", ", compile_type_info(self_value_t), ")");
            } else code_err(ast, "There is no '", call->name, "' method for tables");
        }
        case TableType: {
            DeclareMatch(table, self_value_t, TableType);
            if (streq(call->name, "get")) {
                self = compile_to_pointer_depth(env, call->self, 0, false);
                arg_t *arg_spec = new(arg_t, .name="key", .type=table->key_type);
                return CORD_all(
                    "Table$get_optional(", self, ", ", compile_type(table->key_type), ", ",
                    compile_type(table->value_type), ", ", compile_arguments(env, ast, arg_spec, call->args), ", ",
                    "_, ", optional_into_nonnone(table->value_type, "(*_)"), ", ", compile_none(table->value_type), ", ",
                    compile_type_info(self_value_t), ")");
            } else if (streq(call->name, "get_or_set")) {
                self = compile_to_pointer_depth(env, call->self, 1, false);
                arg_t *arg_spec = new(arg_t, .name="key", .type=table->key_type,
                                      .next=new(arg_t, .name="default", .type=table->value_type, .default_val=table->default_value));
                return CORD_all("*Table$get_or_setdefault(",
                                self, ", ", compile_type(table->key_type), ", ",
                                compile_type(table->value_type), ", ",
                                compile_arguments(env, ast, arg_spec, call->args), ", ",
                                compile_type_info(self_value_t), ")");
            } else if (streq(call->name, "has")) {
                self = compile_to_pointer_depth(env, call->self, 0, false);
                arg_t *arg_spec = new(arg_t, .name="key", .type=table->key_type);
                return CORD_all("Table$has_value(", self, ", ", compile_arguments(env, ast, arg_spec, call->args), ", ",
                                compile_type_info(self_value_t), ")");
            } else if (streq(call->name, "set")) {
                EXPECT_POINTER("a", "table");
                arg_t *arg_spec = new(arg_t, .name="key", .type=table->key_type,
                                      .next=new(arg_t, .name="value", .type=table->value_type));
                return CORD_all("Table$set_value(", self, ", ", compile_arguments(env, ast, arg_spec, call->args), ", ",
                                compile_type_info(self_value_t), ")");
            } else if (streq(call->name, "remove")) {
                EXPECT_POINTER("a", "table");
                arg_t *arg_spec = new(arg_t, .name="key", .type=table->key_type);
                return CORD_all("Table$remove_value(", self, ", ", compile_arguments(env, ast, arg_spec, call->args), ", ",
                                compile_type_info(self_value_t), ")");
            } else if (streq(call->name, "clear")) {
                EXPECT_POINTER("a", "table");
                (void)compile_arguments(env, ast, NULL, call->args);
                return CORD_all("Table$clear(", self, ")");
            } else if (streq(call->name, "sorted")) {
                self = compile_to_pointer_depth(env, call->self, 0, false);
                (void)compile_arguments(env, ast, NULL, call->args);
                return CORD_all("Table$sorted(", self, ", ", compile_type_info(self_value_t), ")");
            } else code_err(ast, "There is no '", call->name, "' method for tables");
        }
        default: {
            DeclareMatch(methodcall, ast, MethodCall);
            type_t *fn_t = get_method_type(env, methodcall->self, methodcall->name);
            arg_ast_t *args = new(arg_ast_t, .value=methodcall->self, .next=methodcall->args);
            binding_t *b = get_namespace_binding(env, methodcall->self, methodcall->name);
            if (!b) code_err(ast, "No such method");
            return CORD_all(b->code, "(", compile_arguments(env, ast, Match(fn_t, FunctionType)->args, args), ")");
        }
        }
#undef EXPECT_POINTER
    }
    case FunctionCall: {
        DeclareMatch(call, ast, FunctionCall);
        type_t *fn_t = get_type(env, call->fn);
        if (fn_t->tag == FunctionType) {
            CORD fn = compile(env, call->fn);
            return CORD_all(fn, "(", compile_arguments(env, ast, Match(fn_t, FunctionType)->args, call->args), ")");
        } else if (fn_t->tag == TypeInfoType) {
            type_t *t = Match(fn_t, TypeInfoType)->type;

            // Literal constructors for numeric types like `Byte(123)` should not go through any conversion, just a cast:
            if (is_numeric_type(t) && call->args && !call->args->next && call->args->value->tag == Int)
                return compile_to_type(env, call->args->value, t);
            else if (t->tag == NumType && call->args && !call->args->next && call->args->value->tag == Num)
                return compile_to_type(env, call->args->value, t);

            binding_t *constructor = get_constructor(env, t, call->args);
            if (constructor) {
                arg_t *arg_spec = Match(constructor->type, FunctionType)->args;
                return CORD_all(constructor->code, "(", compile_arguments(env, ast, arg_spec, call->args), ")");
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
                return expr_as_text(compile(env, call->args->value), actual, "no");
            } else if (t->tag == CStringType) {
                // C String constructor:
                if (!call->args || call->args->next)
                    code_err(call->fn, "This constructor takes exactly 1 argument");
                if (call->args->value->tag == TextLiteral)
                    return compile_string_literal(Match(call->args->value, TextLiteral)->cord);
                else if (call->args->value->tag == TextJoin && Match(call->args->value, TextJoin)->children == NULL)
                    return "\"\"";
                else if (call->args->value->tag == TextJoin && Match(call->args->value, TextJoin)->children->next == NULL)
                    return compile_string_literal(Match(Match(call->args->value, TextJoin)->children->ast, TextLiteral)->cord);
                return CORD_all("Text$as_c_string(", expr_as_text(compile(env, call->args->value), actual, "no"), ")");
            } else if (t->tag == StructType) {
                DeclareMatch(struct_, t, StructType);
                if (!struct_->opaque && is_valid_call(env, struct_->fields, call->args, true)) {
                    return CORD_all("((", compile_type(t), "){",
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
            CORD fn_type_code = compile_type(Type(FunctionType, .args=closure_fn_args, .ret=Match(fn_t, FunctionType)->ret));

            CORD closure = compile(env, call->fn);
            CORD arg_code = compile_arguments(env, ast, type_args, call->args);
            if (arg_code) arg_code = CORD_cat(arg_code, ", ");
            if (call->fn->tag == Var) {
                return CORD_all("((", fn_type_code, ")", closure, ".fn)(", arg_code, closure, ".userdata)");
            } else {
                return CORD_all("({ Closure_t closure = ", closure, "; ((", fn_type_code, ")closure.fn)(",
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
        return CORD_all("({ ", compile_declaration(t, "deserialized"), ";\n"
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
        set_binding(when_env, "when", t, "when");
        return CORD_all(
            "({ ", compile_declaration(t, "when"), ";\n",
            compile_statement(when_env, WrapAST(ast, When, .subject=original->subject, .clauses=new_clauses, .else_body=else_body)),
            "when; })");
    }
    case If: {
        DeclareMatch(if_, ast, If);
        ast_t *condition = if_->condition;
        CORD decl_code = CORD_EMPTY;
        env_t *truthy_scope = env, *falsey_scope = env;

        CORD condition_code;
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
            return CORD_all("({ ", decl_code, "if (", condition_code, ") ", compile_statement(truthy_scope, if_->body),
                            "\n", compile(falsey_scope, if_->else_body), "; })");
        else if (false_type->tag == AbortType || false_type->tag == ReturnType)
            return CORD_all("({ ", decl_code, "if (!(", condition_code, ")) ", compile_statement(falsey_scope, if_->else_body),
                            "\n", compile(truthy_scope, if_->body), "; })");
        else if (decl_code != CORD_EMPTY)
            return CORD_all("({ ", decl_code, "(", condition_code, ") ? ", compile(truthy_scope, if_->body), " : ",
                            compile(falsey_scope, if_->else_body), ";})");
        else
            return CORD_all("((", condition_code, ") ? ",
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
        ast_t *body = LiteralCode("{}"); // placeholder
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

            CORD code = CORD_all(
                "({ // Reduction:\n",
                compile_declaration(item_value_type, "prev"), ";\n"
                "OptionalBool_t result = NONE_BOOL;\n"
                );

            ast_t *comparison = new(ast_t, .file=ast->file, .start=ast->start, .end=ast->end,
                                    .tag=op, .__data.Plus.lhs=LiteralCode("prev", .type=item_value_type), .__data.Plus.rhs=item_value);
            body->__data.InlineCCode.chunks = new(ast_list_t, .ast=FakeAST(TextLiteral, CORD_all(
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
            code = CORD_all(code, compile_statement(env, loop), "\nresult;})");
            return code;
        } else if (op == Min || op == Max) {
            // Min/max:
            const char *superlative = op == Min ? "min" : "max";
            CORD code = CORD_all(
                "({ // Reduction:\n",
                compile_declaration(item_t, superlative), ";\n"
                "Bool_t has_value = no;\n"
                );

            CORD item_code = compile(body_scope, item);
            ast_e cmp_op = op == Min ? LessThan : GreaterThan;
            if (reduction->key) {
                env_t *key_scope = fresh_scope(env);
                set_binding(key_scope, "$", item_t, item_code);
                type_t *key_type = get_type(key_scope, reduction->key);
                const char *superlative_key = op == Min ? "min_key" : "max_key";
                code = CORD_all(code, compile_declaration(key_type, superlative_key), ";\n");

                ast_t *comparison = new(ast_t, .file=ast->file, .start=ast->start, .end=ast->end,
                                        .tag=cmp_op, .__data.Plus.lhs=LiteralCode("key", .type=key_type),
                                        .__data.Plus.rhs=LiteralCode(superlative_key, .type=key_type));

                body->__data.InlineCCode.chunks = new(ast_list_t, .ast=FakeAST(TextLiteral, CORD_all(
                    compile_declaration(key_type, "key"), " = ", compile(key_scope, reduction->key), ";\n",
                    "if (!has_value || ", compile(body_scope, comparison), ") {\n"
                    "    ", superlative, " = ", compile(body_scope, item), ";\n"
                    "    ", superlative_key, " = key;\n"
                    "    has_value = yes;\n"
                    "}\n")));
            } else {
                ast_t *comparison = new(ast_t, .file=ast->file, .start=ast->start, .end=ast->end,
                                        .tag=cmp_op, .__data.Plus.lhs=item,
                                        .__data.Plus.rhs=LiteralCode(superlative, .type=item_t));
                body->__data.InlineCCode.chunks = new(ast_list_t, .ast=FakeAST(TextLiteral, CORD_all(
                    "if (!has_value || ", compile(body_scope, comparison), ") {\n"
                    "    ", superlative, " = ", compile(body_scope, item), ";\n"
                    "    has_value = yes;\n"
                    "}\n")));
            }


            code = CORD_all(code, compile_statement(env, loop), "\nhas_value ? ", promote_to_optional(item_t, superlative),
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

            CORD code = CORD_all(
                "({ // Reduction:\n",
                compile_declaration(reduction_type, "reduction"), ";\n"
                "Bool_t has_value = no;\n"
                );

            // For the special case of (or)/(and), we need to early out if we can:
            CORD early_out = CORD_EMPTY;
            if (op == Compare) {
                if (reduction_type->tag != IntType || Match(reduction_type, IntType)->bits != TYPE_IBITS32)
                    code_err(ast, "<> reductions are only supported for Int32 values");
            } else if (op == And) {
                if (reduction_type->tag == BoolType)
                    early_out = "if (!reduction) break;";
                else if (reduction_type->tag == OptionalType)
                    early_out = CORD_all("if (", check_none(reduction_type, "reduction"), ") break;");
            } else if (op == Or) {
                if (reduction_type->tag == BoolType)
                    early_out = "if (reduction) break;";
                else if (reduction_type->tag == OptionalType)
                    early_out = CORD_all("if (!", check_none(reduction_type, "reduction"), ") break;");
            }

            ast_t *combination = new(ast_t, .file=ast->file, .start=ast->start, .end=ast->end,
                                     .tag=op, .__data.Plus.lhs=LiteralCode("reduction", .type=reduction_type),
                                     .__data.Plus.rhs=item_value);
            body->__data.InlineCCode.chunks = new(ast_list_t, .ast=FakeAST(TextLiteral, CORD_all(
                "if (!has_value) {\n"
                "    reduction = ", compile(body_scope, item_value), ";\n"
                "    has_value = yes;\n"
                "} else {\n"
                "    reduction = ", compile(body_scope, combination), ";\n",
                early_out,
                "}\n")));

            code = CORD_all(code, compile_statement(env, loop), "\nhas_value ? ", promote_to_optional(reduction_type, "reduction"),
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
                for (Table_t *locals = env->locals; locals; locals = locals->fallback) {
                    if (locals == info->env->locals)
                        goto is_inside_type;
                }
                code_err(ast, "Fields that start with underscores are not accessible on types outside of the type definition.");
              is_inside_type:;
            }
            binding_t *b = get_binding(info->env, f->field);
            if (!b) code_err(ast, "I couldn't find the field '", f->field, "' on this type");
            if (!b->code) code_err(ast, "I couldn't figure out how to compile this field");
            return b->code;
        }
        case TextType: {
            const char *lang = Match(value_t, TextType)->lang; 
            if (lang && streq(f->field, "text")) {
                CORD text = compile_to_pointer_depth(env, f->fielded, 0, false);
                return CORD_all("((Text_t)", text, ")");
            } else if (streq(f->field, "length")) {
                return CORD_all("Int$from_int64((", compile_to_pointer_depth(env, f->fielded, 0, false), ").length)");
            }
            code_err(ast, "There is no '", f->field, "' field on ", type_to_str(value_t), " values");
        }
        case StructType: {
            for (arg_t *field = Match(value_t, StructType)->fields; field; field = field->next) {
                if (streq(field->name, f->field)) {
                    if (fielded_t->tag == PointerType) {
                        CORD fielded = compile_to_pointer_depth(env, f->fielded, 1, false);
                        return CORD_asprintf("(%r)->%s", fielded, f->field);
                    } else {
                        CORD fielded = compile(env, f->fielded);
                        return CORD_asprintf("(%r).%s", fielded, f->field);
                    }
                }
            }
            code_err(ast, "The field '", f->field, "' is not a valid field name of ", type_to_str(value_t));
        }
        case EnumType: {
            DeclareMatch(e, value_t, EnumType);
            for (tag_t *tag = e->tags; tag; tag = tag->next) {
                if (streq(f->field, tag->name)) {
                    CORD prefix = namespace_prefix(e->env, e->env->namespace);
                    if (fielded_t->tag == PointerType) {
                        CORD fielded = compile_to_pointer_depth(env, f->fielded, 1, false);
                        return CORD_all("((", fielded, ")->$tag == ", prefix, "tag$", tag->name, ")");
                    } else {
                        CORD fielded = compile(env, f->fielded);
                        return CORD_all("((", fielded, ").$tag == ", prefix, "tag$", tag->name, ")");
                    }
                }
            }
            code_err(ast, "The field '", f->field, "' is not a valid tag name of ", type_to_str(value_t));
        }
        case ListType: {
            if (streq(f->field, "length"))
                return CORD_all("Int$from_int64((", compile_to_pointer_depth(env, f->fielded, 0, false), ").length)");
            code_err(ast, "There is no ", f->field, " field on lists");
        }
        case SetType: {
            if (streq(f->field, "items"))
                return CORD_all("LIST_COPY((", compile_to_pointer_depth(env, f->fielded, 0, false), ").entries)");
            else if (streq(f->field, "length"))
                return CORD_all("Int$from_int64((", compile_to_pointer_depth(env, f->fielded, 0, false), ").entries.length)");
            code_err(ast, "There is no '", f->field, "' field on sets");
        }
        case TableType: {
            if (streq(f->field, "length")) {
                return CORD_all("Int$from_int64((", compile_to_pointer_depth(env, f->fielded, 0, false), ").entries.length)");
            } else if (streq(f->field, "keys")) {
                return CORD_all("LIST_COPY((", compile_to_pointer_depth(env, f->fielded, 0, false), ").entries)");
            } else if (streq(f->field, "values")) {
                DeclareMatch(table, value_t, TableType);
                CORD offset = CORD_all("offsetof(struct { ", compile_declaration(table->key_type, "k"), "; ", compile_declaration(table->value_type, "v"), "; }, v)");
                return CORD_all("({ List_t *entries = &(", compile_to_pointer_depth(env, f->fielded, 0, false), ").entries;\n"
                                "LIST_INCREF(*entries);\n"
                                "List_t values = *entries;\n"
                                "values.data += ", offset, ";\n"
                                "values; })");
            } else if (streq(f->field, "fallback")) {
                return CORD_all("({ Table_t *_fallback = (", compile_to_pointer_depth(env, f->fielded, 0, false), ").fallback; _fallback ? *_fallback : NONE_TABLE; })");
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
                return CORD_all("*({ List_t *list = ", compile(env, indexing->indexed), "; LIST_INCREF(*list); list; })");
            } else if (ptr->pointed->tag == TableType || ptr->pointed->tag == SetType) {
                return CORD_all("*({ Table_t *t = ", compile(env, indexing->indexed), "; TABLE_INCREF(*t); t; })");
            } else {
                return CORD_all("*(", compile(env, indexing->indexed), ")");
            }
        }

        type_t *container_t = value_type(indexed_type);
        type_t *index_t = get_type(env, indexing->index);
        if (container_t->tag == ListType) {
            if (index_t->tag != IntType && index_t->tag != BigIntType && index_t->tag != ByteType)
                code_err(indexing->index, "Lists can only be indexed by integers, not ", type_to_str(index_t));
            type_t *item_type = Match(container_t, ListType)->item_type;
            CORD list = compile_to_pointer_depth(env, indexing->indexed, 0, false);
            file_t *f = indexing->index->file;
            CORD index_code = indexing->index->tag == Int
                ? compile_int_to_type(env, indexing->index, Type(IntType, .bits=TYPE_IBITS64))
                : (index_t->tag == BigIntType ? CORD_all("Int64$from_int(", compile(env, indexing->index), ", no)")
                   : CORD_all("(Int64_t)(", compile(env, indexing->index), ")"));
            if (indexing->unchecked)
                return CORD_all("List_get_unchecked(", compile_type(item_type), ", ", list, ", ", index_code, ")");
            else
                return CORD_all("List_get(", compile_type(item_type), ", ", list, ", ", index_code, ", ",
                                CORD_asprintf("%ld", (int64_t)(indexing->index->start - f->text)), ", ",
                                CORD_asprintf("%ld", (int64_t)(indexing->index->end - f->text)),
                                ")");
        } else if (container_t->tag == TableType) {
            DeclareMatch(table_type, container_t, TableType);
            if (indexing->unchecked)
                code_err(ast, "Table indexes cannot be unchecked");
            if (table_type->default_value) {
                return CORD_all("Table$get_or_default(",
                                compile_to_pointer_depth(env, indexing->indexed, 0, false), ", ",
                                compile_type(table_type->key_type), ", ",
                                compile_type(table_type->value_type), ", ",
                                compile(env, indexing->index), ", ",
                                compile_to_type(env, table_type->default_value, table_type->value_type), ", ",
                                compile_type_info(container_t), ")");
            } else {
                return CORD_all("Table$get_optional(",
                                compile_to_pointer_depth(env, indexing->indexed, 0, false), ", ",
                                compile_type(table_type->key_type), ", ",
                                compile_type(table_type->value_type), ", ",
                                compile(env, indexing->index), ", "
                                "_, ", promote_to_optional(table_type->value_type, "(*_)"), ", ",
                                compile_none(table_type->value_type), ", ",
                                compile_type_info(container_t), ")");
            }
        } else if (container_t->tag == TextType) {
            return CORD_all("Text$cluster(", compile_to_pointer_depth(env, indexing->indexed, 0, false), ", ", compile_to_type(env, indexing->index, Type(BigIntType)), ")");
        } else {
            code_err(ast, "Indexing is not supported for type: ", type_to_str(container_t));
        }
    }
    case InlineCCode: {
        type_t *t = get_type(env, ast);
        if (t->tag == VoidType)
            return CORD_all("{\n", compile_statement(env, ast), "\n}");
        else
            return compile_statement(env, ast);
    }
    case Use: code_err(ast, "Compiling 'use' as expression!");
    case Defer: code_err(ast, "Compiling 'defer' as expression!");
    case Extern: code_err(ast, "Externs are not supported as expressions");
    case TableEntry: code_err(ast, "Table entries should not be compiled directly");
    case Declare: case Assign: case UPDATE_CASES: case For: case While: case Repeat: case StructDef: case LangDef: case Extend:
    case EnumDef: case FunctionDef: case ConvertDef: case Skip: case Stop: case Pass: case Return: case DocTest:
        code_err(ast, "This is not a valid expression");
    default: case Unknown: code_err(ast, "Unknown AST: ", ast_to_xml_str(ast));
    }
}

CORD compile_type_info(type_t *t)
{
    if (t == NULL) compiler_err(NULL, NULL, NULL, "Attempt to compile a NULL type");
    if (t == PATH_TYPE) return "&Path$info";
    else if (t == PATH_TYPE_TYPE) return "&PathType$info";

    switch (t->tag) {
    case BoolType: case ByteType: case IntType: case BigIntType: case NumType: case CStringType:
        return CORD_all("&", type_to_cord(t), "$info");
    case TextType: {
        DeclareMatch(text, t, TextType);
        if (!text->lang || streq(text->lang, "Text"))
            return "&Text$info";
        return CORD_all("(&", namespace_prefix(text->env, text->env->namespace->parent), text->lang, "$$info)");
    }
    case StructType: {
        DeclareMatch(s, t, StructType);
        return CORD_all("(&", namespace_prefix(s->env, s->env->namespace->parent), s->name, "$$info)");
    }
    case EnumType: {
        DeclareMatch(e, t, EnumType);
        return CORD_all("(&", namespace_prefix(e->env, e->env->namespace->parent), e->name, "$$info)");
    }
    case ListType: {
        type_t *item_t = Match(t, ListType)->item_type;
        return CORD_all("List$info(", compile_type_info(item_t), ")");
    }
    case SetType: {
        type_t *item_type = Match(t, SetType)->item_type;
        return CORD_all("Set$info(", compile_type_info(item_type), ")");
    }
    case TableType: {
        DeclareMatch(table, t, TableType);
        type_t *key_type = table->key_type;
        type_t *value_type = table->value_type;
        return CORD_all("Table$info(", compile_type_info(key_type), ", ", compile_type_info(value_type), ")");
    }
    case PointerType: {
        DeclareMatch(ptr, t, PointerType);
        CORD sigil = ptr->is_stack ? "&" : "@";
        return CORD_asprintf("Pointer$info(%r, %r)",
                             CORD_quoted(sigil),
                             compile_type_info(ptr->pointed));
    }
    case FunctionType: {
        return CORD_asprintf("Function$info(%r)", CORD_quoted(type_to_cord(t)));
    }
    case ClosureType: {
        return CORD_asprintf("Closure$info(%r)", CORD_quoted(type_to_cord(t)));
    }
    case OptionalType: {
        type_t *non_optional = Match(t, OptionalType)->type;
        return CORD_asprintf("Optional$info(sizeof(%r), __alignof__(%r), %r)", compile_type(non_optional), compile_type(non_optional), compile_type_info(non_optional));
    }
    case TypeInfoType: return CORD_all("Type$info(", CORD_quoted(type_to_cord(Match(t, TypeInfoType)->type)), ")");
    case MemoryType: return "&Memory$info";
    case VoidType: return "&Void$info";
    default:
        compiler_err(NULL, 0, 0, "I couldn't convert to a type info: ", type_to_str(t));
    }
}

static CORD get_flag_options(type_t *t, CORD separator)
{
    if (t->tag == BoolType) {
        return "yes|no";
    } else if (t->tag == EnumType) {
        CORD options = CORD_EMPTY;
        for (tag_t *tag = Match(t, EnumType)->tags; tag; tag = tag->next) {
            options = CORD_all(options, tag->name);
            if (tag->next) options = CORD_all(options, separator);
        }
        return options;
    } else if (t->tag == IntType || t->tag == NumType || t->tag == BigIntType) {
        return "N";
    } else {
        return "...";
    }
}

CORD compile_cli_arg_call(env_t *env, CORD fn_name, type_t *fn_type)
{
    DeclareMatch(fn_info, fn_type, FunctionType);

    env_t *main_env = fresh_scope(env);

    CORD code = CORD_EMPTY;
    binding_t *usage_binding = get_binding(env, "_USAGE");
    CORD usage_code = usage_binding ? usage_binding->code : "usage";
    binding_t *help_binding = get_binding(env, "_HELP");
    CORD help_code = help_binding ? help_binding->code : usage_code;
    if (!usage_binding) {
        bool explicit_help_flag = false;
        for (arg_t *arg = fn_info->args; arg; arg = arg->next) {
            if (streq(arg->name, "help")) {
                explicit_help_flag = true;
                break;
            }
        }

        CORD usage = explicit_help_flag ? CORD_EMPTY : " [--help]";
        for (arg_t *arg = fn_info->args; arg; arg = arg->next) {
            usage = CORD_cat(usage, " ");
            type_t *t = get_arg_type(main_env, arg);
            CORD flag = CORD_replace(arg->name, "_", "-");
            if (arg->default_val || arg->type->tag == OptionalType) {
                if (strlen(arg->name) == 1) {
                    if (t->tag == BoolType || (t->tag == OptionalType && Match(t, OptionalType)->type->tag == BoolType))
                        usage = CORD_all(usage, "[-", flag, "]");
                    else
                        usage = CORD_all(usage, "[-", flag, " ", get_flag_options(t, "|"), "]");
                } else {
                    if (t->tag == BoolType || (t->tag == OptionalType && Match(t, OptionalType)->type->tag == BoolType))
                        usage = CORD_all(usage, "[--", flag, "]");
                    else if (t->tag == ListType)
                        usage = CORD_all(usage, "[--", flag, " ", get_flag_options(t, "|"), "]");
                    else
                        usage = CORD_all(usage, "[--", flag, "=", get_flag_options(t, "|"), "]");
                }
            } else {
                if (t->tag == BoolType)
                    usage = CORD_all(usage, "<--", flag, "|--no-", flag, ">");
                else if (t->tag == EnumType)
                    usage = CORD_all(usage, get_flag_options(t, "|"));
                else if (t->tag == ListType)
                    usage = CORD_all(usage, "[", flag, "...]");
                else
                    usage = CORD_all(usage, "<", flag, ">");
            }
        }
        code = CORD_all(code, "Text_t usage = Texts(Text(\"Usage: \"), Text$from_str(argv[0])",
                        usage == CORD_EMPTY ? CORD_EMPTY : CORD_all(", Text(", CORD_quoted(usage), ")"), ");\n");
    }


    for (arg_t *arg = fn_info->args; arg; arg = arg->next) {
        type_t *opt_type = arg->type->tag == OptionalType ? arg->type : Type(OptionalType, .type=arg->type);
        code = CORD_all(code, compile_declaration(opt_type, CORD_all("_$", arg->name)));
        if (arg->default_val) {
            CORD default_val = compile(env, arg->default_val);
            if (arg->type->tag != OptionalType)
                default_val = promote_to_optional(arg->type, default_val);
            code = CORD_all(code, " = ", default_val);
        } else {
            code = CORD_all(code, " = ", compile_none(arg->type));
        }
        code = CORD_all(code, ";\n");
    }

    code = CORD_all(code, "tomo_parse_args(argc, argv, ", usage_code, ", ", help_code);
    for (arg_t *arg = fn_info->args; arg; arg = arg->next) {
        code = CORD_all(code, ",\n{", CORD_quoted(CORD_replace(arg->name, "_", "-")), ", ",
                        (arg->default_val || arg->type->tag == OptionalType) ? "false" : "true", ", ",
                        compile_type_info(arg->type),
                        ", &", CORD_all("_$", arg->name), "}");
    }
    code = CORD_all(code, ");\n");

    code = CORD_all(code, fn_name, "(");
    for (arg_t *arg = fn_info->args; arg; arg = arg->next) {
        CORD arg_code = CORD_all("_$", arg->name);
        if (arg->type->tag != OptionalType)
            arg_code = optional_into_nonnone(arg->type, arg_code);

        code = CORD_all(code, arg_code);
        if (arg->next) code = CORD_all(code, ", ");
    }
    code = CORD_all(code, ");\n");
    return code;
}

CORD compile_function(env_t *env, CORD name_code, ast_t *ast, CORD *staticdefs)
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

    CORD arg_signature = "(";
    Table_t used_names = {};
    for (arg_ast_t *arg = args; arg; arg = arg->next) {
        type_t *arg_type = get_arg_ast_type(env, arg);
        arg_signature = CORD_cat(arg_signature, compile_declaration(arg_type, CORD_cat("_$", arg->name)));
        if (arg->next) arg_signature = CORD_cat(arg_signature, ", ");
        if (Table$str_get(used_names, arg->name))
            code_err(ast, "The argument name '", arg->name, "' is used more than once");
        Table$str_set(&used_names, arg->name, arg->name);
    }
    arg_signature = CORD_cat(arg_signature, ")");

    CORD ret_type_code = compile_type(ret_t);
    if (ret_t->tag == AbortType)
        ret_type_code = CORD_all("_Noreturn ", ret_type_code);

    if (is_private)
        *staticdefs = CORD_all(*staticdefs, "static ", ret_type_code, " ", name_code, arg_signature, ";\n");

    CORD code;
    if (cache) {
        code = CORD_all("static ", ret_type_code, " ", name_code, "$uncached", arg_signature);
    } else {
        code = CORD_all(ret_type_code, " ", name_code, arg_signature);
        if (is_inline)
            code = CORD_cat("INLINE ", code);
        if (!is_private)
            code = CORD_cat("public ", code);
    }

    env_t *body_scope = fresh_scope(env);
    while (body_scope->namespace && body_scope->namespace->parent) {
        body_scope->locals->fallback = body_scope->locals->fallback->fallback;
        body_scope->namespace = body_scope->namespace->parent;
    }

    body_scope->deferred = NULL;
    body_scope->namespace = NULL;
    for (arg_ast_t *arg = args; arg; arg = arg->next) {
        type_t *arg_type = get_arg_ast_type(env, arg);
        set_binding(body_scope, arg->name, arg_type, CORD_cat("_$", arg->name));
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
            code_err(ast, "This function can reach the end without returning a ", type_to_str(ret_t), " value!");
    }

    CORD body_code = CORD_all("{\n", compile_inline_block(body_scope, body), "}\n");
    CORD definition = with_source_info(env, ast, CORD_all(code, " ", body_code, "\n"));

    if (cache && args == NULL) { // no-args cache just uses a static var
        CORD wrapper = CORD_all(
            is_private ? CORD_EMPTY : "public ", ret_type_code, " ", name_code, "(void) {\n"
            "static ", compile_declaration(ret_t, "cached_result"), ";\n",
            "static bool initialized = false;\n",
            "if (!initialized) {\n"
            "\tcached_result = ", name_code, "$uncached();\n",
            "\tinitialized = true;\n",
            "}\n",
            "return cached_result;\n"
            "}\n");
        definition = CORD_cat(definition, wrapper);
    } else if (cache && cache->tag == Int) {
        assert(args);
        OptionalInt64_t cache_size = Int64$parse(Text$from_str(Match(cache, Int)->str));
        CORD pop_code = CORD_EMPTY;
        if (cache->tag == Int && !cache_size.is_none && cache_size.value > 0) {
            // FIXME: this currently just deletes the first entry, but this should be more like a
            // least-recently-used cache eviction policy or least-frequently-used
            pop_code = CORD_all("if (cache.entries.length > ", CORD_asprintf("%ld", cache_size.value),
                                ") Table$remove(&cache, cache.entries.data + cache.entries.stride*0, table_type);\n");
        }

        if (!args->next) {
            // Single-argument functions have simplified caching logic
            type_t *arg_type = get_arg_ast_type(env, args);
            CORD wrapper = CORD_all(
                is_private ? CORD_EMPTY : "public ", ret_type_code, " ", name_code, arg_signature, "{\n"
                "static Table_t cache = {};\n",
                "const TypeInfo_t *table_type = Table$info(", compile_type_info(arg_type), ", ", compile_type_info(ret_t), ");\n",
                compile_declaration(Type(PointerType, .pointed=ret_t), "cached"), " = Table$get_raw(cache, &_$", args->name, ", table_type);\n"
                "if (cached) return *cached;\n",
                compile_declaration(ret_t, "ret"), " = ", name_code, "$uncached(_$", args->name, ");\n",
                pop_code,
                "Table$set(&cache, &_$", args->name, ", &ret, table_type);\n"
                "return ret;\n"
                "}\n");
            definition = CORD_cat(definition, wrapper);
        } else {
            // Multi-argument functions use a custom struct type (only defined internally) as a cache key:
            arg_t *fields = NULL;
            for (arg_ast_t *arg = args; arg; arg = arg->next)
                fields = new(arg_t, .name=arg->name, .type=get_arg_ast_type(env, arg), .next=fields);
            REVERSE_LIST(fields);
            type_t *t = Type(StructType, .name=String("func$", get_line_number(ast->file, ast->start), "$args"), .fields=fields, .env=env);

            int64_t num_fields = used_names.entries.length;
            const char *metamethods = is_packed_data(t) ? "PackedData$metamethods" : "Struct$metamethods";
            CORD args_typeinfo = CORD_asprintf("((TypeInfo_t[1]){{.size=sizeof(args), .align=__alignof__(args), .metamethods=%s, "
                                               ".tag=StructInfo, .StructInfo.name=\"FunctionArguments\", "
                                               ".StructInfo.num_fields=%ld, .StructInfo.fields=(NamedType_t[%ld]){",
                                               metamethods, num_fields, num_fields);
            CORD args_type = "struct { ";
            for (arg_t *f = fields; f; f = f->next) {
                args_typeinfo = CORD_all(args_typeinfo, "{\"", f->name, "\", ", compile_type_info(f->type), "}");
                args_type = CORD_all(args_type, compile_declaration(f->type, f->name), "; ");
                if (f->next) args_typeinfo = CORD_all(args_typeinfo, ", ");
            }
            args_type = CORD_all(args_type, "}");
            args_typeinfo = CORD_all(args_typeinfo, "}}})");

            CORD all_args = CORD_EMPTY;
            for (arg_ast_t *arg = args; arg; arg = arg->next)
                all_args = CORD_all(all_args, "_$", arg->name, arg->next ? ", " : CORD_EMPTY);

            CORD wrapper = CORD_all(
                is_private ? CORD_EMPTY : "public ", ret_type_code, " ", name_code, arg_signature, "{\n"
                "static Table_t cache = {};\n",
                args_type, " args = {", all_args, "};\n"
                "const TypeInfo_t *table_type = Table$info(", args_typeinfo, ", ", compile_type_info(ret_t), ");\n",
                compile_declaration(Type(PointerType, .pointed=ret_t), "cached"), " = Table$get_raw(cache, &args, table_type);\n"
                "if (cached) return *cached;\n",
                compile_declaration(ret_t, "ret"), " = ", name_code, "$uncached(", all_args, ");\n",
                pop_code,
                "Table$set(&cache, &args, &ret, table_type);\n"
                "return ret;\n"
                "}\n");
            definition = CORD_cat(definition, wrapper);
        }
    }

    CORD qualified_name = function_name;
    if (env->namespace && env->namespace->parent && env->namespace->name)
        qualified_name = CORD_all(env->namespace->name, ".", qualified_name);
    CORD text = CORD_all("func ", qualified_name, "(");
    for (arg_ast_t *arg = args; arg; arg = arg->next) {
        text = CORD_cat(text, type_to_cord(get_arg_ast_type(env, arg)));
        if (arg->next) text = CORD_cat(text, ", ");
    }
    if (ret_t && ret_t->tag != VoidType)
        text = CORD_all(text, "->", type_to_cord(ret_t));
    text = CORD_all(text, ")");

    if (!is_inline) {
        env->code->function_naming = CORD_all(
            env->code->function_naming,
            CORD_asprintf("register_function(%r, Text(\"%s.tm\"), %ld, Text(%r));\n",
                          name_code, file_base_name(ast->file->filename), get_line_number(ast->file, ast->start), CORD_quoted(text)));
    }
    return definition;
}

CORD compile_top_level_code(env_t *env, ast_t *ast)
{
    if (!ast) return CORD_EMPTY;

    switch (ast->tag) {
    case Use: {
        DeclareMatch(use, ast, Use);
        if (use->what == USE_C_CODE) {
            Path_t path = Path$relative_to(Path$from_str(use->path), Path(".build"));
            return CORD_all("#include \"", Path$as_c_string(path), "\"\n");
        }
        return CORD_EMPTY;
    }
    case Declare: {
        DeclareMatch(decl, ast, Declare);
        const char *decl_name = Match(decl->var, Var)->name;
        CORD full_name = CORD_all(namespace_prefix(env, env->namespace), decl_name);
        type_t *t = decl->type ? parse_type_ast(env, decl->type) : get_type(env, decl->value);
        if (t->tag == FunctionType) t = Type(ClosureType, t);
        CORD val_code = compile_declared_value(env, ast);
        bool is_private = decl_name[0] == '_';
        if ((decl->value && is_constant(env, decl->value)) || (!decl->value && !has_heap_memory(t))) {
            set_binding(env, decl_name, t, full_name);
            return CORD_all(
                is_private ? "static " : CORD_EMPTY,
                compile_declaration(t, full_name), " = ", val_code, ";\n");
        } else {
            CORD checked_access = CORD_all("check_initialized(", full_name, ", \"", decl_name, "\")");
            set_binding(env, decl_name, t, checked_access);

            return CORD_all(
                "static bool ", full_name, "$initialized = false;\n",
                is_private ? "static " : CORD_EMPTY,
                compile_declaration(t, full_name), ";\n");
        }
    }
    case FunctionDef: {
        CORD name_code = CORD_all(namespace_prefix(env, env->namespace), Match(Match(ast, FunctionDef)->name, Var)->name);
        return compile_function(env, name_code, ast, &env->code->staticdefs);
    }
    case ConvertDef: {
        type_t *type = get_function_def_type(env, ast);
        const char *name = get_type_name(Match(type, FunctionType)->ret);
        if (!name)
            code_err(ast, "Conversions are only supported for text, struct, and enum types, not ", type_to_str(Match(type, FunctionType)->ret));
        CORD name_code = CORD_asprintf("%r%s$%ld", namespace_prefix(env, env->namespace), name, get_line_number(ast->file, ast->start));
        return compile_function(env, name_code, ast, &env->code->staticdefs);
    }
    case StructDef: {
        DeclareMatch(def, ast, StructDef);
        type_t *t = Table$str_get(*env->types, def->name);
        assert(t && t->tag == StructType);
        CORD code = compile_struct_typeinfo(env, t, def->name, def->fields, def->secret, def->opaque);
        env_t *ns_env = namespace_env(env, def->name);
        return CORD_all(code, def->namespace ? compile_top_level_code(ns_env, def->namespace) : CORD_EMPTY);
    }
    case EnumDef: {
        DeclareMatch(def, ast, EnumDef);
        CORD code = compile_enum_typeinfo(env, ast);
        code = CORD_all(code, compile_enum_constructors(env, ast));
        env_t *ns_env = namespace_env(env, def->name);
        return CORD_all(code, def->namespace ? compile_top_level_code(ns_env, def->namespace) : CORD_EMPTY);
    }
    case LangDef: {
        DeclareMatch(def, ast, LangDef);
        CORD code = CORD_asprintf("public const TypeInfo_t %r%s$$info = {%zu, %zu, .metamethods=Text$metamethods, .tag=TextInfo, .TextInfo={%r}};\n",
                                  namespace_prefix(env, env->namespace), def->name, sizeof(Text_t), __alignof__(Text_t),
                                  CORD_quoted(def->name));
        env_t *ns_env = namespace_env(env, def->name);
        return CORD_all(code, def->namespace ? compile_top_level_code(ns_env, def->namespace) : CORD_EMPTY);
    }
    case Extend: {
        DeclareMatch(extend, ast, Extend);
        env_t *ns_env = namespace_env(env, extend->name);
        env_t *extended = new(env_t);
        *extended = *ns_env;
        extended->locals = new(Table_t, .fallback=env->locals);
        extended->namespace_bindings = new(Table_t, .fallback=env->namespace_bindings);
        extended->libname = env->libname;
        return compile_top_level_code(extended, extend->body);
    }
    case Extern: return CORD_EMPTY;
    case Block: {
        CORD code = CORD_EMPTY;
        for (ast_list_t *stmt = Match(ast, Block)->statements; stmt; stmt = stmt->next) {
            code = CORD_all(code, compile_top_level_code(env, stmt->ast));
        }
        return code;
    }
    default: return CORD_EMPTY;
    }
}

static void initialize_vars_and_statics(env_t *env, ast_t *ast)
{
    if (!ast) return;

    for (ast_list_t *stmt = Match(ast, Block)->statements; stmt; stmt = stmt->next) {
        if (stmt->ast->tag == InlineCCode) {
            CORD code = compile_statement(env, stmt->ast);
            env->code->staticdefs = CORD_all(env->code->staticdefs, code, "\n");
        } else if (stmt->ast->tag == Declare) {
            DeclareMatch(decl, stmt->ast, Declare);
            const char *decl_name = Match(decl->var, Var)->name;
            CORD full_name = CORD_all(namespace_prefix(env, env->namespace), decl_name);
            type_t *t = decl->type ? parse_type_ast(env, decl->type) : get_type(env, decl->value);
            if (t->tag == FunctionType) t = Type(ClosureType, t);
            CORD val_code = compile_declared_value(env, stmt->ast);
            if ((decl->value && !is_constant(env, decl->value)) || (!decl->value && has_heap_memory(t))) {
                env->code->variable_initializers = CORD_all(
                    env->code->variable_initializers,
                    with_source_info(
                        env, stmt->ast,
                        CORD_all(
                            full_name, " = ", val_code, ",\n",
                            full_name, "$initialized = true;\n")));
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
            CORD code = compile_statement(env, stmt->ast);
            if (code) code_err(stmt->ast, "I did not expect this to generate code");
            assert(!code);
        }
    }
}

CORD compile_file(env_t *env, ast_t *ast)
{
    CORD top_level_code = compile_top_level_code(env, ast);
    CORD use_imports = CORD_EMPTY;

    // First prepare variable initializers to prevent unitialized access:
    for (ast_list_t *stmt = Match(ast, Block)->statements; stmt; stmt = stmt->next) {
        if (stmt->ast->tag == Use)
            use_imports = CORD_all(use_imports, compile_statement(env, stmt->ast));
    }

    initialize_vars_and_statics(env, ast);

    const char *name = file_base_name(ast->file->filename);
    return CORD_all(
        env->do_source_mapping ? CORD_all("#line 1 ", CORD_quoted(ast->file->filename), "\n") : CORD_EMPTY,
        "#define __SOURCE_FILE__ ", CORD_quoted(ast->file->filename), "\n",
        "#include <tomo/tomo.h>\n"
        "#include \"", name, ".tm.h\"\n\n",
        env->code->local_typedefs, "\n",
        env->code->lambdas, "\n",
        env->code->staticdefs, "\n",
        top_level_code,
        "public void _$", env->namespace->name, "$$initialize(void) {\n",
        "static bool initialized = false;\n",
        "if (initialized) return;\n",
        "initialized = true;\n",
        use_imports,
        env->code->variable_initializers,
        env->code->function_naming,
        "}\n");
}

CORD compile_statement_type_header(env_t *env, Path_t header_path, ast_t *ast)
{
    switch (ast->tag) {
    case Use: {
        DeclareMatch(use, ast, Use);
        Path_t source_path = Path$from_str(ast->file->filename);
        Path_t source_dir = Path$parent(source_path);
        Path_t build_dir = Path$resolved(Path$parent(header_path), Path$current_dir());
        switch (use->what) {
        case USE_MODULE: {
            return CORD_all("#include <", use->path, "/", use->path, ".h>\n");
        }
        case USE_LOCAL: {
            Path_t used_path = Path$resolved(Path$from_str(use->path), source_dir);
            Path_t used_build_dir = Path$with_component(Path$parent(used_path), Text(".build"));
            Path_t used_header_path = Path$with_component(used_build_dir, Texts(Path$base_name(used_path), Text(".h")));
            return CORD_all("#include \"", Path$as_c_string(Path$relative_to(used_header_path, build_dir)), "\"\n");
        }
        case USE_HEADER:
            if (use->path[0] == '<') {
                return CORD_all("#include ", use->path, "\n");
            } else {
                Path_t used_path = Path$resolved(Path$from_str(use->path), source_dir);
                return CORD_all("#include \"", Path$as_c_string(Path$relative_to(used_path, build_dir)), "\"\n");
            }
        default:
            return CORD_EMPTY;
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
        CORD full_name = CORD_cat(namespace_prefix(env, env->namespace), def->name);
        return CORD_all(
            // Constructor macro:
            "#define ", namespace_prefix(env, env->namespace), def->name,
                "(text) ((", namespace_prefix(env, env->namespace), def->name, "$$type){.length=sizeof(text)-1, .tag=TEXT_ASCII, .ascii=\"\" text})\n"
            "#define ", namespace_prefix(env, env->namespace), def->name,
                "s(...) ((", namespace_prefix(env, env->namespace), def->name, "$$type)Texts(__VA_ARGS__))\n"
            "extern const TypeInfo_t ", full_name, "$$info;\n"
        );
    }
    case Extend: {
        return CORD_EMPTY;
    }
    default:
        return CORD_EMPTY;
    }
}

CORD compile_statement_namespace_header(env_t *env, Path_t header_path, ast_t *ast)
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
        extended->libname = env->libname;
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
        CORD decl;
        if (t->tag == ClosureType) {
            t = Match(t, ClosureType)->fn;
            DeclareMatch(fn, t, FunctionType);
            decl = CORD_all(compile_type(fn->ret), " ", ext->name, "(");
            for (arg_t *arg = fn->args; arg; arg = arg->next) {
                decl = CORD_all(decl, compile_type(arg->type));
                if (arg->next) decl = CORD_cat(decl, ", ");
            }
            decl = CORD_cat(decl, ")");
        } else {
            decl = compile_declaration(t, ext->name);
        }
        return CORD_all("extern ", decl, ";\n");
    }
    case Declare: {
        DeclareMatch(decl, ast, Declare);
        const char *decl_name = Match(decl->var, Var)->name;
        bool is_private = (decl_name[0] == '_');
        if (is_private)
            return CORD_EMPTY;

        type_t *t = decl->type ? parse_type_ast(env, decl->type) : get_type(env, decl->value);
        if (t->tag == FunctionType)
            t = Type(ClosureType, t);
        assert(t->tag != ModuleType);
        if (t->tag == AbortType || t->tag == VoidType || t->tag == ReturnType)
            code_err(ast, "You can't declare a variable with a ", type_to_str(t), " value");

        return CORD_all(
            decl->value ? compile_statement_type_header(env, header_path, decl->value) : CORD_EMPTY,
            "extern ", compile_declaration(t, CORD_cat(namespace_prefix(env, env->namespace), decl_name)), ";\n");
    }
    case FunctionDef: {
        DeclareMatch(fndef, ast, FunctionDef);
        const char *decl_name = Match(fndef->name, Var)->name;
        bool is_private = decl_name[0] == '_';
        if (is_private) return CORD_EMPTY;
        CORD arg_signature = "(";
        for (arg_ast_t *arg = fndef->args; arg; arg = arg->next) {
            type_t *arg_type = get_arg_ast_type(env, arg);
            arg_signature = CORD_cat(arg_signature, compile_declaration(arg_type, CORD_cat("_$", arg->name)));
            if (arg->next) arg_signature = CORD_cat(arg_signature, ", ");
        }
        arg_signature = CORD_cat(arg_signature, ")");

        type_t *ret_t = fndef->ret_type ? parse_type_ast(env, fndef->ret_type) : Type(VoidType);
        CORD ret_type_code = compile_type(ret_t);
        if (ret_t->tag == AbortType)
            ret_type_code = CORD_all("_Noreturn ", ret_type_code);
        CORD name = CORD_all(namespace_prefix(env, env->namespace), decl_name);
        if (env->namespace && env->namespace->parent && env->namespace->name && streq(decl_name, env->namespace->name))
            name = CORD_asprintf("%r%ld", namespace_prefix(env, env->namespace), get_line_number(ast->file, ast->start));
        return CORD_all(ret_type_code, " ", name, arg_signature, ";\n");
    }
    case ConvertDef: {
        DeclareMatch(def, ast, ConvertDef);

        CORD arg_signature = "(";
        for (arg_ast_t *arg = def->args; arg; arg = arg->next) {
            type_t *arg_type = get_arg_ast_type(env, arg);
            arg_signature = CORD_cat(arg_signature, compile_declaration(arg_type, CORD_cat("_$", arg->name)));
            if (arg->next) arg_signature = CORD_cat(arg_signature, ", ");
        }
        arg_signature = CORD_cat(arg_signature, ")");

        type_t *ret_t = def->ret_type ? parse_type_ast(env, def->ret_type) : Type(VoidType);
        CORD ret_type_code = compile_type(ret_t);
        CORD name = get_type_name(ret_t);
        if (!name)
            code_err(ast, "Conversions are only supported for text, struct, and enum types, not ", type_to_str(ret_t));
        name = CORD_all(namespace_prefix(env, env->namespace), name);
        CORD name_code = CORD_asprintf("%r$%ld", name, get_line_number(ast->file, ast->start));
        return CORD_all(ret_type_code, " ", name_code, arg_signature, ";\n");
    }
    default: return CORD_EMPTY;
    }
    assert(ns_env);
    CORD header = CORD_EMPTY;
    for (ast_list_t *stmt = block ? Match(block, Block)->statements : NULL; stmt; stmt = stmt->next) {
        header = CORD_all(header, compile_statement_namespace_header(ns_env, header_path, stmt->ast));
    }
    return header;
}

typedef struct {
    env_t *env;
    CORD *header;
    Path_t header_path;
} compile_typedef_info_t;

static void _make_typedefs(compile_typedef_info_t *info, ast_t *ast)
{
    if (ast->tag == StructDef) {
        DeclareMatch(def, ast, StructDef);
        if (def->external) return;
        CORD full_name = CORD_cat(namespace_prefix(info->env, info->env->namespace), def->name);
        *info->header = CORD_all(*info->header, "typedef struct ", full_name, "$$struct ", full_name, "$$type;\n");
    } else if (ast->tag == EnumDef) {
        DeclareMatch(def, ast, EnumDef);
        CORD full_name = CORD_cat(namespace_prefix(info->env, info->env->namespace), def->name);
        *info->header = CORD_all(*info->header, "typedef struct ", full_name, "$$struct ", full_name, "$$type;\n");

        for (tag_ast_t *tag = def->tags; tag; tag = tag->next) {
            if (!tag->fields) continue;
            *info->header = CORD_all(*info->header, "typedef struct ", full_name, "$", tag->name, "$$struct ", full_name, "$", tag->name, "$$type;\n");
        }
    } else if (ast->tag == LangDef) {
        DeclareMatch(def, ast, LangDef);
        *info->header = CORD_all(*info->header, "typedef Text_t ", namespace_prefix(info->env, info->env->namespace), def->name, "$$type;\n");
    }
}

static void _define_types_and_funcs(compile_typedef_info_t *info, ast_t *ast)
{
    *info->header = CORD_all(*info->header,
                             compile_statement_type_header(info->env, info->header_path, ast),
                             compile_statement_namespace_header(info->env, info->header_path, ast));
}

CORD compile_file_header(env_t *env, Path_t header_path, ast_t *ast)
{
    CORD header = CORD_all(
        "#pragma once\n",
        env->do_source_mapping ? CORD_all("#line 1 ", CORD_quoted(ast->file->filename), "\n") : CORD_EMPTY,
        "#include <tomo/tomo.h>\n");

    compile_typedef_info_t info = {.env=env, .header=&header, .header_path=header_path};
    visit_topologically(Match(ast, Block)->statements, (Closure_t){.fn=(void*)_make_typedefs, &info});
    visit_topologically(Match(ast, Block)->statements, (Closure_t){.fn=(void*)_define_types_and_funcs, &info});

    header = CORD_all(header, "void _$", env->namespace->name, "$$initialize(void);\n");
    return header;
}

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
