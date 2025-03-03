// Built-in functions

#include <errno.h>
#include <execinfo.h>
#include <fcntl.h>
#include <gc.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/param.h>
#include <sys/random.h>
#include <time.h>

#include "bools.h"
#include "files.h"
#include "functiontype.h"
#include "integers.h"
#include "optionals.h"
#include "metamethods.h"
#include "patterns.h"
#include "paths.h"
#include "rng.h"
#include "siphash.h"
#include "stdlib.h"
#include "tables.h"
#include "text.h"
#include "util.h"

public bool USE_COLOR;

static void signal_handler(int sig, siginfo_t *, void *)
{
    assert(sig == SIGILL);
    fflush(stdout);
    if (USE_COLOR) fputs("\x1b[31;7m ===== ILLEGAL INSTRUCTION ===== \n\n\x1b[m", stderr);
    else fputs("===== ILLEGAL INSTRUCTION =====\n\n", stderr);
    print_stack_trace(stderr, 3, 4);
    fflush(stderr);
    raise(SIGABRT);
    _exit(1);
}

public void tomo_init(void)
{
   GC_INIT();
   USE_COLOR = getenv("COLOR") ? strcmp(getenv("COLOR"), "1") == 0 : isatty(STDOUT_FILENO);
   if (getenv("NO_COLOR") && getenv("NO_COLOR")[0] != '\0')
       USE_COLOR = false;
   getrandom(TOMO_HASH_KEY, sizeof(TOMO_HASH_KEY), 0);

   int rng_fd = open("/dev/urandom", O_RDONLY);
   if (rng_fd < 0)
       fail("Couldn't read from /dev/urandom");
   uint8_t *random_bytes = GC_MALLOC_ATOMIC(40);
   if (read(rng_fd, (void*)random_bytes, 40) < 40)
       fail("Couldn't read from /dev/urandom");
   Array_t rng_seed = {.length=40, .data=random_bytes, .stride=1, .atomic=1};
   RNG$set_seed(default_rng, rng_seed);

   if (register_printf_specifier('k', printf_text, printf_text_size))
       errx(1, "Couldn't set printf specifier");

   struct sigaction sigact;
   sigact.sa_sigaction = signal_handler;
   sigemptyset(&sigact.sa_mask);
   sigact.sa_flags = 0;
   sigaction(SIGILL, &sigact, (struct sigaction *)NULL);
}

