// This file defines how to compile conditionals

#include "../ast.h"
#include "../config.h"
#include "../environment.h"
#include "../stdlib/datatypes.h"
#include "../stdlib/text.h"
#include "../stdlib/util.h"
#include "../typecheck.h"
#include "compilation.h"

public
Text_t compile_condition(env_t *env, ast_t *ast) {
    type_t *t = get_type(env, ast);
    if (t->tag == BoolType) {
        return compile(env, ast);
    } else if (t->tag == TextType) {
        return Texts("(", compile(env, ast), ").length");
    } else if (t->tag == ListType) {
        return Texts("(", compile(env, ast), ").length");
    } else if (t->tag == TableType) {
        return Texts("(", compile(env, ast), ").entries.length");
    } else if (t->tag == OptionalType) {
        return Texts("!", check_none(t, compile(env, ast)));
    } else if (t->tag == PointerType) {
        code_err(ast, "This pointer will always be non-none, so it should not be "
                      "used in a conditional.");
    } else {
        code_err(ast, type_to_text(t), " values cannot be used for conditionals");
    }
    return EMPTY_TEXT;
}

public
Text_t compile_if_statement(env_t *env, ast_t *ast) {
    DeclareMatch(if_, ast, If);
    ast_t *condition = if_->condition;
    if (condition->tag == Declare) {
        DeclareMatch(decl, condition, Declare);
        if (decl->value == NULL) code_err(condition, "This declaration must have a value");

        env_t *truthy_scope = fresh_scope(env);
        ast_t *var = decl->var;
        type_t *var_type = get_type(truthy_scope, decl->value);

        const char *name = Match(var, Var)->name;
        bind_statement(truthy_scope, condition);

        Text_t code = Texts("if (true) {\n", compile_statement(env, condition), //
                            "if (", compile_condition(truthy_scope, var), ")");

        env_t *nonnull_scope = truthy_scope;
        if (var_type->tag == OptionalType) {
            nonnull_scope = fresh_scope(truthy_scope);
            set_binding(nonnull_scope, name, Match(var_type, OptionalType)->type,
                        optional_into_nonnone(var_type, compile(truthy_scope, var)));
        }

        code = Texts(code, compile_block(nonnull_scope, if_->body));

        if (if_->else_body) {
            Text_t label = Texts("_falsey_", (int64_t)(ast->start - ast->file->text));
            code = Texts(code, "else goto ", label,
                         ";\n"
                         "} else {\n",
                         label, ":;\n", compile_inline_block(env, if_->else_body), "}\n");
        } else {
            code = Texts(code, "}\n");
        }

        return code;
    } else {
        Text_t code = Texts("if (", compile_condition(env, condition), ")");
        env_t *truthy_scope = env;
        type_t *cond_t = get_type(env, condition);
        if (condition->tag == Var && cond_t->tag == OptionalType) {
            truthy_scope = fresh_scope(env);
            set_binding(truthy_scope, Match(condition, Var)->name, Match(cond_t, OptionalType)->type,
                        optional_into_nonnone(cond_t, compile(truthy_scope, condition)));
        }
        code = Texts(code, compile_statement(truthy_scope, if_->body));
        if (if_->else_body) code = Texts(code, "\nelse ", compile_statement(env, if_->else_body));
        return code;
    }
}

public
Text_t compile_if_expression(env_t *env, ast_t *ast) {
    DeclareMatch(if_, ast, If);
    ast_t *condition = if_->condition;
    Text_t decl_code = EMPTY_TEXT;
    env_t *truthy_scope = env, *falsey_scope = env;

    Text_t condition_code;
    if (condition->tag == Declare) {
        DeclareMatch(decl, condition, Declare);
        if (decl->value == NULL) code_err(condition, "This declaration must have a value");
        type_t *condition_type =
            decl->type ? parse_type_ast(env, decl->type) : get_type(env, Match(condition, Declare)->value);
        if (condition_type->tag != OptionalType)
            code_err(condition,
                     "This `if var := ...:` declaration should be an "
                     "optional "
                     "type, not ",
                     type_to_text(condition_type));

        if (is_incomplete_type(condition_type)) code_err(condition, "This type is incomplete!");

        decl_code = compile_statement(env, condition);
        ast_t *var = Match(condition, Declare)->var;
        truthy_scope = fresh_scope(env);
        bind_statement(truthy_scope, condition);
        condition_code = compile_condition(truthy_scope, var);
        set_binding(truthy_scope, Match(var, Var)->name, Match(condition_type, OptionalType)->type,
                    optional_into_nonnone(condition_type, compile(truthy_scope, var)));
    } else if (condition->tag == Var) {
        type_t *condition_type = get_type(env, condition);
        condition_code = compile_condition(env, condition);
        if (condition_type->tag == OptionalType) {
            truthy_scope = fresh_scope(env);
            set_binding(truthy_scope, Match(condition, Var)->name, Match(condition_type, OptionalType)->type,
                        optional_into_nonnone(condition_type, compile(truthy_scope, condition)));
        }
    } else {
        condition_code = compile_condition(env, condition);
    }

    type_t *true_type = get_type(truthy_scope, if_->body);
    ast_t *else_body = if_->else_body;
    if (else_body && else_body->tag == Block && Match(else_body, Block)->statements
        && !Match(else_body, Block)->statements->next)
        else_body = Match(else_body, Block)->statements->ast;
    if (else_body == NULL || else_body->tag == None) else_body = WrapAST(ast, None, .type = true_type);
    type_t *false_type = get_type(falsey_scope, else_body);
    if (true_type->tag == AbortType || true_type->tag == ReturnType)
        return Texts("({ ", decl_code, "if (", condition_code, ") ", compile_statement(truthy_scope, if_->body), "\n",
                     compile(falsey_scope, else_body), "; })");
    else if (false_type->tag == AbortType || false_type->tag == ReturnType)
        return Texts("({ ", decl_code, "if (!(", condition_code, ")) ", compile_statement(falsey_scope, else_body),
                     "\n", compile(truthy_scope, if_->body), "; })");
    else if (decl_code.length > 0)
        return Texts("({ ", decl_code, "(", condition_code, ") ? ", compile(truthy_scope, if_->body), " : ",
                     compile(falsey_scope, else_body), ";})");
    else
        return Texts("((", condition_code, ") ? ", compile(truthy_scope, if_->body), " : ",
                     compile(falsey_scope, else_body), ")");
}
