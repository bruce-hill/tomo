// Parsing logic for type definitions

#include <stdbool.h>
#include <string.h>

#include "../ast.h"
#include "../stdlib/util.h"
#include "context.h"
#include "errors.h"
#include "files.h"
#include "functions.h"
#include "statements.h"
#include "text.h"
#include "typedefs.h"
#include "utils.h"

ast_t *parse_namespace(parse_ctx_t *ctx, const char *pos) {
    const char *start = pos;
    whitespace(ctx, &pos);
    int64_t indent = get_indent(ctx, pos);
    ast_list_t *statements = NULL;
    for (;;) {
        const char *next = pos;
        whitespace(ctx, &next);
        if (get_indent(ctx, next) != indent) break;
        ast_t *stmt;
        if ((stmt = optional(ctx, &pos, parse_struct_def)) || (stmt = optional(ctx, &pos, parse_func_def))
            || (stmt = optional(ctx, &pos, parse_enum_def)) || (stmt = optional(ctx, &pos, parse_lang_def))
            || (stmt = optional(ctx, &pos, parse_extend)) || (stmt = optional(ctx, &pos, parse_convert_def))
            || (stmt = optional(ctx, &pos, parse_use)) || (stmt = optional(ctx, &pos, parse_extern))
            || (stmt = optional(ctx, &pos, parse_inline_c)) || (stmt = optional(ctx, &pos, parse_declaration))) {
            statements = new (ast_list_t, .ast = stmt, .next = statements);
            pos = stmt->end;
            whitespace(ctx, &pos); // TODO: check for newline
            // if (!(space_types & WHITESPACE_NEWLINES)) {
            //     pos = stmt->end;
            //     break;
            // }
        } else {
            if (get_indent(ctx, next) > indent && next < eol(next))
                parser_err(ctx, next, eol(next), "I couldn't parse this namespace declaration");
            break;
        }
    }
    REVERSE_LIST(statements);
    return NewAST(ctx->file, start, pos, Block, .statements = statements);
}

ast_t *parse_struct_def(parse_ctx_t *ctx, const char *pos) {
    // struct Foo(...) [: \n body]
    const char *start = pos;
    if (!match_word(&pos, "struct")) return NULL;

    int64_t starting_indent = get_indent(ctx, pos);

    spaces(&pos);
    const char *name = get_id(&pos);
    if (!name) parser_err(ctx, start, pos, "I expected a name for this struct");
    spaces(&pos);

    if (!match(&pos, "(")) parser_err(ctx, pos, pos, "I expected a '(' and a list of fields here");

    arg_ast_t *fields = parse_args(ctx, &pos);

    whitespace(ctx, &pos);
    bool secret = false, external = false, opaque = false;
    if (match(&pos, ";")) { // Extra flags
        whitespace(ctx, &pos);
        for (;;) {
            if (match_word(&pos, "secret")) {
                secret = true;
            } else if (match_word(&pos, "extern")) {
                external = true;
            } else if (match_word(&pos, "opaque")) {
                if (fields)
                    parser_err(ctx, pos - strlen("opaque"), pos, "A struct can't be opaque if it has fields defined");
                opaque = true;
            } else {
                break;
            }

            if (!match_separator(ctx, &pos)) break;
        }
    }

    expect_closing(ctx, &pos, ")", "I wasn't able to parse the rest of this struct");

    ast_t *namespace = NULL;
    const char *ns_pos = pos;
    whitespace(ctx, &ns_pos);
    int64_t ns_indent = get_indent(ctx, ns_pos);
    if (ns_indent > starting_indent) {
        pos = ns_pos;
        namespace = optional(ctx, &pos, parse_namespace);
    }
    if (!namespace) namespace = NewAST(ctx->file, pos, pos, Block, .statements = NULL);
    return NewAST(ctx->file, start, pos, StructDef, .name = name, .fields = fields, .namespace = namespace,
                  .secret = secret, .external = external, .opaque = opaque);
}

ast_t *parse_enum_def(parse_ctx_t *ctx, const char *pos) {
    // tagged union: enum Foo(a, b(x:Int,y:Int)=5, ...) [: \n namespace]
    const char *start = pos;
    if (!match_word(&pos, "enum")) return NULL;
    int64_t starting_indent = get_indent(ctx, pos);
    spaces(&pos);
    const char *name = get_id(&pos);
    if (!name) parser_err(ctx, start, pos, "I expected a name for this enum");
    spaces(&pos);
    if (!match(&pos, "(")) return NULL;

    tag_ast_t *tags = NULL;
    whitespace(ctx, &pos);
    for (;;) {
        spaces(&pos);
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

        tags = new (tag_ast_t, .name = tag_name, .fields = fields, .secret = secret, .next = tags);

        if (!match_separator(ctx, &pos)) break;
    }

    whitespace(ctx, &pos);
    expect_closing(ctx, &pos, ")", "I wasn't able to parse the rest of this enum definition");

    REVERSE_LIST(tags);

    if (tags == NULL) parser_err(ctx, start, pos, "This enum does not have any tags!");

    ast_t *namespace = NULL;
    const char *ns_pos = pos;
    whitespace(ctx, &ns_pos);
    int64_t ns_indent = get_indent(ctx, ns_pos);
    if (ns_indent > starting_indent) {
        pos = ns_pos;
        namespace = optional(ctx, &pos, parse_namespace);
    }
    if (!namespace) namespace = NewAST(ctx->file, pos, pos, Block, .statements = NULL);

    return NewAST(ctx->file, start, pos, EnumDef, .name = name, .tags = tags, .namespace = namespace);
}

ast_t *parse_lang_def(parse_ctx_t *ctx, const char *pos) {
    const char *start = pos;
    // lang Name: [namespace...]
    if (!match_word(&pos, "lang")) return NULL;
    int64_t starting_indent = get_indent(ctx, pos);
    spaces(&pos);
    const char *name = get_id(&pos);
    if (!name) parser_err(ctx, start, pos, "I expected a name for this lang");
    spaces(&pos);

    ast_t *namespace = NULL;
    const char *ns_pos = pos;
    whitespace(ctx, &ns_pos);
    int64_t ns_indent = get_indent(ctx, ns_pos);
    if (ns_indent > starting_indent) {
        pos = ns_pos;
        namespace = optional(ctx, &pos, parse_namespace);
    }
    if (!namespace) namespace = NewAST(ctx->file, pos, pos, Block, .statements = NULL);

    return NewAST(ctx->file, start, pos, LangDef, .name = name, .namespace = namespace);
}

ast_t *parse_extend(parse_ctx_t *ctx, const char *pos) {
    const char *start = pos;
    // extend Name: body...
    if (!match_word(&pos, "extend")) return NULL;
    int64_t starting_indent = get_indent(ctx, pos);
    spaces(&pos);
    const char *name = get_id(&pos);
    if (!name) parser_err(ctx, start, pos, "I expected a name for this lang");

    ast_t *body = NULL;
    const char *ns_pos = pos;
    whitespace(ctx, &ns_pos);
    int64_t ns_indent = get_indent(ctx, ns_pos);
    if (ns_indent > starting_indent) {
        pos = ns_pos;
        body = optional(ctx, &pos, parse_namespace);
    }
    if (!body) body = NewAST(ctx->file, pos, pos, Block, .statements = NULL);

    return NewAST(ctx->file, start, pos, Extend, .name = name, .body = body);
}
