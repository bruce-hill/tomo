// Logic for the environmental context information during compilation
// (variable bindings, code sections, etc.)
#include <stdlib.h>
#include <signal.h>

#include "cordhelpers.h"
#include "environment.h"
#include "parse.h"
#include "stdlib/datatypes.h"
#include "stdlib/tables.h"
#include "stdlib/text.h"
#include "stdlib/util.h"
#include "typecheck.h"

type_t *TEXT_TYPE = NULL;
public type_t *PATH_TYPE = NULL;
public type_t *PATH_TYPE_TYPE = NULL;

static type_t *declare_type(env_t *env, const char *def_str)
{
    ast_t *ast = parse(def_str);
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

static type_t *bind_type(env_t *env, const char *name, type_t *type)
{
    if (Table$str_get(*env->types, name))
        errx(1, "Duplicate binding for type: %s", name);
    Table$str_set(env->types, name, type);
    return type;
}

env_t *global_env(bool source_mapping)
{
    static env_t *_global_env = NULL;
    if (_global_env != NULL) return _global_env;

    env_t *env = new(env_t);
    env->code = new(compilation_unit_t);
    env->types = new(Table_t);
    env->globals = new(Table_t);
    env->locals = env->globals;
    env->imports = new(Table_t);
    env->do_source_mapping = source_mapping;

    TEXT_TYPE = bind_type(env, "Text", Type(TextType, .lang="Text", .env=namespace_env(env, "Text")));
    (void)bind_type(env, "Int", Type(BigIntType));
    (void)bind_type(env, "Int32", Type(IntType, .bits=TYPE_IBITS32));
    (void)bind_type(env, "Memory", Type(MemoryType));
    PATH_TYPE_TYPE = declare_type(env, "enum PathType(Relative, Absolute, Home)");
    PATH_TYPE = declare_type(env, "struct Path(type:PathType, components:[Text])");

    typedef struct {
        const char *name, *code, *type_str;
    } ns_entry_t;

    struct {
        const char *name;
        type_t *type;
        CORD typename;
        CORD typeinfo;
        List_t namespace;
    } global_types[] = {
        {"Void", Type(VoidType), "Void_t", "Void$info", {}},
        {"Abort", Type(AbortType), "void", "Abort$info", {}},
        {"Memory", Type(MemoryType), "Memory_t", "Memory$info", {}},
        {"Bool", Type(BoolType), "Bool_t", "Bool$info", TypedList(ns_entry_t,
            {"parse", "Bool$parse", "func(text:Text -> Bool?)"},
        )},
        {"Byte", Type(ByteType), "Byte_t", "Byte$info", TypedList(ns_entry_t,
            {"max", "Byte$max", "Byte"},
            {"hex", "Byte$hex", "func(byte:Byte, uppercase=yes, prefix=no -> Text)"},
            {"is_between", "Byte$is_between", "func(x:Byte,low:Byte,high:Byte -> Bool)"},
            {"min", "Byte$min", "Byte"},
            {"to", "Byte$to", "func(first:Byte,last:Byte,step:Int8?=none -> func(->Byte?))"},
        )},
        {"Int", Type(BigIntType), "Int_t", "Int$info", TypedList(ns_entry_t,
            {"abs", "Int$abs", "func(x:Int -> Int)"},
            {"bit_and", "Int$bit_and", "func(x,y:Int -> Int)"},
            {"bit_or", "Int$bit_or", "func(x,y:Int -> Int)"},
            {"bit_xor", "Int$bit_xor", "func(x,y:Int -> Int)"},
            {"choose", "Int$choose", "func(x,y:Int -> Int)"},
            {"clamped", "Int$clamped", "func(x,low,high:Int -> Int)"},
            {"divided_by", "Int$divided_by", "func(x,y:Int -> Int)"},
            {"factorial", "Int$factorial", "func(x:Int -> Int)"},
            {"format", "Int$format", "func(i:Int, digits=0 -> Text)"},
            {"gcd", "Int$gcd", "func(x,y:Int -> Int)"},
            {"hex", "Int$hex", "func(i:Int, digits=0, uppercase=yes, prefix=yes -> Text)"},
            {"is_between", "Int$is_between", "func(x:Int,low:Int,high:Int -> Bool)"},
            {"is_prime", "Int$is_prime", "func(x:Int,reps=50 -> Bool)"},
            {"left_shifted", "Int$left_shifted", "func(x,y:Int -> Int)"},
            {"minus", "Int$minus", "func(x,y:Int -> Int)"},
            {"modulo", "Int$modulo", "func(x,y:Int -> Int)"},
            {"modulo1", "Int$modulo1", "func(x,y:Int -> Int)"},
            {"negated", "Int$negated", "func(x:Int -> Int)"},
            {"negative", "Int$negative", "func(x:Int -> Int)"},
            {"next_prime", "Int$next_prime", "func(x:Int -> Int)"},
            {"octal", "Int$octal", "func(i:Int, digits=0, prefix=yes -> Text)"},
            {"onward", "Int$onward", "func(first:Int,step=1 -> func(->Int?))"},
            {"parse", "Int$parse", "func(text:Text -> Int?)"},
            {"plus", "Int$plus", "func(x,y:Int -> Int)"},
            {"power", "Int$power", "func(base:Int,exponent:Int -> Int)"},
#if __GNU_MP_VERSION >= 6
#if __GNU_MP_VERSION_MINOR >= 3
            {"prev_prime", "Int$prev_prime", "func(x:Int -> Int)"},
#endif
#endif
            {"right_shifted", "Int$right_shifted", "func(x,y:Int -> Int)"},
            {"sqrt", "Int$sqrt", "func(x:Int -> Int?)"},
            {"times", "Int$times", "func(x,y:Int -> Int)"},
            {"to", "Int$to", "func(first:Int,last:Int,step:Int?=none -> func(->Int?))"},
        )},
        {"Int64", Type(IntType, .bits=TYPE_IBITS64), "Int64_t", "Int64$info", TypedList(ns_entry_t,
            {"abs", "labs", "func(i:Int64 -> Int64)"},
            {"bits", "Int64$bits", "func(x:Int64 -> [Bool])"},
            {"clamped", "Int64$clamped", "func(x,low,high:Int64 -> Int64)"},
            {"divided_by", "Int64$divided_by", "func(x,y:Int64 -> Int64)"},
            {"format", "Int64$format", "func(i:Int64, digits=0 -> Text)"},
            {"gcd", "Int64$gcd", "func(x,y:Int64 -> Int64)"},
            {"parse", "Int64$parse", "func(text:Text -> Int64?)"},
            {"hex", "Int64$hex", "func(i:Int64, digits=0, uppercase=yes, prefix=yes -> Text)"},
            {"is_between", "Int64$is_between", "func(x:Int64,low:Int64,high:Int64 -> Bool)"},
            {"max", "Int64$max", "Int64"},
            {"min", "Int64$min", "Int64"},
            {"modulo", "Int64$modulo", "func(x,y:Int64 -> Int64)"},
            {"modulo1", "Int64$modulo1", "func(x,y:Int64 -> Int64)"},
            {"octal", "Int64$octal", "func(i:Int64, digits=0, prefix=yes -> Text)"},
            {"onward", "Int64$onward", "func(first:Int64,step=Int64(1) -> func(->Int64?))"},
            {"to", "Int64$to", "func(first:Int64,last:Int64,step:Int64?=none -> func(->Int64?))"},
            {"unsigned_left_shifted", "Int64$unsigned_left_shifted", "func(x:Int64,y:Int64 -> Int64)"},
            {"unsigned_right_shifted", "Int64$unsigned_right_shifted", "func(x:Int64,y:Int64 -> Int64)"},
            {"wrapping_minus", "Int64$wrapping_minus", "func(x:Int64,y:Int64 -> Int64)"},
            {"wrapping_plus", "Int64$wrapping_plus", "func(x:Int64,y:Int64 -> Int64)"},
        )},
        {"Int32", Type(IntType, .bits=TYPE_IBITS32), "Int32_t", "Int32$info", TypedList(ns_entry_t,
            {"abs", "abs", "func(i:Int32 -> Int32)"},
            {"bits", "Int32$bits", "func(x:Int32 -> [Bool])"},
            {"clamped", "Int32$clamped", "func(x,low,high:Int32 -> Int32)"},
            {"divided_by", "Int32$divided_by", "func(x,y:Int32 -> Int32)"},
            {"format", "Int32$format", "func(i:Int32, digits=0 -> Text)"},
            {"gcd", "Int32$gcd", "func(x,y:Int32 -> Int32)"},
            {"parse", "Int32$parse", "func(text:Text -> Int32?)"},
            {"hex", "Int32$hex", "func(i:Int32, digits=0, uppercase=yes, prefix=yes -> Text)"},
            {"is_between", "Int32$is_between", "func(x:Int32,low:Int32,high:Int32 -> Bool)"},
            {"max", "Int32$max", "Int32"},
            {"min", "Int32$min", "Int32"},
            {"modulo", "Int32$modulo", "func(x,y:Int32 -> Int32)"},
            {"modulo1", "Int32$modulo1", "func(x,y:Int32 -> Int32)"},
            {"octal", "Int32$octal", "func(i:Int32, digits=0, prefix=yes -> Text)"},
            {"onward", "Int32$onward", "func(first:Int32,step=Int32(1) -> func(->Int32?))"},
            {"to", "Int32$to", "func(first:Int32,last:Int32,step:Int32?=none -> func(->Int32?))"},
            {"unsigned_left_shifted", "Int32$unsigned_left_shifted", "func(x:Int32,y:Int32 -> Int32)"},
            {"unsigned_right_shifted", "Int32$unsigned_right_shifted", "func(x:Int32,y:Int32 -> Int32)"},
            {"wrapping_minus", "Int32$wrapping_minus", "func(x:Int32,y:Int32 -> Int32)"},
            {"wrapping_plus", "Int32$wrapping_plus", "func(x:Int32,y:Int32 -> Int32)"},
        )},
        {"Int16", Type(IntType, .bits=TYPE_IBITS16), "Int16_t", "Int16$info", TypedList(ns_entry_t,
            {"abs", "abs", "func(i:Int16 -> Int16)"},
            {"bits", "Int16$bits", "func(x:Int16 -> [Bool])"},
            {"clamped", "Int16$clamped", "func(x,low,high:Int16 -> Int16)"},
            {"divided_by", "Int16$divided_by", "func(x,y:Int16 -> Int16)"},
            {"format", "Int16$format", "func(i:Int16, digits=0 -> Text)"},
            {"gcd", "Int16$gcd", "func(x,y:Int16 -> Int16)"},
            {"parse", "Int16$parse", "func(text:Text -> Int16?)"},
            {"hex", "Int16$hex", "func(i:Int16, digits=0, uppercase=yes, prefix=yes -> Text)"},
            {"is_between", "Int16$is_between", "func(x:Int16,low:Int16,high:Int16 -> Bool)"},
            {"max", "Int16$max", "Int16"},
            {"min", "Int16$min", "Int16"},
            {"modulo", "Int16$modulo", "func(x,y:Int16 -> Int16)"},
            {"modulo1", "Int16$modulo1", "func(x,y:Int16 -> Int16)"},
            {"octal", "Int16$octal", "func(i:Int16, digits=0, prefix=yes -> Text)"},
            {"onward", "Int16$onward", "func(first:Int16,step=Int16(1) -> func(->Int16?))"},
            {"to", "Int16$to", "func(first:Int16,last:Int16,step:Int16?=none -> func(->Int16?))"},
            {"unsigned_left_shifted", "Int16$unsigned_left_shifted", "func(x:Int16,y:Int16 -> Int16)"},
            {"unsigned_right_shifted", "Int16$unsigned_right_shifted", "func(x:Int16,y:Int16 -> Int16)"},
            {"wrapping_minus", "Int16$wrapping_minus", "func(x:Int16,y:Int16 -> Int16)"},
            {"wrapping_plus", "Int16$wrapping_plus", "func(x:Int16,y:Int16 -> Int16)"},
        )},
        {"Int8", Type(IntType, .bits=TYPE_IBITS8), "Int8_t", "Int8$info", TypedList(ns_entry_t,
            {"abs", "abs", "func(i:Int8 -> Int8)"},
            {"bits", "Int8$bits", "func(x:Int8 -> [Bool])"},
            {"clamped", "Int8$clamped", "func(x,low,high:Int8 -> Int8)"},
            {"divided_by", "Int8$divided_by", "func(x,y:Int8 -> Int8)"},
            {"format", "Int8$format", "func(i:Int8, digits=0 -> Text)"},
            {"gcd", "Int8$gcd", "func(x,y:Int8 -> Int8)"},
            {"parse", "Int8$parse", "func(text:Text -> Int8?)"},
            {"hex", "Int8$hex", "func(i:Int8, digits=0, uppercase=yes, prefix=yes -> Text)"},
            {"is_between", "Int8$is_between", "func(x:Int8,low:Int8,high:Int8 -> Bool)"},
            {"max", "Int8$max", "Int8"},
            {"min", "Int8$min", "Int8"},
            {"modulo", "Int8$modulo", "func(x,y:Int8 -> Int8)"},
            {"modulo1", "Int8$modulo1", "func(x,y:Int8 -> Int8)"},
            {"octal", "Int8$octal", "func(i:Int8, digits=0, prefix=yes -> Text)"},
            {"onward", "Int8$onward", "func(first:Int8,step=Int8(1) -> func(->Int8?))"},
            {"to", "Int8$to", "func(first:Int8,last:Int8,step:Int8?=none -> func(->Int8?))"},
            {"unsigned_left_shifted", "Int8$unsigned_left_shifted", "func(x:Int8,y:Int8 -> Int8)"},
            {"unsigned_right_shifted", "Int8$unsigned_right_shifted", "func(x:Int8,y:Int8 -> Int8)"},
            {"wrapping_minus", "Int8$wrapping_minus", "func(x:Int8,y:Int8 -> Int8)"},
            {"wrapping_plus", "Int8$wrapping_plus", "func(x:Int8,y:Int8 -> Int8)"},
        )},
#define C(name) {#name, "M_"#name, "Num"}
#define F(name) {#name, #name, "func(n:Num -> Num)"}
#define F_opt(name) {#name, #name, "func(n:Num -> Num?)"}
#define F2(name) {#name, #name, "func(x,y:Num -> Num)"}
        {"Num", Type(NumType, .bits=TYPE_NBITS64), "Num_t", "Num$info", TypedList(ns_entry_t,
            {"near", "Num$near", "func(x,y:Num, ratio=1e-9, min_epsilon=1e-9 -> Bool)"},
            {"clamped", "Num$clamped", "func(x,low,high:Num -> Num)"},
            {"format", "Num$format", "func(n:Num, precision=16 -> Text)"},
            {"scientific", "Num$scientific", "func(n:Num,precision=0 -> Text)"},
            {"percent", "Num$percent", "func(n:Num,precision=0 -> Text)"},
            {"is_between", "Num$is_between", "func(x:Num,low:Num,high:Num -> Bool)"},
            {"isinf", "Num$isinf", "func(n:Num -> Bool)"},
            {"isfinite", "Num$isfinite", "func(n:Num -> Bool)"},
            {"modulo", "Num$mod", "func(x,y:Num -> Num)"},
            {"modulo1", "Num$mod1", "func(x,y:Num -> Num)"},
            C(2_SQRTPI), C(E), C(PI_2), C(2_PI), C(1_PI), C(LN10), C(LN2), C(LOG2E),
            C(PI), C(PI_4), C(SQRT2), C(SQRT1_2),
            {"INF", "(Num_t)(INFINITY)", "Num"},
            {"TAU", "(Num_t)(2.*M_PI)", "Num"},
            {"mix", "Num$mix", "func(amount,x,y:Num -> Num)"},
            {"parse", "Num$parse", "func(text:Text -> Num?)"},
            {"abs", "fabs", "func(n:Num -> Num)"},
            F_opt(acos), F_opt(acosh), F_opt(asin), F(asinh), F(atan), F_opt(atanh),
            F(cbrt), F(ceil), F_opt(cos), F(cosh), F(erf), F(erfc),
            F(exp), F(exp2), F(expm1), F(floor), F(j0), F(j1), F_opt(log), F_opt(log10), F_opt(log1p),
            F_opt(log2), F(logb), F(rint), F(round), F(significand), F_opt(sin), F(sinh), F_opt(sqrt),
            F_opt(tan), F(tanh), F_opt(tgamma), F(trunc), F_opt(y0), F_opt(y1),
            F2(atan2), F2(copysign), F2(fdim), F2(hypot), F2(nextafter),
        )},
#undef F2
#undef F_opt
#undef F
#undef C
#define C(name) {#name, "(Num32_t)(M_"#name")", "Num32"}
#define F(name) {#name, #name"f", "func(n:Num32 -> Num32)"}
#define F_opt(name) {#name, #name"f", "func(n:Num32 -> Num32?)"}
#define F2(name) {#name, #name"f", "func(x,y:Num32 -> Num32)"}
        {"Num32", Type(NumType, .bits=TYPE_NBITS32), "Num32_t", "Num32$info", TypedList(ns_entry_t,
            {"near", "Num32$near", "func(x,y:Num32, ratio=Num32(1e-9), min_epsilon=Num32(1e-9) -> Bool)"},
            {"clamped", "Num32$clamped", "func(x,low,high:Num32 -> Num32)"},
            {"format", "Num32$format", "func(n:Num32, precision=8 -> Text)"},
            {"scientific", "Num32$scientific", "func(n:Num32, precision=0 -> Text)"},
            {"percent", "Num32$percent", "func(n:Num32,precision=0 -> Text)"},
            {"is_between", "Num32$is_between", "func(x:Num32,low:Num32,high:Num32 -> Bool)"},
            {"isinf", "Num32$isinf", "func(n:Num32 -> Bool)"},
            {"isfinite", "Num32$isfinite", "func(n:Num32 -> Bool)"},
            C(2_SQRTPI), C(E), C(PI_2), C(2_PI), C(1_PI), C(LN10), C(LN2), C(LOG2E),
            C(PI), C(PI_4), C(SQRT2), C(SQRT1_2),
            {"INF", "(Num32_t)(INFINITY)", "Num32"},
            {"TAU", "(Num32_t)(2.f*M_PI)", "Num32"},
            {"mix", "Num32$mix", "func(amount,x,y:Num32 -> Num32)"},
            {"parse", "Num32$parse", "func(text:Text -> Num32?)"},
            {"abs", "fabsf", "func(n:Num32 -> Num32)"},
            {"modulo", "Num32$mod", "func(x,y:Num32 -> Num32)"},
            {"modulo1", "Num32$mod1", "func(x,y:Num32 -> Num32)"},
            F_opt(acos), F_opt(acosh), F_opt(asin), F(asinh), F(atan), F_opt(atanh),
            F(cbrt), F(ceil), F_opt(cos), F(cosh), F(erf), F(erfc),
            F(exp), F(exp2), F(expm1), F(floor), F(j0), F(j1), F_opt(log), F_opt(log10), F_opt(log1p),
            F_opt(log2), F(logb), F(rint), F(round), F(significand), F_opt(sin), F(sinh), F_opt(sqrt),
            F_opt(tan), F(tanh), F_opt(tgamma), F(trunc), F_opt(y0), F_opt(y1),
            F2(atan2), F2(copysign), F2(fdim), F2(hypot), F2(nextafter),
        )},
        {"CString", Type(CStringType), "char*", "CString$info", TypedList(ns_entry_t,
            {"as_text", "CString$as_text_simple", "func(str:CString -> Text)"},
        )},
#undef F2
#undef F_opt
#undef F
#undef C
        {"PathType", PATH_TYPE_TYPE, "PathType_t", "PathType$info", TypedList(ns_entry_t,
            {"Relative", "((PathType_t){.$tag=PATH_RELATIVE})", "PathType"},
            {"Absolute", "((PathType_t){.$tag=PATH_ABSOLUTE})", "PathType"},
            {"Home", "((PathType_t){.$tag=PATH_HOME})", "PathType"},
        )},
        {"Path", PATH_TYPE, "Path_t", "Path$info", TypedList(ns_entry_t,
            {"accessed", "Path$accessed", "func(path:Path, follow_symlinks=yes -> Int64?)"},
            {"append", "Path$append", "func(path:Path, text:Text, permissions=Int32(0o644))"},
            {"append_bytes", "Path$append_bytes", "func(path:Path, bytes:[Byte], permissions=Int32(0o644))"},
            {"base_name", "Path$base_name", "func(path:Path -> Text)"},
            {"by_line", "Path$by_line", "func(path:Path -> func(->Text?)?)"},
            {"can_execute", "Path$can_execute", "func(path:Path -> Bool)"},
            {"can_read", "Path$can_read", "func(path:Path -> Bool)"},
            {"can_write", "Path$can_write", "func(path:Path -> Bool)"},
            {"changed", "Path$changed", "func(path:Path, follow_symlinks=yes -> Int64?)"},
            {"child", "Path$with_component", "func(path:Path, child:Text -> Path)"},
            {"children", "Path$children", "func(path:Path, include_hidden=no -> [Path])"},
            {"concatenated_with", "Path$concat", "func(a,b:Path -> Path)"},
            {"create_directory", "Path$create_directory", "func(path:Path, permissions=Int32(0o755))"},
            {"current_dir", "Path$current_dir", "func(->Path)"},
            {"exists", "Path$exists", "func(path:Path -> Bool)"},
            {"expand_home", "Path$expand_home", "func(path:Path -> Path)"},
            {"extension", "Path$extension", "func(path:Path, full=yes -> Text)"},
            {"files", "Path$children", "func(path:Path, include_hidden=no -> [Path])"},
            {"from_components", "Path$from_components", "func(components:[Text] -> Path)"},
            {"glob", "Path$glob", "func(path:Path -> [Path])"},
            {"group", "Path$group", "func(path:Path, follow_symlinks=yes -> Text?)"},
            {"is_directory", "Path$is_directory", "func(path:Path, follow_symlinks=yes -> Bool)"},
            {"is_file", "Path$is_file", "func(path:Path, follow_symlinks=yes -> Bool)"},
            {"is_pipe", "Path$is_pipe", "func(path:Path, follow_symlinks=yes -> Bool)"},
            {"is_socket", "Path$is_socket", "func(path:Path, follow_symlinks=yes -> Bool)"},
            {"is_symlink", "Path$is_symlink", "func(path:Path -> Bool)"},
            {"modified", "Path$modified", "func(path:Path, follow_symlinks=yes -> Int64?)"},
            {"owner", "Path$owner", "func(path:Path, follow_symlinks=yes -> Text?)"},
            {"parent", "Path$parent", "func(path:Path -> Path)"},
            {"read", "Path$read", "func(path:Path -> Text?)"},
            {"read_bytes", "Path$read_bytes", "func(path:Path, limit:Int?=none -> [Byte]?)"},
            {"relative_to", "Path$relative_to", "func(path:Path, relative_to:Path -> Path)"},
            {"remove", "Path$remove", "func(path:Path, ignore_missing=no)"},
            {"resolved", "Path$resolved", "func(path:Path, relative_to=(./) -> Path)"},
            {"set_owner", "Path$set_owner", "func(path:Path, owner:Text?=none, group:Text?=none, follow_symlinks=yes)"},
            {"subdirectories", "Path$children", "func(path:Path, include_hidden=no -> [Path])"},
            {"unique_directory", "Path$unique_directory", "func(path:Path -> Path)"},
            {"write", "Path$write", "func(path:Path, text:Text, permissions=Int32(0o644))"},
            {"write_bytes", "Path$write_bytes", "func(path:Path, bytes:[Byte], permissions=Int32(0o644))"},
            {"write_unique", "Path$write_unique", "func(path:Path, text:Text -> Path)"},
            {"write_unique_bytes", "Path$write_unique_bytes", "func(path:Path, bytes:[Byte] -> Path)"},
        )},
        {"Text", TEXT_TYPE, "Text_t", "Text$info", TypedList(ns_entry_t,
            {"as_c_string", "Text$as_c_string", "func(text:Text -> CString)"},
            {"at", "Text$cluster", "func(text:Text, index:Int -> Text)"},
            {"by_line", "Text$by_line", "func(text:Text -> func(->Text?))"},
            {"by_split", "Text$by_split", "func(text:Text, delimiter='' -> func(->Text?))"},
            {"by_split_any", "Text$by_split_any", "func(text:Text, delimiters=' \\t\\r\\n' -> func(->Text?))"},
            {"bytes", "Text$utf8_bytes", "func(text:Text -> [Byte])"},
            {"caseless_equals", "Text$equal_ignoring_case", "func(a,b:Text, language='C' -> Bool)"},
            {"codepoint_names", "Text$codepoint_names", "func(text:Text -> [Text])"},
            {"ends_with", "Text$ends_with", "func(text,suffix:Text -> Bool)"},
            {"from", "Text$from", "func(text:Text, first:Int -> Text)"},
            {"from_bytes", "Text$from_bytes", "func(bytes:[Byte] -> Text?)"},
            {"from_c_string", "Text$from_str", "func(str:CString -> Text?)"},
            {"from_codepoint_names", "Text$from_codepoint_names", "func(codepoint_names:[Text] -> Text?)"},
            {"from_codepoints", "Text$from_codepoints", "func(codepoints:[Int32] -> Text)"},
            {"from_text", "Path$from_text", "func(text:Text -> Path)"},
            {"has", "Text$has", "func(text:Text, target:Text -> Bool)"},
            {"join", "Text$join", "func(glue:Text, pieces:[Text] -> Text)"},
            {"left_pad", "Text$left_pad", "func(text:Text, count:Int, pad=' ', language='C' -> Text)"},
            {"lines", "Text$lines", "func(text:Text -> [Text])"},
            {"lower", "Text$lower", "func(text:Text, language='C' -> Text)"},
            {"middle_pad", "Text$middle_pad", "func(text:Text, count:Int, pad=' ', language='C' -> Text)"},
            {"quoted", "Text$quoted", "func(text:Text, color=no, quotation_mark='\"' -> Text)"},
            {"repeat", "Text$repeat", "func(text:Text, count:Int -> Text)"},
            {"replace", "Text$replace", "func(text:Text, target:Text, replacement:Text -> Text)"},
            {"reversed", "Text$reversed", "func(text:Text -> Text)"},
            {"right_pad", "Text$right_pad", "func(text:Text, count:Int, pad=' ', language='C' -> Text)"},
            {"slice", "Text$slice", "func(text:Text, from=1, to=-1 -> Text)"},
            {"split", "Text$split", "func(text:Text, delimiter='' -> [Text])"},
            {"split_any", "Text$split_any", "func(text:Text, delimiters=' \\t\\r\\n' -> [Text])"},
            {"starts_with", "Text$starts_with", "func(text,prefix:Text -> Bool)"},
            {"title", "Text$title", "func(text:Text, language='C' -> Text)"},
            {"to", "Text$to", "func(text:Text, last:Int -> Text)"},
            {"translate", "Text$translate", "func(text:Text, translations:{Text=Text} -> Text)"},
            {"trim", "Text$trim", "func(text:Text, to_trim=\" \t\r\n\", left=yes, right=yes -> Text)"},
            {"upper", "Text$upper", "func(text:Text, language='C' -> Text)"},
            {"utf32_codepoints", "Text$utf32_codepoints", "func(text:Text -> [Int32])"},
            {"width", "Text$width", "func(text:Text, language='C' -> Int)"},
            {"without_prefix", "Text$without_prefix", "func(text,prefix:Text -> Text)"},
            {"without_suffix", "Text$without_suffix", "func(text,suffix:Text -> Text)"},
        )},
    };

    for (size_t i = 0; i < sizeof(global_types)/sizeof(global_types[0]); i++) {
        env_t *ns_env = NULL;
        switch (global_types[i].type->tag) {
        case TextType:
            ns_env = Match(global_types[i].type, TextType)->env;
            break;
        case StructType:
            ns_env = Match(global_types[i].type, StructType)->env;
            break;
        case EnumType:
            ns_env = Match(global_types[i].type, EnumType)->env;
            break;
        default: break;
        }
        if (ns_env == NULL) ns_env = namespace_env(env, global_types[i].name);
        binding_t *binding = new(binding_t, .type=Type(TypeInfoType, .name=global_types[i].name, .type=global_types[i].type, .env=ns_env),
                                 .code=global_types[i].typeinfo);
        Table$str_set(env->globals, global_types[i].name, binding);
        Table$str_set(env->types, global_types[i].name, global_types[i].type);
    }

    for (size_t i = 0; i < sizeof(global_types)/sizeof(global_types[0]); i++) {
        binding_t *type_binding = Table$str_get(*env->globals, global_types[i].name);
        assert(type_binding);
        env_t *ns_env = Match(type_binding->type, TypeInfoType)->env;
        for (int64_t j = 0; j < global_types[i].namespace.length; j++) {
            ns_entry_t *entry = global_types[i].namespace.data + j*global_types[i].namespace.stride;
            type_t *type = parse_type_string(ns_env, entry->type_str);
            if (!type) compiler_err(NULL, NULL, NULL, "Couldn't parse type string: ", entry->type_str);
            if (type->tag == ClosureType) type = Match(type, ClosureType)->fn;
            set_binding(ns_env, entry->name, type, entry->code);
        }
    }


    // Conversion constructors:
#define ADD_CONSTRUCTORS(type_name, ...) do {\
    env_t *ns_env = namespace_env(env, type_name); \
    struct { const char *c_name, *type_str; } constructor_infos[] = {__VA_ARGS__}; \
    for (size_t i = 0; i < sizeof(constructor_infos)/sizeof(constructor_infos[0]); i++) { \
        type_t *t = parse_type_string(ns_env, constructor_infos[i].type_str); \
        List$insert(&ns_env->namespace->constructors, \
                     ((binding_t[1]){{.code=constructor_infos[i].c_name, \
                      .type=Match(t, ClosureType)->fn}}), I(0), sizeof(binding_t)); \
    } \
} while (0)

    ADD_CONSTRUCTORS("Bool",
                     {"Bool$from_byte", "func(b:Byte -> Bool)"},
                     {"Bool$from_int8", "func(i:Int8 -> Bool)"},
                     {"Bool$from_int16", "func(i:Int16 -> Bool)"},
                     {"Bool$from_int32", "func(i:Int32 -> Bool)"},
                     {"Bool$from_int64", "func(i:Int64 -> Bool)"},
                     {"Bool$from_int", "func(i:Int -> Bool)"});
    ADD_CONSTRUCTORS("Byte",
                     {"Byte$from_bool", "func(b:Bool -> Byte)"},
                     {"Byte$from_int8", "func(i:Int8 -> Byte)"},
                     {"Byte$from_int16", "func(i:Int16, truncate=no -> Byte)"},
                     {"Byte$from_int32", "func(i:Int32, truncate=no -> Byte)"},
                     {"Byte$from_int64", "func(i:Int64, truncate=no -> Byte)"},
                     {"Byte$from_int", "func(i:Int, truncate=no -> Byte)"});
    ADD_CONSTRUCTORS("Int",
                     {"Int$from_bool", "func(b:Bool -> Int)"},
                     {"Int$from_byte", "func(b:Byte -> Int)"},
                     {"Int$from_int8", "func(i:Int8 -> Int)"},
                     {"Int$from_int16", "func(i:Int16 -> Int)"},
                     {"Int$from_int32", "func(i:Int32 -> Int)"},
                     {"Int$from_int64", "func(i:Int64 -> Int)"},
                     {"Int$from_num", "func(n:Num, truncate=no -> Int)"},
                     {"Int$from_num32", "func(n:Num32, truncate=no -> Int)"});
    ADD_CONSTRUCTORS("Int64",
                     {"Int64$from_bool", "func(b:Bool -> Int64)"},
                     {"Int64$from_byte", "func(b:Byte -> Int64)"},
                     {"Int64$from_int8", "func(i:Int8 -> Int64)"},
                     {"Int64$from_int16", "func(i:Int16 -> Int64)"},
                     {"Int64$from_int32", "func(i:Int32 -> Int64)"},
                     {"Int64$from_int", "func(i:Int, truncate=no -> Int64)"},
                     {"Int64$from_num", "func(n:Num, truncate=no -> Int64)"},
                     {"Int64$from_num32", "func(n:Num32, truncate=no -> Int64)"});
    ADD_CONSTRUCTORS("Int32",
                     {"Int32$from_bool", "func(b:Bool -> Int32)"},
                     {"Int32$from_byte", "func(b:Byte -> Int32)"},
                     {"Int32$from_int8", "func(i:Int8 -> Int32)"},
                     {"Int32$from_int16", "func(i:Int16 -> Int32)"},
                     {"Int32$from_int64", "func(i:Int64, truncate=no -> Int32)"},
                     {"Int32$from_int", "func(i:Int, truncate=no -> Int32)"},
                     {"Int32$from_num", "func(n:Num, truncate=no -> Int32)"},
                     {"Int32$from_num32", "func(n:Num32, truncate=no -> Int32)"});
    ADD_CONSTRUCTORS("Int16",
                     {"Int16$from_bool", "func(b:Bool -> Int16)"},
                     {"Int16$from_byte", "func(b:Byte -> Int16)"},
                     {"Int16$from_int8", "func(i:Int8 -> Int16)"},
                     {"Int16$from_int32", "func(i:Int32, truncate=no -> Int16)"},
                     {"Int16$from_int64", "func(i:Int64, truncate=no -> Int16)"},
                     {"Int16$from_int", "func(i:Int, truncate=no -> Int16)"},
                     {"Int16$from_num", "func(n:Num, truncate=no -> Int16)"},
                     {"Int16$from_num32", "func(n:Num32, truncate=no -> Int16)"});
    ADD_CONSTRUCTORS("Int8",
                     {"Int8$from_bool", "func(b:Bool -> Int8)"},
                     {"Int8$from_byte", "func(b:Byte -> Int8)"},
                     {"Int8$from_int16", "func(i:Int16, truncate=no -> Int8)"},
                     {"Int8$from_int32", "func(i:Int32, truncate=no -> Int8)"},
                     {"Int8$from_int64", "func(i:Int64, truncate=no -> Int8)"},
                     {"Int8$from_int", "func(i:Int, truncate=no -> Int8)"},
                     {"Int8$from_num", "func(n:Num, truncate=no -> Int8)"},
                     {"Int8$from_num32", "func(n:Num32, truncate=no -> Int8)"});
    ADD_CONSTRUCTORS("Num",
                     {"Num$from_bool", "func(b:Bool -> Num)"},
                     {"Num$from_byte", "func(b:Byte -> Num)"},
                     {"Num$from_int8", "func(i:Int8 -> Num)"},
                     {"Num$from_int16", "func(i:Int16 -> Num)"},
                     {"Num$from_int32", "func(i:Int32 -> Num)"},
                     {"Num$from_int64", "func(i:Int64, truncate=no -> Num)"},
                     {"Num$from_int", "func(i:Int, truncate=no -> Num)"},
                     {"Num$from_num32", "func(n:Num32 -> Num)"});
    ADD_CONSTRUCTORS("Num32",
                     {"Num32$from_bool", "func(b:Bool -> Num32)"},
                     {"Num32$from_byte", "func(b:Byte -> Num32)"},
                     {"Num32$from_int8", "func(i:Int8 -> Num32)"},
                     {"Num32$from_int16", "func(i:Int16 -> Num32)"},
                     {"Num32$from_int32", "func(i:Int32, truncate=no -> Num32)"},
                     {"Num32$from_int64", "func(i:Int64, truncate=no -> Num32)"},
                     {"Num32$from_int", "func(i:Int, truncate=no -> Num32)"},
                     {"Num32$from_num", "func(n:Num -> Num32)"});
    ADD_CONSTRUCTORS("Path",
                     {"Path$escape_text", "func(text:Text -> Path)"},
                     {"Path$escape_path", "func(path:Path -> Path)"},
                     {"Int$value_as_text", "func(i:Int -> Path)"});
    ADD_CONSTRUCTORS("CString", {"Text$as_c_string", "func(text:Text -> CString)"});
#undef ADD_CONSTRUCTORS

    set_binding(namespace_env(env, "Path"), "from_text",
                NewFunctionType(PATH_TYPE, {.name="text", .type=TEXT_TYPE}),
                "Path$from_text");

    struct {
        const char *name, *code, *type_str;
    } global_vars[] = {
        {"USE_COLOR", "USE_COLOR", "Bool"},
        {"say", "say", "func(text:Text, newline=yes)"},
        {"print", "say", "func(text:Text, newline=yes)"},
        {"ask", "ask", "func(prompt:Text, bold=yes, force_tty=yes -> Text?)"},
        {"exit", "tomo_exit", "func(message:Text?=none, code=Int32(1) -> Abort)"},
        {"fail", "fail_text", "func(message:Text -> Abort)"},
        {"sleep", "sleep_num", "func(seconds:Num)"},
    };

    for (size_t i = 0; i < sizeof(global_vars)/sizeof(global_vars[0]); i++) {
        type_t *type = parse_type_string(env, global_vars[i].type_str);
        if (!type) compiler_err(NULL, NULL, NULL, "Couldn't parse type string for ", global_vars[i].name, ": ", global_vars[i].type_str);
        if (type->tag == ClosureType) type = Match(type, ClosureType)->fn;
        Table$str_set(env->globals, global_vars[i].name, new(binding_t, .type=type, .code=global_vars[i].code));
    }

    _global_env = env;
    return env;
}

