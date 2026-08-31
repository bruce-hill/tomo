// This file defines how to compile CLI argument parsing

#include <gc.h>
#include <string.h>

#include "../environment.h"
#include "../naming.h"
#include "../stdlib/cli.h"
#include "../stdlib/datatypes.h"
#include "../stdlib/optionals.h"
#include "../stdlib/text.h"
#include "../typecheck.h"
#include "../types.h"
#include "../util.h"
#include "compilation.h"

static Text_t get_flag_options(type_t *t, Text_t separator) {
    if (t->tag == BoolType) {
        return Text("yes|no");
    } else if (t->tag == PathType) {
        return Text("path");
    } else if (t->tag == EnumType) {
        Text_t options = EMPTY_TEXT;
        for (tag_t *tag = Match(t, EnumType)->tags; tag; tag = tag->next) {
            if (Match(tag->type, StructType)->fields)
                options = Texts(options, tag->name, " ", get_flag_options(tag->type, separator));
            else options = Texts(options, tag->name);
            if (tag->next) options = Texts(options, separator);
        }
        return options;
    } else if (t->tag == StructType) {
        Text_t options = EMPTY_TEXT;
        for (arg_t *field = Match(t, StructType)->fields; field; field = field->next) {
            options = Texts(options, get_flag_options(field->type, separator));
            if (field->next) options = Texts(options, " ");
        }
        return options;
    } else if (is_numeric_type(t)) {
        return Text("N");
    } else if (t->tag == TextType || t->tag == CStringType) {
        return Text("text");
    } else if (t->tag == ListType) {
        Text_t item_option = get_flag_options(Match(t, ListType)->item_type, separator);
        return Texts(item_option, "1 ", item_option, "2...");
    } else if (t->tag == TableType && Match(t, TableType)->value_type == PRESENT_TYPE) {
        Text_t item_option = get_flag_options(Match(t, TableType)->key_type, separator);
        return Texts(item_option, "1 ", item_option, "2...");
    } else if (t->tag == TableType) {
        Text_t key_option = get_flag_options(Match(t, TableType)->key_type, separator);
        Text_t value_option = get_flag_options(Match(t, TableType)->value_type, separator);
        return Texts(key_option, "1:", value_option, "1 ", key_option, "2:", value_option, "2...");
    } else {
        return Text("value");
    }
}

// An argument's flag as the runtime actually accepts it. The dashes follow the
// role, not the length: `arg->name` is registered as `.name` (always a long
// `--flag`, even when it is one letter) and `arg->alias` as `.short_flag`
// (always a single-letter `-f`). Deriving them from the length instead
// documented `-x` for `func main.add(x:Int)`, which the parser rejects.
static OptionalText_t flagify(const char *name, bool is_short) {
    if (!name) return NONE_TEXT;
    Text_t flag = Text$from_str(name);
    flag = Text$replace(flag, Text("_"), Text("-"));
    return is_short ? Texts("-", flag) : Texts("--", flag);
}

// Emit a compile-time Text_t value for `text`. The `Text(...)` macro is
// ASCII-only (it stores the literal's *byte* count as a grapheme count and
// tags it TEXT_ASCII), so anything that can contain non-ASCII, whether Unicode
// command names, doc comments, or user-supplied USAGE/HELP metadata, has to go
// through Text$from_str(), which decodes the UTF-8, or text operations on it
// crash.
static Text_t text_literal(Text_t text) {
    return Texts("Text$from_str(", compile_text_literal(text), ")");
}

// A subcommand's CLI word: the identifier with underscores converted to dashes
static const char *cli_word(const char *part, size_t len) {
    char *word = GC_MALLOC_ATOMIC(len + 1);
    for (size_t i = 0; i < len; i++)
        word[i] = part[i] == '_' ? '-' : part[i];
    word[len] = '\0';
    return word;
}

static cli_command_def_t *get_child(cli_command_def_t **children, const char *word) {
    cli_command_def_t **p = children;
    while (*p && !streq((*p)->word, word))
        p = &(*p)->next;
    if (!*p) *p = new (cli_command_def_t, .word = word);
    return *p;
}

