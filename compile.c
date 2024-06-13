// Compilation logic
#include <ctype.h>
#include <gc/cord.h>
#include <gc.h>
#include <stdio.h>
#include <uninorm.h>

#include "ast.h"
#include "builtins/text.h"
#include "compile.h"
#include "enums.h"
#include "structs.h"
#include "environment.h"
#include "typecheck.h"
#include "builtins/util.h"

static CORD compile_to_pointer_depth(env_t *env, ast_t *ast, int64_t target_depth, bool allow_optional);
static env_t *with_enum_scope(env_t *env, type_t *t);
static CORD compile_statement_header(env_t *env, ast_t *ast);

CORD compile_type_ast(env_t *env, type_ast_t *t)
{
    switch (t->tag) {
    case VarTypeAST: return CORD_all(env->file_prefix, Match(t, VarTypeAST)->name, "_t");
    case PointerTypeAST: return CORD_cat(compile_type_ast(env, Match(t, PointerTypeAST)->pointed), "*");
    case TableTypeAST: return "table_t";
    case ArrayTypeAST: return "array_t";
    case FunctionTypeAST: return "const void*";
    default: code_err(t, "Compiling type AST is not implemented");
    }
}

static bool promote(env_t *env, CORD *code, type_t *actual, type_t *needed)
{
    if (type_eq(actual, needed))
        return true;

    if (!can_promote(actual, needed))
        return false;

    if (actual->tag == IntType || actual->tag == NumType)
        return true;

    // Automatic dereferencing:
    if (actual->tag == PointerType && !Match(actual, PointerType)->is_optional
        && can_promote(Match(actual, PointerType)->pointed, needed)) {
        *code = CORD_all("*(", *code, ")");
        return promote(env, code, Match(actual, PointerType)->pointed, needed);
    }

    // Optional and stack ref promotion:
    if (actual->tag == PointerType && needed->tag == PointerType)
        return true;

    if (needed->tag == ClosureType && actual->tag == FunctionType) {
        *code = CORD_all("(closure_t){", *code, ", NULL}");
        return true;
    }

    if (needed->tag == ClosureType && actual->tag == ClosureType)
        return true;

    if (needed->tag == FunctionType && actual->tag == FunctionType) {
        *code = CORD_all("(", compile_type(env, needed), ")", *code);
        return true;
    }

    return false;
}

static table_t *get_closed_vars(env_t *env, ast_t *lambda_ast)
{
    auto lambda = Match(lambda_ast, Lambda);
    env_t *body_scope = fresh_scope(env);
    for (arg_ast_t *arg = lambda->args; arg; arg = arg->next) {
        type_t *arg_type = get_arg_ast_type(env, arg);
        set_binding(body_scope, arg->name, new(binding_t, .type=arg_type, .code=CORD_cat("$", arg->name)));
    }

    fn_ctx_t fn_ctx = (fn_ctx_t){
        .closure_scope=body_scope->locals->fallback,
        .closed_vars=new(table_t),
    };
    body_scope->fn_ctx = &fn_ctx;
    body_scope->locals->fallback = env->globals;
    type_t *ret_t = get_type(body_scope, lambda->body);
    fn_ctx.return_type = ret_t;

    // Find which variables are captured in the closure:
    env_t *tmp_scope = fresh_scope(body_scope);
    for (ast_list_t *stmt = Match(lambda->body, Block)->statements; stmt; stmt = stmt->next) {
        type_t *stmt_type = get_type(tmp_scope, stmt->ast);
        if (stmt->next || (stmt_type->tag == VoidType || stmt_type->tag == AbortType))
            (void)compile_statement(tmp_scope, stmt->ast);
        else
            (void)compile(tmp_scope, stmt->ast);
    }
    return fn_ctx.closed_vars;
}

CORD compile_declaration(env_t *env, type_t *t, CORD name)
{
    if (t->tag == FunctionType) {
        auto fn = Match(t, FunctionType);
        CORD code = CORD_all(compile_type(env, fn->ret), " (*", name, ")(");
        for (arg_t *arg = fn->args; arg; arg = arg->next) {
            code = CORD_all(code, compile_type(env, arg->type));
            if (arg->next) code = CORD_cat(code, ", ");
        }
        return CORD_all(code, ")");
    } else if (t->tag != ModuleType) {
        return CORD_all(compile_type(env, t), " ", name);
    } else {
        return CORD_EMPTY;
    }
}

