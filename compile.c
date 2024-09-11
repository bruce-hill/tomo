// Compilation logic
#include <ctype.h>
#include <gc.h>
#include <gc/cord.h>
#include <gmp.h>
#include <stdio.h>
#include <uninorm.h>

#include "ast.h"
#include "builtins/integers.h"
#include "builtins/text.h"
#include "compile.h"
#include "enums.h"
#include "structs.h"
#include "environment.h"
#include "typecheck.h"
#include "builtins/util.h"

static CORD compile_to_pointer_depth(env_t *env, ast_t *ast, int64_t target_depth, bool needs_incref);
static env_t *with_enum_scope(env_t *env, type_t *t);
static CORD compile_math_method(env_t *env, binop_e op, ast_t *lhs, ast_t *rhs, type_t *required_type);
static CORD compile_string(env_t *env, ast_t *ast, CORD color);
static CORD compile_arguments(env_t *env, ast_t *call_ast, arg_t *spec_args, arg_ast_t *call_args);
static CORD compile_maybe_incref(env_t *env, ast_t *ast);
static CORD compile_int_to_type(env_t *env, ast_t *ast, type_t *target);
static CORD promote_to_optional(type_t *t, CORD code);
static CORD compile_null(type_t *t);

CORD promote_to_optional(type_t *t, CORD code)
{
    if (t == THREAD_TYPE) {
        return code;
    } else if (t->tag == IntType) {
        switch (Match(t, IntType)->bits) {
        case TYPE_IBITS8: return CORD_all("((OptionalInt8_t){.i=", code, "})");
        case TYPE_IBITS16: return CORD_all("((OptionalInt16_t){.i=", code, "})");
        case TYPE_IBITS32: return CORD_all("((OptionalInt32_t){.i=", code, "})");
        case TYPE_IBITS64: return CORD_all("((OptionalInt64_t){.i=", code, "})");
        default: errx(1, "Unsupported in type: %T", t);
        }
    } else if (t->tag == StructType) {
        return CORD_all("((", compile_type(Type(OptionalType, .type=t)), "){.value=", code, "})");
    } else {
        return code;
    }
}
static bool promote(env_t *env, CORD *code, type_t *actual, type_t *needed)
{
    if (type_eq(actual, needed))
        return true;

    if (!can_promote(actual, needed))
        return false;

    if (actual->tag == IntType && needed->tag == BigIntType) {
        *code = CORD_all("I(", *code, ")");
        return true;
    }

    if ((actual->tag == IntType || actual->tag == BigIntType) && needed->tag == NumType) {
        *code = CORD_all(type_to_cord(actual), "_to_", type_to_cord(needed), "(", *code, ")");
        return true;
    }

    if (actual->tag == NumType && needed->tag == IntType)
        return false;

    if (actual->tag == IntType || actual->tag == NumType)
        return true;

    // Text to C String
    if (actual->tag == TextType && !Match(actual, TextType)->lang && needed->tag == CStringType) {
        *code = CORD_all("Text$as_c_string(", *code, ")");
        return true;
    }

    // Automatic dereferencing:
    if (actual->tag == PointerType
        && can_promote(Match(actual, PointerType)->pointed, needed)) {
        *code = CORD_all("*(", *code, ")");
        return promote(env, code, Match(actual, PointerType)->pointed, needed);
    }

    // Optional promotion:
    if (needed->tag == OptionalType && type_eq(actual, Match(needed, OptionalType)->type)) {
        *code = promote_to_optional(actual, *code);
        return true;
    }

    // Stack ref promotion:
    if (actual->tag == PointerType && needed->tag == PointerType)
        return true;

    if (needed->tag == ClosureType && actual->tag == FunctionType) {
        *code = CORD_all("((Closure_t){", *code, ", NULL})");
        return true;
    }

    if (needed->tag == ClosureType && actual->tag == ClosureType)
        return true;

    if (needed->tag == FunctionType && actual->tag == FunctionType) {
        *code = CORD_all("(", compile_type(needed), ")", *code);
        return true;
    }

    // Set -> Array promotion:
    if (needed->tag == ArrayType && actual->tag == SetType
        && type_eq(Match(needed, ArrayType)->item_type, Match(actual, SetType)->item_type)) {
        *code = CORD_all("(", *code, ").entries");
        return true;
    }

    return false;
}

CORD compile_maybe_incref(env_t *env, ast_t *ast)
{
    type_t *t = get_type(env, ast);
    if (is_idempotent(ast) && can_be_mutated(env, ast)) {
        if (t->tag == ArrayType)
            return CORD_all("ARRAY_COPY(", compile(env, ast), ")");
        else if (t->tag == TableType || t->tag == SetType)
            return CORD_all("TABLE_COPY(", compile(env, ast), ")");
    }
    return compile(env, ast);
}


static Table_t *get_closed_vars(env_t *env, ast_t *lambda_ast)
{
    auto lambda = Match(lambda_ast, Lambda);
    env_t *body_scope = fresh_scope(env);
    body_scope->code = new(compilation_unit_t); // Don't put any code in the headers or anything
    for (arg_ast_t *arg = lambda->args; arg; arg = arg->next) {
        type_t *arg_type = get_arg_ast_type(env, arg);
        set_binding(body_scope, arg->name, new(binding_t, .type=arg_type, .code=CORD_cat("$", arg->name)));
    }

    fn_ctx_t fn_ctx = (fn_ctx_t){
        .parent=env->fn_ctx,
        .closure_scope=env->locals,
        .closed_vars=new(Table_t),
    };
    body_scope->fn_ctx = &fn_ctx;
    body_scope->locals->fallback = env->globals;
    type_t *ret_t = get_type(body_scope, lambda->body);
    if (ret_t->tag == ReturnType)
        ret_t = Match(ret_t, ReturnType)->ret;
    fn_ctx.return_type = ret_t;

    // Find which variables are captured in the closure:
    env_t *tmp_scope = fresh_scope(body_scope);
    for (ast_list_t *stmt = Match(lambda->body, Block)->statements; stmt; stmt = stmt->next) {
        bind_statement(tmp_scope, stmt->ast);
        type_t *stmt_type = get_type(tmp_scope, stmt->ast);
        if (stmt->next || (stmt_type->tag == VoidType || stmt_type->tag == AbortType || get_type(tmp_scope, stmt->ast)->tag == ReturnType))
            (void)compile_statement(tmp_scope, stmt->ast);
        else
            (void)compile(tmp_scope, stmt->ast);
    }
    return fn_ctx.closed_vars;
}

CORD compile_declaration(type_t *t, CORD name)
{
    if (t->tag == FunctionType) {
        auto fn = Match(t, FunctionType);
        CORD code = CORD_all(compile_type(fn->ret), " (*", name, ")(");
        for (arg_t *arg = fn->args; arg; arg = arg->next) {
            code = CORD_all(code, compile_type(arg->type));
            if (arg->next) code = CORD_cat(code, ", ");
        }
        return CORD_all(code, ")");
    } else if (t->tag != ModuleType) {
        return CORD_all(compile_type(t), " ", name);
    } else {
        return CORD_EMPTY;
    }
}

CORD compile_type(type_t *t)
{
    switch (t->tag) {
    case ReturnType: errx(1, "Shouldn't be compiling ReturnType to a type");
    case AbortType: return "void";
    case VoidType: return "void";
    case MemoryType: return "void";
    case BoolType: return "Bool_t";
    case CStringType: return "char*";
    case BigIntType: return "Int_t";
    case IntType: return CORD_asprintf("Int%ld_t", Match(t, IntType)->bits);
    case NumType: return Match(t, NumType)->bits == TYPE_NBITS64 ? "Num_t" : CORD_asprintf("Num%ld_t", Match(t, NumType)->bits);
    case TextType: {
        auto text = Match(t, TextType);
        return text->lang ? CORD_all(namespace_prefix(text->env->libname, text->env->namespace->parent), text->lang, "_t") : "Text_t";
    }
    case ArrayType: return "Array_t";
    case SetType: return "Table_t";
    case ChannelType: return "Channel_t*";
    case TableType: return "Table_t";
    case FunctionType: {
        auto fn = Match(t, FunctionType);
        CORD code = CORD_all(compile_type(fn->ret), " (*)(");
        for (arg_t *arg = fn->args; arg; arg = arg->next) {
            code = CORD_all(code, compile_type(arg->type));
            if (arg->next) code = CORD_cat(code, ", ");
        }
        return CORD_all(code, ")");
    }
    case ClosureType: return "Closure_t";
    case PointerType: return CORD_cat(compile_type(Match(t, PointerType)->pointed), "*");
    case StructType: {
        if (t == THREAD_TYPE)
            return "pthread_t*";
        auto s = Match(t, StructType);
        return CORD_all("struct ", namespace_prefix(s->env->libname, s->env->namespace->parent), s->name, "_s");
    }
    case EnumType: {
        auto e = Match(t, EnumType);
        return CORD_all(namespace_prefix(e->env->libname, e->env->namespace->parent), e->name, "_t");
    }
    case OptionalType: {
        type_t *nonnull = Match(t, OptionalType)->type;
        switch (nonnull->tag) {
        case BoolType: return "OptionalBool_t";
        case CStringType: case BigIntType: case NumType: case TextType:
        case ArrayType: case SetType: case TableType: case FunctionType: case ClosureType:
        case PointerType: case EnumType: case ChannelType:
            return compile_type(nonnull);
        case IntType:
            return CORD_all("Optional", compile_type(nonnull));
        case StructType: {
            if (nonnull == THREAD_TYPE)
                return "pthread_t*";
            auto s = Match(nonnull, StructType);
            return CORD_all(namespace_prefix(s->env->libname, s->env->namespace->parent), "$Optional", s->name, "_t");
        }
        default:
            compiler_err(NULL, NULL, NULL, "Optional types are not supported for: %T", t);
        }
    }
    case TypeInfoType: return "TypeInfo";
    default: compiler_err(NULL, NULL, NULL, "Compiling type is not implemented for type with tag %d", t->tag);
    }
}

static CORD compile_lvalue(env_t *env, ast_t *ast)
{
    if (!can_be_mutated(env, ast)) {
        if (ast->tag == Index || ast->tag == FieldAccess) {
            ast_t *subject = ast->tag == Index ? Match(ast, Index)->indexed : Match(ast, FieldAccess)->fielded;
            code_err(subject, "This is an immutable value, you can't assign to it");
        } else {
            code_err(ast, "This is a value of type %T and can't be used as an assignment target", get_type(env, ast));
        }
    }

    if (ast->tag == Index) {
        auto index = Match(ast, Index);
        type_t *container_t = get_type(env, index->indexed);
        if (container_t->tag == OptionalType)
            code_err(index->indexed, "This value might be null, so it can't be safely used as an assignment target");

        if (!index->index && container_t->tag == PointerType)
            return compile(env, ast);

        container_t = value_type(container_t);
        if (container_t->tag == ArrayType) {
            CORD target_code = compile_to_pointer_depth(env, index->indexed, 1, false);
            type_t *item_type = Match(container_t, ArrayType)->item_type;
            if (index->unchecked) {
                return CORD_all("Array_lvalue_unchecked(", compile_type(item_type), ", ", target_code, ", ", 
                                compile_int_to_type(env, index->index, Type(IntType, .bits=TYPE_IBITS64)),
                                ", ", CORD_asprintf("%ld", padded_type_size(item_type)), ")");
            } else {
                return CORD_all("Array_lvalue(", compile_type(item_type), ", ", target_code, ", ", 
                                compile_int_to_type(env, index->index, Type(IntType, .bits=TYPE_IBITS64)),
                                ", ", CORD_asprintf("%ld", padded_type_size(item_type)),
                                ", ", heap_strf("%ld", ast->start - ast->file->text),
                                ", ", heap_strf("%ld", ast->end - ast->file->text), ")");
            }
        } else {
            code_err(ast, "I don't know how to assign to this target");
        }
    } else if (ast->tag == Var || ast->tag == FieldAccess) {
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
        bind_statement(env, stmt->ast);
        code = CORD_all(code, compile_statement(env, stmt->ast), "\n");
    }
    for (deferral_t *deferred = env->deferred; deferred && deferred != prev_deferred; deferred = deferred->next) {
        code = CORD_all(code, compile_statement(deferred->defer_env, deferred->block));
    }
    return code;
}

static CORD optional_var_into_nonnull(binding_t *b)
{
    switch (b->type->tag) {
    case IntType:
        return CORD_all(b->code, ".i");
    case StructType:
        return CORD_all(b->code, ".value");
    default:
        return b->code;
    }
}

static CORD compile_optional_check(env_t *env, ast_t *ast)
{
    type_t *t = get_type(env, ast);
    t = Match(t, OptionalType)->type;
    if (t->tag == PointerType || t->tag == FunctionType || t->tag == CStringType
        || t->tag == ChannelType || t == THREAD_TYPE)
        return CORD_all("(", compile(env, ast), " != NULL)");
    else if (t->tag == BigIntType)
        return CORD_all("((", compile(env, ast), ").small != 0)");
    else if (t->tag == ClosureType)
        return CORD_all("((", compile(env, ast), ").fn != NULL)");
    else if (t->tag == NumType)
        return CORD_all("!isnan(", compile(env, ast), ")");
    else if (t->tag == ArrayType)
        return CORD_all("((", compile(env, ast), ").length >= 0)");
    else if (t->tag == TableType || t->tag == SetType)
        return CORD_all("((", compile(env, ast), ").entries.length >= 0)");
    else if (t->tag == BoolType)
        return CORD_all("((", compile(env, ast), ") != NULL_BOOL)");
    else if (t->tag == TextType)
        return CORD_all("((", compile(env, ast), ").length >= 0)");
    else if (t->tag == IntType)
        return CORD_all("!(", compile(env, ast), ").is_null");
    else if (t->tag == StructType)
        return CORD_all("!(", compile(env, ast), ").is_null");
    else if (t->tag == EnumType)
        return CORD_all("((", compile(env, ast), ").tag != 0)");
    errx(1, "Optional check not implemented for: %T", t);
}

