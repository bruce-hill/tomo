// This file defines how to compile CLI argument parsing

#include "../stdlib/cli.h"
#include "../environment.h"
#include "../stdlib/datatypes.h"
#include "../stdlib/optionals.h"
#include "../stdlib/text.h"
#include "../stdlib/util.h"
#include "../typecheck.h"
#include "../types.h"
#include "compilation.h"

static Text_t get_flag_options(type_t *t, Text_t separator) {
    if (t->tag == BoolType) {
        return Text("yes|no");
    } else if (t == PATH_TYPE) {
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
    } else if (t->tag == TableType && Match(t, TableType)->value_type == EMPTY_TYPE) {
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

static OptionalText_t flagify(const char *name, bool prefix) {
    if (!name) return NONE_TEXT;
    Text_t flag = Text$from_str(name);
    flag = Text$replace(flag, Text("_"), Text("-"));
    if (prefix) flag = flag.length == 1 ? Texts("-", flag) : Texts("--", flag);
    return flag;
}

public
Text_t compile_cli_arg_call(env_t *env, Text_t fn_name, type_t *fn_type, const char *version) {
    DeclareMatch(fn_info, fn_type, FunctionType);

    env_t *main_env = fresh_scope(env);

    Text_t code = EMPTY_TEXT;
    binding_t *usage_binding = get_binding(env, "_USAGE");
    Text_t usage_code = usage_binding ? usage_binding->code : Text("usage");
    binding_t *help_binding = get_binding(env, "_HELP");
    Text_t help_code = help_binding ? help_binding->code : usage_code;
    if (!usage_binding) {
        bool explicit_help_flag = false;
        for (arg_t *arg = fn_info->args; arg; arg = arg->next) {
            if (streq(arg->name, "help")) {
                explicit_help_flag = true;
                break;
            }
        }

        Text_t usage = explicit_help_flag ? EMPTY_TEXT : Text(" [\x1b[1m--help\x1b[m]");
        for (arg_t *arg = fn_info->args; arg; arg = arg->next) {
            usage = Texts(usage, " ");
            type_t *t = get_arg_type(main_env, arg);
            OptionalText_t flag = flagify(arg->name, arg->default_val != NULL);
            assert(flag.tag != TEXT_NONE);
            OptionalText_t alias_flag = flagify(arg->alias, arg->default_val != NULL);
            Text_t flags = Texts("\x1b[1m", flag, "\x1b[m");
            if (alias_flag.tag != TEXT_NONE) flags = Texts(flags, ",\x1b[1m", alias_flag, "\x1b[m");
            if (t->tag == BoolType || (t->tag == OptionalType && Match(t, OptionalType)->type->tag == BoolType))
                flags = Texts(flags, "|\x1b[1m--no-", Text$without_prefix(flag, Text("--")), "\x1b[m");
            if (arg->default_val || value_type(t)->tag == BoolType) {
                if (t->tag == BoolType || (t->tag == OptionalType && Match(t, OptionalType)->type->tag == BoolType))
                    usage = Texts(usage, "[", flags, "]");
                else if (t->tag == ListType) usage = Texts(usage, "[", flags, " ", get_flag_options(t, Text("|")), "]");
                else usage = Texts(usage, "[", flags, " ", get_flag_options(t, Text("|")), "]");
            } else {
                usage = Texts(usage, "\x1b[1m", get_flag_options(t, Text("|")), "\x1b[m");
            }
        }
        code = Texts(code,
                     "Text_t usage = Texts(Text(\"\\x1b[1mUsage:\\x1b[m \"), "
                     "Text$from_str(argv[0])",
                     usage.length == 0 ? EMPTY_TEXT : Texts(", Text(", quoted_text(usage), ")"), ");\n");
    }
    if (!help_binding) {
        Text_t help_text = fn_info->args ? Text("\n") : Text("\n\n\x1b[2;3m  No arguments...\x1b[m");

        for (arg_t *arg = fn_info->args; arg; arg = arg->next) {
            help_text = Texts(help_text, "\n");
            type_t *t = get_arg_type(main_env, arg);
            OptionalText_t flag = flagify(arg->name, true);
            assert(flag.tag != TEXT_NONE);
            OptionalText_t alias_flag = flagify(arg->alias, true);
            Text_t flags = Texts("\x1b[33;1m", flag, "\x1b[m");
            if (alias_flag.tag != TEXT_NONE) flags = Texts(flags, ",\x1b[33;1m", alias_flag, "\x1b[m");
            if (t->tag == BoolType || (t->tag == OptionalType && Match(t, OptionalType)->type->tag == BoolType))
                flags = Texts(flags, "|\x1b[33;1m--no-", Text$without_prefix(flag, Text("--")), "\x1b[m");
            if (t->tag == BoolType || (t->tag == OptionalType && Match(t, OptionalType)->type->tag == BoolType))
                help_text = Texts(help_text, "  ", flags);
            else
                help_text = Texts(help_text, "  ", flags, " \x1b[1;34m",
                                  get_flag_options(t, Text("\x1b[m | \x1b[1;34m")), "\x1b[m");

            if (arg->comment.length > 0) help_text = Texts(help_text, " \x1b[3m", arg->comment, "\x1b[m");
            if (arg->default_val) {
                Text_t default_text =
                    Text$from_strn(arg->default_val->start, (size_t)(arg->default_val->end - arg->default_val->start));
                help_text = Texts(help_text, " \x1b[2m(default:", default_text, ")\x1b[m");
            }
        }
        code = Texts(code, "Text_t help = Texts(usage, ", quoted_text(Texts(help_text, "\n")), ");\n");
        help_code = Text("help");
    }

    for (arg_t *arg = fn_info->args; arg; arg = arg->next) {
        code = Texts(code, compile_declaration(arg->type, Texts("_$", Text$from_str(arg->name))), " = ",
                     compile_empty(arg->type), ";\n");
    }

    Text_t version_code = quoted_str(version);
    code = Texts(code, "cli_arg_t cli_args[] = {\n");
    for (arg_t *arg = fn_info->args; arg; arg = arg->next) {
        code = Texts(code, "{", quoted_text(Text$replace(Text$from_str(arg->name), Text("_"), Text("-"))), ", &",
                     Texts("_$", Text$from_str(arg->name)), ", ", compile_type_info(arg->type),
                     arg->default_val ? Text("") : Text(", .required=true"),
                     arg->alias ? Texts(", .short_flag=", quoted_text(Text$from_str(arg->alias)),
                                        "[0]") // TODO: escape char properly
                                : Text(""),

                     "},\n");
    }
    code = Texts(code, "};\n");
    code = Texts(code, "tomo_parse_args(argc, argv, ", usage_code, ", ", help_code, ", ", version_code,
                 ", sizeof(cli_args)/sizeof(cli_args[0]), cli_args);\n");

    // Lazily initialize default values to prevent side effects
    int64_t i = 0;
    for (arg_t *arg = fn_info->args; arg; arg = arg->next) {
        if (arg->default_val) {
            Text_t default_val;
            if (arg->type) {
                default_val = compile_to_type(env, arg->default_val, arg->type);
                if (arg->type->tag != OptionalType) default_val = promote_to_optional(arg->type, default_val);
            } else {
                default_val = compile(env, arg->default_val);
            }
            code = Texts(code, "if (!cli_args[", i, "].populated) ", Texts("_$", Text$from_str(arg->name)), " = ",
                         default_val, ";\n");
        }
        i += 1;
    }

    code = Texts(code, fn_name, "(");
    for (arg_t *arg = fn_info->args; arg; arg = arg->next) {
        code = Texts(code, Texts("_$", arg->name));
        if (arg->next) code = Texts(code, ", ");
    }
    code = Texts(code, ");\n");
    return code;
}

public
Text_t compile_manpage(Text_t program, OptionalText_t synopsis, OptionalText_t description, arg_t *args) {
    Text_t date = Text(""); // TODO: use date
    Text_t man = Texts(".\\\" Automatically generated by Tomo\n"
                       ".TH \"",
                       Text$upper(program, Text("C")), "\" \"1\" \"", date,
                       "\" \"\" \"\"\n"
                       ".SH NAME\n",
                       program, " \\- ", synopsis.tag == TEXT_NONE ? Text("a Tomo program") : synopsis, "\n");

    if (description.tag != TEXT_NONE) {
        man = Texts(man, ".SH DESCRIPTION\n", description, "\n");
    }

    man = Texts(man, ".SH OPTIONS\n");
    for (arg_t *arg = args; arg; arg = arg->next) {
        OptionalText_t flag = flagify(arg->name, true);
        assert(flag.tag != TEXT_NONE);
        Text_t flags = Texts("\\f[B]", flag, "\\f[R]");
        if (arg->alias) flags = Texts(flags, ", \\f[B]", flagify(arg->alias, true), "\\f[R]");
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
