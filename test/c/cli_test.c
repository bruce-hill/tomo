// Tests for the command-line argument parser (src/stdlib/cli.c), which is
// shared by the `tomo` compiler and by every program it compiles.
// To run the test: make test-cli
//
// The parser answers by printing and exiting, with errors to stderr and
// --help/--version to stdout, so those cases run in a forked child (see
// run_in_child) and assert on the child's exit status and output.

#include "cli.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "bools.h"
#include "bytes.h"
#include "c_strings.h"
#include "datatypes.h"
#include "enums.h"
#include "floats.h"
#include "integers.h"
#include "lists.h"
#include "metamethods.h"
#include "nums.h"
#include "optionals.h"
#include "paths.h"
#include "pointers.h"
#include "print.h"
#include "structs.h"
#include "tables.h"
#include "text.h"
#include "types.h"

static int checks = 0;
static int failures = 0;

#define FAIL(...)                                                                                                      \
    do {                                                                                                               \
        fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);                                                           \
        fprintf(stderr, __VA_ARGS__);                                                                                  \
        fputc('\n', stderr);                                                                                           \
        failures++;                                                                                                    \
    } while (0)

#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        checks++;                                                                                                      \
        if (!(cond)) FAIL("%s", #cond);                                                                                \
    } while (0)

// Compare a value against its rendering, using the type's own as_text. Gives a
// readable diff for the compound types (lists, tables, optionals).
#define CHECK_TEXT(value_ptr, type, want)                                                                              \
    do {                                                                                                               \
        checks++;                                                                                                      \
        const char *_got = Text$as_c_string(generic_as_text((value_ptr), false, (type)));                              \
        if (strcmp(_got, (want)) != 0) FAIL("got `%s`, want `%s`", _got, (want));                                      \
    } while (0)

#define CHECK_STR(got, want)                                                                                           \
    do {                                                                                                               \
        checks++;                                                                                                      \
        const char *_g = (got), *_w = (want);                                                                          \
        if (_g == NULL || strcmp(_g, _w) != 0) FAIL("got `%s`, want `%s`", _g ? _g : "(null)", _w);                    \
    } while (0)

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// An argument list, as the parser takes it:
#define ARGS(...) TypedList(const char *, __VA_ARGS__)

// The arguments left over after a pop, joined with spaces, i.e. what the parser
// would go on to treat as positional values.
static const char *leftovers(List_t args) {
    return CString$join(" ", args);
}

// Pop `flag` from `args` and check what it parsed and what it left behind.
// The parsed value is compared through its own type's as_text, so one form
// covers every argument type: a struct renders as `Point{x=1, y=2}`, an
// optional as its value or `none`, a list as `[1, 2]`.
#define CHECK_POP(type, initial, short_flag, flag, args, want_value, want_left)                                        \
    do {                                                                                                               \
        __typeof(initial) _v = (initial);                                                                              \
        List_t _args = (args);                                                                                         \
        checks++;                                                                                                      \
        if (!pop_cli_flag(&_args, (short_flag), (flag), &_v, (type))) FAIL("--%s was not popped", (flag));             \
        else {                                                                                                         \
            CHECK_TEXT(&_v, (type), (want_value));                                                                     \
            CHECK_STR(leftovers(_args), (want_left));                                                                  \
        }                                                                                                              \
    } while (0)

// The same, for a flag that isn't present: nothing is parsed, nothing consumed.
#define CHECK_NO_POP(type, initial, short_flag, flag, args, want_value, want_left)                                     \
    do {                                                                                                               \
        __typeof(initial) _v = (initial);                                                                              \
        List_t _args = (args);                                                                                         \
        checks++;                                                                                                      \
        if (pop_cli_flag(&_args, (short_flag), (flag), &_v, (type))) FAIL("--%s was popped", (flag));                  \
        else {                                                                                                         \
            CHECK_TEXT(&_v, (type), (want_value));                                                                     \
            CHECK_STR(leftovers(_args), (want_left));                                                                  \
        }                                                                                                              \
    } while (0)

// ...and for a value filled positionally rather than by flag.
#define CHECK_POSITIONAL(type, initial, args, allow_dashes, want_value, want_left)                                     \
    do {                                                                                                               \
        __typeof(initial) _v = (initial);                                                                              \
        List_t _args = (args);                                                                                         \
        checks++;                                                                                                      \
        if (!pop_cli_positional(&_args, "arg", &_v, (type), (allow_dashes))) FAIL("nothing was filled");               \
        else {                                                                                                         \
            CHECK_TEXT(&_v, (type), (want_value));                                                                     \
            CHECK_STR(leftovers(_args), (want_left));                                                                  \
        }                                                                                                              \
    } while (0)

// Run `fn(arg)` in a forked child with stdout and stderr captured, and return
// the child's exit status. Used for the cases that print and exit.
static int run_in_child(void (*fn)(void *), void *arg, char *output, size_t cap) {
    fflush(NULL);
    int pipe_fds[2];
    if (pipe(pipe_fds) != 0) {
        perror("pipe");
        exit(1);
    }
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        exit(1);
    }
    if (pid == 0) {
        close(pipe_fds[0]);
        dup2(pipe_fds[1], STDOUT_FILENO);
        dup2(pipe_fds[1], STDERR_FILENO);
        close(pipe_fds[1]);
        fn(arg);
        fflush(NULL);
        _exit(0);
    }
    close(pipe_fds[1]);
    size_t len = 0;
    for (;;) {
        ssize_t n = read(pipe_fds[0], output + len, cap - 1 - len);
        if (n <= 0) break;
        len += (size_t)n;
        if (len >= cap - 1) break;
    }
    output[len] = '\0';
    close(pipe_fds[0]);
    int status = 0;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

// A whole `tomo_parse_arg_list` invocation, packaged so it can be run in a
// child process:
typedef struct {
    List_t args;
    cli_help_info_t info;
    int spec_len;
    cli_arg_t *spec;
} parse_case_t;

static void do_parse(void *p) {
    parse_case_t *c = p;
    tomo_parse_arg_list(c->args, c->info, c->spec_len, c->spec);
}

// Assert that parsing exits nonzero with a message containing `want`:
static void expect_parse_error(parse_case_t *c, const char *want, int line) {
    checks++;
    char output[8192];
    int status = run_in_child(do_parse, c, output, sizeof(output));
    if (status == 0) FAIL("(line %d) expected a failure, but it succeeded: %s", line, output);
    else if (strstr(output, want) == NULL)
        FAIL("(line %d) expected an error mentioning `%s`, got: %s", line, want, output);
}

#define EXPECT_PARSE_ERROR(case_ptr, want) expect_parse_error((case_ptr), (want), __LINE__)

// Assert that parsing exits 0 (the --help path) and prints something
// containing `want`:
static void expect_parse_output(parse_case_t *c, const char *want, int line) {
    checks++;
    char output[8192];
    int status = run_in_child(do_parse, c, output, sizeof(output));
    if (status != 0) FAIL("(line %d) expected success, got status %d: %s", line, status, output);
    else if (strstr(output, want) == NULL)
        FAIL("(line %d) expected output mentioning `%s`, got: %s", line, want, output);
}

#define EXPECT_PARSE_OUTPUT(case_ptr, want) expect_parse_output((case_ptr), (want), __LINE__)

// ---------------------------------------------------------------------------
// Type infos for the compound types, synthesized here the way the compiler
// emits them. Used throughout: as argument types, as list/table contents, and
// as the things optional and pointer wrappers wrap.
// ---------------------------------------------------------------------------

static NamedType_t point_fields[] = {{"x", &Int32$info}, {"y", &Int32$info}};
static const TypeInfo_t Point$info = {
    .size = 2 * sizeof(int32_t),
    .align = __alignof__(int32_t),
    .tag = StructInfo,
    .StructInfo.name = "Point",
    .StructInfo.fields = point_fields,
    .StructInfo.num_fields = 2,
    .metamethods = Struct$metamethods,
};
typedef struct {
    int32_t x, y;
} Point_t;
typedef struct {
    Point_t value;
    bool has_value;
} OptionalPoint_t;

