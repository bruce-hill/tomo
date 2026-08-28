// Command-line argument parsing

#include <fcntl.h>
#include <gc.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/param.h>
#include <time.h>

#include "../config.h"
#include "bools.h"
#include "bytes.h"
#include "c_strings.h"
#include "cli.h"
#include "integers.h"
#include "metamethods.h"
#include "floats.h"
#include "nums.h"
#include "number.h"
#include "optionals.h"
#include "paths.h"
#include "print.h"
#include "stdlib.h"
#include "tables.h"
#include "text.h"
#include "util.h"

// The palette, or an all-empty one when color is off:
public
cli_style_t tomo_cli_style(void) {
    if (USE_COLOR)
        return (cli_style_t){
            .bold = "\x1b[1m",       .dim = "\x1b[2m",     .italic = "\x1b[3m",  .reset = "\x1b[m",
            .heading = "\x1b[4;1m",  .usage = "\x1b[93;4;1m", .flag = "\x1b[93;1m", .value = "\x1b[1;94m",
            .command = "\x1b[1;32m", .error = "\x1b[91;1m",
        };
    return (cli_style_t){
        .bold = "",    .dim = "",   .italic = "", .reset = "",  .heading = "",
        .usage = "",   .flag = "",  .value = "",  .command = "", .error = "",
    };
}

static bool pop_boolean_cli_flag(List_t *args, char short_flag, const char *flag, bool *dest) {
    const char *no_flag = String("no-", flag);
    for (int64_t i = 0; i < (int64_t)args->length; i++) {
        const char *arg = *(const char **)(args->data + i * args->stride);
        if (arg[0] == '-' && arg[1] == '-') {
            if (arg[2] == '\0') {
                // Case: -- (end of flags and beginning of positional args)
                break;
            } else if (streq(arg + 2, flag)) {
                // Case: --flag
                *dest = true;
                List$remove_at(args, I(i + 1), I(1), sizeof(const char *));
                return true;
            } else if (streq(arg + 2, no_flag)) {
                // Case: --no-flag
                *dest = false;
                List$remove_at(args, I(i + 1), I(1), sizeof(const char *));
                return true;
            } else if (starts_with(arg + 2, flag) && arg[2 + strlen(flag)] == '=') {
                // Case: --flag=yes|no|true|false|on|off|0|1
                OptionalBool_t b = Bool$parse(Text$from_str(arg + 2 + strlen(flag) + 1), NULL);
                if (b == NONE_BOOL) print_err("Invalid boolean value for flag ", flag, ": ", arg);
                *dest = b;
                List$remove_at(args, I(i + 1), I(1), sizeof(const char *));
                return true;
            }
        } else if (short_flag && arg[0] == '-' && arg[1] != '-' && strchr(arg + 1, short_flag)) {
            const char *loc = strchr(arg + 1, short_flag);
            if (loc[1] == '=') {
                // Case: -f=yes|no|true|false|on|off|1|0
                OptionalBool_t b = Bool$parse(Text$from_str(loc + 2), NULL);
                if (b == NONE_BOOL) {
                    char short_str[2] = {short_flag, '\0'};
                    print_err("Invalid boolean value for flag -", short_str, ": ", arg);
                }
                *dest = b;
                if (loc > arg + 1) {
                    // Case: -abcdef=... -> -abcde
                    char *remainder = String(string_slice(arg, (size_t)(loc - arg)));
                    if unlikely (args->data_refcount > 0) List$compact(args, sizeof(const char *));
                    *(const char **)(args->data + i * args->stride) = remainder;
                } else {
                    // Case: -f=... -> pop flag entirely
                    List$remove_at(args, I(i + 1), I(1), sizeof(const char *));
                }
                return true;
            } else {
                // Case: -...f...
                *dest = true;
                if (strlen(arg) == 2) {
                    // Case: -f -> pop flag entirely
                    List$remove_at(args, I(i + 1), I(1), sizeof(const char *));
                } else {
                    // Case: -abcdefgh... -> -abcdegh...
                    char *remainder =
                        String(string_slice(arg, (size_t)(loc - arg)), string_slice(loc + 1, strlen(loc + 1)));
                    if unlikely (args->data_refcount > 0) List$compact(args, sizeof(const char *));
                    *(const char **)(args->data + i * args->stride) = remainder;
                }
                return true;
            }
        }
    }
    return false;
}

