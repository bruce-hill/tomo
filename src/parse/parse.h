#pragma once

// Parsing logic

#include "../ast.h"
#include "context.h"

ast_t *parse(const char *str);
ast_t *parse_expression(const char *str);

ast_e match_binary_operator(const char **pos);
ast_t *parse_comprehension_suffix(parse_ctx_t *ctx, ast_t *expr);
ast_t *parse_field_suffix(parse_ctx_t *ctx, ast_t *lhs);
ast_t *parse_fncall_suffix(parse_ctx_t *ctx, ast_t *fn);
ast_t *parse_index_suffix(parse_ctx_t *ctx, ast_t *lhs);
ast_t *parse_method_call_suffix(parse_ctx_t *ctx, ast_t *self);
ast_t *parse_non_optional_suffix(parse_ctx_t *ctx, ast_t *lhs);
ast_t *parse_optional_conditional_suffix(parse_ctx_t *ctx, ast_t *stmt);
ast_t *parse_optional_suffix(parse_ctx_t *ctx, ast_t *lhs);
arg_ast_t *parse_args(parse_ctx_t *ctx, const char **pos);
type_ast_t *parse_list_type(parse_ctx_t *ctx, const char *pos);
type_ast_t *parse_func_type(parse_ctx_t *ctx, const char *pos);
type_ast_t *parse_non_optional_type(parse_ctx_t *ctx, const char *pos);
type_ast_t *parse_pointer_type(parse_ctx_t *ctx, const char *pos);
type_ast_t *parse_set_type(parse_ctx_t *ctx, const char *pos);
type_ast_t *parse_table_type(parse_ctx_t *ctx, const char *pos);
type_ast_t *parse_type(parse_ctx_t *ctx, const char *pos);
type_ast_t *parse_type_name(parse_ctx_t *ctx, const char *pos);
ast_t *parse_list(parse_ctx_t *ctx, const char *pos);
ast_t *parse_assignment(parse_ctx_t *ctx, const char *pos);
ast_t *parse_block(parse_ctx_t *ctx, const char *pos);
ast_t *parse_bool(parse_ctx_t *ctx, const char *pos);
ast_t *parse_convert_def(parse_ctx_t *ctx, const char *pos);
ast_t *parse_declaration(parse_ctx_t *ctx, const char *pos);
ast_t *parse_defer(parse_ctx_t *ctx, const char *pos);
ast_t *parse_do(parse_ctx_t *ctx, const char *pos);
ast_t *parse_doctest(parse_ctx_t *ctx, const char *pos);
ast_t *parse_assert(parse_ctx_t *ctx, const char *pos);
ast_t *parse_enum_def(parse_ctx_t *ctx, const char *pos);
ast_t *parse_expr(parse_ctx_t *ctx, const char *pos);
ast_t *parse_extended_expr(parse_ctx_t *ctx, const char *pos);
ast_t *parse_extern(parse_ctx_t *ctx, const char *pos);
ast_t *parse_for(parse_ctx_t *ctx, const char *pos);
ast_t *parse_func_def(parse_ctx_t *ctx, const char *pos);
ast_t *parse_heap_alloc(parse_ctx_t *ctx, const char *pos);
ast_t *parse_if(parse_ctx_t *ctx, const char *pos);
ast_t *parse_inline_c(parse_ctx_t *ctx, const char *pos);
ast_t *parse_int(parse_ctx_t *ctx, const char *pos);
ast_t *parse_lambda(parse_ctx_t *ctx, const char *pos);
ast_t *parse_lang_def(parse_ctx_t *ctx, const char *pos);
ast_t *parse_extend(parse_ctx_t *ctx, const char *pos);
ast_t *parse_namespace(parse_ctx_t *ctx, const char *pos);
ast_t *parse_negative(parse_ctx_t *ctx, const char *pos);
ast_t *parse_not(parse_ctx_t *ctx, const char *pos);
ast_t *parse_none(parse_ctx_t *ctx, const char *pos);
ast_t *parse_num(parse_ctx_t *ctx, const char *pos);
ast_t *parse_parens(parse_ctx_t *ctx, const char *pos);
ast_t *parse_pass(parse_ctx_t *ctx, const char *pos);
ast_t *parse_path(parse_ctx_t *ctx, const char *pos);
ast_t *parse_reduction(parse_ctx_t *ctx, const char *pos);
ast_t *parse_repeat(parse_ctx_t *ctx, const char *pos);
ast_t *parse_return(parse_ctx_t *ctx, const char *pos);
ast_t *parse_set(parse_ctx_t *ctx, const char *pos);
ast_t *parse_skip(parse_ctx_t *ctx, const char *pos);
ast_t *parse_stack_reference(parse_ctx_t *ctx, const char *pos);
ast_t *parse_statement(parse_ctx_t *ctx, const char *pos);
ast_t *parse_stop(parse_ctx_t *ctx, const char *pos);
ast_t *parse_struct_def(parse_ctx_t *ctx, const char *pos);
ast_t *parse_table(parse_ctx_t *ctx, const char *pos);
ast_t *parse_term(parse_ctx_t *ctx, const char *pos);
ast_t *parse_term_no_suffix(parse_ctx_t *ctx, const char *pos);
ast_t *parse_text(parse_ctx_t *ctx, const char *pos);
ast_t *parse_update(parse_ctx_t *ctx, const char *pos);
ast_t *parse_use(parse_ctx_t *ctx, const char *pos);
ast_t *parse_var(parse_ctx_t *ctx, const char *pos);
ast_t *parse_when(parse_ctx_t *ctx, const char *pos);
ast_t *parse_while(parse_ctx_t *ctx, const char *pos);
ast_t *parse_deserialize(parse_ctx_t *ctx, const char *pos);
ast_list_t *_parse_text_helper(parse_ctx_t *ctx, const char **out_pos, char open_quote, char close_quote,
                               char open_interp, bool allow_escapes);