// Collect a file's `func main.foo.baz(...)` definitions into a subcommand
// tree (or NULL if the file doesn't define any subcommands):
public
cli_command_def_t *get_cli_subcommands(env_t *env, ast_t *file_ast) {
    cli_command_def_t *commands = NULL;
    for (ast_list_t *stmt = Match(file_ast, Block)->statements; stmt; stmt = stmt->next) {
        if (stmt->ast->tag != FunctionDef) continue;
        const char *name = Match(Match(stmt->ast, FunctionDef)->name, Var)->name;
        if (strncmp(name, "main.", strlen("main.")) != 0) continue;

        cli_command_def_t **level = &commands, *node = NULL;
        for (const char *p = name + strlen("main."); p;) {
            const char *dot = strchr(p, '.');
            node = get_child(level, cli_word(p, dot ? (size_t)(dot - p) : strlen(p)));
            level = &node->children;
            p = dot ? dot + 1 : NULL;
        }
        if (node->def) code_err(stmt->ast, "The subcommand '", node->word, "' is already defined in this file");
        node->def = stmt->ast;
        node->binding = get_binding(env, name);
        assert(node->binding && node->binding->type->tag == FunctionType);
        type_t *ret = Match(node->binding->type, FunctionType)->ret;
        if (ret->tag != VoidType && ret->tag != AbortType)
            code_err(stmt->ast, "This subcommand function has a return type of ", type_to_text(ret),
                     ", but it should not have any return value!");
    }
    return commands;
}

// Emit the storage, argument spec, and handler function for one command, plus
// (recursively) its children, and return the name of its cli_command_t. The
// runtime (src/stdlib/cli.c) does all the dispatching and help rendering from
// these, the same code path the `tomo` CLI itself goes through.
static Text_t compile_command_spec(env_t *env, cli_command_def_t *node, Text_t c_path, Text_t *defs, Text_t *inits) {
    // Children first: a parent's child list refers to their cli_command_t's.
    Text_t child_names = EMPTY_TEXT;
    int num_children = 0;
    for (cli_command_def_t *child = node->children; child; child = child->next) {
        Text_t child_path = Texts(c_path, "$", Text$replace(Text$from_str(child->word), Text("-"), Text("_")));
        Text_t child_name = compile_command_spec(env, child, child_path, defs, inits);
        child_names = Texts(child_names, num_children > 0 ? Text(", ") : EMPTY_TEXT, "&", child_name);
        num_children += 1;
    }
    if (num_children > 0)
        *defs = Texts(*defs, "static cli_command_t *cli_children$", c_path, "[] = {", child_names, "};\n");

    arg_t *args = node->binding ? Match(node->binding->type, FunctionType)->args : NULL;
    int num_args = 0;
    if (node->def) {
        // One static per argument for the parser to fill in, reset to the
        // type's empty value before parsing (statics start zeroed, which isn't
        // every type's empty value):
        for (arg_t *arg = args; arg; arg = arg->next) {
            Text_t storage = Texts("cli_arg$", c_path, "$", Text$from_str(arg->name));
            *defs = Texts(*defs, "static ", compile_declaration(arg->type, storage), ";\n");
            *inits = Texts(*inits, storage, " = ", compile_empty(arg->type), ";\n");
        }

        *defs = Texts(*defs, "static cli_arg_t cli_spec$", c_path, "[] = {\n");
        for (arg_t *arg = args; arg; arg = arg->next) {
            *defs = Texts(
                *defs, "{", quoted_text(Text$replace(Text$from_str(arg->name), Text("_"), Text("-"))), ", &cli_arg$",
                c_path, "$", Text$from_str(arg->name), ", ", compile_type_info(arg->type),
                arg->default_val ? EMPTY_TEXT : Text(", .required=true"),
                arg->alias ? Texts(", .short_flag=", quoted_text(Text$from_str(arg->alias)),
                                   "[0]") // TODO: escape char properly
                           : EMPTY_TEXT,
                arg->comment.length > 0 ? Texts(", .description=", compile_text_literal(arg->comment)) : EMPTY_TEXT,
                arg->default_val
                    ? Texts(", .default_text=",
                            compile_text_literal(Text$from_strn(
                                arg->default_val->start, (size_t)(arg->default_val->end - arg->default_val->start))))
                    : EMPTY_TEXT,
                "},\n");
            num_args += 1;
        }
        *defs = Texts(*defs, "};\n");

        // The handler applies default values (lazily, so their side effects
        // only happen when the argument wasn't given) and calls the function:
        *defs = Texts(*defs, "static int cli_handler$", c_path,
                      "(cli_command_t *self, List_t extra_args) {\n"
                      "(void)self, (void)extra_args;\n");
        int64_t i = 0;
        for (arg_t *arg = args; arg; arg = arg->next) {
            if (arg->default_val) {
                Text_t default_val =
                    arg->type ? compile_to_type(env, arg->default_val, arg->type) : compile(env, arg->default_val);
                *defs = Texts(*defs, "if (!cli_spec$", c_path, "[", i, "].populated) cli_arg$", c_path, "$",
                              Text$from_str(arg->name), " = ", default_val, ";\n");
            }
            i += 1;
        }
        *defs = Texts(*defs, node->binding->code, "(");
        for (arg_t *arg = args; arg; arg = arg->next)
            *defs =
                Texts(*defs, "cli_arg$", c_path, "$", Text$from_str(arg->name), arg->next ? Text(", ") : EMPTY_TEXT);
        *defs = Texts(*defs, ");\nreturn 0;\n}\n");
    }

    Text_t command_name = Texts("cli_command$", c_path);
    *defs = Texts(
        *defs, "static cli_command_t ", command_name, " = {",
        node->word ? Texts(".name=", quoted_text(Text$from_str(node->word)), ", ") : EMPTY_TEXT,
        node->def && Match(node->def, FunctionDef)->comment.length > 0
            ? Texts(".summary=", compile_text_literal(Match(node->def, FunctionDef)->comment), ", ")
            : EMPTY_TEXT,
        node->def ? Texts(".spec_len=", num_args, ", .spec=cli_spec$", c_path, ", .handler=cli_handler$", c_path, ", ")
                  : EMPTY_TEXT,
        num_children > 0 ? Texts(".num_children=", (int64_t)num_children, ", .children=cli_children$", c_path)
                         : EMPTY_TEXT,
        "};\n");
    return command_name;
}

