// Logic for the environmental context information during compilation
// (variable bindings, code sections, etc.)

#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>

#include "config.h"
#include "environment.h"
#include "naming.h"
#include "parse/files.h"
#include "stdlib/datatypes.h"
#include "stdlib/tables.h"
#include "stdlib/text.h"
#include "typecheck.h"
#include "util.h"

public
type_t *TEXT_TYPE = NULL;
public
type_t *PATH_TYPE = NULL;
public
type_t *PRESENT_TYPE = NULL;
public
type_t *RESULT_TYPE = NULL;

static type_t *declare_type(env_t *env, const char *def_str) {
    ast_t *ast = parse_file_str(def_str);
    if (!ast) errx(1, "Couldn't not parse struct def: %s", def_str);
    if (ast->tag != Block) errx(1, "Couldn't not parse struct def: %s", def_str);
    ast_list_t *statements = Match(ast, Block)->statements;
    if (statements == NULL || statements->next) errx(1, "Couldn't not parse struct def: %s", def_str);
    switch (statements->ast->tag) {
    case StructDef: {
        DeclareMatch(def, statements->ast, StructDef);
        prebind_statement(env, statements->ast);
        bind_statement(env, statements->ast);
        return Table$str_get(*env->types, def->name);
    }
    case EnumDef: {
        DeclareMatch(def, statements->ast, EnumDef);
        prebind_statement(env, statements->ast);
        bind_statement(env, statements->ast);
        return Table$str_get(*env->types, def->name);
    }
    default: errx(1, "Not a type definition: %s", def_str);
    }
    return NULL;
}

static type_t *bind_type(env_t *env, const char *name, type_t *type) {
    if (Table$str_get(*env->types, name)) errx(1, "Duplicate binding for type: %s", name);
    Table$str_set(env->types, name, type);
    return type;
}