CORD namespace_prefix(env_t *env, namespace_t *ns)
{
    CORD prefix = CORD_EMPTY;
    for (; ns; ns = ns->parent)
        prefix = CORD_all(ns->name, "$", prefix);
    if (env->locals != env->globals) {
        if (env->libname)
            prefix = CORD_all("_$", env->libname, "$", prefix);
        else
            prefix = CORD_all("_$", prefix);
    }
    return prefix;
}

env_t *load_module_env(env_t *env, ast_t *ast)
{
    const char *name = ast->file->filename;
    env_t *cached = Table$str_get(*env->imports, name);
    if (cached) return cached;
    env_t *module_env = fresh_scope(env);
    module_env->code = new(compilation_unit_t);
    module_env->namespace = new(namespace_t, .name=file_base_id(name)); 
    module_env->namespace_bindings = module_env->locals;
    Table$str_set(module_env->imports, name, module_env);

    ast_list_t *statements = Match(ast, Block)->statements;
    visit_topologically(statements, (Closure_t){.fn=(void*)prebind_statement, .userdata=module_env});
    visit_topologically(statements, (Closure_t){.fn=(void*)bind_statement, .userdata=module_env});

    return module_env;
}

env_t *namespace_scope(env_t *env)
{
    env_t *scope = new(env_t);
    *scope = *env;
    scope->locals = new(Table_t, .fallback=env->namespace_bindings ? env->namespace_bindings : env->globals);
    return scope;
}

