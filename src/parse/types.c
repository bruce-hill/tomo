#include <stdarg.h>
#include <stdbool.h>
#include <string.h>

#include <unictype.h>
#include <uniname.h>

#include "../ast.h"
#include "../stdlib/datatypes.h"
#include "../stdlib/print.h"
#include "../stdlib/text.h"
#include "context.h"
#include "errors.h"
#include "expressions.h"
#include "functions.h"
#include "types.h"
#include "utils.h"

type_ast_t *parse_table_type(parse_ctx_t *ctx, const char *pos) {
    const char *start = pos;
    if (!match(&pos, "{")) return NULL;
    whitespace(ctx, &pos);
    type_ast_t *key_type = parse_type(ctx, pos);
    if (!key_type) return NULL;
    pos = key_type->end;
    whitespace(ctx, &pos);
    type_ast_t *value_type = NULL;
    if (match(&pos, "=")) {
        value_type = expect(ctx, start, &pos, parse_type, "I couldn't parse the rest of this table type");
    } else {
        return NULL;
    }
    spaces(&pos);
    ast_t *default_value = NULL;
    if (match(&pos, ";") && match_word(&pos, "default")) {
        expect_str(ctx, pos, &pos, "=", "I expected an '=' here");
        default_value =
            expect(ctx, start, &pos, parse_extended_expr, "I couldn't parse the default value for this table");
    }
    whitespace(ctx, &pos);
    expect_closing(ctx, &pos, "}", "I wasn't able to parse the rest of this table type");
    return NewTypeAST(ctx->file, start, pos, TableTypeAST, .key = key_type, .value = value_type,
                      .default_value = default_value);
}

type_ast_t *parse_set_type(parse_ctx_t *ctx, const char *pos) {
    const char *start = pos;
    if (!match(&pos, "|")) return NULL;
    whitespace(ctx, &pos);
    type_ast_t *item_type = parse_type(ctx, pos);
    if (!item_type) return NULL;
    pos = item_type->end;
    whitespace(ctx, &pos);
    expect_closing(ctx, &pos, "|", "I wasn't able to parse the rest of this set type");
    return NewTypeAST(ctx->file, start, pos, SetTypeAST, .item = item_type);
}

type_ast_t *parse_func_type(parse_ctx_t *ctx, const char *pos) {
    const char *start = pos;
    if (!match_word(&pos, "func")) return NULL;
    spaces(&pos);
    expect_str(ctx, start, &pos, "(", "I expected a parenthesis here");
    arg_ast_t *args = parse_args(ctx, &pos);
    spaces(&pos);
    type_ast_t *ret = match(&pos, "->") ? optional(ctx, &pos, parse_type) : NULL;
    expect_closing(ctx, &pos, ")", "I wasn't able to parse the rest of this function type");
    return NewTypeAST(ctx->file, start, pos, FunctionTypeAST, .args = args, .ret = ret);
}

type_ast_t *parse_list_type(parse_ctx_t *ctx, const char *pos) {
    const char *start = pos;
    if (!match(&pos, "[")) return NULL;
    type_ast_t *type = expect(ctx, start, &pos, parse_type, "I couldn't parse a list item type after this point");
    expect_closing(ctx, &pos, "]", "I wasn't able to parse the rest of this list type");
    return NewTypeAST(ctx->file, start, pos, ListTypeAST, .item = type);
}

type_ast_t *parse_pointer_type(parse_ctx_t *ctx, const char *pos) {
    const char *start = pos;
    bool is_stack;
    if (match(&pos, "@")) is_stack = false;
    else if (match(&pos, "&")) is_stack = true;
    else return NULL;

    spaces(&pos);
    type_ast_t *type =
        expect(ctx, start, &pos, parse_non_optional_type, "I couldn't parse a pointer type after this point");
    type_ast_t *ptr_type = NewTypeAST(ctx->file, start, pos, PointerTypeAST, .pointed = type, .is_stack = is_stack);
    spaces(&pos);
    while (match(&pos, "?"))
        ptr_type = NewTypeAST(ctx->file, start, pos, OptionalTypeAST, .type = ptr_type);
    return ptr_type;
}

