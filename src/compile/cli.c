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

static Text_t get_flag_options(type_t *t, const char *separator) {
    if (t->tag == BoolType) {
        return Text("yes|no");
    } else if (t->tag == EnumType) {
        Text_t options = EMPTY_TEXT;
        for (tag_t *tag = Match(t, EnumType)->tags; tag; tag = tag->next) {
            options = Texts(options, tag->name);
            if (tag->next) options = Texts(options, separator);
        }
        return options;
    } else if (t->tag == StructType) {
        Text_t options = EMPTY_TEXT;
        for (arg_t *field = Match(t, StructType)->fields; field; field = field->next) {
            options = Texts(options, get_flag_options(field->type, separator));
            if (field->next) options = Texts(options, separator);
        }
        return options;
    } else if (is_numeric_type(t)) {
        return Text("N");
    } else if (t->tag == TextType || t->tag == CStringType) {
        return Text("text");
    } else if (t->tag == ListType || (t->tag == TableType && Match(t, TableType)->value_type == EMPTY_TYPE)) {
        return Text("value1 value2...");
    } else if (t->tag == TableType) {
        return Text("key1:value1 key2:value2...");
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

        Text_t usage = explicit_help_flag ? EMPTY_TEXT : Text(" [--help]");
        for (arg_t *arg = fn_info->args; arg; arg = arg->next) {
            usage = Texts(usage, " ");
            type_t *t = get_arg_type(main_env, arg);
            if (arg->default_val) {
                OptionalText_t flag = flagify(arg->name, true);
                assert(flag.tag != TEXT_NONE);
                OptionalText_t alias_flag = flagify(arg->alias, true);
                Text_t flags = alias_flag.tag != TEXT_NONE ? Texts(flag, "|", alias_flag) : flag;
                if (t->tag == BoolType || (t->tag == OptionalType && Match(t, OptionalType)->type->tag == BoolType))
                    usage = Texts(usage, "[", flags, "]");
                else if (t->tag == ListType) usage = Texts(usage, "[", flags, " ", get_flag_options(t, "|"), "]");
                else usage = Texts(usage, "[", flags, "=", get_flag_options(t, "|"), "]");
            } else {
                OptionalText_t flag = flagify(arg->name, false);
                assert(flag.tag != TEXT_NONE);
                OptionalText_t alias_flag = flagify(arg->alias, true);
                if (t->tag == BoolType)
                    usage = Texts(usage, "<--", flag, alias_flag.tag != TEXT_NONE ? Texts("|", alias_flag) : EMPTY_TEXT,
                                  "|--no-", flag, ">");
                else if (t->tag == EnumType) usage = Texts(usage, get_flag_options(t, "|"));
                else if (t->tag == ListType) usage = Texts(usage, "[", flag, "...]");
                else usage = Texts(usage, "<", flag, ">");
            }
        }
        code = Texts(code,
                     "Text_t usage = Texts(Text(\"\\x1b[1mUsage:\\x1b[m \"), "
                     "Text$from_str(argv[0])",
                     usage.length == 0 ? EMPTY_TEXT : Texts(", Text(", quoted_text(usage), ")"), ");\n");
    }
    if (!help_binding) {
        Text_t help_text = EMPTY_TEXT;
        for (arg_t *arg = fn_info->args; arg; arg = arg->next) {
            help_text = Texts(help_text, "\n");
            type_t *t = get_arg_type(main_env, arg);
            OptionalText_t flag = flagify(arg->name, true);
            assert(flag.tag != TEXT_NONE);
            OptionalText_t alias_flag = flagify(arg->alias, true);
            Text_t flags = alias_flag.tag != TEXT_NONE ? Texts(flag, "|", alias_flag) : flag;
            if (t->tag == BoolType || (t->tag == OptionalType && Match(t, OptionalType)->type->tag == BoolType))
                help_text = Texts(help_text, "  \x1b[1m", flags, "|--no-", flag, "\x1b[m");
            else help_text = Texts(help_text, "  \x1b[1m", flags, " \x1b[34m", get_flag_options(t, "|"), "\x1b[m");
            if (arg->default_val) {
                Text_t default_text =
                    Text$from_strn(arg->default_val->start, (size_t)(arg->default_val->end - arg->default_val->start));
                help_text = Texts(help_text, " \x1b[2mdefault:", default_text, "\x1b[m");
            }
            if (arg->comment.length > 0) help_text = Texts(help_text, " \x1b[3m", arg->comment, "\x1b[m");
        }
        code = Texts(code, "Text_t help = Texts(usage, ", quoted_text(help_text), ");\n");
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
                     arg->alias ? Texts(", .short_flag=", quoted_text(Text$from_str(arg->name)),
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
        man = Texts(man, "\n.TP\n\\f[B]\\-\\-", Text$from_str(arg->name), "\\f[R]");
        if (arg->alias) man = Texts(man, ", \\f[B]\\-", Text$from_str(arg->alias), "\\f[R]");
        if (arg->type->tag == BoolType) {
            man = Texts(man, "\n.TP\n\\f[B]\\-\\-no\\-", Text$from_str(arg->name));
        } else if (is_numeric_type(arg->type)) {
            man = Texts(man, " \\f[I]N\\f[R]");
        } else if (arg->type->tag == ListType) {
            man = Texts(man, " \\f[I]value1\\f[R] \\f[I]value2...\\f[R]");
        } else if (arg->type->tag == TableType) {
            man = Texts(man, " \\f[I]key1:value1\\f[R] \\f[I]key2:value2...\\f[R]");
        } else {
            man = Texts(man, " \\f[I]value\\f[R]");
        }

        if (arg->comment.length > 0) {
            man = Texts(man, "\n", arg->comment);
        }
    }

    return man;
}