// A struct whose leading field isn't numeric, so its values can't start with
// a dash:
static NamedType_t label_fields[] = {{"name", &Text$info}, {"n", &Int32$info}};
static const TypeInfo_t Label$info = {
    .size = sizeof(Text_t) + sizeof(int32_t),
    .align = __alignof__(Text_t),
    .tag = StructInfo,
    .StructInfo.name = "Label",
    .StructInfo.fields = label_fields,
    .StructInfo.num_fields = 2,
    .metamethods = Struct$metamethods,
};
typedef struct {
    Text_t name;
    int32_t n;
} Label_t;

// An enum of plain tags (no associated values), like `enum Color(Red, Green, Blue)`:
static NamedType_t color_tags[] = {{"Red", NULL}, {"Green", NULL}, {"Blue", NULL}};
static const TypeInfo_t Color$info = {
    .size = sizeof(int32_t),
    .align = __alignof__(int32_t),
    .tag = EnumInfo,
    .EnumInfo.name = "Color",
    .EnumInfo.tags = color_tags,
    .EnumInfo.num_tags = 3,
    .metamethods = Enum$metamethods,
};

// An enum whose tags carry values, like `enum Shape(Circle(r:Int32), Rect(x,y:Int32))`:
static NamedType_t circle_fields[] = {{"r", &Int32$info}};
static const TypeInfo_t Circle$info = {
    .size = sizeof(int32_t),
    .align = __alignof__(int32_t),
    .tag = StructInfo,
    .StructInfo.name = "Circle",
    .StructInfo.fields = circle_fields,
    .StructInfo.num_fields = 1,
    .metamethods = Struct$metamethods,
};
static NamedType_t shape_tags[] = {{"Circle", &Circle$info}, {"Rect", &Point$info}};
typedef struct {
    int32_t tag;
    union {
        struct {
            int32_t r;
        } Circle;
        Point_t Rect;
    };
} Shape_t;
static const TypeInfo_t Shape$info = {
    .size = sizeof(int32_t) + 2 * sizeof(int32_t),
    .align = __alignof__(int32_t),
    .tag = EnumInfo,
    .EnumInfo.name = "Shape",
    .EnumInfo.tags = shape_tags,
    .EnumInfo.num_tags = 2,
    .metamethods = Enum$metamethods,
};

// ---------------------------------------------------------------------------
// pop_cli_flag: the long-flag versions
// ---------------------------------------------------------------------------

static void test_long_flags(void) {
    // --flag value, and --flag=value, which consumes only the one token:
    CHECK_POP(&Int$info, I(0), 0, "count", ARGS("--count", "42", "rest"), "42", "rest");
    CHECK_POP(&Int$info, I(0), 0, "count", ARGS("--count=42", "rest"), "42", "rest");

    // A flag can appear anywhere in the list, not just first:
    CHECK_POP(&Int$info, I(0), 0, "count", ARGS("first", "--count", "42", "last"), "42", "first last");

    // An absent flag leaves both the value and the arguments untouched:
    CHECK_NO_POP(&Int$info, I(7), 0, "count", ARGS("a", "b"), "7", "a b");

    // A longer flag that merely starts with this flag's name is not a match:
    CHECK_NO_POP(&Int$info, I(0), 0, "count", ARGS("--counts", "42"), "0", "--counts 42");

    // A bare "--" ends flag parsing: what follows is values, not flags.
    CHECK_NO_POP(&Int$info, I(0), 0, "count", ARGS("--", "--count", "42"), "0", "-- --count 42");
    CHECK_NO_POP(&Int$info, I(-5), 0, "count", ARGS("--", "--count", "-1"), "-5", "-- --count -1");
}

// ---------------------------------------------------------------------------
// pop_cli_flag: the short-flag versions, including clusters
// ---------------------------------------------------------------------------

static void test_short_flags(void) {
    // -f value, -f=value, and -fVALUE (e.g. -O3):
    CHECK_POP(&Int$info, I(0), 'n', "count", ARGS("-n", "42", "rest"), "42", "rest");
    CHECK_POP(&Int$info, I(0), 'n', "count", ARGS("-n=42", "rest"), "42", "rest");
    CHECK_POP(&Int$info, I(0), 'n', "count", ARGS("-n42", "rest"), "42", "rest");

    // In a cluster, the flag takes its value from the next argument (or from
    // inside the token) and the rest of the cluster is left for the others:
    CHECK_POP(&Int$info, I(0), 'n', "count", ARGS("-abn", "42", "rest"), "42", "-ab rest");
    CHECK_POP(&Int$info, I(0), 'n', "count", ARGS("-abn=42", "rest"), "42", "-ab rest");
    // The trailing ";" stops the leftover from being read as another cluster
    // with a value: `-ab1 2` must not parse as b=1, then a=2:
    CHECK_POP(&Int$info, I(0), 'n', "count", ARGS("-abn42", "rest"), "42", "-ab; rest");

    // With no short flag registered, -n is just another argument:
    CHECK_NO_POP(&Int$info, I(0), 0, "count", ARGS("-n", "42"), "0", "-n 42");

    // A long flag and its short alias both fill the same spec entry:
    CHECK_POP(&Int$info, I(0), 'n', "count", ARGS("--count", "1"), "1", "");
}

// ---------------------------------------------------------------------------
// Boolean flags, which have their own versions (--no-flag, --flag=yes, ...)
// ---------------------------------------------------------------------------

static void test_boolean_flags(void) {
    // A bare --flag means yes, --no-flag means no, and both are consumed whole:
    CHECK_POP(&Bool$info, false, 'f', "force", ARGS("--force", "rest"), "yes", "rest");
    CHECK_POP(&Bool$info, true, 'f', "force", ARGS("--no-force", "rest"), "no", "rest");

    // --flag=<boolean> and -f=<boolean>, in every form Bool$parse accepts:
    static const char *const yes_values[] = {"yes", "true", "on", "1"};
    static const char *const no_values[] = {"no", "false", "off", "0"};
    for (size_t i = 0; i < sizeof(yes_values) / sizeof(yes_values[0]); i++) {
        CHECK_POP(&Bool$info, false, 'f', "force", ARGS(String("--force=", yes_values[i])), "yes", "");
        CHECK_POP(&Bool$info, false, 'f', "force", ARGS(String("-f=", yes_values[i])), "yes", "");
        CHECK_POP(&Bool$info, true, 'f', "force", ARGS(String("--force=", no_values[i])), "no", "");
        CHECK_POP(&Bool$info, true, 'f', "force", ARGS(String("-f=", no_values[i])), "no", "");
    }

    // A bare short flag takes no value, so it can be clustered with others:
    CHECK_POP(&Bool$info, false, 'f', "force", ARGS("-abfc", "rest"), "yes", "-abc rest");
    CHECK_POP(&Bool$info, true, 'f', "force", ARGS("-abf=no", "rest"), "no", "-ab rest");

    // A boolean flag never swallows the following argument:
    CHECK_POP(&Bool$info, false, 'f', "force", ARGS("--force", "no"), "yes", "no");

    // A longer flag that merely starts with this one's name is not a match, in
    // any of the boolean values:
    CHECK_NO_POP(&Bool$info, false, 'f', "force", ARGS("--forced"), "no", "--forced");
    CHECK_NO_POP(&Bool$info, false, 'f', "force", ARGS("--forced=yes"), "no", "--forced=yes");
    CHECK_NO_POP(&Bool$info, false, 'f', "force", ARGS("--no-forced"), "no", "--no-forced");

    // ...and it stops at a bare "--" like every other flag:
    CHECK_NO_POP(&Bool$info, false, 'f', "force", ARGS("--", "--force"), "no", "-- --force");
}

// ---------------------------------------------------------------------------
// Value types
// ---------------------------------------------------------------------------