type_ast_t *parse_enum_type(parse_ctx_t *ctx, const char *pos) {
    // tagged union: enum(A, B(x:Int,y:Int)=5, ...)
    const char *start = pos;
    if (!match_word(&pos, "enum")) return NULL;
    spaces(&pos);
    if (!match(&pos, "(")) return NULL;

    tag_ast_t *tags = NULL;
    whitespace(ctx, &pos);
    for (;;) {
        spaces(&pos);
        const char *tag_start = pos;
        const char *tag_name = get_id(&pos);
        if (!tag_name) break;

        spaces(&pos);
        arg_ast_t *fields;
        bool secret = false;
        if (match(&pos, "(")) {
            whitespace(ctx, &pos);
            fields = parse_args(ctx, &pos);
            whitespace(ctx, &pos);
            if (match(&pos, ";")) { // Extra flags
                whitespace(ctx, &pos);
                secret = match_word(&pos, "secret");
                whitespace(ctx, &pos);
            }
            expect_closing(ctx, &pos, ")", "I wasn't able to parse the rest of this tagged union member");
        } else {
            fields = NULL;
        }

        tags = new (tag_ast_t, .file = ctx->file, .start = tag_start, .end = pos, .name = tag_name, .fields = fields,
                    .secret = secret, .next = tags);

        if (!match_separator(ctx, &pos)) break;
    }

    whitespace(ctx, &pos);
    expect_closing(ctx, &pos, ")", "I wasn't able to parse the rest of this enum definition");

    REVERSE_LIST(tags);
    Text_t name = Texts("enum$", (int64_t)(start - ctx->file->text));
    return NewTypeAST(ctx->file, start, pos, EnumTypeAST, .name = name, .tags = tags);
}

type_ast_t *parse_type_name(parse_ctx_t *ctx, const char *pos) {
    const char *start = pos;
    const char *id = get_id(&pos);
    if (!id) return NULL;
    for (;;) {
        const char *next = pos;
        spaces(&next);
        if (!match(&next, ".")) break;
        const char *next_id = get_id(&next);
        if (!next_id) break;
        id = String(id, ".", next_id);
        pos = next;
    }
    return NewTypeAST(ctx->file, start, pos, VarTypeAST, .name = id);
}

type_ast_t *parse_non_optional_type(parse_ctx_t *ctx, const char *pos) {
    const char *start = pos;
    type_ast_t *type = NULL;
    bool success = (false || (type = parse_pointer_type(ctx, pos)) || (type = parse_list_type(ctx, pos))
                    || (type = parse_table_type(ctx, pos)) || (type = parse_set_type(ctx, pos))
                    || (type = parse_enum_type(ctx, pos)) || (type = parse_type_name(ctx, pos))
                    || (type = parse_func_type(ctx, pos)));
    if (!success && match(&pos, "(")) {
        whitespace(ctx, &pos);
        type = optional(ctx, &pos, parse_type);
        if (!type) return NULL;
        whitespace(ctx, &pos);
        expect_closing(ctx, &pos, ")", "I wasn't able to parse the rest of this type");
        type->start = start;
        type->end = pos;
    }

    return type;
}

type_ast_t *parse_type(parse_ctx_t *ctx, const char *pos) {
    const char *start = pos;
    type_ast_t *type = parse_non_optional_type(ctx, pos);
    if (!type) return NULL;
    pos = type->end;
    spaces(&pos);
    while (match(&pos, "?"))
        type = NewTypeAST(ctx->file, start, pos, OptionalTypeAST, .type = type);
    return type;
}

type_ast_t *parse_type_str(const char *str) {
    file_t *file = spoof_file("<type>", str);
    parse_ctx_t ctx = {
        .file = file,
        .on_err = NULL,
    };

    const char *pos = file->text;
    whitespace(&ctx, &pos);
    type_ast_t *ast = parse_type(&ctx, pos);
    if (!ast) return ast;
    pos = ast->end;
    whitespace(&ctx, &pos);
    if (strlen(pos) > 0) {
        parser_err(&ctx, pos, pos + strlen(pos), "I couldn't parse this part of the type");
    }
    return ast;
}