env_t *fresh_scope(env_t *env)
{
    env_t *scope = new(env_t);
    *scope = *env;
    scope->locals = new(Table_t, .fallback=env->locals);
    return scope;
}

env_t *with_enum_scope(env_t *env, type_t *t)
{
    while (t->tag == OptionalType)
        t = Match(t, OptionalType)->type;

    if (t->tag != EnumType) return env;
    env = fresh_scope(env);
    env_t *ns_env = Match(t, EnumType)->env;
    for (tag_t *tag = Match(t, EnumType)->tags; tag; tag = tag->next) {
        if (get_binding(env, tag->name))
            continue;
        binding_t *b = get_binding(ns_env, tag->name);
        assert(b);
        Table$str_set(env->locals, tag->name, b);
    }
    return env;
}


env_t *for_scope(env_t *env, ast_t *ast)
{
    DeclareMatch(for_, ast, For);
    type_t *iter_t = value_type(get_type(env, for_->iter));
    env_t *scope = fresh_scope(env);

    switch (iter_t->tag) {
    case ListType: {
        type_t *item_t = Match(iter_t, ListType)->item_type;
        const char *vars[2] = {};
        int64_t num_vars = 0;
        for (ast_list_t *var = for_->vars; var; var = var->next) {
            if (num_vars >= 2)
                code_err(var->ast, "This is too many variables for this loop");
            vars[num_vars++] = Match(var->ast, Var)->name;
        }
        if (num_vars == 1) {
            set_binding(scope, vars[0], item_t, CORD_cat("_$", vars[0]));
        } else if (num_vars == 2) {
            set_binding(scope, vars[0], INT_TYPE, CORD_cat("_$", vars[0]));
            set_binding(scope, vars[1], item_t, CORD_cat("_$", vars[1]));
        }
        return scope;
    }
    case SetType: {
        if (for_->vars) {
            if (for_->vars->next)
                code_err(for_->vars->next->ast, "This is too many variables for this loop");
            type_t *item_type = Match(iter_t, SetType)->item_type;
            const char *name = Match(for_->vars->ast, Var)->name;
            set_binding(scope, name, item_type, CORD_cat("_$", name));
        }
        return scope;
    }
    case TableType: {
        const char *vars[2] = {};
        int64_t num_vars = 0;
        for (ast_list_t *var = for_->vars; var; var = var->next) {
            if (num_vars >= 2)
                code_err(var->ast, "This is too many variables for this loop");
            vars[num_vars++] = Match(var->ast, Var)->name;
        }

        type_t *key_t = Match(iter_t, TableType)->key_type;
        if (num_vars == 1) {
            set_binding(scope, vars[0], key_t, CORD_cat("_$", vars[0]));
        } else if (num_vars == 2) {
            set_binding(scope, vars[0], key_t, CORD_cat("_$", vars[0]));
            type_t *value_t = Match(iter_t, TableType)->value_type;
            set_binding(scope, vars[1], value_t, CORD_cat("_$", vars[1]));
        }
        return scope;
    }
    case BigIntType: {
        if (for_->vars) {
            if (for_->vars->next)
                code_err(for_->vars->next->ast, "This is too many variables for this loop");
            const char *var = Match(for_->vars->ast, Var)->name;
            set_binding(scope, var, INT_TYPE, CORD_cat("_$", var));
        }
        return scope;
    }
    case FunctionType: case ClosureType: {
        __typeof(iter_t->__data.FunctionType) *fn = iter_t->tag == ClosureType ? Match(Match(iter_t, ClosureType)->fn, FunctionType) : Match(iter_t, FunctionType);

        if (for_->vars) {
            if (for_->vars->next)
                code_err(for_->vars->next->ast, "This is too many variables for this loop");
            const char *var = Match(for_->vars->ast, Var)->name;
            type_t *non_opt_type = fn->ret->tag == OptionalType ? Match(fn->ret, OptionalType)->type : fn->ret;
            set_binding(scope, var, non_opt_type, CORD_cat("_$", var));
        }
        return scope;
    }
    default: code_err(for_->iter, "Iteration is not implemented for type: ", type_to_str(iter_t));
    }
    return NULL;
}