env_t *global_env(bool source_mapping) {
    static env_t *_global_env = NULL;
    if (_global_env != NULL) return _global_env;

    env_t *env = new (env_t);
    env->code = new (compilation_unit_t);
    env->types = new (Table_t);
    env->globals = new (Table_t);
    env->locals = env->globals;
    env->imports = new (Table_t);

    env->build_info = new (Table_t);
    Table$str_set(env->build_info, "Tomo compiler version", TOMO_VERSION);
    Table$str_set(env->build_info, "Tomo compiler git", GIT_VERSION);
    Table$str_set(env->build_info, "Zig version", ZIG_VERSION);
    Table$str_set(env->build_info, "Boehm GC version", GC_VERSION);
    Table$str_set(env->build_info, "GMP version", GMP_VERSION);
    Table$str_set(env->build_info, "libunistring version", UNISTRING_VERSION);
    Table$str_set(env->build_info, "libbacktrace version", LIBBACKTRACE_VERSION);
    Table$str_set(env->build_info, "miniz version", MINIZ_VERSION);

    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    char *timestamp = GC_MALLOC_ATOMIC(32);
    strftime(timestamp, 32, "%Y-%m-%d %H:%M:%S %Z", tm);
    Table$str_set(env->build_info, "Binary compiled at", timestamp);

    env->do_source_mapping = source_mapping;

    TEXT_TYPE = bind_type(env, "Text", Type(TextType, .lang = "Text", .env = namespace_env(env, "Text")));
    (void)bind_type(env, "Int", Type(BigIntType));
    (void)bind_type(env, "Int32", Type(IntType, .bits = TYPE_IBITS32));
    (void)bind_type(env, "Memory", Type(MemoryType));
    PATH_TYPE = bind_type(env, "Path", Type(PathType));
    RESULT_TYPE = declare_type(env, "enum Result(Success, Failure(reason:Text))");

    PRESENT_TYPE = declare_type(env, "struct Present()");

    typedef struct {
        const char *name, *code, *type_str;
    } ns_entry_t;

#define MAKE_TYPE(name, type, type_name, type_info, ...)                                                               \
    {                                                                                                                  \
        name, type, type_name, type_info, TypedList(ns_entry_t, __VA_ARGS__)                                           \
    }
    struct {
        const char *name;
        type_t *type;
        Text_t typename;
        Text_t typeinfo;
        List_t namespace;
    } global_types[] = {
        MAKE_TYPE("Void", Type(VoidType), Text("void"), Text("Void$info")),
        MAKE_TYPE("Abort", Type(AbortType), Text("void"), Text("Abort$info")),
        MAKE_TYPE("Memory", Type(MemoryType), Text("void"), Text("Memory$info")),
        MAKE_TYPE("Present", PRESENT_TYPE, Text("Present$$type"), Text("Present$$info")),
        MAKE_TYPE("Result", RESULT_TYPE, Text("Result_t"), Text("Result$$info")),
        MAKE_TYPE( //
            "Bool", Type(BoolType), Text("Bool_t"), Text("Bool$info"),
            {"parse", "Bool$parse", "func(text:Text, remainder:&Text?=none -> Bool?)"}),
        MAKE_TYPE( //
            "Byte", Type(ByteType), Text("Byte_t"), Text("Byte$info"),
            {"get_bit", "Byte$get_bit", "func(x:Byte, bit_index:Int -> Bool)"}, //
            {"hex", "Byte$hex", "func(byte:Byte, uppercase=yes, prefix=no -> Text)"}, //
            {"is_between", "Byte$is_between", "func(x:Byte, a:Byte, b:Byte -> Bool)"}, //
            {"max", "Byte$max", "Byte"}, //
            {"min", "Byte$min", "Byte"}, //
            {"parse", "Byte$parse", "func(text:Text, base:Int?=none, remainder:&Text?=none -> Byte?)"}, //
            {"to", "Byte$to", "func(first:Byte, last:Byte, step:Int8?=none -> func(->Byte?))"}),
        MAKE_TYPE( //
            "Int", Type(BigIntType), Text("Int_t"), Text("Int$info"), {"abs", "Int$abs", "func(x:Int -> Int)"}, //
            {"bit_and", "Int$bit_and", "func(x,y:Int -> Int)"}, //
            {"bit_or", "Int$bit_or", "func(x,y:Int -> Int)"}, //
            {"bit_xor", "Int$bit_xor", "func(x,y:Int -> Int)"}, //
            {"choose", "Int$choose", "func(x,y:Int -> Int)"}, //
            {"clamped", "Int$clamped", "func(x,low,high:Int -> Int)"}, //
            {"divided_by", "Int$divided_by", "func(x,y:Int -> Int)"}, //
            {"factorial", "Int$factorial", "func(x:Int -> Int)"}, //
            {"gcd", "Int$gcd", "func(x,y:Int -> Int)"}, //
            {"get_bit", "Int$get_bit", "func(x,bit_index:Int -> Bool)"}, //
            {"hex", "Int$hex", "func(i:Int, digits=0, uppercase=yes, prefix=yes -> Text)"}, //
            {"is_between", "Int$is_between", "func(x:Int, a:Int, b:Int -> Bool)"}, //
            {"is_prime", "Int$is_prime", "func(x:Int,reps=50 -> Bool)"}, //
            {"left_shifted", "Int$left_shifted", "func(x,y:Int -> Int)"}, //
            {"minus", "Int$minus", "func(x,y:Int -> Int)"}, //
            {"modulo", "Int$modulo", "func(x,y:Int -> Int)"}, //
            {"modulo1", "Int$modulo1", "func(x,y:Int -> Int)"}, //
            {"negated", "Int$negated", "func(x:Int -> Int)"}, //
            {"negative", "Int$negative", "func(x:Int -> Int)"}, //
            {"next_prime", "Int$next_prime", "func(x:Int -> Int)"}, //
            {"octal", "Int$octal", "func(i:Int, digits=0, prefix=yes -> Text)"}, //
            {"onward", "Int$onward", "func(first:Int,step=1 -> func(->Int?))"}, //
            {"parse", "Int$parse", "func(text:Text, base:Int?=none, remainder:&Text?=none -> Int?)"}, //
            {"plus", "Int$plus", "func(x,y:Int -> Int)"}, //
            {"power", "Int$power", "func(base:Int,exponent:Int -> Int)"}, //
            {"right_shifted", "Int$right_shifted", "func(x,y:Int -> Int)"}, //
            {"sqrt", "Int$sqrt", "func(x:Int -> Int?)"}, //
            {"times", "Int$times", "func(x,y:Int -> Int)"}, //
            {"to", "Int$to", "func(first:Int,last:Int,step:Int?=none -> func(->Int?))"}),
        MAKE_TYPE( //
            "Int64", Type(IntType, .bits = TYPE_IBITS64), Text("Int64_t"), Text("Int64$info"),
            {"abs", "labs", "func(i:Int64 -> Int64)"}, //
            {"bits", "Int64$bits", "func(x:Int64 -> [Bool])"}, //
            {"clamped", "Int64$clamped", "func(x,low,high:Int64 -> Int64)"}, //
            {"divided_by", "Int64$divided_by", "func(x,y:Int64 -> Int64)"}, //
            {"gcd", "Int64$gcd", "func(x,y:Int64 -> Int64)"}, //
            {"parse", "Int64$parse", "func(text:Text, base:Int?=none, remainder:&Text?=none -> Int64?)"}, //
            {"get_bit", "Int64$get_bit", "func(x:Int64, bit_index:Int -> Bool)"}, //
            {"hex", "Int64$hex", "func(i:Int64, digits=0, uppercase=yes, prefix=yes -> Text)"}, //
            {"is_between", "Int64$is_between", "func(x:Int64, a:Int64, b:Int64 -> Bool)"}, //
            {"max", "Int64$max", "Int64"}, //
            {"min", "Int64$min", "Int64"}, //
            {"modulo", "Int64$modulo", "func(x,y:Int64 -> Int64)"}, //
            {"modulo1", "Int64$modulo1", "func(x,y:Int64 -> Int64)"}, //
            {"octal", "Int64$octal", "func(i:Int64, digits=0, prefix=yes -> Text)"}, //
            {"onward", "Int64$onward", "func(first:Int64,step=Int64(1) -> func(->Int64?))"}, //
            {"to", "Int64$to", "func(first:Int64,last:Int64,step:Int64?=none -> func(->Int64?))"}, //
            {"unsigned_left_shifted", "Int64$unsigned_left_shifted", "func(x:Int64,y:Int64 -> Int64)"}, //
            {"unsigned_right_shifted", "Int64$unsigned_right_shifted", "func(x:Int64,y:Int64 -> Int64)"}, //
            {"wrapping_minus", "Int64$wrapping_minus", "func(x:Int64,y:Int64 -> Int64)"}, //
            {"wrapping_plus", "Int64$wrapping_plus", "func(x:Int64,y:Int64 -> Int64)"}),
        MAKE_TYPE( //
            "Int32", Type(IntType, .bits = TYPE_IBITS32), Text("Int32_t"), Text("Int32$info"), //
            {"abs", "abs", "func(i:Int32 -> Int32)"}, //
            {"bits", "Int32$bits", "func(x:Int32 -> [Bool])"}, //
            {"clamped", "Int32$clamped", "func(x,low,high:Int32 -> Int32)"}, //
            {"divided_by", "Int32$divided_by", "func(x,y:Int32 -> Int32)"}, //
            {"gcd", "Int32$gcd", "func(x,y:Int32 -> Int32)"}, //
            {"parse", "Int32$parse", "func(text:Text, base:Int?=none, remainder:&Text?=none -> Int32?)"}, //
            {"get_bit", "Int32$get_bit", "func(x:Int32, bit_index:Int -> Bool)"}, //
            {"hex", "Int32$hex", "func(i:Int32, digits=0, uppercase=yes, prefix=yes -> Text)"}, //
            {"is_between", "Int32$is_between", "func(x:Int32, a:Int32, b:Int32 -> Bool)"}, //
            {"max", "Int32$max", "Int32"}, //
            {"min", "Int32$min", "Int32"}, //
            {"modulo", "Int32$modulo", "func(x,y:Int32 -> Int32)"}, //
            {"modulo1", "Int32$modulo1", "func(x,y:Int32 -> Int32)"}, //
            {"octal", "Int32$octal", "func(i:Int32, digits=0, prefix=yes -> Text)"}, //
            {"onward", "Int32$onward", "func(first:Int32,step=Int32(1) -> func(->Int32?))"}, //
            {"to", "Int32$to", "func(first:Int32,last:Int32,step:Int32?=none -> func(->Int32?))"}, //
            {"unsigned_left_shifted", "Int32$unsigned_left_shifted", "func(x:Int32,y:Int32 -> Int32)"}, //
            {"unsigned_right_shifted", "Int32$unsigned_right_shifted", "func(x:Int32,y:Int32 -> Int32)"}, //
            {"wrapping_minus", "Int32$wrapping_minus", "func(x:Int32,y:Int32 -> Int32)"}, //
            {"wrapping_plus", "Int32$wrapping_plus", "func(x:Int32,y:Int32 -> Int32)"}),
        MAKE_TYPE( //
            "Int16", Type(IntType, .bits = TYPE_IBITS16), Text("Int16_t"), Text("Int16$info"),
            {"abs", "abs", "func(i:Int16 -> Int16)"}, //
            {"bits", "Int16$bits", "func(x:Int16 -> [Bool])"}, //
            {"clamped", "Int16$clamped", "func(x,low,high:Int16 -> Int16)"}, //
            {"divided_by", "Int16$divided_by", "func(x,y:Int16 -> Int16)"}, //
            {"gcd", "Int16$gcd", "func(x,y:Int16 -> Int16)"}, //
            {"parse", "Int16$parse", "func(text:Text, base:Int?=none, remainder:&Text?=none -> Int16?)"}, //
            {"get_bit", "Int16$get_bit", "func(x:Int16, bit_index:Int -> Bool)"}, //
            {"hex", "Int16$hex", "func(i:Int16, digits=0, uppercase=yes, prefix=yes -> Text)"}, //
            {"is_between", "Int16$is_between", "func(x:Int16, a:Int16, b:Int16 -> Bool)"}, //
            {"max", "Int16$max", "Int16"}, //
            {"min", "Int16$min", "Int16"}, //
            {"modulo", "Int16$modulo", "func(x,y:Int16 -> Int16)"}, //
            {"modulo1", "Int16$modulo1", "func(x,y:Int16 -> Int16)"}, //
            {"octal", "Int16$octal", "func(i:Int16, digits=0, prefix=yes -> Text)"}, //
            {"onward", "Int16$onward", "func(first:Int16,step=Int16(1) -> func(->Int16?))"}, //
            {"to", "Int16$to", "func(first:Int16,last:Int16,step:Int16?=none -> func(->Int16?))"}, //
            {"unsigned_left_shifted", "Int16$unsigned_left_shifted", "func(x:Int16,y:Int16 -> Int16)"}, //
            {"unsigned_right_shifted", "Int16$unsigned_right_shifted", "func(x:Int16,y:Int16 -> Int16)"}, //
            {"wrapping_minus", "Int16$wrapping_minus", "func(x:Int16,y:Int16 -> Int16)"}, //
            {"wrapping_plus", "Int16$wrapping_plus", "func(x:Int16,y:Int16 -> Int16)"}),
        MAKE_TYPE( //
            "Int8", Type(IntType, .bits = TYPE_IBITS8), Text("Int8_t"), Text("Int8$info"),
            {"abs", "abs", "func(i:Int8 -> Int8)"}, //
            {"bits", "Int8$bits", "func(x:Int8 -> [Bool])"}, //
            {"clamped", "Int8$clamped", "func(x,low,high:Int8 -> Int8)"}, //
            {"divided_by", "Int8$divided_by", "func(x,y:Int8 -> Int8)"}, //
            {"gcd", "Int8$gcd", "func(x,y:Int8 -> Int8)"}, //
            {"parse", "Int8$parse", "func(text:Text, base:Int?=none, remainder:&Text?=none -> Int8?)"}, //
            {"get_bit", "Int8$get_bit", "func(x:Int8, bit_index:Int -> Bool)"}, //
            {"hex", "Int8$hex", "func(i:Int8, digits=0, uppercase=yes, prefix=yes -> Text)"}, //
            {"is_between", "Int8$is_between", "func(x:Int8, a:Int8, b:Int8 -> Bool)"}, //
            {"max", "Int8$max", "Int8"}, //
            {"min", "Int8$min", "Int8"}, //
            {"modulo", "Int8$modulo", "func(x,y:Int8 -> Int8)"}, //
            {"modulo1", "Int8$modulo1", "func(x,y:Int8 -> Int8)"}, //
            {"octal", "Int8$octal", "func(i:Int8, digits=0, prefix=yes -> Text)"}, //
            {"onward", "Int8$onward", "func(first:Int8,step=Int8(1) -> func(->Int8?))"}, //
            {"to", "Int8$to", "func(first:Int8,last:Int8,step:Int8?=none -> func(->Int8?))"}, //
            {"unsigned_left_shifted", "Int8$unsigned_left_shifted", "func(x:Int8,y:Int8 -> Int8)"}, //
            {"unsigned_right_shifted", "Int8$unsigned_right_shifted", "func(x:Int8,y:Int8 -> Int8)"}, //
            {"wrapping_minus", "Int8$wrapping_minus", "func(x:Int8,y:Int8 -> Int8)"}, //
            {"wrapping_plus", "Int8$wrapping_plus", "func(x:Int8,y:Int8 -> Int8)"}, ),
#define C(name) {#name, "M_" #name, "Num"}
#define F(name) {#name, #name, "func(n:Num -> Num)"}
#define F_opt(name) {#name, #name, "func(n:Num -> Num?)"}
#define F2(name) {#name, #name, "func(x,y:Num -> Num)"}
        MAKE_TYPE( //
            "Num", Type(NumType, .bits = TYPE_NBITS64), Text("Num_t"), Text("Num$info"),
            {"near", "Num$near", "func(x,y:Num, ratio=1e-9, min_epsilon=1e-9 -> Bool)"}, //
            {"clamped", "Num$clamped", "func(x,low,high:Num -> Num)"}, //
            {"percent", "Num$percent", "func(n:Num,precision=0.01 -> Text)"}, //
            {"with_precision", "Num$with_precision", "func(n:Num,precision:Num -> Num)"}, //
            {"is_between", "Num$is_between", "func(x:Num, a:Num, b:Num -> Bool)"}, //
            {"isinf", "Num$isinf", "func(n:Num -> Bool)"}, //
            {"isfinite", "Num$finite", "func(n:Num -> Bool)"}, //
            {"modulo", "Num$mod", "func(x,y:Num -> Num)"}, //
            {"modulo1", "Num$mod1", "func(x,y:Num -> Num)"}, //
            C(2_SQRTPI), C(E), C(PI_2), C(2_PI), C(1_PI), C(LN10), C(LN2), C(LOG2E), C(PI), C(PI_4), C(SQRT2),
            C(SQRT1_2), {"INF", "(Num_t)(INFINITY)", "Num"}, //
            {"TAU", "(Num_t)(2.*M_PI)", "Num"}, //
            {"mix", "Num$mix", "func(amount,x,y:Num -> Num)"}, //
            {"parse", "Num$parse", "func(text:Text, remainder:&Text?=none -> Num?)"}, //
            {"abs", "fabs", "func(n:Num -> Num)"}, //
            F_opt(acos), F_opt(acosh), F_opt(asin), F(asinh), F(atan), F_opt(atanh), F(cbrt), F(ceil), F_opt(cos),
            F(cosh), F(erf), F(erfc), F(exp), F(exp2), F(expm1), F(floor), F(j0), F(j1), F_opt(log), F_opt(log10),
            F_opt(log1p), F_opt(log2), F(logb), F(rint), F(round), F(significand), F_opt(sin), F(sinh), F_opt(sqrt),
            F_opt(tan), F(tanh), F_opt(tgamma), F(trunc), F_opt(y0), F_opt(y1), F2(atan2), F2(copysign), F2(fdim),
            F2(hypot), F2(nextafter)),
#undef F2
#undef F_opt
#undef F
#undef C
#define C(name) {#name, "(Num32_t)(M_" #name ")", "Num32"}
#define F(name) {#name, #name "f", "func(n:Num32 -> Num32)"}
#define F_opt(name) {#name, #name "f", "func(n:Num32 -> Num32?)"}
#define F2(name) {#name, #name "f", "func(x,y:Num32 -> Num32)"}
        MAKE_TYPE( //
            "Num32", Type(NumType, .bits = TYPE_NBITS32), Text("Num32_t"), Text("Num32$info"), //
            {"near", "Num32$near", "func(x,y:Num32, ratio=Num32(1e-9), min_epsilon=Num32(1e-9) -> Bool)"}, //
            {"clamped", "Num32$clamped", "func(x,low,high:Num32 -> Num32)"}, //
            {"percent", "Num32$percent", "func(n:Num32,precision=Num32(.01) -> Text)"}, //
            {"with_precision", "Num32$with_precision", "func(n:Num32,precision:Num32 -> Num32)"}, //
            {"is_between", "Num32$is_between", "func(x:Num32, a:Num32, b:Num32 -> Bool)"}, //
            {"isinf", "Num32$isinf", "func(n:Num32 -> Bool)"}, //
            {"isfinite", "Num32$isfinite", "func(n:Num32 -> Bool)"}, //
            C(2_SQRTPI), C(E), C(PI_2), C(2_PI), C(1_PI), C(LN10), C(LN2), C(LOG2E), C(PI), C(PI_4), C(SQRT2),
            C(SQRT1_2), //
            {"INF", "(Num32_t)(INFINITY)", "Num32"}, //
            {"TAU", "(Num32_t)(2.f*M_PI)", "Num32"}, //
            {"mix", "Num32$mix", "func(amount,x,y:Num32 -> Num32)"}, //
            {"parse", "Num32$parse", "func(text:Text, remainder:&Text?=none -> Num32?)"}, //
            {"abs", "fabsf", "func(n:Num32 -> Num32)"}, //
            {"modulo", "Num32$mod", "func(x,y:Num32 -> Num32)"}, //
            {"modulo1", "Num32$mod1", "func(x,y:Num32 -> Num32)"}, //
            F_opt(acos), F_opt(acosh), F_opt(asin), F(asinh), F(atan), F_opt(atanh), F(cbrt), F(ceil), F_opt(cos),
            F(cosh), F(erf), F(erfc), F(exp), F(exp2), F(expm1), F(floor), F(j0), F(j1), F_opt(log), F_opt(log10),
            F_opt(log1p), F_opt(log2), F(logb), F(rint), F(round), F(significand), F_opt(sin), F(sinh), F_opt(sqrt),
            F_opt(tan), F(tanh), F_opt(tgamma), F(trunc), F_opt(y0), F_opt(y1), F2(atan2), F2(copysign), F2(fdim),
            F2(hypot), F2(nextafter)),
        MAKE_TYPE( //
            "CString", Type(CStringType), Text("char*"), Text("CString$info"), //
            {"as_text", "Text$from_str", "func(str:CString -> Text)"},
            {"join", "CString$join", "func(glue:CString, pieces:[CString] -> CString)"}),
#undef F2
#undef F_opt
#undef F
#undef C
        MAKE_TYPE( //
            "Path", PATH_TYPE, Text("Path_t"), Text("Path$info"), //
            {"accessed", "Path$accessed", "func(path:Path, follow_symlinks=yes -> Int64?)"}, //
            {"append", "Path$append", "func(path:Path, text:Text, permissions=Int32(0o644) -> Result)"}, //
            {"append_bytes", "Path$append_bytes",
             "func(path:Path, bytes:[Byte], permissions=Int32(0o644) -> Result)"}, //
            {"base_name", "Path$base_name", "func(path:Path -> Text)"}, //
            {"by_line", "Path$by_line", "func(path:Path -> func(->Text?)?)"}, //
            {"can_execute", "Path$can_execute", "func(path:Path -> Bool)"}, //
            {"can_read", "Path$can_read", "func(path:Path -> Bool)"}, //
            {"can_write", "Path$can_write", "func(path:Path -> Bool)"}, //
            {"changed", "Path$changed", "func(path:Path, follow_symlinks=yes -> Int64?)"}, //
            {"child", "Path$child", "func(path:Path, child:Text -> Path)"}, //
            {"children", "Path$children", "func(path:Path, include_hidden=no -> [Path])"}, //
            {"concatenated_with", "Path$concat", "func(a,b:Path -> Path)"}, //
            {"components", "Path$components", "func(path:Path -> [Text])"}, //
            {"copy_to", "Path$copy_to", "func(path:Path, dest:Path, overwrite=no -> Result)"}, //
            {"create_directory", "Path$create_directory",
             "func(path:Path, permissions=Int32(0o755), recursive=yes -> Result)"}, //
            {"current_dir", "Path$current_dir", "func(->Path)"}, //
            {"each_child", "Path$each_child", "func(path:Path, include_hidden=no -> func(->Path?))"}, //
            {"exists", "Path$exists", "func(path:Path -> Bool)"}, //
            {"expand_home", "Path$expand_home", "func(path:Path -> Path)"}, //
            {"extension", "Path$extension", "func(path:Path, full=yes -> Text)"}, //
            {"files", "Path$children", "func(path:Path, include_hidden=no -> [Path])"}, //
            {"glob", "Path$glob", "func(path:Path -> [Path])"}, //
            {"group", "Path$group", "func(path:Path, follow_symlinks=yes -> Text?)"}, //
            {"has_extension", "Path$has_extension", "func(path:Path, extension:Text -> Bool)"}, //
            {"is_directory", "Path$is_directory", "func(path:Path, follow_symlinks=yes -> Bool)"}, //
            {"is_file", "Path$is_file", "func(path:Path, follow_symlinks=yes -> Bool)"}, //
            {"is_pipe", "Path$is_pipe", "func(path:Path, follow_symlinks=yes -> Bool)"}, //
            {"is_socket", "Path$is_socket", "func(path:Path, follow_symlinks=yes -> Bool)"}, //
            {"is_symlink", "Path$is_symlink", "func(path:Path -> Bool)"}, //
            {"lines", "Path$lines", "func(path:Path -> [Text]?)"}, //
            {"matches_glob", "Path$matches_glob", "func(path:Path, glob:Text -> Bool)"}, //
            {"modified", "Path$modified", "func(path:Path, follow_symlinks=yes -> Int64?)"}, //
            {"move", "Path$move", "func(path:Path, dest:Path, overwrite=no -> Result)"}, //
            {"owner", "Path$owner", "func(path:Path, follow_symlinks=yes -> Text?)"}, //
            {"parent", "Path$parent", "func(path:Path -> Path?)"}, //
            {"read", "Path$read", "func(path:Path -> Text?)"}, //
            {"read_bytes", "Path$read_bytes", "func(path:Path, limit:Int?=none -> [Byte]?)"}, //
            {"relative_to", "Path$relative_to", "func(path:Path, relative_to:Path -> Path)"}, //
            {"remove", "Path$remove", "func(path:Path, ignore_missing=no -> Result)"}, //
            {"resolved", "Path$resolved", "func(path:Path, relative_to=(./) -> Path)"}, //
            {"set_owner", "Path$set_owner",
             "func(path:Path, owner:Text?=none, group:Text?=none, follow_symlinks=yes -> Result)"}, //
            {"sibling", "Path$sibling", "func(path:Path, name:Text -> Path)"}, //
            {"subdirectories", "Path$children", "func(path:Path, include_hidden=no -> [Path])"}, //
            {"unique_directory", "Path$unique_directory", "func(path:Path -> Path)"}, //
            {"walk", "Path$walk", "func(path:Path, include_hidden=no, follow_symlinks=no -> func(->Path?))"}, //
            {"with_extension", "Path$with_extension", "func(path:Path, extension:Text, replace:Bool=yes -> Path)"}, //
            {"write", "Path$write", "func(path:Path, text:Text, permissions=Int32(0o644) -> Result)"}, //
            {"writer", "Path$writer",
             "func(path:Path, append=no, permissions=Int32(0o644) -> func(text:Text, close=no -> Result))"}, //
            {"byte_writer", "Path$byte_writer",
             "func(path:Path, append=no, permissions=Int32(0o644) -> func(bytes:[Byte], close=no -> Result))"}, //
            {"write_bytes", "Path$write_bytes", "func(path:Path, bytes:[Byte], permissions=Int32(0o644) -> Result)"}, //
            {"write_unique", "Path$write_unique", "func(path:Path, text:Text -> Path?)"}, //
            {"write_unique_bytes", "Path$write_unique_bytes", "func(path:Path, bytes:[Byte] -> Path?)"}),
        MAKE_TYPE( //
            "Text", TEXT_TYPE, Text("Text_t"), Text("Text$info"), //
            {"as_c_string", "Text$as_c_string", "func(text:Text -> CString)"}, //
            {"at", "Text$cluster", "func(text:Text, index:Int -> Text)"}, //
            {"by_line", "Text$by_line", "func(text:Text -> func(->Text?))"}, //
            {"by_split", "Text$by_split", "func(text:Text, delimiter='' -> func(->Text?))"}, //
            {"by_split_any", "Text$by_split_any", "func(text:Text, delimiters=' \\t\\r\\n' -> func(->Text?))"}, //
            {"caseless_equals", "Text$equal_ignoring_case", "func(a,b:Text, language='C' -> Bool)"}, //
            {"codepoint_names", "Text$codepoint_names", "func(text:Text -> [Text])"}, //
            {"distance", "Text$distance", "func(a,b:Text, language='C' -> Num)"}, //
            {"ends_with", "Text$ends_with", "func(text,suffix:Text, remainder:&Text? = none -> Bool)"}, //
            {"find", "Text$find", "func(text,target:Text, start=1 -> Int?)"}, //
            {"from", "Text$from", "func(text:Text, first:Int -> Text)"}, //
            {"from_c_string", "Text$from_str", "func(str:CString -> Text?)"}, //
            {"from_codepoint_names", "Text$from_codepoint_names", "func(codepoint_names:[Text] -> Text?)"}, //
            {"from_text", "Path$from_text", "func(text:Text -> Path)"}, //
            {"from_utf8", "Text$from_utf8", "func(bytes:[Byte] -> Text?)"}, //
            {"from_utf16", "Text$from_utf16", "func(codepoints:[Int16] -> Text?)"}, //
            {"from_utf32", "Text$from_utf32", "func(codepoints:[Int32] -> Text?)"}, //
            {"has", "Text$has", "func(text:Text, target:Text -> Bool)"}, //
            {"join", "Text$join", "func(glue:Text, pieces:[Text] -> Text)"}, //
            {"layout", "Text$layout", "func(text:Text -> Text)"}, //
            {"left_pad", "Text$left_pad", "func(text:Text, count:Int, pad=' ', language='C' -> Text)"}, //
            {"lines", "Text$lines", "func(text:Text -> [Text])"}, //
            {"lower", "Text$lower", "func(text:Text, language='C' -> Text)"}, //
            {"matches_glob", "Text$matches_glob", "func(text:Text, glob:Text -> Bool)"}, //
            {"memory_size", "Text$memory_size", "func(text:Text -> Int)"}, //
            {"middle_pad", "Text$middle_pad", "func(text:Text, count:Int, pad=' ', language='C' -> Text)"}, //
            {"quoted", "Text$quoted", "func(text:Text, color=no, quotation_mark='\"' -> Text)"}, //
            {"repeat", "Text$repeat", "func(text:Text, count:Int -> Text)"}, //
            {"replace", "Text$replace", "func(text:Text, target:Text, replacement:Text -> Text)"}, //
            {"reversed", "Text$reversed", "func(text:Text -> Text)"}, //
            {"right_pad", "Text$right_pad", "func(text:Text, count:Int, pad=' ', language='C' -> Text)"}, //
            {"slice", "Text$slice", "func(text:Text, from=1, to=-1 -> Text)"}, //
            {"split", "Text$split", "func(text:Text, delimiter='' -> [Text])"}, //
            {"split_any", "Text$split_any", "func(text:Text, delimiters=' \\t\\r\\n' -> [Text])"}, //
            {"starts_with", "Text$starts_with", "func(text,prefix:Text, remainder:&Text? = none -> Bool)"}, //
            {"title", "Text$title", "func(text:Text, language='C' -> Text)"}, //
            {"to", "Text$to", "func(text:Text, last:Int -> Text)"}, //
            {"translate", "Text$translate", "func(text:Text, translations:{Text:Text} -> Text)"}, //
            {"trim", "Text$trim", "func(text:Text, to_trim=\" \\t\\r\\n\", left=yes, right=yes -> Text)"}, //
            {"upper", "Text$upper", "func(text:Text, language='C' -> Text)"}, //
            {"utf8", "Text$utf8", "func(text:Text -> [Byte])"}, //
            {"utf16", "Text$utf16", "func(text:Text -> [Int16])"}, //
            {"utf32", "Text$utf32", "func(text:Text -> [Int32])"}, //
            {"width", "Text$width", "func(text:Text, language='C' -> Int)"}, //
            {"without_prefix", "Text$without_prefix", "func(text,prefix:Text -> Text)"}, //
            {"without_suffix", "Text$without_suffix", "func(text,suffix:Text -> Text)"}),
    };
#undef MAKE_TYPE

    for (size_t i = 0; i < sizeof(global_types) / sizeof(global_types[0]); i++) {
        env_t *ns_env = NULL;
        switch (global_types[i].type->tag) {
        case TextType: ns_env = Match(global_types[i].type, TextType)->env; break;
        case StructType: ns_env = Match(global_types[i].type, StructType)->env; break;
        case EnumType: ns_env = Match(global_types[i].type, EnumType)->env; break;
        default: break;
        }
        if (ns_env == NULL) ns_env = namespace_env(env, global_types[i].name);
        binding_t *binding =
            new (binding_t,
                 .type = Type(TypeInfoType, .name = global_types[i].name, .type = global_types[i].type, .env = ns_env),
                 .code = global_types[i].typeinfo);
        Table$str_set(env->globals, global_types[i].name, binding);
        Table$str_set(env->types, global_types[i].name, global_types[i].type);
    }

    for (size_t i = 0; i < sizeof(global_types) / sizeof(global_types[0]); i++) {
        binding_t *type_binding = Table$str_get(*env->globals, global_types[i].name);
        assert(type_binding);
        env_t *ns_env = Match(type_binding->type, TypeInfoType)->env;
        for (int64_t j = 0; j < (int64_t)global_types[i].namespace.length; j++) {
            ns_entry_t *entry = global_types[i].namespace.data + j * global_types[i].namespace.stride;
            type_t *type = parse_type_string(ns_env, entry->type_str);
            if (!type) compiler_err(NULL, NULL, NULL, "Couldn't parse type string: ", entry->type_str);
            if (type->tag == ClosureType) type = Match(type, ClosureType)->fn;
            set_binding(ns_env, entry->name, type, Text$from_str(entry->code));
        }
    }

// Conversion constructors:
#define ADD_CONSTRUCTORS(type_name, ...)                                                                               \
    do {                                                                                                               \
        env_t *ns_env = namespace_env(env, type_name);                                                                 \
        struct {                                                                                                       \
            const char *c_name, *type_str;                                                                             \
        } constructor_infos[] = {__VA_ARGS__};                                                                         \
        for (size_t i = 0; i < sizeof(constructor_infos) / sizeof(constructor_infos[0]); i++) {                        \
            type_t *t = parse_type_string(ns_env, constructor_infos[i].type_str);                                      \
            List$insert(&ns_env->namespace->constructors,                                                              \
                        ((binding_t[1]){                                                                               \
                            {.code = Text$from_str(constructor_infos[i].c_name), .type = Match(t, ClosureType)->fn}}), \
                        I(0), sizeof(binding_t));                                                                      \
        }                                                                                                              \
    } while (0)

    ADD_CONSTRUCTORS("Bool", //
                     {"Bool$from_byte", "func(b:Byte -> Bool)"}, //
                     {"Bool$from_int8", "func(i:Int8 -> Bool)"}, //
                     {"Bool$from_int16", "func(i:Int16 -> Bool)"}, //
                     {"Bool$from_int32", "func(i:Int32 -> Bool)"}, //
                     {"Bool$from_int64", "func(i:Int64 -> Bool)"}, //
                     {"Bool$from_int", "func(i:Int -> Bool)"});
    ADD_CONSTRUCTORS("Byte", //
                     {"Byte$from_bool", "func(b:Bool -> Byte)"}, //
                     {"Byte$from_int8", "func(i:Int8 -> Byte)"}, //
                     {"Byte$from_int16", "func(i:Int16, truncate=no -> Byte)"}, //
                     {"Byte$from_int32", "func(i:Int32, truncate=no -> Byte)"}, //
                     {"Byte$from_int64", "func(i:Int64, truncate=no -> Byte)"}, //
                     {"Byte$from_int", "func(i:Int, truncate=no -> Byte)"});
    ADD_CONSTRUCTORS("Int", //
                     {"Int$from_bool", "func(b:Bool -> Int)"}, //
                     {"Int$from_byte", "func(b:Byte -> Int)"}, //
                     {"Int$from_int8", "func(i:Int8 -> Int)"}, //
                     {"Int$from_int16", "func(i:Int16 -> Int)"}, //
                     {"Int$from_int32", "func(i:Int32 -> Int)"}, //
                     {"Int$from_int64", "func(i:Int64 -> Int)"}, //
                     {"Int$from_num64", "func(n:Num, truncate=no -> Int)"}, //
                     {"Int$from_num32", "func(n:Num32, truncate=no -> Int)"});
    ADD_CONSTRUCTORS("Int64", //
                     {"Int64$from_bool", "func(b:Bool -> Int64)"}, //
                     {"Int64$from_byte", "func(b:Byte -> Int64)"}, //
                     {"Int64$from_int8", "func(i:Int8 -> Int64)"}, //
                     {"Int64$from_int16", "func(i:Int16 -> Int64)"}, //
                     {"Int64$from_int32", "func(i:Int32 -> Int64)"}, //
                     {"Int64$from_int", "func(i:Int, truncate=no -> Int64)"}, //
                     {"Int64$from_num64", "func(n:Num, truncate=no -> Int64)"}, //
                     {"Int64$from_num32", "func(n:Num32, truncate=no -> Int64)"});
    ADD_CONSTRUCTORS("Int32", //
                     {"Int32$from_bool", "func(b:Bool -> Int32)"}, //
                     {"Int32$from_byte", "func(b:Byte -> Int32)"}, //
                     {"Int32$from_int8", "func(i:Int8 -> Int32)"}, //
                     {"Int32$from_int16", "func(i:Int16 -> Int32)"}, //
                     {"Int32$from_int64", "func(i:Int64, truncate=no -> Int32)"}, //
                     {"Int32$from_int", "func(i:Int, truncate=no -> Int32)"}, //
                     {"Int32$from_num64", "func(n:Num, truncate=no -> Int32)"}, //
                     {"Int32$from_num32", "func(n:Num32, truncate=no -> Int32)"});
    ADD_CONSTRUCTORS("Int16", //
                     {"Int16$from_bool", "func(b:Bool -> Int16)"}, //
                     {"Int16$from_byte", "func(b:Byte -> Int16)"}, //
                     {"Int16$from_int8", "func(i:Int8 -> Int16)"}, //
                     {"Int16$from_int32", "func(i:Int32, truncate=no -> Int16)"}, //
                     {"Int16$from_int64", "func(i:Int64, truncate=no -> Int16)"}, //
                     {"Int16$from_int", "func(i:Int, truncate=no -> Int16)"}, //
                     {"Int16$from_num64", "func(n:Num, truncate=no -> Int16)"}, //
                     {"Int16$from_num32", "func(n:Num32, truncate=no -> Int16)"});
    ADD_CONSTRUCTORS("Int8", //
                     {"Int8$from_bool", "func(b:Bool -> Int8)"}, //
                     {"Int8$from_byte", "func(b:Byte -> Int8)"}, //
                     {"Int8$from_int16", "func(i:Int16, truncate=no -> Int8)"}, //
                     {"Int8$from_int32", "func(i:Int32, truncate=no -> Int8)"}, //
                     {"Int8$from_int64", "func(i:Int64, truncate=no -> Int8)"}, //
                     {"Int8$from_int", "func(i:Int, truncate=no -> Int8)"}, //
                     {"Int8$from_num64", "func(n:Num, truncate=no -> Int8)"}, //
                     {"Int8$from_num32", "func(n:Num32, truncate=no -> Int8)"});
    ADD_CONSTRUCTORS("Num", //
                     {"Num$from_bool", "func(b:Bool -> Num)"}, //
                     {"Num$from_byte", "func(b:Byte -> Num)"}, //
                     {"Num$from_int8", "func(i:Int8 -> Num)"}, //
                     {"Num$from_int16", "func(i:Int16 -> Num)"}, //
                     {"Num$from_int32", "func(i:Int32 -> Num)"}, //
                     {"Num$from_int64", "func(i:Int64, truncate=no -> Num)"}, //
                     {"Num$from_int", "func(i:Int, truncate=no -> Num)"}, //
                     {"Num$from_num32", "func(n:Num32 -> Num)"});
    ADD_CONSTRUCTORS("Num32", //
                     {"Num32$from_bool", "func(b:Bool -> Num32)"}, //
                     {"Num32$from_byte", "func(b:Byte -> Num32)"}, //
                     {"Num32$from_int8", "func(i:Int8 -> Num32)"}, //
                     {"Num32$from_int16", "func(i:Int16 -> Num32)"}, //
                     {"Num32$from_int32", "func(i:Int32, truncate=no -> Num32)"}, //
                     {"Num32$from_int64", "func(i:Int64, truncate=no -> Num32)"}, //
                     {"Num32$from_int", "func(i:Int, truncate=no -> Num32)"}, //
                     {"Num32$from_num64", "func(n:Num -> Num32)"});
    ADD_CONSTRUCTORS("Path", //
                     {"Path$from_text", "func(text:Text -> Path)"}, //
                     {"Path$escape_path", "func(path:Path -> Path)"}, //
                     {"Int$value_as_text", "func(i:Int -> Path)"});
    ADD_CONSTRUCTORS("CString", //
                     {"Text$as_c_string", "func(text:Text -> CString)"});
    ADD_CONSTRUCTORS("Text", //
                     {"Text$from_str", "func(str:CString -> Text)"}, //
                     {"Text$from_str", "func(path:Path -> Text)"});
#undef ADD_CONSTRUCTORS

    set_binding(namespace_env(env, "Path"), "from_text",
                NewFunctionType(PATH_TYPE, {.name = "text", .type = TEXT_TYPE}), Text("Path$from_text"));

    struct {
        const char *name, *code, *type_str;
    } global_vars[] = {
        {"PRESENT", "PRESENT", "Present"},
        {"TOMO_VERSION", "TOMO_VERSION_TEXT", "Text"},
        {"USE_COLOR", "USE_COLOR", "Bool"},
        {"ask", "ask", "func(prompt:Text, bold=yes, force_tty=yes -> Text?)"},
        {"at_cleanup", "tomo_at_cleanup", "func(fn:func())"},
        {"exit", "tomo_exit", "func(message:Text?=none, code=Int32(1) -> Abort)"},
        {"fail", "fail_text", "func(message:Text -> Abort)"},
        {"getenv", "getenv_text", "func(name:Text -> Text?)"},
        {"print", "say", "func(text:Text, newline=yes)"},
        {"say", "say", "func(text:Text, newline=yes)"},
        {"setenv", "setenv_text", "func(name:Text, value:Text?)"},
        {"sleep", "sleep_seconds", "func(seconds:Num)"},
    };

    for (size_t i = 0; i < sizeof(global_vars) / sizeof(global_vars[0]); i++) {
        type_t *type = parse_type_string(env, global_vars[i].type_str);
        if (!type)
            compiler_err(NULL, NULL, NULL, "Couldn't parse type string for ", global_vars[i].name, ": ",
                         global_vars[i].type_str);
        if (type->tag == ClosureType) type = Match(type, ClosureType)->fn;
        Table$str_set(env->globals, global_vars[i].name,
                      new (binding_t, .type = type, .code = Text$from_str(global_vars[i].code)));
    }

    _global_env = env;
    return env;
}