static void test_scalar_types(void) {
    // Each numeric type parses up to its own limits:
    CHECK_POP(&Int64$info, (int64_t)0, 0, "n", ARGS("--n", "9223372036854775807"), "9223372036854775807", "");
    CHECK_POP(&Int32$info, (int32_t)0, 0, "n", ARGS("--n=2147483647"), "2147483647", "");
    CHECK_POP(&Int16$info, (int16_t)0, 0, "n", ARGS("--n=32767"), "32767", "");
    CHECK_POP(&Int8$info, (int8_t)0, 0, "n", ARGS("--n=127"), "127", "");
    CHECK_POP(&Byte$info, (uint8_t)0, 0, "n", ARGS("--n=255"), "0xff", ""); // bytes render in hex
    // Big integers are arbitrary-precision, so they aren't capped at 64 bits:
    CHECK_POP(&Int$info, I(0), 0, "n", ARGS("--n=123456789012345678901234567890"), "123456789012345678901234567890",
              "");
    CHECK_POP(&Float64$info, 0.0, 0, "x", ARGS("--x=1.5"), "1.5", "");
    CHECK_POP(&Float32$info, 0.0f, 0, "x", ARGS("--x=1.5"), "1.5", "");
    CHECK_POP(&Num$info, NONE_NUM, 0, "x", ARGS("--x=1.5"), "1.5", "");

    // The negative extremes are reachable positionally, where dashes are
    // allowed (as a flag value they need the numeric exception; see
    // test_negative_numbers):
    CHECK_POSITIONAL(&Int64$info, (int64_t)0, ARGS("-9223372036854775808"), true, "-9223372036854775808", "");
    CHECK_POSITIONAL(&Int8$info, (int8_t)0, ARGS("-128", "rest"), true, "-128", "rest");

    // Text is taken verbatim, and a leading backslash escapes a value that
    // would otherwise look like a flag:
    CHECK_POP(&Text$info, EMPTY_TEXT, 0, "name", ARGS("--name", "Åsa Ünicode"), "\"Åsa Ünicode\"", "");
    CHECK_POP(&Text$info, EMPTY_TEXT, 0, "name", ARGS("--name", "\\-dash"), "\"-dash\"", "");
    CHECK_POP(&CString$info, (const char *)NULL, 0, "name", ARGS("--name=hello"), "\"hello\"", "");
    CHECK_POP(&Path$info, (Path_t)NULL, 0, "file", ARGS("--file", "/tmp/x.txt"), "/tmp/x.txt", "");

    // Booleans are also parseable as a plain value type:
    CHECK_POP(&Bool$info, false, 'f', "force", ARGS("--force=yes"), "yes", "");

    // A pointer argument allocates the pointed-to value and parses into it:
    {
        Int32_t *ptr = NULL;
        List_t args = ARGS("--n", "42");
        CHECK(pop_cli_flag(&args, 0, "n", &ptr, Pointer$info("@", &Int32$info)));
        CHECK(ptr && *ptr == 42);
    }
}

// An optional argument renders as its value or as `none`, which is exactly
// what has to be checked, whichever way the type represents none internally
// (a has_value byte, a NaN, a NULL pointer, a reserved bit pattern).
#define OPT(t, inner) Optional$info(sizeof(t), __alignof__(t), (inner))

static void test_optional_types(void) {
    const TypeInfo_t *opt_int = OPT(Int_t, &Int$info);
    const TypeInfo_t *opt_i64 = OPT(OptionalInt64_t, &Int64$info);
    const TypeInfo_t *opt_byte = OPT(OptionalByte_t, &Byte$info);
    const TypeInfo_t *opt_text = OPT(Text_t, &Text$info);
    const TypeInfo_t *opt_path = OPT(Path_t, &Path$info);
    const TypeInfo_t *opt_f64 = OPT(double, &Float64$info);

    // "none" produces the none value for each representation...
    CHECK_POP(opt_int, I(1), 0, "n", ARGS("--n=none"), "none", "");
    CHECK_POP(opt_i64, ((OptionalInt64_t){.value = 1, .has_value = true}), 0, "n", ARGS("--n=none"), "none", "");
    CHECK_POP(opt_text, (OptionalText_t)Text("x"), 0, "name", ARGS("--name=none"), "none", "");
    CHECK_POP(opt_path, (OptionalPath_t)Path("/x"), 0, "file", ARGS("--file=none"), "none", "");
    CHECK_POP(opt_f64, 1.0, 0, "x", ARGS("--x=none"), "none", "");

    // ...and a real value sets both the value and, where there is one, the flag:
    CHECK_POP(opt_int, NONE_INT, 0, "n", ARGS("--n=42"), "42", "");
    CHECK_POP(opt_i64, ((OptionalInt64_t){0}), 0, "n", ARGS("--n=42"), "42", "");
    CHECK_POP(opt_byte, ((OptionalByte_t){0}), 0, "n", ARGS("--n=7"), "0x07", "");

    // An optional struct carries its has_value flag in a byte past the value:
    CHECK_POP(OPT(OptionalPoint_t, &Point$info), ((OptionalPoint_t){0}), 0, "at", ARGS("--at", "3", "4"),
              "Point{x=3, y=4}", "");
}

static void test_compound_types(void) {
    const TypeInfo_t *texts = List$info(&Text$info), *ints = List$info(&Int32$info);
    const TypeInfo_t *table = Table$info(&Text$info, &Int32$info), *set = Set$info(&Text$info);

    // A list flag consumes every following non-flag argument...
    CHECK_POP(texts, EMPTY_LIST, 0, "files", ARGS("--files", "a", "b", "c", "--other"), "[\"a\", \"b\", \"c\"]",
              "--other");
    // ...or splits on commas when the values are inside the token:
    CHECK_POP(texts, EMPTY_LIST, 0, "files", ARGS("--files=a,b,c", "rest"), "[\"a\", \"b\", \"c\"]", "rest");
    CHECK_POP(texts, EMPTY_LIST, 'f', "files", ARGS("-fa,b,c"), "[\"a\", \"b\", \"c\"]", "");
    CHECK_POP(ints, EMPTY_LIST, 0, "nums", ARGS("--nums", "1", "2", "3"), "[1, 2, 3]", "");
    // A flag with nothing parseable after it is an empty list, not a failure:
    CHECK_POP(ints, EMPTY_LIST, 0, "nums", ARGS("--nums", "--other"), "[]", "--other");

    // A table takes key:value pairs, and stops at the first thing that isn't one:
    CHECK_POP(table, EMPTY_TABLE, 0, "defs", ARGS("--defs", "a:1", "b:2", "not-a-pair"), "{\"a\": 1, \"b\": 2}",
              "not-a-pair");
    CHECK_POP(table, EMPTY_TABLE, 0, "defs", ARGS("--defs=a:1,b:2"), "{\"a\": 1, \"b\": 2}", "");
    // A set (a table with no values) takes bare keys:
    CHECK_POP(set, EMPTY_TABLE, 0, "tags", ARGS("--tags", "a", "b", "--other"), "{\"a\", \"b\"}", "--other");

    // A struct consumes one argument per field, in declaration order:
    CHECK_POP(&Point$info, ((Point_t){0, 0}), 0, "at", ARGS("--at", "3", "4", "rest"), "Point{x=3, y=4}", "rest");

    // An enum of plain tags is matched by name...
    CHECK_POP(&Color$info, (int32_t)0, 0, "color", ARGS("--color", "Green"), "Green", "");
    // ...and a tag with associated values parses them from what follows. The
    // tag number is 1-based, as the compiler numbers tags; the payload is
    // checked field by field rather than by rendering, since how this
    // synthesized enum prints is beside the point.
    {
        Shape_t shape = {0};
        List_t args = ARGS("--shape", "Circle", "5");
        CHECK(pop_cli_flag(&args, 0, "shape", &shape, &Shape$info));
        CHECK(shape.tag == 1 && shape.Circle.r == 5);
    }
    {
        Shape_t shape = {0};
        List_t args = ARGS("--shape", "Rect", "2", "3");
        CHECK(pop_cli_flag(&args, 0, "shape", &shape, &Shape$info));
        CHECK(shape.tag == 2 && shape.Rect.x == 2 && shape.Rect.y == 3);
    }
}

