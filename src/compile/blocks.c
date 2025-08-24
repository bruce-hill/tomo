
#include "blocks.h"
#include "../ast.h"
#include "../compile.h"
#include "../environment.h"
#include "../stdlib/datatypes.h"
#include "../stdlib/text.h"
#include "../stdlib/util.h"
#include "../typecheck.h"
#include "statements.h"

public
Text_t compile_block(env_t *env, ast_t *ast) { return Texts("{\n", compile_inline_block(env, ast), "}\n"); }

Text_t compile_block_expression(env_t *env, ast_t *ast) {
    ast_list_t *stmts = Match(ast, Block)->statements;
    if (stmts && !stmts->next) return compile(env, stmts->ast);

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
            for (deferral_t *deferred = env->deferred; deferred && deferred != prev_deferred;
                 deferred = deferred->next) {
                code = Texts(code, compile_statement(deferred->defer_env, deferred->block));
            }
            code = Texts(code, compile(env, stmt->ast), ";\n");
        }
        bind_statement(env, stmt->ast);
    }

    return Texts(code, "})");
}

public
Text_t compile_inline_block(env_t *env, ast_t *ast) {
    if (ast->tag != Block) return compile_statement(env, ast);

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
