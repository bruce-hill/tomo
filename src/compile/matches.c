// This file defines how to compile 'match' statements/expressions

#include "../ast.h"
#include "../config.h"
#include "../environment.h"
#include "../naming.h"
#include "../stdlib/datatypes.h"
#include "../stdlib/text.h"
#include "../typecheck.h"
#include "../util.h"
#include "compilation.h"

public
Text_t compile_match_statement(env_t *env, ast_t *ast) {
    // Typecheck to verify exhaustiveness:
    type_t *result_t = get_type(env, ast);
    (void)result_t;

    DeclareMatch(match, ast, Match);
    type_t *subject_t = get_type(env, match->subject);

    if (subject_t->tag != EnumType) {
        Text_t prefix = EMPTY_TEXT, suffix = EMPTY_TEXT;
        ast_t *subject = match->subject;
        if (!is_idempotent(match->subject)) {
            prefix = Texts("{\n", compile_declaration(subject_t, Text("_match_subject")), " = ", compile(env, subject),
                           ";\n");
            suffix = Text("}\n");
            subject = LiteralCode(Text("_match_subject"), .type = subject_t);
        }

        Text_t code = EMPTY_TEXT;
        for (match_clause_t *clause = match->clauses; clause; clause = clause->next) {
            ast_t *comparison = WrapAST(clause->pattern, Equals, .lhs = subject, .rhs = clause->pattern);
            (void)get_type(env, comparison);
            if (code.length > 0) code = Texts(code, "else ");
            code = Texts(code, "if (", compile(env, comparison), ")", compile_statement(env, clause->body));
        }
        if (match->else_body) code = Texts(code, "else ", compile_statement(env, match->else_body));
        code = Texts(prefix, code, suffix);
        return code;
    }

    DeclareMatch(enum_t, subject_t, EnumType);

    // Written out rather than wrapped in a macro: the clause bodies below
    // carry `#line` directives, and a preprocessor directive inside a
    // function-like macro's arguments is undefined: the preprocessor drops
    // them, and every line of every clause ends up attributed to the line the
    // `match` itself is on. That makes a debugger unable to tell one clause
    // from another, or to break on a line inside one.
    Text_t code = Texts("{\n", compile_type(subject_t), " _match_subject = ", compile(env, match->subject),
                        ";\nswitch (_match_subject.$tag) {\n");
    for (match_clause_t *clause = match->clauses; clause; clause = clause->next) {
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

        if (clause->pattern->tag != RecordLiteral || Match(clause->pattern, RecordLiteral)->type->tag != Var)
            code_err(clause->pattern, "This is not a valid pattern for a ", type_to_text(subject_t), " enum type");

        const char *clause_tag_name = Match(Match(clause->pattern, RecordLiteral)->type, Var)->name;
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
        arg_ast_t *args = Match(clause->pattern, RecordLiteral)->args;
        if (args && !args->next && tag_struct->fields && tag_struct->fields->next) {
            if (args->value->tag != Var) code_err(args->value, "This is not a valid variable to bind to");
            const char *var_name = Match(args->value, Var)->name;
            if (!streq(var_name, "_")) {
                Text_t var = Texts("_$", var_name);
                ast_t *member =
                    WrapLiteralCode(ast, Texts("_match_subject.", valid_c_name(clause_tag_name)), .type = tag_type);
                code = Texts(code, compile_debug_typeinfo(env, var_name, tag_type), compile_declaration(tag_type, var),
                             " = ", compile_maybe_incref(env, member, tag_type), ";\n");
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
                    ast_t *member =
                        WrapLiteralCode(ast, Texts("_match_subject.", valid_c_name(clause_tag_name)), .type = tag_type);
                    code = Texts(code, compile_debug_typeinfo(env, var_name, field->type),
                                 compile_declaration(field->type, var), " = ",
                                 compile_maybe_incref(env, member, tag_type), ".", valid_c_name(field->name), ";\n");
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
    if (match->else_body) {
        if (match->else_body->tag == Block) {
            ast_list_t *statements = Match(match->else_body, Block)->statements;
            if (!statements || (statements->ast->tag == Pass && !statements->next))
                code = Texts(code, "default: break;");
            else code = Texts(code, "default: {\n", compile_inline_block(env, match->else_body), "\nbreak;\n}\n");
        } else {
            code = Texts(code, "default: {\n", compile_statement(env, match->else_body), "\nbreak;\n}\n");
        }
    } else {
        code = Texts(code, "default: errx(1, \"Invalid tag!\");\n");
    }
    code = Texts(code, "\n}\n}\n");
    return code;
}

public
Text_t compile_match_expression(env_t *env, ast_t *ast) {
    DeclareMatch(original, ast, Match);
    ast_t *match_var = WrapAST(ast, Var, .name = "match");
    match_clause_t *new_clauses = NULL;
    type_t *subject_t = get_type(env, original->subject);
    for (match_clause_t *clause = original->clauses; clause; clause = clause->next) {
        type_t *clause_type = get_clause_type(env, subject_t, clause);
        if (clause_type->tag == AbortType || clause_type->tag == ReturnType) {
            new_clauses = new (match_clause_t, .pattern = clause->pattern, .body = clause->body, .next = new_clauses);
        } else {
            ast_t *assign = WrapAST(clause->body, Assign, .targets = new (ast_list_t, .ast = match_var),
                                    .values = new (ast_list_t, .ast = clause->body));
            new_clauses = new (match_clause_t, .pattern = clause->pattern, .body = assign, .next = new_clauses);
        }
    }
    REVERSE_LIST(new_clauses);
    ast_t *else_body = original->else_body;
    if (else_body) {
        type_t *clause_type = get_type(env, else_body);
        if (clause_type->tag != AbortType && clause_type->tag != ReturnType) {
            else_body = WrapAST(else_body, Assign, .targets = new (ast_list_t, .ast = match_var),
                                .values = new (ast_list_t, .ast = else_body));
        }
    }

    type_t *t = get_type(env, ast);
    env_t *match_env = fresh_scope(env);
    set_binding(match_env, "match", t, Text("match"));
    return Texts("({ ", compile_declaration(t, Text("match")), ";\n",
                 compile_statement(match_env, WrapAST(ast, Match, .subject = original->subject, .clauses = new_clauses,
                                                      .else_body = else_body)),
                 "match; })");
}
