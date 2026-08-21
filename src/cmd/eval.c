// `tomo eval`: evaluate a Tomo expression and print its result

#include <gc.h>
#include <stdlib.h>
#include <string.h>

#include "../ast.h"
#include "../config.h"
#include "../parse/context.h"
#include "../parse/files.h"
#include "../parse/statements.h"
#include "../parse/utils.h"
#include "../sha256.h"
#include "../stdlib/datatypes.h"
#include "../stdlib/files.h"
#include "../stdlib/paths.h"
#include "../stdlib/print.h"
#include "../stdlib/stdlib.h"
#include "../stdlib/text.h"
#include "../util.h"
#include "commands.h"
#include "common.h"

static Text_t expr = EMPTY_TEXT;

static cli_arg_t eval_spec[] = {
    {"expr", &expr, &Text$info, .positional = true, .required = true, .metavar = "'<expr>'",
     .description = "the Tomo expression to evaluate"},
    OPTIMIZATION_FLAG,
    VERBOSE_FLAG,
};

// Whether the final statement of an eval string yields a value worth printing.
// Statements that evaluate to nothing (declarations, assignments, loops,
// control flow, definitions, ...) are run as-is; everything else is wrapped in
// a colorized print. This mirrors the tags the typechecker treats as VoidType
// for a block's final statement (see get_type()'s Block case).
static bool prints_a_value(ast_e tag) {
    switch (tag) {
    case Declare:
    case Assign:
    case PowerUpdate:
    case MultiplyUpdate:
    case DivideUpdate:
    case FloorDivideUpdate:
    case ModUpdate:
    case Mod1Update:
    case PlusUpdate:
    case MinusUpdate:
    case ConcatUpdate:
    case LeftShiftUpdate:
    case UnsignedLeftShiftUpdate:
    case RightShiftUpdate:
    case UnsignedRightShiftUpdate:
    case AndUpdate:
    case OrUpdate:
    case XorUpdate:
    case FunctionDef:
    case ConvertDef:
    case StructDef:
    case EnumDef:
    case LangDef:
    case Use:
    case For:
    case While:
    case Repeat:
    case Continue:
    case Break:
    case Pass:
    case Defer:
    case Return:
    case DebugLog:
    case Assert:
    case Metadata: return false;
    default: return true;
    }
}

// The source text a statement was parsed from (its start/end point into the
// original expression string):
static Text_t stmt_source(ast_t *ast) {
    return Text$from_strn(ast->start, (size_t)(ast->end - ast->start));
}

static int cmd_eval(cli_command_t *self, List_t extra_args) {
    (void)self;
    set_default_logs(0);

    // Parse the expression as a sequence of statements. This is like parsing a
    // block, but `use` statements (only allowed at the top level of a file) are
    // also accepted so compound inputs like `use random; random.int(1, 100)`
    // work. Statements may be separated by newlines or `;`.
    file_t *file = spoof_file("<eval>", Text$as_c_string(expr));
    parse_ctx_t ctx = {.file = file};
    const char *pos = file->text;
    whitespace(&ctx, &pos);

    ast_list_t *statements = NULL, *statements_tail = NULL;
    while (*pos) {
        ast_t *stmt = parse_use(&ctx, pos);
        if (!stmt) stmt = parse_statement(&ctx, pos);
        if (!stmt) break;
        ast_list_t *node = new (ast_list_t, .ast = stmt);
        if (statements_tail) statements_tail->next = node;
        else statements = node;
        statements_tail = node;
        pos = stmt->end;
        spaces(&pos);
        while (match(&pos, ";"))
            spaces(&pos);
        whitespace(&ctx, &pos);
    }
    if (*pos) print_err("I couldn't parse this part of the expression: ", Text$from_str(pos));

    // `use` statements are only allowed at the top level, so hoist them above
    // the generated main(); everything else becomes main()'s body:
    Text_t uses = EMPTY_TEXT;
    Text_t body = EMPTY_TEXT;
    ast_t *last = NULL;
    for (ast_list_t *stmt = statements; stmt; stmt = stmt->next) {
        if (stmt->ast->tag == Use) continue;
        last = stmt->ast;
    }
    if (last == NULL) print_err("There is no expression to evaluate!");

    for (ast_list_t *stmt = statements; stmt; stmt = stmt->next) {
        if (stmt->ast->tag == Use) {
            uses = Texts(uses, stmt_source(stmt->ast), "\n");
            continue;
        }
        Text_t line = stmt_source(stmt->ast);
        // Wrap the final value-producing statement so its result is printed
        // (colorized only when the output is going to a terminal):
        if (stmt->ast == last && prints_a_value(stmt->ast->tag))
            line = Texts("say(\"$(", line, ")\"", USE_COLOR ? Text("~colorized") : EMPTY_TEXT, ")");
        // Indent every line so the statement sits inside main()'s body:
        body = Texts(body, "    ", Text$replace(line, Text("\n"), Text("\n    ")), "\n");
    }

    Text_t program = Texts(uses, uses.length > 0 ? Text("\n") : EMPTY_TEXT, "func main()\n", body);

    // Name the scratch file by a hash of its contents so that distinct
    // expressions never share a stale cached executable (the build system's
    // mtime staleness check has second granularity, which would otherwise
    // reuse the previous eval's binary for a different expression written in
    // the same second), while re-evaluating the same expression reuses its
    // cached build:
    const char *program_str = Text$as_c_string(program);
    char hash[SHA256_HEX_SIZE];
    sha256_hex(program_str, strlen(program_str), hash);
    hash[12] = '\0';

    Path_t dir = Path$child(xdg_tomo_dir("XDG_STATE_HOME", "~/.local/state"), Texts("tomo@", TOMO_VERSION));
    Path$create_directory(dir, 0755, true);
    Path_t path = Path$child(dir, Texts("eval-", Text$from_str(hash), ".tm"));
    if (!Path$exists(path)) Path$write(path, program, 0644);

    return compile_and_exec(path, extra_args);
}

cli_command_t eval_command = {
    .name = "eval",
    .summary = "Evaluate a Tomo expression and print its result",
    .description = "The expression may be several statements separated by newlines or `;`, e.g.\n"
                   "`tomo eval 'use random; random.int(1, 100)'`. The value of the final statement\n"
                   "is printed (with syntax coloring when stdout is a terminal).",
    .spec_len = sizeof(eval_spec) / sizeof(eval_spec[0]),
    .spec = eval_spec,
    .handler = cmd_eval,
};
