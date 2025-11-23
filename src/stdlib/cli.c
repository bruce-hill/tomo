// Comman-line argument parsing

#include <execinfo.h>
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
#include "nums.h"
#include "optionals.h"
#include "paths.h"
#include "print.h"
#include "stdlib.h"
#include "tables.h"
#include "text.h"
#include "util.h"

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
            char *loc = strchr(arg + 1, short_flag);
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
void tomo_parse_args(int argc, char *argv[], Text_t usage, Text_t help, const char *version, int spec_len,
                     cli_arg_t spec[spec_len]) {
    List_t args = EMPTY_LIST;
    for (int i = 1; i < argc; i++) {
        List$insert(&args, &argv[i], I(0), sizeof(const char *));
    }

    for (int i = 0; i < spec_len; i++) {
        spec[i].populated = pop_cli_flag(&args, spec[i].short_flag, spec[i].name, spec[i].dest, spec[i].type);
    }

    bool show_help = false;
    if (pop_boolean_cli_flag(&args, 'h', "help", &show_help) && show_help) {
        print(help);
        exit(0);
    }
    bool show_version = false;
    if (pop_boolean_cli_flag(&args, 'v', "version", &show_version) && show_version) {
        print(version);
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
        if (!spec[i].populated) {
            spec[i].populated =
                pop_cli_positional(&before_double_dash, spec[i].name, spec[i].dest, spec[i].type, false);
        }
    }

    for (int i = 0; i < spec_len && after_double_dash.length > 0; i++) {
        if (!spec[i].populated) {
            spec[i].populated = pop_cli_positional(&after_double_dash, spec[i].name, spec[i].dest, spec[i].type, true);
        }
    }

    for (int i = 0; i < spec_len; i++) {
        if (!spec[i].populated && spec[i].required) print_err("Missing required flag: ", spec[i].name, "\n", usage);
    }

    List_t remaining_args = List$concat(before_double_dash, after_double_dash, sizeof(const char *));
    if (remaining_args.length > 0) {
        print_err("Unknown flag values: ", CString$join(" ", remaining_args));
    }
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
            print_err("Not a valid argument for flag ", flag, ": ", arg);
        }
    }

    if (type->tag == OptionalInfo) {
        const TypeInfo_t *nonnull = type->OptionalInfo.type;
        if (streq(arg, "none")) {
            if (nonnull == &Num$info) *(double *)dest = (double)NAN;
            else if (nonnull == &Num32$info) *(float *)dest = (float)NAN;
            else memset(dest, 0, (size_t)type->size);
            return List$from(args, I(2));
        } else {
            args = parse_arg_list(args, flag, dest, nonnull, allow_dashes);
            if (nonnull == &Int$info || nonnull == &Path$info || nonnull == &Num$info || nonnull == &Num32$info
                || nonnull->tag == TextInfo || nonnull->tag == EnumInfo)
                return args;
            else if (nonnull == &Int64$info) ((OptionalInt64_t *)dest)->has_value = true;
            else if (nonnull == &Int32$info) ((OptionalInt32_t *)dest)->has_value = true;
            else if (nonnull == &Int16$info) ((OptionalInt16_t *)dest)->has_value = true;
            else if (nonnull == &Int8$info) ((OptionalInt8_t *)dest)->has_value = true;
            else if (nonnull == &Byte$info) ((OptionalByte_t *)dest)->has_value = true;
            else if (nonnull->tag == StructInfo && nonnull != &Path$info) *(bool *)(dest + nonnull->size) = true;
            else print_err("Unsupported type: ", generic_as_text(NULL, true, nonnull));
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
        OptionalInt64_t parsed = Int64$parse(Text$from_str(arg), NULL);
        if (!parsed.has_value) print_err("Could not parse argument for ", flag, ": ", arg);
        *(Int64_t *)dest = parsed.value;
    } else if (type == &Int32$info) {
        OptionalInt32_t parsed = Int32$parse(Text$from_str(arg), NULL);
        if (!parsed.has_value) print_err("Could not parse argument for ", flag, ": ", arg);
        *(Int32_t *)dest = parsed.value;
    } else if (type == &Int16$info) {
        OptionalInt16_t parsed = Int16$parse(Text$from_str(arg), NULL);
        if (!parsed.has_value) print_err("Could not parse argument for ", flag, ": ", arg);
        *(Int16_t *)dest = parsed.value;
    } else if (type == &Int8$info) {
        OptionalInt8_t parsed = Int8$parse(Text$from_str(arg), NULL);
        if (!parsed.has_value) print_err("Could not parse argument for ", flag, ": ", arg);
        *(Int8_t *)dest = parsed.value;
    } else if (type == &Byte$info) {
        OptionalByte_t parsed = Byte$parse(Text$from_str(arg), NULL);
        if (!parsed.has_value) print_err("Could not parse argument for ", flag, ": ", arg);
        *(Byte_t *)dest = parsed.value;
    } else if (type == &Bool$info) {
        OptionalBool_t parsed = Bool$parse(Text$from_str(arg), NULL);
        if (parsed == NONE_BOOL) print_err("Could not parse argument for ", flag, ": ", arg);
        *(Bool_t *)dest = parsed;
    } else if (type == &Num$info) {
        OptionalNum_t parsed = Num$parse(Text$from_str(arg), NULL);
        if (isnan(parsed)) print_err("Could not parse argument for ", flag, ": ", arg);
        *(Num_t *)dest = parsed;
    } else if (type == &Num32$info) {
        OptionalNum32_t parsed = Num32$parse(Text$from_str(arg), NULL);
        if (isnan(parsed)) print_err("Could not parse argument for ", flag, ": ", arg);
        *(Num32_t *)dest = parsed;
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
                *args = parse_arg_list(values, flag, dest, type, false);
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
                        List$insert_value(&texts, Text$as_c_string(*(Text_t *)(texts.data + j * texts.stride)), I(0),
                                          sizeof(const char *));
                } else {
                    values = List(arg_value);
                }
                *args = parse_arg_list(values, flag, dest, type, false);
                return true;
            }
        } else if (short_flag && arg[0] == '-' && arg[1] != '-' && strchr(arg + 1, short_flag)) {
            char *loc = strchr(arg + 1, short_flag);
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
                        List$insert_value(&texts, Text$as_c_string(*(Text_t *)(texts.data + j * texts.stride)), I(0),
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