// Compile a program's whole command-line interface to a static cli_spec_t that
// `tomo_dispatch_command()` (src/stdlib/cli.c) drives, the same as the `tomo`
// CLI's own. If the file defines a plain `main()`, it becomes the root
// command's handler: it runs when the first argument doesn't name a
// subcommand, exactly like `main.stash` runs when the word after it isn't
// `pop`.
public
Text_t compile_cli_dispatch(env_t *env, ast_t *file_ast, cli_command_def_t *commands, Text_t entry) {
    cli_command_def_t root = {.children = commands};
    for (ast_list_t *stmt = Match(file_ast, Block)->statements; stmt; stmt = stmt->next) {
        if (stmt->ast->tag != FunctionDef) continue;
        if (!streq(Match(Match(stmt->ast, FunctionDef)->name, Var)->name, "main")) continue;
        root.def = stmt->ast;
        root.binding = get_binding(env, "main");
        assert(root.binding && root.binding->type->tag == FunctionType);
        break;
    }

    Text_t defs = EMPTY_TEXT, inits = EMPTY_TEXT;
    Text_t root_name = compile_command_spec(env, &root, Text("main"), &defs, &inits);

    OptionalText_t version = ast_metadata(file_ast, "VERSION");
    if (version.tag == TEXT_NONE) version = Text("0.0.1");
    // -v is the long-standing short alias for --version in generated programs
    // (the `tomo` CLI itself is long-only, since -v there means --verbose):
    defs = Texts(defs, "static cli_spec_t cli_spec$program = {.version=", Text$quoted(version, false, Text("\"")),
                 ", .version_short='v'};\n");

    // USAGE/HELP metadata overrides the autogenerated text for the program as
    // a whole (Text_t values can't be static initializers, so they're assigned
    // at startup along with the rest of the spec):
    OptionalText_t usage = ast_metadata(file_ast, "USAGE");
    OptionalText_t help = ast_metadata(file_ast, "HELP");
    inits = Texts(inits, "cli_spec$program.root = ", root_name, ";\n");
    if (usage.tag != TEXT_NONE)
        // Built at startup so it picks up the palette (which depends on whether
        // the output is colored), same as the autogenerated usage text:
        inits = Texts(inits,
                      "cli_spec$program.root.usage = Texts(tomo_cli_style().usage, \"Usage:\", "
                      "tomo_cli_style().reset, \" \", Text$from_str(argv[0]), Text(\" \"), ",
                      text_literal(usage), ");\n");
    if (help.tag != TEXT_NONE) inits = Texts(inits, "cli_spec$program.root.help = ", text_literal(help), ";\n");

    return Texts(defs, "int parse_and_run$$", entry,
                 "(int argc, char *argv[]) {\n"
                 "tomo_init();\n",
                 namespace_name(env, env->namespace, Text("$initialize")), "();\n\n", inits,
                 "return tomo_dispatch_command(argc, argv, &cli_spec$program);\n"
                 "}\n");
}