env_t *load_module_env(env_t *env, ast_t *ast) {
    const char *name = ast->file->filename;
    env_t *cached = Table$str_get(*env->imports, name);
    if (cached) return cached;
    env_t *module_env = fresh_scope(env);
    module_env->code = new (compilation_unit_t);
    module_env->namespace_bindings = module_env->locals;
    module_env->id_suffix = get_id_suffix(ast->file->filename);

    Table$str_set(module_env->imports, name, module_env);

    ast_list_t *statements = Match(ast, Block)->statements;
    visit_topologically(statements, (Closure_t){.fn = (void *)prebind_statement, .userdata = module_env});
    visit_topologically(statements, (Closure_t){.fn = (void *)bind_statement, .userdata = module_env});

    return module_env;
}

env_t *fresh_scope(env_t *env) {
    env_t *scope = new (env_t);
    *scope = *env;
    scope->locals = new (Table_t, .fallback = env->locals);
    scope->types = new (Table_t, .fallback = env->types);
    return scope;
}

env_t *with_enum_scope(env_t *env, type_t *t) {
    while (t && t->tag == OptionalType)
        t = Match(t, OptionalType)->type;

    if (t == NULL || t->tag != EnumType) return env;
    env = fresh_scope(env);
    env_t *ns_env = Match(t, EnumType)->env;
    for (tag_t *tag = Match(t, EnumType)->tags; tag; tag = tag->next) {
        if (get_binding(env, tag->name)) continue;
        binding_t *b = get_binding(ns_env, tag->name);
        assert(b);
        Table$str_set(env->locals, tag->name, b);
    }
    return env;
}