CORD compile_type(env_t *env, type_t *t)
{
    switch (t->tag) {
    case AbortType: return "void";
    case VoidType: return "void";
    case MemoryType: return "void";
    case BoolType: return "Bool_t";
    case CStringType: return "char*";
    case IntType: return Match(t, IntType)->bits == 64 ? "Int_t" : CORD_asprintf("Int%ld_t", Match(t, IntType)->bits);
    case NumType: return Match(t, NumType)->bits == 64 ? "Num_t" : CORD_asprintf("Num%ld_t", Match(t, NumType)->bits);
    case TextType: {
        auto text = Match(t, TextType);
        return text->lang ? CORD_all(text->env->file_prefix, text->lang, "_t") : "Text_t";
    }
    case ArrayType: return "array_t";
    case TableType: return "table_t";
    case FunctionType: {
        auto fn = Match(t, FunctionType);
        CORD code = CORD_all(compile_type(env, fn->ret), " (*)(");
        for (arg_t *arg = fn->args; arg; arg = arg->next) {
            code = CORD_all(code, compile_type(env, arg->type));
            if (arg->next) code = CORD_cat(code, ", ");
        }
        return CORD_all(code, ")");
    }
    case ClosureType: return "closure_t";
    case PointerType: return CORD_cat(compile_type(env, Match(t, PointerType)->pointed), "*");
    case StructType: {
        auto s = Match(t, StructType);
        return CORD_all("struct ", s->env->file_prefix, s->name, "_s");
    }
    case EnumType: {
        auto e = Match(t, EnumType);
        return CORD_all(e->env->file_prefix, e->name, "_t");
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
            code_err(ast, "This is a value of type %T and can't be assigned to", get_type(env, ast));
        }
    }

    if (ast->tag == Index) {
        auto index = Match(ast, Index);
        type_t *container_t = get_type(env, index->indexed);
        if (!index->index && container_t->tag == PointerType) {
            if (Match(container_t, PointerType)->is_optional)
                code_err(index->indexed, "This pointer might be null, so it can't be safely assigned to");
            return compile(env, ast);
        }
        container_t = value_type(container_t);
        if (container_t->tag == ArrayType) {
            CORD target_code = compile_to_pointer_depth(env, index->indexed, 1, false);
            type_t *item_type = Match(container_t, ArrayType)->item_type;
            return CORD_all("Array_lvalue(", compile_type(env, item_type), ", ", target_code, ", ", 
                            compile(env, index->index), ", ", compile_type_info(env, container_t),
                            ", ", Text$quoted(ast->file->filename, false), ", ", heap_strf("%ld", ast->start - ast->file->text),
                            ", ", heap_strf("%ld", ast->end - ast->file->text), ")");
        } else if (container_t->tag == TableType) {
            CORD target_code = compile_to_pointer_depth(env, index->indexed, 1, false);
            type_t *value_t = Match(container_t, TableType)->value_type;
            return CORD_all("*(", compile_type(env, value_t), "*)Table$reserve_value(", target_code, ", ",
                            compile(env, index->index),", ", compile_type_info(env, container_t), ")");
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
    return CORD_all(compile_lvalue(env, target), " = ", value, ";\n");
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

        if (subject_t->tag == PointerType) {
            ast_t *var = when->clauses->args->ast;
            CORD var_code = compile(env, var);
            env_t *non_null_scope = fresh_scope(env);
            auto ptr = Match(subject_t, PointerType);
            type_t *non_optional_t = Type(PointerType, .pointed=ptr->pointed, .is_stack=ptr->is_stack,
                                          .is_readonly=ptr->is_readonly, .is_optional=false);
            set_binding(non_null_scope, Match(var, Var)->name, new(binding_t, .type=non_optional_t, .code=var_code));
            return CORD_all(
                "{\n",
                compile_declaration(env, subject_t, var_code), "  = ", compile(env, when->subject), ";\n"
                "if (", var_code, ")\n", compile_statement(non_null_scope, when->clauses->body),
                "\nelse\n", compile_statement(env, when->else_body), "\n}");
        }

        auto enum_t = Match(subject_t, EnumType);
        CORD code = CORD_all("{ ", compile_type(env, subject_t), " subject = ", compile(env, when->subject), ";\n"
                             "switch (subject.$tag) {");
        for (when_clause_t *clause = when->clauses; clause; clause = clause->next) {
            const char *clause_tag_name = Match(clause->tag_name, Var)->name;
            code = CORD_all(code, "case ", enum_t->env->file_prefix, enum_t->name, "$tag$", clause_tag_name, ": {\n");
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
                code = CORD_all(code, compile_type(env, tag_type), " ", compile(env, clause->args->ast), " = subject.", clause_tag_name, ";\n");
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
                    code = CORD_all(code, compile_type(env, field->type), " ", compile(env, var->ast), " = subject.", clause_tag_name, ".", field->name, ";\n");
                    set_binding(scope, Match(var->ast, Var)->name, new(binding_t, .type=field->type));
                    var = var->next;
                    field = field->next;
                }
            }
            code = CORD_all(code, compile_statement(scope, clause->body), "\nbreak;\n}\n");
        }
        if (when->else_body) {
            code = CORD_all(code, "default: {\n", compile_statement(env, when->else_body), "\nbreak;\n}");
        }
        code = CORD_all(code, "\n}\n}");
        return code;
    }
    case DocTest: {
        auto test = Match(ast, DocTest);
        type_t *expr_t = get_type(env, test->expr);
        if (!expr_t)
            code_err(test->expr, "I couldn't figure out the type of this expression");

        CORD output = NULL;
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
            if (decl->value->tag == Use) {
                assert(compile_statement(env, test->expr) == CORD_EMPTY);
                return CORD_asprintf(
                    "test(NULL, NULL, %r, %r, %ld, %ld);",
                    compile(env, WrapAST(test->expr, TextLiteral, .cord=output)),
                    compile(env, WrapAST(test->expr, TextLiteral, .cord=test->expr->file->filename)),
                    (int64_t)(test->expr->start - test->expr->file->text),
                    (int64_t)(test->expr->end - test->expr->file->text));
            } else {
                CORD var = CORD_all("$", Match(decl->var, Var)->name);
                return CORD_asprintf(
                    "%r;\n"
                    "test(({ %r = %r; &%r;}), %r, %r, %r, %ld, %ld);\n",
                    compile_declaration(env, get_type(env, decl->value), var),
                    var,
                    compile(env, decl->value),
                    var,
                    compile_type_info(env, get_type(env, decl->value)),
                    compile(env, WrapAST(test->expr, TextLiteral, .cord=output)),
                    compile(env, WrapAST(test->expr, TextLiteral, .cord=test->expr->file->filename)),
                    (int64_t)(test->expr->start - test->expr->file->text),
                    (int64_t)(test->expr->end - test->expr->file->text));
            }
        } else if (test->expr->tag == Assign) {
            auto assign = Match(test->expr, Assign);
            if (!assign->targets->next && assign->targets->ast->tag == Var) {
                // Common case: assigning to one variable:
                type_t *lhs_t = get_type(env, assign->targets->ast);
                if (lhs_t->tag == PointerType && Match(lhs_t, PointerType)->is_stack)
                    code_err(test->expr, "Stack references cannot be assigned to local variables because the variable may outlive the stack memory.");
                env_t *val_scope = with_enum_scope(env, lhs_t);
                type_t *rhs_t = get_type(val_scope, assign->values->ast);
                CORD value = compile(val_scope, assign->values->ast);
                if (!promote(env, &value, rhs_t, lhs_t))
                    code_err(assign->values->ast, "You cannot assign a %T value to a %T operand", rhs_t, lhs_t);
                return CORD_asprintf(
                    "test(({ %r; &%r; }), %r, %r, %r, %ld, %ld);",
                    compile_assignment(env, assign->targets->ast, value),
                    compile(env, assign->targets->ast),
                    compile_type_info(env, lhs_t),
                    compile(env, WrapAST(test->expr, TextLiteral, .cord=test->output)),
                    compile(env, WrapAST(test->expr, TextLiteral, .cord=test->expr->file->filename)),
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
                    CORD val_code = compile(val_scope, value->ast);
                    if (!promote(env, &val_code, value_type, target_type))
                        code_err(value->ast, "This %T value cannot be converted to a %T type", value_type, target_type);
                    CORD_appendf(&code, "%r $%ld = %r;\n", compile_type(env, target_type), i++, val_code);
                }
                i = 1;
                for (ast_list_t *target = assign->targets; target; target = target->next)
                    code = CORD_all(code, compile_assignment(env, target->ast, CORD_asprintf("$%ld", i++)));

                CORD_appendf(&code, "(%r[1]){$1}; }), %r, %r, %r, %ld, %ld);",
                    compile_type(env, get_type(env, assign->targets->ast)),
                    compile_type_info(env, get_type(env, assign->targets->ast)),
                    compile(env, WrapAST(test->expr, TextLiteral, .cord=test->output)),
                    compile(env, WrapAST(test->expr, TextLiteral, .cord=test->expr->file->filename)),
                    (int64_t)(test->expr->start - test->expr->file->text),
                    (int64_t)(test->expr->end - test->expr->file->text));
                return code;
            }
        } else if (expr_t->tag == VoidType || expr_t->tag == AbortType) {
            return CORD_asprintf(
                "test(({ %r; NULL; }), NULL, NULL, %r, %ld, %ld);",
                compile_statement(env, test->expr),
                compile(env, WrapAST(test->expr, TextLiteral, .cord=test->expr->file->filename)),
                (int64_t)(test->expr->start - test->expr->file->text),
                (int64_t)(test->expr->end - test->expr->file->text));
        } else {
            return CORD_asprintf(
                "test((%r[1]){%r}, %r, %r, %r, %ld, %ld);",
                compile_type(env, expr_t),
                compile(env, test->expr),
                compile_type_info(env, expr_t),
                compile(env, WrapAST(test->expr, TextLiteral, .cord=output)),
                compile(env, WrapAST(test->expr, TextLiteral, .cord=test->expr->file->filename)),
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
            if (t->tag == AbortType || t->tag == VoidType)
                code_err(ast, "You can't declare a variable with a %T value", t);
            return CORD_all(compile_declaration(env, t, CORD_cat("$", Match(decl->var, Var)->name)), " = ", compile(env, decl->value), ";");
        }
    }
    case Assign: {
        auto assign = Match(ast, Assign);
        // Single assignment, no temp vars needed:
        if (assign->targets && !assign->targets->next) {
            type_t *lhs_t = get_type(env, assign->targets->ast);
            if (lhs_t->tag == PointerType && Match(lhs_t, PointerType)->is_stack)
                code_err(ast, "Stack references cannot be assigned to local variables because the variable may outlive the stack memory.");
            env_t *val_env = with_enum_scope(env, lhs_t);
            type_t *rhs_t = get_type(val_env, assign->values->ast);
            CORD val = compile(val_env, assign->values->ast);
            if (!promote(env, &val, rhs_t, lhs_t))
                code_err(assign->values->ast, "You cannot assign a %T value to a %T operand", rhs_t, lhs_t);
            return compile_assignment(env, assign->targets->ast, val);
        }

        CORD code = "{ // Assignment\n";
        int64_t i = 1;
        for (ast_list_t *value = assign->values, *target = assign->targets; value && target; value = value->next, target = target->next) {
            type_t *lhs_t = get_type(env, target->ast);
            if (lhs_t->tag == PointerType && Match(lhs_t, PointerType)->is_stack)
                code_err(ast, "Stack references cannot be assigned to local variables because the variable may outlive the stack memory.");
            env_t *val_env = with_enum_scope(env, lhs_t);
            type_t *rhs_t = get_type(val_env, value->ast);
            CORD val = compile(val_env, value->ast);
            if (!promote(env, &val, rhs_t, lhs_t))
                code_err(value->ast, "You cannot assign a %T value to a %T operand", rhs_t, lhs_t);
            CORD_appendf(&code, "%r $%ld = %r;\n", compile_type(env, lhs_t), i++, val);
        }
        i = 1;
        for (ast_list_t *target = assign->targets; target; target = target->next) {
            code = CORD_cat(code, compile_assignment(env, target->ast, CORD_asprintf("$%ld", i++)));
        }
        return CORD_cat(code, "\n}");
    }
    case UpdateAssign: {
        auto update = Match(ast, UpdateAssign);
        CORD lhs = compile_lvalue(env, update->lhs);
        CORD rhs = compile(env, update->rhs);

        type_t *lhs_t = get_type(env, update->lhs);
        type_t *rhs_t = get_type(env, update->rhs);
        type_t *operand_t;
        if (promote(env, &rhs, rhs_t, lhs_t))
            operand_t = lhs_t;
        else if (promote(env, &lhs, lhs_t, rhs_t))
            operand_t = rhs_t;
        else if (lhs_t->tag == ArrayType && promote(env, &rhs, rhs_t, Match(lhs_t, ArrayType)->item_type))
            operand_t = lhs_t;
        else
            code_err(ast, "I can't do operations between %T and %T", lhs_t, rhs_t);
        
        switch (update->op) {
        case BINOP_MULT: return CORD_asprintf("%r *= %r;", lhs, rhs);
        case BINOP_DIVIDE: return CORD_asprintf("%r /= %r;", lhs, rhs);
        case BINOP_MOD: return CORD_asprintf("%r = %r %% %r;", lhs, lhs, rhs);
        case BINOP_MOD1: return CORD_asprintf("%r = (%r %% %r) + 1;", lhs, lhs, rhs);
        case BINOP_PLUS: return CORD_asprintf("%r += %r;", lhs, rhs);
        case BINOP_MINUS: return CORD_asprintf("%r -= %r;", lhs, rhs);
        case BINOP_POWER: {
            if (lhs_t->tag != NumType)
                code_err(ast, "'^=' is only supported for Num types");
            if (lhs_t->tag == NumType && Match(lhs_t, NumType)->bits == 32)
                return CORD_all(lhs, " = powf(", lhs, ", ", rhs, ");");
            else
                return CORD_all(lhs, " = pow(", lhs, ", ", rhs, ");");
        }
        case BINOP_LSHIFT: return CORD_asprintf("%r <<= %r;", lhs, rhs);
        case BINOP_RSHIFT: return CORD_asprintf("%r >>= %r;", lhs, rhs);
        case BINOP_AND: {
            if (operand_t->tag == BoolType)
                return CORD_asprintf("if (%r) %r = %r;", lhs, lhs, rhs);
            else if (operand_t->tag == IntType)
                return CORD_asprintf("%r &= %r;", lhs, rhs);
            else
                code_err(ast, "'or=' is not implemented for %T types", operand_t);
        }
        case BINOP_OR: {
            if (operand_t->tag == BoolType)
                return CORD_asprintf("if (!(%r)) %r = %r;", lhs, lhs, rhs);
            else if (operand_t->tag == IntType)
                return CORD_asprintf("%r |= %r;", lhs, rhs);
            else
                code_err(ast, "'or=' is not implemented for %T types", operand_t);
        }
        case BINOP_XOR: return CORD_asprintf("%r ^= %r;", lhs, rhs);
        case BINOP_CONCAT: {
            if (operand_t->tag == TextType) {
                return CORD_asprintf("%r = CORD_cat(%r, %r);", lhs, lhs, rhs);
            } else if (operand_t->tag == ArrayType) {
                if (promote(env, &rhs, rhs_t, Match(lhs_t, ArrayType)->item_type)) {
                    // arr ++= item
                    if (update->lhs->tag == Var)
                        return CORD_all("Array$insert(&", lhs, ", stack(", rhs, "), 0, ", compile_type_info(env, operand_t), ");");
                    else
                        return CORD_all(lhs, "Array$concat(", lhs, ", Array(", rhs, "), ", compile_type_info(env, operand_t), ");");
                } else {
                    // arr ++= [...]
                    if (update->lhs->tag == Var)
                        return CORD_all("Array$insert_all(&", lhs, ", ", rhs, ", 0, ", compile_type_info(env, operand_t), ");");
                    else
                        return CORD_all(lhs, "Array$concat(", lhs, ", ", rhs, ", ", compile_type_info(env, operand_t), ");");
                }
            } else {
                code_err(ast, "'++=' is not implemented for %T types", operand_t);
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
                     env->file_prefix, def->name, sizeof(CORD), __alignof__(CORD),
                     Text$quoted(def->name, false));
        compile_namespace(env, def->name, def->namespace);
        return CORD_EMPTY;
    }
    case FunctionDef: {
        auto fndef = Match(ast, FunctionDef);
        bool is_private = Match(fndef->name, Var)->name[0] == '_';
        CORD name = is_private ? CORD_cat("$", Match(fndef->name, Var)->name) : compile(env, fndef->name);
        type_t *ret_t = fndef->ret_type ? parse_type_ast(env, fndef->ret_type) : Type(VoidType);

        CORD arg_signature = "(";
        for (arg_ast_t *arg = fndef->args; arg; arg = arg->next) {
            type_t *arg_type = get_arg_ast_type(env, arg);
            arg_signature = CORD_cat(arg_signature, compile_declaration(env, arg_type, CORD_cat("$", arg->name)));
            if (arg->next) arg_signature = CORD_cat(arg_signature, ", ");
        }
        arg_signature = CORD_cat(arg_signature, ")");

        CORD ret_type_code = compile_type(env, ret_t);

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
        body_scope->scope_prefix = CORD_EMPTY;
        for (arg_ast_t *arg = fndef->args; arg; arg = arg->next) {
            type_t *arg_type = get_arg_ast_type(env, arg);
            set_binding(body_scope, arg->name, new(binding_t, .type=arg_type, .code=CORD_cat("$", arg->name)));
        }

        fn_ctx_t fn_ctx = (fn_ctx_t){
            .return_type=ret_t,
            .closure_scope=NULL,
            .closed_vars=NULL,
        };
        body_scope->fn_ctx = &fn_ctx;

        CORD body = compile_statement(body_scope, fndef->body);
        if (CORD_fetch(body, 0) != '{')
            body = CORD_asprintf("{\n%r\n}", body);
        env->code->funcs = CORD_all(env->code->funcs, code, " ", body, "\n");

        if (fndef->cache && fndef->cache->tag == Int && Match(fndef->cache, Int)->i > 0) {
            const char *arg_type_name = heap_strf("%s$args", CORD_to_const_char_star(name));
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
            if (fndef->cache->tag == Int && Match(fndef->cache, Int)->i < INT64_MAX) {
                pop_code = CORD_all("if (Table$length(cache) > ", compile(body_scope, fndef->cache),
                                    ") Table$remove(&cache, NULL, table_type);\n");
            }

            CORD arg_typedef = compile_struct_header(env, args_def);
            env->code->local_typedefs = CORD_all(env->code->local_typedefs, arg_typedef);
            CORD wrapper = CORD_all(
                is_private ? CORD_EMPTY : "public ", ret_type_code, " ", name, arg_signature, "{\n"
                "static table_t cache = {};\n",
                compile_type(env, args_t), " args = {", all_args, "};\n"
                "const TypeInfo *table_type = $TableInfo(", compile_type_info(env, args_t), ", ", compile_type_info(env, ret_t), ");\n",
                ret_type_code, "*cached = Table$get_raw(cache, &args, table_type);\n"
                "if (cached) return *cached;\n",
                ret_type_code, " ret = ", name, "$uncached(", all_args, ");\n",
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
            if (!target || CORD_cmp(target, ctx->loop_name) == 0
                || CORD_cmp(target, ctx->key_name) == 0 || CORD_cmp(target, ctx->value_name) == 0) {
                if (!ctx->skip_label) {
                    static int64_t skip_label_count = 1;
                    CORD_sprintf(&ctx->skip_label, "skip_%ld", skip_label_count);
                    ++skip_label_count;
                }
                return CORD_all("goto ", ctx->skip_label, ";");
            }
        }
        if (env->loop_ctx)
            code_err(ast, "This 'skip' is not inside any loop");
        else if (target)
            code_err(ast, "No loop target named '%s' was found", target);
        else
            code_err(ast, "I couldn't figure out how to make this skip work!");
    }
    case Stop: {
        const char *target = Match(ast, Stop)->target;
        for (loop_ctx_t *ctx = env->loop_ctx; ctx; ctx = ctx->next) {
            if (!target || CORD_cmp(target, ctx->loop_name) == 0 
                || CORD_cmp(target, ctx->key_name) == 0 || CORD_cmp(target, ctx->value_name) == 0) {
                if (!ctx->stop_label) {
                    static int64_t stop_label_count = 1;
                    CORD_sprintf(&ctx->stop_label, "stop_%ld", stop_label_count);
                    ++stop_label_count;
                }
                return CORD_all("goto ", ctx->stop_label, ";");
            }
        }
        if (env->loop_ctx)
            code_err(ast, "This 'stop' is not inside any loop");
        else if (target)
            code_err(ast, "No loop target named '%s' was found", target);
        else
            code_err(ast, "I couldn't figure out how to make this stop work!");
    }
    case Pass: return ";";
    case Return: {
        if (!env->fn_ctx) code_err(ast, "This return statement is not inside any function");
        auto ret = Match(ast, Return)->value;
        assert(env->fn_ctx->return_type);
        if (ret) {
            env = with_enum_scope(env, env->fn_ctx->return_type);
            type_t *ret_t = get_type(env, ret);
            CORD value = compile(env, ret);
            if (!promote(env, &value, ret_t, env->fn_ctx->return_type))
                code_err(ast, "This function expects a return value of type %T, but this return has type %T", 
                         env->fn_ctx->return_type, ret_t);
            return CORD_all("return ", value, ";");
        } else {
            if (env->fn_ctx->return_type->tag != VoidType)
                code_err(ast, "This function expects a return value of type %T", env->fn_ctx->return_type->tag);
            return "return;";
        }
    }
    case While: {
        auto while_ = Match(ast, While);
        env_t *scope = fresh_scope(env);
        loop_ctx_t loop_ctx = (loop_ctx_t){
            .loop_name="while",
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
        // TODO: optimize case for iterating over comprehensions so we don't need to create
        // an intermediary array/table
        type_t *iter_t = get_type(env, for_->iter);
        env_t *body_scope = for_scope(env, ast);
        loop_ctx_t loop_ctx = (loop_ctx_t){
            .loop_name="for",
            .key_name=for_->index ? Match(for_->index, Var)->name : CORD_EMPTY,
            .value_name=for_->value ? Match(for_->value, Var)->name : CORD_EMPTY,
            .next=body_scope->loop_ctx,
        };
        body_scope->loop_ctx = &loop_ctx;
        CORD body = compile_statement(body_scope, for_->body);
        if (loop_ctx.skip_label)
            body = CORD_all(body, "\n", loop_ctx.skip_label, ": continue;");
        CORD stop = loop_ctx.stop_label ? CORD_all("\n", loop_ctx.stop_label, ":;") : CORD_EMPTY;

        switch (iter_t->tag) {
        case ArrayType: {
            type_t *item_t = Match(iter_t, ArrayType)->item_type;
            CORD index = for_->index ? compile(env, for_->index) : "i";
            CORD value = compile(env, for_->value);
            CORD array = is_idempotent(for_->iter) ? compile(env, for_->iter) : "arr";
            CORD loop = CORD_all("ARRAY_INCREF(", array, ");\n"
                                 "for (int64_t ", index, " = 1; ", index, " <= ", array, ".length; ++", index, ") {\n",
                                 compile_type(env, item_t), " ", value,
                                 " = *(", compile_type(env, item_t), "*)(", array, ".data + (",index,"-1)*", array, ".stride);\n",
                                 body, "\n}");
            if (for_->empty)
                loop = CORD_all("if (", array, ".length > 0) {\n", loop, "\n} else ", compile_statement(env, for_->empty));
            loop = CORD_all(loop, stop, "\nARRAY_DECREF(", array, ");\n");
            if (!is_idempotent(for_->iter))
                loop = CORD_all("{\narray_t ",array," = ", compile(env, for_->iter), ";\n", loop, "\n}");
            return loop;
        }
        case TableType: {
            type_t *key_t = Match(iter_t, TableType)->key_type;
            type_t *value_t = Match(iter_t, TableType)->value_type;

            CORD table = is_idempotent(for_->iter) ? compile(env, for_->iter) : "table";
            CORD loop = CORD_all("ARRAY_INCREF(", table, ".entries);\n"
                                 "for (int64_t i = 0; i < ",table,".entries.length; ++i) {\n");
            if (for_->index) {
                loop = CORD_all(loop, compile_type(env, key_t), " ", compile(env, for_->index), " = *(", compile_type(env, key_t), "*)(",
                                table,".entries.data + i*", table, ".entries.stride);\n");

                size_t value_offset = type_size(key_t);
                if (type_align(value_t) > 1 && value_offset % type_align(value_t))
                    value_offset += type_align(value_t) - (value_offset % type_align(value_t)); // padding
                loop = CORD_all(loop, compile_type(env, value_t), " ", compile(env, for_->value), " = *(", compile_type(env, value_t), "*)(",
                                table,".entries.data + i*", table, ".entries.stride + ", heap_strf("%zu", value_offset), ");\n");
            } else {
                loop = CORD_all(loop, compile_type(env, key_t), " ", compile(env, for_->value), " = *(", compile_type(env, key_t), "*)(",
                                table,".entries.data + i*", table, ".entries.stride);\n");
            }
            loop = CORD_all(loop, body, "\n}");
            if (for_->empty)
                loop = CORD_all("if (", table, ".entries.length > 0) {\n", loop, "\n} else ", compile_statement(env, for_->empty));
            loop = CORD_all(loop, stop, "\nARRAY_DECREF(", table, ".entries);\n");
            if (!is_idempotent(for_->iter))
                loop = CORD_all("{\ntable_t ",table," = ", compile(env, for_->iter), ";\n", loop, "\n}");
            return loop;
        }
        case IntType: {
            CORD value = compile(env, for_->value);
            CORD n = compile(env, for_->iter);
            CORD index = for_->index ? compile(env, for_->index) : CORD_EMPTY;
            if (for_->empty && index) {
                return CORD_all(
                    "{\n"
                    "int64_t n = ", n, ";\n"
                    "if (n > 0) {\n"
                    "for (int64_t ", index, " = 1, ", value, "; (", value, "=", index,") <= n; ++", index, ") {\n"
                    "\t", body, "\n}"
                    "\n} else ", compile_statement(env, for_->empty),
                    stop, "\n}");
            } else if (for_->empty) {
                return CORD_all(
                    "{\n"
                    "int64_t n = ", n, ";\n"
                    "if (n > 0) {\n"
                    "for (int64_t ", value, " = 1; ", value, " <= n; ++", value, ") {\n"
                    "\t", body,
                    "\n}"
                    "\n} else ", compile_statement(env, for_->empty),
                    stop,
                    "\n}");
            } else if (index) {
                return CORD_all(
                    "for (int64_t ", value, ", ", index, " = 1, n = ", n, "; (", value, "=", index,") <= n; ++", value, ") {\n"
                    "\t", body,
                    "\n}",
                    stop,
                    "\n");
            } else {
                return CORD_all(
                    "for (int64_t ", value, " = 1, n = ", compile(env, for_->iter), "; ", value, " <= n; ++", value, ") {\n"
                    "\t", body,
                    "\n}",
                    stop,
                    "\n");
            }
        }
        default: code_err(for_->iter, "Iteration is not implemented for type: %T", iter_t);
        }
    }
    case If: {
        auto if_ = Match(ast, If);
        type_t *cond_t = get_type(env, if_->condition);
        if (cond_t->tag == PointerType) {
            if (!Match(cond_t, PointerType)->is_optional)
                code_err(if_->condition, "This pointer will always be non-null, so it should not be used in a conditional.");
        } else if (cond_t->tag != BoolType) {
            code_err(if_->condition, "Only boolean values and optional pointers can be used in conditionals (this is a %T)", cond_t);
        }
        CORD code;
        CORD_sprintf(&code, "if (%r) %r", compile(env, if_->condition), compile_statement(env, if_->body));
        if (if_->else_body)
            code = CORD_all(code, "\nelse ", compile_statement(env, if_->else_body));
        return code;
    }
    case Block: {
        ast_list_t *stmts = Match(ast, Block)->statements;
        CORD code = "{\n";
        env = fresh_scope(env);
        for (ast_list_t *stmt = stmts; stmt; stmt = stmt->next)
            prebind_statement(env, stmt->ast);
        for (ast_list_t *stmt = stmts; stmt; stmt = stmt->next) {
            bind_statement(env, stmt->ast);
            code = CORD_all(code, compile_statement(env, stmt->ast), "\n");
        }
        return CORD_cat(code, "}");
    }
    case Comprehension: {
        auto comp = Match(ast, Comprehension);
        assert(env->comprehension_var);
        if (comp->expr->tag == Comprehension) { // Nested comprehension
            ast_t *body = comp->filter ? WrapAST(ast, If, .condition=comp->filter, .body=comp->expr) : comp->expr;
            ast_t *loop = WrapAST(ast, For, .index=comp->key, .value=comp->value, .iter=comp->iter, .body=body);
            return compile_statement(env, loop);
        } else if (comp->expr->tag == TableEntry) { // Table comprehension
            auto e = Match(comp->expr, TableEntry);
            ast_t *body = WrapAST(ast, MethodCall, .name="set", .self=FakeAST(StackReference, FakeAST(Var, env->comprehension_var)),
                                  .args=new(arg_ast_t, .value=e->key, .next=new(arg_ast_t, .value=e->value)));
            if (comp->filter)
                body = WrapAST(body, If, .condition=comp->filter, .body=body);
            ast_t *loop = WrapAST(ast, For, .index=comp->key, .value=comp->value, .iter=comp->iter, .body=body);
            return compile_statement(env, loop);
        } else { // Array comprehension
            ast_t *body = WrapAST(comp->expr, MethodCall, .name="insert", .self=FakeAST(StackReference, FakeAST(Var, env->comprehension_var)),
                                  .args=new(arg_ast_t, .value=comp->expr));
            if (comp->filter)
                body = WrapAST(body, If, .condition=comp->filter, .body=body);
            ast_t *loop = WrapAST(ast, For, .index=comp->key, .value=comp->value, .iter=comp->iter, .body=body);
            return compile_statement(env, loop);
        }
    }
    case Extern: return CORD_EMPTY;
    case InlineCCode: return Match(ast, InlineCCode)->code;
    case Use: return CORD_EMPTY;
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
    case MemoryType: return CORD_asprintf("Memory$as_text(stack(%r), %r, &$Memory)", expr, color);
    case BoolType: return CORD_asprintf("Bool$as_text(stack(%r), %r, &$Bool)", expr, color);
    case CStringType: return CORD_asprintf("CString$as_text(stack(%r), %r, &$CString)", expr, color);
    case IntType: {
        CORD name = type_to_cord(t);
        return CORD_asprintf("%r$as_text(stack(%r), %r, &$%r)", name, expr, color, name);
    }
    case NumType: {
        CORD name = type_to_cord(t);
        return CORD_asprintf("%r$as_text(stack(%r), %r, &$%r)", name, expr, color, name);
    }
    case TextType: {
        const char *lang = Match(t, TextType)->lang;
        return CORD_asprintf("Text$as_text(stack(%r), %r, &%s)", expr, color, lang ? lang : "$Text");
    }
    case ArrayType: return CORD_asprintf("Array$as_text(stack(%r), %r, %r)", expr, color, compile_type_info(env, t));
    case TableType: return CORD_asprintf("Table$as_text(stack(%r), %r, %r)", expr, color, compile_type_info(env, t));
    case FunctionType: case ClosureType: return CORD_asprintf("Func$as_text(stack(%r), %r, %r)", expr, color, compile_type_info(env, t));
    case PointerType: return CORD_asprintf("Pointer$as_text(stack(%r), %r, %r)", expr, color, compile_type_info(env, t));
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

CORD compile_to_pointer_depth(env_t *env, ast_t *ast, int64_t target_depth, bool allow_optional)
{
    CORD val = compile(env, ast);
    type_t *t = get_type(env, ast);
    int64_t depth = 0;
    for (type_t *tt = t; tt->tag == PointerType; tt = Match(tt, PointerType)->pointed)
        ++depth;

    while (depth != target_depth) {
        if (depth < target_depth) {
            if (ast->tag == Var && target_depth == 1)
                val = CORD_all("(&", val, ")");
            else
                val = CORD_all("stack(", val, ")");
            t = Type(PointerType, .pointed=t, .is_stack=true);
            ++depth;
        } else {
            auto ptr = Match(t, PointerType);
            if (ptr->is_optional)
                code_err(ast, "You can't dereference this value, since it's not guaranteed to be non-null");
            val = CORD_all("*(", val, ")");
            t = ptr->pointed;
            --depth;
        }
    }
    if (!allow_optional) {
        while (t->tag == PointerType) {
            auto ptr = Match(t, PointerType);
            if (ptr->is_optional)
                code_err(ast, "You can't dereference this value, since it's not guaranteed to be non-null");
            t = ptr->pointed;
        }
    }

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

static CORD compile_arguments(env_t *env, ast_t *call_ast, arg_t *spec_args, arg_ast_t *call_args)
{
    table_t used_args = {};
    CORD code = CORD_EMPTY;
    env_t *default_scope = global_scope(env);
    for (arg_t *spec_arg = spec_args; spec_arg; spec_arg = spec_arg->next) {
        // Find keyword:
        if (spec_arg->name) {
            for (arg_ast_t *call_arg = call_args; call_arg; call_arg = call_arg->next) {
                if (call_arg->name && streq(call_arg->name, spec_arg->name)) {
                    env_t *arg_env = with_enum_scope(env, spec_arg->type);
                    type_t *actual_t = get_type(arg_env, call_arg->value);
                    CORD value = compile(arg_env, call_arg->value);
                    if (!promote(arg_env, &value, actual_t, spec_arg->type))
                        code_err(call_arg->value, "This argument is supposed to be a %T, but this value is a %T", spec_arg->type, actual_t);
                    Table$str_set(&used_args, call_arg->name, call_arg);
                    if (code) code = CORD_cat(code, ", ");
                    code = CORD_cat(code, value);
                    goto found_it;
                }
            }
        }
        // Find positional:
        int64_t i = 1;
        for (arg_ast_t *call_arg = call_args; call_arg; call_arg = call_arg->next) {
            if (call_arg->name) continue;
            const char *pseudoname = heap_strf("%ld", i++);
            if (!Table$str_get(used_args, pseudoname)) {
                env_t *arg_env = with_enum_scope(env, spec_arg->type);
                type_t *actual_t = get_type(arg_env, call_arg->value);
                CORD value = compile(arg_env, call_arg->value);
                if (!promote(arg_env, &value, actual_t, spec_arg->type))
                    code_err(call_arg->value, "This argument is supposed to be a %T, but this value is a %T", spec_arg->type, actual_t);
                Table$str_set(&used_args, pseudoname, call_arg);
                if (code) code = CORD_cat(code, ", ");
                code = CORD_cat(code, value);
                goto found_it;
            }
        }

        if (spec_arg->default_val) {
            if (code) code = CORD_cat(code, ", ");
            code = CORD_cat(code, compile(default_scope, spec_arg->default_val));
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

CORD compile(env_t *env, ast_t *ast)
{
    switch (ast->tag) {
    case Nil: {
        type_t *t = parse_type_ast(env, Match(ast, Nil)->type);
        return CORD_all("((", compile_type(env, t), ")NULL)");
    }
    case Bool: return Match(ast, Bool)->b ? "yes" : "no";
    case Var: {
        binding_t *b = get_binding(env, Match(ast, Var)->name);
        if (b)
            return b->code ? b->code : CORD_cat("$", Match(ast, Var)->name);
        return CORD_cat("$", Match(ast, Var)->name);
        // code_err(ast, "I don't know of any variable by this name");
    }
    case Int: return CORD_asprintf("I%ld(%ld)", Match(ast, Int)->bits, Match(ast, Int)->i);
    case Num: {
        return CORD_asprintf(Match(ast, Num)->bits == 64 ? "%.9g" : "(float)%.9g", Match(ast, Num)->n);
    }
    case Length: {
        ast_t *expr = Match(ast, Length)->value;
        type_t *t = get_type(env, expr);
        switch (value_type(t)->tag) {
        case TextType: {
            CORD str = compile_to_pointer_depth(env, expr, 0, false);
            return CORD_all("Text$num_clusters(", str, ")");
        }
        case ArrayType: {
            if (t->tag == PointerType) {
                CORD arr = compile_to_pointer_depth(env, expr, 1, false);
                return CORD_all("I64((", arr, ")->length)");
            } else {
                CORD arr = compile_to_pointer_depth(env, expr, 0, false);
                return CORD_all("I64((", arr, ").length)");
            }
        }
        case TableType: {
            if (t->tag == PointerType) {
                CORD table = compile_to_pointer_depth(env, expr, 1, false);
                return CORD_all("I64((", table, ")->entries.length)");
            } else {
                CORD table = compile_to_pointer_depth(env, expr, 0, false);
                return CORD_all("I64((", table, ").entries.length)");
            }
        }
        default: code_err(ast, "Length is only supported for strings, arrays, and tables, not: %T", t);
        }
        break;
    }
    case Not: {
        type_t *t = get_type(env, ast);
        ast_t *value = Match(ast, Not)->value;
        if (t->tag == BoolType)
            return CORD_all("!(", compile(env, value), ")");
        else if (t->tag == IntType)
            return CORD_all("~(", compile(env, value), ")");
        else if (t->tag == ArrayType || t->tag == TableType)
            return CORD_all("!(", compile(env, WrapAST(ast, Length, value)), ")");
        else if (t->tag == TextType)
            return CORD_all("!(", compile(env, value), ")");
        else
            code_err(ast, "I don't know how to negate values of type %T", t);
    }
    case Negative: return CORD_asprintf("-(%r)", compile(env, Match(ast, Negative)->value));
    case HeapAllocate: return CORD_asprintf("heap(%r)", compile(env, Match(ast, HeapAllocate)->value));
    case StackReference: {
        ast_t *subject = Match(ast, StackReference)->value;
        if (can_be_mutated(env, subject))
            return CORD_all("(&", compile(env, subject), ")");
        return CORD_all("stack(", compile(env, subject), ")");
    }
    case Optional: {
        return compile(env, Match(ast, Optional)->value);
    }
    case BinaryOp: {
        auto binop = Match(ast, BinaryOp);
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
            if (operand_t->tag != NumType && operand_t->tag != IntType)
                code_err(ast, "Exponentiation is only supported for numeric types");
            if (operand_t->tag == NumType && Match(operand_t, NumType)->bits == 32)
                return CORD_all("powf(", lhs, ", ", rhs, ")");
            else
                return CORD_all("pow(", lhs, ", ", rhs, ")");
        }
        case BINOP_MULT: {
            if (operand_t->tag != IntType && operand_t->tag != NumType)
                code_err(ast, "Math operations are only supported for numeric types");
            return CORD_asprintf("(%r * %r)", lhs, rhs);
        }
        case BINOP_DIVIDE: {
            if (operand_t->tag != IntType && operand_t->tag != NumType)
                code_err(ast, "Math operations are only supported for numeric types");
            return CORD_asprintf("(%r / %r)", lhs, rhs);
        }
        case BINOP_MOD: {
            if (operand_t->tag != IntType && operand_t->tag != NumType)
                code_err(ast, "Math operations are only supported for numeric types");
            return CORD_asprintf("(%r %% %r)", lhs, rhs);
        }
        case BINOP_MOD1: {
            if (operand_t->tag != IntType && operand_t->tag != NumType)
                code_err(ast, "Math operations are only supported for numeric types");
            return CORD_asprintf("((%r %% %r) + 1)", lhs, rhs);
        }
        case BINOP_PLUS: {
            if (operand_t->tag != IntType && operand_t->tag != NumType)
                code_err(ast, "Math operations are only supported for numeric types");
            return CORD_asprintf("(%r + %r)", lhs, rhs);
        }
        case BINOP_MINUS: {
            if (operand_t->tag != IntType && operand_t->tag != NumType)
                code_err(ast, "Math operations are only supported for numeric types");
            return CORD_asprintf("(%r - %r)", lhs, rhs);
        }
        case BINOP_LSHIFT: {
            if (operand_t->tag != IntType && operand_t->tag != NumType)
                code_err(ast, "Math operations are only supported for numeric types");
            return CORD_asprintf("(%r << %r)", lhs, rhs);
        }
        case BINOP_RSHIFT: {
            if (operand_t->tag != IntType && operand_t->tag != NumType)
                code_err(ast, "Math operations are only supported for numeric types");
            return CORD_asprintf("(%r >> %r)", lhs, rhs);
        }
        case BINOP_EQ: {
            switch (operand_t->tag) {
            case BoolType: case IntType: case NumType: case PointerType: case FunctionType:
                return CORD_asprintf("(%r == %r)", lhs, rhs);
            default:
                return CORD_asprintf("generic_equal(stack(%r), stack(%r), %r)", lhs, rhs, compile_type_info(env, operand_t));
            }
        }
        case BINOP_NE: {
            switch (operand_t->tag) {
            case BoolType: case IntType: case NumType: case PointerType: case FunctionType:
                return CORD_asprintf("(%r != %r)", lhs, rhs);
            default:
                return CORD_asprintf("!generic_equal(stack(%r), stack(%r), %r)", lhs, rhs, compile_type_info(env, operand_t));
            }
        }
        case BINOP_LT: {
            switch (operand_t->tag) {
            case BoolType: case IntType: case NumType: case PointerType: case FunctionType:
                return CORD_asprintf("(%r < %r)", lhs, rhs);
            default:
                return CORD_asprintf("(generic_compare(stack(%r), stack(%r), %r) < 0)", lhs, rhs, compile_type_info(env, operand_t));
            }
        }
        case BINOP_LE: {
            switch (operand_t->tag) {
            case BoolType: case IntType: case NumType: case PointerType: case FunctionType:
                return CORD_asprintf("(%r <= %r)", lhs, rhs);
            default:
                return CORD_asprintf("(generic_compare(stack(%r), stack(%r), %r) <= 0)", lhs, rhs, compile_type_info(env, operand_t));
            }
        }
        case BINOP_GT: {
            switch (operand_t->tag) {
            case BoolType: case IntType: case NumType: case PointerType: case FunctionType:
                return CORD_asprintf("(%r > %r)", lhs, rhs);
            default:
                return CORD_asprintf("(generic_compare(stack(%r), stack(%r), %r) > 0)", lhs, rhs, compile_type_info(env, operand_t));
            }
        }
        case BINOP_GE: {
            switch (operand_t->tag) {
            case BoolType: case IntType: case NumType: case PointerType: case FunctionType:
                return CORD_asprintf("(%r >= %r)", lhs, rhs);
            default:
                return CORD_asprintf("(generic_compare(stack(%r), stack(%r), %r) >= 0)", lhs, rhs, compile_type_info(env, operand_t));
            }
        }
        case BINOP_AND: {
            if (operand_t->tag == BoolType)
                return CORD_asprintf("(%r && %r)", lhs, rhs);
            else if (operand_t->tag == IntType)
                return CORD_asprintf("(%r & %r)", lhs, rhs);
            else
                code_err(ast, "Boolean operators are only supported for Bool and integer types");
        }
        case BINOP_CMP: {
            return CORD_all("generic_compare(stack(", lhs, "), stack(", rhs, "), ", compile_type_info(env, operand_t), ")");
        }
        case BINOP_OR: {
            if (operand_t->tag == BoolType)
                return CORD_asprintf("(%r || %r)", lhs, rhs);
            else if (operand_t->tag == IntType)
                return CORD_asprintf("(%r | %r)", lhs, rhs);
            else
                code_err(ast, "Boolean operators are only supported for Bool and integer types");
        }
        case BINOP_XOR: {
            if (operand_t->tag == BoolType || operand_t->tag == IntType)
                return CORD_asprintf("(%r ^ %r)", lhs, rhs);
            else
                code_err(ast, "Boolean operators are only supported for Bool and integer types");
        }
        case BINOP_CONCAT: {
            switch (operand_t->tag) {
            case TextType: {
                return CORD_all("CORD_cat(", lhs, ", ", rhs, ")");
            }
            case ArrayType: {
                return CORD_all("Array$concat(", lhs, ", ", rhs, ", ", compile_type_info(env, operand_t), ")");
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
            return "(CORD)CORD_EMPTY";
        CORD code = "(CORD)\"";
        CORD_pos i;
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
                    CORD_sprintf(&code, "%r\\x%02X", code, (uint8_t)c);
                break;
            }
            }
        }
        return CORD_cat_char(code, '"');
    }
    case TextJoin: {
        const char *lang = Match(ast, TextJoin)->lang;
        type_t *text_t = Table$str_get(*env->types, lang ? lang : "Text");
        if (!text_t || text_t->tag != TextType)
            code_err(ast, "%s is not a valid text language name", lang);
        env_t *lang_env = lang ? Match(get_binding(env, lang)->type, TypeInfoType)->env : NULL;
        ast_list_t *chunks = Match(ast, TextJoin)->children;
        if (!chunks) {
            return "(CORD)CORD_EMPTY";
        } else if (!chunks->next && chunks->ast->tag == TextLiteral) {
            return compile(env, chunks->ast);
        } else {
            CORD code = "CORD_all(";
            for (ast_list_t *chunk = chunks; chunk; chunk = chunk->next) {
                CORD chunk_code;
                type_t *chunk_t = get_type(env, chunk->ast);
                if (chunk->ast->tag == TextLiteral) {
                    chunk_code = compile(env, chunk->ast);
                } else if (chunk_t->tag == TextType && streq(Match(chunk_t, TextType)->lang, lang)) {
                    chunk_code = compile(env, chunk->ast);
                } else if (lang && lang_env) {
                    // Get conversion function:
                    chunk_code = compile(env, chunk->ast);
                    for (int64_t i = 1; i <= Table$length(*lang_env->locals); i++) {
                        struct {const char *name; binding_t *b; } *entry = Table$entry(*lang_env->locals, i);
                        if (entry->b->type->tag != FunctionType) continue;
                        if (!(streq(entry->name, "escape") || strncmp(entry->name, "escape_", strlen("escape_")) == 0))
                            continue;
                        auto fn = Match(entry->b->type, FunctionType);
                        if (!fn->args || fn->args->next) continue;
                        if (fn->ret->tag != TextType || !streq(Match(fn->ret, TextType)->lang, lang))
                            continue;
                        if (!promote(env, &chunk_code, chunk_t, fn->args->type))
                            continue;
                        chunk_code = CORD_all(entry->b->code, "(", chunk_code, ")");
                        goto found_conversion;
                    }
                    code_err(chunk->ast, "I don't know how to convert %T to %T", chunk_t, text_t);
                  found_conversion:;
                } else {
                    chunk_code = compile_string(env, chunk->ast, "no");
                }
                code = CORD_cat(code, chunk_code);
                if (chunk->next) code = CORD_cat(code, ", ");
            }
            return CORD_cat(code, ")");
        }
    }
    case Block: {
        ast_list_t *stmts = Match(ast, Block)->statements;
        if (stmts && !stmts->next)
            return compile(env, stmts->ast);

        CORD code = "({\n";
        env = fresh_scope(env);
        for (ast_list_t *stmt = stmts; stmt; stmt = stmt->next)
            prebind_statement(env, stmt->ast);
        for (ast_list_t *stmt = stmts; stmt; stmt = stmt->next) {
            bind_statement(env, stmt->ast);
            if (stmt->next) {
                code = CORD_all(code, compile_statement(env, stmt->ast), "\n");
            } else {
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
        if (key_t->tag == IntType || key_t->tag == NumType || key_t->tag == BoolType || key_t->tag == PointerType)
            comparison = CORD_all("((", lhs_key, ")", (ast->tag == Min ? "<=" : ">="), "(", rhs_key, "))");
        else if (key_t->tag == TextType)
            comparison = CORD_all("CORD_cmp(", lhs_key, ", ", rhs_key, ")", (ast->tag == Min ? "<=" : ">="), "0");
        else
            comparison = CORD_all("generic_compare(stack(", lhs_key, "), stack(", rhs_key, "), ", compile_type_info(env, key_t), ")",
                                  (ast->tag == Min ? "<=" : ">="), "0");

        return CORD_all(
            "({\n",
            compile_type(env, t), " ternary$lhs = ", compile(env, lhs), ", ternary$rhs = ", compile(env, rhs), ";\n",
            comparison, " ? ternary$lhs : ternary$rhs;\n"
            "})");
    }
    case Array: {
        auto array = Match(ast, Array);
        if (!array->items)
            return "(array_t){.length=0}";

        type_t *array_type = get_type(env, ast);

        int64_t n = 0;
        for (ast_list_t *item = array->items; item; item = item->next) {
            ++n;
            if (item->ast->tag == Comprehension)
                goto array_comprehension;
        }

        {
            type_t *item_type = Match(array_type, ArrayType)->item_type;
            CORD code = CORD_all("TypedArrayN(", compile_type(env, item_type), CORD_asprintf(", %ld", n));
            for (ast_list_t *item = array->items; item; item = item->next)
                code = CORD_all(code, ", ", compile(env, item->ast));
            return CORD_cat(code, ")");
        }

      array_comprehension:
        {
            env_t *scope = fresh_scope(env);
            static int64_t comp_num = 1;
            scope->comprehension_var = heap_strf("arr$%ld", comp_num++);
            CORD code = CORD_all("({ array_t ", scope->comprehension_var, " = {};");
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
    case Table: {
        auto table = Match(ast, Table);
        if (!table->entries) {
            CORD code = "(table_t){";
            if (table->fallback)
                code = CORD_all(code, ".fallback=", compile(env, table->fallback),",");
            if (table->default_value)
                code = CORD_all(code, ".default_value=heap(", compile(env, table->default_value),"),");
            return CORD_cat(code, "}");
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
                                 compile_type(env, key_t), ", ",
                                 compile_type(env, value_t), ", ",
                                 compile_type_info(env, key_t), ", ",
                                 compile_type_info(env, value_t));
            if (table->fallback)
                code = CORD_all(code, ", /*fallback:*/ heap(", compile(env, table->fallback), ")");
            else
                code = CORD_all(code, ", /*fallback:*/ NULL");

            if (table->default_value)
                code = CORD_all(code, ", /*default:*/ heap(", compile(env, table->default_value), ")");
            else
                code = CORD_all(code, ", /*default:*/ NULL");

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

            CORD code = CORD_all("({ table_t ", scope->comprehension_var, " = {");
            if (table->fallback)
                code = CORD_all(code, ".fallback=heap(", compile(env, table->fallback), "), ");

            if (table->default_value)
                code = CORD_all(code, ".default_value=heap(", compile(env, table->default_value), "), ");
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
        CORD name = CORD_asprintf("%slambda$%ld", env->file_prefix, lambda->id);

        env_t *body_scope = fresh_scope(env);
        for (arg_ast_t *arg = lambda->args; arg; arg = arg->next) {
            type_t *arg_type = get_arg_ast_type(env, arg);
            set_binding(body_scope, arg->name, new(binding_t, .type=arg_type, .code=CORD_cat("$", arg->name)));
        }

        fn_ctx_t fn_ctx = (fn_ctx_t){
            .closure_scope=body_scope->locals->fallback,
            .closed_vars=new(table_t),
        };
        body_scope->fn_ctx = &fn_ctx;
        body_scope->locals->fallback = env->globals;
        type_t *ret_t = get_type(body_scope, lambda->body);
        fn_ctx.return_type = ret_t;

        CORD code = CORD_all("static ", compile_type(env, ret_t), " ", name, "(");
        for (arg_ast_t *arg = lambda->args; arg; arg = arg->next) {
            type_t *arg_type = get_arg_ast_type(env, arg);
            code = CORD_all(code, compile_type(env, arg_type), " $", arg->name, ", ");
        }

        CORD args_typedef = compile_statement_header(env, ast);
        env->code->local_typedefs = CORD_all(env->code->local_typedefs, args_typedef);

        table_t *closed_vars = get_closed_vars(env, ast);
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
                userdata = CORD_all(userdata, ", ", entry->b->code);
            }
            userdata = CORD_all(userdata, ")");
            code = CORD_all(code, name, "$userdata_t *userdata)");
        }

        CORD body = CORD_EMPTY;
        for (ast_list_t *stmt = Match(lambda->body, Block)->statements; stmt; stmt = stmt->next) {
            if (stmt->next || ret_t->tag == VoidType || ret_t->tag == AbortType)
                body = CORD_all(body, compile_statement(body_scope, stmt->ast), "\n");
            else
                body = CORD_all(body, compile_statement(body_scope, FakeAST(Return, stmt->ast)), "\n");
        }
        env->code->funcs = CORD_all(env->code->funcs, code, " {\n", body, "\n}");
        return CORD_all("(closure_t){", name, ", ", userdata, "}");
    }
    case MethodCall: {
        auto call = Match(ast, MethodCall);
        type_t *self_t = get_type(env, call->self);
        type_t *self_value_t = value_type(self_t);
        switch (self_value_t->tag) {
        case ArrayType: {
            // TODO: check for readonly
            type_t *item_t = Match(self_value_t, ArrayType)->item_type;
            if (streq(call->name, "insert")) {
                CORD self = compile_to_pointer_depth(env, call->self, 1, false);
                arg_t *arg_spec = new(arg_t, .name="item", .type=item_t,
                                      .next=new(arg_t, .name="at", .type=Type(IntType, .bits=64), .default_val=FakeAST(Int, .i=0, .bits=64)));
                return CORD_all("Array$insert_value(", self, ", ", compile_arguments(env, ast, arg_spec, call->args), ", ",
                                compile_type_info(env, self_value_t), ")");
            } else if (streq(call->name, "insert_all")) {
                CORD self = compile_to_pointer_depth(env, call->self, 1, false);
                arg_t *arg_spec = new(arg_t, .name="items", .type=self_value_t,
                                      .next=new(arg_t, .name="at", .type=Type(IntType, .bits=64), .default_val=FakeAST(Int, .i=0, .bits=64)));
                return CORD_all("Array$insert_all(", self, ", ", compile_arguments(env, ast, arg_spec, call->args), ", ",
                                compile_type_info(env, self_value_t), ")");
            } else if (streq(call->name, "remove")) {
                CORD self = compile_to_pointer_depth(env, call->self, 1, false);
                arg_t *arg_spec = new(arg_t, .name="index", .type=Type(IntType, .bits=64), .default_val=FakeAST(Int, .i=-1, .bits=64),
                                      .next=new(arg_t, .name="count", .type=Type(IntType, .bits=64), .default_val=FakeAST(Int, .i=1, .bits=64)));
                return CORD_all("Array$remove(", self, ", ", compile_arguments(env, ast, arg_spec, call->args), ", ",
                                compile_type_info(env, self_value_t), ")");
            } else if (streq(call->name, "random")) {
                CORD self = compile_to_pointer_depth(env, call->self, 0, false);
                (void)compile_arguments(env, ast, NULL, call->args);
                return CORD_all("Array$random(", self, ")");
            } else if (streq(call->name, "sample")) {
                CORD self = compile_to_pointer_depth(env, call->self, 0, false);
                arg_t *arg_spec = new(arg_t, .name="count", .type=Type(IntType, .bits=64),
                                      .next=new(arg_t, .name="weights", .type=Type(ArrayType, .item_type=Type(NumType, .bits=64)),
                                                .default_val=FakeAST(Array, .type=new(type_ast_t, .tag=VarTypeAST, .__data.VarTypeAST.name="Num"))));
                return CORD_all("Array$sample(", self, ", ", compile_arguments(env, ast, arg_spec, call->args), ", ",
                                compile_type_info(env, self_value_t), ")");
            } else if (streq(call->name, "shuffle")) {
                CORD self = compile_to_pointer_depth(env, call->self, 1, false);
                (void)compile_arguments(env, ast, NULL, call->args);
                return CORD_all("Array$shuffle(", self, ", ", compile_type_info(env, self_value_t), ")");
            } else if (streq(call->name, "sort") || streq(call->name, "sorted")) {
                CORD self = compile_to_pointer_depth(env, call->self, streq(call->name, "sort") ? 1 : 0, false);
                CORD comparison;
                if (call->args) {
                    type_t *item_ptr = Type(PointerType, .pointed=item_t, .is_stack=true, .is_readonly=true);
                    type_t *fn_t = Type(FunctionType, .args=new(arg_t, .name="x", .type=item_ptr, .next=new(arg_t, .name="y", .type=item_ptr)),
                                        .ret=Type(IntType, .bits=32));
                    arg_t *arg_spec = new(arg_t, .name="by", .type=Type(ClosureType, .fn=fn_t));
                    comparison = compile_arguments(env, ast, arg_spec, call->args);
                } else {
                    comparison = CORD_all("(closure_t){.fn=generic_compare, .userdata=(void*)", compile_type_info(env, item_t), "}");
                }
                return CORD_all("Array$", call->name, "(", self, ", ", comparison, ", ", compile_type_info(env, self_value_t), ")");
            } else if (streq(call->name, "heapify")) {
                CORD self = compile_to_pointer_depth(env, call->self, 1, false);
                CORD comparison;
                if (call->args) {
                    type_t *item_ptr = Type(PointerType, .pointed=item_t, .is_stack=true);
                    type_t *fn_t = Type(FunctionType, .args=new(arg_t, .name="x", .type=item_ptr, .next=new(arg_t, .name="y", .type=item_ptr)),
                                        .ret=Type(IntType, .bits=32));
                    arg_t *arg_spec = new(arg_t, .name="by", .type=Type(ClosureType, .fn=fn_t));
                    comparison = compile_arguments(env, ast, arg_spec, call->args);
                } else {
                    comparison = CORD_all("((closure_t){.fn=generic_compare, .userdata=(void*)", compile_type_info(env, item_t), "})");
                }
                return CORD_all("Array$heapify(", self, ", ", comparison, ", ", compile_type_info(env, self_value_t), ")");
            } else if (streq(call->name, "heap_push")) {
                CORD self = compile_to_pointer_depth(env, call->self, 1, false);
                type_t *item_ptr = Type(PointerType, .pointed=item_t, .is_stack=true);
                type_t *fn_t = Type(FunctionType, .args=new(arg_t, .name="x", .type=item_ptr, .next=new(arg_t, .name="y", .type=item_ptr)),
                                    .ret=Type(IntType, .bits=32));
                ast_t *default_cmp = FakeAST(InlineCCode, .code=CORD_all("((closure_t){.fn=generic_compare, .userdata=(void*)", compile_type_info(env, item_t), "})"), .type=NewTypeAST(NULL, NULL, NULL, FunctionTypeAST));
                arg_t *arg_spec = new(arg_t, .name="item", .type=item_t, .next=new(arg_t, .name="by", .type=Type(ClosureType, .fn=fn_t), .default_val=default_cmp));
                CORD arg_code = compile_arguments(env, ast, arg_spec, call->args);
                return CORD_all("Array$heap_push_value(", self, ", ", arg_code, ", ", compile_type_info(env, self_value_t), ")");
            } else if (streq(call->name, "heap_pop")) {
                CORD self = compile_to_pointer_depth(env, call->self, 1, false);
                type_t *item_ptr = Type(PointerType, .pointed=item_t, .is_stack=true);
                type_t *fn_t = Type(FunctionType, .args=new(arg_t, .name="x", .type=item_ptr, .next=new(arg_t, .name="y", .type=item_ptr)),
                                    .ret=Type(IntType, .bits=32));
                ast_t *default_cmp = FakeAST(InlineCCode, .code=CORD_all("((closure_t){.fn=generic_compare, .userdata=(void*)", compile_type_info(env, item_t), "})"), .type=NewTypeAST(NULL, NULL, NULL, FunctionTypeAST));
                arg_t *arg_spec = new(arg_t, .name="by", .type=Type(ClosureType, .fn=fn_t), .default_val=default_cmp);
                CORD arg_code = compile_arguments(env, ast, arg_spec, call->args);
                return CORD_all("Array$heap_pop_value(", self, ", ", arg_code, ", ", compile_type_info(env, self_value_t), ", ", compile_type(env, item_t), ")");
            } else if (streq(call->name, "clear")) {
                CORD self = compile_to_pointer_depth(env, call->self, 1, false);
                (void)compile_arguments(env, ast, NULL, call->args);
                return CORD_all("Array$clear(", self, ")");
            } else if (streq(call->name, "slice")) {
                CORD self = compile_to_pointer_depth(env, call->self, 1, false);
                arg_t *arg_spec = new(arg_t, .name="first", .type=Type(IntType, .bits=64), .default_val=FakeAST(Int, .i=1, .bits=64),
                                      .next=new(arg_t, .name="length", .type=Type(IntType, .bits=64), .default_val=FakeAST(Int, .i=INT64_MAX, .bits=64),
                                                .next=new(arg_t, .name="stride", .type=Type(IntType, .bits=64), .default_val=FakeAST(Int, .i=1, .bits=64))));
                return CORD_all("Array$slice(", self, ", ", compile_arguments(env, ast, arg_spec, call->args), ", ",
                                compile_type_info(env, self_value_t), ")");
            } else if (streq(call->name, "reversed")) {
                CORD self = compile_to_pointer_depth(env, call->self, 0, false);
                (void)compile_arguments(env, ast, NULL, call->args);
                return CORD_all("Array$reversed(", self, ")");
            } else code_err(ast, "There is no '%s' method for arrays", call->name);
        }
        case TableType: {
            auto table = Match(self_value_t, TableType);
            if (streq(call->name, "get")) {
                CORD self = compile_to_pointer_depth(env, call->self, 0, false);
                arg_t *arg_spec = new(arg_t, .name="key", .type=Type(PointerType, .pointed=table->key_type, .is_stack=true, .is_readonly=true));
                return CORD_all("Table$get(", self, ", ", compile_arguments(env, ast, arg_spec, call->args), ", ",
                                compile_type_info(env, self_value_t), ")");
            } else if (streq(call->name, "set")) {
                CORD self = compile_to_pointer_depth(env, call->self, 1, false);
                arg_t *arg_spec = new(arg_t, .name="key", .type=table->key_type,
                                      .next=new(arg_t, .name="value", .type=table->value_type));
                return CORD_all("Table$set_value(", self, ", ", compile_arguments(env, ast, arg_spec, call->args), ", ",
                                compile_type_info(env, self_value_t), ")");
            } else if (streq(call->name, "remove")) {
                CORD self = compile_to_pointer_depth(env, call->self, 1, false);
                arg_t *arg_spec = new(arg_t, .name="key", .type=Type(PointerType, .pointed=table->key_type, .is_stack=true, .is_readonly=true));
                return CORD_all("Table$remove(", self, ", ", compile_arguments(env, ast, arg_spec, call->args), ", ",
                                compile_type_info(env, self_value_t), ")");
            } else if (streq(call->name, "clear")) {
                CORD self = compile_to_pointer_depth(env, call->self, 1, false);
                (void)compile_arguments(env, ast, NULL, call->args);
                return CORD_all("Table$clear(", self, ")");
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
                return CORD_all("((", compile_type(env, t), "){", compile_arguments(env, ast, Match(fn_t, FunctionType)->args, call->args), "})");
            } else if (t->tag == IntType || t->tag == NumType) {
                // Int/Num constructor:
                if (!call->args || call->args->next)
                    code_err(call->fn, "This constructor takes exactly 1 argument");
                type_t *actual = get_type(env, call->args->value);
                if (actual->tag != IntType && actual->tag != NumType)
                    code_err(call->args->value, "This %T value cannot be converted to a %T", actual, t);
                return CORD_all("((", compile_type(env, t), ")(", compile(env, call->args->value), "))");
            } else if (t->tag == TextType) {
                // Text constructor:
                if (!call->args || call->args->next)
                    code_err(call->fn, "This constructor takes exactly 1 argument");
                type_t *actual = get_type(env, call->args->value);
                return expr_as_text(env, compile(env, call->args->value), actual, "no");
            } else if (t->tag == CStringType) {
                // C String constructor:
                if (!call->args || call->args->next)
                    code_err(call->fn, "This constructor takes exactly 1 argument");
                type_t *actual = get_type(env, call->args->value);
                return CORD_all("CORD_to_char_star(", expr_as_text(env, compile(env, call->args->value), actual, "no"), ")");
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
            CORD fn_type_code = compile_type(env, Type(FunctionType, .args=closure_fn_args, .ret=Match(fn_t, FunctionType)->ret));

            CORD closure = compile(env, call->fn);
            CORD arg_code = compile_arguments(env, ast, type_args, call->args);
            if (arg_code) arg_code = CORD_cat(arg_code, ", ");
            if (call->fn->tag == Var) {
                return CORD_all("((", fn_type_code, ")", closure, ".fn)(", arg_code, closure, ".userdata)");
            } else {
                return CORD_all("({ closure_t closure = ", closure, "; ((", fn_type_code, ")closure.fn)(",
                                arg_code, "closure.userdata); })");
            }
        } else {
            code_err(call->fn, "This is not a function, it's a %T", fn_t);
        }
    }
    case When:
        code_err(ast, "'when' expressions are not yet implemented");
    case If: {
        auto if_ = Match(ast, If);
        if (!if_->else_body)
            code_err(ast, "'if' expressions can only be used if you also have an 'else' block");

        type_t *t = get_type(env, ast);
        if (t->tag == VoidType || t->tag == AbortType)
            code_err(ast, "This expression has a %T type, but it needs to have a real value", t);

        type_t *true_type = get_type(env, if_->body);
        type_t *false_type = get_type(env, if_->else_body);
        if (true_type->tag == AbortType)
            return CORD_all("({ if (", compile(env, if_->condition), ") ", compile_statement(env, if_->body),
                            "\n", compile(env, if_->else_body), "; })");
        else if (false_type->tag == AbortType)
            return CORD_all("({ if (!(", compile(env, if_->condition), ")) ", compile_statement(env, if_->else_body),
                            "\n", compile(env, if_->body), "; })");
        else
            return CORD_all("((", compile(env, if_->condition), ") ? ",
                            compile(env, if_->body), " : ", compile(env, if_->else_body), ")");
    }
    case Reduction: {
        auto reduction = Match(ast, Reduction);
        type_t *t = get_type(env, ast);
        CORD code = CORD_all(
            "({ // Reduction:\n",
            compile_declaration(env, t, "reduction"), ";\n"
            );
        env_t *scope = fresh_scope(env);
        ast_t *result = FakeAST(Var, "$reduction");
        set_binding(scope, "$reduction", new(binding_t, .type=t, .code="reduction"));
        ast_t *empty = NULL;
        if (reduction->fallback) {
            type_t *fallback_type = get_type(scope, reduction->fallback);
            if (fallback_type->tag == AbortType) {
                empty = reduction->fallback;
            } else {
                empty = FakeAST(Assign, .targets=new(ast_list_t, .ast=result), .values=new(ast_list_t, .ast=reduction->fallback));
            }
        } else {
            empty = FakeAST(
                InlineCCode, 
                CORD_asprintf("fail_source(%r, %ld, %ld, \"This collection was empty!\");\n",
                              Text$quoted(ast->file->filename, false), (long)(reduction->iter->start - reduction->iter->file->text),
                              (long)(reduction->iter->end - reduction->iter->file->text)));
        }
        ast_t *i = FakeAST(Var, "$i");
        ast_t *item = FakeAST(Var, "$iter_value");
        set_binding(scope, "$iter_value", new(binding_t, .type=t, .code="iter_value"));
        ast_t *body = FakeAST(InlineCCode, CORD_all("reduction = $$i == 1 ? iter_value : ", compile(scope, reduction->combination), ";"));
        ast_t *loop = FakeAST(For, .index=i, .value=item, .iter=reduction->iter, .body=body, .empty=empty);
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
            binding_t *b = get_binding(info->env, f->field);
            if (!b) code_err(ast, "I couldn't find the field '%s' on this type", f->field);
            if (!b->code) code_err(ast, "I couldn't figure out how to compile this field");
            return b->code;
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
            code_err(ast, "The field '%s' is not a valid field name of %T", f->field, value_t);
        }
        case TableType: {
            if (streq(f->field, "keys")) {
                return CORD_all("({ table_t *t = ", compile_to_pointer_depth(env, f->fielded, 1, false), ";\n"
                                "t->entries.data_refcount = 3;\n"
                                "t->entries; })");
            } else if (streq(f->field, "values")) {
                auto table = Match(value_t, TableType);
                size_t offset = type_size(table->key_type);
                size_t align = type_align(table->value_type);
                if (align > 1 && offset % align > 0)
                    offset += align - (offset % align);
                return CORD_all("({ table_t *t = ", compile_to_pointer_depth(env, f->fielded, 1, false), ";\n"
                                "t->entries.data_refcount = 3;\n"
                                "(array_t){.data = t->entries.data + ", CORD_asprintf("%zu", offset),
                                ",\n .length=t->entries.length,\n .stride=t->entries.stride,\n .data_refcount=3};})");
            } else if (streq(f->field, "fallback")) {
                return CORD_all("(", compile_to_pointer_depth(env, f->fielded, 0, false), ").fallback");
            } else if (streq(f->field, "default")) {
                return CORD_all("(", compile_to_pointer_depth(env, f->fielded, 0, false), ").default_value");
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
        if (!indexing->index && indexed_type->tag == PointerType) {
            auto ptr = Match(indexed_type, PointerType);
            if (ptr->is_optional)
                code_err(ast, "This pointer is potentially null, so it can't be safely dereferenced");
            if (ptr->pointed->tag == ArrayType) {
                return CORD_all("({ array_t *arr = ", compile(env, indexing->indexed), "; arr->data_refcount = 3; *arr; })");
            } else if (ptr->pointed->tag == TableType) {
                return CORD_all("({ table_t *t = ", compile(env, indexing->indexed), "; Table$mark_copy_on_write(t); *t; })");
            } else {
                return CORD_all("*(", compile(env, indexing->indexed), ")");
            }
        }
        type_t *container_t = value_type(indexed_type);
        type_t *index_t = get_type(env, indexing->index);
        switch (container_t->tag) {
        case ArrayType: {
            if (index_t->tag != IntType)
                code_err(indexing->index, "Arrays can only be indexed by integers, not %T", index_t);
            type_t *item_type = Match(container_t, ArrayType)->item_type;
            CORD arr = compile_to_pointer_depth(env, indexing->indexed, 0, false);
            CORD index = compile(env, indexing->index);
            file_t *f = indexing->index->file;
            if (indexing->unchecked)
                return CORD_all("Array_get_unchecked", compile_type(env, item_type), ", ", arr, ", ", index, ")");
            else
                return CORD_all("Array_get(", compile_type(env, item_type), ", ", arr, ", ", index, ", ",
                                Text$quoted(f->filename, false), ", ", CORD_asprintf("%ld", (int64_t)(indexing->index->start - f->text)), ", ",
                                CORD_asprintf("%ld", (int64_t)(indexing->index->end - f->text)),
                                ")");
        }
        case TableType: {
            type_t *key_t = Match(container_t, TableType)->key_type;
            type_t *value_t = Match(container_t, TableType)->value_type;
            CORD table = compile_to_pointer_depth(env, indexing->indexed, 0, false);
            CORD key = compile(env, indexing->index);
            if (!promote(env, &key, index_t, key_t))
                code_err(indexing->index, "This value has type %T, but this table can only be index with keys of type %T", index_t, key_t);
            file_t *f = indexing->index->file;
            return CORD_all("Table_get(", table, ", ", compile_type(env, key_t), ", ", compile_type(env, value_t), ", ",
                            key, ", ", compile_type_info(env, container_t), ", ",
                            Text$quoted(f->filename, false), ", ", CORD_asprintf("%ld", (int64_t)(indexing->index->start - f->text)), ", ",
                            CORD_asprintf("%ld", (int64_t)(indexing->index->end - f->text)),
                            ")");
        }
        default: code_err(ast, "Indexing is not supported for type: %T", container_t);
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
    case LinkerDirective: code_err(ast, "Linker directives are not supported yet");
    case Extern: code_err(ast, "Externs are not supported as expressions");
    case TableEntry: code_err(ast, "Table entries should not be compiled directly");
    case Declare: case Assign: case UpdateAssign: case For: case While: case StructDef: case LangDef:
    case EnumDef: case FunctionDef: case Skip: case Stop: case Pass: case Return: case DocTest:
        code_err(ast, "This is not a valid expression");
    case Unknown: code_err(ast, "Unknown AST");
    }
    code_err(ast, "Unknown AST: %W", ast);
    return CORD_EMPTY;
}

void compile_namespace(env_t *env, const char *ns_name, ast_t *block)
{
    env_t *ns_env = namespace_env(env, ns_name);
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

            if (!is_constant(env, decl->value))
                code_err(decl->value, "This value is supposed to be a compile-time constant, but I can't figure out how to make it one");
            CORD var_decl = CORD_all(compile_type(env, t), " ", compile(ns_env, decl->var), " = ", compile(ns_env, decl->value), ";\n");
            env->code->staticdefs = CORD_all(env->code->staticdefs, var_decl);
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

CORD compile_namespace_headers(env_t *env, const char *ns_name, ast_t *block)
{
    env_t *ns_env = namespace_env(env, ns_name);
    CORD header = CORD_EMPTY;
    for (ast_list_t *stmt = block ? Match(block, Block)->statements : NULL; stmt; stmt = stmt->next) {
        header = CORD_all(header, compile_statement_header(ns_env, stmt->ast));
    }
    return header;
}

CORD compile_type_info(env_t *env, type_t *t)
{
    switch (t->tag) {
    case BoolType: case IntType: case NumType: case CStringType:
        return CORD_asprintf("&$%r", type_to_cord(t));
    case TextType: {
        auto text = Match(t, TextType);
        return text->lang ? CORD_all("(&", text->env->file_prefix, text->lang, ")") : "&$Text";
    }
    case StructType: {
        auto s = Match(t, StructType);
        return CORD_all("(&", s->env->file_prefix, s->name, ")");
    }
    case EnumType: {
        auto e = Match(t, EnumType);
        return CORD_all("(&", e->env->file_prefix, e->name, ")");
    }
    case ArrayType: {
        type_t *item_t = Match(t, ArrayType)->item_type;
        return CORD_asprintf("$ArrayInfo(%r)", compile_type_info(env, item_t));
    }
    case TableType: {
        type_t *key_type = Match(t, TableType)->key_type;
        type_t *value_type = Match(t, TableType)->value_type;
        return CORD_asprintf("$TableInfo(%r, %r)", compile_type_info(env, key_type), compile_type_info(env, value_type));
    }
    case PointerType: {
        auto ptr = Match(t, PointerType);
        CORD sigil = ptr->is_stack ? "&" : "@";
        if (ptr->is_readonly) sigil = CORD_cat(sigil, "%");
        return CORD_asprintf("$PointerInfo(%r, %r, %s)",
                             Text$quoted(sigil, false),
                             compile_type_info(env, ptr->pointed),
                             ptr->is_optional ? "yes" : "no");
    }
    case FunctionType: {
        return CORD_asprintf("$FunctionInfo(%r)", Text$quoted(type_to_cord(t), false));
    }
    case ClosureType: {
        return CORD_asprintf("$ClosureInfo(%r)", Text$quoted(type_to_cord(t), false));
    }
    case TypeInfoType: return "&$TypeInfo";
    case MemoryType: return "&$Memory";
    case VoidType: return "&$Void";
    default:
        compiler_err(NULL, 0, 0, "I couldn't convert to a type info: %T", t);
    }
}

CORD compile_cli_arg_call(env_t *env, CORD fn_name, type_t *fn_type)
{
    auto fn_info = Match(fn_type, FunctionType);
    if (!fn_info->args) {
        return CORD_all(
            "if (argc > 1)\n"
            "errx(1, \"This program doesn't take any arguments.\");\n",
            fn_name, "();\n");
    }
    env_t *main_env = fresh_scope(env);

    CORD usage = CORD_EMPTY;
    for (arg_t *arg = fn_info->args; arg; arg = arg->next) {
        usage = CORD_cat(usage, " ");
        type_t *t = get_arg_type(main_env, arg);
        CORD flag = Text$replace(arg->name, "_", "-", INT64_MAX);
        if (arg->default_val) {
            if (t->tag == BoolType)
                usage = CORD_all(usage, "[--", flag, "]");
            else
                usage = CORD_all(usage, "[--", flag, "=...]");
        } else {
            if (t->tag == BoolType)
                usage = CORD_all(usage, "[--", flag, "|--no-", flag, "]");
            else if (t->tag == ArrayType)
                usage = CORD_all(usage, "<", flag, "...>");
            else
                usage = CORD_all(usage, "<", flag, ">");
        }
    }
    CORD code = CORD_all("CORD usage = CORD_all(\"Usage: \", argv[0], ", usage ? Text$quoted(usage, false) : "CORD_EMPTY", ");\n",
                         "#define USAGE_ERR(...) errx(1, CORD_to_const_char_star(CORD_all(__VA_ARGS__)))\n"
                         "#define IS_FLAG(str, flag) (strncmp(str, flag, strlen(flag) == 0 && (str[strlen(flag)] == 0 || str[strlen(flag)] == '=')) == 0)\n");

    // Declare args:
    for (arg_t *arg = fn_info->args; arg; arg = arg->next) {
        type_t *t = get_arg_type(main_env, arg);
        assert(arg->name);
        code = CORD_all(
            code, compile_declaration(env, t, CORD_cat("$", arg->name)), ";\n",
            "bool ", arg->name, "$is_set = no;\n");
        set_binding(env, arg->name, new(binding_t, .type=t, .code=CORD_cat("$", arg->name)));
    }
    // Provide --flags:
    code = CORD_all(code, "CORD flag;\n"
                    "for (int i = 1; i < argc; ) {\n"
                    "if (streq(argv[i], \"--\")) {\n"
                    "argv[i] = NULL;\n"
                    "break;\n"
                    "}\n"
                    "if (strncmp(argv[i], \"--\", 2) != 0) {\n++i;\ncontinue;\n}\n");
    for (arg_t *arg = fn_info->args; arg; arg = arg->next) {
        type_t *t = get_arg_type(main_env, arg);
        CORD flag = Text$replace(arg->name, "_", "-", INT64_MAX);
        switch (t->tag) {
        case BoolType: {
            code = CORD_all(code, "else if (pop_flag(argv, &i, \"", flag, "\", &flag)) {\n"
                            "if (flag) {\n",
                            "$", arg->name, " = Bool$from_text(flag, &", arg->name, "$is_set", ");\n"
                            "if (!", arg->name, "$is_set) \n"
                            "USAGE_ERR(\"Invalid argument for '--", flag, "'\\n\", usage);\n",
                            "} else {\n",
                            "$", arg->name, " = yes;\n",
                            arg->name, "$is_set = yes;\n"
                            "}\n"
                            "}\n");
            break;
        }
        case TextType: {
            code = CORD_all(code, "else if (pop_flag(argv, &i, \"", flag, "\", &flag)) {\n",
                            "$", arg->name, " = CORD_to_const_char_star(flag);\n",
                            arg->name, "$is_set = yes;\n"
                            "}\n");
            break;
        }
        case ArrayType: {
            if (Match(t, ArrayType)->item_type->tag != TextType)
                compiler_err(NULL, NULL, NULL, "Main function has unsupported argument type: %T (only arrays of Text are supported)", t);
            code = CORD_all(code, "else if (pop_flag(argv, &i, \"", flag, "\", &flag)) {\n",
                            "$", arg->name, " = Text$split(CORD_to_const_char_star(flag), \",\");\n",
                            arg->name, "$is_set = yes;\n"
                            "}\n");
            break;
        }
        case IntType: case NumType: {
            CORD type_name = type_to_cord(t);
            code = CORD_all(code, "else if (pop_flag(argv, &i, \"", flag, "\", &flag)) {\n",
                            "if (flag == CORD_EMPTY)\n"
                            "USAGE_ERR(\"No value provided for '--", flag, "'\\n\", usage);\n"
                            "CORD invalid = CORD_EMPTY;\n",
                            "$", arg->name, " = ", type_name, "$from_text(flag, &invalid);\n"
                            "if (invalid != CORD_EMPTY)\n"
                            "USAGE_ERR(\"Invalid value provided for '--", flag, "'\\n\", usage);\n",
                            arg->name, "$is_set = yes;\n"
                            "}\n");
            break;
        }
        default:
            compiler_err(NULL, NULL, NULL, "Main function has unsupported argument type: %T", t);
        }
    }

    code = CORD_all(
        code, "else {\n"
        "USAGE_ERR(\"Unrecognized argument: \", argv[i], \"\\n\", usage);\n"
        "}\n"
        "}\n"
        "int i = 1;\n"
        "while (i < argc && argv[i] == NULL)\n"
        "++i;\n");

    for (arg_t *arg = fn_info->args; arg; arg = arg->next) {
        type_t *t = get_arg_type(env, arg);
        code = CORD_all(code, "if (!", arg->name, "$is_set) {\n");
        if (t->tag == ArrayType) {
            code = CORD_all(
                code, "$", arg->name, " = (array_t){};\n"
                "for (; i < argc; i++) {\n"
                "if (argv[i]) {\n"
                "CORD arg = CORD_from_char_star(argv[i]);\n"
                "Array$insert(&$", arg->name, ", &arg, 0, $ArrayInfo(&$Text));\n"
                "argv[i] = NULL;\n"
                "}\n"
                "}\n",
                arg->name, "$is_set = yes;\n");
        } else if (arg->default_val) {
            code = CORD_all(code, "$", arg->name, " = ", compile(env, arg->default_val), ";\n");
        } else {
            code = CORD_all(
                code,
                "if (i < argc) {");
            if (t->tag == TextType) {
                code = CORD_all(code, "$", arg->name, " = CORD_from_char_star(argv[i]);\n");
            } else {
                code = CORD_all(
                    code,
                    "CORD invalid;\n",
                    "$", arg->name, " = ", type_to_cord(t), "$from_text(argv[i], &invalid)", ";\n"
                    "if (invalid != CORD_EMPTY)\n"
                    "USAGE_ERR(\"Unable to parse this argument as a ", type_to_cord(t), ": \", CORD_from_char_star(argv[i]));\n");
            }
            code = CORD_all(
                code,
                "argv[i++] = NULL;\n"
                "while (i < argc && argv[i] == NULL)\n"
                "++i;\n} else {\n"
                "USAGE_ERR(\"Required argument '", arg->name, "' was not provided!\\n\", usage);\n",
                "}\n");
        }
        code = CORD_all(code, "}\n");
    }


    code = CORD_all(code, "for (; i < argc; i++) {\n"
                    "if (argv[i])\nUSAGE_ERR(\"Unexpected argument: \", Text$quoted(argv[i], false), \"\\n\", usage);\n}\n");

    code = CORD_all(code, fn_name, "(");
    for (arg_t *arg = fn_info->args; arg; arg = arg->next) {
        code = CORD_all(code, "$", arg->name);
        if (arg->next) code = CORD_all(code, ", ");
    }
    code = CORD_all(code, ");\n");
    return code;
}

CORD compile_file(env_t *env, ast_t *ast)
{
    for (ast_list_t *stmt = Match(ast, Block)->statements; stmt; stmt = stmt->next) {
        if (stmt->ast->tag == Declare) {
            auto decl = Match(stmt->ast, Declare);
            const char *decl_name = Match(decl->var, Var)->name;
            bool is_private = (decl_name[0] == '_');
            type_t *t = get_type(env, decl->value);
            if (t->tag == AbortType || t->tag == VoidType)
                code_err(stmt->ast, "You can't declare a variable with a %T value", t);
            if (!is_constant(env, decl->value))
                code_err(decl->value, "This value is not a valid constant initializer.");

            if (decl->value->tag == Use) {
                assert(compile_statement(env, stmt->ast) == CORD_EMPTY);
            } else if (is_private) {
                env->code->staticdefs = CORD_all(
                    env->code->staticdefs,
                    "static ", compile_type(env, t), " $", decl_name, " = ",
                    compile(env, decl->value), ";\n");
            } else {
                env->code->staticdefs = CORD_all(
                    env->code->staticdefs,
                    compile_type(env, t), " ", env->file_prefix, decl_name, " = ",
                    compile(env, decl->value), ";\n");
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
        // "#line 1 ", Text$quoted(ast->file->filename, false), "\n",
        "#include <tomo/tomo.h>\n"
        "#include \"", name, ".tm.h\"\n\n",
        env->code->local_typedefs, "\n",
        env->code->staticdefs, "\n",
        env->code->funcs, "\n",
        env->code->typeinfos, "\n"
    );
}

CORD compile_statement_header(env_t *env, ast_t *ast)
{
    switch (ast->tag) {
    case DocTest: {
        auto test = Match(ast, DocTest);
        return compile_statement_header(env, test->expr);
    }
    case Declare: {
        auto decl = Match(ast, Declare);
        if (decl->value->tag == Use) {
            return compile_statement_header(env, decl->value);
        }
        type_t *t = get_type(env, decl->value);
        assert(t->tag != ModuleType);
        if (t->tag == AbortType || t->tag == VoidType)
            code_err(ast, "You can't declare a variable with a %T value", t);
        const char *decl_name = Match(decl->var, Var)->name;
        bool is_private = (decl_name[0] == '_');
        if (!is_constant(env, decl->value))
            code_err(decl->value, "This value is not a valid constant initializer.");

        CORD code = (decl->value->tag == Use) ? compile_statement_header(env, decl->value) : CORD_EMPTY;
        if (is_private) {
            return code;
        } else {
            return CORD_all(
                code, "\n" "extern ", compile_declaration(env, t, CORD_cat(env->file_prefix, decl_name)), ";\n");
        }
    }
    case StructDef: {
        return compile_struct_header(env, ast);
    }
    case EnumDef: {
        return compile_enum_header(env, ast);
    }
    case LangDef: {
        auto def = Match(ast, LangDef);
        return CORD_all(
            "typedef CORD ", env->file_prefix, def->name, "_t;\n",
            "extern const TypeInfo ", env->file_prefix, def->name, ";\n");
        compile_namespace(env, def->name, def->namespace);
        return CORD_EMPTY;
    }
    case FunctionDef: {
        auto fndef = Match(ast, FunctionDef);
        bool is_private = Match(fndef->name, Var)->name[0] == '_';
        if (is_private) return CORD_EMPTY;
        CORD name = compile(env, fndef->name);

        CORD arg_signature = "(";
        for (arg_ast_t *arg = fndef->args; arg; arg = arg->next) {
            type_t *arg_type = get_arg_ast_type(env, arg);
            arg_signature = CORD_cat(arg_signature, compile_declaration(env, arg_type, CORD_cat("$", arg->name)));
            if (arg->next) arg_signature = CORD_cat(arg_signature, ", ");
        }
        arg_signature = CORD_cat(arg_signature, ")");

        type_t *ret_t = fndef->ret_type ? parse_type_ast(env, fndef->ret_type) : Type(VoidType);
        CORD ret_type_code = compile_type(env, ret_t);
        CORD header = CORD_all(ret_type_code, " ", name, arg_signature, ";\n");
        return header;
    }
    case Lambda: {
        auto lambda = Match(ast, Lambda);
        CORD name = CORD_asprintf("%slambda$%ld", env->file_prefix, lambda->id);
        table_t *closed_vars = get_closed_vars(env, ast);
        if (Table$length(*closed_vars) == 0) {
            return CORD_EMPTY;
        } else {
            CORD def = "typedef struct {";
            for (int64_t i = 1; i <= Table$length(*closed_vars); i++) {
                struct { const char *name; binding_t *b; } *entry = Table$entry(*closed_vars, i);
                if (entry->b->type->tag == ModuleType)
                    continue;
                def = CORD_all(def, compile_declaration(env, entry->b->type, entry->name), "; ");
            }
            return CORD_all(def, "} ", name, "$userdata_t;");
        }
    }
    case Extern: {
        auto ext = Match(ast, Extern);
        type_t *t = parse_type_ast(env, ext->type);
        CORD decl;
        if (t->tag == ClosureType) {
            t = Match(t, ClosureType)->fn;
            auto fn = Match(t, FunctionType);
            decl = CORD_all(compile_type(env, fn->ret), " ", ext->name, "(");
            for (arg_t *arg = fn->args; arg; arg = arg->next) {
                decl = CORD_all(decl, compile_type(env, arg->type));
                if (arg->next) decl = CORD_cat(decl, ", ");
            }
            decl = CORD_cat(decl, ")");
        } else {
            decl = compile_declaration(env, t, ext->name);
        }
        return CORD_all("extern ", decl, ";\n");
    }
    case Use: {
        auto use = Match(ast, Use);
        const char *path = use->raw_path;
        if (strncmp(path, "./", 2) == 0 || strncmp(path, "../", 3) == 0 || strncmp(path, "~/", 2) == 0 || strncmp(path, "/", 1) == 0) {
            return CORD_all("#include \"", path, ".tm.h\"\n");
        } else {
            return CORD_all("#include <tomo/lib", path, ".h>\n");
        }
    }
    default:
        return CORD_EMPTY;
    }
}

CORD compile_header(env_t *env, ast_t *ast)
{
    // "#line 1 ", Text$quoted(ast->file->filename, false), "\n",
    CORD header = "#pragma once\n"
                  "#include <tomo/tomo.h>\n";

    for (ast_list_t *stmt = Match(ast, Block)->statements; stmt; stmt = stmt->next)
        header = CORD_all(header, compile_statement_header(env, stmt->ast));

    return header;
}

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