static bool parse_single_arg(const TypeInfo_t *info, char *arg, void *dest)
{
    if (!arg) return false;

    while (info->tag == OptionalInfo)
        info = info->OptionalInfo.type;

    if (info == &Int$info) {
        OptionalInt_t parsed = Int$from_str(arg);
        if (parsed.small != 0)
            *(OptionalInt_t*)dest = parsed;
        return parsed.small != 0;
    } else if (info == &Int64$info) {
        OptionalInt64_t parsed = Int64$parse(Text$from_str(arg));
        if (!parsed.is_none)
            *(OptionalInt64_t*)dest = parsed;
        return !parsed.is_none;
    } else if (info == &Int32$info) {
        OptionalInt32_t parsed = Int32$parse(Text$from_str(arg));
        if (!parsed.is_none)
            *(OptionalInt32_t*)dest = parsed;
        return !parsed.is_none;
    } else if (info == &Int16$info) {
        OptionalInt16_t parsed = Int16$parse(Text$from_str(arg));
        if (!parsed.is_none)
            *(OptionalInt16_t*)dest = parsed;
        return !parsed.is_none;
    } else if (info == &Int8$info) {
        OptionalInt8_t parsed = Int8$parse(Text$from_str(arg));
        if (!parsed.is_none)
            *(OptionalInt8_t*)dest = parsed;
        return !parsed.is_none;
    } else if (info == &Bool$info) {
        OptionalBool_t parsed = Bool$parse(Text$from_str(arg));
        if (parsed != NONE_BOOL)
            *(OptionalBool_t*)dest = parsed;
        return parsed != NONE_BOOL;
    } else if (info == &Num$info) {
        OptionalNum_t parsed = Num$parse(Text$from_str(arg));
        if (!isnan(parsed))
            *(OptionalNum_t*)dest = parsed;
        return !isnan(parsed);
    } else if (info == &Num32$info) {
        OptionalNum32_t parsed = Num32$parse(Text$from_str(arg));
        if (!isnan(parsed))
            *(OptionalNum32_t*)dest = parsed;
        return !isnan(parsed);
    } else if (info == &Path$info) {
        Path_t path = Text$from_str(arg);
        if (Text$equal_values(path, Path("~"))) {
            path = Path("~/");
        } else if (Text$equal_values(path, Path("."))) {
            path = Path("./");
        } else if (Text$equal_values(path, Path(".."))) {
            path = Path("../");
        } else if (!Text$starts_with(path, Text("./"))
            && !Text$starts_with(path, Text("../"))
            && !Text$starts_with(path, Text("/"))
            && !Text$starts_with(path, Text("~/"))) {
            path = Text$concat(Text("./"), path);
        }
        *(OptionalText_t*)dest = path;
        return true;
    } else if (info->tag == TextInfo) {
        *(OptionalText_t*)dest = Text$from_str(arg);
        return true;
    } else if (info->tag == EnumInfo) {
        for (int t = 0; t < info->EnumInfo.num_tags; t++) {
            NamedType_t named = info->EnumInfo.tags[t];
            size_t len = strlen(named.name);
            if (strncmp(arg, named.name, len) == 0 && (arg[len] == '\0' || arg[len] == ':')) {
                *(int32_t*)dest = (t + 1);

                // Simple tag (no associated data):
                if (!named.type || (named.type->tag == StructInfo && named.type->StructInfo.num_fields == 0))
                    return true;

                // Single-argument tag:
                if (arg[len] != ':')
                    errx(1, "Invalid value for %k.%s: %s", &t, named.name, arg);
                size_t offset = sizeof(int32_t);
                if (named.type->align > 0 && offset % (size_t)named.type->align > 0)
                    offset += (size_t)named.type->align - (offset % (size_t)named.type->align);
                if (!parse_single_arg(named.type, arg + len + 1, dest + offset))
                    return false;
                return true;
            }
        }
        errx(1, "Invalid value for %s: %s", info->EnumInfo.name, arg);
    } else if (info->tag == StructInfo) {
        if (info->StructInfo.num_fields == 0)
            return true;
        else if (info->StructInfo.num_fields == 1)
            return parse_single_arg(info->StructInfo.fields[0].type, arg, dest);

        Text_t t = generic_as_text(NULL, false, info);
        errx(1, "Unsupported multi-argument struct type for argument parsing: %k", &t);
    } else if (info->tag == ArrayInfo) {
        errx(1, "Array arguments must be specified as `--flag ...` not `--flag=...`");
    } else if (info->tag == TableInfo) {
        errx(1, "Table arguments must be specified as `--flag ...` not `--flag=...`");
    } else {
        Text_t t = generic_as_text(NULL, false, info);
        errx(1, "Unsupported type for argument parsing: %k", &t);
    }
}

static Array_t parse_array(const TypeInfo_t *item_info, int n, char *args[])
{
    int64_t padded_size = item_info->size;
    if ((padded_size % item_info->align) > 0)
        padded_size = padded_size + item_info->align - (padded_size % item_info->align);

    Array_t items = {
        .stride=padded_size,
        .length=n,
        .data=GC_MALLOC((size_t)(padded_size*n)),
    };
    for (int i = 0; i < n; i++) {
        bool success = parse_single_arg(item_info, args[i], items.data + items.stride*i);
        if (!success)
            errx(1, "Couldn't parse argument: %s", args[i]);
    }
    return items;
}