public
void tomo_parse_arg_list(List_t args, cli_help_info_t info, int spec_len, cli_arg_t spec[spec_len]) {
    for (int i = 0; i < spec_len; i++) {
        spec[i].populated = pop_cli_flag(&args, spec[i].short_flag, spec[i].name, spec[i].dest, spec[i].type);
    }

    bool show_help = false;
    if (pop_boolean_cli_flag(&args, info.help_short, "help", &show_help) && show_help) {
        print(info.help);
        exit(0);
    }
    List_t before_double_dash = args, after_double_dash = EMPTY_LIST;
    for (int i = 0; i < (int64_t)args.length; i++) {
        const char *arg = *(const char **)(args.data + i * args.stride);
        if (streq(arg, "--")) {
            before_double_dash = List$slice(args, I(1), I(i));
            after_double_dash = List$slice(args, I(i + 2), I(-1));
            break;
        }
    }

    for (int i = 0; i < spec_len && before_double_dash.length > 0; i++) {
        if (info.strict_positionals && !spec[i].positional) continue;
        if (!spec[i].populated) {
            spec[i].populated =
                pop_cli_positional(&before_double_dash, spec[i].name, spec[i].dest, spec[i].type, false);
        }
    }

    for (int i = 0; i < spec_len && after_double_dash.length > 0; i++) {
        if (info.strict_positionals && !spec[i].positional) continue;
        if (!spec[i].populated) {
            spec[i].populated = pop_cli_positional(&after_double_dash, spec[i].name, spec[i].dest, spec[i].type, true);
        }
    }

    for (int i = 0; i < spec_len; i++) {
        if (!spec[i].populated && spec[i].required)
            print_err("Missing required ", spec[i].positional ? "argument" : "flag", ": ", spec[i].name, "\n",
                      info.usage, info.command_hint);
    }

    List_t remaining_args = List$concat(before_double_dash, after_double_dash, sizeof(const char *));
    if (remaining_args.length > 0) {
        // Show the usage too: leftovers usually mean an earlier word was
        // consumed as something the user didn't intend (a mistyped command
        // name swallowed as a positional argument, say).
        print_err("Unknown flag values: ", CString$join(" ", remaining_args), "\n", info.usage,
                  info.command_hint);
    }
}

// The value placeholder shown for a flag in usage/help text, derived from its
// parsing type (mirroring the compiler's generated help in src/compile/cli.c):
static Text_t flag_value_options(const char *metavar, const TypeInfo_t *type) {
    if (metavar) return Text$from_str(metavar);
    if (type == &Bool$info) return Text("yes|no");
    if (type == &Path$info) return Text("path");
    if (type == &Int$info || type == &Int64$info || type == &Int32$info || type == &Int16$info || type == &Int8$info
        || type == &Byte$info || type == &Num$info || type == &Float64$info || type == &Float32$info)
        return Text("N");
    if (type == &CString$info || type->tag == TextInfo) return Text("text");
    if (type->tag == OptionalInfo) return flag_value_options(NULL, type->OptionalInfo.type);
    if (type->tag == PointerInfo) return flag_value_options(NULL, type->PointerInfo.pointed);
    if (type->tag == ListInfo) {
        Text_t item = flag_value_options(NULL, type->ListInfo.item);
        return Texts(item, "1 ", item, "2...");
    }
    if (type->tag == TableInfo) {
        Text_t key = flag_value_options(NULL, type->TableInfo.key);
        if (type->TableInfo.value->size == 0) return Texts(key, "1 ", key, "2...");
        Text_t value = flag_value_options(NULL, type->TableInfo.value);
        return Texts(key, "1:", value, "1 ", key, "2:", value, "2...");
    }
    if (type->tag == EnumInfo) {
        Text_t options = EMPTY_TEXT;
        for (int t = 0; t < type->EnumInfo.num_tags; t++) {
            if (t > 0) options = Texts(options, "|");
            options = Texts(options, type->EnumInfo.tags[t].name);
        }
        return options;
    }
    return Text("value");
}

static bool is_bool_arg(const cli_arg_t *arg) {
    const TypeInfo_t *type = arg->type;
    while (type->tag == OptionalInfo)
        type = type->OptionalInfo.type;
    return type == &Bool$info;
}

// A positional argument's display name: its metavar (or name), with "..."
// appended for lists:
static Text_t positional_display(const cli_arg_t *arg) {
    Text_t name = Text$from_str(arg->metavar ? arg->metavar : arg->name);
    if (arg->type->tag == ListInfo || arg->type->tag == TableInfo) name = Texts(name, "...");
    return name;
}