env_t *get_namespace_by_type(env_t *env, type_t *t)
{
    t = value_type(t);
    switch (t->tag) {
    case ListType: return NULL;
    case TableType: return NULL;
    case CStringType:
    case BoolType: case IntType: case BigIntType: case NumType: case ByteType: {
        binding_t *b = get_binding(env, CORD_to_const_char_star(type_to_cord(t)));
        assert(b);
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

env_t *namespace_env(env_t *env, const char *namespace_name)
{
    binding_t *b = get_binding(env, namespace_name);
    if (b)
        return Match(b->type, TypeInfoType)->env;

    env_t *ns_env = new(env_t);
    *ns_env = *env;
    ns_env->locals = new(Table_t, .fallback=env->locals);
    ns_env->namespace = new(namespace_t, .name=namespace_name, .parent=env->namespace);
    ns_env->namespace_bindings = ns_env->locals;
    return ns_env;
}

PUREFUNC binding_t *get_binding(env_t *env, const char *name)
{
    return Table$str_get(*env->locals, name);
}

binding_t *get_namespace_binding(env_t *env, ast_t *self, const char *name)
{
    type_t *self_type = get_type(env, self);
    if (!self_type)
        code_err(self, "I couldn't get this type");
    env_t *ns_env = get_namespace_by_type(env, self_type);
    return ns_env ? get_binding(ns_env, name) : NULL;
}

PUREFUNC binding_t *get_constructor(env_t *env, type_t *t, arg_ast_t *args)
{
    env_t *type_env = get_namespace_by_type(env, t);
    if (!type_env) return NULL;
    List_t constructors = type_env->namespace->constructors;
    // Prioritize exact matches:
    for (int64_t i = constructors.length-1; i >= 0; i--) {
        binding_t *b = constructors.data + i*constructors.stride;
        DeclareMatch(fn, b->type, FunctionType);
        if (type_eq(fn->ret, t) && is_valid_call(env, fn->args, args, false))
            return b;
    }
    // Fall back to promotion:
    for (int64_t i = constructors.length-1; i >= 0; i--) {
        binding_t *b = constructors.data + i*constructors.stride;
        DeclareMatch(fn, b->type, FunctionType);
        if (type_eq(fn->ret, t) && is_valid_call(env, fn->args, args, true))
            return b;
    }
    return NULL;
}

PUREFUNC binding_t *get_metamethod_binding(env_t *env, ast_e tag, ast_t *lhs, ast_t *rhs, type_t *ret)
{
    const char *method_name = binop_method_name(tag);
    if (!method_name) return NULL;
    binding_t *b = get_namespace_binding(env, lhs, method_name);
    if (!b || b->type->tag != FunctionType) return NULL;
    DeclareMatch(fn, b->type, FunctionType);
    if (!type_eq(fn->ret, ret)) return NULL;
    arg_ast_t *args = new(arg_ast_t, .value=lhs, .next=new(arg_ast_t, .value=rhs));
    return is_valid_call(env, fn->args, args, true) ? b : NULL;
}

void set_binding(env_t *env, const char *name, type_t *type, CORD code)
{
    assert(name);
    Table$str_set(env->locals, name, new(binding_t, .type=type, .code=code));
}

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