// Arguments take the form key=value, with a guarantee that there is an '='
static Table_t parse_table(const TypeInfo_t *table, int n, char *args[])
{
    const TypeInfo_t *key = table->TableInfo.key, *value = table->TableInfo.value;
    int64_t padded_size = key->size;
    if ((padded_size % value->align) > 0)
        padded_size = padded_size + value->align - (padded_size % value->align);
    int64_t value_offset = padded_size;
    padded_size += value->size;
    if ((padded_size % key->align) > 0)
        padded_size = padded_size + key->align - (padded_size % key->align);

    Array_t entries = {
        .stride=padded_size,
        .length=n,
        .data=GC_MALLOC((size_t)(padded_size*n)),
    };
    for (int i = 0; i < n; i++) {
        char *key_arg = args[i];
        char *equals = strchr(key_arg, '=');
        assert(equals);
        char *value_arg = equals + 1;
        *equals = '\0';

        bool success = parse_single_arg(key, key_arg, entries.data + entries.stride*i);
        if (!success)
            errx(1, "Couldn't parse table key: %s", key_arg);

        success = parse_single_arg(value, value_arg, entries.data + entries.stride*i + value_offset);
        if (!success)
            errx(1, "Couldn't parse table value: %s", value_arg);

        *equals = '=';
    }
    return Table$from_entries(entries, table);
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstack-protector"
public void _tomo_parse_args(int argc, char *argv[], Text_t usage, Text_t help, int spec_len, cli_arg_t spec[spec_len])
{
    bool populated_args[spec_len] = {};
    bool used_args[argc] = {};
    for (int i = 1; i < argc; ) {
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

                if (non_opt_type == &Bool$info
                    && strncmp(argv[i], "--no-", strlen("--no-")) == 0
                    && strcmp(argv[i] + strlen("--no-"), spec[s].name) == 0) {
                    *(OptionalBool_t*)spec[s].dest = false;
                    populated_args[s] = true;
                    used_args[i] = true;
                    goto next_arg;
                }

                if (strncmp(spec[s].name, argv[i] + 2, strlen(spec[s].name)) != 0)
                    continue;

                char after_name = argv[i][2+strlen(spec[s].name)];
                if (after_name == '\0') { // --foo val
                    used_args[i] = true;
                    if (non_opt_type->tag == ArrayInfo) {
                        int num_args = 0;
                        while (i + 1 + num_args < argc) {
                            if (argv[i+1+num_args][0] == '-')
                                break;
                            used_args[i+1+num_args] = true;
                            num_args += 1;
                        }
                        populated_args[s] = true;
                        *(OptionalArray_t*)spec[s].dest = parse_array(non_opt_type->ArrayInfo.item, num_args, &argv[i+1]);
                    } else if (non_opt_type->tag == TableInfo) {
                        int num_args = 0;
                        while (i + 1 + num_args < argc) {
                            if (argv[i+1+num_args][0] == '-' || !strchr(argv[i+1+num_args], '='))
                                break;
                            used_args[i+1+num_args] = true;
                            num_args += 1;
                        }
                        populated_args[s] = true;
                        *(OptionalTable_t*)spec[s].dest = parse_table(non_opt_type, num_args, &argv[i+1]);
                    } else if (non_opt_type == &Bool$info) { // --flag
                        populated_args[s] = true;
                        *(OptionalBool_t*)spec[s].dest = true;
                    } else {
                        if (i + 1 >= argc)
                            errx(1, "Missing argument: %s\n%k", argv[i], &usage);
                        used_args[i+1] = true;
                        populated_args[s] = parse_single_arg(spec[s].type, argv[i+1], spec[s].dest);
                        if (!populated_args[s])
                            errx(1, "Couldn't parse argument: %s %s\n%k", argv[i], argv[i+1], &usage);
                    }
                    goto next_arg;
                } else if (after_name == '=') { // --foo=val
                    used_args[i] = true;
                    populated_args[s] = parse_single_arg(spec[s].type, 2 + argv[i] + strlen(spec[s].name) + 1, spec[s].dest);
                    if (!populated_args[s])
                        errx(1, "Couldn't parse argument: %s\n%k", argv[i], &usage);
                    goto next_arg;
                } else {
                    continue;
                }
            }

            if (streq(argv[i], "--help")) {
                say(help, true);
                exit(0);
            }
            errx(1, "Unrecognized argument: %s\n%k", argv[i], &usage);
        } else if (argv[i][0] == '-' && argv[i][1] && argv[i][1] != '-') { // Single flag args
            used_args[i] = true;
            for (char *f = argv[i] + 1; *f; f++) {
                for (int s = 0; s < spec_len; s++) {
                    if (spec[s].name[0] != *f || strlen(spec[s].name) > 1)
                        continue;

                    const TypeInfo_t *non_opt_type = spec[s].type;
                    while (non_opt_type->tag == OptionalInfo)
                        non_opt_type = non_opt_type->OptionalInfo.type;

                    if (non_opt_type->tag == ArrayInfo) {
                        if (f[1]) errx(1, "No value provided for -%c\n%k", *f, &usage);
                        int num_args = 0;
                        while (i + 1 + num_args < argc) {
                            if (argv[i+1+num_args][0] == '-')
                                break;
                            used_args[i+1+num_args] = true;
                            num_args += 1;
                        }
                        populated_args[s] = true;
                        *(OptionalArray_t*)spec[s].dest = parse_array(non_opt_type->ArrayInfo.item, num_args, &argv[i+1]);
                    } else if (non_opt_type->tag == TableInfo) {
                        int num_args = 0;
                        while (i + 1 + num_args < argc) {
                            if (argv[i+1+num_args][0] == '-' || !strchr(argv[i+1+num_args], '='))
                                break;
                            used_args[i+1+num_args] = true;
                            num_args += 1;
                        }
                        populated_args[s] = true;
                        *(OptionalTable_t*)spec[s].dest = parse_table(non_opt_type, num_args, &argv[i+1]);
                    } else if (non_opt_type == &Bool$info) { // -f
                        populated_args[s] = true;
                        *(OptionalBool_t*)spec[s].dest = true;
                    } else {
                        if (f[1] || i+1 >= argc) errx(1, "No value provided for -%c\n%k", *f, &usage);
                        used_args[i+1] = true;
                        populated_args[s] = parse_single_arg(spec[s].type, argv[i+1], spec[s].dest);
                        if (!populated_args[s])
                            errx(1, "Couldn't parse argument: %s %s\n%k", argv[i], argv[i+1], &usage);
                    }
                    goto next_flag;
                }

                if (*f == 'h') {
                    say(help, true);
                    exit(0);
                }
                errx(1, "Unrecognized flag: -%c\n%k", *f, &usage);
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
            if (s >= spec_len)
                errx(1, "Extra argument: %s\n%k", argv[i], &usage);
        }

        const TypeInfo_t *non_opt_type = spec[s].type;
        while (non_opt_type->tag == OptionalInfo)
            non_opt_type = non_opt_type->OptionalInfo.type;

        // You can't specify boolean flags positionally
        if (non_opt_type == &Bool$info)
            goto next_non_bool_flag;

        if (non_opt_type->tag == ArrayInfo) {
            int num_args = 0;
            while (i + num_args < argc) {
                if (!ignore_dashes && argv[i+num_args][0] == '-')
                    break;
                used_args[i+num_args] = true;
                num_args += 1;
            }
            populated_args[s] = true;
            *(OptionalArray_t*)spec[s].dest = parse_array(non_opt_type->ArrayInfo.item, num_args, &argv[i]);
        } else if (non_opt_type->tag == TableInfo) {
            int num_args = 0;
            while (i + num_args < argc) {
                if (argv[i+num_args][0] == '-' || !strchr(argv[i+num_args], '='))
                    break;
                used_args[i+num_args] = true;
                num_args += 1;
            }
            populated_args[s] = true;
            *(OptionalTable_t*)spec[s].dest = parse_table(non_opt_type, num_args, &argv[i]);
        } else {
            populated_args[s] = parse_single_arg(spec[s].type, argv[i], spec[s].dest);
        }

        if (!populated_args[s])
            errx(1, "Invalid value for %s: %s\n%k", spec[s].name, argv[i], &usage);
    }

    for (int s = 0; s < spec_len; s++) {
        if (!populated_args[s] && spec[s].required) {
            if (spec[s].type->tag == ArrayInfo)
                *(OptionalArray_t*)spec[s].dest = (Array_t){};
            else if (spec[s].type->tag == TableInfo)
                *(OptionalTable_t*)spec[s].dest = (Table_t){};
            else
                errx(1, "The required argument '%s' was not provided\n%k", spec[s].name, &usage);
        }
    }
}
#pragma GCC diagnostic pop