// Generate a one-line "Usage: <prefix> [flags] positionals" from an arg spec:
public
Text_t tomo_generate_usage(Text_t prefix, int spec_len, cli_arg_t spec[spec_len]) {
    cli_style_t style = tomo_cli_style();
    Text_t usage = Texts(style.usage, "Usage:", style.reset, " ", prefix);
    for (int i = 0; i < spec_len; i++) { // Flags first...
        if (spec[i].positional) continue;
        Text_t flag = Texts(style.bold, "--", spec[i].name, style.reset);
        if (spec[i].short_flag)
            flag = Texts(flag, "|", style.bold, "-", Text$from_strn((char[]){spec[i].short_flag}, 1), style.reset);
        if (!is_bool_arg(&spec[i])) flag = Texts(flag, " ", flag_value_options(spec[i].metavar, spec[i].type));
        usage = Texts(usage, " ", spec[i].required ? flag : Texts("[", flag, "]"));
    }
    for (int i = 0; i < spec_len; i++) { // ...then positionals
        if (!spec[i].positional) continue;
        Text_t name = Texts(style.bold, positional_display(&spec[i]), style.reset);
        usage = Texts(usage, " ", spec[i].required ? name : Texts("[", name, "]"));
    }
    return usage;
}

// One "  --flag|-f value  description" line of help text for an arg:
static Text_t arg_help_line(const cli_arg_t *arg) {
    cli_style_t style = tomo_cli_style();
    Text_t line;
    if (arg->positional) {
        line = Texts("  ", style.bold, positional_display(arg), style.reset);
    } else {
        Text_t flags = Texts(style.flag, "--", arg->name, style.reset);
        if (arg->short_flag)
            // reset before dim: the comma is dim on its own, not dim-on-top-of
            // the flag color it follows
            flags = Texts(style.flag, "-", Text$from_strn((char[]){arg->short_flag}, 1), style.reset, style.dim, ",",
                          style.reset, " ", flags);
        if (is_bool_arg(arg)) flags = Texts(flags, "|", style.flag, "--no-", arg->name, style.reset);
        line = Texts("  ", flags);
        if (!is_bool_arg(arg))
            line = Texts(line, " ", style.value, flag_value_options(arg->metavar, arg->type), style.reset);
    }
    if (arg->description) line = Texts(line, " ", style.italic, arg->description, style.reset);
    if (arg->default_text) line = Texts(line, " ", style.dim, "(default:", arg->default_text, ")", style.reset);
    return Texts(line, "\n");
}

// The "Commands:" listing of a command's children, names column-aligned:
static Text_t commands_listing(cli_command_t *command) {
    int64_t width = 0;
    for (int i = 0; i < command->num_children; i++) {
        int64_t w = Text$from_str(command->children[i]->name).length;
        if (w > width) width = w;
    }
    cli_style_t style = tomo_cli_style();
    Text_t listing = Texts("\n", style.heading, "Commands:", style.reset, "\n");
    for (int i = 0; i < command->num_children; i++) {
        cli_command_t *child = command->children[i];
        Text_t name = Text$from_str(child->name);
        listing = Texts(listing, "  ", style.command, name, style.reset);
        // A pure namespace has no summary of its own to show:
        const char *summary = child->summary ? child->summary : (child->num_children > 0 ? "<command> ..." : NULL);
        if (summary) {
            for (int64_t pad = name.length; pad < width; pad++)
                listing = Texts(listing, " ");
            listing = Texts(listing, "  ", style.italic, summary, style.reset);
        }
        listing = Texts(listing, "\n");
    }
    return listing;
}

// Fill in a command's usage/help (and its children's, recursively). `prefix`
// is how the parent is invoked, so each command can name itself in full.
// Anything set explicitly by the program is left alone.
static void materialize_command(cli_spec_t *cli, cli_command_t *command, Text_t prefix) {
    cli_style_t style = tomo_cli_style();
    // The full invocation of this command, e.g. "git submodule init":
    Text_t invocation = command->name ? Texts(prefix, " ", command->name) : prefix;

    if (command->usage.length == 0) {
        Text_t subcommand_form = Texts(" ", style.bold, "<command>", style.reset, " ...");
        if (command->handler)
            command->usage = tomo_generate_usage(invocation, command->spec_len, command->spec);
        else command->usage = Texts(style.usage, "Usage:", style.reset, " ", invocation, subcommand_form);
        if (command->handler && command->num_children > 0)
            // The spaces go outside the style, or they get underlined too:
            command->usage =
                Texts(command->usage, "\n   ", style.usage, "or:", style.reset, " ", invocation, subcommand_form);
    }

    if (command->help.length == 0) {
        Text_t help = Texts(style.bold, invocation, style.reset);
        if (command->summary) help = Texts(help, ": ", command->summary);
        help = Texts(help, "\n\n", command->usage);
        if (command->description) help = Texts(help, "\n\n", command->description);

        Text_t args_text = EMPTY_TEXT, flags_text = EMPTY_TEXT;
        for (int i = 0; i < command->spec_len; i++) {
            if (command->spec[i].positional) args_text = Texts(args_text, arg_help_line(&command->spec[i]));
            else flags_text = Texts(flags_text, arg_help_line(&command->spec[i]));
        }
        if (args_text.length > 0)
            help = Texts(help, "\n\n", style.heading, "Arguments:", style.reset, "\n",
                         Text$trim(args_text, Text("\n"), false, true));
        if (flags_text.length > 0)
            help = Texts(help, "\n\n", style.heading, "Flags:", style.reset, "\n",
                         Text$trim(flags_text, Text("\n"), false, true));
        // Trimmed like the sections above, so each section is separated from
        // the next by exactly one blank line:
        if (command->num_children > 0)
            help = Texts(help, "\n", Text$trim(commands_listing(command), Text("\n"), false, true));
        // Global flags apply at every level, so document them everywhere:
        if (cli->global_len > 0) {
            Text_t globals = EMPTY_TEXT;
            for (int i = 0; i < cli->global_len; i++)
                globals = Texts(globals, arg_help_line(&cli->global_spec[i]));
            help = Texts(help, "\n\n", style.heading, "Global flags", style.reset,
                         " (valid anywhere on the command line):\n", Text$trim(globals, Text("\n"), false, true));
        }
        command->help = help;
    }

    for (int i = 0; i < command->num_children; i++)
        materialize_command(cli, command->children[i], invocation);
}