public
arg_t *iterator_yield_args(type_t *iter_value_t) {
    type_t *fn_t = iter_value_t;
    if (fn_t->tag == ClosureType) fn_t = Match(fn_t, ClosureType)->fn;
    if (fn_t->tag != FunctionType) return NULL;
    DeclareMatch(fn, fn_t, FunctionType);
    if (!fn->ret || fn->ret->tag != BoolType) return NULL;
    if (!fn->args) return NULL;
    for (arg_t *arg = fn->args; arg; arg = arg->next) {
        if (arg->type->tag != PointerType || !Match(arg->type, PointerType)->is_stack || arg->default_val) return NULL;
    }
    return fn->args;
}

// One entry per value that a single iterable yields per iteration: a list,
// count, range, or text yields 1, a table yields 2 (key, value), and a
// multi-value iterator function yields one per `&` out-parameter. Used to
// distribute the variables of a lockstep loop (`for x, y in xs, ys`) among its
// iterables and to enforce a strict arity match.
public
arg_t *iteration_slots(env_t *env, ast_t *iter_ast) {
    type_t *iter_t = value_type(get_type(env, iter_ast));
    switch (iter_t->tag) {
    case ListType: return new (arg_t, .name = "item", .type = Match(iter_t, ListType)->item_type);
    case TableType:
        return new (arg_t, .name = "key", .type = Match(iter_t, TableType)->key_type,
                    .next = new (arg_t, .name = "value", .type = Match(iter_t, TableType)->value_type));
    case BigIntType:
    case IntType: return new (arg_t, .name = "count", .type = iter_t);
    case TextType: return new (arg_t, .name = "grapheme", .type = TEXT_TYPE);
    case FunctionType:
    case ClosureType: {
        arg_t *yields = iterator_yield_args(iter_t);
        if (yields) {
            arg_t *slots = NULL;
            for (arg_t *a = yields; a; a = a->next)
                slots = new (arg_t, .name = a->name, .type = Match(a->type, PointerType)->pointed, .next = slots);
            REVERSE_LIST(slots);
            return slots;
        }
        type_t *ret = iter_t->tag == ClosureType ? Match(Match(iter_t, ClosureType)->fn, FunctionType)->ret
                                                 : Match(iter_t, FunctionType)->ret;
        return new (arg_t, .name = "item",
                    .type = ret->tag == OptionalType ? Match(ret, OptionalType)->type : ret);
    }
    default: code_err(iter_ast, "Iteration is not implemented for type: ", type_to_text(iter_t));
    }
}