static void print_stack_line(FILE *out, OptionalText_t fn_name, const char *filename, int64_t line_num)
{
    // NOTE: this function is a bit inefficient. Each time we print a line, we
    // do a linear scan through the whole file. However, performance shouldn't
    // really matter if we only print stack lines when there's a crash.
    if (filename) {
        fprintf(out, "\033[34mFile\033[m \033[35;1m%s\033[m", filename);
        if (line_num >= 1)
            fprintf(out, "\033[34m line\033[m \033[35;1m%ld\033[m", line_num);
    }
    if (fn_name.length > 0) {
        fprintf(out, filename ? "\033[34m, in \033[m \033[36;1m%k\033[m" : "\033[36;1m%k\033[m", &fn_name);
    }
    fprintf(out, "\n");

    FILE *f = fopen(filename, "r");
    if (!f) return;
    char *line = NULL;
    size_t size = 0;
    ssize_t nread;
    int64_t cur_line = 1;
    while ((nread = getline(&line, &size, f)) != -1) {
        if (line[strlen(line)-1] == '\n')
            line[strlen(line)-1] = '\0';

        if (cur_line >= line_num)
            fprintf(out, "\033[33;1m%s\033[m\n", line);

        cur_line += 1;
        if (cur_line > line_num)
            break;
    }
    if (line) free(line);
    fclose(f);
}