static void materialize_help_text(const char *prog, cli_spec_t *cli) {
    cli->root.name = NULL; // the root is the program itself
    if (cli->root.summary == NULL) cli->root.summary = cli->summary;
    if (cli->root.description == NULL) cli->root.description = cli->description;
    materialize_command(cli, &cli->root, Text$from_str(prog));
}

static cli_command_t *find_child(cli_command_t *command, const char *name) {
    for (int i = 0; i < command->num_children; i++) {
        if (streq(name, command->children[i]->name)) return command->children[i];
    }
    return NULL;
}

// The closest of a command's subcommand names to `word`, if any is close
// enough to be worth suggesting:
static OptionalText_t nearest_command(cli_command_t *command, const char *word) {
    List_t names = EMPTY_LIST;
    for (int i = 0; i < command->num_children; i++) {
        Text_t name = Text$from_str(command->children[i]->name);
        List$insert(&names, &name, I(0), sizeof(Text_t));
    }
    return Text$nearest(Text$from_str(word), names, NUMBER_SMALL(3, 5) /* 0.6 */);
}

// The short alias to use for an automatic --help/--version flag, or 0 when the
// command (or a global flag) already claims that letter for something else:
// `-v` is far more often a program's own "verbose" than "version", and the
// automatic flags are popped before the command's own spec is parsed, so
// without this they would shadow it.
static char unclaimed_short_flag(cli_spec_t *cli, cli_command_t *command, char flag) {
    if (!flag) return 0;
    for (int i = 0; i < command->spec_len; i++)
        if (command->spec[i].short_flag == flag) return 0;
    for (int i = 0; i < cli->global_len; i++)
        if (cli->global_spec[i].short_flag == flag) return 0;
    return flag;
}

// Same, for the long name: a program that declares its own `help` or `version`
// argument means that one, so the automatic flag has to step aside entirely
// rather than answer (and exit) before the command's spec is ever parsed.
static bool claims_long_flag(cli_spec_t *cli, cli_command_t *command, const char *name) {
    for (int i = 0; i < command->spec_len; i++)
        if (!command->spec[i].positional && streq(command->spec[i].name, name)) return true;
    for (int i = 0; i < cli->global_len; i++)
        if (streq(cli->global_spec[i].name, name)) return true;
    return false;
}

// Report a first word that names neither a child command nor anything this
// command can do, suggesting the closest command name:
static int unrecognized_command(cli_command_t *command, const char *word) {
    OptionalText_t nearest = nearest_command(command, word);
    cli_style_t style = tomo_cli_style();
    fprint(stderr, style.error, "Unrecognized command: ", word, style.reset,
           nearest.tag == TEXT_NONE ? EMPTY_TEXT
                                    : Texts("\nDid you mean ", style.bold, nearest, style.reset, "?"),
           "\n", command->help);
    return 1;
}