// The .TP option entries for one argument spec:
static Text_t manpage_options(arg_t *args) {
    Text_t man = EMPTY_TEXT;
    for (arg_t *arg = args; arg; arg = arg->next) {
        OptionalText_t flag = flagify(arg->name, /*is_short=*/false);
        assert(flag.tag != TEXT_NONE);
        Text_t flags = Texts("\\f[B]", flag, "\\f[R]");
        if (arg->alias) flags = Texts(flags, ", \\f[B]", flagify(arg->alias, /*is_short=*/true), "\\f[R]");
        if (non_optional(arg->type)->tag == BoolType)
            flags = Texts(flags, " | \\f[B]--no-", Text$without_prefix(flag, Text("--")), "\\f[R]");

        man = Texts(man, "\n.TP\n", flags);
        if (non_optional(arg->type)->tag != BoolType) {
            Text_t options = Texts("\\f[I]", get_flag_options(arg->type, Text("\\f[R] | \\f[I]")), "\\f[R]");
            man = Texts(man, " ", options);
        }

        if (arg->comment.length > 0) {
            man = Texts(man, "\n", arg->comment);
        }
    }
    return man;
}

// The .SS sections documenting each subcommand (and sub-subcommand):
static Text_t manpage_commands(Text_t program, cli_command_def_t *commands, Text_t path) {
    Text_t man = EMPTY_TEXT;
    for (cli_command_def_t *c = commands; c; c = c->next) {
        Text_t subpath = path.length > 0 ? Texts(path, " ", c->word) : Text$from_str(c->word);
        if (c->def) {
            man = Texts(man, ".SS \"", program, " ", subpath, "\"\n");
            Text_t comment = Match(c->def, FunctionDef)->comment;
            if (comment.length > 0) man = Texts(man, comment, "\n");
            man = Texts(man, manpage_options(Match(c->binding->type, FunctionType)->args), "\n");
        }
        man = Texts(man, manpage_commands(program, c->children, subpath));
    }
    return man;
}

public
Text_t compile_manpage(Text_t program, ast_t *ast, arg_t *args, cli_command_def_t *commands) {
    OptionalText_t user_manpage = ast_metadata(ast, "MANPAGE");
    if (user_manpage.tag != TEXT_NONE) {
        // Still stamp the marker so `tomo uninstall` recognizes it as ours:
        return Texts(Text(TOMO_MANPAGE_MARKER "\n"), user_manpage);
    }

    OptionalText_t synopsys = ast_metadata(ast, "MANPAGE_SYNOPSYS");
    OptionalText_t description = ast_metadata(ast, "MANPAGE_DESCRIPTION");
    Text_t date = Text(""); // TODO: use date
    Text_t man = Texts(TOMO_MANPAGE_MARKER "\n"
                                           ".TH \"",
                       Text$upper(program, Text("C")), "\" \"1\" \"", date,
                       "\" \"\" \"\"\n"
                       ".SH NAME\n",
                       program, " \\- ", synopsys.tag == TEXT_NONE ? Text("a Tomo program") : synopsys, "\n");

    if (description.tag != TEXT_NONE) {
        man = Texts(man, ".SH DESCRIPTION\n", description, "\n");
    }

    // A program can have both: `main()`'s own flags, plus subcommands.
    // manpage_options() doesn't end in a newline, so add one before any
    // section that follows it:
    if (args) man = Texts(man, ".SH OPTIONS\n", manpage_options(args), "\n");
    if (commands) man = Texts(man, ".SH COMMANDS\n", manpage_commands(program, commands, EMPTY_TEXT));

    return man;
}