void print_stack_trace(FILE *out, int start, int stop)
{
    // Print stack trace:
    void *stack[1024];
    int64_t size = (int64_t)backtrace(stack, sizeof(stack)/sizeof(stack[0]));
    char **strings = strings = backtrace_symbols(stack, size);
    for (int64_t i = start; i < size - stop; i++) {
        char *filename = strings[i];
        char *paren = strchrnul(strings[i], '(');
        char *addr_end = paren + 1 + strcspn(paren + 1, ")");
        ptrdiff_t offset = strtol(paren + 1, &addr_end, 16) - 1;
        const char *cmd = heap_strf("addr2line -e %.*s -is +0x%x", strcspn(filename, "("), filename, offset);
        FILE *fp = popen(cmd, "r");
        OptionalText_t fn_name = get_function_name(stack[i]);
        const char *src_filename = NULL;
        int64_t line_number = 0;
        if (fp) {
            char buf[PATH_MAX + 10] = {};
            if (fgets(buf, sizeof(buf), fp)) {
                char *saveptr, *line_num_str;
                if ((src_filename=strtok_r(buf, ":", &saveptr))
                    && (line_num_str=strtok_r(NULL, ":", &saveptr)))
                    line_number = atoi(line_num_str);
            }
            pclose(fp);
        }
        print_stack_line(out, fn_name, src_filename, line_number);
    }
}