// Walk the command tree: descend into the longest matching command path, then
// parse the remaining arguments against whatever command we landed on.
static int dispatch_into(cli_spec_t *cli, cli_command_t *command, List_t head, List_t extra_args) {
    const char *word = head.length > 0 ? *(const char **)head.data : NULL;
    if (word) {
        cli_command_t *child = find_child(command, word);
        if (child) {
            List$remove_at(&head, I(1), I(1), sizeof(const char *)); // drop the command name itself
            return dispatch_into(cli, child, head, extra_args);
        }
    }

    if (!command->handler) {
        // A namespace with nothing to run itself:
        if (word) return unrecognized_command(command, word);
        fprint(stderr, command->help);
        return 1;
    }

    // If this command has subcommands and the first word resembles one, say so
    // when parsing fails. `tomo bulid file.tm` parses "bulid" as the file to
    // run and then chokes on the leftover "file.tm"; the useful thing to say
    // is that `bulid` looks like a typo for `build`. Held back until the parse
    // actually fails, so `tomo tests` still runs a directory called `tests`
    // even though it's one edit from the `test` command.
    Text_t command_hint = EMPTY_TEXT;
    cli_style_t style = tomo_cli_style();
    if (word && command->num_children > 0) {
        OptionalText_t nearest = nearest_command(command, word);
        if (nearest.tag != TEXT_NONE)
            command_hint = Texts("\n", style.bold, word, style.reset, " isn't a command -- did you mean ", style.bold,
                                 nearest, style.reset, "?");
    }

    cli_help_info_t info = {.usage = command->usage,
                            .help = command->help,
                            .help_short = 'h',
                            .strict_positionals = cli->strict_positionals,
                            .command_hint = command_hint};
    tomo_parse_arg_list(head, info, command->spec_len, command->spec);
    return command->handler(command, extra_args);
}

public
int tomo_dispatch_command(int argc, char *argv[], cli_spec_t *cli) {
    materialize_help_text(cli->name ? cli->name : argv[0], cli);

    List_t args = EMPTY_LIST;
    for (int i = 1; i < argc; i++) {
        List$insert(&args, &argv[i], I(0), sizeof(const char *));
    }

    // Split at the first bare "--": everything after it is passed through to
    // the handler raw (e.g. the arguments for the program `run` executes).
    // Programs that don't relay arguments leave "--" alone, so the argument
    // parser can give it its usual "the rest are values" meaning:
    List_t head = args, extra_args = EMPTY_LIST;
    if (cli->passthrough_after_double_dash) {
        for (int64_t i = 0; i < (int64_t)args.length; i++) {
            const char *arg = *(const char **)(args.data + i * args.stride);
            if (streq(arg, "--")) {
                head = List$slice(args, I(1), I(i));
                extra_args = List$slice(args, I(i + 2), I(-1));
                break;
            }
        }
    }

    // Global flags are valid anywhere on the command line (before or after
    // the command name), so they all get popped before dispatching. This is
    // why command-specific flag names must not collide with global ones: the
    // global pop would steal them.
    for (int i = 0; i < cli->global_len; i++) {
        cli_arg_t *flag = &cli->global_spec[i];
        flag->populated = pop_cli_flag(&head, flag->short_flag, flag->name, flag->dest, flag->type);
    }

    // --help/--version are answered for whichever command was named, so find
    // it (without consuming anything) before looking for them:
    cli_command_t *named = &cli->root;
    for (int64_t i = 0; i < (int64_t)head.length; i++) {
        cli_command_t *child = find_child(named, *(const char **)(head.data + i * head.stride));
        if (!child) break;
        named = child;
    }

    if (!claims_long_flag(cli, named, "help")) {
        bool show_help = false;
        if (pop_boolean_cli_flag(&head, unclaimed_short_flag(cli, named, 'h'), "help", &show_help) && show_help) {
            print(named->help);
            return 0;
        }
    }
    if (cli->version && !claims_long_flag(cli, named, "version")) {
        bool show_version = false;
        if (pop_boolean_cli_flag(&head, unclaimed_short_flag(cli, named, cli->version_short), "version", &show_version)
            && show_version) {
            print(cli->version);
            return 0;
        }
    }

    if (cli->after_globals) cli->after_globals();

    return dispatch_into(cli, &cli->root, head, extra_args);
}

