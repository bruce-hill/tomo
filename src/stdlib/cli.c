// Comman-line argument parsing

#include <ctype.h>
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

static bool is_numeric_type(const TypeInfo_t *info) {
    return (info == &Num$info || info == &Num32$info || info == &Int$info || info == &Int64$info || info == &Int32$info
            || info == &Int16$info || info == &Int8$info || info == &Byte$info);
}

static bool parse_single_arg(const TypeInfo_t *info, char *arg, void *dest) {
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

static List_t parse_list(const TypeInfo_t *item_info, int n, char *args[]) {
    int64_t padded_size = item_info->size;
    if ((padded_size % item_info->align) > 0)
        padded_size = padded_size + item_info->align - (padded_size % item_info->align);

    uint64_t u = (uint64_t)n;
    List_t items = {
        .stride = padded_size,
        .length = u,
        .data = GC_MALLOC((size_t)(padded_size * n)),
    };
    for (int i = 0; i < n; i++) {
        bool success = parse_single_arg(item_info, args[i], items.data + items.stride * i);
        if (!success) print_err("Couldn't parse argument: ", args[i]);
    }
    return items;
}

// Arguments take the form key=value, with a guarantee that there is an '='
static Table_t parse_table(const TypeInfo_t *table, int n, char *args[]) {
    const TypeInfo_t *key = table->TableInfo.key, *value = table->TableInfo.value;
    int64_t padded_size = key->size;
    if ((padded_size % value->align) > 0) padded_size = padded_size + value->align - (padded_size % value->align);
    int64_t value_offset = padded_size;
    padded_size += value->size;
    if ((padded_size % key->align) > 0) padded_size = padded_size + key->align - (padded_size % key->align);

    uint64_t u = (uint64_t)n;
    List_t entries = {
        .stride = padded_size,
        .length = u,
        .data = GC_MALLOC((size_t)(padded_size * n)),
    };
    for (int i = 0; i < n; i++) {
        char *key_arg = args[i];
        char *equals = strchr(key_arg, '=');
        assert(equals);
        char *value_arg = equals + 1;
        *equals = '\0';

        bool success = parse_single_arg(key, key_arg, entries.data + entries.stride * i);
        if (!success) print_err("Couldn't parse table key: ", key_arg);

        success = parse_single_arg(value, value_arg, entries.data + entries.stride * i + value_offset);
        if (!success) print_err("Couldn't parse table value: ", value_arg);

        *equals = '=';
    }
    return Table$from_entries(entries, table);
}

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstack-protector"
#endif
public
void _tomo_parse_args(int argc, char *argv[], Text_t usage, Text_t help, const char *version, int spec_len,
                      cli_arg_t spec[spec_len]) {
    bool populated_args[spec_len];
    bool used_args[argc];
    memset(populated_args, 0, sizeof(populated_args));
    memset(used_args, 0, sizeof(used_args));
    for (int i = 1; i < argc;) {
        if (argv[i][0] == '-' && argv[i][1] == '-') {
            if (argv[i][2] == '\0') { // "--" signals the rest of the arguments are literal
                used_args[i] = true;
                i += 1;
                break;
            }

            for (int s = 0; s < spec_len; s++) {
                const TypeInfo_t *non_opt_type = spec[s].type;
                while (non_opt_type->tag == OptionalInfo)
                    non_opt_type = non_opt_type->OptionalInfo.type;

                if (non_opt_type == &Bool$info && strncmp(argv[i], "--no-", strlen("--no-")) == 0
                    && strcmp(argv[i] + strlen("--no-"), spec[s].name) == 0) {
                    *(OptionalBool_t *)spec[s].dest = false;
                    populated_args[s] = true;
                    used_args[i] = true;
                    goto next_arg;
                }

                if (strncmp(spec[s].name, argv[i] + 2, strlen(spec[s].name)) != 0) continue;

                char after_name = argv[i][2 + strlen(spec[s].name)];
                if (after_name == '\0') { // --foo val
                    used_args[i] = true;
                    if (non_opt_type->tag == ListInfo) {
                        int num_args = 0;
                        while (i + 1 + num_args < argc) {
                            if (argv[i + 1 + num_args][0] == '-') break;
                            used_args[i + 1 + num_args] = true;
                            num_args += 1;
                        }
                        populated_args[s] = true;
                        *(OptionalList_t *)spec[s].dest =
                            parse_list(non_opt_type->ListInfo.item, num_args, &argv[i + 1]);
                    } else if (non_opt_type->tag == TableInfo) {
                        int num_args = 0;
                        while (i + 1 + num_args < argc) {
                            if (argv[i + 1 + num_args][0] == '-' || !strchr(argv[i + 1 + num_args], '=')) break;
                            used_args[i + 1 + num_args] = true;
                            num_args += 1;
                        }
                        populated_args[s] = true;
                        *(OptionalTable_t *)spec[s].dest = parse_table(non_opt_type, num_args, &argv[i + 1]);
                    } else if (non_opt_type == &Bool$info) { // --flag
                        populated_args[s] = true;
                        *(OptionalBool_t *)spec[s].dest = true;
                    } else {
                        if (i + 1 >= argc) print_err("Missing argument: ", argv[i], "\n", usage);
                        used_args[i + 1] = true;
                        populated_args[s] = parse_single_arg(spec[s].type, argv[i + 1], spec[s].dest);
                        if (!populated_args[s])
                            print_err("Couldn't parse argument: ", argv[i], " ", argv[i + 1], "\n", usage);
                    }
                    goto next_arg;
                } else if (after_name == '=') { // --foo=val
                    used_args[i] = true;
                    populated_args[s] =
                        parse_single_arg(spec[s].type, 2 + argv[i] + strlen(spec[s].name) + 1, spec[s].dest);
                    if (!populated_args[s]) print_err("Couldn't parse argument: ", argv[i], "\n", usage);
                    goto next_arg;
                } else {
                    continue;
                }
            }

            if (streq(argv[i], "--help")) {
                print(help);
                exit(0);
            }
            if (streq(argv[i], "--version")) {
                print(version);
                exit(0);
            }
            print_err("Unrecognized argument: ", argv[i], "\n", usage);
        } else if (argv[i][0] == '-' && argv[i][1] && argv[i][1] != '-' && !isdigit(argv[i][1])) { // Single flag args
            used_args[i] = true;
            for (char *f = argv[i] + 1; *f; f++) {
                char flag[] = {'-', *f, 0};
                for (int s = 0; s < spec_len; s++) {
                    if (spec[s].name[0] != *f || strlen(spec[s].name) > 1) continue;

                    const TypeInfo_t *non_opt_type = spec[s].type;
                    while (non_opt_type->tag == OptionalInfo)
                        non_opt_type = non_opt_type->OptionalInfo.type;

                    if (f[1] == '=') {
                        populated_args[s] = parse_single_arg(spec[s].type, f + 2, spec[s].dest);
                        if (!populated_args[s]) print_err("Couldn't parse argument: ", argv[i], "\n", usage);
                        f += strlen(f) - 1;
                    } else if (non_opt_type->tag == ListInfo) {
                        if (f[1]) print_err("No value provided for ", flag, "\n", usage);
                        int num_args = 0;
                        while (i + 1 + num_args < argc) {
                            if (argv[i + 1 + num_args][0] == '-') break;
                            used_args[i + 1 + num_args] = true;
                            num_args += 1;
                        }
                        populated_args[s] = true;
                        *(OptionalList_t *)spec[s].dest =
                            parse_list(non_opt_type->ListInfo.item, num_args, &argv[i + 1]);
                    } else if (non_opt_type->tag == TableInfo) {
                        int num_args = 0;
                        while (i + 1 + num_args < argc) {
                            if (argv[i + 1 + num_args][0] == '-' || !strchr(argv[i + 1 + num_args], '=')) break;
                            used_args[i + 1 + num_args] = true;
                            num_args += 1;
                        }
                        populated_args[s] = true;
                        *(OptionalTable_t *)spec[s].dest = parse_table(non_opt_type, num_args, &argv[i + 1]);
                    } else if (non_opt_type == &Bool$info) { // -f
                        populated_args[s] = true;
                        *(OptionalBool_t *)spec[s].dest = true;
                    } else if (is_numeric_type(non_opt_type) && (f[1] == '-' || f[1] == '.' || isdigit(f[1]))) { // -O3
                        size_t len = strspn(f + 1, "-0123456789._");
                        populated_args[s] =
                            parse_single_arg(spec[s].type, String(string_slice(f + 1, len)), spec[s].dest);
                        if (!populated_args[s]) print_err("Couldn't parse argument: ", argv[i], "\n", usage);
                        f += len;
                    } else {
                        if (f[1] || i + 1 >= argc) print_err("No value provided for ", flag, "\n", usage);
                        used_args[i + 1] = true;
                        populated_args[s] = parse_single_arg(spec[s].type, argv[i + 1], spec[s].dest);
                        if (!populated_args[s])
                            print_err("Couldn't parse argument: ", argv[i], " ", argv[i + 1], "\n", usage);
                    }
                    goto next_flag;
                }

                if (*f == 'h') {
                    print(help);
                    exit(0);
                }
                print_err("Unrecognized flag: ", flag, "\n", usage);
            next_flag:;
            }
        } else {
            // Handle positional args later
            i += 1;
            continue;
        }

    next_arg:
        while (used_args[i] && i < argc)
            i += 1;
    }

    // Get remaining positional arguments
    bool ignore_dashes = false;
    for (int i = 1, s = 0; i < argc; i++) {
        if (!ignore_dashes && streq(argv[i], "--")) {
            ignore_dashes = true;
            continue;
        }
        if (used_args[i]) continue;

        while (populated_args[s]) {
        next_non_bool_flag:
            ++s;
            if (s >= spec_len) print_err("Extra argument: ", argv[i], "\n", usage);
        }

        const TypeInfo_t *non_opt_type = spec[s].type;
        while (non_opt_type->tag == OptionalInfo)
            non_opt_type = non_opt_type->OptionalInfo.type;

        // You can't specify boolean flags positionally
        if (non_opt_type == &Bool$info) goto next_non_bool_flag;

        if (non_opt_type->tag == ListInfo) {
            int num_args = 0;
            while (i + num_args < argc) {
                if (!ignore_dashes && (argv[i + num_args][0] == '-' && !isdigit(argv[i + num_args][1]))) break;
                used_args[i + num_args] = true;
                num_args += 1;
            }
            populated_args[s] = true;
            *(OptionalList_t *)spec[s].dest = parse_list(non_opt_type->ListInfo.item, num_args, &argv[i]);
        } else if (non_opt_type->tag == TableInfo) {
            int num_args = 0;
            while (i + num_args < argc) {
                if ((argv[i + num_args][0] == '-' && !isdigit(argv[i + num_args][1]))
                    || !strchr(argv[i + num_args], '='))
                    break;
                used_args[i + num_args] = true;
                num_args += 1;
            }
            populated_args[s] = true;
            *(OptionalTable_t *)spec[s].dest = parse_table(non_opt_type, num_args, &argv[i]);
        } else {
            populated_args[s] = parse_single_arg(spec[s].type, argv[i], spec[s].dest);
        }

        if (!populated_args[s]) print_err("Invalid value for ", spec[s].name, ": ", argv[i], "\n", usage);
    }

    for (int s = 0; s < spec_len; s++) {
        if (!populated_args[s] && spec[s].required) {
            if (spec[s].type->tag == ListInfo) *(OptionalList_t *)spec[s].dest = EMPTY_LIST;
            else if (spec[s].type->tag == TableInfo) *(OptionalTable_t *)spec[s].dest = (Table_t){};
            else print_err("The required argument '", spec[s].name, "' was not provided\n", usage);
        }
    }
}
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