__attribute__((format(printf, 1, 2)))
public _Noreturn void fail(const char *fmt, ...)
{
    fflush(stdout);
    if (USE_COLOR) fputs("\x1b[31;7m ==================== ERROR ==================== \n\n\x1b[0;1m", stderr);
    else fputs("==================== ERROR ====================\n\n", stderr);
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    if (USE_COLOR) fputs("\x1b[m", stderr);
    fputs("\n\n", stderr);
    va_end(args);
    print_stack_trace(stderr, 2, 4);
    fflush(stderr);
    raise(SIGABRT);
    _exit(1);
}

__attribute__((format(printf, 4, 5)))
public _Noreturn void fail_source(const char *filename, int64_t start, int64_t end, const char *fmt, ...)
{
    if (USE_COLOR) fputs("\n\x1b[31;7m ==================== ERROR ==================== \n\n\x1b[0;1m", stderr);
    else fputs("\n==================== ERROR ====================\n\n", stderr);

    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);

    file_t *file = filename ? load_file(filename) : NULL;
    if (filename && file) {
        fputs("\n", stderr);
        highlight_error(file, file->text+start, file->text+end, "\x1b[31;1m", 2, USE_COLOR);
        fputs("\n", stderr);
    }
    if (USE_COLOR) fputs("\x1b[m", stderr);

    print_stack_trace(stderr, 2, 4);
    fflush(stderr);
    raise(SIGABRT);
    _exit(1);
}

public Text_t builtin_last_err()
{
    return Text$from_str(strerror(errno));
}

static int TEST_DEPTH = 0;
static file_t *file = NULL;

public void start_test(const char *filename, int64_t start, int64_t end)
{
    if (filename && (file == NULL || strcmp(file->filename, filename) != 0))
        file = load_file(filename);

    if (filename && file) {
        const char *spaces = "                                                  ";
        int64_t first_line_len = (int64_t)strcspn(file->text + start, "\r\n");
        const char *slash = strrchr(filename, '/');
        const char *file_base = slash ? slash + 1 : filename;

        int64_t line_num = get_line_number(file, file->text + start);
        fprintf(stderr, USE_COLOR ? "%.*s\x1b[33;1m>> \x1b[m%.*s   %.*s\x1b[32;2m[%s:%ld]\x1b[m\n" : "%.*s>> %.*s   %.*s[%s:%ld]\n",
                3*TEST_DEPTH, spaces, first_line_len, file->text + start,
                MAX(0, 35-first_line_len-3*TEST_DEPTH), spaces, file_base, line_num);

        // For multi-line expressions, dedent each and print it on a new line with ".. " in front:
        if (end > start + first_line_len) {
            const char *line_start = get_line(file, line_num);
            int64_t indent_len = (int64_t)strspn(line_start, " \t");
            for (const char *line = file->text + start + first_line_len; line < file->text + end; line += strcspn(line, "\r\n")) {
                line += strspn(line, "\r\n");
                if ((int64_t)strspn(line, " \t") >= indent_len)
                    line += indent_len;
                fprintf(stderr, USE_COLOR ? "%.*s\x1b[33m.. \x1b[m%.*s\n" : "%.*s.. %.*s\n",
                        3*TEST_DEPTH, spaces, strcspn(line, "\r\n"), line);
            }
        }
    }
    ++TEST_DEPTH;
}