static List_t parse_arg_list(List_t args, const char *flag, void *dest, const TypeInfo_t *type, bool allow_dashes) {
    if (type->tag == ListInfo) {
        void *item = type->ListInfo.item->size ? GC_MALLOC((size_t)type->ListInfo.item->size) : NULL;
        while (args.length > 0) {
            const char *arg = *(const char **)args.data;
            if (arg[0] == '-' && !allow_dashes) break;
            args = parse_arg_list(args, flag, item, type->ListInfo.item, allow_dashes);
            List$insert(dest, item, I(0), type->ListInfo.item->size);
        }
        return args;
    } else if (type->tag == TableInfo) {
        // Arguments take the form key:value
        void *key = type->TableInfo.key->size ? GC_MALLOC((size_t)type->TableInfo.key->size) : NULL;
        void *value = type->TableInfo.value->size ? GC_MALLOC((size_t)type->TableInfo.value->size) : NULL;
        while (args.length > 0) {
            const char *arg = *(const char **)args.data;
            if (arg[0] == '-' && !allow_dashes) break;
            if (type->TableInfo.value->size == 0) {
                List_t key_arg = List(arg);
                (void)parse_arg_list(key_arg, flag, key, type->TableInfo.key, allow_dashes);
                Table$set(dest, key, NULL, type);
                args = List$from(args, I(2));
            } else {
                const char *colon = strchr(arg, ':');
                if (!colon) break;
                List_t key_arg = List(String(string_slice(arg, (size_t)(colon - arg))));
                (void)parse_arg_list(key_arg, flag, key, type->TableInfo.key, allow_dashes);
                List_t value_arg = List(colon + 1);
                (void)parse_arg_list(value_arg, flag, value, type->TableInfo.value, allow_dashes);
                Table$set(dest, key, value, type);
                args = List$from(args, I(2));
            }
        }
        return args;
    } else if (type->tag == StructInfo) {
        for (int i = 0; i < type->StructInfo.num_fields; i++) {
            const TypeInfo_t *field_type = type->StructInfo.fields[i].type;
            if (field_type->align > 0 && (size_t)dest % (size_t)field_type->align > 0)
                dest += (size_t)field_type->align - ((size_t)dest % (size_t)field_type->align);
            args = parse_arg_list(args, String(flag, ".", type->StructInfo.fields[i].name), dest, field_type,
                                  allow_dashes);
            dest += field_type->size;
        }
        return args;
    }

    if (args.length == 0) print_err("No value provided for flag: ", flag);

    const char *arg = *(const char **)args.data;

    if (!allow_dashes) {
        if ((type->tag == TextInfo || type == &CString$info) && arg[0] == '\\' && arg[1] == '-') {
            arg = arg + 1;
        } else if (arg[0] == '-') {
            print_err("Not a valid flag: ", arg);
        }
    }

    if (type->tag == OptionalInfo) {
        const TypeInfo_t *nonnull = type->OptionalInfo.type;
        if (streq(arg, "none")) {
            if (nonnull == &Float64$info) *(double *)dest = (double)NAN;
            else if (nonnull == &Float32$info) *(float *)dest = (float)NAN;
            else memset(dest, 0, (size_t)type->size);
            return List$from(args, I(2));
        } else {
            args = parse_arg_list(args, flag, dest, nonnull, allow_dashes);
            if (nonnull == &Int$info || nonnull == &Path$info || nonnull == &Num$info || nonnull == &Float64$info
                || nonnull == &Float32$info || nonnull->tag == TextInfo || nonnull->tag == EnumInfo)
                return args;
            else if (nonnull == &Int64$info) ((OptionalInt64_t *)dest)->has_value = true;
            else if (nonnull == &Int32$info) ((OptionalInt32_t *)dest)->has_value = true;
            else if (nonnull == &Int16$info) ((OptionalInt16_t *)dest)->has_value = true;
            else if (nonnull == &Int8$info) ((OptionalInt8_t *)dest)->has_value = true;
            else if (nonnull == &Byte$info) ((OptionalByte_t *)dest)->has_value = true;
            else if (nonnull->tag == StructInfo && nonnull != &Path$info) *(bool *)(dest + nonnull->size) = true;
            else print_err("Unsupported type: ", generic_as_text(NULL, USE_COLOR, nonnull));
            return args;
        }
    }

    List_t rest_of_args = List$from(args, I(2));

    if (type == &CString$info) {
        *(const char **)dest = arg;
    } else if (type == &Int$info) {
        OptionalInt_t parsed = Int$from_str(arg);
        if (parsed.small == 0) print_err("Could not parse argument for ", flag, ": ", arg);
        *(Int_t *)dest = parsed;
    } else if (type == &Int64$info) {
        OptionalInt64_t parsed = Int64$parse(Text$from_str(arg), NONE_INT, NULL);
        if (!parsed.has_value) print_err("Could not parse argument for ", flag, ": ", arg);
        *(Int64_t *)dest = parsed.value;
    } else if (type == &Int32$info) {
        OptionalInt32_t parsed = Int32$parse(Text$from_str(arg), NONE_INT, NULL);
        if (!parsed.has_value) print_err("Could not parse argument for ", flag, ": ", arg);
        *(Int32_t *)dest = parsed.value;
    } else if (type == &Int16$info) {
        OptionalInt16_t parsed = Int16$parse(Text$from_str(arg), NONE_INT, NULL);
        if (!parsed.has_value) print_err("Could not parse argument for ", flag, ": ", arg);
        *(Int16_t *)dest = parsed.value;
    } else if (type == &Int8$info) {
        OptionalInt8_t parsed = Int8$parse(Text$from_str(arg), NONE_INT, NULL);
        if (!parsed.has_value) print_err("Could not parse argument for ", flag, ": ", arg);
        *(Int8_t *)dest = parsed.value;
    } else if (type == &Byte$info) {
        OptionalByte_t parsed = Byte$parse(Text$from_str(arg), NONE_INT, NULL);
        if (!parsed.has_value) print_err("Could not parse argument for ", flag, ": ", arg);
        *(Byte_t *)dest = parsed.value;
    } else if (type == &Bool$info) {
        OptionalBool_t parsed = Bool$parse(Text$from_str(arg), NULL);
        if (parsed == NONE_BOOL) print_err("Could not parse argument for ", flag, ": ", arg);
        *(Bool_t *)dest = parsed;
    } else if (type == &Num$info) {
        OptionalNum_t parsed = Num$parse(Text$from_str(arg));
        if (parsed.bits == NONE_NUM.bits) print_err("Could not parse argument for ", flag, ": ", arg);
        *(Num_t *)dest = parsed;
    } else if (type == &Float64$info) {
        OptionalFloat64_t parsed = Float64$parse(Text$from_str(arg), NULL);
        if (isnan(parsed)) print_err("Could not parse argument for ", flag, ": ", arg);
        *(Float64_t *)dest = parsed;
    } else if (type == &Float32$info) {
        OptionalFloat32_t parsed = Float32$parse(Text$from_str(arg), NULL);
        if (isnan(parsed)) print_err("Could not parse argument for ", flag, ": ", arg);
        *(Float32_t *)dest = parsed;
    } else if (type->tag == PointerInfo) {
        // For pointers, we can just allocate memory for the value and then parse the value
        void *value = GC_MALLOC((size_t)type->PointerInfo.pointed->size);
        args = parse_arg_list(args, flag, value, type->PointerInfo.pointed, allow_dashes);
        *(void **)dest = value;
        return args;
    } else if (type == &Path$info) {
        *(Path_t *)dest = Path$from_str(arg);
    } else if (type->tag == TextInfo) {
        *(Text_t *)dest = Text$from_str(arg);
    } else if (type->tag == EnumInfo) {
        List_t tag_names = EMPTY_LIST;
        for (int t = 0; t < type->EnumInfo.num_tags; t++) {
            NamedType_t named = type->EnumInfo.tags[t];
            Text_t name_text = Text$from_str(named.name);
            List$insert(&tag_names, &name_text, I(0), sizeof(name_text));
            size_t len = strlen(named.name);
            if (strncmp(arg, named.name, len) == 0 && (arg[len] == '\0' || arg[len] == ':')) {
                *(int32_t *)dest = (t + 1);

                // Simple tag (no associated data):
                if (!named.type || (named.type->tag == StructInfo && named.type->StructInfo.num_fields == 0))
                    return rest_of_args;

                dest += sizeof(int32_t);

                if (named.type->align > 0 && (size_t)dest % (size_t)named.type->align > 0)
                    dest += (size_t)named.type->align - ((size_t)dest % (size_t)named.type->align);

                return parse_arg_list(rest_of_args, String(flag, ".", named.name), dest, named.type, allow_dashes);
            }
        }
        print_err("Invalid enum name for ", type->EnumInfo.name, ": ", arg,
                  "\nValid names are: ", Text$join(Text(", "), tag_names));
    } else {
        Text_t t = generic_as_text(NULL, false, type);
        print_err("Unsupported type for argument parsing: ", t);
    }
    return rest_of_args;
}