// ---------------------------------------------------------------------------
// Optional arguments whose value type is itself a container or a boolean.
// These carry their own `none` (a bit pattern a parsed value can't collide
// with) rather than a separate has_value flag.
// ---------------------------------------------------------------------------

static void test_optional_containers(void) {
    const TypeInfo_t *ints = List$info(&Int32$info), *table = Table$info(&Text$info, &Int32$info);
    const TypeInfo_t *set = Set$info(&Text$info);
    const TypeInfo_t *opt_list = OPT(List_t, ints), *opt_table = OPT(Table_t, table), *opt_set = OPT(Table_t, set);

    // These start out as NONE_LIST/NONE_TABLE (a NULL data pointer), which is
    // what the compiler emits for an argument with no default:
    CHECK_POP(opt_list, (OptionalList_t)NONE_LIST, 0, "nums", ARGS("--nums", "1", "2"), "[1, 2]", "");
    CHECK_POP(opt_list, (OptionalList_t)NONE_LIST, 0, "nums", ARGS("--nums=1,2"), "[1, 2]", "");
    CHECK_POP(opt_table, (OptionalTable_t)NONE_TABLE, 0, "defs", ARGS("--defs", "a:1", "b:2"), "{\"a\": 1, \"b\": 2}",
              "");
    CHECK_POP(opt_set, (OptionalTable_t)NONE_TABLE, 0, "tags", ARGS("--tags", "a", "b"), "{\"a\", \"b\"}", "");

    // A flag given with no values of its own is empty, not none, since the flag
    // was still given:
    CHECK_POP(opt_list, (OptionalList_t)NONE_LIST, 0, "nums", ARGS("--nums", "--other"), "[]", "--other");
    CHECK_POP(opt_table, (OptionalTable_t)NONE_TABLE, 0, "defs", ARGS("--defs", "--other"), "{}", "--other");
    CHECK_POP(opt_set, (OptionalTable_t)NONE_TABLE, 0, "tags", ARGS("--tags", "--other"), "{}", "--other");

    // "none" restores it, and a value already there is appended to:
    CHECK_POP(opt_list, (OptionalList_t)EMPTY_LIST, 0, "nums", ARGS("--nums=none"), "none", "");
    CHECK_POP(opt_table, (OptionalTable_t)EMPTY_TABLE, 0, "defs", ARGS("--defs=none"), "none", "");
    CHECK_POP(opt_set, (OptionalTable_t)EMPTY_TABLE, 0, "tags", ARGS("--tags=none"), "none", "");
    CHECK_POP(opt_list, (OptionalList_t)TypedList(int32_t, 9), 0, "nums", ARGS("--nums", "1"), "[9, 1]", "");
}

