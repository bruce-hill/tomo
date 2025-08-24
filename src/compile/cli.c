#include "../compile.h"
#include "../environment.h"
#include "../stdlib/datatypes.h"
#include "../stdlib/text.h"
#include "../stdlib/util.h"
#include "../typecheck.h"
#include "../types.h"
#include "assignments.h"
#include "optionals.h"
#include "promotions.h"
#include "text.h"
#include "types.h"

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
    } else if (t->tag == IntType || t->tag == NumType || t->tag == BigIntType) {
        return Text("N");
    } else {
        return Text("...");
    }
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
            Text_t flag = Text$replace(Text$from_str(arg->name), Text("_"), Text("-"));
            if (arg->default_val || arg->type->tag == OptionalType) {
                if (strlen(arg->name) == 1) {
                    if (t->tag == BoolType || (t->tag == OptionalType && Match(t, OptionalType)->type->tag == BoolType))
                        usage = Texts(usage, "[-", flag, "]");
                    else usage = Texts(usage, "[-", flag, " ", get_flag_options(t, "|"), "]");
                } else {
                    if (t->tag == BoolType || (t->tag == OptionalType && Match(t, OptionalType)->type->tag == BoolType))
                        usage = Texts(usage, "[--", flag, "]");
                    else if (t->tag == ListType) usage = Texts(usage, "[--", flag, " ", get_flag_options(t, "|"), "]");
                    else usage = Texts(usage, "[--", flag, "=", get_flag_options(t, "|"), "]");
                }
            } else {
                if (t->tag == BoolType) usage = Texts(usage, "<--", flag, "|--no-", flag, ">");
                else if (t->tag == EnumType) usage = Texts(usage, get_flag_options(t, "|"));
                else if (t->tag == ListType) usage = Texts(usage, "[", flag, "...]");
                else usage = Texts(usage, "<", flag, ">");
            }
        }
        code = Texts(code,
                     "Text_t usage = Texts(Text(\"Usage: \"), "
                     "Text$from_str(argv[0])",
                     usage.length == 0 ? EMPTY_TEXT : Texts(", Text(", quoted_text(usage), ")"), ");\n");
    }

    for (arg_t *arg = fn_info->args; arg; arg = arg->next) {
        type_t *opt_type = arg->type->tag == OptionalType ? arg->type : Type(OptionalType, .type = arg->type);
        code = Texts(code, compile_declaration(opt_type, Texts("_$", arg->name)));
        if (arg->default_val) {
            Text_t default_val = compile(env, arg->default_val);
            if (arg->type->tag != OptionalType) default_val = promote_to_optional(arg->type, default_val);
            code = Texts(code, " = ", default_val);
        } else {
            code = Texts(code, " = ", compile_none(arg->type));
        }
        code = Texts(code, ";\n");
    }

    Text_t version_code = quoted_str(version);
    code = Texts(code, "tomo_parse_args(argc, argv, ", usage_code, ", ", help_code, ", ", version_code);
    for (arg_t *arg = fn_info->args; arg; arg = arg->next) {
        code = Texts(code, ",\n{", quoted_text(Text$replace(Text$from_str(arg->name), Text("_"), Text("-"))), ", ",
                     (arg->default_val || arg->type->tag == OptionalType) ? "false" : "true", ", ",
                     compile_type_info(arg->type), ", &", Texts("_$", arg->name), "}");
    }
    code = Texts(code, ");\n");

    code = Texts(code, fn_name, "(");
    for (arg_t *arg = fn_info->args; arg; arg = arg->next) {
        Text_t arg_code = Texts("_$", arg->name);
        if (arg->type->tag != OptionalType) arg_code = optional_into_nonnone(arg->type, arg_code);

        code = Texts(code, arg_code);
        if (arg->next) code = Texts(code, ", ");
    }
    code = Texts(code, ");\n");
    return code;
}