bool pop_cli_flag(List_t *args, char short_flag, const char *flag, void *dest, const TypeInfo_t *type) {
    if (type == &Bool$info) {
        return pop_boolean_cli_flag(args, short_flag, flag, dest);
    }

    for (int64_t i = 0; i < (int64_t)args->length; i++) {
        const char *arg = *(const char **)(args->data + i * args->stride);
        if (arg[0] == '-' && arg[1] == '-') {
            if (arg[2] == '\0') {
                // Case: -- (end of flags and beginning of positional args)
                break;
            } else if (streq(arg + 2, flag)) {
                // Case: --flag values...
                if (i + 1 >= (int64_t)args->length) print_err("No value provided for flag: ", flag);
                List_t values = List$slice(*args, I(i + 2), I(-1));
                List_t remaining_args = parse_arg_list(values, flag, dest, type, false);
                *args = List$concat(List$to(*args, I(i)), remaining_args, sizeof(const char *));
                return true;
            } else if (starts_with(arg + 2, flag) && arg[2 + strlen(flag)] == '=') {
                // Case: --flag=...
                const char *arg_value = arg + 2 + strlen(flag) + 1;
                List_t values;
                if (type->tag == ListInfo || type->tag == TableInfo) {
                    // For lists and tables, --flag=a,b,c or --flag=a:1,b:2,c:3
                    List_t texts = Text$split(Text$from_str(arg_value), Text(","));
                    values = EMPTY_LIST;
                    for (int64_t j = 0; j < (int64_t)texts.length; j++)
                        List$insert_value(&values, Text$as_c_string(*(Text_t *)(texts.data + j * texts.stride)), I(0),
                                          sizeof(const char *));
                } else {
                    values = List(arg_value);
                }
                (void)parse_arg_list(values, flag, dest, type, false);
                // The value is inside this token, so only the token is
                // consumed; everything after it is still to be parsed:
                List$remove_at(args, I(i + 1), I(1), sizeof(const char *));
                return true;
            }
        } else if (short_flag && arg[0] == '-' && arg[1] != '-' && strchr(arg + 1, short_flag)) {
            const char *loc = strchr(arg + 1, short_flag);
            char short_str[2] = {short_flag, '\0'};
            if (loc[1] == '=') {
                // Case: -f=...
                const char *arg_value = loc + 2;
                List_t values;
                if (type->tag == ListInfo || type->tag == TableInfo) {
                    // For lists and tables, -f=a,b,c or -f=a:1,b:2,c:3
                    List_t texts = Text$split(Text$from_str(arg_value), Text(","));
                    values = EMPTY_LIST;
                    for (int64_t j = 0; j < (int64_t)texts.length; j++)
                        List$insert_value(&values, Text$as_c_string(*(Text_t *)(texts.data + j * texts.stride)), I(0),
                                          sizeof(const char *));
                } else {
                    // Case: -f=value
                    values = List(arg_value);
                }
                values = parse_arg_list(values, flag, dest, type, false);

                if (loc > arg + 1) {
                    // Case: -abcdef=... -> -abcde
                    char *remainder = String(string_slice(arg, (size_t)(loc - arg)));
                    if unlikely (args->data_refcount > 0) List$compact(args, sizeof(const char *));
                    *(const char **)(args->data + i * args->stride) = remainder;
                } else {
                    // Case: -f=... -> pop flag entirely
                    List$remove_at(args, I(i + 1), I(1), sizeof(const char *));
                }
                return true;
            } else if (loc[1] == '\0') {
                // Case: -...f value...
                if (i + 1 >= (int64_t)args->length) print_err("No value provided for flag: -", short_str);
                List_t values = List$slice(*args, I(i + 2), I(-1));
                List_t remaining_values = parse_arg_list(values, flag, dest, type, false);
                if (loc == arg + 1) {
                    // Case: -f values...
                    *args = List$concat(List$to(*args, I(i)), remaining_values, sizeof(const char *));
                } else {
                    // Case: -abcdef values... -> -abcde
                    char *remainder = String(string_slice(arg, (size_t)(loc - arg)));
                    if unlikely (args->data_refcount > 0) List$compact(args, sizeof(const char *));
                    *args = List$concat(List$to(*args, I(i)),
                                        List$concat(List(remainder), remaining_values, sizeof(const char *)),
                                        sizeof(const char *));
                }
                return true;
            } else {
                // Case: -...fVALUE (e.g. -O3)
                const char *arg_value = loc + 1;
                List_t values;
                if (type->tag == ListInfo || type->tag == TableInfo) {
                    // For lists and tables, -fa,b,c or -fa:1,b:2,c:3
                    List_t texts = Text$split(Text$from_str(arg_value), Text(","));
                    values = EMPTY_LIST;
                    for (int64_t j = 0; j < (int64_t)texts.length; j++)
                        List$insert_value(&values, Text$as_c_string(*(Text_t *)(texts.data + j * texts.stride)), I(0),
                                          sizeof(const char *));
                } else {
                    // Case: -fVALUE
                    values = List(arg_value);
                }
                (void)parse_arg_list(values, flag, dest, type, false);
                if (loc > arg + 1) {
                    // Case: -abcdefVALUE -> -abcde;
                    // NOTE: adding a semicolon means that `-ab1 2` won't parse as b=1, then a=2
                    char *remainder = String(string_slice(arg, (size_t)(loc - arg)), ";");
                    if unlikely (args->data_refcount > 0) List$compact(args, sizeof(const char *));
                    *(const char **)(args->data + i * args->stride) = remainder;
                } else {
                    // Case: -fVALUE -> pop flag entirely
                    List$remove_at(args, I(i + 1), I(1), sizeof(const char *));
                }
                return true;
            }
        }
    }
    return false;
}

bool pop_cli_positional(List_t *args, const char *flag, void *dest, const TypeInfo_t *type, bool allow_dashes) {
    if (args->length == 0) {
        print_err("No value provided for flag: ", flag);
        return false;
    }
    *args = parse_arg_list(*args, flag, dest, type, allow_dashes);
    return true;
}