public
ast_t *single_loop_var(ast_list_t *vars) {
    if (!vars) return NULL;
    if (vars->next)
        code_err(vars->next->ast, "This is too many variables for this loop: this iterable only yields one value "
                                  "per iteration. To bind an iteration counter, use `at` (e.g. `for x at i in ...`).");
    return vars->ast;
}

env_t *for_scope(env_t *env, ast_t *ast) {
    DeclareMatch(for_, ast, For);
    ast_t *iter_ast = for_->iters->ast;
    type_t *raw_iter_t = get_type(env, iter_ast);
    type_t *iter_t = value_type(raw_iter_t);
    env_t *scope = fresh_scope(env);

    // The `at` variable is an Int64 iteration counter (1, 2, 3, ...) for any
    // kind of iteration:
    if (for_->at)
        set_binding(scope, Match(for_->at, Var)->name, INT64_TYPE, Texts("_$", Match(for_->at, Var)->name));

    // Lockstep iteration (`for x, y in xs, ys`): every iterable's yielded
    // values get variables, in order, and the arity must match exactly (`_`
    // discards a value). The loop ends when the shortest iterable runs out.
    if (for_->iters->next) {
        int64_t total_slots = 0;
        Text_t breakdown = EMPTY_TEXT;
        for (ast_list_t *it = for_->iters; it; it = it->next) {
            int64_t n = 0;
            for (arg_t *s = iteration_slots(env, it->ast); s; s = s->next) n += 1;
            total_slots += n;
            breakdown = Texts(breakdown, breakdown.length > 0 ? ", " : "", "`", ast_source(it->ast), "` yields ", n,
                              " value", n == 1 ? "" : "s");
        }
        int64_t num_vars = 0;
        for (ast_list_t *var = for_->vars; var; var = var->next) {
            if (var->ast->tag == StackReference)
                code_err(var->ast, "By-reference `&` variables aren't supported when iterating over "
                                   "multiple values in lockstep");
            num_vars += 1;
        }
        if (num_vars != total_slots) {
            // Highlight just the variables through the iterables (`for {x, y
            // in xs, ys, zs} ...`), not the whole loop:
            ast_list_t *last_iter = for_->iters;
            while (last_iter->next)
                last_iter = last_iter->next;
            const char *start = for_->vars ? for_->vars->ast->start : for_->iters->ast->start;
            compiler_err(ast->file, start, last_iter->ast->end, "These iterables yield a total of ", total_slots,
                         " value", total_slots == 1 ? "" : "s", " per iteration (", breakdown,
                         "), but this loop has ", num_vars, " variable", num_vars == 1 ? "" : "s",
                         ". Use `_` to discard a value you don't need, or `at` to bind an iteration counter.");
        }

        ast_list_t *var = for_->vars;
        for (ast_list_t *it = for_->iters; it; it = it->next) {
            for (arg_t *s = iteration_slots(env, it->ast); s; s = s->next, var = var->next) {
                const char *name = Match(var->ast, Var)->name;
                if (!streq(name, "_")) set_binding(scope, name, s->type, Texts("_$", name));
            }
        }
        return scope;
    }

    // By-reference variables (`for &x in xs`) are only supported for the value
    // variable of a mutable list iteration:
    for (ast_list_t *var = for_->vars; var; var = var->next) {
        if (var->ast->tag != StackReference) continue;
        if (iter_t->tag != ListType)
            code_err(var->ast, "Iterating by reference is only supported for lists, not ", type_to_text(iter_t));
        if (raw_iter_t->tag != PointerType)
            code_err(iter_ast, "This is an immutable list value, so it can't be iterated by reference. "
                               "You need a pointer to a list (`@` or `&`) to update its elements in place.");
        if (var->next != NULL)
            code_err(var->ast, "Only the value variable of a list iteration can be a `&` reference");
    }

    switch (iter_t->tag) {
    case ListType: {
        type_t *item_t = Match(iter_t, ListType)->item_type;
        ast_t *value_var = single_loop_var(for_->vars);
        if (value_var) {
            bool by_ref = value_var->tag == StackReference;
            const char *name = Match(by_ref ? Match(value_var, StackReference)->value : value_var, Var)->name;
            type_t *var_t = by_ref ? Type(PointerType, .pointed = item_t, .is_stack = true) : item_t;
            set_binding(scope, name, var_t, Texts("_$", name));
        }
        return scope;
    }
    case TableType: {
        const char *vars[2] = {};
        int64_t num_vars = 0;
        for (ast_list_t *var = for_->vars; var; var = var->next) {
            if (num_vars >= 2) code_err(var->ast, "This is too many variables for this loop");
            vars[num_vars++] = Match(var->ast, Var)->name;
        }

        type_t *key_t = Match(iter_t, TableType)->key_type;
        if (num_vars == 1) {
            set_binding(scope, vars[0], key_t, Texts("_$", vars[0]));
        } else if (num_vars == 2) {
            set_binding(scope, vars[0], key_t, Texts("_$", vars[0]));
            type_t *value_t = Match(iter_t, TableType)->value_type;
            set_binding(scope, vars[1], value_t, Texts("_$", vars[1]));
        }
        return scope;
    }
    case BigIntType:
    case IntType: {
        // The loop variable has the count's own type:
        ast_t *value_var = single_loop_var(for_->vars);
        if (value_var) set_binding(scope, Match(value_var, Var)->name, iter_t, Texts("_$", Match(value_var, Var)->name));
        return scope;
    }
    case FunctionType:
    case ClosureType: {
        __typeof(iter_t->__data.FunctionType) *fn = iter_t->tag == ClosureType
                                                        ? Match(Match(iter_t, ClosureType)->fn, FunctionType)
                                                        : Match(iter_t, FunctionType);

        arg_t *yields = iterator_yield_args(iter_t);
        if (yields) {
            int64_t num_yields = 0;
            for (arg_t *a = yields; a; a = a->next) num_yields += 1;
            int64_t num_vars = 0;
            for (ast_list_t *v = for_->vars; v; v = v->next) num_vars += 1;
            if (num_vars != 0 && num_vars != num_yields) {
                if (num_vars == num_yields + 1)
                    code_err(for_->vars->ast, "This iterator yields ", num_yields, " value", num_yields == 1 ? "" : "s",
                             " per iteration, but this loop has ", num_vars,
                             " variables. If the extra variable is meant to be an iteration counter, bind it with "
                             "`at` (e.g. `for x at i in ...`).");
                code_err(for_->vars->ast, "This iterator yields ", num_yields, " value", num_yields == 1 ? "" : "s",
                         " per iteration, but this loop has ", num_vars, " variable", num_vars == 1 ? "" : "s", ".");
            }
            arg_t *arg = yields;
            for (ast_list_t *v = for_->vars; v; v = v->next, arg = arg->next) {
                const char *name = Match(v->ast, Var)->name;
                if (!streq(name, "_"))
                    set_binding(scope, name, Match(arg->type, PointerType)->pointed, Texts("_$", name));
            }
            return scope;
        }
        type_t *non_opt_type = fn->ret->tag == OptionalType ? Match(fn->ret, OptionalType)->type : fn->ret;
        ast_t *value_var = single_loop_var(for_->vars);
        if (value_var) set_binding(scope, Match(value_var, Var)->name, non_opt_type, Texts("_$", Match(value_var, Var)->name));
        return scope;
    }
    case TextType: {
        ast_t *value_var = single_loop_var(for_->vars);
        if (value_var) set_binding(scope, Match(value_var, Var)->name, TEXT_TYPE, Texts("_$", Match(value_var, Var)->name));
        return scope;
    }
    default: code_err(iter_ast, "Iteration is not implemented for type: ", type_to_text(iter_t));
    }
    return NULL;
}