public void end_test(const void *expr, const TypeInfo_t *type, const char *expected)
{
    --TEST_DEPTH;
    if (!expr || !type) return;

    Text_t expr_text = generic_as_text(expr, USE_COLOR, type);
    Text_t type_name = generic_as_text(NULL, false, type);

    for (int i = 0; i < 3*TEST_DEPTH; i++) fputc(' ', stderr);
    fprintf(stderr, USE_COLOR ? "\x1b[2m=\x1b[0m %k \x1b[2m: \x1b[36m%k\x1b[m\n" : "= %k : %k\n", &expr_text, &type_name);
    if (expected && expected[0]) {
        Text_t expected_text = Text$from_str(expected);
        Text_t expr_plain = USE_COLOR ? generic_as_text(expr, false, type) : expr_text;
        bool success = Text$equal_values(expr_plain, expected_text);
        if (!success) {
            OptionalMatch_t colon = Text$find(expected_text, Text(":"), I_small(1));
            if (colon.index.small) {
                Text_t with_type = Text$concat(expr_plain, Text(" : "), type_name);
                success = Text$equal_values(with_type, expected_text);
            }
        }

        if (!success) {
            fprintf(stderr, 
                    USE_COLOR
                    ? "\n\x1b[31;7m ==================== TEST FAILED ==================== \x1b[0;1m\n\nExpected: \x1b[1;32m%s\x1b[0m\n\x1b[1m But got:\x1b[m %k\n\n"
                    : "\n==================== TEST FAILED ====================\n\nExpected: %s\n But got: %k\n\n",
                    expected, &expr_text);

            print_stack_trace(stderr, 2, 4);
            fflush(stderr);
            raise(SIGABRT);
        }
    }
}

public void say(Text_t text, bool newline)
{
    Text$print(stdout, text);
    if (newline)
        fputc('\n', stdout);
    fflush(stdout);
}

public _Noreturn void tomo_exit(Text_t text, int32_t status)
{
    if (text.length > 0)
        say(text, true);
    _exit(status);
}

public Text_t ask(Text_t prompt, bool bold, bool force_tty)
{
    OptionalText_t ret = NONE_TEXT;
    FILE *out = stdout;
    FILE *in = stdin;

    char *line = NULL;
    size_t bufsize = 0;
    ssize_t length = 0;
    char *gc_input = NULL;

    if (force_tty && !isatty(STDOUT_FILENO)) {
        out = fopen("/dev/tty", "w");
        if (!out) goto cleanup;
    }

    if (bold) fputs("\x1b[1m", out);
    Text$print(out, prompt);
    if (bold) fputs("\x1b[m", out);
    fflush(out);

    if (force_tty && !isatty(STDIN_FILENO)) {
        in = fopen("/dev/tty", "r");
        if (!in) {
            fputs("\n", out); // finish the line, since the user can't
            goto cleanup;
        }
    }

    length = getline(&line, &bufsize, in);
    if (length == -1) {
        fputs("\n", out); // finish the line, since we didn't get any input
        goto cleanup;
    }

    if (length > 0 && line[length-1] == '\n') {
        line[length-1] = '\0';
        --length;
    }

    gc_input = GC_MALLOC_ATOMIC((size_t)(length + 1));
    memcpy(gc_input, line, (size_t)(length + 1));

    ret = Text$from_strn(gc_input, (size_t)(length));

  cleanup:
    if (out && out != stdout) fclose(out);
    if (in && in != stdin) fclose(in);
    return ret;
}

public bool pop_flag(char **argv, int *i, const char *flag, Text_t *result)
{
    if (argv[*i][0] != '-' || argv[*i][1] != '-') {
        return false;
    } else if (streq(argv[*i] + 2, flag)) {
        *result = EMPTY_TEXT;
        argv[*i] = NULL;
        *i += 1;
        return true;
    } else if (strncmp(argv[*i] + 2, "no-", 3) == 0 && streq(argv[*i] + 5, flag)) {
        *result = Text("no");
        argv[*i] = NULL;
        *i += 1;
        return true;
    } else if (strncmp(argv[*i] + 2, flag, strlen(flag)) == 0 && argv[*i][2 + strlen(flag)] == '=') {
        *result = Text$from_str(argv[*i] + 2 + strlen(flag) + 1);
        argv[*i] = NULL;
        *i += 1;
        return true;
    } else {
        return false;
    }
}

public void sleep_num(double seconds)
{
    struct timespec ts;
    ts.tv_sec = (time_t)seconds;
    ts.tv_nsec = (long)((seconds - (double)ts.tv_sec) * 1e9);
    nanosleep(&ts, NULL);
}

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