// An optional boolean is a boolean flag too, since is_bool_arg() looks through
// the optional, so the help text advertises it as a toggle, with `--flag=none`
// for the third value.
static void test_optional_booleans(void) {
    const TypeInfo_t *opt_bool = OPT(OptionalBool_t, &Bool$info);

    // Every boolean value, plus `none` for the third value:
    static const struct {
        const char *arg, *want;
    } cases[] = {
        {"--force", "yes"},       {"--no-force", "no"},  {"--force=yes", "yes"}, {"--force=no", "no"},
        {"--force=true", "yes"},  {"--force=off", "no"}, {"--force=1", "yes"},   {"--force=0", "no"},
        {"--force=none", "none"}, {"-f", "yes"},         {"-f=no", "no"},        {"-f=none", "none"},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
        CHECK_POP(opt_bool, (OptionalBool_t)99, 'f', "force", ARGS(cases[i].arg), cases[i].want, "");

    // It behaves like any other boolean flag: it takes no value from the
    // following argument, it clusters, and an absent flag changes nothing.
    CHECK_POP(opt_bool, NONE_BOOL, 'f', "force", ARGS("--force", "yes"), "yes", "yes");
    CHECK_POP(opt_bool, NONE_BOOL, 'f', "force", ARGS("-abfc"), "yes", "-abc");
    CHECK_NO_POP(opt_bool, NONE_BOOL, 'f', "force", ARGS("--other"), "none", "--other");

    // Given positionally it goes through the general value path instead, and
    // has to reach the same three values:
    CHECK_POSITIONAL(opt_bool, (OptionalBool_t)99, ARGS("yes"), false, "yes", "");
    CHECK_POSITIONAL(opt_bool, (OptionalBool_t)99, ARGS("no"), false, "no", "");
    CHECK_POSITIONAL(opt_bool, (OptionalBool_t)no, ARGS("none"), false, "none", "");

    // "none" is a value, not a way to smuggle a bad one past the parser:
    {
        OptionalBool_t force = no;
        cli_arg_t spec[] = {{.name = "force", .dest = &force, .type = opt_bool, .short_flag = 'f'}};
        cli_help_info_t info = {.usage = Text("Usage: prog")};
        parse_case_t c = {.args = ARGS("--force=banana"), .info = info, .spec_len = 1, .spec = spec};
        EXPECT_PARSE_ERROR(&c, "Invalid boolean value for flag force");
        parse_case_t short_c = {.args = ARGS("-f=banana"), .info = info, .spec_len = 1, .spec = spec};
        EXPECT_PARSE_ERROR(&short_c, "Invalid boolean value for flag -f");
    }

    // ...and a *non*-optional boolean has no "none" at all:
    {
        bool force = false;
        cli_arg_t spec[] = {{.name = "force", .dest = &force, .type = &Bool$info}};
        parse_case_t c = {
            .args = ARGS("--force=none"), .info = {.usage = Text("Usage: prog")}, .spec_len = 1, .spec = spec};
        EXPECT_PARSE_ERROR(&c, "Invalid boolean value for flag force");
    }

    // The help text advertises it as the toggle it is:
    {
        OptionalBool_t force = NONE_BOOL;
        cli_arg_t spec[] = {{.name = "force", .dest = &force, .type = opt_bool, .short_flag = 'f', .required = true}};
        CHECK_STR(Text$as_c_string(tomo_generate_usage(Text("prog"), 1, spec)), "Usage: prog --force|-f");
    }
}

// ---------------------------------------------------------------------------
// Negative numbers: a dashed token is a value, not a flag, when the argument
// it fills is numeric.
// ---------------------------------------------------------------------------

static void test_negative_numbers(void) {
    const TypeInfo_t *ints = List$info(&Int32$info), *texts = List$info(&Text$info);

    // Make sure negative values are parsed right:
    CHECK_POP(&Int$info, I(0), 'n', "count", ARGS("--count", "-1"), "-1", "");
    CHECK_POP(&Int$info, I(0), 'n', "count", ARGS("--count=-1"), "-1", "");
    CHECK_POP(&Int$info, I(0), 'n', "count", ARGS("-n", "-1"), "-1", "");
    CHECK_POP(&Int$info, I(0), 'n', "count", ARGS("-n=-1"), "-1", "");
    CHECK_POP(&Int$info, I(0), 'n', "count", ARGS("-n-1"), "-1", "");

    // Every numeric type takes one, in both integer and fractional forms:
    CHECK_POP(&Int64$info, (int64_t)0, 0, "n", ARGS("--n=-64"), "-64", "");
    CHECK_POP(&Int32$info, (int32_t)0, 0, "n", ARGS("--n=-32"), "-32", "");
    CHECK_POP(&Int16$info, (int16_t)0, 0, "n", ARGS("--n=-16"), "-16", "");
    CHECK_POP(&Int8$info, (int8_t)0, 0, "n", ARGS("--n=-8"), "-8", "");
    CHECK_POP(&Float64$info, 0.0, 0, "x", ARGS("--x", "-.5"), "-0.5", ""); // may lead with the dot
    CHECK_POP(&Float32$info, 0.0f, 0, "x", ARGS("--x=-1.5"), "-1.5", "");
    CHECK_POP(&Num$info, NONE_NUM, 0, "x", ARGS("--x=-1.5e3"), "-1500", "");
    CHECK_POP(OPT(OptionalInt64_t, &Int64$info), ((OptionalInt64_t){0}), 0, "n", ARGS("--n=-42"), "-42", "");

    // A dashed token is still a flag to a non-numeric argument, so "-1" here
    // is left over rather than becoming the name. (Giving it to --name
    // explicitly is an error; see test_errors.)
    CHECK_NO_POP(&Text$info, (Text_t)Text("unset"), 0, "name", ARGS("-1"), "\"unset\"", "-1");

    // Containers defer to what they hold: a list of numbers keeps taking
    // negative values, while a list of text stops at the first dashed token.
    CHECK_POP(ints, EMPTY_LIST, 0, "nums", ARGS("--nums", "-1", "2", "-3", "--other"), "[-1, 2, -3]", "--other");
    CHECK_POP(ints, EMPTY_LIST, 0, "nums", ARGS("--nums=-1,-2"), "[-1, -2]", "");
    CHECK_POP(OPT(List_t, ints), NONE_LIST, 0, "nums", ARGS("--nums=-1,2"), "[-1, 2]", "");
    CHECK_POP(texts, EMPTY_LIST, 0, "files", ARGS("--files", "a", "-1", "b"), "[\"a\"]", "-1 b");
    CHECK_POP(Table$info(&Int32$info, &Text$info), EMPTY_TABLE, 0, "defs", ARGS("--defs", "-1:a", "2:b"),
              "{-1: \"a\", 2: \"b\"}", "");

    // A struct of numbers takes them in every field, and so does one behind an
    // optional, where the check sees the wrapper rather than the fields:
    CHECK_POP(&Point$info, ((Point_t){0, 0}), 0, "at", ARGS("--at", "-3", "-4"), "Point{x=-3, y=-4}", "");
    CHECK_POP(OPT(OptionalPoint_t, &Point$info), ((OptionalPoint_t){0}), 0, "at", ARGS("--at", "-3", "-4"),
              "Point{x=-3, y=-4}", "");
    // A struct whose leading field isn't numeric can still take a negative in
    // the fields that are (its leading token is rejected; see test_errors):
    CHECK_POP(&Label$info, ((Label_t){0}), 0, "label", ARGS("--label", "x", "-2"), "Label{name=\"x\", n=-2}", "");

    // A pointer looks through to what it points at:
    {
        Int32_t *ptr = NULL;
        List_t args = ARGS("--n", "-42");
        CHECK(pop_cli_flag(&args, 0, "n", &ptr, Pointer$info("@", &Int32$info)));
        CHECK(ptr && *ptr == -42);
    }
    {
        List_t *nums = NULL;
        List_t args = ARGS("--nums", "-1", "2");
        CHECK(pop_cli_flag(&args, 0, "nums", &nums, Pointer$info("@", ints)));
        CHECK(nums != NULL);
        CHECK_TEXT(nums, ints, "[-1, 2]");
    }

    // Infinity is a value a float can be given, even though it isn't written
    // with digits:
    CHECK_POP(&Float64$info, 0.0, 0, "x", ARGS("--x", "-inf"), "-inf", "");
    CHECK_POP(&Float32$info, 0.0f, 0, "x", ARGS("--x=-INFINITY"), "-inf", "");

    // A negative number is never a cluster of short flags, so a flag whose
    // letter happens to appear inside one doesn't claim it:
    CHECK_NO_POP(&Int32$info, (int32_t)7, 'e', "expr", ARGS("--x", "-1e5"), "7", "--x -1e5");
    CHECK_NO_POP(&Int32$info, (int32_t)7, 'x', "hex", ARGS("--n", "-0x10"), "7", "--n -0x10");
    CHECK_NO_POP(&Bool$info, false, 'e', "expr", ARGS("--x", "-1e5"), "no", "--x -1e5");
    // ...including one written with a leading dot:
    CHECK_NO_POP(&Int32$info, (int32_t)7, 'e', "expr", ARGS("--x", "-.5e3"), "7", "--x -.5e3");
    // ...but a token that only looks like a word still is one (`-inf` could be
    // `-i -n -f`), and a short flag followed by digits is unaffected:
    CHECK_POP(&Bool$info, false, 'n', "no", ARGS("-inf"), "yes", "-if");
    CHECK_POP(&Int32$info, (int32_t)0, 'O', "opt", ARGS("-O3"), "3", "");

    // Recognizing a number is more permissive than accepting one: a token that
    // is a number but not one this argument can hold still reaches the parse
    // error naming the argument and the value, rather than the flag error.
    {
        uint8_t byte = 0;
        cli_arg_t spec[] = {{.name = "n", .dest = &byte, .type = &Byte$info}};
        parse_case_t c = {
            .args = ARGS("--n", "-1"), .info = {.usage = Text("Usage: prog")}, .spec_len = 1, .spec = spec};
        EXPECT_PARSE_ERROR(&c, "Could not parse argument for n: -1");
    }
    {
        // A malformed number is still a number, not an unknown flag, so it
        // reports the same way its positive form would:
        Int_t n = I(0);
        cli_arg_t spec[] = {{.name = "n", .dest = &n, .type = &Int$info}};
        parse_case_t c = {
            .args = ARGS("--n", "-1abc"), .info = {.usage = Text("Usage: prog")}, .spec_len = 1, .spec = spec};
        EXPECT_PARSE_ERROR(&c, "Could not parse argument for n: -1abc");
    }
    {
        // ...while `-inf` is no kind of number for an integer argument:
        Int_t n = I(0);
        cli_arg_t spec[] = {{.name = "n", .dest = &n, .type = &Int$info}};
        parse_case_t c = {
            .args = ARGS("--n", "-inf"), .info = {.usage = Text("Usage: prog")}, .spec_len = 1, .spec = spec};
        EXPECT_PARSE_ERROR(&c, "Not a valid flag: -inf");
    }
    {
        // Num has no infinity, though Float64 takes it:
        Num_t limit = NONE_NUM;
        cli_arg_t spec[] = {{.name = "limit", .dest = &limit, .type = &Num$info}};
        parse_case_t c = {
            .args = ARGS("--limit", "-inf"), .info = {.usage = Text("Usage: prog")}, .spec_len = 1, .spec = spec};
        EXPECT_PARSE_ERROR(&c, "Could not parse argument for limit: -inf");
    }
}

// ---------------------------------------------------------------------------
// tomo_parse_arg_list: positional filling and "--"
// ---------------------------------------------------------------------------

// Parse `args` against a two-Text spec and summarize what each entry ended up
// with, as "<first>/<second>", writing "-" for one that nothing filled. The
// positional-filling rules are all about which entry gets what, so one line
// of this says the whole thing.
static const char *fill_two(List_t args, cli_help_info_t info, bool second_is_positional) {
    Text_t first = EMPTY_TEXT, second = EMPTY_TEXT;
    cli_arg_t spec[] = {
        {.name = "first", .dest = &first, .type = &Text$info},
        {.name = "second", .dest = &second, .type = &Text$info, .positional = second_is_positional},
    };
    tomo_parse_arg_list(args, info, 2, spec);
    return String(spec[0].populated ? Text$as_c_string(first) : "-", "/",
                  spec[1].populated ? Text$as_c_string(second) : "-");
}

// The same for a one-argument spec, checked through the value's own rendering.
#define CHECK_FILL(type_info, initial, args, info, want)                                                               \
    do {                                                                                                               \
        __typeof(initial) _v = (initial);                                                                              \
        cli_arg_t _spec[] = {{.name = "arg", .dest = &_v, .type = (type_info)}};                                       \
        tomo_parse_arg_list((args), (info), 1, _spec);                                                                 \
        CHECK_TEXT(&_v, (type_info), (want));                                                                          \
    } while (0)

static void test_positionals(void) {
    const cli_help_info_t plain = {0}, strict = {.strict_positionals = true};

    // Unpopulated entries are filled positionally, in spec order...
    CHECK_STR(fill_two(ARGS("a", "b"), plain, false), "a/b");
    // ...but an entry a flag already filled is skipped, so the lone positional
    // goes to `first`:
    CHECK_STR(fill_two(ARGS("--second", "b", "a"), plain, false), "a/b");
    // Everything after "--" is a value, even if it starts with a dash:
    CHECK_STR(fill_two(ARGS("a", "--", "-b"), plain, false), "a/-b");

    // With strict_positionals, only entries marked .positional are filled from
    // bare words, before and after "--" alike. (This is the mode
    // hand-written CLIs like the compiler's use.)
    CHECK_STR(fill_two(ARGS("a"), strict, true), "-/a");
    CHECK_STR(fill_two(ARGS("--", "-a"), strict, true), "-/-a"); // the dash is part of the value here

    // A list positional after "--" takes the dashed arguments too:
    CHECK_FILL(List$info(&Text$info), EMPTY_LIST, ARGS("--", "-a", "--b"), plain, "[\"-a\", \"--b\"]");

    // A negative number fills a numeric positional, both directly and after
    // the "--" that allows dashes generally:
    CHECK_FILL(&Int$info, I(0), ARGS("-1"), plain, "-1");
    CHECK_FILL(&Int$info, I(0), ARGS("--", "-1"), plain, "-1");

    // An unpopulated, non-required entry is left at its default:
    CHECK_FILL(&Text$info, (Text_t)Text("default"), ARGS(), plain, "\"default\"");
}

static void test_help_flag(void) {
    Text_t name = EMPTY_TEXT;
    cli_arg_t spec[] = {{.name = "name", .dest = &name, .type = &Text$info}};
    cli_help_info_t info = {.usage = Text("Usage: prog"), .help = Text("HELP TEXT HERE"), .help_short = 'h'};

    parse_case_t with_long = {.args = ARGS("--help"), .info = info, .spec_len = 1, .spec = spec};
    EXPECT_PARSE_OUTPUT(&with_long, "HELP TEXT HERE");

    parse_case_t with_short = {.args = ARGS("-h"), .info = info, .spec_len = 1, .spec = spec};
    EXPECT_PARSE_OUTPUT(&with_short, "HELP TEXT HERE");

    // --no-help is an explicit "no", so it's consumed but no help is shown and
    // parsing carries on normally:
    tomo_parse_arg_list(ARGS("--no-help", "bob"), info, 1, spec);
    CHECK_STR(Text$as_c_string(name), "bob");
}

// ---------------------------------------------------------------------------
// Failure modes
// ---------------------------------------------------------------------------

// Somewhere for a failing parse to write into: these cases only check the
// message, and the process exits before the value is ever read.
static union {
    Int_t i;
    Text_t text;
    int32_t i32;
    uint8_t byte;
    List_t list;
    Table_t table;
    bool b;
} error_dest;

#define BAD_ARG(flag, type_info, ...) {.name = (flag), .dest = &error_dest, .type = (type_info), __VA_ARGS__}

// Parse `args` against a spec and expect it to fail with a message containing
// `want`. The usage line is always shown, so it is checked once, below.
#define CHECK_PARSE_ERROR(args_, want, ...)                                                                            \
    do {                                                                                                               \
        cli_arg_t _spec[] = {__VA_ARGS__};                                                                             \
        parse_case_t _c = {.args = (args_),                                                                            \
                           .info = {.usage = Text("Usage: prog [--name text]")},                                       \
                           .spec_len = sizeof(_spec) / sizeof(_spec[0]),                                               \
                           .spec = _spec};                                                                             \
        EXPECT_PARSE_ERROR(&_c, (want));                                                                               \
    } while (0)

static void test_errors(void) {
    // A required argument that nothing fills, named as a flag or as an
    // argument depending on how it is declared:
    CHECK_PARSE_ERROR(ARGS(), "Missing required flag: name", BAD_ARG("name", &Text$info, .required = true));
    CHECK_PARSE_ERROR(ARGS(), "Missing required argument: file",
                      BAD_ARG("file", &Text$info, .required = true, .positional = true));

    // Arguments nothing in the spec can absorb:
    CHECK_PARSE_ERROR(ARGS("a", "b"), "Unknown flag values: b", BAD_ARG("name", &Text$info));

    // A flag at the very end, with nothing left to consume:
    CHECK_PARSE_ERROR(ARGS("--count"), "No value provided for flag: count",
                      BAD_ARG("count", &Int$info, .short_flag = 'n'));
    CHECK_PARSE_ERROR(ARGS("-n"), "No value provided for flag: -n", BAD_ARG("count", &Int$info, .short_flag = 'n'));

    // A value the type can't parse, and a flag where a value was expected:
    CHECK_PARSE_ERROR(ARGS("--count", "banana"), "Could not parse argument for count: banana",
                      BAD_ARG("count", &Int$info));
    CHECK_PARSE_ERROR(ARGS("--count", "--name"), "Not a valid flag: --name", BAD_ARG("count", &Int$info),
                      BAD_ARG("name", &Text$info));

    // A dashed value is a flag to a non-numeric argument even when it represents a
    // number, and a dashed non-number is a flag even to a numeric one:
    CHECK_PARSE_ERROR(ARGS("--name", "-1"), "Not a valid flag: -1", BAD_ARG("name", &Text$info));
    CHECK_PARSE_ERROR(ARGS("--count", "-x"), "Not a valid flag: -x", BAD_ARG("count", &Int$info));
    CHECK_PARSE_ERROR(ARGS("--count", "-"), "Not a valid flag: -", BAD_ARG("count", &Int$info));

    // A wrapped container stops at a dashed token exactly where a bare one
    // does, since the wrapper defers to it rather than erroring first, so
    // the token is left over rather than rejected:
    CHECK_PARSE_ERROR(ARGS("--files", "-1"), "Unknown flag values: -1",
                      BAD_ARG("files", Pointer$info("@", List$info(&Text$info))));
    // ...while a wrapper around a struct still rejects it, since a struct
    // consumes a fixed number of values rather than stopping on its own:
    CHECK_PARSE_ERROR(ARGS("--label", "-1", "2"), "Not a valid flag: -1",
                      BAD_ARG("label", Pointer$info("@", &Label$info)));

    // An enum tag that doesn't exist, with the valid names listed:
    CHECK_PARSE_ERROR(ARGS("--color", "Purple"), "Invalid enum name for Color: Purple", BAD_ARG("color", &Color$info));
    CHECK_PARSE_ERROR(ARGS("--color", "Purple"), "Valid names are: ", BAD_ARG("color", &Color$info));

    // A non-boolean value given to a boolean flag:
    CHECK_PARSE_ERROR(ARGS("--force=banana"), "Invalid boolean value for flag force",
                      BAD_ARG("force", &Bool$info, .short_flag = 'f'));
    CHECK_PARSE_ERROR(ARGS("-f=banana"), "Invalid boolean value for flag -f",
                      BAD_ARG("force", &Bool$info, .short_flag = 'f'));

    CHECK_PARSE_ERROR(ARGS(), "Usage: prog [--name text]", BAD_ARG("name", &Text$info, .required = true));

    // ...and the command hint is appended too, so a mistyped subcommand is
    // reported as such rather than as a stray argument:
    {
        Text_t name = EMPTY_TEXT;
        cli_arg_t spec[] = {{.name = "name", .dest = &name, .type = &Text$info}};
        parse_case_t c = {.args = ARGS("bulid", "x"),
                          .info = {.usage = Text("Usage: prog"),
                                   .command_hint = Text("\nbulid isn't a command. Did you mean build?")},
                          .spec_len = 1,
                          .spec = spec};
        EXPECT_PARSE_ERROR(&c, "Did you mean build?");
    }
}

// ---------------------------------------------------------------------------
// Generated usage and help text
// ---------------------------------------------------------------------------

// Somewhere for a spec to point at. Usage generation never reads `dest`, but
// the field is there, so give it something real rather than NULL.
static union {
    Int_t i;
    Text_t text;
    List_t list;
    Table_t table;
    int32_t i32;
    OptionalBool_t b;
} usage_dest;

// The usage line generated from a spec:
#define CHECK_USAGE(want, ...)                                                                                         \
    do {                                                                                                               \
        cli_arg_t _spec[] = {__VA_ARGS__};                                                                             \
        CHECK_STR(Text$as_c_string(tomo_generate_usage(Text("prog"), sizeof(_spec) / sizeof(_spec[0]), _spec)),        \
                  (want));                                                                                             \
    } while (0)

#define ARG(flag, type_info, ...) {.name = (flag), .dest = &usage_dest, .type = (type_info), __VA_ARGS__}

static void test_usage_text(void) {
    // Flags come first, then positionals; optional ones are bracketed, and
    // each flag shows a value placeholder derived from its type:
    CHECK_USAGE("Usage: prog --name|-n text [--force|-f] [--count N] [files...]",
                ARG("name", &Text$info, .short_flag = 'n', .required = true),
                ARG("force", &Bool$info, .short_flag = 'f'), ARG("count", &Int$info),
                ARG("files", List$info(&Path$info), .positional = true));

    // A metavar overrides the type-derived placeholder:
    CHECK_USAGE("Usage: prog --out FILE", ARG("out", &Text$info, .metavar = "FILE", .required = true));

    // An optional shows the placeholder of the type it wraps, and an
    // optional boolean is still a boolean flag, with no placeholder at all:
    CHECK_USAGE("Usage: prog --count N", ARG("count", OPT(Int_t, &Int$info), .required = true));
    CHECK_USAGE("Usage: prog --force", ARG("force", OPT(OptionalBool_t, &Bool$info), .required = true));

    // A list *flag* shows that it takes many values (a list positional is
    // displayed differently; see the first spec above):
    CHECK_USAGE("Usage: prog --files path1 path2...", ARG("files", List$info(&Path$info), .required = true));
    // An optional list takes many values too, as a flag and as a positional:
    CHECK_USAGE("Usage: prog --files path1 path2...",
                ARG("files", OPT(List_t, List$info(&Path$info)), .required = true));
    CHECK_USAGE("Usage: prog [files...]", ARG("files", OPT(List_t, List$info(&Path$info)), .positional = true));

    // Tables, sets, and enums get their own placeholders:
    CHECK_USAGE("Usage: prog --defs text1:N1 text2:N2... --tags text1 text2... --color Red|Green|Blue",
                ARG("defs", Table$info(&Text$info, &Int$info), .required = true),
                ARG("tags", Set$info(&Text$info), .required = true), ARG("color", &Color$info, .required = true));
}

// ---------------------------------------------------------------------------
// tomo_dispatch_command: the git-style command tree
// ---------------------------------------------------------------------------

// What the most recent dispatch ran, so the tests can assert on it:
static const char *ran_command = NULL;
static const char *ran_extra_args = NULL;

static int record_handler(cli_command_t *self, List_t extra_args) {
    ran_command = self->name ? self->name : "(root)";
    ran_extra_args = CString$join(" ", extra_args);
    return 0;
}

// A `prog [--verbose] [add <file>|submodule init <path>|<file>]` CLI, rebuilt
// for each test so that materialized help text and populated flags don't leak
// between cases.
typedef struct {
    cli_spec_t cli;
    Text_t add_file;
    Path_t init_path;
    Text_t root_file;
    bool verbose;
    cli_arg_t add_spec[1];
    cli_arg_t init_spec[1];
    cli_arg_t root_spec[1];
    cli_arg_t global_spec[1];
    cli_command_t add, init, submodule;
    cli_command_t *root_children[2];
    cli_command_t *submodule_children[1];
} test_cli_t;

static test_cli_t *make_test_cli(void) {
    test_cli_t *t = GC_MALLOC(sizeof(test_cli_t));
    memset(t, 0, sizeof(*t));
    t->add_spec[0] =
        (cli_arg_t){.name = "file", .dest = &t->add_file, .type = &Text$info, .required = true, .positional = true};
    t->init_spec[0] = (cli_arg_t){.name = "path", .dest = &t->init_path, .type = &Path$info, .positional = true};
    t->root_spec[0] = (cli_arg_t){.name = "file", .dest = &t->root_file, .type = &Text$info, .positional = true};
    t->global_spec[0] = (cli_arg_t){
        .name = "verbose", .dest = &t->verbose, .type = &Bool$info, .short_flag = 'V', .description = "Say more"};
    t->add = (cli_command_t){
        .name = "add", .summary = "Add a file", .spec_len = 1, .spec = t->add_spec, .handler = record_handler};
    t->init = (cli_command_t){
        .name = "init", .summary = "Initialize", .spec_len = 1, .spec = t->init_spec, .handler = record_handler};
    t->submodule_children[0] = &t->init;
    t->submodule = (cli_command_t){.name = "submodule", .num_children = 1, .children = t->submodule_children};
    t->root_children[0] = &t->add;
    t->root_children[1] = &t->submodule;
    t->cli = (cli_spec_t){
        .name = "prog",
        .summary = "A test program",
        .version = "prog 1.2.3",
        .version_short = 'v',
        .global_len = 1,
        .global_spec = t->global_spec,
        .root = {.spec_len = 1,
                 .spec = t->root_spec,
                 .num_children = 2,
                 .children = t->root_children,
                 .handler = record_handler},
    };
    return t;
}

typedef struct {
    cli_spec_t *cli;
    const char **argv;
} dispatch_case_t;

static _Noreturn void do_dispatch(void *p) {
    dispatch_case_t *c = p;
    int argc = 0;
    while (c->argv[argc])
        argc++;
    int status = tomo_dispatch_command(argc, (char **)c->argv, c->cli);
    fflush(NULL);
    _exit(status);
}

// Dispatch in a child process (so --help/--version output and parse failures
// can be inspected), asserting on the exit status and what it printed:
static void check_dispatch_output(cli_spec_t *cli, const char **argv, int want_status, const char *want, int line) {
    checks++;
    char output[16384];
    dispatch_case_t c = {.cli = cli, .argv = argv};
    int status = run_in_child(do_dispatch, &c, output, sizeof(output));
    if (status != want_status) FAIL("(line %d) got status %d, want %d. Output: %s", line, status, want_status, output);
    else if (strstr(output, want) == NULL)
        FAIL("(line %d) expected output mentioning `%s`, got: %s", line, want, output);
}

#define CHECK_DISPATCH_OF(cli, want_status, want, ...)                                                                 \
    check_dispatch_output((cli), (const char *[]){"prog", __VA_ARGS__ __VA_OPT__(, ) NULL}, want_status, want, __LINE__)

// The usual case: a freshly built test CLI, so materialized help text and
// populated flags don't leak between cases.
#define CHECK_DISPATCH(want_status, want, ...)                                                                         \
    CHECK_DISPATCH_OF(&make_test_cli()->cli, want_status, want __VA_OPT__(, ) __VA_ARGS__)

// Dispatch in-process, for the cases that succeed and have side effects to
// check. argv is counted here rather than at each call site, where a
// hand-written argc is one edit away from being wrong.
static int dispatch_argv(cli_spec_t *cli, const char **argv) {
    int argc = 0;
    while (argv[argc])
        argc++;
    ran_command = NULL;
    ran_extra_args = NULL;
    return tomo_dispatch_command(argc, (char **)argv, cli);
}

#define DISPATCH_OF(cli, ...) dispatch_argv((cli), (const char *[]){"prog", __VA_ARGS__ __VA_OPT__(, ) NULL})

// The usual case again, returned so the test can check what the command's
// arguments were filled with.
static test_cli_t *dispatch(const char **argv) {
    test_cli_t *t = make_test_cli();
    dispatch_argv(&t->cli, argv);
    return t;
}

#define DISPATCH(...) dispatch((const char *[]){"prog", __VA_ARGS__ __VA_OPT__(, ) NULL})

static void test_dispatch(void) {
    { // A named command runs with its own arguments:
        test_cli_t *t = DISPATCH("add", "hello.txt");
        CHECK_STR(ran_command, "add");
        CHECK_STR(Text$as_c_string(t->add_file), "hello.txt");
    }

    { // Commands nest arbitrarily deep:
        test_cli_t *t = DISPATCH("submodule", "init", "libs/x");
        CHECK_STR(ran_command, "init");
        CHECK_STR(Path$as_c_string(t->init_path), "libs/x");
    }

    { // A first word that isn't a command falls through to the root handler:
        test_cli_t *t = DISPATCH("hello.txt");
        CHECK_STR(ran_command, "(root)");
        CHECK_STR(Text$as_c_string(t->root_file), "hello.txt");
    }

    { // Global flags are valid before the command name...
        test_cli_t *t = DISPATCH("--verbose", "add", "x");
        CHECK(t->verbose);
        CHECK_STR(ran_command, "add");
    }
    { // ...and after it, and by their short alias:
        test_cli_t *t = DISPATCH("add", "x", "-V");
        CHECK(t->verbose);
        CHECK_STR(ran_command, "add");
    }

    { // A namespace with no handler of its own prints help and fails:
        CHECK_DISPATCH(1, "Commands:", "submodule");
    }

    { // An unrecognized word under a namespace is reported, with a suggestion:
        CHECK_DISPATCH(1, "Unrecognized command: inti", "submodule", "inti");
        CHECK_DISPATCH(1, "Did you mean init?", "submodule", "inti");
    }

    { // A word that resembles a top-level command is only blamed once the
        // parse actually fails. The root command here can absorb one file
        // argument, so the second word is what triggers the error:
        CHECK_DISPATCH(1, "isn't a command. Did you mean add?", "adk", "x");
    }

    { // ...but a lone word that parses fine just runs, even though it's one
        // edit away from a command name:
        test_cli_t *t = DISPATCH("adk");
        CHECK_STR(ran_command, "(root)");
        CHECK_STR(Text$as_c_string(t->root_file), "adk");
    }

    { // --help prints the help for whichever command was named:
        CHECK_DISPATCH(0, "prog add: Add a file", "add", "--help");
        CHECK_DISPATCH(0, "prog submodule init: Initialize", "submodule", "init", "--help");
        CHECK_DISPATCH(0, "A test program", "--help");
        // -h is the short alias, since nothing else claims it:
        CHECK_DISPATCH(0, "prog add: Add a file", "add", "-h");
    }

    { // Generated help lists the global flags (with their descriptions) and
        // the subcommands:
        CHECK_DISPATCH(0, "Global flags", "--help");
        CHECK_DISPATCH(0, "Say more", "--help");
        CHECK_DISPATCH(0, "add", "--help");
    }

    { // --version prints the version...
        CHECK_DISPATCH(0, "prog 1.2.3", "--version");
        // ...but -v is claimed by nothing here, so it works as the alias:
        CHECK_DISPATCH(0, "prog 1.2.3", "-v");
    }
}

// A CLI that defines its own `help` and `version` arguments, plus a `-v` of its
// own: the automatic flags must step aside rather than answering first.
static void test_dispatch_flag_shadowing(void) {
    static Text_t help_value, version_value;
    static bool verbose;
    static cli_arg_t spec[] = {
        {.name = "help", .dest = &help_value, .type = &Text$info},
        {.name = "version", .dest = &version_value, .type = &Text$info},
        {.name = "verbose", .dest = &verbose, .type = &Bool$info, .short_flag = 'v'},
    };
    static cli_command_t root;
    static cli_spec_t cli;
    root = (cli_command_t){.spec_len = 3, .spec = spec, .handler = record_handler};
    cli = (cli_spec_t){.name = "prog", .version = "prog 9.9.9", .version_short = 'v', .root = root};

    help_value = EMPTY_TEXT;
    version_value = EMPTY_TEXT;
    CHECK(DISPATCH_OF(&cli, "--help", "topic", "--version", "3") == 0);
    CHECK_STR(ran_command, "(root)");
    CHECK_STR(Text$as_c_string(help_value), "topic");
    CHECK_STR(Text$as_c_string(version_value), "3");

    // -v belongs to --verbose, not to the automatic --version:
    verbose = false;
    help_value = EMPTY_TEXT;
    version_value = EMPTY_TEXT;
    CHECK(DISPATCH_OF(&cli, "-v", "--help", "x", "--version", "1") == 0);
    CHECK(verbose);
}

// A program that claims -v (or -h) for something of its own keeps it: the
// automatic flags are popped before the command's spec is parsed, so without
// this they would shadow it. The long --version/--help still work, since only
// the short alias is contested.
static void test_dispatch_short_flag_shadowing(void) {
    static bool verbose, human;
    static cli_arg_t spec[] = {
        {.name = "verbose", .dest = &verbose, .type = &Bool$info, .short_flag = 'v'},
        {.name = "human", .dest = &human, .type = &Bool$info, .short_flag = 'h'},
    };
    static cli_spec_t cli;
    cli = (cli_spec_t){.name = "prog",
                       .version = "prog 9.9.9",
                       .version_short = 'v',
                       .root = {.spec_len = 2, .spec = spec, .handler = record_handler}};

    verbose = human = false;
    CHECK(DISPATCH_OF(&cli, "-v", "-h") == 0);
    CHECK_STR(ran_command, "(root)"); // i.e. it ran, rather than printing help/version
    CHECK(verbose && human);

    // The same applies when the short flag is claimed by a *global* flag:
    static bool global_verbose;
    static cli_arg_t global_spec[] = {{.name = "loud", .dest = &global_verbose, .type = &Bool$info, .short_flag = 'v'}};
    static cli_spec_t global_cli;
    static cli_arg_t empty_spec[1];
    global_cli = (cli_spec_t){.name = "prog",
                              .version = "prog 9.9.9",
                              .version_short = 'v',
                              .global_len = 1,
                              .global_spec = global_spec,
                              .root = {.spec_len = 0, .spec = empty_spec, .handler = record_handler}};
    global_verbose = false;
    CHECK(DISPATCH_OF(&global_cli, "-v") == 0);
    CHECK_STR(ran_command, "(root)");
    CHECK(global_verbose);

    // ...while the long versions, which nothing here contests, still work:
    CHECK_DISPATCH_OF(&cli, 0, "prog 9.9.9", "--version");
    CHECK_DISPATCH_OF(&cli, 0, "Usage: prog", "--help");
}

// Everything after the first bare "--" is handed to the handler untouched when
// the program asks for pass-through:
static void test_dispatch_passthrough(void) {
    test_cli_t *t = make_test_cli();
    t->cli.passthrough_after_double_dash = true;
    DISPATCH_OF(&t->cli, "add", "x", "--", "--not-my-flag", "-a");
    CHECK_STR(ran_command, "add");
    CHECK_STR(Text$as_c_string(t->add_file), "x");
    CHECK_STR(ran_extra_args, "--not-my-flag -a");
}

// after_globals runs once globals are popped, before any command is dispatched:
static int after_globals_calls = 0;
static void count_after_globals(void) {
    after_globals_calls++;
}

static void test_after_globals(void) {
    test_cli_t *t = make_test_cli();
    t->cli.after_globals = count_after_globals;
    after_globals_calls = 0;
    DISPATCH_OF(&t->cli, "add", "x");
    CHECK(after_globals_calls == 1);
}

// ---------------------------------------------------------------------------

int main(void) {
    // Keep the expected strings free of ANSI escapes:
    setenv("NO_COLOR", "1", 1);
    tomo_init();

    test_long_flags();
    test_short_flags();
    test_boolean_flags();
    test_scalar_types();
    test_optional_types();
    test_compound_types();
    test_optional_containers();
    test_optional_booleans();
    test_negative_numbers();
    test_positionals();
    test_help_flag();
    test_errors();
    test_usage_text();
    test_dispatch();
    test_dispatch_flag_shadowing();
    test_dispatch_short_flag_shadowing();
    test_dispatch_passthrough();
    test_after_globals();

    if (failures > 0) {
        fprintf(stderr, "%d of %d CLI checks FAILED\n", failures, checks);
        return 1;
    }
    printf("All %d CLI argument parsing checks passed.\n", checks);
    return 0;
}
