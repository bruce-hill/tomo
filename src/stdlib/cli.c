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

static bool parse_single_arg(const TypeInfo_t *info, const char *arg, void *dest) {
    if (!arg) return false;

    if (info->tag == OptionalInfo) {
        const TypeInfo_t *nonnull = info->OptionalInfo.type;
        if (streq(arg, "none")) {
            if (nonnull == &Num$info) *(double *)dest = (double)NAN;
            else if (nonnull == &Num32$info) *(float *)dest = (float)NAN;
            else memset(dest, 0, (size_t)info->size);
            return true;
        } else {
            bool success = parse_single_arg(nonnull, arg, dest);
            if (success) {
                if (nonnull == &Int64$info) ((OptionalInt64_t *)dest)->has_value = true;
                else if (nonnull == &Int32$info) ((OptionalInt32_t *)dest)->has_value = true;
                else if (nonnull == &Int16$info) ((OptionalInt16_t *)dest)->has_value = true;
                else if (nonnull == &Int8$info) ((OptionalInt8_t *)dest)->has_value = true;
                else if (nonnull == &Byte$info) ((OptionalByte_t *)dest)->has_value = true;
                else if (nonnull->tag == StructInfo && nonnull != &Path$info) *(bool *)(dest + nonnull->size) = true;
            }
            return success;
        }
    } else if (info == &Int$info) {
        OptionalInt_t parsed = Int$from_str(arg);
        if (parsed.small != 0) *(Int_t *)dest = parsed;
        return parsed.small != 0;
    } else if (info == &Int64$info) {
        OptionalInt64_t parsed = Int64$parse(Text$from_str(arg), NULL);
        if (parsed.has_value) *(Int64_t *)dest = parsed.value;
        return parsed.has_value;
    } else if (info == &Int32$info) {
        OptionalInt32_t parsed = Int32$parse(Text$from_str(arg), NULL);
        if (parsed.has_value) *(Int32_t *)dest = parsed.value;
        return parsed.has_value;
    } else if (info == &Int16$info) {
        OptionalInt16_t parsed = Int16$parse(Text$from_str(arg), NULL);
        if (parsed.has_value) *(Int16_t *)dest = parsed.value;
        return parsed.has_value;
    } else if (info == &Int8$info) {
        OptionalInt8_t parsed = Int8$parse(Text$from_str(arg), NULL);
        if (parsed.has_value) *(Int8_t *)dest = parsed.value;
        return parsed.has_value;
    } else if (info == &Byte$info) {
        OptionalByte_t parsed = Byte$parse(Text$from_str(arg), NULL);
        if (parsed.has_value) *(Byte_t *)dest = parsed.value;
        return parsed.has_value;
    } else if (info == &Bool$info) {
        OptionalBool_t parsed = Bool$parse(Text$from_str(arg), NULL);
        if (parsed != NONE_BOOL) *(Bool_t *)dest = parsed;
        return parsed != NONE_BOOL;
    } else if (info == &Num$info) {
        OptionalNum_t parsed = Num$parse(Text$from_str(arg), NULL);
        if (!isnan(parsed)) *(Num_t *)dest = parsed;
        return !isnan(parsed);
    } else if (info == &Num32$info) {
        OptionalNum32_t parsed = Num32$parse(Text$from_str(arg), NULL);
        if (!isnan(parsed)) *(Num32_t *)dest = parsed;
        return !isnan(parsed);
    } else if (info == &Path$info) {
        *(Path_t *)dest = Path$from_str(arg);
        return true;
    } else if (info->tag == TextInfo) {
        *(Text_t *)dest = Text$from_str(arg);
        return true;
    } else if (info->tag == EnumInfo) {
        for (int t = 0; t < info->EnumInfo.num_tags; t++) {
            NamedType_t named = info->EnumInfo.tags[t];
            size_t len = strlen(named.name);
            if (strncmp(arg, named.name, len) == 0 && (arg[len] == '\0' || arg[len] == ':')) {
                *(int32_t *)dest = (t + 1);

                // Simple tag (no associated data):
                if (!named.type || (named.type->tag == StructInfo && named.type->StructInfo.num_fields == 0))
                    return true;

                // Single-argument tag:
                if (arg[len] != ':') print_err("Invalid value for ", t, ".", named.name, ": ", arg);
                size_t offset = sizeof(int32_t);
                if (named.type->align > 0 && offset % (size_t)named.type->align > 0)
                    offset += (size_t)named.type->align - (offset % (size_t)named.type->align);
                if (!parse_single_arg(named.type, arg + len + 1, dest + offset)) return false;
                return true;
            }
        }
        print_err("Invalid value for ", info->EnumInfo.name, ": ", arg);
    } else if (info->tag == StructInfo) {
        if (info->StructInfo.num_fields == 0) return true;
        else if (info->StructInfo.num_fields == 1) return parse_single_arg(info->StructInfo.fields[0].type, arg, dest);

        Text_t t = generic_as_text(NULL, false, info);
        print_err("Unsupported multi-argument struct type for argument parsing: ", t);
    } else if (info->tag == ListInfo) {
        print_err("List arguments must be specified as `--flag ...` not `--flag=...`");
    } else if (info->tag == TableInfo) {
        print_err("Table arguments must be specified as `--flag ...` not `--flag=...`");
    } else {
        Text_t t = generic_as_text(NULL, false, info);
        print_err("Unsupported type for argument parsing: ", t);
    }
    return false;
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
                if (b == NONE_BOOL) print_err("Invalid boolean value for flag --", flag, ": ", arg);
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
void _tomo_parse_args(int argc, char *argv[], Text_t usage, Text_t help, const char *version, int spec_len,
                      cli_arg_t spec[spec_len]) {
    bool *parsed = GC_MALLOC_ATOMIC(sizeof(bool[spec_len]));
    List_t args = EMPTY_LIST;
    for (int i = 1; i < argc; i++) {
        List$insert(&args, &argv[i], I(0), sizeof(const char *));
    }
    for (int i = 0; i < spec_len; i++) {
        parsed[i] = pop_cli_flag(&args, spec[i].short_flag, spec[i].name, spec[i].dest, spec[i].type);
    }
    for (int64_t i = 0; i < (int64_t)args.length; i++) {
        const char *arg = *(const char **)(args.data + i * args.stride);
        if (streq(arg, "--")) {
            List$remove_at(&args, I(i + 1), I(1), sizeof(const char *));
            break;
        } else if (arg[0] == '-') {
            print_err("Unrecognized argument: ", arg);
        }
    }
    for (int i = 0; i < spec_len && args.length > 0; i++) {
        if (!parsed[i] && spec[i].required) {
            parsed[i] = pop_cli_positional(&args, spec[i].name, spec[i].dest, spec[i].type);
        }
    }
    for (int i = 0; i < spec_len; i++) {
        if (!parsed[i] && spec[i].required) print_err("Missing required flag: --", spec[i].name, "\n", usage);
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
    if (args.length > 0) {
        print_err("Unknown flag values: ", generic_as_text(&args, true, List$info(&CString$info)));
    }
}

static int64_t parse_arg_list(List_t args, const char *flag, void *dest, const TypeInfo_t *type, bool allow_dashes) {
    if (type->tag == ListInfo) {
        void *item = GC_MALLOC((size_t)type->ListInfo.item->size);
        int64_t n = 0;
        for (; n < (int64_t)args.length; n++) {
            const char *arg = *(const char **)(args.data + n * args.stride);
            if (arg[0] == '-' && !allow_dashes) break;
            if (!parse_single_arg(type->ListInfo.item, arg, item))
                print_err("Couldn't parse argument for flag --", flag, ": ", arg);
            List$insert(dest, item, I(0), type->ListInfo.item->size);
        }
        return n;
    } else if (type->tag == TableInfo) {
        // Arguments take the form key=value, with a guarantee that there is an '='
        void *key = GC_MALLOC((size_t)type->TableInfo.key->size);
        void *value = GC_MALLOC((size_t)type->TableInfo.value->size);
        int64_t n = 0;
        for (; n < (int64_t)args.length; n++) {
            const char *arg = *(const char **)(args.data + n * args.stride);
            if (arg[0] == '-' && !allow_dashes) break;
            const char *colon = strchr(arg, ':');
            if (!colon) break;
            const char *key_arg = String(string_slice(arg, (size_t)(colon - arg)));
            if (!parse_single_arg(type->TableInfo.key, key_arg, key))
                print_err("Couldn't parse table key for flag --", flag, ": ", key_arg);

            const char *value_arg = colon + 1;
            if (!parse_single_arg(type->TableInfo.value, value_arg, value))
                print_err("Couldn't parse table value for flag --", flag, ": ", value_arg);
            Table$set(dest, key, value, type);
        }
        return n;
    } else {
        if (args.length == 0) print_err("No value provided for flag --", flag);
        const char *arg = *(const char **)args.data;
        if (!parse_single_arg(type, arg, dest)) print_err("Couldn't parse value for flag --", flag, ": ", arg);
        return 1;
    }
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
                if (i + 1 >= (int64_t)args->length) print_err("No value provided for flag: --", flag);
                List_t values = List$slice(*args, I(i + 2), I(-1));
                int64_t n = parse_arg_list(values, flag, dest, type, false);
                if (n == 0) print_err("No value provided for flag: --", flag);
                List$remove_at(args, I(i + 1), I(n + 1), sizeof(const char *));
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
                if (parse_arg_list(values, flag, dest, type, false) == 0)
                    print_err("No value provided for flag: --", flag);
                List$remove_at(args, I(i + 1), I(1), sizeof(const char *));
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
                if (parse_arg_list(values, flag, dest, type, false) == 0)
                    print_err("No value provided for flag: -", short_str);

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
                int64_t n = parse_arg_list(values, flag, dest, type, false);
                if (n == 0) print_err("No value provided for flag: -", short_str);
                if (loc == arg + 1) {
                    // Case: -f values...
                    List$remove_at(args, I(i + 1), I(n + 1), sizeof(const char *));
                } else {
                    // Case: -abcdef values... -> -abcde
                    char *remainder = String(string_slice(arg, (size_t)(loc - arg)));
                    if unlikely (args->data_refcount > 0) List$compact(args, sizeof(const char *));
                    *(const char **)(args->data + i * args->stride) = remainder;
                    List$remove_at(args, I(i + 2), I(n), sizeof(const char *));
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
                        List$insert_value(&texts, Text$as_c_string(*(Text_t *)(texts.data + j * texts.stride)), I(0),
                                          sizeof(const char *));
                } else {
                    // Case: -fVALUE
                    values = List(arg_value);
                }
                if (parse_arg_list(values, flag, dest, type, false) == 0)
                    print_err("No value provided for flag: -", short_str);
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

bool pop_cli_positional(List_t *args, const char *flag, void *dest, const TypeInfo_t *type) {
    if (args->length == 0) {
        print_err("No value provided for flag: --", flag);
        return false;
    }
    int64_t n = parse_arg_list(*args, flag, dest, type, true);
    if (n == 0) print_err("No value provided for flag: --", flag);
    List$remove_at(args, I(1), I(n), sizeof(const char *));
    return true;
}