env_t *get_namespace_by_type(env_t *env, type_t *t) {
    t = non_optional(value_type(t));
    switch (t->tag) {
    case ListType: return NULL;
    case TableType: return NULL;
    case CStringType:
    case PathType:
    case BoolType:
    case IntType:
    case BigIntType:
    case NumType:
    case ByteType: {
        binding_t *b = get_binding(env, Text$as_c_string(type_to_text(t)));
        if (!b) compiler_err(NULL, NULL, NULL, "Couldn't get type namespace: ", Text$as_c_string(type_to_text(t)));
        return Match(b->type, TypeInfoType)->env;
    }
    case TextType: return Match(t, TextType)->env;
    case StructType: {
        DeclareMatch(struct_, t, StructType);
        return struct_->env;
    }
    case EnumType: {
        DeclareMatch(enum_, t, EnumType);
        return enum_->env;
    }
    case TypeInfoType: {
        DeclareMatch(info, t, TypeInfoType);
        return info->env;
    }
    default: break;
    }
    return NULL;
}

env_t *namespace_env(env_t *env, const char *namespace_name) {
    binding_t *b = get_binding(env, namespace_name);
    if (b) return Match(b->type, TypeInfoType)->env;

    env_t *ns_env = new (env_t);
    *ns_env = *env;
    ns_env->locals = new (Table_t, .fallback = env->locals);
    ns_env->namespace = new (namespace_t, .name = namespace_name, .parent = env->namespace);
    ns_env->namespace_bindings = ns_env->locals;
    return ns_env;
}