CORD compile_statement(env_t *env, ast_t *ast)
{
    switch (ast->tag) {
    case When: {
        // Typecheck to verify exhaustiveness:
        type_t *result_t = get_type(env, ast);
        (void)result_t;

        auto when = Match(ast, When);
        type_t *subject_t = get_type(env, when->subject);

        auto enum_t = Match(subject_t, EnumType);
        CORD code = CORD_all("{ ", compile_type(subject_t), " subject = ", compile(env, when->subject), ";\n"
                             "switch (subject.tag) {");
        for (when_clause_t *clause = when->clauses; clause; clause = clause->next) {
            const char *clause_tag_name = Match(clause->tag_name, Var)->name;
            code = CORD_all(code, "case ", namespace_prefix(enum_t->env->libname, enum_t->env->namespace), "tag$", clause_tag_name, ": {\n");
            type_t *tag_type = NULL;
            for (tag_t *tag = enum_t->tags; tag; tag = tag->next) {
                if (streq(tag->name, clause_tag_name)) {
                    tag_type = tag->type;
                    break;
                }
            }
            assert(tag_type);
            env_t *scope = env;

            auto tag_struct = Match(tag_type, StructType);
            if (clause->args && !clause->args->next && tag_struct->fields && tag_struct->fields->next) {
                code = CORD_all(code, compile_type(tag_type), " ", compile(env, clause->args->ast), " = subject.$", clause_tag_name, ";\n");
                scope = fresh_scope(scope);
                set_binding(scope, Match(clause->args->ast, Var)->name, new(binding_t, .type=tag_type));
            } else if (clause->args) {
                scope = fresh_scope(scope);
                ast_list_t *var = clause->args;
                arg_t *field = tag_struct->fields;
                while (var || field) {
                    if (!var)
                        code_err(clause->tag_name, "The field %T.%s.%s wasn't accounted for", subject_t, clause_tag_name, field->name);
                    if (!field)
                        code_err(var->ast, "This is one more field than %T has", subject_t);
                    code = CORD_all(code, compile_type(field->type), " ", compile(env, var->ast), " = subject.$", clause_tag_name, ".$", field->name, ";\n");
                    set_binding(scope, Match(var->ast, Var)->name, new(binding_t, .type=field->type));
                    var = var->next;
                    field = field->next;
                }
            }
            code = CORD_all(code, compile_statement(scope, clause->body), "\nbreak;\n}\n");
        }
        if (when->else_body) {
            code = CORD_all(code, "default: {\n", compile_statement(env, when->else_body), "\nbreak;\n}");
        } else {
            code = CORD_all(code, "default: errx(1, \"Invalid tag!\");\n");
        }
        code = CORD_all(code, "\n}\n}");
        return code;
    }
    case DocTest: {
        auto test = Match(ast, DocTest);
        type_t *expr_t = get_type(env, test->expr);
        if (!expr_t)
            code_err(test->expr, "I couldn't figure out the type of this expression");

        CORD output = CORD_EMPTY;
        if (test->output) {
            const uint8_t *raw = (const uint8_t*)CORD_to_const_char_star(test->output);
            uint8_t buf[128] = {0};
            size_t norm_len = sizeof(buf);
            uint8_t *norm = u8_normalize(UNINORM_NFD, (uint8_t*)raw, strlen((char*)raw)+1, buf, &norm_len);
            assert(norm[norm_len-1] == 0);
            output = CORD_from_char_star((char*)norm);
            if (norm && norm != buf) free(norm);
        }

        if (test->expr->tag == Declare) {
            auto decl = Match(test->expr, Declare);
            CORD var = CORD_all("$", Match(decl->var, Var)->name);
            type_t *t = get_type(env, decl->value);
            CORD val_code = compile_maybe_incref(env, decl->value);
            if (t->tag == FunctionType) {
                assert(promote(env, &val_code, t, Type(ClosureType, t)));
                t = Type(ClosureType, t);
            }
            return CORD_asprintf(
                "%r;\n"
                "test((%r = %r), %r, %r, %ld, %ld);\n",
                compile_declaration(t, var),
                var, val_code,
                compile_type_info(env, get_type(env, decl->value)),
                CORD_quoted(output),
                (int64_t)(test->expr->start - test->expr->file->text),
                (int64_t)(test->expr->end - test->expr->file->text));
        } else if (test->expr->tag == Assign) {
            auto assign = Match(test->expr, Assign);
            if (!assign->targets->next && assign->targets->ast->tag == Var && is_idempotent(assign->targets->ast)) {
                // Common case: assigning to one variable:
                type_t *lhs_t = get_type(env, assign->targets->ast);
                if (lhs_t->tag == PointerType && Match(lhs_t, PointerType)->is_stack)
                    code_err(test->expr, "Stack references cannot be assigned to local variables because the variable may outlive the stack memory.");
                env_t *val_scope = with_enum_scope(env, lhs_t);
                type_t *rhs_t = get_type(val_scope, assign->values->ast);
                CORD value;
                if (lhs_t->tag == IntType && assign->values->ast->tag == Int) {
                    value = compile_int_to_type(val_scope, assign->values->ast, lhs_t);
                } else {
                    value = compile_maybe_incref(val_scope, assign->values->ast);
                    if (!promote(val_scope, &value, rhs_t, lhs_t))
                        code_err(assign->values->ast, "You cannot assign a %T value to a %T operand", rhs_t, lhs_t);
                }
                return CORD_asprintf(
                    "test((%r), %r, %r, %ld, %ld);",
                    compile_assignment(env, assign->targets->ast, value),
                    compile_type_info(env, lhs_t),
                    CORD_quoted(test->output),
                    (int64_t)(test->expr->start - test->expr->file->text),
                    (int64_t)(test->expr->end - test->expr->file->text));
            } else {
                // Multi-assign or assignment to potentially non-idempotent targets
                if (test->output && assign->targets->next)
                    code_err(ast, "Sorry, but doctesting with '=' is not supported for multi-assignments");

                CORD code = "test(({ // Assignment\n";

                int64_t i = 1;
                for (ast_list_t *target = assign->targets, *value = assign->values; target && value; target = target->next, value = value->next) {
                    type_t *target_type = get_type(env, target->ast);
                    if (target_type->tag == PointerType && Match(target_type, PointerType)->is_stack)
                        code_err(ast, "Stack references cannot be assigned to local variables because the variable may outlive the stack memory.");
                    env_t *val_scope = with_enum_scope(env, target_type);
                    type_t *value_type = get_type(val_scope, value->ast);
                    CORD val_code;
                    if (target_type->tag == IntType && value->ast->tag == Int) {
                        val_code = compile_int_to_type(val_scope, value->ast, target_type);
                    } else {
                        val_code = compile_maybe_incref(val_scope, value->ast);
                        if (!promote(val_scope, &val_code, value_type, target_type))
                            code_err(value->ast, "You cannot assign a %T value to a %T operand", value_type, target_type);
                    }
                    CORD_appendf(&code, "%r $%ld = %r;\n", compile_type(target_type), i++, val_code);
                }
                i = 1;
                for (ast_list_t *target = assign->targets; target; target = target->next)
                    code = CORD_all(code, compile_assignment(env, target->ast, CORD_asprintf("$%ld", i++)), ";\n");

                CORD_appendf(&code, "$1; }), %r, %r, %ld, %ld);",
                    compile_type_info(env, get_type(env, assign->targets->ast)),
                    CORD_quoted(test->output),
                    (int64_t)(test->expr->start - test->expr->file->text),
                    (int64_t)(test->expr->end - test->expr->file->text));
                return code;
            }
        } else if (test->expr->tag == UpdateAssign) {
            type_t *lhs_t = get_type(env, Match(test->expr, UpdateAssign)->lhs);
            return CORD_asprintf(
                "test(({%r %r;}), %r, %r, %ld, %ld);",
                compile_statement(env, test->expr),
                compile_lvalue(env, Match(test->expr, UpdateAssign)->lhs),
                compile_type_info(env, lhs_t),
                CORD_quoted(test->output),
                (int64_t)(test->expr->start - test->expr->file->text),
                (int64_t)(test->expr->end - test->expr->file->text));
        } else if (expr_t->tag == VoidType || expr_t->tag == AbortType || expr_t->tag == ReturnType) {
            return CORD_asprintf(
                "test(({%r NULL;}), NULL, NULL, %ld, %ld);",
                compile_statement(env, test->expr),
                (int64_t)(test->expr->start - test->expr->file->text),
                (int64_t)(test->expr->end - test->expr->file->text));
        } else {
            return CORD_asprintf(
                "test(%r, %r, %r, %ld, %ld);",
                compile(env, test->expr),
                compile_type_info(env, expr_t),
                CORD_quoted(output),
                (int64_t)(test->expr->start - test->expr->file->text),
                (int64_t)(test->expr->end - test->expr->file->text));
        }
    }
    case Declare: {
        auto decl = Match(ast, Declare);
        if (decl->value->tag == Use) {
            return compile_statement(env, decl->value);
        } else {
            type_t *t = get_type(env, decl->value);
            if (t->tag == AbortType || t->tag == VoidType || t->tag == ReturnType)
                code_err(ast, "You can't declare a variable with a %T value", t);

            CORD val_code = compile_maybe_incref(env, decl->value);
            if (t->tag == FunctionType) {
                assert(promote(env, &val_code, t, Type(ClosureType, t)));
                t = Type(ClosureType, t);
            }

            return CORD_all(compile_declaration(t, CORD_cat("$", Match(decl->var, Var)->name)), " = ", val_code, ";");
        }
    }
    case Assign: {
        auto assign = Match(ast, Assign);
        // Single assignment, no temp vars needed:
        if (assign->targets && !assign->targets->next && is_idempotent(assign->targets->ast)) {
            type_t *lhs_t = get_type(env, assign->targets->ast);
            if (lhs_t->tag == PointerType && Match(lhs_t, PointerType)->is_stack)
                code_err(ast, "Stack references cannot be assigned to local variables because the variable may outlive the stack memory.");
            env_t *val_env = with_enum_scope(env, lhs_t);
            type_t *rhs_t = get_type(val_env, assign->values->ast);
            CORD val;
            if (rhs_t->tag == IntType && assign->values->ast->tag == Int) {
                val = compile_int_to_type(val_env, assign->values->ast, rhs_t);
            } else {
                val = compile_maybe_incref(val_env, assign->values->ast);
                if (!promote(val_env, &val, rhs_t, lhs_t))
                    code_err(assign->values->ast, "You cannot assign a %T value to a %T operand", lhs_t, rhs_t);
            }
            return CORD_all(compile_assignment(env, assign->targets->ast, val), ";\n");
        }

        CORD code = "{ // Assignment\n";
        int64_t i = 1;
        for (ast_list_t *value = assign->values, *target = assign->targets; value && target; value = value->next, target = target->next) {
            type_t *lhs_t = get_type(env, target->ast);
            if (lhs_t->tag == PointerType && Match(lhs_t, PointerType)->is_stack)
                code_err(ast, "Stack references cannot be assigned to local variables because the variable may outlive the stack memory.");
            env_t *val_env = with_enum_scope(env, lhs_t);
            type_t *rhs_t = get_type(val_env, value->ast);
            CORD val;
            if (rhs_t->tag == IntType && value->ast->tag == Int) {
                val = compile_int_to_type(val_env, value->ast, rhs_t);
            } else {
                val = compile_maybe_incref(val_env, value->ast);
                if (!promote(val_env, &val, rhs_t, lhs_t))
                    code_err(value->ast, "You cannot assign a %T value to a %T operand", rhs_t, lhs_t);
            }
            CORD_appendf(&code, "%r $%ld = %r;\n", compile_type(lhs_t), i++, val);
        }
        i = 1;
        for (ast_list_t *target = assign->targets; target; target = target->next) {
            code = CORD_all(code, compile_assignment(env, target->ast, CORD_asprintf("$%ld", i++)), ";\n");
        }
        return CORD_cat(code, "\n}");
    }
    case UpdateAssign: {
        auto update = Match(ast, UpdateAssign);
        CORD lhs = compile_lvalue(env, update->lhs);

        CORD method_call = compile_math_method(env, update->op, update->lhs, update->rhs, get_type(env, update->lhs));
        if (method_call)
            return CORD_all(lhs, " = ", method_call, ";");

        CORD rhs = compile(env, update->rhs);

        type_t *lhs_t = get_type(env, update->lhs);
        type_t *rhs_t = get_type(env, update->rhs);
        if (!promote(env, &rhs, rhs_t, lhs_t)) {
            if (update->rhs->tag == Int && lhs_t->tag == IntType)
                rhs = compile_int_to_type(env, update->rhs, lhs_t);
            else if (!(lhs_t->tag == ArrayType && promote(env, &rhs, rhs_t, Match(lhs_t, ArrayType)->item_type)))
                code_err(ast, "I can't do operations between %T and %T", lhs_t, rhs_t);
        }

        switch (update->op) {
        case BINOP_MULT:
            if (lhs_t->tag != IntType && lhs_t->tag != NumType)
                code_err(ast, "I can't do a multiply assignment with this operator between %T and %T", lhs_t, rhs_t);
            return CORD_all(lhs, " *= ", rhs, ";");
        case BINOP_DIVIDE:
            if (lhs_t->tag != IntType && lhs_t->tag != NumType)
                code_err(ast, "I can't do a divide assignment with this operator between %T and %T", lhs_t, rhs_t);
            return CORD_all(lhs, " /= ", rhs, ";");
        case BINOP_MOD:
            if (lhs_t->tag != IntType && lhs_t->tag != NumType)
                code_err(ast, "I can't do a mod assignment with this operator between %T and %T", lhs_t, rhs_t);
            return CORD_all(lhs, " = ", lhs, " % ", rhs);
        case BINOP_MOD1:
            if (lhs_t->tag != IntType && lhs_t->tag != NumType)
                code_err(ast, "I can't do a mod assignment with this operator between %T and %T", lhs_t, rhs_t);
            return CORD_all(lhs, " = (((", lhs, ") - 1) % ", rhs, ") + 1;");
        case BINOP_PLUS:
            if (lhs_t->tag != IntType && lhs_t->tag != NumType)
                code_err(ast, "I can't do an addition assignment with this operator between %T and %T", lhs_t, rhs_t);
            return CORD_all(lhs, " += ", rhs, ";");
        case BINOP_MINUS:
            if (lhs_t->tag != IntType && lhs_t->tag != NumType)
                code_err(ast, "I can't do a subtraction assignment with this operator between %T and %T", lhs_t, rhs_t);
            return CORD_all(lhs, " -= ", rhs, ";");
        case BINOP_POWER: {
            if (lhs_t->tag != NumType)
                code_err(ast, "'^=' is only supported for Num types");
            if (lhs_t->tag == NumType && Match(lhs_t, NumType)->bits == TYPE_NBITS32)
                return CORD_all(lhs, " = powf(", lhs, ", ", rhs, ");");
            else
                return CORD_all(lhs, " = pow(", lhs, ", ", rhs, ");");
        }
        case BINOP_LSHIFT:
            if (lhs_t->tag != IntType)
                code_err(ast, "I can't do a shift assignment with this operator between %T and %T", lhs_t, rhs_t);
            return CORD_all(lhs, " <<= ", rhs, ";");
        case BINOP_RSHIFT:
            if (lhs_t->tag != IntType)
                code_err(ast, "I can't do a shift assignment with this operator between %T and %T", lhs_t, rhs_t);
            return CORD_all(lhs, " >>= ", rhs, ";");
        case BINOP_AND: {
            if (lhs_t->tag == BoolType)
                return CORD_all("if (", lhs, ") ", lhs, " = ", rhs, ";");
            else if (lhs_t->tag == IntType)
                return CORD_all(lhs, " &= ", rhs, ";");
            else
                code_err(ast, "'or=' is not implemented for %T types", lhs_t);
        }
        case BINOP_OR: {
            if (lhs_t->tag == BoolType)
                return CORD_all("if (!(", lhs, ")) ", lhs, " = ", rhs, ";");
            else if (lhs_t->tag == IntType)
                return CORD_all(lhs, " |= ", rhs, ";");
            else
                code_err(ast, "'or=' is not implemented for %T types", lhs_t);
        }
        case BINOP_XOR:
            if (lhs_t->tag != IntType && lhs_t->tag != BoolType)
                code_err(ast, "I can't do an xor assignment with this operator between %T and %T", lhs_t, rhs_t);
            return CORD_all(lhs, " ^= ", rhs, ";");
        case BINOP_CONCAT: {
            if (lhs_t->tag == TextType) {
                return CORD_all(lhs, " = Texts(", lhs, ", ", rhs, ");");
            } else if (lhs_t->tag == ArrayType) {
                CORD padded_item_size = CORD_asprintf("%ld", padded_type_size(Match(lhs_t, ArrayType)->item_type));
                if (promote(env, &rhs, rhs_t, Match(lhs_t, ArrayType)->item_type)) {
                    // arr ++= item
                    if (update->lhs->tag == Var)
                        return CORD_all("Array$insert(&", lhs, ", stack(", rhs, "), I(0), ", padded_item_size, ");");
                    else
                        return CORD_all(lhs, "Array$concat(", lhs, ", Array(", rhs, "), ", padded_item_size, ");");
                } else {
                    // arr ++= [...]
                    if (update->lhs->tag == Var)
                        return CORD_all("Array$insert_all(&", lhs, ", ", rhs, ", I(0), ", padded_item_size, ");");
                    else
                        return CORD_all(lhs, "Array$concat(", lhs, ", ", rhs, ", ", padded_item_size, ");");
                }
            } else {
                code_err(ast, "'++=' is not implemented for %T types", lhs_t);
            }
        }
        default: code_err(ast, "Update assignments are not implemented for this operation");
        }
    }
    case StructDef: {
        compile_struct_def(env, ast);
        return CORD_EMPTY;
    }
    case EnumDef: {
        compile_enum_def(env, ast);
        return CORD_EMPTY;
    }
    case LangDef: {
        auto def = Match(ast, LangDef);
        CORD_appendf(&env->code->typeinfos, "public const TypeInfo %r%s = {%zu, %zu, {.tag=TextInfo, .TextInfo={%r}}};\n",
                     namespace_prefix(env->libname, env->namespace), def->name, sizeof(Text_t), __alignof__(Text_t),
                     CORD_quoted(def->name));
        compile_namespace(env, def->name, def->namespace);
        return CORD_EMPTY;
    }
    case FunctionDef: {
        auto fndef = Match(ast, FunctionDef);
        bool is_private = Match(fndef->name, Var)->name[0] == '_';
        CORD name = compile(env, fndef->name);
        type_t *ret_t = fndef->ret_type ? parse_type_ast(env, fndef->ret_type) : Type(VoidType);

        CORD arg_signature = "(";
        for (arg_ast_t *arg = fndef->args; arg; arg = arg->next) {
            type_t *arg_type = get_arg_ast_type(env, arg);
            arg_signature = CORD_cat(arg_signature, compile_declaration(arg_type, CORD_cat("$", arg->name)));
            if (arg->next) arg_signature = CORD_cat(arg_signature, ", ");
        }
        arg_signature = CORD_cat(arg_signature, ")");

        CORD ret_type_code = compile_type(ret_t);

        if (is_private)
            env->code->staticdefs = CORD_all(env->code->staticdefs, "static ", ret_type_code, " ", name, arg_signature, ";\n");

        CORD code;
        if (fndef->cache) {
            code = CORD_all("static ", ret_type_code, " ", name, "$uncached", arg_signature);
        } else {
            code = CORD_all(ret_type_code, " ", name, arg_signature);
            if (fndef->is_inline)
                code = CORD_cat("inline ", code);
            if (!is_private)
                code = CORD_cat("public ", code);
        }

        env_t *body_scope = fresh_scope(env);
        body_scope->deferred = NULL;
        body_scope->namespace = NULL;
        for (arg_ast_t *arg = fndef->args; arg; arg = arg->next) {
            type_t *arg_type = get_arg_ast_type(env, arg);
            set_binding(body_scope, arg->name, new(binding_t, .type=arg_type, .code=CORD_cat("$", arg->name)));
        }

        fn_ctx_t fn_ctx = (fn_ctx_t){
            .parent=NULL,
            .return_type=ret_t,
            .closure_scope=NULL,
            .closed_vars=NULL,
        };
        body_scope->fn_ctx = &fn_ctx;

        type_t *body_type = get_type(body_scope, fndef->body);
        if (ret_t->tag != VoidType && ret_t->tag != AbortType && body_type->tag != AbortType && body_type->tag != ReturnType)
            code_err(ast, "This function can reach the end without returning a %T value!", ret_t);

        CORD body = compile_statement(body_scope, fndef->body);
        if (streq(Match(fndef->name, Var)->name, "main"))
            body = CORD_all(env->namespace->name, "$$initialize();\n", body);
        if (CORD_fetch(body, 0) != '{')
            body = CORD_asprintf("{\n%r\n}", body);
        env->code->funcs = CORD_all(env->code->funcs, code, " ", body, "\n");

        if (fndef->cache && fndef->args == NULL) { // no-args cache just uses a static var
            CORD wrapper = CORD_all(
                is_private ? CORD_EMPTY : "public ", ret_type_code, " ", name, "(void) {\n"
                "static ", compile_declaration(ret_t, "cached_result"), ";\n",
                "static bool initialized = false;\n",
                "if (!initialized) {\n"
                "\tcached_result = ", name, "$uncached();\n",
                "\tinitialized = true;\n",
                "}\n",
                "return cached_result;\n"
                "}\n");
            env->code->funcs = CORD_cat(env->code->funcs, wrapper);
        } else if (fndef->cache && fndef->cache->tag == Int) {
            OptionalInt64_t cache_size = Int64$from_text(Text$from_str(Match(fndef->cache, Int)->str));
            const char *arg_type_name = heap_strf("%s$args", Match(fndef->name, Var)->name);
            ast_t *args_def = FakeAST(StructDef, .name=arg_type_name, .fields=fndef->args);
            prebind_statement(env, args_def);
            bind_statement(env, args_def);
            (void)compile_statement(env, args_def);
            type_t *args_t = Table$str_get(*env->types, arg_type_name);
            assert(args_t);

            CORD all_args = CORD_EMPTY;
            for (arg_ast_t *arg = fndef->args; arg; arg = arg->next)
                all_args = CORD_all(all_args, "$", arg->name, arg->next ? ", " : CORD_EMPTY);

            CORD pop_code = CORD_EMPTY;
            if (fndef->cache->tag == Int && !cache_size.is_null && cache_size.i > 0) {
                pop_code = CORD_all("if (cache.entries.length > ", CORD_asprintf("%ld", cache_size.i),
                                    ") Table$remove(&cache, cache.entries.data + cache.entries.stride*Int$random(I(0), I(cache.entries.length-1)), table_type);\n");
            }

            CORD arg_typedef = compile_struct_typedef(env, args_def);
            env->code->local_typedefs = CORD_all(env->code->local_typedefs, arg_typedef);
            env->code->staticdefs = CORD_all(env->code->staticdefs,
                                             "extern const TypeInfo ", namespace_prefix(env->libname, env->namespace), arg_type_name, ";\n");
            CORD wrapper = CORD_all(
                is_private ? CORD_EMPTY : "public ", ret_type_code, " ", name, arg_signature, "{\n"
                "static Table_t cache = {};\n",
                compile_type(args_t), " args = {", all_args, "};\n"
                "const TypeInfo *table_type = Table$info(", compile_type_info(env, args_t), ", ", compile_type_info(env, ret_t), ");\n",
                compile_declaration(Type(PointerType, .pointed=ret_t), "cached"), " = Table$get_raw(cache, &args, table_type);\n"
                "if (cached) return *cached;\n",
                compile_declaration(ret_t, "ret"), " = ", name, "$uncached(", all_args, ");\n",
                pop_code,
                "Table$set(&cache, &args, &ret, table_type);\n"
                "return ret;\n"
                "}\n");
            env->code->funcs = CORD_cat(env->code->funcs, wrapper);
        }

        return CORD_EMPTY;
    }
    case Skip: {
        const char *target = Match(ast, Skip)->target;
        for (loop_ctx_t *ctx = env->loop_ctx; ctx; ctx = ctx->next) {
            bool matched = !target || CORD_cmp(target, ctx->loop_name) == 0;
            for (ast_list_t *var = ctx->loop_vars; var && !matched; var = var->next)
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
                return CORD_all(code, "goto ", ctx->skip_label, ";");
            }
        }
        if (env->loop_ctx)
            code_err(ast, "This 'skip' is not inside any loop");
        else if (target)
            code_err(ast, "No loop target named '%s' was found", target);
        else
            return "continue;";
    }
    case Stop: {
        const char *target = Match(ast, Stop)->target;
        for (loop_ctx_t *ctx = env->loop_ctx; ctx; ctx = ctx->next) {
            bool matched = !target || CORD_cmp(target, ctx->loop_name) == 0;
            for (ast_list_t *var = ctx->loop_vars; var && !matched; var = var->next)
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
                return CORD_all(code, "goto ", ctx->stop_label, ";");
            }
        }
        if (env->loop_ctx)
            code_err(ast, "This 'stop' is not inside any loop");
        else if (target)
            code_err(ast, "No loop target named '%s' was found", target);
        else
            return "break;";
    }
    case Pass: return ";";
    case Defer: {
        ast_t *body = Match(ast, Defer)->body;
        Table_t *closed_vars = get_closed_vars(env, FakeAST(Lambda, .args=NULL, .body=body));

        static int defer_id = 0;
        env_t *defer_env = fresh_scope(env);
        CORD code = CORD_EMPTY;
        for (int64_t i = 1; i <= Table$length(*closed_vars); i++) {
            struct { const char *name; binding_t *b; } *entry = Table$entry(*closed_vars, i);
            if (entry->b->type->tag == ModuleType)
                continue;
            CORD defer_name = CORD_asprintf("defer$%d$%s", ++defer_id, entry->name);
            code = CORD_all(
                code, compile_declaration(entry->b->type, defer_name), " = ", entry->b->code, ";\n");
            set_binding(defer_env, entry->name, new(binding_t, .type=entry->b->type, .code=defer_name));

            if (env->fn_ctx->closed_vars)
                Table$str_set(env->fn_ctx->closed_vars, entry->name, entry->b);
        }
        env->deferred = new(deferral_t, .defer_env=defer_env, .block=body, .next=env->deferred);
        return code;
    }
    case PrintStatement: {
        ast_list_t *to_print = Match(ast, PrintStatement)->to_print;
        if (!to_print)
            return CORD_EMPTY;

        CORD code = "say(";
        if (to_print->next) code = CORD_all(code, "Texts(");
        for (ast_list_t *chunk = to_print; chunk; chunk = chunk->next) {
            if (chunk->ast->tag == TextLiteral) {
                code = CORD_cat(code, compile(env, chunk->ast));
            } else {
                code = CORD_cat(code, compile_string(env, chunk->ast, "USE_COLOR"));
            }
            if (chunk->next) code = CORD_cat(code, ", ");
        }
        if (to_print->next) code = CORD_all(code, ")");
        return CORD_cat(code, ", yes);");
    }
    case Return: {
        if (!env->fn_ctx) code_err(ast, "This return statement is not inside any function");
        auto ret = Match(ast, Return)->value;
        assert(env->fn_ctx->return_type);

        CORD code = CORD_EMPTY;
        for (deferral_t *deferred = env->deferred; deferred; deferred = deferred->next) {
            code = CORD_all(code, compile_statement(deferred->defer_env, deferred->block));
        }

        if (ret) {
            if (env->fn_ctx->return_type->tag == VoidType || env->fn_ctx->return_type->tag == AbortType)
                code_err(ast, "This function is not supposed to return any values, according to its type signature");

            env = with_enum_scope(env, env->fn_ctx->return_type);
            CORD value;
            if (env->fn_ctx->return_type->tag == IntType && ret->tag == Int) {
                value = compile_int_to_type(env, ret, env->fn_ctx->return_type);
            } else {
                type_t *ret_value_t = get_type(env, ret);
                value = compile(env, ret);
                if (!promote(env, &value, ret_value_t, env->fn_ctx->return_type))
                    code_err(ast, "This function expects a return value of type %T, but this return has type %T", 
                             env->fn_ctx->return_type, ret_value_t);
            }
            return CORD_all(code, "return ", value, ";");
        } else {
            if (env->fn_ctx->return_type->tag != VoidType)
                code_err(ast, "This function expects you to return a %T value", env->fn_ctx->return_type);
            return CORD_all(code, "return;");
        }
    }
    case While: {
        auto while_ = Match(ast, While);
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
    case For: {
        auto for_ = Match(ast, For);

        // If we're iterating over a comprehension, that's actually just doing
        // one loop, we don't need to compile the comprehension as an array
        // comprehension. This is a common case for reducers like `(+) i*2 for i in 5`
        // or `(and) x:is_good() for x in xs`
        if (for_->iter->tag == Comprehension) {
            auto comp = Match(for_->iter, Comprehension);
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

        type_t *iter_t = get_type(env, for_->iter);
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

        if (iter_t == RANGE_TYPE) {
            CORD range = compile(env, for_->iter);
            CORD value = for_->vars ? compile(body_scope, for_->vars->ast) : "i";
            if (for_->empty)
                code_err(ast, "Ranges are never empty, they always contain at least their starting element");
            return CORD_all(
                "{\n"
                "const Range_t range = ", range, ";\n"
                "if (range.step.small == 0) fail(\"This range has a 'step' of zero and will loop infinitely!\");\n"
                "bool negative = (Int$compare_value(range.step, I(0)) < 0);\n"
                "for (Int_t ", value, " = range.first; ({ int32_t cmp = Int$compare_value(", value, ", range.last); negative ? cmp >= 0 : cmp <= 0;}) ; ", value, " = Int$plus(", value, ", range.step)) {\n"
                "\t", naked_body,
                "\n}",
                stop,
                "\n}");
        }

        switch (iter_t->tag) {
        case ArrayType: {
            type_t *item_t = Match(iter_t, ArrayType)->item_type;
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
            ast_t *array = for_->iter;
            // Micro-optimization: inline the logic for iterating over
            // `array:from(i)` and `array:to(i)` because these happen inside
            // hot path inner loops and can actually meaningfully affect
            // performance:
            // if (for_->iter->tag == MethodCall && streq(Match(for_->iter, MethodCall)->name, "to")
            //     && value_type(get_type(env, Match(for_->iter, MethodCall)->self))->tag == ArrayType) {
            //     array = Match(for_->iter, MethodCall)->self;
            //     CORD limit = compile_arguments(env, for_->iter, new(arg_t, .type=INT_TYPE, .name="last"), Match(for_->iter, MethodCall)->args);
            //     loop = CORD_all(loop, "for (int64_t ", index, " = 1, raw_limit = ", limit,
            //                     ", limit = raw_limit < 0 ? iterating.length + raw_limit + 1 : raw_limit; ",
            //                     index, " <= limit; ++", index, ")");
            // } else if (for_->iter->tag == MethodCall && streq(Match(for_->iter, MethodCall)->name, "from")
            //            && value_type(get_type(env, Match(for_->iter, MethodCall)->self))->tag == ArrayType) {
            //     array = Match(for_->iter, MethodCall)->self;
            //     CORD first = compile_arguments(env, for_->iter, new(arg_t, .type=INT_TYPE, .name="last"), Match(for_->iter, MethodCall)->args);
            //     loop = CORD_all(loop, "for (int64_t first = ", first, ", ", index, " = MAX(1, first < 1 ? iterating.length + first + 1 : first", "); ",
            //                     index, " <= iterating.length; ++", index, ")");
            // } else {
                loop = CORD_all(loop, "for (int64_t i = 1; i <= iterating.length; ++i)");
            // }

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

            if (can_be_mutated(env, array) && is_idempotent(array)) {
                CORD array_code = compile(env, array);
                loop = CORD_all("{\n"
                                "Array_t iterating = ARRAY_COPY(", array_code, ");\n",
                                loop, 
                                stop,
                                "\nARRAY_DECREF(", array_code, ");\n"
                                "}\n");

                if (for_->empty)
                    loop = CORD_all("if (", array_code, ".length > 0) {\n", loop, "\n} else ", compile_statement(env, for_->empty));
            } else {
                loop = CORD_all("{\n"
                                "Array_t iterating = ", compile(env, array), ";\n",
                                for_->empty ? "if (iterating.length > 0) {\n" : CORD_EMPTY,
                                loop, 
                                for_->empty ? CORD_all("\n} else ", compile_statement(env, for_->empty)) : CORD_EMPTY,
                                stop,
                                "}\n");
            }
            return loop;
        }
        case SetType: case TableType: {
            CORD loop = "for (int64_t i = 0; i < iterating.length; ++i) {\n";
            if (for_->vars) {
                if (iter_t->tag == SetType) {
                    if (for_->vars->next)
                        code_err(for_->vars->next->ast, "This is too many variables for this loop");
                    CORD item = compile(body_scope, for_->vars->ast);
                    type_t *item_type = Match(iter_t, SetType)->item_type;
                    loop = CORD_all(loop, compile_declaration(item_type, item), " = *(", compile_type(item_type), "*)(",
                                    "iterating.data + i*iterating.stride);\n");
                } else {
                    CORD key = compile(body_scope, for_->vars->ast);
                    type_t *key_t = Match(iter_t, TableType)->key_type;
                    loop = CORD_all(loop, compile_declaration(key_t, key), " = *(", compile_type(key_t), "*)(",
                                    "iterating.data + i*iterating.stride);\n");

                    if (for_->vars->next) {
                        if (for_->vars->next->next)
                            code_err(for_->vars->next->next->ast, "This is too many variables for this loop");

                        type_t *value_t = Match(iter_t, TableType)->value_type;
                        CORD value = compile(body_scope, for_->vars->next->ast);
                        size_t value_offset = type_size(key_t);
                        if (type_align(value_t) > 1 && value_offset % type_align(value_t))
                            value_offset += type_align(value_t) - (value_offset % type_align(value_t)); // padding
                        loop = CORD_all(loop, compile_declaration(value_t, value), " = *(", compile_type(value_t), "*)(",
                                        "iterating.data + i*iterating.stride + ", heap_strf("%zu", value_offset), ");\n");
                    }
                }
            }

            loop = CORD_all(loop, naked_body, "\n}");

            if (for_->empty) {
                loop = CORD_all("if (iterating.length > 0) {\n", loop, "\n} else ", compile_statement(env, for_->empty));
            }

            if (can_be_mutated(env, for_->iter) && is_idempotent(for_->iter)) {
                loop = CORD_all(
                    "{\n",
                    "Array_t iterating = ARRAY_COPY((", compile(env, for_->iter), ").entries);\n",
                    loop,
                    "ARRAY_DECREF((", compile(env, for_->iter), ").entries);\n"
                    "}\n");
            } else {
                loop = CORD_all(
                    "{\n",
                    "Array_t iterating = (", compile(env, for_->iter), ").entries;\n",
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
            n = compile(env, for_->iter);
            CORD i = for_->vars ? compile(body_scope, for_->vars->ast) : "i";
            CORD n_var = for_->vars ? CORD_all("max", i) : "n";
            if (for_->empty) {
                return CORD_all(
                    "{\n"
                    "Int_t ", n_var, " = ", compile(env, for_->iter), ";\n"
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

            code = CORD_all(code, compile_declaration(iter_t, "next"), " = ", compile(env, for_->iter), ";\n");

            auto fn = iter_t->tag == ClosureType ? Match(Match(iter_t, ClosureType)->fn, FunctionType) : Match(iter_t, FunctionType);
            code = CORD_all(code, compile_declaration(fn->ret, "cur"), ";\n"); // Iteration enum

            CORD next_fn;
            if (iter_t->tag == ClosureType) {
                type_t *fn_t = Match(iter_t, ClosureType)->fn;
                arg_t *closure_fn_args = NULL;
                for (arg_t *arg = Match(fn_t, FunctionType)->args; arg; arg = arg->next)
                    closure_fn_args = new(arg_t, .name=arg->name, .type=arg->type, .default_val=arg->default_val, .next=closure_fn_args);
                closure_fn_args = new(arg_t, .name="userdata", .type=Type(PointerType, .pointed=Type(MemoryType)), .next=closure_fn_args);
                REVERSE_LIST(closure_fn_args);
                CORD fn_type_code = compile_type(Type(FunctionType, .args=closure_fn_args, .ret=Match(fn_t, FunctionType)->ret));
                next_fn = CORD_all("((", fn_type_code, ")next.fn)");
            } else {
                next_fn = "next";
            }

            env_t *enum_env = Match(fn->ret, EnumType)->env;
            next_fn = CORD_all("(cur=", next_fn, iter_t->tag == ClosureType ? "(next.userdata)" : "()", ").tag == ",
                               namespace_prefix(enum_env->libname, enum_env->namespace), "tag$Next");

            if (for_->empty) {
                code = CORD_all(code, "if (", next_fn, ") {\n"
                                "\tdo{\n\t\t", naked_body, "\t} while(", next_fn, ");\n"
                                "} else {\n\t", compile_statement(env, for_->empty), "}", stop, "\n}\n");
            } else {
                code = CORD_all(code, "for(; ", next_fn, "; ) {\n\t", naked_body, "}\n", stop, "\n}\n");
            }
            return code;
        }
        default: code_err(for_->iter, "Iteration is not implemented for type: %T", iter_t);
        }
    }
    case If: {
        auto if_ = Match(ast, If);
        ast_t *condition = if_->condition;
        CORD code = CORD_EMPTY;
        if (condition->tag == Declare) {
            env = fresh_scope(env);
            code = compile_statement(env, condition);
            bind_statement(env, condition);
            condition = Match(condition, Declare)->var;
        }

        type_t *cond_t = get_type(env, condition);
        if (cond_t->tag == PointerType)
            code_err(condition, "This pointer will always be non-null, so it should not be used in a conditional.");

        env_t *truthy_scope = env; 
        CORD condition_code;
        if (cond_t->tag == TextType) {
            condition_code = CORD_all("(", compile(env, condition), ").length");
        } else if (cond_t->tag == ArrayType) {
            condition_code = CORD_all("(", compile(env, condition), ").length");
        } else if (cond_t->tag == TableType || cond_t->tag == SetType) {
            condition_code = CORD_all("(", compile(env, condition), ").entries.length");
        } else if (cond_t->tag == OptionalType) {
            if (condition->tag == Var) {
                truthy_scope = fresh_scope(env);
                const char *varname = Match(condition, Var)->name;
                binding_t *b = get_binding(env, varname);
                binding_t *nonnull_b = new(binding_t);
                *nonnull_b = *b;
                nonnull_b->type = Match(cond_t, OptionalType)->type;
                nonnull_b->code = optional_var_into_nonnull(b);
                set_binding(truthy_scope, varname, nonnull_b);
            }
            condition_code = compile_optional_check(env, condition);
        } else if (cond_t->tag == BoolType) {
            condition_code = compile(env, condition);
        } else {
            code_err(condition, "%T values cannot be used for conditionals", cond_t);
        }

        code = CORD_all(code, "if (", condition_code, ")", compile_statement(truthy_scope, if_->body));
        if (if_->else_body)
            code = CORD_all(code, "\nelse ", compile_statement(env, if_->else_body));

        if (if_->condition->tag == Declare)
            code = CORD_all("{\n", code, "}\n");
        return code;
    }
    case Block: {
        return CORD_all("{\n", compile_inline_block(env, ast), "}\n");
    }
    case Comprehension: {
        auto comp = Match(ast, Comprehension);
        assert(env->comprehension_var);
        if (comp->expr->tag == Comprehension) { // Nested comprehension
            ast_t *body = comp->filter ? WrapAST(ast, If, .condition=comp->filter, .body=comp->expr) : comp->expr;
            ast_t *loop = WrapAST(ast, For, .vars=comp->vars, .iter=comp->iter, .body=body);
            return compile_statement(env, loop);
        } else if (comp->expr->tag == TableEntry) { // Table comprehension
            auto e = Match(comp->expr, TableEntry);
            ast_t *body = WrapAST(ast, MethodCall, .name="set", .self=FakeAST(StackReference, FakeAST(Var, env->comprehension_var)),
                                  .args=new(arg_ast_t, .value=e->key, .next=new(arg_ast_t, .value=e->value)));
            if (comp->filter)
                body = WrapAST(body, If, .condition=comp->filter, .body=body);
            ast_t *loop = WrapAST(ast, For, .vars=comp->vars, .iter=comp->iter, .body=body);
            return compile_statement(env, loop);
        } else { // Array comprehension
            ast_t *body = WrapAST(comp->expr, MethodCall, .name="insert", .self=FakeAST(StackReference, FakeAST(Var, env->comprehension_var)),
                                  .args=new(arg_ast_t, .value=comp->expr));
            if (comp->filter)
                body = WrapAST(body, If, .condition=comp->filter, .body=body);
            ast_t *loop = WrapAST(ast, For, .vars=comp->vars, .iter=comp->iter, .body=body);
            return compile_statement(env, loop);
        }
    }
    case Extern: return CORD_EMPTY;
    case InlineCCode: return Match(ast, InlineCCode)->code;
    case Use: {
        auto use = Match(ast, Use);
        if (use->what == USE_LOCAL) {
            CORD name = file_base_name(Match(ast, Use)->path);
            env->code->variable_initializers = CORD_all(env->code->variable_initializers, name, "$$initialize();\n");
        } else if (use->what == USE_MODULE) {
            const char *libname = file_base_name(use->path);
            const char *files_filename = heap_strf("%s/lib%s.files", libname, libname);
            const char *resolved_path = resolve_path(files_filename, ast->file->filename, getenv("TOMO_IMPORT_PATH"));
            if (!resolved_path)
                code_err(ast, "No such library exists: \"lib%s.files\"", libname);
            file_t *files_f = load_file(resolved_path);
            if (!files_f) errx(1, "Couldn't open file: %s", resolved_path);
            for (int64_t i = 1; i <= files_f->num_lines; i++) {
                const char *line = get_line(files_f, i);
                line = GC_strndup(line, strcspn(line, "\r\n"));
                env->code->variable_initializers = CORD_all(
                    env->code->variable_initializers, use->path, "$", file_base_name(line), "$$initialize();\n");
            }
        }
        return CORD_EMPTY;
    }
    default:
        if (!is_discardable(env, ast))
            code_err(ast, "The result of this statement cannot be discarded");
        return CORD_asprintf("(void)%r;", compile(env, ast));
    }
}

// CORD compile_statement(env_t *env, ast_t *ast) {
//     CORD stmt = _compile_statement(env, ast);
//     if (!stmt)
//         return stmt;
//     int64_t line = get_line_number(ast->file, ast->start);
//     return CORD_asprintf("#line %ld\n%r", line, stmt);
// }

CORD expr_as_text(env_t *env, CORD expr, type_t *t, CORD color)
{
    switch (t->tag) {
    case MemoryType: return CORD_asprintf("Memory$as_text(stack(%r), %r, &Memory$info)", expr, color);
    case BoolType:
         // NOTE: this cannot use stack(), since bools may actually be bit fields:
         return CORD_asprintf("Bool$as_text((Bool_t[1]){%r}, %r, &Bool$info)", expr, color);
    case CStringType: return CORD_asprintf("CString$as_text(stack(%r), %r, &CString$info)", expr, color);
    case BigIntType: case IntType: {
        CORD name = type_to_cord(t);
        return CORD_asprintf("%r$as_text(stack(%r), %r, &%r$info)", name, expr, color, name);
    }
    case NumType: {
        CORD name = type_to_cord(t);
        return CORD_asprintf("%r$as_text(stack(%r), %r, &%r$info)", name, expr, color, name);
    }
    case TextType: {
        return CORD_asprintf("Text$as_text(stack(%r), %r, %r)", expr, color, compile_type_info(env, t));
    }
    case ArrayType: return CORD_asprintf("Array$as_text(stack(%r), %r, %r)", expr, color, compile_type_info(env, t));
    case SetType: return CORD_asprintf("Table$as_text(stack(%r), %r, %r)", expr, color, compile_type_info(env, t));
    case ChannelType: return CORD_asprintf("Channel$as_text(stack(%r), %r, %r)", expr, color, compile_type_info(env, t));
    case TableType: return CORD_asprintf("Table$as_text(stack(%r), %r, %r)", expr, color, compile_type_info(env, t));
    case FunctionType: case ClosureType: return CORD_asprintf("Func$as_text(stack(%r), %r, %r)", expr, color, compile_type_info(env, t));
    case PointerType: return CORD_asprintf("Pointer$as_text(stack(%r), %r, %r)", expr, color, compile_type_info(env, t));
    case OptionalType: return CORD_asprintf("Optional$as_text(stack(%r), %r, %r)", expr, color, compile_type_info(env, t));
    case StructType: case EnumType:
        return CORD_asprintf("(%r)->CustomInfo.as_text(stack(%r), %r, %r)",
                             compile_type_info(env, t), expr, color, compile_type_info(env, t));
    default: compiler_err(NULL, NULL, NULL, "Stringifying is not supported for %T", t);
    }
}

CORD compile_string(env_t *env, ast_t *ast, CORD color)
{
    type_t *t = get_type(env, ast);
    CORD expr = compile(env, ast);
    return expr_as_text(env, expr, t, color);
}

CORD compile_to_pointer_depth(env_t *env, ast_t *ast, int64_t target_depth, bool needs_incref)
{
    CORD val = compile(env, ast);
    type_t *t = get_type(env, ast);
    int64_t depth = 0;
    for (type_t *tt = t; tt->tag == PointerType; tt = Match(tt, PointerType)->pointed)
        ++depth;

    // Passing a literal value won't trigger an incref, because it's ephemeral,
    // e.g. [10, 20]:reversed()
    if (t->tag != PointerType && needs_incref && !can_be_mutated(env, ast))
        needs_incref = false;

    while (depth != target_depth) {
        if (depth < target_depth) {
            if (ast->tag == Var && target_depth == 1)
                val = CORD_all("(&", val, ")");
            else
                code_err(ast, "This should be a pointer, not %T", get_type(env, ast));
            t = Type(PointerType, .pointed=t, .is_stack=true);
            ++depth;
        } else {
            auto ptr = Match(t, PointerType);
            val = CORD_all("*(", val, ")");
            t = ptr->pointed;
            --depth;
        }
    }

    while (t->tag == PointerType) {
        auto ptr = Match(t, PointerType);
        t = ptr->pointed;
    }

    if (needs_incref && t->tag == ArrayType)
        val = CORD_all("ARRAY_COPY(", val, ")");
    else if (needs_incref && (t->tag == TableType || t->tag == SetType))
        val = CORD_all("TABLE_COPY(", val, ")");

    return val;
}

env_t *with_enum_scope(env_t *env, type_t *t)
{
    if (t->tag != EnumType) return env;
    env = fresh_scope(env);
    env_t *ns_env = Match(t, EnumType)->env;
    for (tag_t *tag = Match(t, EnumType)->tags; tag; tag = tag->next) {
        if (get_binding(env, tag->name))
            continue;
        binding_t *b = get_binding(ns_env, tag->name);
        assert(b);
        set_binding(env, tag->name, b);
    }
    return env;
}

CORD compile_int_to_type(env_t *env, ast_t *ast, type_t *target)
{
    if (target->tag == BigIntType)
        return compile(env, ast);

    if (ast->tag != Int) {
        CORD code = compile(env, ast);
        type_t *actual_type = get_type(env, ast);
        if (!promote(env, &code, actual_type, target))
            code = CORD_all(type_to_cord(actual_type), "_to_", type_to_cord(target), "(", code, ", no)");
        return code;
    }

    int64_t target_bits = (int64_t)Match(target, IntType)->bits;
    OptionalInt_t int_val = Int$from_str(Match(ast, Int)->str);
    if (int_val.small == 0)
        code_err(ast, "Failed to parse this integer");

    mpz_t i;
    mpz_init_set_int(i, int_val);

    switch (target_bits) {
    case TYPE_IBITS64:
        if (mpz_cmp_si(i, INT64_MAX) <= 0 && mpz_cmp_si(i, INT64_MIN) >= 0)
            return CORD_asprintf("I64(%s)", Match(ast, Int)->str);
        break;
    case TYPE_IBITS32:
        if (mpz_cmp_si(i, INT32_MAX) <= 0 && mpz_cmp_si(i, INT32_MIN) >= 0)
            return CORD_asprintf("I32(%s)", Match(ast, Int)->str);
        break;
    case TYPE_IBITS16:
        if (mpz_cmp_si(i, INT16_MAX) <= 0 && mpz_cmp_si(i, INT16_MIN) >= 0)
            return CORD_asprintf("I16(%s)", Match(ast, Int)->str);
        break;
    case TYPE_IBITS8:
        if (mpz_cmp_si(i, INT8_MAX) <= 0 && mpz_cmp_si(i, INT8_MIN) >= 0)
            return CORD_asprintf("I8(%s)", Match(ast, Int)->str);
        break;
    default: break;
    }
    code_err(ast, "This integer cannot fit in a %d-bit value", target_bits);
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
                        double n = Int_to_Num(int_val);
                        value = CORD_asprintf(Match(spec_arg->type, NumType)->bits == TYPE_NBITS64
                                              ? "N64(%.20g)" : "N32(%.10g)", n);
                    } else {
                        env_t *arg_env = with_enum_scope(env, spec_arg->type);
                        type_t *actual_t = get_type(arg_env, call_arg->value);
                        value = compile_maybe_incref(arg_env, call_arg->value);
                        if (!promote(arg_env, &value, actual_t, spec_arg->type))
                            code_err(call_arg->value, "This argument is supposed to be a %T, but this value is a %T", spec_arg->type, actual_t);
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
            const char *pseudoname = heap_strf("%ld", i++);
            if (!Table$str_get(used_args, pseudoname)) {
                CORD value;
                if (spec_arg->type->tag == IntType && call_arg->value->tag == Int) {
                    value = compile_int_to_type(env, call_arg->value, spec_arg->type);
                } else if (spec_arg->type->tag == NumType && call_arg->value->tag == Int) {
                    OptionalInt_t int_val = Int$from_str(Match(call_arg->value, Int)->str);
                    if (int_val.small == 0)
                        code_err(call_arg->value, "Failed to parse this integer");
                    double n = Int_to_Num(int_val);
                    value = CORD_asprintf(Match(spec_arg->type, NumType)->bits == TYPE_NBITS64
                                          ? "N64(%.20g)" : "N32(%.10g)", n);
                } else {
                    env_t *arg_env = with_enum_scope(env, spec_arg->type);
                    type_t *actual_t = get_type(arg_env, call_arg->value);
                    value = compile_maybe_incref(arg_env, call_arg->value);
                    if (!promote(arg_env, &value, actual_t, spec_arg->type))
                        code_err(call_arg->value, "This argument is supposed to be a %T, but this value is a %T", spec_arg->type, actual_t);
                }

                Table$str_set(&used_args, pseudoname, call_arg);
                if (code) code = CORD_cat(code, ", ");
                code = CORD_cat(code, value);
                goto found_it;
            }
        }

        if (spec_arg->default_val) {
            if (code) code = CORD_cat(code, ", ");
            code = CORD_cat(code, compile_maybe_incref(default_scope, spec_arg->default_val));
            goto found_it;
        }

        assert(spec_arg->name);
        code_err(call_ast, "The required argument '%s' was not provided", spec_arg->name);
      found_it: continue;
    }

    int64_t i = 1;
    for (arg_ast_t *call_arg = call_args; call_arg; call_arg = call_arg->next) {
        if (call_arg->name) {
            if (!Table$str_get(used_args, call_arg->name))
                code_err(call_arg->value, "There is no argument with the name '%s'", call_arg->name);
        } else {
            const char *pseudoname = heap_strf("%ld", i++);
            if (!Table$str_get(used_args, pseudoname))
                code_err(call_arg->value, "This is one argument too many!");
        }
    }
    return code;
}

CORD compile_math_method(env_t *env, binop_e op, ast_t *lhs, ast_t *rhs, type_t *required_type)
{
    // Math methods are things like plus(), minus(), etc. If we don't find a
    // matching method, return CORD_EMPTY.
    const char *method_name = binop_method_names[op];
    if (!method_name)
        return CORD_EMPTY;

    type_t *lhs_t = get_type(env, lhs);
    type_t *rhs_t = get_type(env, rhs);
#define binding_works(b, lhs_t, rhs_t, ret_t) \
         (b && b->type->tag == FunctionType && ({ auto fn = Match(b->type, FunctionType);  \
                                                (type_eq(fn->ret, ret_t) \
                                                 && (fn->args && type_eq(fn->args->type, lhs_t)) \
                                                 && (fn->args->next && can_promote(fn->args->next->type, rhs_t)) \
                                                 && (!required_type || type_eq(required_type, fn->ret))); }))
    switch (op) {
    case BINOP_MULT: {
        if (type_eq(lhs_t, rhs_t)) {
            binding_t *b = get_namespace_binding(env, lhs, binop_method_names[op]);
            if (binding_works(b, lhs_t, rhs_t, lhs_t))
                return CORD_all(b->code, "(", compile(env, lhs), ", ", compile(env, rhs), ")");
        } else if (lhs_t->tag == NumType || lhs_t->tag == IntType || lhs_t->tag == BigIntType) {
            binding_t *b = get_namespace_binding(env, rhs, "scaled_by");
            if (binding_works(b, rhs_t, lhs_t, rhs_t))
                return CORD_all(b->code, "(", compile(env, rhs), ", ", compile(env, lhs), ")");
        } else if (rhs_t->tag == NumType || rhs_t->tag == IntType|| rhs_t->tag == BigIntType) {
            binding_t *b = get_namespace_binding(env, lhs, "scaled_by");
            if (binding_works(b, lhs_t, rhs_t, lhs_t))
                return CORD_all(b->code, "(", compile(env, lhs), ", ", compile(env, rhs), ")");
        }
        break;
    }
    case BINOP_PLUS: case BINOP_MINUS: case BINOP_AND: case BINOP_OR: case BINOP_XOR: {
        if (type_eq(lhs_t, rhs_t)) {
            binding_t *b = get_namespace_binding(env, lhs, binop_method_names[op]);
            if (binding_works(b, lhs_t, rhs_t, lhs_t))
                return CORD_all(b->code, "(", compile(env, lhs), ", ", compile(env, rhs), ")");
        }
        break;
    }
    case BINOP_DIVIDE: case BINOP_MOD: case BINOP_MOD1: {
        if (rhs_t->tag == NumType || rhs_t->tag == IntType || rhs_t->tag == BigIntType) {
            binding_t *b = get_namespace_binding(env, lhs, binop_method_names[op]);
            if (binding_works(b, lhs_t, rhs_t, lhs_t))
                return CORD_all(b->code, "(", compile(env, lhs), ", ", compile(env, rhs), ")");
        }
        break;
    }
    case BINOP_LSHIFT: case BINOP_RSHIFT: {
        if (rhs_t->tag == IntType || rhs_t->tag == BigIntType) {
            binding_t *b = get_namespace_binding(env, lhs, binop_method_names[op]);
            if (binding_works(b, lhs_t, rhs_t, lhs_t))
                return CORD_all(b->code, "(", compile(env, lhs), ", ", compile(env, rhs), ")");
        }
        break;
    }
    case BINOP_POWER: {
        if (rhs_t->tag == NumType || rhs_t->tag == IntType || rhs_t->tag == BigIntType) {
            binding_t *b = get_namespace_binding(env, lhs, binop_method_names[op]);
            if (binding_works(b, lhs_t, rhs_t, lhs_t))
                return CORD_all(b->code, "(", compile(env, lhs), ", ", compile(env, rhs), ")");
        }
        break;
    }
    default: break;
    }
    return CORD_EMPTY;
}

static CORD compile_string_literal(CORD literal)
{
    CORD code = "\"";
    CORD_pos i;
#pragma GCC diagnostic ignored "-Wsign-conversion"
    CORD_FOR(i, literal) {
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
    CORD_FOR(i, literal) {
        if (!isascii(CORD_pos_fetch(i)))
            return false;
    }
    return true;
}

CORD compile_null(type_t *t)
{
    if (t == THREAD_TYPE) return "NULL";

    switch (t->tag) {
    case BigIntType: return "NULL_INT";
    case IntType: {
        switch (Match(t, IntType)->bits) {
        case TYPE_IBITS8: return "NULL_INT8";
        case TYPE_IBITS16: return "NULL_INT16";
        case TYPE_IBITS32: return "NULL_INT32";
        case TYPE_IBITS64: return "NULL_INT64";
        default: errx(1, "Invalid integer bit size");
        }
        break;
    }
    case BoolType: return "NULL_BOOL";
    case ArrayType: return "NULL_ARRAY";
    case TableType: return "NULL_TABLE";
    case SetType: return "NULL_TABLE";
    case ChannelType: return "NULL";
    case TextType: return "NULL_TEXT";
    case CStringType: return "NULL";
    case PointerType: return CORD_all("((", compile_type(t), ")NULL)");
    case ClosureType: return "NULL_CLOSURE";
    case NumType: return "nan(\"null\")";
    case StructType: return CORD_all("((", compile_type(Type(OptionalType, .type=t)), "){.is_null=true})");
    case EnumType: {
        env_t *enum_env = Match(t, EnumType)->env;
        return CORD_all("((", compile_type(t), "){", namespace_prefix(enum_env->libname, enum_env->namespace), "null})");
    }
    default: compiler_err(NULL, NULL, NULL, "Null isn't implemented for this type: %T", t);
    }
}

CORD compile(env_t *env, ast_t *ast)
{
    switch (ast->tag) {
    case Null: {
        type_t *t = parse_type_ast(env, Match(ast, Null)->type);
        return compile_null(t);
    }
    case Bool: return Match(ast, Bool)->b ? "yes" : "no";
    case Var: {
        binding_t *b = get_binding(env, Match(ast, Var)->name);
        if (b)
            return b->code ? b->code : CORD_cat("$", Match(ast, Var)->name);
        return CORD_cat("$", Match(ast, Var)->name);
        // code_err(ast, "I don't know of any variable by this name");
    }
    case Int: {
        const char *str = Match(ast, Int)->str;
        OptionalInt_t int_val = Int$from_str(str);
        if (int_val.small == 0)
            code_err(ast, "Failed to parse this integer");
        mpz_t i;
        mpz_init_set_int(i, int_val);

        switch (Match(ast, Int)->bits) {
        case IBITS_UNSPECIFIED:
        if (mpz_cmpabs_ui(i, BIGGEST_SMALL_INT) <= 0) {
            return CORD_asprintf("I_small(%s)", str);
        } else if (mpz_cmp_si(i, INT64_MAX) <= 0 && mpz_cmp_si(i, INT64_MIN) >= 0) {
            return CORD_asprintf("Int64_to_Int(%s)", str);
        } else {
            return CORD_asprintf("Int$from_str(\"%s\")", str);
        }
        case IBITS64:
        if ((mpz_cmp_si(i, INT64_MAX) < 0) && (mpz_cmp_si(i, INT64_MIN) > 0))
            return CORD_asprintf("I64(%ldl)", mpz_get_si(i));
        code_err(ast, "This value cannot fit in a 64-bit integer");
        case IBITS32:
        if ((mpz_cmp_si(i, INT32_MAX) < 0) && (mpz_cmp_si(i, INT32_MIN) > 0))
            return CORD_asprintf("I32(%ld)", mpz_get_si(i));
        code_err(ast, "This value cannot fit in a 32-bit integer");
        case IBITS16:
        if ((mpz_cmp_si(i, INT16_MAX) < 0) && (mpz_cmp_si(i, INT16_MIN) > 0))
            return CORD_asprintf("I16(%ld)", mpz_get_si(i));
        code_err(ast, "This value cannot fit in a 16-bit integer");
        case IBITS8:
        if ((mpz_cmp_si(i, INT8_MAX) < 0) && (mpz_cmp_si(i, INT8_MIN) > 0))
            return CORD_asprintf("I8(%ld)", mpz_get_si(i));
        code_err(ast, "This value cannot fit in a 8-bit integer");
        default: code_err(ast, "Not a valid integer bit width");
        }
    }
    case Num: {
        switch (Match(ast, Num)->bits) {
        case NBITS_UNSPECIFIED: case NBITS64:
            return CORD_asprintf("N64(%.20g)", Match(ast, Num)->n);
        case NBITS32:
            return CORD_asprintf("N32(%.10g)", Match(ast, Num)->n);
        default: code_err(ast, "This is not a valid number bit width");
        }
    }
    case Not: {
        ast_t *value = Match(ast, Not)->value;
        type_t *t = get_type(env, ast);

        binding_t *b = get_namespace_binding(env, value, "negated");
        if (b && b->type->tag == FunctionType) {
            auto fn = Match(b->type, FunctionType);
            if (fn->args && can_promote(t, get_arg_type(env, fn->args)))
                return CORD_all(b->code, "(", compile_arguments(env, ast, fn->args, new(arg_ast_t, .value=value)), ")");
        }

        if (t->tag == BoolType)
            return CORD_all("!(", compile(env, value), ")");
        else if (t->tag == IntType)
            return CORD_all("~(", compile(env, value), ")");
        else if (t->tag == ArrayType)
            return CORD_all("((", compile(env, value), ").length == 0)");
        else if (t->tag == SetType || t->tag == TableType)
            return CORD_all("((", compile(env, value), ").entries.length == 0)");
        else if (t->tag == TextType)
            return CORD_all("(", compile(env, value), " == CORD_EMPTY)");
        else if (t->tag == OptionalType)
            return CORD_all("!(", compile_optional_check(env, value), ")");

        code_err(ast, "I don't know how to negate values of type %T", t);
    }
    case Negative: {
        ast_t *value = Match(ast, Negative)->value;
        type_t *t = get_type(env, value);
        binding_t *b = get_namespace_binding(env, value, "negative");
        if (b && b->type->tag == FunctionType) {
            auto fn = Match(b->type, FunctionType);
            if (fn->args && can_promote(t, get_arg_type(env, fn->args)))
                return CORD_all(b->code, "(", compile_arguments(env, ast, fn->args, new(arg_ast_t, .value=value)), ")");
        }

        if (t->tag == IntType || t->tag == NumType)
            return CORD_all("-(", compile(env, value), ")");

        code_err(ast, "I don't know how to get the negative value of type %T", t);

    }
    case HeapAllocate: return CORD_asprintf("heap(%r)", compile(env, Match(ast, HeapAllocate)->value));
    case StackReference: {
        ast_t *subject = Match(ast, StackReference)->value;
        if (can_be_mutated(env, subject))
            return CORD_all("(&", compile_lvalue(env, subject), ")");
        else
            code_err(subject, "This subject can't be mutated!");
    }
    case Optional: {
        ast_t *value = Match(ast, Optional)->value;
        CORD value_code = compile(env, value);
        return promote_to_optional(get_type(env, value), value_code);
    }
    case BinaryOp: {
        auto binop = Match(ast, BinaryOp);
        CORD method_call = compile_math_method(env, binop->op, binop->lhs, binop->rhs, NULL);
        if (method_call != CORD_EMPTY)
            return method_call;

        CORD lhs = compile(env, binop->lhs);
        CORD rhs = compile(env, binop->rhs);

        type_t *lhs_t = get_type(env, binop->lhs);
        type_t *rhs_t = get_type(env, binop->rhs);
        type_t *operand_t;
        if (promote(env, &rhs, rhs_t, lhs_t))
            operand_t = lhs_t;
        else if (promote(env, &lhs, lhs_t, rhs_t))
            operand_t = rhs_t;
        else
            code_err(ast, "I can't do operations between %T and %T", lhs_t, rhs_t);

        switch (binop->op) {
        case BINOP_POWER: {
            if (operand_t->tag != NumType)
                code_err(ast, "Exponentiation is only supported for Num types");
            if (operand_t->tag == NumType && Match(operand_t, NumType)->bits == TYPE_NBITS32)
                return CORD_all("powf(", lhs, ", ", rhs, ")");
            else
                return CORD_all("pow(", lhs, ", ", rhs, ")");
        }
        case BINOP_MULT: {
            if (operand_t->tag != IntType && operand_t->tag != NumType)
                code_err(ast, "Math operations are only supported for numeric types");
            return CORD_all("(", lhs, " * ", rhs, ")");
        }
        case BINOP_DIVIDE: {
            if (operand_t->tag != IntType && operand_t->tag != NumType)
                code_err(ast, "Math operations are only supported for numeric types");
            return CORD_all("(", lhs, " / ", rhs, ")");
        }
        case BINOP_MOD: {
            if (operand_t->tag != IntType && operand_t->tag != NumType)
                code_err(ast, "Math operations are only supported for numeric types");
            return CORD_all("(", lhs, " % ", rhs, ")");
        }
        case BINOP_MOD1: {
            if (operand_t->tag != IntType && operand_t->tag != NumType)
                code_err(ast, "Math operations are only supported for numeric types");
            return CORD_all("((((", lhs, ")-1) % (", rhs, ")) + 1)");
        }
        case BINOP_PLUS: {
            if (operand_t->tag != IntType && operand_t->tag != NumType)
                code_err(ast, "Math operations are only supported for numeric types");
            return CORD_all("(", lhs, " + ", rhs, ")");
        }
        case BINOP_MINUS: {
            if (operand_t->tag != IntType && operand_t->tag != NumType)
                code_err(ast, "Math operations are only supported for numeric types");
            return CORD_all("(", lhs, " - ", rhs, ")");
        }
        case BINOP_LSHIFT: {
            if (operand_t->tag != IntType && operand_t->tag != NumType)
                code_err(ast, "Math operations are only supported for numeric types");
            return CORD_all("(", lhs, " << ", rhs, ")");
        }
        case BINOP_RSHIFT: {
            if (operand_t->tag != IntType && operand_t->tag != NumType)
                code_err(ast, "Math operations are only supported for numeric types");
            return CORD_all("(", lhs, " >> ", rhs, ")");
        }
        case BINOP_EQ: {
            switch (operand_t->tag) {
            case BigIntType:
                return CORD_all("Int$equal_value(", lhs, ", ", rhs, ")");
            case BoolType: case IntType: case NumType: case PointerType: case FunctionType:
                return CORD_all("(", lhs, " == ", rhs, ")");
            default:
                return CORD_asprintf("generic_equal(stack(%r), stack(%r), %r)", lhs, rhs, compile_type_info(env, operand_t));
            }
        }
        case BINOP_NE: {
            switch (operand_t->tag) {
            case BigIntType:
                return CORD_all("!Int$equal_value(", lhs, ", ", rhs, ")");
            case BoolType: case IntType: case NumType: case PointerType: case FunctionType:
                return CORD_all("(", lhs, " != ", rhs, ")");
            default:
                return CORD_asprintf("!generic_equal(stack(%r), stack(%r), %r)", lhs, rhs, compile_type_info(env, operand_t));
            }
        }
        case BINOP_LT: {
            switch (operand_t->tag) {
            case BigIntType:
                return CORD_all("(Int$compare_value(", lhs, ", ", rhs, ") < 0)");
            case BoolType: case IntType: case NumType: case PointerType: case FunctionType:
                return CORD_all("(", lhs, " < ", rhs, ")");
            default:
                return CORD_asprintf("(generic_compare(stack(%r), stack(%r), %r) < 0)", lhs, rhs, compile_type_info(env, operand_t));
            }
        }
        case BINOP_LE: {
            switch (operand_t->tag) {
            case BigIntType:
                return CORD_all("(Int$compare_value(", lhs, ", ", rhs, ") <= 0)");
            case BoolType: case IntType: case NumType: case PointerType: case FunctionType:
                return CORD_all("(", lhs, " <= ", rhs, ")");
            default:
                return CORD_asprintf("(generic_compare(stack(%r), stack(%r), %r) <= 0)", lhs, rhs, compile_type_info(env, operand_t));
            }
        }
        case BINOP_GT: {
            switch (operand_t->tag) {
            case BigIntType:
                return CORD_all("(Int$compare_value(", lhs, ", ", rhs, ") > 0)");
            case BoolType: case IntType: case NumType: case PointerType: case FunctionType:
                return CORD_all("(", lhs, " > ", rhs, ")");
            default:
                return CORD_asprintf("(generic_compare(stack(%r), stack(%r), %r) > 0)", lhs, rhs, compile_type_info(env, operand_t));
            }
        }
        case BINOP_GE: {
            switch (operand_t->tag) {
            case BigIntType:
                return CORD_all("(Int$compare_value(", lhs, ", ", rhs, ") >= 0)");
            case BoolType: case IntType: case NumType: case PointerType: case FunctionType:
                return CORD_all("(", lhs, " >= ", rhs, ")");
            default:
                return CORD_asprintf("(generic_compare(stack(%r), stack(%r), %r) >= 0)", lhs, rhs, compile_type_info(env, operand_t));
            }
        }
        case BINOP_AND: {
            if (operand_t->tag == BoolType)
                return CORD_all("(", lhs, " && ", rhs, ")");
            else if (operand_t->tag == IntType)
                return CORD_all("(", lhs, " & ", rhs, ")");
            else
                code_err(ast, "Boolean operators are only supported for Bool and integer types");
        }
        case BINOP_CMP: {
            return CORD_all("generic_compare(stack(", lhs, "), stack(", rhs, "), ", compile_type_info(env, operand_t), ")");
        }
        case BINOP_OR: {
            if (operand_t->tag == BoolType)
                return CORD_all("(", lhs, " || ", rhs, ")");
            else if (operand_t->tag == IntType)
                return CORD_all("(", lhs, " | ", rhs, ")");
            else
                code_err(ast, "Boolean operators are only supported for Bool and integer types");
        }
        case BINOP_XOR: {
            if (operand_t->tag == BoolType || operand_t->tag == IntType)
                return CORD_all("(", lhs, " ^ ", rhs, ")");
            else
                code_err(ast, "Boolean operators are only supported for Bool and integer types");
        }
        case BINOP_CONCAT: {
            switch (operand_t->tag) {
            case TextType: {
                const char *lang = Match(operand_t, TextType)->lang; 
                if (streq(lang, "Path"))
                    return CORD_all("Path$concat(", lhs, ", ", rhs, ")");
                return CORD_all("Text$concat(", lhs, ", ", rhs, ")");
            }
            case ArrayType: {
                CORD padded_item_size = CORD_asprintf("%ld", padded_type_size(Match(operand_t, ArrayType)->item_type));
                return CORD_all("Array$concat(", lhs, ", ", rhs, ", ", padded_item_size, ")");
            }
            default:
                code_err(ast, "Concatenation isn't supported for %T types", operand_t);
            }
        }
        default: break;
        }
        code_err(ast, "unimplemented binop");
    }
    case TextLiteral: {
        CORD literal = Match(ast, TextLiteral)->cord; 
        if (literal == CORD_EMPTY)
            return "Text(\"\")";

        if (string_literal_is_all_ascii(literal))
            return CORD_all("Text(", compile_string_literal(literal), ")");
        else
            return CORD_all("Text$from_str(", compile_string_literal(literal), ")");
    }
    case TextJoin: {
        const char *lang = Match(ast, TextJoin)->lang;

        type_t *text_t = lang ? Table$str_get(*env->types, lang) : TEXT_TYPE;
        if (!text_t || text_t->tag != TextType)
            code_err(ast, "%s is not a valid text language name", lang);

        CORD lang_constructor = !lang ? "Text"
            : CORD_all(namespace_prefix(Match(text_t, TextType)->env->libname, Match(text_t, TextType)->env->namespace->parent), lang);

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
                if (chunk->ast->tag == TextLiteral) {
                    chunk_code = compile(env, chunk->ast);
                } else if (chunk_t->tag == TextType && streq(Match(chunk_t, TextType)->lang, lang)) {
                    binding_t *esc = get_lang_escape_function(env, lang, chunk_t);
                    if (esc) {
                        arg_t *arg_spec = Match(esc->type, FunctionType)->args;
                        arg_ast_t *args = new(arg_ast_t, .value=chunk->ast);
                        chunk_code = CORD_all(esc->code, "(", compile_arguments(env, ast, arg_spec, args), ")");
                    } else {
                        chunk_code = compile(env, chunk->ast);
                    }
                } else if (lang) {
                    binding_t *esc = get_lang_escape_function(env, lang, chunk_t);
                    if (!esc)
                        code_err(chunk->ast, "I don't know how to convert %T to %T", chunk_t, text_t);

                    arg_t *arg_spec = Match(esc->type, FunctionType)->args;
                    arg_ast_t *args = new(arg_ast_t, .value=chunk->ast);
                    chunk_code = CORD_all(esc->code, "(", compile_arguments(env, ast, arg_spec, args), ")");
                } else {
                    chunk_code = compile_string(env, chunk->ast, "no");
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
            bind_statement(env, stmt->ast);
            if (stmt->next) {
                code = CORD_all(code, compile_statement(env, stmt->ast), "\n");
            } else {
                // TODO: put defer after evaluating block expression
                for (deferral_t *deferred = env->deferred; deferred && deferred != prev_deferred; deferred = deferred->next) {
                    code = CORD_all(code, compile_statement(deferred->defer_env, deferred->block));
                }
                code = CORD_all(code, compile(env, stmt->ast), ";\n");
            }
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
        set_binding(expr_env, key_name, new(binding_t, .type=t, .code="ternary$lhs"));
        CORD lhs_key = compile(expr_env, key);

        set_binding(expr_env, key_name, new(binding_t, .type=t, .code="ternary$rhs"));
        CORD rhs_key = compile(expr_env, key);

        type_t *key_t = get_type(expr_env, key);
        CORD comparison;
        if (key_t->tag == BigIntType)
            comparison = CORD_all("(Int$compare_value(", lhs_key, ", ", rhs_key, ")", (ast->tag == Min ? "<=" : ">="), "0)");
        else if (key_t->tag == IntType || key_t->tag == NumType || key_t->tag == BoolType || key_t->tag == PointerType)
            comparison = CORD_all("((", lhs_key, ")", (ast->tag == Min ? "<=" : ">="), "(", rhs_key, "))");
        else
            comparison = CORD_all("generic_compare(stack(", lhs_key, "), stack(", rhs_key, "), ", compile_type_info(env, key_t), ")",
                                  (ast->tag == Min ? "<=" : ">="), "0");

        return CORD_all(
            "({\n",
            compile_type(t), " ternary$lhs = ", compile(env, lhs), ", ternary$rhs = ", compile(env, rhs), ";\n",
            comparison, " ? ternary$lhs : ternary$rhs;\n"
            "})");
    }
    case Array: {
        type_t *array_type = get_type(env, ast);
        if (padded_type_size(Match(array_type, ArrayType)->item_type) > ARRAY_MAX_STRIDE)
            code_err(ast, "This array holds items that take up %ld bytes, but the maximum supported size is %ld bytes. Consider using an array of pointers instead.",
                     padded_type_size(Match(array_type, ArrayType)->item_type), ARRAY_MAX_STRIDE);

        auto array = Match(ast, Array);
        if (!array->items)
            return "(Array_t){.length=0}";

        int64_t n = 0;
        for (ast_list_t *item = array->items; item; item = item->next) {
            ++n;
            if (item->ast->tag == Comprehension)
                goto array_comprehension;
        }

        {
            type_t *item_type = Match(array_type, ArrayType)->item_type;
            CORD code = CORD_all("TypedArrayN(", compile_type(item_type), CORD_asprintf(", %ld", n));
            for (ast_list_t *item = array->items; item; item = item->next)
                code = CORD_all(code, ", ", compile(env, item->ast));
            return CORD_cat(code, ")");
        }

      array_comprehension:
        {
            env_t *scope = fresh_scope(env);
            static int64_t comp_num = 1;
            scope->comprehension_var = heap_strf("arr$%ld", comp_num++);
            CORD code = CORD_all("({ Array_t ", scope->comprehension_var, " = {};");
            set_binding(scope, scope->comprehension_var, new(binding_t, .type=array_type, .code=scope->comprehension_var));
            for (ast_list_t *item = array->items; item; item = item->next) {
                if (item->ast->tag == Comprehension) {
                    code = CORD_all(code, "\n", compile_statement(scope, item->ast));
                } else {
                    CORD insert = compile_statement(
                        scope, WrapAST(item->ast, MethodCall, .name="insert", .self=FakeAST(StackReference, FakeAST(Var, scope->comprehension_var)),
                                       .args=new(arg_ast_t, .value=item->ast)));
                    code = CORD_all(code, "\n", insert);
                }
            }
            code = CORD_all(code, " ", scope->comprehension_var, "; })");
            return code;
        }
    }
    case Channel: {
        auto chan = Match(ast, Channel);
        type_t *item_t = parse_type_ast(env, chan->item_type);
        if (!can_send_over_channel(item_t))
            code_err(ast, "This item type can't be sent over a channel because it contains reference to memory that may not be thread-safe.");
        if (chan->max_size) {
            CORD max_size = compile(env, chan->max_size);
            if (!promote(env, &max_size, get_type(env, chan->max_size), INT_TYPE))
                code_err(chan->max_size, "This value must be an integer, not %T", get_type(env, chan->max_size));
            return CORD_all("Channel$new(", max_size, ")");
        } else {
            return "Channel$new(I(INT32_MAX))";
        }
    }
    case Table: {
        auto table = Match(ast, Table);
        if (!table->entries) {
            CORD code = "((Table_t){";
            if (table->fallback)
                code = CORD_all(code, ".fallback=heap(", compile(env, table->fallback),")");
            return CORD_cat(code, "})");
        }

        type_t *table_type = get_type(env, ast);
        type_t *key_t = Match(table_type, TableType)->key_type;
        type_t *value_t = Match(table_type, TableType)->value_type;

        for (ast_list_t *entry = table->entries; entry; entry = entry->next) {
            if (entry->ast->tag == Comprehension)
                goto table_comprehension;
        }
           
        { // No comprehension:
            CORD code = CORD_all("Table(",
                                 compile_type(key_t), ", ",
                                 compile_type(value_t), ", ",
                                 compile_type_info(env, key_t), ", ",
                                 compile_type_info(env, value_t));
            if (table->fallback)
                code = CORD_all(code, ", /*fallback:*/ heap(", compile(env, table->fallback), ")");
            else
                code = CORD_all(code, ", /*fallback:*/ NULL");

            size_t n = 0;
            for (ast_list_t *entry = table->entries; entry; entry = entry->next)
                ++n;
            CORD_appendf(&code, ", %zu", n);

            for (ast_list_t *entry = table->entries; entry; entry = entry->next) {
                auto e = Match(entry->ast, TableEntry);
                code = CORD_all(code, ",\n\t{", compile(env, e->key), ", ", compile(env, e->value), "}");
            }
            return CORD_cat(code, ")");
        }

      table_comprehension:
        {
            static int64_t comp_num = 1;
            env_t *scope = fresh_scope(env);
            scope->comprehension_var = heap_strf("table$%ld", comp_num++);

            CORD code = CORD_all("({ Table_t ", scope->comprehension_var, " = {");
            if (table->fallback)
                code = CORD_all(code, ".fallback=heap(", compile(env, table->fallback), "), ");

            code = CORD_cat(code, "};");

            set_binding(scope, scope->comprehension_var, new(binding_t, .type=table_type, .code=scope->comprehension_var));
            for (ast_list_t *entry = table->entries; entry; entry = entry->next) {
                if (entry->ast->tag == Comprehension) {
                    code = CORD_all(code, "\n", compile_statement(scope, entry->ast));
                } else {
                    auto e = Match(entry->ast, TableEntry);
                    CORD set = compile_statement(
                        scope, WrapAST(entry->ast, MethodCall, .name="set", .self=FakeAST(StackReference, FakeAST(Var, scope->comprehension_var)),
                                       .args=new(arg_ast_t, .value=e->key, .next=new(arg_ast_t, .value=e->value))));
                    code = CORD_all(code, "\n", set);
                }
            }
            code = CORD_all(code, " ", scope->comprehension_var, "; })");
            return code;
        }

    }
    case Set: {
        auto set = Match(ast, Set);
        if (!set->items)
            return "((Table_t){})";

        type_t *set_type = get_type(env, ast);
        type_t *item_type = Match(set_type, SetType)->item_type;

        for (ast_list_t *item = set->items; item; item = item->next) {
            if (item->ast->tag == Comprehension)
                goto set_comprehension;
        }
           
        { // No comprehension:
            CORD code = CORD_all("Set(",
                                 compile_type(item_type), ", ",
                                 compile_type_info(env, item_type));

            size_t n = 0;
            for (ast_list_t *item = set->items; item; item = item->next)
                ++n;
            CORD_appendf(&code, ", %zu", n);

            for (ast_list_t *item = set->items; item; item = item->next) {
                code = CORD_all(code, ",\n\t", compile(env, item->ast));
            }
            return CORD_cat(code, ")");
        }

      set_comprehension:
        {
            static int64_t comp_num = 1;
            env_t *scope = fresh_scope(env);
            scope->comprehension_var = heap_strf("set$%ld", comp_num++);

            CORD code = CORD_all("({ Table_t ", scope->comprehension_var, " = {};");
            set_binding(scope, scope->comprehension_var, new(binding_t, .type=set_type, .code=scope->comprehension_var));
            for (ast_list_t *item = set->items; item; item = item->next) {
                if (item->ast->tag == Comprehension) {
                    code = CORD_all(code, "\n", compile_statement(scope, item->ast));
                } else {
                    CORD add_item = compile_statement(
                        scope, WrapAST(item->ast, MethodCall, .name="add", .self=FakeAST(StackReference, FakeAST(Var, scope->comprehension_var)),
                                       .args=new(arg_ast_t, .value=item->ast)));
                    code = CORD_all(code, "\n", add_item);
                }
            }
            code = CORD_all(code, " ", scope->comprehension_var, "; })");
            return code;
        }

    }
    case Comprehension: {
        ast_t *base = Match(ast, Comprehension)->expr;
        while (base->tag == Comprehension)
            base = Match(ast, Comprehension)->expr;
        if (base->tag == TableEntry)
            return compile(env, WrapAST(ast, Table, .entries=new(ast_list_t, .ast=ast)));
        else
            return compile(env, WrapAST(ast, Array, .items=new(ast_list_t, .ast=ast)));
    }
    case Lambda: {
        auto lambda = Match(ast, Lambda);
        CORD name = CORD_asprintf("%rlambda$%ld", namespace_prefix(env->libname, env->namespace), lambda->id);

        env_t *body_scope = fresh_scope(env);
        for (arg_ast_t *arg = lambda->args; arg; arg = arg->next) {
            type_t *arg_type = get_arg_ast_type(env, arg);
            set_binding(body_scope, arg->name, new(binding_t, .type=arg_type, .code=CORD_cat("$", arg->name)));
        }

        fn_ctx_t fn_ctx = (fn_ctx_t){
            .parent=env->fn_ctx,
            .closure_scope=env->locals,
            .closed_vars=new(Table_t),
        };
        body_scope->fn_ctx = &fn_ctx;
        body_scope->locals->fallback = env->globals;
        body_scope->deferred = NULL;
        type_t *ret_t = get_type(body_scope, lambda->body);
        if (ret_t->tag == ReturnType)
            ret_t = Match(ret_t, ReturnType)->ret;
        fn_ctx.return_type = ret_t;

        if (env->fn_ctx->closed_vars) {
            for (int64_t i = 1; i <= Table$length(*env->fn_ctx->closed_vars); i++) {
                struct { const char *name; binding_t *b; } *entry = Table$entry(*env->fn_ctx->closed_vars, i);
                set_binding(body_scope, entry->name, new(binding_t, .type=entry->b->type, .code=CORD_cat("userdata->", entry->name)));
                Table$str_set(fn_ctx.closed_vars, entry->name, entry->b);
            }
        }

        CORD code = CORD_all("static ", compile_type(ret_t), " ", name, "(");
        for (arg_ast_t *arg = lambda->args; arg; arg = arg->next) {
            type_t *arg_type = get_arg_ast_type(env, arg);
            code = CORD_all(code, compile_type(arg_type), " $", arg->name, ", ");
        }

        CORD args_typedef = compile_statement_typedefs(env, ast);
        env->code->local_typedefs = CORD_all(env->code->local_typedefs, args_typedef);

        Table_t *closed_vars = get_closed_vars(env, ast);
        CORD userdata;
        if (Table$length(*closed_vars) == 0) {
            code = CORD_cat(code, "void *userdata)");
            userdata = "NULL";
        } else {
            userdata = CORD_all("new(", name, "$userdata_t");
            for (int64_t i = 1; i <= Table$length(*closed_vars); i++) {
                struct { const char *name; binding_t *b; } *entry = Table$entry(*closed_vars, i);
                if (entry->b->type->tag == ModuleType)
                    continue;
                CORD binding_code = get_binding(env, entry->name)->code;
                if (entry->b->type->tag == ArrayType)
                    userdata = CORD_all(userdata, ", ARRAY_COPY(", binding_code, ")");
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
            bind_statement(body_scope, stmt->ast);
            if (stmt->next || ret_t->tag == VoidType || ret_t->tag == AbortType || get_type(body_scope, stmt->ast)->tag == ReturnType)
                body = CORD_all(body, compile_statement(body_scope, stmt->ast), "\n");
            else
                body = CORD_all(body, compile_statement(body_scope, FakeAST(Return, stmt->ast)), "\n");
        }
        if ((ret_t->tag == VoidType || ret_t->tag == AbortType) && body_scope->deferred)
            body = CORD_all(body, compile_statement(body_scope, FakeAST(Return)), "\n");

        env->code->funcs = CORD_all(env->code->funcs, code, " {\n", body, "\n}\n");
        return CORD_all("((Closure_t){", name, ", ", userdata, "})");
    }
    case MethodCall: {
        auto call = Match(ast, MethodCall);
        type_t *self_t = get_type(env, call->self);
        type_t *self_value_t = value_type(self_t);
        switch (self_value_t->tag) {
        case ArrayType: {
            // TODO: check for readonly
            type_t *item_t = Match(self_value_t, ArrayType)->item_type;
            CORD padded_item_size = CORD_asprintf("%ld", padded_type_size(item_t));
            if (streq(call->name, "insert")) {
                CORD self = compile_to_pointer_depth(env, call->self, 1, false);
                arg_t *arg_spec = new(arg_t, .name="item", .type=item_t,
                                      .next=new(arg_t, .name="at", .type=INT_TYPE, .default_val=FakeAST(Int, .str="0")));
                return CORD_all("Array$insert_value(", self, ", ", compile_arguments(env, ast, arg_spec, call->args), ", ",
                                padded_item_size, ")");
            } else if (streq(call->name, "insert_all")) {
                CORD self = compile_to_pointer_depth(env, call->self, 1, false);
                arg_t *arg_spec = new(arg_t, .name="items", .type=self_value_t,
                                      .next=new(arg_t, .name="at", .type=INT_TYPE, .default_val=FakeAST(Int, .str="0")));
                return CORD_all("Array$insert_all(", self, ", ", compile_arguments(env, ast, arg_spec, call->args), ", ",
                                padded_item_size, ")");
            } else if (streq(call->name, "remove_at")) {
                CORD self = compile_to_pointer_depth(env, call->self, 1, false);
                arg_t *arg_spec = new(arg_t, .name="index", .type=INT_TYPE, .default_val=FakeAST(Int, .str="-1"),
                                      .next=new(arg_t, .name="count", .type=INT_TYPE, .default_val=FakeAST(Int, .str="1")));
                return CORD_all("Array$remove_at(", self, ", ", compile_arguments(env, ast, arg_spec, call->args), ", ",
                                padded_item_size, ")");
            } else if (streq(call->name, "remove_item")) {
                CORD self = compile_to_pointer_depth(env, call->self, 1, false);
                arg_t *arg_spec = new(arg_t, .name="item", .type=item_t,
                                      .next=new(arg_t, .name="max_count", .type=INT_TYPE, .default_val=FakeAST(Int, .str="-1")));
                return CORD_all("Array$remove_item_value(", self, ", ", compile_arguments(env, ast, arg_spec, call->args), ", ",
                                compile_type_info(env, self_value_t), ")");
            } else if (streq(call->name, "random")) {
                CORD self = compile_to_pointer_depth(env, call->self, 0, false);
                (void)compile_arguments(env, ast, NULL, call->args);
                return CORD_all("Array$random_value(", self, ", ", compile_type(item_t), ")");
            } else if (streq(call->name, "has")) {
                CORD self = compile_to_pointer_depth(env, call->self, 0, false);
                arg_t *arg_spec = new(arg_t, .name="item", .type=item_t);
                return CORD_all("Array$has_value(", self, ", ", compile_arguments(env, ast, arg_spec, call->args), ", ",
                                compile_type_info(env, self_value_t), ")");
            } else if (streq(call->name, "sample")) {
                CORD self = compile_to_pointer_depth(env, call->self, 0, false);
                arg_t *arg_spec = new(arg_t, .name="count", .type=INT_TYPE,
                                      .next=new(arg_t, .name="weights", .type=Type(ArrayType, .item_type=Type(NumType)),
                                                .default_val=FakeAST(Array, .item_type=new(type_ast_t, .tag=VarTypeAST, .__data.VarTypeAST.name="Num"))));
                return CORD_all("Array$sample(", self, ", ", compile_arguments(env, ast, arg_spec, call->args), ", ",
                                padded_item_size, ")");
            } else if (streq(call->name, "shuffle")) {
                CORD self = compile_to_pointer_depth(env, call->self, 1, false);
                (void)compile_arguments(env, ast, NULL, call->args);
                return CORD_all("Array$shuffle(", self, ", ", padded_item_size, ")");
            } else if (streq(call->name, "shuffled")) {
                CORD self = compile_to_pointer_depth(env, call->self, 0, false);
                (void)compile_arguments(env, ast, NULL, call->args);
                return CORD_all("Array$shuffled(", self, ", ", padded_item_size, ")");
            } else if (streq(call->name, "sort") || streq(call->name, "sorted")) {
                CORD self = compile_to_pointer_depth(env, call->self, streq(call->name, "sort") ? 1 : 0, false);
                CORD comparison;
                if (call->args) {
                    type_t *item_ptr = Type(PointerType, .pointed=item_t, .is_stack=true, .is_readonly=true);
                    type_t *fn_t = Type(FunctionType, .args=new(arg_t, .name="x", .type=item_ptr, .next=new(arg_t, .name="y", .type=item_ptr)),
                                        .ret=Type(IntType, .bits=TYPE_IBITS32));
                    arg_t *arg_spec = new(arg_t, .name="by", .type=Type(ClosureType, .fn=fn_t));
                    comparison = compile_arguments(env, ast, arg_spec, call->args);
                } else {
                    comparison = CORD_all("((Closure_t){.fn=generic_compare, .userdata=(void*)", compile_type_info(env, item_t), "})");
                }
                return CORD_all("Array$", call->name, "(", self, ", ", comparison, ", ", padded_item_size, ")");
            } else if (streq(call->name, "heapify")) {
                CORD self = compile_to_pointer_depth(env, call->self, 1, false);
                CORD comparison;
                if (call->args) {
                    type_t *item_ptr = Type(PointerType, .pointed=item_t, .is_stack=true);
                    type_t *fn_t = Type(FunctionType, .args=new(arg_t, .name="x", .type=item_ptr, .next=new(arg_t, .name="y", .type=item_ptr)),
                                        .ret=Type(IntType, .bits=TYPE_IBITS32));
                    arg_t *arg_spec = new(arg_t, .name="by", .type=Type(ClosureType, .fn=fn_t));
                    comparison = compile_arguments(env, ast, arg_spec, call->args);
                } else {
                    comparison = CORD_all("((Closure_t){.fn=generic_compare, .userdata=(void*)", compile_type_info(env, item_t), "})");
                }
                return CORD_all("Array$heapify(", self, ", ", comparison, ", ", padded_item_size, ")");
            } else if (streq(call->name, "heap_push")) {
                CORD self = compile_to_pointer_depth(env, call->self, 1, false);
                type_t *item_ptr = Type(PointerType, .pointed=item_t, .is_stack=true);
                type_t *fn_t = Type(FunctionType, .args=new(arg_t, .name="x", .type=item_ptr, .next=new(arg_t, .name="y", .type=item_ptr)),
                                    .ret=Type(IntType, .bits=TYPE_IBITS32));
                ast_t *default_cmp = FakeAST(InlineCCode,
                                             .code=CORD_all("((Closure_t){.fn=generic_compare, .userdata=(void*)",
                                                            compile_type_info(env, item_t), "})"),
                                             .type=NewTypeAST(NULL, NULL, NULL, FunctionTypeAST));
                arg_t *arg_spec = new(arg_t, .name="item", .type=item_t,
                                      .next=new(arg_t, .name="by", .type=Type(ClosureType, .fn=fn_t), .default_val=default_cmp));
                CORD arg_code = compile_arguments(env, ast, arg_spec, call->args);
                return CORD_all("Array$heap_push_value(", self, ", ", arg_code, ", ", padded_item_size, ")");
            } else if (streq(call->name, "heap_pop")) {
                CORD self = compile_to_pointer_depth(env, call->self, 1, false);
                type_t *item_ptr = Type(PointerType, .pointed=item_t, .is_stack=true);
                type_t *fn_t = Type(FunctionType, .args=new(arg_t, .name="x", .type=item_ptr, .next=new(arg_t, .name="y", .type=item_ptr)),
                                    .ret=Type(IntType, .bits=TYPE_IBITS32));
                ast_t *default_cmp = FakeAST(InlineCCode,
                                             .code=CORD_all("((Closure_t){.fn=generic_compare, .userdata=(void*)",
                                                            compile_type_info(env, item_t), "})"),
                                             .type=NewTypeAST(NULL, NULL, NULL, FunctionTypeAST));
                arg_t *arg_spec = new(arg_t, .name="by", .type=Type(ClosureType, .fn=fn_t), .default_val=default_cmp);
                CORD arg_code = compile_arguments(env, ast, arg_spec, call->args);
                return CORD_all("Array$heap_pop_value(", self, ", ", arg_code, ", ", padded_item_size, ", ", compile_type(item_t), ")");
            } else if (streq(call->name, "binary_search")) {
                CORD self = compile_to_pointer_depth(env, call->self, 0, false);
                type_t *item_ptr = Type(PointerType, .pointed=item_t, .is_stack=true);
                type_t *fn_t = Type(FunctionType, .args=new(arg_t, .name="x", .type=item_ptr, .next=new(arg_t, .name="y", .type=item_ptr)),
                                    .ret=Type(IntType, .bits=TYPE_IBITS32));
                ast_t *default_cmp = FakeAST(InlineCCode,
                                             .code=CORD_all("((Closure_t){.fn=generic_compare, .userdata=(void*)",
                                                            compile_type_info(env, item_t), "})"),
                                             .type=NewTypeAST(NULL, NULL, NULL, FunctionTypeAST));
                arg_t *arg_spec = new(arg_t, .name="target", .type=item_t,
                                      .next=new(arg_t, .name="by", .type=Type(ClosureType, .fn=fn_t), .default_val=default_cmp));
                CORD arg_code = compile_arguments(env, ast, arg_spec, call->args);
                return CORD_all("Array$binary_search_value(", self, ", ", arg_code, ")");
            } else if (streq(call->name, "clear")) {
                CORD self = compile_to_pointer_depth(env, call->self, 1, false);
                (void)compile_arguments(env, ast, NULL, call->args);
                return CORD_all("Array$clear(", self, ")");
            } else if (streq(call->name, "find")) {
                CORD self = compile_to_pointer_depth(env, call->self, 0, false);
                arg_t *arg_spec = new(arg_t, .name="item", .type=item_t);
                return CORD_all("Array$find_value(", self, ", ", compile_arguments(env, ast, arg_spec, call->args),
                                ", ", compile_type_info(env, self_value_t), ")");
            } else if (streq(call->name, "first")) {
                CORD self = compile_to_pointer_depth(env, call->self, 0, false);
                type_t *item_ptr = Type(PointerType, .pointed=item_t, .is_stack=true);
                type_t *predicate_type = Type(
                    ClosureType, .fn=Type(FunctionType, .args=new(arg_t, .name="item", .type=item_ptr), .ret=Type(BoolType)));
                arg_t *arg_spec = new(arg_t, .name="predicate", .type=predicate_type);
                return CORD_all("Array$first(", self, ", ", compile_arguments(env, ast, arg_spec, call->args), ")");
            } else if (streq(call->name, "from")) {
                CORD self = compile_to_pointer_depth(env, call->self, 0, true);
                arg_t *arg_spec = new(arg_t, .name="first", .type=INT_TYPE);
                return CORD_all("Array$from(", self, ", ", compile_arguments(env, ast, arg_spec, call->args), ")");
            } else if (streq(call->name, "to")) {
                CORD self = compile_to_pointer_depth(env, call->self, 0, true);
                arg_t *arg_spec = new(arg_t, .name="last", .type=INT_TYPE);
                return CORD_all("Array$to(", self, ", ", compile_arguments(env, ast, arg_spec, call->args), ")");
            } else if (streq(call->name, "by")) {
                CORD self = compile_to_pointer_depth(env, call->self, 0, true);
                arg_t *arg_spec = new(arg_t, .name="stride", .type=INT_TYPE);
                return CORD_all("Array$by(", self, ", ", compile_arguments(env, ast, arg_spec, call->args), ", ", padded_item_size, ")");
            } else if (streq(call->name, "reversed")) {
                CORD self = compile_to_pointer_depth(env, call->self, 0, true);
                (void)compile_arguments(env, ast, NULL, call->args);
                return CORD_all("Array$reversed(", self, ", ", padded_item_size, ")");
            } else if (streq(call->name, "unique")) {
                CORD self = compile_to_pointer_depth(env, call->self, 0, false);
                (void)compile_arguments(env, ast, NULL, call->args);
                return CORD_all("Table$from_entries(", self, ", Set$info(", compile_type_info(env, item_t), "))");
            } else if (streq(call->name, "counts")) {
                CORD self = compile_to_pointer_depth(env, call->self, 0, false);
                (void)compile_arguments(env, ast, NULL, call->args);
                return CORD_all("Array$counts(", self, ", ", compile_type_info(env, self_value_t), ")");
            } else code_err(ast, "There is no '%s' method for arrays", call->name);
        }
        case SetType: {
            auto set = Match(self_value_t, SetType);
            if (streq(call->name, "has")) {
                CORD self = compile_to_pointer_depth(env, call->self, 0, false);
                arg_t *arg_spec = new(arg_t, .name="key", .type=set->item_type);
                return CORD_all("Table$has_value(", self, ", ", compile_arguments(env, ast, arg_spec, call->args), ", ",
                                compile_type_info(env, self_value_t), ")");
            } else if (streq(call->name, "add")) {
                CORD self = compile_to_pointer_depth(env, call->self, 1, false);
                arg_t *arg_spec = new(arg_t, .name="item", .type=set->item_type);
                return CORD_all("Table$set_value(", self, ", ", compile_arguments(env, ast, arg_spec, call->args), ", NULL, ",
                                compile_type_info(env, self_value_t), ")");
            } else if (streq(call->name, "add_all")) {
                arg_t *arg_spec = new(arg_t, .name="items", .type=Type(ArrayType, .item_type=Match(self_value_t, SetType)->item_type));
                return CORD_all("({ Table_t *set = ", compile_to_pointer_depth(env, call->self, 1, false), "; ",
                                "Array_t to_add = ", compile_arguments(env, ast, arg_spec, call->args), "; ",
                                "for (int64_t i = 0; i < to_add.length; i++)\n"
                                "Table$set(set, to_add.data + i*to_add.stride, NULL, ", compile_type_info(env, self_value_t), ");\n",
                                "(void)0; })");
            } else if (streq(call->name, "remove")) {
                CORD self = compile_to_pointer_depth(env, call->self, 1, false);
                arg_t *arg_spec = new(arg_t, .name="item", .type=set->item_type);
                return CORD_all("Table$remove_value(", self, ", ", compile_arguments(env, ast, arg_spec, call->args), ", ",
                                compile_type_info(env, self_value_t), ")");
            } else if (streq(call->name, "remove_all")) {
                arg_t *arg_spec = new(arg_t, .name="items", .type=Type(ArrayType, .item_type=Match(self_value_t, SetType)->item_type));
                return CORD_all("({ Table_t *set = ", compile_to_pointer_depth(env, call->self, 1, false), "; ",
                                "Array_t to_add = ", compile_arguments(env, ast, arg_spec, call->args), "; ",
                                "for (int64_t i = 0; i < to_add.length; i++)\n"
                                "Table$remove(set, to_add.data + i*to_add.stride, ", compile_type_info(env, self_value_t), ");\n",
                                "(void)0; })");
            } else if (streq(call->name, "clear")) {
                CORD self = compile_to_pointer_depth(env, call->self, 1, false);
                (void)compile_arguments(env, ast, NULL, call->args);
                return CORD_all("Table$clear(", self, ")");
            } else if (streq(call->name, "with")) {
                CORD self = compile_to_pointer_depth(env, call->self, 0, false);
                arg_t *arg_spec = new(arg_t, .name="other", .type=self_value_t);
                return CORD_all("Table$with(", self, ", ", compile_arguments(env, ast, arg_spec, call->args),
                                ", ", compile_type_info(env, self_value_t), ")");
            } else if (streq(call->name, "overlap")) {
                CORD self = compile_to_pointer_depth(env, call->self, 0, false);
                arg_t *arg_spec = new(arg_t, .name="other", .type=self_value_t);
                return CORD_all("Table$overlap(", self, ", ", compile_arguments(env, ast, arg_spec, call->args),
                                ", ", compile_type_info(env, self_value_t), ")");
            } else if (streq(call->name, "without")) {
                CORD self = compile_to_pointer_depth(env, call->self, 0, false);
                arg_t *arg_spec = new(arg_t, .name="other", .type=self_value_t);
                return CORD_all("Table$without(", self, ", ", compile_arguments(env, ast, arg_spec, call->args),
                                ", ", compile_type_info(env, self_value_t), ")");
            } else if (streq(call->name, "is_subset_of")) {
                CORD self = compile_to_pointer_depth(env, call->self, 0, false);
                arg_t *arg_spec = new(arg_t, .name="other", .type=self_value_t,
                                      .next=new(arg_t, .name="strict", .type=Type(BoolType), .default_val=FakeAST(Bool, false)));
                return CORD_all("Table$is_subset_of(", self, ", ", compile_arguments(env, ast, arg_spec, call->args),
                                ", ", compile_type_info(env, self_value_t), ")");
            } else if (streq(call->name, "is_superset_of")) {
                CORD self = compile_to_pointer_depth(env, call->self, 0, false);
                arg_t *arg_spec = new(arg_t, .name="other", .type=self_value_t,
                                      .next=new(arg_t, .name="strict", .type=Type(BoolType), .default_val=FakeAST(Bool, false)));
                return CORD_all("Table$is_superset_of(", self, ", ", compile_arguments(env, ast, arg_spec, call->args),
                                ", ", compile_type_info(env, self_value_t), ")");
            } else code_err(ast, "There is no '%s' method for tables", call->name);
        }
        case ChannelType: {
            type_t *item_t = Match(self_value_t, ChannelType)->item_type;
            CORD padded_item_size = CORD_asprintf("%ld", padded_type_size(item_t));
            arg_t *front_default_end = new(arg_t, .name="front", .type=Type(BoolType), .default_val=FakeAST(Bool, false));
            arg_t *front_default_start = new(arg_t, .name="front", .type=Type(BoolType), .default_val=FakeAST(Bool, true));
            if (streq(call->name, "give")) {
                CORD self = compile_to_pointer_depth(env, call->self, 0, false);
                arg_t *arg_spec = new(arg_t, .name="item", .type=item_t, .next=front_default_end);
                return CORD_all("Channel$give_value(", self, ", ", compile_arguments(env, ast, arg_spec, call->args), ", ",
                                padded_item_size, ")");
            } else if (streq(call->name, "give_all")) {
                CORD self = compile_to_pointer_depth(env, call->self, 0, false);
                arg_t *arg_spec = new(arg_t, .name="to_give", .type=Type(ArrayType, .item_type=item_t), .next=front_default_end);
                return CORD_all("Channel$give_all(", self, ", ", compile_arguments(env, ast, arg_spec, call->args), ", ",
                                padded_item_size, ")");
            } else if (streq(call->name, "get")) {
                CORD self = compile_to_pointer_depth(env, call->self, 0, false);
                arg_t *arg_spec = front_default_start;
                return CORD_all("Channel$get_value(", self, ", ", compile_arguments(env, ast, arg_spec, call->args),
                                ", ", compile_type(item_t), ", ", padded_item_size, ")");
            } else if (streq(call->name, "peek")) {
                CORD self = compile_to_pointer_depth(env, call->self, 0, false);
                arg_t *arg_spec = front_default_start;
                return CORD_all("Channel$peek_value(", self, ", ", compile_arguments(env, ast, arg_spec, call->args),
                                ", ", compile_type(item_t), ")");
            } else if (streq(call->name, "clear")) {
                CORD self = compile_to_pointer_depth(env, call->self, 0, false);
                (void)compile_arguments(env, ast, NULL, call->args);
                return CORD_all("Channel$clear(", self, ")");
            } else if (streq(call->name, "view")) {
                CORD self = compile_to_pointer_depth(env, call->self, 0, false);
                (void)compile_arguments(env, ast, NULL, call->args);
                return CORD_all("Channel$view(", self, ")");
            } else code_err(ast, "There is no '%s' method for channels", call->name);
        }
        case TableType: {
            auto table = Match(self_value_t, TableType);
            if (streq(call->name, "get")) {
                CORD self = compile_to_pointer_depth(env, call->self, 0, false);
                if (call->args->next) {
                    arg_t *arg_spec = new(arg_t, .name="key", .type=table->key_type, .next=new(arg_t, .name="default", .type=table->value_type));
                    return CORD_all("Table$get_value_or_default(", self, ", ", compile_type(table->key_type), ", ", compile_type(table->value_type), ", ",
                                    compile_arguments(env, ast, arg_spec, call->args), ", ", compile_type_info(env, self_value_t), ")");
                } else {
                    arg_t *arg_spec = new(arg_t, .name="key", .type=table->key_type);
                    file_t *f = ast->file;
                    return CORD_all("Table$get_value_or_fail(", self, ", ", compile_type(table->key_type), ", ", compile_type(table->value_type), ", ",
                                    compile_arguments(env, ast, arg_spec, call->args), ", ", compile_type_info(env, self_value_t), ", ",
                                    CORD_asprintf("%ld", (int64_t)(ast->start - f->text)), ", ",
                                    CORD_asprintf("%ld", (int64_t)(ast->end - f->text)),
                                    ")");
                }
            } else if (streq(call->name, "get_or_null")) {
                if (table->value_type->tag != PointerType)
                    code_err(ast, "The table method :get_or_null() is only supported for tables whose value type is a pointer, not %T", table->value_type);
                CORD self = compile_to_pointer_depth(env, call->self, 0, false);
                arg_t *arg_spec = new(arg_t, .name="key", .type=table->key_type);
                return CORD_all("Table$get_value_or_default(", self, ", ", compile_type(table->key_type), ", ", compile_type(table->value_type), ", ",
                                compile_arguments(env, ast, arg_spec, call->args), ", NULL, ", compile_type_info(env, self_value_t), ")");
            } else if (streq(call->name, "has")) {
                CORD self = compile_to_pointer_depth(env, call->self, 0, false);
                arg_t *arg_spec = new(arg_t, .name="key", .type=table->key_type);
                return CORD_all("Table$has_value(", self, ", ", compile_arguments(env, ast, arg_spec, call->args), ", ",
                                compile_type_info(env, self_value_t), ")");
            } else if (streq(call->name, "set")) {
                CORD self = compile_to_pointer_depth(env, call->self, 1, false);
                arg_t *arg_spec = new(arg_t, .name="key", .type=table->key_type,
                                      .next=new(arg_t, .name="value", .type=table->value_type));
                return CORD_all("Table$set_value(", self, ", ", compile_arguments(env, ast, arg_spec, call->args), ", ",
                                compile_type_info(env, self_value_t), ")");
            } else if (streq(call->name, "bump")) {
                CORD self = compile_to_pointer_depth(env, call->self, 1, false);
                if (!(table->value_type->tag == IntType || table->value_type->tag == NumType))
                    code_err(ast, "bump() is only supported for tables with numeric value types, not %T", self_value_t);
                ast_t *one = table->value_type->tag == IntType
                    ? FakeAST(Int, .str="1")
                    : FakeAST(Num, .n=1);
                arg_t *arg_spec = new(arg_t, .name="key", .type=table->key_type,
                                      .next=new(arg_t, .name="amount", .type=table->value_type, .default_val=one));
                return CORD_all("Table$bump(", self, ", ", compile_arguments(env, ast, arg_spec, call->args), ", ",
                                compile_type_info(env, self_value_t), ")");
            } else if (streq(call->name, "remove")) {
                CORD self = compile_to_pointer_depth(env, call->self, 1, false);
                arg_t *arg_spec = new(arg_t, .name="key", .type=table->key_type);
                return CORD_all("Table$remove_value(", self, ", ", compile_arguments(env, ast, arg_spec, call->args), ", ",
                                compile_type_info(env, self_value_t), ")");
            } else if (streq(call->name, "clear")) {
                CORD self = compile_to_pointer_depth(env, call->self, 1, false);
                (void)compile_arguments(env, ast, NULL, call->args);
                return CORD_all("Table$clear(", self, ")");
            } else if (streq(call->name, "sorted")) {
                CORD self = compile_to_pointer_depth(env, call->self, 0, false);
                (void)compile_arguments(env, ast, NULL, call->args);
                return CORD_all("Table$sorted(", self, ", ", compile_type_info(env, self_value_t), ")");
            } else code_err(ast, "There is no '%s' method for tables", call->name);
        }
        default: {
            auto methodcall = Match(ast, MethodCall);
            type_t *fn_t = get_method_type(env, methodcall->self, methodcall->name);
            arg_ast_t *args = new(arg_ast_t, .value=methodcall->self, .next=methodcall->args);
            binding_t *b = get_namespace_binding(env, methodcall->self, methodcall->name);
            if (!b) code_err(ast, "No such method");
            return CORD_all(b->code, "(", compile_arguments(env, ast, Match(fn_t, FunctionType)->args, args), ")");
        }
        }
    }
    case FunctionCall: {
        auto call = Match(ast, FunctionCall);
        type_t *fn_t = get_type(env, call->fn);
        if (fn_t->tag == FunctionType) {
            CORD fn = compile(env, call->fn);
            return CORD_all(fn, "(", compile_arguments(env, ast, Match(fn_t, FunctionType)->args, call->args), ")");
        } else if (fn_t->tag == TypeInfoType) {
            type_t *t = Match(fn_t, TypeInfoType)->type;
            if (t->tag == StructType) {
                // Struct constructor:
                fn_t = Type(FunctionType, .args=Match(t, StructType)->fields, .ret=t);
                return CORD_all("((", compile_type(t), "){", compile_arguments(env, ast, Match(fn_t, FunctionType)->args, call->args), "})");
            } else if (t->tag == NumType || t->tag == BigIntType) {
                if (!call->args) code_err(ast, "This constructor needs a value");
                type_t *actual = get_type(env, call->args->value);
                arg_t *args = new(arg_t, .name="i", .type=actual); // No truncation argument
                CORD arg_code = compile_arguments(env, ast, args, call->args);
                return CORD_all(type_to_cord(actual), "_to_", type_to_cord(t), "(", arg_code, ")");
            } else if (t->tag == IntType) {
                type_t *actual = get_type(env, call->args->value);
                arg_t *args = new(arg_t, .name="i", .type=actual, .next=new(arg_t, .name="truncate", .type=Type(BoolType),
                                                                            .default_val=FakeAST(Bool, false)));
                CORD arg_code = compile_arguments(env, ast, args, call->args);
                return CORD_all(type_to_cord(actual), "_to_", type_to_cord(t), "(", arg_code, ")");
            } else if (t->tag == TextType) {
                if (!call->args) code_err(ast, "This constructor needs a value");
                const char *lang = Match(t, TextType)->lang;
                if (lang) { // Escape for DSL
                    type_t *first_type = get_type(env, call->args->value);
                    if (type_eq(first_type, t))
                        return compile(env, call->args->value);

                    binding_t *esc = get_lang_escape_function(env, lang, first_type);
                    if (!esc)
                        code_err(ast, "I don't know how to convert %T to %T", first_type, t);

                    arg_t *arg_spec = Match(esc->type, FunctionType)->args;
                    return CORD_all(esc->code, "(", compile_arguments(env, ast, arg_spec, call->args), ")");
                } else {
                    // Text constructor:
                    if (!call->args || call->args->next)
                        code_err(call->fn, "This constructor takes exactly 1 argument");
                    type_t *actual = get_type(env, call->args->value);
                    if (type_eq(actual, t))
                        return compile(env, call->args->value);
                    return expr_as_text(env, compile(env, call->args->value), actual, "no");
                }
            } else if (t->tag == CStringType) {
                // C String constructor:
                if (!call->args || call->args->next)
                    code_err(call->fn, "This constructor takes exactly 1 argument");
                type_t *actual = get_type(env, call->args->value);
                return CORD_all("Text$as_c_string(", expr_as_text(env, compile(env, call->args->value), actual, "no"), ")");
            } else {
                code_err(call->fn, "This is not a type that has a constructor");
            }
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
            code_err(call->fn, "This is not a function, it's a %T", fn_t);
        }
    }
    case When: {
        auto original = Match(ast, When);
        ast_t *when_var = WrapAST(ast, Var, .name="when"); 
        when_clause_t *new_clauses = NULL;
        type_t *subject_t = get_type(env, original->subject);
        for (when_clause_t *clause = original->clauses; clause; clause = clause->next) {
            type_t *clause_type = get_clause_type(env, subject_t, clause);
            if (clause_type->tag == AbortType || clause_type->tag == ReturnType) {
                new_clauses = new(when_clause_t, .tag_name=clause->tag_name, .args=clause->args, .body=clause->body, .next=new_clauses);
            } else {
                ast_t *assign = WrapAST(clause->body, Assign,
                                        .targets=new(ast_list_t, .ast=when_var),
                                        .values=new(ast_list_t, .ast=clause->body));
                new_clauses = new(when_clause_t, .tag_name=clause->tag_name, .args=clause->args, .body=assign, .next=new_clauses);
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
        set_binding(when_env, "when", new(binding_t, .type=t, .code="when"));
        return CORD_all(
            "({ ", compile_declaration(t, "when"), ";\n",
            compile_statement(when_env, WrapAST(ast, When, .subject=original->subject, .clauses=new_clauses, .else_body=else_body)),
            "when; })");
    }
    case If: {
        auto if_ = Match(ast, If);
        if (!if_->else_body)
            code_err(ast, "'if' expressions can only be used if you also have an 'else' block");

        type_t *t = get_type(env, ast);
        if (t->tag == VoidType || t->tag == AbortType)
            code_err(ast, "This expression has a %T type, but it needs to have a real value", t);

        CORD condition;
        if (get_type(env, if_->condition)->tag == TextType)
            condition = CORD_all("(", compile(env, if_->condition), ").length");
        else
            condition = compile(env, if_->condition);

        type_t *true_type = get_type(env, if_->body);
        type_t *false_type = get_type(env, if_->else_body);
        if (true_type->tag == AbortType || true_type->tag == ReturnType)
            return CORD_all("({ if (", condition, ") ", compile_statement(env, if_->body),
                            "\n", compile(env, if_->else_body), "; })");
        else if (false_type->tag == AbortType || false_type->tag == ReturnType)
            return CORD_all("({ if (!(", condition, ")) ", compile_statement(env, if_->else_body),
                            "\n", compile(env, if_->body), "; })");
        else
            return CORD_all("((", condition, ") ? ",
                            compile(env, if_->body), " : ", compile(env, if_->else_body), ")");
    }
    case Reduction: {
        auto reduction = Match(ast, Reduction);
        type_t *t = get_type(env, ast);
        CORD code = CORD_all(
            "({ // Reduction:\n",
            compile_declaration(t, "reduction"), ";\n"
            "Bool_t is_first = yes;\n"
            );
        env_t *scope = fresh_scope(env);
        ast_t *result = FakeAST(Var, "$reduction");
        set_binding(scope, "$reduction", new(binding_t, .type=t, .code="reduction"));
        ast_t *empty = NULL;
        if (reduction->fallback) {
            type_t *fallback_type = get_type(scope, reduction->fallback);
            if (fallback_type->tag == AbortType || fallback_type->tag == ReturnType) {
                empty = reduction->fallback;
            } else {
                empty = FakeAST(Assign, .targets=new(ast_list_t, .ast=result), .values=new(ast_list_t, .ast=reduction->fallback));
            }
        } else {
            empty = FakeAST(
                InlineCCode, 
                CORD_asprintf("fail_source(%r, %ld, %ld, \"This collection was empty!\");\n",
                              CORD_quoted(ast->file->filename),
                              (long)(reduction->iter->start - reduction->iter->file->text),
                              (long)(reduction->iter->end - reduction->iter->file->text)));
        }
        ast_t *item = FakeAST(Var, "$iter_value");
        ast_t *body = FakeAST(InlineCCode, .code="{}"); // placeholder
        ast_t *loop = FakeAST(For, .vars=new(ast_list_t, .ast=item), .iter=reduction->iter, .body=body, .empty=empty);
        env_t *body_scope = for_scope(scope, loop);

        // For the special case of (or)/(and), we need to early out if we can:
        CORD early_out = CORD_EMPTY;
        if (t->tag == BoolType && reduction->combination->tag == BinaryOp) {
            auto binop = Match(reduction->combination, BinaryOp);
            if (binop->op == BINOP_AND)
                early_out = "if (!reduction) break;";
            else if (binop->op == BINOP_OR)
                early_out = "if (reduction) break;";
        }

        body->__data.InlineCCode.code = CORD_all(
            "if (is_first) {\n"
            "    reduction = ", compile(body_scope, item), ";\n"
            "    is_first = no;\n"
            "} else {\n"
            "    reduction = ", compile(body_scope, reduction->combination), ";\n",
            early_out,
            "}\n");
        code = CORD_all(code, compile_statement(scope, loop), "\nreduction;})");
        return code;
    }
    case FieldAccess: {
        auto f = Match(ast, FieldAccess);
        type_t *fielded_t = get_type(env, f->fielded);
        type_t *value_t = value_type(fielded_t);
        switch (value_t->tag) {
        case TypeInfoType: {
            auto info = Match(value_t, TypeInfoType);
            if (f->field[0] == '_') {
                for (Table_t *locals = env->locals; locals; locals = locals->fallback) {
                    if (locals == info->env->locals)
                        goto is_inside_type;
                }
                code_err(ast, "Fields that start with underscores are not accessible on types outside of the type definition.", f->field);
              is_inside_type:;
            }
            binding_t *b = get_binding(info->env, f->field);
            if (!b) code_err(ast, "I couldn't find the field '%s' on this type", f->field);
            if (!b->code) code_err(ast, "I couldn't figure out how to compile this field");
            return b->code;
        }
        case TextType: {
            const char *lang = Match(value_t, TextType)->lang; 
            if (lang && streq(f->field, "text_content")) {
                CORD text = compile_to_pointer_depth(env, f->fielded, 0, false);
                return CORD_all("((Text_t)", text, ")");
            } else if (streq(f->field, "length")) {
                return CORD_all("Int64_to_Int((", compile_to_pointer_depth(env, f->fielded, 0, false), ").length)");
            }
            code_err(ast, "There is no '%s' field on %T values", f->field, value_t);
        }
        case StructType: {
            for (arg_t *field = Match(value_t, StructType)->fields; field; field = field->next) {
                if (streq(field->name, f->field)) {
                    if (fielded_t->tag == PointerType) {
                        CORD fielded = compile_to_pointer_depth(env, f->fielded, 1, false);
                        return CORD_asprintf("(%r)->$%s", fielded, f->field);
                    } else {
                        CORD fielded = compile(env, f->fielded);
                        return CORD_asprintf("(%r).$%s", fielded, f->field);
                    }
                }
            }
            code_err(ast, "The field '%s' is not a valid field name of %T", f->field, value_t);
        }
        case EnumType: {
            auto e = Match(value_t, EnumType);
            for (tag_t *tag = e->tags; tag; tag = tag->next) {
                if (streq(f->field, tag->name)) {
                    CORD prefix = namespace_prefix(e->env->libname, e->env->namespace);
                    if (fielded_t->tag == PointerType) {
                        CORD fielded = compile_to_pointer_depth(env, f->fielded, 1, false);
                        return CORD_all("((", fielded, ")->tag == ", prefix, "tag$", tag->name, ")");
                    } else {
                        CORD fielded = compile(env, f->fielded);
                        return CORD_all("((", fielded, ").tag == ", prefix, "tag$", tag->name, ")");
                    }
                }
            }
            code_err(ast, "The field '%s' is not a valid tag name of %T", f->field, value_t);
        }
        case ArrayType: {
            if (streq(f->field, "length"))
                return CORD_all("Int64_to_Int((", compile_to_pointer_depth(env, f->fielded, 0, false), ").length)");
            code_err(ast, "There is no %s field on arrays", f->field);
        }
        case ChannelType: {
            if (streq(f->field, "max_size"))
                return CORD_all("Int64_to_Int((", compile_to_pointer_depth(env, f->fielded, 0, false), ")->max_size)");
            code_err(ast, "There is no %s field on arrays", f->field);
        }
        case SetType: {
            if (streq(f->field, "items"))
                return CORD_all("ARRAY_COPY((", compile_to_pointer_depth(env, f->fielded, 0, false), ").entries)");
            else if (streq(f->field, "length"))
                return CORD_all("Int64_to_Int((", compile_to_pointer_depth(env, f->fielded, 0, false), ").entries.length)");
            code_err(ast, "There is no '%s' field on sets", f->field);
        }
        case TableType: {
            if (streq(f->field, "length")) {
                return CORD_all("Int64_to_Int((", compile_to_pointer_depth(env, f->fielded, 0, false), ").entries.length)");
            } else if (streq(f->field, "keys")) {
                return CORD_all("ARRAY_COPY((", compile_to_pointer_depth(env, f->fielded, 0, false), ").entries)");
            } else if (streq(f->field, "values")) {
                auto table = Match(value_t, TableType);
                size_t offset = type_size(table->key_type);
                size_t align = type_align(table->value_type);
                if (align > 1 && offset % align > 0)
                    offset += align - (offset % align);
                return CORD_all("({ Array_t *entries = &(", compile_to_pointer_depth(env, f->fielded, 0, false), ").entries;\n"
                                "ARRAY_INCREF(*entries);\n"
                                "Array_t values = *entries;\n"
                                "values.data += ", CORD_asprintf("%zu", offset), ";\n"
                                "values; })");
            } else if (streq(f->field, "fallback")) {
                return CORD_all("({ Table_t *_fallback = (", compile_to_pointer_depth(env, f->fielded, 0, false), ").fallback; _fallback ? *_fallback : NULL_TABLE; })");
            }
            code_err(ast, "There is no '%s' field on tables", f->field);
        }
        case ModuleType: {
            const char *name = Match(value_t, ModuleType)->name;
            env_t *module_env = Table$str_get(*env->imports, name);
            return compile(module_env, WrapAST(ast, Var, f->field));
        }
        default:
            code_err(ast, "Field accesses are not supported on %T values", fielded_t);
        }
    }
    case Index: {
        auto indexing = Match(ast, Index);
        type_t *indexed_type = get_type(env, indexing->indexed);
        if (!indexing->index) {
            if (indexed_type->tag != PointerType)
                code_err(ast, "Only pointers can use the '[]' operator to dereference the entire value.");
            auto ptr = Match(indexed_type, PointerType);
            if (ptr->pointed->tag == ArrayType) {
                return CORD_all("({ Array_t *arr = ", compile(env, indexing->indexed), "; ARRAY_INCREF(*arr); *arr; })");
            } else if (ptr->pointed->tag == TableType || ptr->pointed->tag == SetType) {
                return CORD_all("({ Table_t *t = ", compile(env, indexing->indexed), "; TABLE_INCREF(*t); *t; })");
            } else {
                return CORD_all("*(", compile(env, indexing->indexed), ")");
            }
        }

        type_t *container_t = value_type(indexed_type);
        type_t *index_t = get_type(env, indexing->index);
        if (container_t->tag == ArrayType) {
            if (index_t->tag != IntType && index_t->tag != BigIntType)
                code_err(indexing->index, "Arrays can only be indexed by integers, not %T", index_t);
            type_t *item_type = Match(container_t, ArrayType)->item_type;
            CORD arr = compile_to_pointer_depth(env, indexing->indexed, 0, false);
            file_t *f = indexing->index->file;
            if (indexing->unchecked)
                return CORD_all("Array_get_unchecked(", compile_type(item_type), ", ", arr, ", ",
                                compile_int_to_type(env, indexing->index, Type(IntType, .bits=TYPE_IBITS64)), ")");
            else
                return CORD_all("Array_get(", compile_type(item_type), ", ", arr, ", ",
                                compile_int_to_type(env, indexing->index, Type(IntType, .bits=TYPE_IBITS64)), ", ",
                                CORD_asprintf("%ld", (int64_t)(indexing->index->start - f->text)), ", ",
                                CORD_asprintf("%ld", (int64_t)(indexing->index->end - f->text)),
                                ")");
        } else {
            code_err(ast, "Indexing is not supported for type: %T", container_t);
        }
    }
    case InlineCCode: {
        type_t *t = get_type(env, ast);
        if (t->tag == VoidType)
            return CORD_all("{\n", Match(ast, InlineCCode)->code, "\n}");
        else
            return Match(ast, InlineCCode)->code;
    }
    case Use: code_err(ast, "Compiling 'use' as expression!");
    case Defer: code_err(ast, "Compiling 'defer' as expression!");
    case LinkerDirective: code_err(ast, "Linker directives are not supported yet");
    case Extern: code_err(ast, "Externs are not supported as expressions");
    case TableEntry: code_err(ast, "Table entries should not be compiled directly");
    case Declare: case Assign: case UpdateAssign: case For: case While: case StructDef: case LangDef:
    case EnumDef: case FunctionDef: case Skip: case Stop: case Pass: case Return: case DocTest: case PrintStatement:
        code_err(ast, "This is not a valid expression");
    default: case Unknown: code_err(ast, "Unknown AST");
    }
}

void compile_namespace(env_t *env, const char *ns_name, ast_t *block)
{
    env_t *ns_env = namespace_env(env, ns_name);
    CORD prefix = namespace_prefix(ns_env->libname, ns_env->namespace);

    // First prepare variable initializers to prevent unitialized access:
    for (ast_list_t *stmt = block ? Match(block, Block)->statements : NULL; stmt; stmt = stmt->next) {
        if (stmt->ast->tag == Declare) {
            auto decl = Match(stmt->ast, Declare);
            type_t *t = get_type(ns_env, decl->value);
            if (t->tag == AbortType || t->tag == VoidType || t->tag == ReturnType)
                code_err(stmt->ast, "You can't declare a variable with a %T value", t);
            CORD name_code = CORD_all(prefix, Match(decl->var, Var)->name);

            if (!is_constant(env, decl->value)) {
                env->code->variable_initializers = CORD_all(
                    env->code->variable_initializers,
                    name_code, " = ", compile_maybe_incref(ns_env, decl->value), ",\n",
                    name_code, "$initialized = true;\n");

                CORD checked_access = CORD_all("check_initialized(", name_code, ", \"", Match(decl->var, Var)->name, "\")");
                set_binding(ns_env, Match(decl->var, Var)->name, new(binding_t, .type=t, .code=checked_access));
            }
        }
    }

    for (ast_list_t *stmt = block ? Match(block, Block)->statements : NULL; stmt; stmt = stmt->next) {
        ast_t *ast = stmt->ast;
        switch (ast->tag) {
        case FunctionDef: {
            CORD code = compile_statement(ns_env, ast);
            env->code->funcs = CORD_cat(env->code->funcs, code);
            break;
        }
        case Declare: {
            auto decl = Match(ast, Declare);
            type_t *t = get_type(ns_env, decl->value);
            if (t->tag == FunctionType)
                t = Type(ClosureType, t);
            bool is_private = (Match(decl->var, Var)->name[0] == '_');
            CORD name_code = CORD_all(prefix, Match(decl->var, Var)->name);
            if (!is_constant(env, decl->value)) {
                if (t->tag == FunctionType)
                    t = Type(ClosureType, t);

                env->code->staticdefs = CORD_all(
                    env->code->staticdefs,
                    "static bool ", name_code, "$initialized = false;\n",
                    is_private ? "static " : CORD_EMPTY,
                    compile_declaration(t, name_code), ";\n");
            } else {
                CORD val_code = compile_maybe_incref(ns_env, decl->value);
                if (t->tag == FunctionType) {
                    assert(promote(env, &val_code, t, Type(ClosureType, t)));
                    t = Type(ClosureType, t);
                }

                env->code->staticdefs = CORD_all(
                    env->code->staticdefs,
                    is_private ? "static " : CORD_EMPTY,
                    compile_declaration(t, name_code), " = ", val_code, ";\n");
            }
            break;
        }
        default: {
            CORD code = compile_statement(ns_env, ast);
            assert(!code);
            break;
        }
        }
    }
}

CORD compile_namespace_definitions(env_t *env, const char *ns_name, ast_t *block)
{
    env_t *ns_env = namespace_env(env, ns_name);
    CORD header = CORD_EMPTY;
    for (ast_list_t *stmt = block ? Match(block, Block)->statements : NULL; stmt; stmt = stmt->next) {
        header = CORD_all(header, compile_statement_definitions(ns_env, stmt->ast));
    }
    return header;
}

CORD compile_type_info(env_t *env, type_t *t)
{
    switch (t->tag) {
    case BoolType: case IntType: case BigIntType: case NumType: case CStringType:
        return CORD_all("&", type_to_cord(t), "$info");
    case TextType: {
        auto text = Match(t, TextType);
        if (!text->lang)
            return "&Text$info";
        else if (streq(text->lang, "Pattern"))
            return "&Pattern$info";
        else if (streq(text->lang, "Shell"))
            return "&Shell$info";
        else if (streq(text->lang, "Path"))
            return "&Path$info";
        return CORD_all("(&", namespace_prefix(text->env->libname, text->env->namespace->parent), text->lang, ")");
    }
    case StructType: {
        auto s = Match(t, StructType);
        return CORD_all("(&", namespace_prefix(s->env->libname, s->env->namespace->parent), s->name, ")");
    }
    case EnumType: {
        auto e = Match(t, EnumType);
        return CORD_all("(&", namespace_prefix(e->env->libname, e->env->namespace->parent), e->name, ")");
    }
    case ArrayType: {
        type_t *item_t = Match(t, ArrayType)->item_type;
        return CORD_all("Array$info(", compile_type_info(env, item_t), ")");
    }
    case SetType: {
        type_t *item_type = Match(t, SetType)->item_type;
        return CORD_all("Set$info(", compile_type_info(env, item_type), ")");
    }
    case ChannelType: {
        type_t *item_t = Match(t, ChannelType)->item_type;
        return CORD_asprintf("Channel$info(%r)", compile_type_info(env, item_t));
    }
    case TableType: {
        type_t *key_type = Match(t, TableType)->key_type;
        type_t *value_type = Match(t, TableType)->value_type;
        return CORD_all("Table$info(", compile_type_info(env, key_type), ", ", compile_type_info(env, value_type), ")");
    }
    case PointerType: {
        auto ptr = Match(t, PointerType);
        CORD sigil = ptr->is_stack ? "&" : "@";
        if (ptr->is_readonly) sigil = CORD_cat(sigil, "%");
        return CORD_asprintf("Pointer$info(%r, %r)",
                             CORD_quoted(sigil),
                             compile_type_info(env, ptr->pointed));
    }
    case FunctionType: {
        return CORD_asprintf("Function$info(%r)", CORD_quoted(type_to_cord(t)));
    }
    case ClosureType: {
        return CORD_asprintf("Closure$info(%r)", CORD_quoted(type_to_cord(t)));
    }
    case OptionalType: {
        return CORD_asprintf("Optional$info(%r)", compile_type_info(env, Match(t, OptionalType)->type));
    }
    case TypeInfoType: return "&TypeInfo$info";
    case MemoryType: return "&Memory$info";
    case VoidType: return "&Void$info";
    default:
        compiler_err(NULL, 0, 0, "I couldn't convert to a type info: %T", t);
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
    auto fn_info = Match(fn_type, FunctionType);

    binding_t *usage_binding = get_binding(env, "_USAGE");
    CORD usage_code = usage_binding ? usage_binding->code : "usage";
    binding_t *help_binding = get_binding(env, "_HELP");
    CORD help_code = help_binding ? help_binding->code : usage_code;

    if (!fn_info->args) {
        CORD code = "Text_t usage = Texts(Text(\"Usage: \"), Text$from_str(argv[0]), Text(\" [--help]\"));\n";
        code = CORD_all(code, "if (argc > 1 && streq(argv[1], \"--help\")) {\n",
                        "Text$print(stdout, ", help_code, ");\n"
                        "puts(\"\");\n"
                        "return 0;\n}\n");

        return CORD_all(
            code,
            "if (argc > 1)\n"
            "errx(1, \"This program doesn't take any arguments.\\n%k\", &", usage_code, ");\n",
            fn_name, "();\n");
    }

    CORD code = CORD_all(
        "#define USAGE_ERR(fmt, ...) errx(1, fmt \"\\n%s\" __VA_OPT__(,) __VA_ARGS__, Text$as_c_string(", usage_code, "))\n"
        "#define IS_FLAG(str, flag) (strncmp(str, flag, strlen(flag) == 0 && (str[strlen(flag)] == 0 || str[strlen(flag)] == '=')) == 0)\n");

    env_t *main_env = fresh_scope(env);

    bool explicit_help_flag = false;
    for (arg_t *arg = fn_info->args; arg; arg = arg->next) {
        if (streq(arg->name, "help")) {
            explicit_help_flag = true;
            break;
        }
    }

    if (!usage_binding) {
        CORD usage = explicit_help_flag ? CORD_EMPTY : " [--help]";
        for (arg_t *arg = fn_info->args; arg; arg = arg->next) {
            usage = CORD_cat(usage, " ");
            type_t *t = get_arg_type(main_env, arg);
            CORD flag = CORD_replace(arg->name, "_", "-");
            if (arg->default_val) {
                if (t->tag == BoolType)
                    usage = CORD_all(usage, "[--", flag, "|--no-", flag, "]");
                else
                    usage = CORD_all(usage, "[--", flag, "=", get_flag_options(t, "|"), "]");
            } else {
                if (t->tag == BoolType)
                    usage = CORD_all(usage, "[--", flag, "|--no-", flag, "]");
                else if (t->tag == EnumType)
                    usage = CORD_all(usage, get_flag_options(t, "|"));
                else if (t->tag == ArrayType)
                    usage = CORD_all(usage, "<", flag, "...>");
                else
                    usage = CORD_all(usage, "<", flag, ">");
            }
        }
        code = CORD_all(code, "Text_t usage = Texts(Text(\"Usage: \"), Text$from_str(argv[0])",
                        usage == CORD_EMPTY ? CORD_EMPTY : CORD_all(", Text(", CORD_quoted(usage), ")"), ");\n");
    }

    // Declare args:
    for (arg_t *arg = fn_info->args; arg; arg = arg->next) {
        type_t *t = get_arg_type(main_env, arg);
        assert(arg->name);
        type_t *optional = t->tag == OptionalType ? t : Type(OptionalType, .type=t);
        type_t *non_optional = t->tag == OptionalType ? Match(t, OptionalType)->type : t;
        code = CORD_all(
            code, compile_declaration(optional, CORD_cat("$", arg->name)), " = ", compile_null(non_optional), ";\n");
        set_binding(main_env, arg->name, new(binding_t, .type=optional, .code=CORD_cat("$", arg->name)));
    }
    // Provide --flags:
    code = CORD_all(code, "Text_t flag;\n"
                    "for (int i = 1; i < argc; ) {\n"
                    "if (streq(argv[i], \"--\")) {\n"
                    "argv[i] = NULL;\n"
                    "break;\n"
                    "}\n"
                    "if (strncmp(argv[i], \"--\", 2) != 0) {\n++i;\ncontinue;\n}\n");

    if (!explicit_help_flag) {
        code = CORD_all(code, "else if (pop_flag(argv, &i, \"help\", &flag)) {\n"
                        "Text$print(stdout, ", help_code, ");\n"
                        "puts(\"\");\n"
                        "return 0;\n"
                        "}\n");
    }

    for (arg_t *arg = fn_info->args; arg; arg = arg->next) {
        type_t *t = get_arg_type(main_env, arg);
        type_t *non_optional = t->tag == OptionalType ? Match(t, OptionalType)->type : t;
        CORD flag = CORD_replace(arg->name, "_", "-");
        switch (non_optional->tag) {
        case BoolType: {
            code = CORD_all(code, "else if (pop_flag(argv, &i, \"", flag, "\", &flag)) {\n"
                            "if (flag.length != 0) {\n",
                            "$", arg->name, " = Bool$from_text(flag);\n"
                            "if (!", compile_optional_check(main_env, FakeAST(Var, arg->name)), ") \n"
                            "USAGE_ERR(\"Invalid argument for '--", flag, "'\");\n",
                            "} else {\n",
                            "$", arg->name, " = yes;\n",
                            "}\n"
                            "}\n");
            break;
        }
        case TextType: {
            code = CORD_all(code, "else if (pop_flag(argv, &i, \"", flag, "\", &flag)) {\n",
                            "$", arg->name, " = ", streq(Match(t, TextType)->lang, "Path") ? "Path$cleanup(flag)" : "flag",";\n",
                            "}\n");
            break;
        }
        case ArrayType: {
            if (Match(t, ArrayType)->item_type->tag != TextType)
                compiler_err(NULL, NULL, NULL, "Main function has unsupported argument type: %T (only arrays of Text are supported)", t);
            code = CORD_all(code, "else if (pop_flag(argv, &i, \"", flag, "\", &flag)) {\n",
                            "$", arg->name, " = Text$split(flag, Pattern(\",\"));\n");
            if (streq(Match(Match(t, ArrayType)->item_type, TextType)->lang, "Path")) {
                code = CORD_all(code, "for (int64_t j = 0; j < $", arg->name, ".length; j++)\n"
                                "*(Path_t*)($", arg->name, ".data + j*$", arg->name, ".stride) "
                                "= Path$cleanup(*(Path_t*)($", arg->name, ".data + j*$", arg->name, ".stride));\n");
            }
            code = CORD_all(code, "}\n");
            break;
        }
        case BigIntType: case IntType: case NumType: {
            CORD type_name = type_to_cord(non_optional);
            code = CORD_all(code, "else if (pop_flag(argv, &i, \"", flag, "\", &flag)) {\n",
                            "if (flag.length == 0)\n"
                            "USAGE_ERR(\"No value provided for '--", flag, "'\");\n"
                            "$", arg->name, " = ", type_name, "$from_text(flag);\n"
                            "if (!", compile_optional_check(main_env, FakeAST(Var, arg->name)), ")\n"
                            "USAGE_ERR(\"Invalid value provided for '--", flag, "'\");\n",
                            "}\n");
            break;
        }
        case EnumType: {
            env_t *enum_env = Match(t, EnumType)->env;
            code = CORD_all(code, "else if (pop_flag(argv, &i, \"", flag, "\", &flag)) {\n",
                            "if (flag.length == 0)\n"
                            "USAGE_ERR(\"No value provided for '--", flag, "'\");\n");
            for (tag_t *tag = Match(t, EnumType)->tags; tag; tag = tag->next) {
                if (tag->type && Match(tag->type, StructType)->fields)
                    compiler_err(NULL, NULL, NULL,
                                 "The type %T has enum fields with member values, which is not yet supported for command line arguments.");
                binding_t *b = get_binding(enum_env, tag->name);
                code = CORD_all(code,
                                "if (Text$equal_ignoring_case(flag, Text(\"", tag->name, "\"))) {\n"
                                "$", arg->name, " = ", b->code, ";\n",
                                "} else ");
            }
            code = CORD_all(code, "USAGE_ERR(\"Invalid value provided for '--", flag, "', valid values are: ",
                            get_flag_options(t, ", "), "\");\n",
                            "}\n");
            break;
        }
        default:
            compiler_err(NULL, NULL, NULL, "Main function has unsupported argument type: %T", t);
        }
    }

    code = CORD_all(
        code, "else {\n"
        "USAGE_ERR(\"Unrecognized argument: %s\", argv[i]);\n"
        "}\n"
        "}\n"
        "int i = 1;\n"
        "while (i < argc && argv[i] == NULL)\n"
        "++i;\n");

    for (arg_t *arg = fn_info->args; arg; arg = arg->next) {
        type_t *t = get_arg_type(main_env, arg);
        type_t *non_optional = t->tag == OptionalType ? Match(t, OptionalType)->type : t;
        code = CORD_all(code, "if (!", compile_optional_check(main_env, FakeAST(Var, arg->name)), ") {\n");
        if (non_optional->tag == ArrayType) {
            if (Match(non_optional, ArrayType)->item_type->tag != TextType)
                compiler_err(NULL, NULL, NULL, "Main function has unsupported argument type: %T (only arrays of Text are supported)", non_optional);

            code = CORD_all(
                code, "$", arg->name, " = (Array_t){};\n"
                "for (; i < argc; i++) {\n"
                "if (argv[i]) {\n");
            if (streq(Match(Match(non_optional, ArrayType)->item_type, TextType)->lang, "Path")) {
                code = CORD_all(code, "Path_t arg = Path$cleanup(Text$from_str(argv[i]));\n");
            } else {
                code = CORD_all(code, "Text_t arg = Text$from_str(argv[i]);\n");
            }
            code = CORD_all(code, "Array$insert(&$", arg->name, ", &arg, I(0), sizeof(Text_t));\n"
                            "argv[i] = NULL;\n"
                            "}\n"
                            "}\n");
        } else if (arg->default_val) {
            code = CORD_all(code, "$", arg->name, " = ", compile(main_env, arg->default_val), ";\n");
        } else {
            code = CORD_all(
                code,
                "if (i < argc) {");
            if (non_optional->tag == TextType) {
                code = CORD_all(code, "$", arg->name, " = Text$from_str(argv[i]);\n");
                if (streq(Match(non_optional, TextType)->lang, "Path"))
                    code = CORD_all(code, "$", arg->name, " = Path$cleanup($", arg->name, ");\n");

            } else if (non_optional->tag == EnumType) {
                env_t *enum_env = Match(non_optional, EnumType)->env;
                for (tag_t *tag = Match(non_optional, EnumType)->tags; tag; tag = tag->next) {
                    if (tag->type && Match(tag->type, StructType)->fields)
                        compiler_err(NULL, NULL, NULL,
                                     "The type %T has enum fields with member values, which is not yet supported for command line arguments.",
                                     non_optional);
                    binding_t *b = get_binding(enum_env, tag->name);
                    code = CORD_all(code,
                                    "if (strcasecmp(argv[i], \"", tag->name, "\") == 0) {\n"
                                    "$", arg->name, " = ", b->code, ";\n",
                                    "} else ");
                }
                code = CORD_all(code, "USAGE_ERR(\"Invalid value provided for '--", arg->name, "', valid values are: ",
                                get_flag_options(non_optional, ", "), "\");\n");
            } else {
                code = CORD_all(
                    code,
                    "$", arg->name, " = ", type_to_cord(non_optional), "$from_text(Text$from_str(argv[i]))", ";\n"
                    "if (!", compile_optional_check(main_env, FakeAST(Var, arg->name)), ")\n"
                    "USAGE_ERR(\"Unable to parse this argument as a ", type_to_cord(non_optional), ": %s\", argv[i]);\n");
            }
            code = CORD_all(
                code,
                "argv[i++] = NULL;\n"
                "while (i < argc && argv[i] == NULL)\n"
                "++i;\n");
            if (t->tag != OptionalType) {
                code = CORD_all(code, "} else {\n"
                                "USAGE_ERR(\"Required argument '", arg->name, "' was not provided!\");\n");
            }
            code = CORD_all(code, "}\n");
        }
        code = CORD_all(code, "}\n");
    }


    code = CORD_all(code, "for (; i < argc; i++) {\n"
                    "if (argv[i])\nUSAGE_ERR(\"Unexpected argument: %s\", argv[i]);\n}\n");

    code = CORD_all(code, fn_name, "(");
    for (arg_t *arg = fn_info->args; arg; arg = arg->next) {
        if (arg->type->tag == OptionalType)
            code = CORD_all(code, "$", arg->name);
        else
            code = CORD_all(code, optional_var_into_nonnull(get_binding(main_env, arg->name)));
            
        if (arg->next) code = CORD_all(code, ", ");
    }
    code = CORD_all(code, ");\n");
    return code;
}

CORD compile_file(env_t *env, ast_t *ast)
{
    // First prepare variable initializers to prevent unitialized access:
    for (ast_list_t *stmt = Match(ast, Block)->statements; stmt; stmt = stmt->next) {
        if (stmt->ast->tag == Declare) {
            auto decl = Match(stmt->ast, Declare);
            const char *decl_name = Match(decl->var, Var)->name;
            CORD full_name = CORD_all(namespace_prefix(env->libname, env->namespace), decl_name);
            type_t *t = get_type(env, decl->value);
            if (t->tag == AbortType || t->tag == VoidType || t->tag == ReturnType)
                code_err(stmt->ast, "You can't declare a variable with a %T value", t);
            if (!(decl->value->tag == Use || is_constant(env, decl->value))) {
                CORD val_code = compile_maybe_incref(env, decl->value);
                if (t->tag == FunctionType) {
                    assert(promote(env, &val_code, t, Type(ClosureType, t)));
                    t = Type(ClosureType, t);
                }

                env->code->variable_initializers = CORD_all(
                    env->code->variable_initializers,
                    full_name, " = ", val_code, ",\n",
                    full_name, "$initialized = true;\n");

                CORD checked_access = CORD_all("check_initialized(", full_name, ", \"", decl_name, "\")");
                set_binding(env, decl_name, new(binding_t, .type=t, .code=checked_access));
            }
        }
    }

    for (ast_list_t *stmt = Match(ast, Block)->statements; stmt; stmt = stmt->next) {
        if (stmt->ast->tag == Declare) {
            auto decl = Match(stmt->ast, Declare);
            const char *decl_name = Match(decl->var, Var)->name;
            CORD full_name = CORD_all(namespace_prefix(env->libname, env->namespace), decl_name);
            bool is_private = (decl_name[0] == '_');
            type_t *t = get_type(env, decl->value);
            if (decl->value->tag == Use) {
                assert(compile_statement(env, stmt->ast) == CORD_EMPTY);
            } else if (!is_constant(env, decl->value)) {
                env->code->staticdefs = CORD_all(
                    env->code->staticdefs,
                    "static bool ", full_name, "$initialized = false;\n",
                    is_private ? "static " : CORD_EMPTY,
                    compile_declaration(t, full_name), ";\n");
            } else {
                CORD val_code = compile_maybe_incref(env, decl->value);
                if (t->tag == FunctionType) {
                    assert(promote(env, &val_code, t, Type(ClosureType, t)));
                    t = Type(ClosureType, t);
                }
                env->code->staticdefs = CORD_all(
                    env->code->staticdefs,
                    is_private ? "static " : CORD_EMPTY,
                    compile_declaration(t, full_name), " = ", val_code, ";\n");
            }
        } else if (stmt->ast->tag == InlineCCode) {
            CORD code = compile_statement(env, stmt->ast);
            env->code->staticdefs = CORD_all(env->code->staticdefs, code, "\n");
        } else {
            CORD code = compile_statement(env, stmt->ast);
            assert(!code);
        }
    }

    const char *name = file_base_name(ast->file->filename);
    return CORD_all(
        // "#line 1 ", CORD_quoted(ast->file->filename), "\n",
        "#define __SOURCE_FILE__ ", CORD_quoted(ast->file->filename), "\n",
        "#include <tomo/tomo.h>\n"
        "#include \"", name, ".tm.h\"\n\n",
        env->code->local_typedefs, "\n",
        env->code->staticdefs, "\n",
        "public void ", env->namespace->name, "$$initialize(void) {\n",
        "static bool initialized = false;\n",
        "if (initialized) return;\n",
        "initialized = true;\n",
        env->code->variable_initializers,
        "}\n",
        env->code->funcs, "\n",
        env->code->typeinfos, "\n");
}

CORD compile_statement_imports(env_t *env, ast_t *ast)
{
    switch (ast->tag) {
    case DocTest: {
        auto test = Match(ast, DocTest);
        return compile_statement_imports(env, test->expr);
    }
    case Declare: {
        auto decl = Match(ast, Declare);
        if (decl->value->tag == Use)
            return compile_statement_imports(env, decl->value);
        return CORD_EMPTY;
    }
    case Use: {
        auto use = Match(ast, Use);
        switch (use->what) {
        case USE_MODULE: 
            return CORD_all("#include <tomo/lib", use->path, ".h>\n");
        case USE_LOCAL: 
            return CORD_all("#include \"", use->path, ".h\"\n");
        case USE_HEADER:
            return CORD_all("#include ", use->path, "\n");
        default:
            return CORD_EMPTY;
        }
    }
    default: return CORD_EMPTY;
    }
}

CORD compile_statement_typedefs(env_t *env, ast_t *ast)
{
    switch (ast->tag) {
    case DocTest: {
        auto test = Match(ast, DocTest);
        return compile_statement_typedefs(env, test->expr);
    }
    case StructDef: {
        return compile_struct_typedef(env, ast);
    }
    case EnumDef: {
        return compile_enum_typedef(env, ast);
    }
    case LangDef: {
        auto def = Match(ast, LangDef);
        return CORD_all(
            "typedef Text_t ", namespace_prefix(env->libname, env->namespace), def->name, "_t;\n"
            // Constructor macro:
            "#define ", namespace_prefix(env->libname, env->namespace), def->name,
                "(text) ((", namespace_prefix(env->libname, env->namespace), def->name, "_t){.length=sizeof(text)-1, .tag=TEXT_ASCII, .ascii=\"\" text})\n"
            "#define ", namespace_prefix(env->libname, env->namespace), def->name,
                "s(...) ((", namespace_prefix(env->libname, env->namespace), def->name, "_t)Texts(__VA_ARGS__))\n"
        );
    }
    case Lambda: {
        auto lambda = Match(ast, Lambda);
        Table_t *closed_vars = get_closed_vars(env, ast);
        if (Table$length(*closed_vars) == 0)
            return CORD_EMPTY;

        CORD def = "typedef struct {";
        for (int64_t i = 1; i <= Table$length(*closed_vars); i++) {
            struct { const char *name; binding_t *b; } *entry = Table$entry(*closed_vars, i);
            if (entry->b->type->tag == ModuleType)
                continue;
            def = CORD_all(def, compile_declaration(entry->b->type, entry->name), "; ");
        }
        CORD name = CORD_asprintf("%rlambda$%ld", namespace_prefix(env->libname, env->namespace), lambda->id);
        return CORD_all(def, "} ", name, "$userdata_t;");
    }
    default:
        return CORD_EMPTY;
    }
}

CORD compile_statement_definitions(env_t *env, ast_t *ast)
{
    switch (ast->tag) {
    case DocTest: {
        auto test = Match(ast, DocTest);
        return compile_statement_definitions(env, test->expr);
    }
    case Declare: {
        auto decl = Match(ast, Declare);
        if (decl->value->tag == Use) {
            return compile_statement_definitions(env, decl->value);
        }
        type_t *t = get_type(env, decl->value);
        if (t->tag == FunctionType)
            t = Type(ClosureType, t);
        assert(t->tag != ModuleType);
        if (t->tag == AbortType || t->tag == VoidType || t->tag == ReturnType)
            code_err(ast, "You can't declare a variable with a %T value", t);
        const char *decl_name = Match(decl->var, Var)->name;
        bool is_private = (decl_name[0] == '_');
        CORD code = (decl->value->tag == Use) ? compile_statement_definitions(env, decl->value) : CORD_EMPTY;
        if (is_private) {
            return code;
        } else {
            return CORD_all(
                code, "\n" "extern ", compile_declaration(t, CORD_cat(namespace_prefix(env->libname, env->namespace), decl_name)), ";\n");
        }
    }
    case StructDef: {
        auto def = Match(ast, StructDef);
        CORD full_name = CORD_cat(namespace_prefix(env->libname, env->namespace), def->name);
        return CORD_all(
            "extern const TypeInfo ", full_name, ";\n",
            compile_namespace_definitions(env, def->name, def->namespace));
    }
    case EnumDef: {
        return compile_enum_declarations(env, ast);
    }
    case LangDef: {
        auto def = Match(ast, LangDef);
        CORD full_name = CORD_cat(namespace_prefix(env->libname, env->namespace), def->name);
        return CORD_all(
            "extern const TypeInfo ", full_name, ";\n",
            compile_namespace_definitions(env, def->name, def->namespace));
    }
    case FunctionDef: {
        auto fndef = Match(ast, FunctionDef);
        const char *decl_name = Match(fndef->name, Var)->name;
        bool is_private = decl_name[0] == '_';
        if (is_private) return CORD_EMPTY;
        CORD arg_signature = "(";
        for (arg_ast_t *arg = fndef->args; arg; arg = arg->next) {
            type_t *arg_type = get_arg_ast_type(env, arg);
            arg_signature = CORD_cat(arg_signature, compile_declaration(arg_type, CORD_cat("$", arg->name)));
            if (arg->next) arg_signature = CORD_cat(arg_signature, ", ");
        }
        arg_signature = CORD_cat(arg_signature, ")");

        type_t *ret_t = fndef->ret_type ? parse_type_ast(env, fndef->ret_type) : Type(VoidType);
        CORD ret_type_code = compile_type(ret_t);
        return CORD_all(ret_type_code, " ", namespace_prefix(env->libname, env->namespace), decl_name, arg_signature, ";\n");
    }
    case Extern: {
        auto ext = Match(ast, Extern);
        type_t *t = parse_type_ast(env, ext->type);
        CORD decl;
        if (t->tag == ClosureType) {
            t = Match(t, ClosureType)->fn;
            auto fn = Match(t, FunctionType);
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
    default:
        return CORD_EMPTY;
    }
}

CORD compile_header(env_t *env, ast_t *ast)
{
    CORD header = CORD_all(
        "#pragma once\n"
        // "#line 1 ", CORD_quoted(ast->file->filename), "\n",
        "#include <tomo/tomo.h>\n");

    for (ast_list_t *stmt = Match(ast, Block)->statements; stmt; stmt = stmt->next)
        header = CORD_all(header, compile_statement_imports(env, stmt->ast));

    for (ast_list_t *stmt = Match(ast, Block)->statements; stmt; stmt = stmt->next)
        header = CORD_all(header, compile_statement_typedefs(env, stmt->ast));

    for (ast_list_t *stmt = Match(ast, Block)->statements; stmt; stmt = stmt->next)
        header = CORD_all(header, compile_statement_definitions(env, stmt->ast));

    header = CORD_all(header, "void ", env->namespace->name, "$$initialize(void);\n");

    return header;
}

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
