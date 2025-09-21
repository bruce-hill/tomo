// This file defines how to compile 'when' statements/expressions

#include "../ast.h"
#include "../config.h"
#include "../environment.h"
#include "../naming.h"
#include "../stdlib/datatypes.h"
#include "../stdlib/text.h"
#include "../stdlib/util.h"
#include "../typecheck.h"
#include "compilation.h"

public
Text_t compile_when_statement(env_t *env, ast_t *ast) {
    // Typecheck to verify exhaustiveness:
    type_t *result_t = get_type(env, ast);
    (void)result_t;

    DeclareMatch(when, ast, When);
    type_t *subject_t = get_type(env, when->subject);

    if (subject_t->tag != EnumType) {
        Text_t prefix = EMPTY_TEXT, suffix = EMPTY_TEXT;
        ast_t *subject = when->subject;
        if (!is_idempotent(when->subject)) {
            prefix = Texts("{\n", compile_declaration(subject_t, Text("_when_subject")), " = ", compile(env, subject),
                           ";\n");
            suffix = Text("}\n");
            subject = LiteralCode(Text("_when_subject"), .type = subject_t);
        }

        Text_t code = EMPTY_TEXT;
        for (when_clause_t *clause = when->clauses; clause; clause = clause->next) {
            ast_t *comparison = WrapAST(clause->pattern, Equals, .lhs = subject, .rhs = clause->pattern);
            (void)get_type(env, comparison);
            if (code.length > 0) code = Texts(code, "else ");
            code = Texts(code, "if (", compile(env, comparison), ")", compile_statement(env, clause->body));
        }
        if (when->else_body) code = Texts(code, "else ", compile_statement(env, when->else_body));
        code = Texts(prefix, code, suffix);
        return code;
    }

    DeclareMatch(enum_t, subject_t, EnumType);

    Text_t code;
    if (enum_has_fields(subject_t))
        code = Texts("WHEN(", compile_type(subject_t), ", ", compile(env, when->subject), ", _when_subject, {\n");
    else code = Texts("switch(", compile(env, when->subject), ") {\n");

    for (when_clause_t *clause = when->clauses; clause; clause = clause->next) {
        if (clause->pattern->tag == Var) {
            const char *clause_tag_name = Match(clause->pattern, Var)->name;
            type_t *clause_type = clause->body ? get_type(env, clause->body) : Type(VoidType);
            code = Texts(
                code, "case ", namespace_name(enum_t->env, enum_t->env->namespace, Texts("tag$", clause_tag_name)),
                ": {\n", compile_inline_block(env, clause->body),
                (clause_type->tag == ReturnType || clause_type->tag == AbortType) ? EMPTY_TEXT : Text("break;\n"),
                "}\n");
            continue;
        }

        if (clause->pattern->tag != FunctionCall || Match(clause->pattern, FunctionCall)->fn->tag != Var)
            code_err(clause->pattern, "This is not a valid pattern for a ", type_to_text(subject_t), " enum type");

        const char *clause_tag_name = Match(Match(clause->pattern, FunctionCall)->fn, Var)->name;
        code = Texts(code, "case ", namespace_name(enum_t->env, enum_t->env->namespace, Texts("tag$", clause_tag_name)),
                     ": {\n");
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
            if (args->value->tag != Var) code_err(args->value, "This is not a valid variable to bind to");
            const char *var_name = Match(args->value, Var)->name;
            if (!streq(var_name, "_")) {
                Text_t var = Texts("_$", var_name);
                code = Texts(code, compile_declaration(tag_type, var), " = _when_subject.",
                             valid_c_name(clause_tag_name), ";\n");
                scope = fresh_scope(scope);
                set_binding(scope, Match(args->value, Var)->name, tag_type, EMPTY_TEXT);
            }
        } else if (args) {
            scope = fresh_scope(scope);
            arg_t *field = tag_struct->fields;
            for (arg_ast_t *arg = args; arg || field; arg = arg->next) {
                if (!arg)
                    code_err(ast, "The field ", type_to_text(subject_t), ".", clause_tag_name, ".", field->name,
                             " wasn't accounted for");
                if (!field) code_err(arg->value, "This is one more field than ", type_to_text(subject_t), " has");
                if (arg->name) code_err(arg->value, "Named arguments are not currently supported");

                const char *var_name = Match(arg->value, Var)->name;
                if (!streq(var_name, "_")) {
                    Text_t var = Texts("_$", var_name);
                    code = Texts(code, compile_declaration(field->type, var), " = _when_subject.",
                                 valid_c_name(clause_tag_name), ".", valid_c_name(field->name), ";\n");
                    set_binding(scope, Match(arg->value, Var)->name, field->type, var);
                }
                field = field->next;
            }
        }
        if (clause->body->tag == Block) {
            ast_list_t *statements = Match(clause->body, Block)->statements;
            if (!statements || (statements->ast->tag == Pass && !statements->next)) code = Texts(code, "break;\n}\n");
            else code = Texts(code, compile_inline_block(scope, clause->body), "\nbreak;\n}\n");
        } else {
            code = Texts(code, compile_statement(scope, clause->body), "\nbreak;\n}\n");
        }
    }
    if (when->else_body) {
        if (when->else_body->tag == Block) {
            ast_list_t *statements = Match(when->else_body, Block)->statements;
            if (!statements || (statements->ast->tag == Pass && !statements->next))
                code = Texts(code, "default: break;");
            else code = Texts(code, "default: {\n", compile_inline_block(env, when->else_body), "\nbreak;\n}\n");
        } else {
            code = Texts(code, "default: {\n", compile_statement(env, when->else_body), "\nbreak;\n}\n");
        }
    } else {
        code = Texts(code, "default: errx(1, \"Invalid tag!\");\n");
    }
    code = Texts(code, "\n}", enum_has_fields(subject_t) ? Text(")") : EMPTY_TEXT, "\n");
    return code;
}

public
Text_t compile_when_expression(env_t *env, ast_t *ast) {
    DeclareMatch(original, ast, When);
    ast_t *when_var = WrapAST(ast, Var, .name = "when");
    when_clause_t *new_clauses = NULL;
    type_t *subject_t = get_type(env, original->subject);
    for (when_clause_t *clause = original->clauses; clause; clause = clause->next) {
        type_t *clause_type = get_clause_type(env, subject_t, clause);
        if (clause_type->tag == AbortType || clause_type->tag == ReturnType) {
            new_clauses = new (when_clause_t, .pattern = clause->pattern, .body = clause->body, .next = new_clauses);
        } else {
            ast_t *assign = WrapAST(clause->body, Assign, .targets = new (ast_list_t, .ast = when_var),
                                    .values = new (ast_list_t, .ast = clause->body));
            new_clauses = new (when_clause_t, .pattern = clause->pattern, .body = assign, .next = new_clauses);
        }
    }
    REVERSE_LIST(new_clauses);
    ast_t *else_body = original->else_body;
    if (else_body) {
        type_t *clause_type = get_type(env, else_body);
        if (clause_type->tag != AbortType && clause_type->tag != ReturnType) {
            else_body = WrapAST(else_body, Assign, .targets = new (ast_list_t, .ast = when_var),
                                .values = new (ast_list_t, .ast = else_body));
        }
    }

    type_t *t = get_type(env, ast);
    env_t *when_env = fresh_scope(env);
    set_binding(when_env, "when", t, Text("when"));
    return Texts("({ ", compile_declaration(t, Text("when")), ";\n",
                 compile_statement(when_env, WrapAST(ast, When, .subject = original->subject, .clauses = new_clauses,
                                                     .else_body = else_body)),
                 "when; })");
}