PUREFUNC binding_t *get_binding(env_t *env, const char *name) {
    return Table$str_get(*env->locals, name);
}

binding_t *get_namespace_binding(env_t *env, ast_t *self, const char *name) {
    type_t *self_type = get_type(env, self);
    if (!self_type) code_err(self, "I couldn't get this type");
    env_t *ns_env = get_namespace_by_type(env, self_type);
    return ns_env ? get_binding(ns_env, name) : NULL;
}

PUREFUNC binding_t *get_constructor(env_t *env, type_t *t, arg_ast_t *args, bool allow_underscores) {
    env_t *type_env = get_namespace_by_type(env, t);
    if (!type_env) return NULL;
    List_t constructors = type_env->namespace->constructors;
    // Prioritize exact matches:
    call_opts_t options = {.promotion = false, .underscores = allow_underscores};
    for (int64_t i = (int64_t)constructors.length - 1; i >= 0; i--) {
        binding_t *constructor = constructors.data + i * constructors.stride;
        DeclareMatch(fn, constructor->type, FunctionType);
        if (type_eq(fn->ret, t) && is_valid_call(env, fn->args, args, options)) return constructor;
    }
    // Fall back to promotion:
    options.promotion = true;
    for (int64_t i = (int64_t)constructors.length - 1; i >= 0; i--) {
        binding_t *constructor = constructors.data + i * constructors.stride;
        DeclareMatch(fn, constructor->type, FunctionType);
        if (type_eq(fn->ret, t) && is_valid_call(env, fn->args, args, options)) return constructor;
    }
    return NULL;
}

PUREFUNC binding_t *get_metamethod_binding(env_t *env, ast_e tag, ast_t *lhs, ast_t *rhs, type_t *ret) {
    const char *method_name = binop_info[tag].method_name;
    if (!method_name) return NULL;
    binding_t *b = get_namespace_binding(env, lhs, method_name);
    if (!b || b->type->tag != FunctionType) return NULL;
    DeclareMatch(fn, b->type, FunctionType);
    if (!type_eq(fn->ret, ret)) return NULL;
    arg_ast_t *args = new (arg_ast_t, .value = lhs, .next = new (arg_ast_t, .value = rhs));
    return is_valid_call(env, fn->args, args, (call_opts_t){.promotion = true}) ? b : NULL;
}

void set_binding(env_t *env, const char *name, type_t *type, Text_t code) {
    assert(name);
    Table$str_set(env->locals, name, new (binding_t, .type = type, .code = code));
}
