// Logic for the environmental context information during compilation
// (variable bindings, code sections, etc.)
#include <stdlib.h>
#include <signal.h>
#include <sys/stat.h>

#include "environment.h"
#include "naming.h"
#include "parse.h"
#include "stdlib/datatypes.h"
#include "stdlib/paths.h"
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
        return Tableヽstr_get(*env->types, def->name);
    }
    case EnumDef: {
        DeclareMatch(def, statements->ast, EnumDef);
        prebind_statement(env, statements->ast);
        bind_statement(env, statements->ast);
        return Tableヽstr_get(*env->types, def->name);
    }
    default: errx(1, "Not a type definition: %s", def_str);
    }
    return NULL;
}

static type_t *bind_type(env_t *env, const char *name, type_t *type)
{
    if (Tableヽstr_get(*env->types, name))
        errx(1, "Duplicate binding for type: %s", name);
    Tableヽstr_set(env->types, name, type);
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
        Text_t typename;
        Text_t typeinfo;
        List_t namespace;
    } global_types[] = {
        {"Void", Type(VoidType), Text("Void_t"), Text("Voidヽinfo"), {}},
        {"Abort", Type(AbortType), Text("void"), Text("Abortヽinfo"), {}},
        {"Memory", Type(MemoryType), Text("Memory_t"), Text("Memoryヽinfo"), {}},
        {"Bool", Type(BoolType), Text("Bool_t"), Text("Boolヽinfo"), TypedList(ns_entry_t,
            {"parse", "Boolヽparse", "func(text:Text, remainder:&Text? = none -> Bool?)"},
        )},
        {"Byte", Type(ByteType), Text("Byte_t"), Text("Byteヽinfo"), TypedList(ns_entry_t,
            {"get_bit", "Byteヽget_bit", "func(x:Byte, bit_index:Int -> Bool)"},
            {"hex", "Byteヽhex", "func(byte:Byte, uppercase=yes, prefix=no -> Text)"},
            {"is_between", "Byteヽis_between", "func(x:Byte, low:Byte, high:Byte -> Bool)"},
            {"max", "Byteヽmax", "Byte"},
            {"min", "Byteヽmin", "Byte"},
            {"parse", "Byteヽparse", "func(text:Text, remainder:&Text? = none -> Byte?)"},
            {"to", "Byteヽto", "func(first:Byte, last:Byte, step:Int8?=none -> func(->Byte?))"},
        )},
        {"Int", Type(BigIntType), Text("Int_t"), Text("Intヽinfo"), TypedList(ns_entry_t,
            {"abs", "Intヽabs", "func(x:Int -> Int)"},
            {"bit_and", "Intヽbit_and", "func(x,y:Int -> Int)"},
            {"bit_or", "Intヽbit_or", "func(x,y:Int -> Int)"},
            {"bit_xor", "Intヽbit_xor", "func(x,y:Int -> Int)"},
            {"choose", "Intヽchoose", "func(x,y:Int -> Int)"},
            {"clamped", "Intヽclamped", "func(x,low,high:Int -> Int)"},
            {"divided_by", "Intヽdivided_by", "func(x,y:Int -> Int)"},
            {"factorial", "Intヽfactorial", "func(x:Int -> Int)"},
            {"gcd", "Intヽgcd", "func(x,y:Int -> Int)"},
            {"get_bit", "Intヽget_bit", "func(x,bit_index:Int -> Bool)"},
            {"hex", "Intヽhex", "func(i:Int, digits=0, uppercase=yes, prefix=yes -> Text)"},
            {"is_between", "Intヽis_between", "func(x:Int,low:Int,high:Int -> Bool)"},
            {"is_prime", "Intヽis_prime", "func(x:Int,reps=50 -> Bool)"},
            {"left_shifted", "Intヽleft_shifted", "func(x,y:Int -> Int)"},
            {"minus", "Intヽminus", "func(x,y:Int -> Int)"},
            {"modulo", "Intヽmodulo", "func(x,y:Int -> Int)"},
            {"modulo1", "Intヽmodulo1", "func(x,y:Int -> Int)"},
            {"negated", "Intヽnegated", "func(x:Int -> Int)"},
            {"negative", "Intヽnegative", "func(x:Int -> Int)"},
            {"next_prime", "Intヽnext_prime", "func(x:Int -> Int)"},
            {"octal", "Intヽoctal", "func(i:Int, digits=0, prefix=yes -> Text)"},
            {"onward", "Intヽonward", "func(first:Int,step=1 -> func(->Int?))"},
            {"parse", "Intヽparse", "func(text:Text, remainder:&Text? = none -> Int?)"},
            {"plus", "Intヽplus", "func(x,y:Int -> Int)"},
            {"power", "Intヽpower", "func(base:Int,exponent:Int -> Int)"},
#if __GNU_MP_VERSION >= 6
#if __GNU_MP_VERSION_MINOR >= 3
            {"prev_prime", "Intヽprev_prime", "func(x:Int -> Int?)"},
#endif
#endif
            {"right_shifted", "Intヽright_shifted", "func(x,y:Int -> Int)"},
            {"sqrt", "Intヽsqrt", "func(x:Int -> Int?)"},
            {"times", "Intヽtimes", "func(x,y:Int -> Int)"},
            {"to", "Intヽto", "func(first:Int,last:Int,step:Int?=none -> func(->Int?))"},
        )},
        {"Int64", Type(IntType, .bits=TYPE_IBITS64), Text("Int64_t"), Text("Int64ヽinfo"), TypedList(ns_entry_t,
            {"abs", "labs", "func(i:Int64 -> Int64)"},
            {"bits", "Int64ヽbits", "func(x:Int64 -> [Bool])"},
            {"clamped", "Int64ヽclamped", "func(x,low,high:Int64 -> Int64)"},
            {"divided_by", "Int64ヽdivided_by", "func(x,y:Int64 -> Int64)"},
            {"gcd", "Int64ヽgcd", "func(x,y:Int64 -> Int64)"},
            {"parse", "Int64ヽparse", "func(text:Text, remainder:&Text? = none -> Int64?)"},
            {"get_bit", "Int64ヽget_bit", "func(x:Int64, bit_index:Int -> Bool)"},
            {"hex", "Int64ヽhex", "func(i:Int64, digits=0, uppercase=yes, prefix=yes -> Text)"},
            {"is_between", "Int64ヽis_between", "func(x:Int64,low:Int64,high:Int64 -> Bool)"},
            {"max", "Int64ヽmax", "Int64"},
            {"min", "Int64ヽmin", "Int64"},
            {"modulo", "Int64ヽmodulo", "func(x,y:Int64 -> Int64)"},
            {"modulo1", "Int64ヽmodulo1", "func(x,y:Int64 -> Int64)"},
            {"octal", "Int64ヽoctal", "func(i:Int64, digits=0, prefix=yes -> Text)"},
            {"onward", "Int64ヽonward", "func(first:Int64,step=Int64(1) -> func(->Int64?))"},
            {"to", "Int64ヽto", "func(first:Int64,last:Int64,step:Int64?=none -> func(->Int64?))"},
            {"unsigned_left_shifted", "Int64ヽunsigned_left_shifted", "func(x:Int64,y:Int64 -> Int64)"},
            {"unsigned_right_shifted", "Int64ヽunsigned_right_shifted", "func(x:Int64,y:Int64 -> Int64)"},
            {"wrapping_minus", "Int64ヽwrapping_minus", "func(x:Int64,y:Int64 -> Int64)"},
            {"wrapping_plus", "Int64ヽwrapping_plus", "func(x:Int64,y:Int64 -> Int64)"},
        )},
        {"Int32", Type(IntType, .bits=TYPE_IBITS32), Text("Int32_t"), Text("Int32ヽinfo"), TypedList(ns_entry_t,
            {"abs", "abs", "func(i:Int32 -> Int32)"},
            {"bits", "Int32ヽbits", "func(x:Int32 -> [Bool])"},
            {"clamped", "Int32ヽclamped", "func(x,low,high:Int32 -> Int32)"},
            {"divided_by", "Int32ヽdivided_by", "func(x,y:Int32 -> Int32)"},
            {"gcd", "Int32ヽgcd", "func(x,y:Int32 -> Int32)"},
            {"parse", "Int32ヽparse", "func(text:Text, remainder:&Text? = none -> Int32?)"},
            {"get_bit", "Int32ヽget_bit", "func(x:Int32, bit_index:Int -> Bool)"},
            {"hex", "Int32ヽhex", "func(i:Int32, digits=0, uppercase=yes, prefix=yes -> Text)"},
            {"is_between", "Int32ヽis_between", "func(x:Int32,low:Int32,high:Int32 -> Bool)"},
            {"max", "Int32ヽmax", "Int32"},
            {"min", "Int32ヽmin", "Int32"},
            {"modulo", "Int32ヽmodulo", "func(x,y:Int32 -> Int32)"},
            {"modulo1", "Int32ヽmodulo1", "func(x,y:Int32 -> Int32)"},
            {"octal", "Int32ヽoctal", "func(i:Int32, digits=0, prefix=yes -> Text)"},
            {"onward", "Int32ヽonward", "func(first:Int32,step=Int32(1) -> func(->Int32?))"},
            {"to", "Int32ヽto", "func(first:Int32,last:Int32,step:Int32?=none -> func(->Int32?))"},
            {"unsigned_left_shifted", "Int32ヽunsigned_left_shifted", "func(x:Int32,y:Int32 -> Int32)"},
            {"unsigned_right_shifted", "Int32ヽunsigned_right_shifted", "func(x:Int32,y:Int32 -> Int32)"},
            {"wrapping_minus", "Int32ヽwrapping_minus", "func(x:Int32,y:Int32 -> Int32)"},
            {"wrapping_plus", "Int32ヽwrapping_plus", "func(x:Int32,y:Int32 -> Int32)"},
        )},
        {"Int16", Type(IntType, .bits=TYPE_IBITS16), Text("Int16_t"), Text("Int16ヽinfo"), TypedList(ns_entry_t,
            {"abs", "abs", "func(i:Int16 -> Int16)"},
            {"bits", "Int16ヽbits", "func(x:Int16 -> [Bool])"},
            {"clamped", "Int16ヽclamped", "func(x,low,high:Int16 -> Int16)"},
            {"divided_by", "Int16ヽdivided_by", "func(x,y:Int16 -> Int16)"},
            {"gcd", "Int16ヽgcd", "func(x,y:Int16 -> Int16)"},
            {"parse", "Int16ヽparse", "func(text:Text, remainder:&Text? = none -> Int16?)"},
            {"get_bit", "Int16ヽget_bit", "func(x:Int16, bit_index:Int -> Bool)"},
            {"hex", "Int16ヽhex", "func(i:Int16, digits=0, uppercase=yes, prefix=yes -> Text)"},
            {"is_between", "Int16ヽis_between", "func(x:Int16,low:Int16,high:Int16 -> Bool)"},
            {"max", "Int16ヽmax", "Int16"},
            {"min", "Int16ヽmin", "Int16"},
            {"modulo", "Int16ヽmodulo", "func(x,y:Int16 -> Int16)"},
            {"modulo1", "Int16ヽmodulo1", "func(x,y:Int16 -> Int16)"},
            {"octal", "Int16ヽoctal", "func(i:Int16, digits=0, prefix=yes -> Text)"},
            {"onward", "Int16ヽonward", "func(first:Int16,step=Int16(1) -> func(->Int16?))"},
            {"to", "Int16ヽto", "func(first:Int16,last:Int16,step:Int16?=none -> func(->Int16?))"},
            {"unsigned_left_shifted", "Int16ヽunsigned_left_shifted", "func(x:Int16,y:Int16 -> Int16)"},
            {"unsigned_right_shifted", "Int16ヽunsigned_right_shifted", "func(x:Int16,y:Int16 -> Int16)"},
            {"wrapping_minus", "Int16ヽwrapping_minus", "func(x:Int16,y:Int16 -> Int16)"},
            {"wrapping_plus", "Int16ヽwrapping_plus", "func(x:Int16,y:Int16 -> Int16)"},
        )},
        {"Int8", Type(IntType, .bits=TYPE_IBITS8), Text("Int8_t"), Text("Int8ヽinfo"), TypedList(ns_entry_t,
            {"abs", "abs", "func(i:Int8 -> Int8)"},
            {"bits", "Int8ヽbits", "func(x:Int8 -> [Bool])"},
            {"clamped", "Int8ヽclamped", "func(x,low,high:Int8 -> Int8)"},
            {"divided_by", "Int8ヽdivided_by", "func(x,y:Int8 -> Int8)"},
            {"gcd", "Int8ヽgcd", "func(x,y:Int8 -> Int8)"},
            {"parse", "Int8ヽparse", "func(text:Text, remainder:&Text? = none -> Int8?)"},
            {"get_bit", "Int8ヽget_bit", "func(x:Int8, bit_index:Int -> Bool)"},
            {"hex", "Int8ヽhex", "func(i:Int8, digits=0, uppercase=yes, prefix=yes -> Text)"},
            {"is_between", "Int8ヽis_between", "func(x:Int8,low:Int8,high:Int8 -> Bool)"},
            {"max", "Int8ヽmax", "Int8"},
            {"min", "Int8ヽmin", "Int8"},
            {"modulo", "Int8ヽmodulo", "func(x,y:Int8 -> Int8)"},
            {"modulo1", "Int8ヽmodulo1", "func(x,y:Int8 -> Int8)"},
            {"octal", "Int8ヽoctal", "func(i:Int8, digits=0, prefix=yes -> Text)"},
            {"onward", "Int8ヽonward", "func(first:Int8,step=Int8(1) -> func(->Int8?))"},
            {"to", "Int8ヽto", "func(first:Int8,last:Int8,step:Int8?=none -> func(->Int8?))"},
            {"unsigned_left_shifted", "Int8ヽunsigned_left_shifted", "func(x:Int8,y:Int8 -> Int8)"},
            {"unsigned_right_shifted", "Int8ヽunsigned_right_shifted", "func(x:Int8,y:Int8 -> Int8)"},
            {"wrapping_minus", "Int8ヽwrapping_minus", "func(x:Int8,y:Int8 -> Int8)"},
            {"wrapping_plus", "Int8ヽwrapping_plus", "func(x:Int8,y:Int8 -> Int8)"},
        )},
#define C(name) {#name, "M_"#name, "Num"}
#define F(name) {#name, #name, "func(n:Num -> Num)"}
#define F_opt(name) {#name, #name, "func(n:Num -> Num?)"}
#define F2(name) {#name, #name, "func(x,y:Num -> Num)"}
        {"Num", Type(NumType, .bits=TYPE_NBITS64), Text("Num_t"), Text("Numヽinfo"), TypedList(ns_entry_t,
            {"near", "Numヽnear", "func(x,y:Num, ratio=1e-9, min_epsilon=1e-9 -> Bool)"},
            {"clamped", "Numヽclamped", "func(x,low,high:Num -> Num)"},
            {"percent", "Numヽpercent", "func(n:Num,precision=0.01 -> Text)"},
            {"with_precision", "Numヽwith_precision", "func(n:Num,precision:Num -> Num)"},
            {"is_between", "Numヽis_between", "func(x:Num,low:Num,high:Num -> Bool)"},
            {"isinf", "Numヽisinf", "func(n:Num -> Bool)"},
            {"isfinite", "Numヽisfinite", "func(n:Num -> Bool)"},
            {"modulo", "Numヽmod", "func(x,y:Num -> Num)"},
            {"modulo1", "Numヽmod1", "func(x,y:Num -> Num)"},
            C(2_SQRTPI), C(E), C(PI_2), C(2_PI), C(1_PI), C(LN10), C(LN2), C(LOG2E),
            C(PI), C(PI_4), C(SQRT2), C(SQRT1_2),
            {"INF", "(Num_t)(INFINITY)", "Num"},
            {"TAU", "(Num_t)(2.*M_PI)", "Num"},
            {"mix", "Numヽmix", "func(amount,x,y:Num -> Num)"},
            {"parse", "Numヽparse", "func(text:Text, remainder:&Text? = none -> Num?)"},
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
        {"Num32", Type(NumType, .bits=TYPE_NBITS32), Text("Num32_t"), Text("Num32ヽinfo"), TypedList(ns_entry_t,
            {"near", "Num32ヽnear", "func(x,y:Num32, ratio=Num32(1e-9), min_epsilon=Num32(1e-9) -> Bool)"},
            {"clamped", "Num32ヽclamped", "func(x,low,high:Num32 -> Num32)"},
            {"percent", "Num32ヽpercent", "func(n:Num32,precision=Num32(.01) -> Text)"},
            {"with_precision", "Num32ヽwith_precision", "func(n:Num32,precision:Num32 -> Num32)"},
            {"is_between", "Num32ヽis_between", "func(x:Num32,low:Num32,high:Num32 -> Bool)"},
            {"isinf", "Num32ヽisinf", "func(n:Num32 -> Bool)"},
            {"isfinite", "Num32ヽisfinite", "func(n:Num32 -> Bool)"},
            C(2_SQRTPI), C(E), C(PI_2), C(2_PI), C(1_PI), C(LN10), C(LN2), C(LOG2E),
            C(PI), C(PI_4), C(SQRT2), C(SQRT1_2),
            {"INF", "(Num32_t)(INFINITY)", "Num32"},
            {"TAU", "(Num32_t)(2.f*M_PI)", "Num32"},
            {"mix", "Num32ヽmix", "func(amount,x,y:Num32 -> Num32)"},
            {"parse", "Num32ヽparse", "func(text:Text, remainder:&Text? = none -> Num32?)"},
            {"abs", "fabsf", "func(n:Num32 -> Num32)"},
            {"modulo", "Num32ヽmod", "func(x,y:Num32 -> Num32)"},
            {"modulo1", "Num32ヽmod1", "func(x,y:Num32 -> Num32)"},
            F_opt(acos), F_opt(acosh), F_opt(asin), F(asinh), F(atan), F_opt(atanh),
            F(cbrt), F(ceil), F_opt(cos), F(cosh), F(erf), F(erfc),
            F(exp), F(exp2), F(expm1), F(floor), F(j0), F(j1), F_opt(log), F_opt(log10), F_opt(log1p),
            F_opt(log2), F(logb), F(rint), F(round), F(significand), F_opt(sin), F(sinh), F_opt(sqrt),
            F_opt(tan), F(tanh), F_opt(tgamma), F(trunc), F_opt(y0), F_opt(y1),
            F2(atan2), F2(copysign), F2(fdim), F2(hypot), F2(nextafter),
        )},
        {"CString", Type(CStringType), Text("char*"), Text("CStringヽinfo"), TypedList(ns_entry_t,
            {"as_text", "Textヽfrom_str", "func(str:CString -> Text)"},
        )},
#undef F2
#undef F_opt
#undef F
#undef C
        {"PathType", PATH_TYPE_TYPE, Text("PathType_t"), Text("PathTypeヽinfo"), TypedList(ns_entry_t,
            {"Relative", "((PathType_t){.ヽtag=PATH_RELATIVE})", "PathType"},
            {"Absolute", "((PathType_t){.ヽtag=PATH_ABSOLUTE})", "PathType"},
            {"Home", "((PathType_t){.ヽtag=PATH_HOME})", "PathType"},
        )},
        {"Path", PATH_TYPE, Text("Path_t"), Text("Pathヽinfo"), TypedList(ns_entry_t,
            {"accessed", "Pathヽaccessed", "func(path:Path, follow_symlinks=yes -> Int64?)"},
            {"append", "Pathヽappend", "func(path:Path, text:Text, permissions=Int32(0o644))"},
            {"append_bytes", "Pathヽappend_bytes", "func(path:Path, bytes:[Byte], permissions=Int32(0o644))"},
            {"base_name", "Pathヽbase_name", "func(path:Path -> Text)"},
            {"by_line", "Pathヽby_line", "func(path:Path -> func(->Text?)?)"},
            {"can_execute", "Pathヽcan_execute", "func(path:Path -> Bool)"},
            {"can_read", "Pathヽcan_read", "func(path:Path -> Bool)"},
            {"can_write", "Pathヽcan_write", "func(path:Path -> Bool)"},
            {"changed", "Pathヽchanged", "func(path:Path, follow_symlinks=yes -> Int64?)"},
            {"child", "Pathヽchild", "func(path:Path, child:Text -> Path)"},
            {"children", "Pathヽchildren", "func(path:Path, include_hidden=no -> [Path])"},
            {"concatenated_with", "Pathヽconcat", "func(a,b:Path -> Path)"},
            {"create_directory", "Pathヽcreate_directory", "func(path:Path, permissions=Int32(0o755))"},
            {"current_dir", "Pathヽcurrent_dir", "func(->Path)"},
            {"exists", "Pathヽexists", "func(path:Path -> Bool)"},
            {"expand_home", "Pathヽexpand_home", "func(path:Path -> Path)"},
            {"extension", "Pathヽextension", "func(path:Path, full=yes -> Text)"},
            {"files", "Pathヽchildren", "func(path:Path, include_hidden=no -> [Path])"},
            {"from_components", "Pathヽfrom_components", "func(components:[Text] -> Path)"},
            {"glob", "Pathヽglob", "func(path:Path -> [Path])"},
            {"group", "Pathヽgroup", "func(path:Path, follow_symlinks=yes -> Text?)"},
            {"has_extension", "Pathヽhas_extension", "func(path:Path, extension:Text -> Bool)"},
            {"is_directory", "Pathヽis_directory", "func(path:Path, follow_symlinks=yes -> Bool)"},
            {"is_file", "Pathヽis_file", "func(path:Path, follow_symlinks=yes -> Bool)"},
            {"is_pipe", "Pathヽis_pipe", "func(path:Path, follow_symlinks=yes -> Bool)"},
            {"is_socket", "Pathヽis_socket", "func(path:Path, follow_symlinks=yes -> Bool)"},
            {"is_symlink", "Pathヽis_symlink", "func(path:Path -> Bool)"},
            {"modified", "Pathヽmodified", "func(path:Path, follow_symlinks=yes -> Int64?)"},
            {"owner", "Pathヽowner", "func(path:Path, follow_symlinks=yes -> Text?)"},
            {"parent", "Pathヽparent", "func(path:Path -> Path)"},
            {"read", "Pathヽread", "func(path:Path -> Text?)"},
            {"read_bytes", "Pathヽread_bytes", "func(path:Path, limit:Int?=none -> [Byte]?)"},
            {"relative_to", "Pathヽrelative_to", "func(path:Path, relative_to:Path -> Path)"},
            {"remove", "Pathヽremove", "func(path:Path, ignore_missing=no)"},
            {"resolved", "Pathヽresolved", "func(path:Path, relative_to=(./) -> Path)"},
            {"set_owner", "Pathヽset_owner", "func(path:Path, owner:Text?=none, group:Text?=none, follow_symlinks=yes)"},
            {"sibling", "Pathヽsibling", "func(path:Path, name:Text -> Path)"},
            {"subdirectories", "Pathヽchildren", "func(path:Path, include_hidden=no -> [Path])"},
            {"unique_directory", "Pathヽunique_directory", "func(path:Path -> Path)"},
            {"write", "Pathヽwrite", "func(path:Path, text:Text, permissions=Int32(0o644))"},
            {"write_bytes", "Pathヽwrite_bytes", "func(path:Path, bytes:[Byte], permissions=Int32(0o644))"},
            {"write_unique", "Pathヽwrite_unique", "func(path:Path, text:Text -> Path)"},
            {"write_unique_bytes", "Pathヽwrite_unique_bytes", "func(path:Path, bytes:[Byte] -> Path)"},
        )},
        {"Text", TEXT_TYPE, Text("Text_t"), Text("Textヽinfo"), TypedList(ns_entry_t,
            {"as_c_string", "Textヽas_c_string", "func(text:Text -> CString)"},
            {"at", "Textヽcluster", "func(text:Text, index:Int -> Text)"},
            {"by_line", "Textヽby_line", "func(text:Text -> func(->Text?))"},
            {"by_split", "Textヽby_split", "func(text:Text, delimiter='' -> func(->Text?))"},
            {"by_split_any", "Textヽby_split_any", "func(text:Text, delimiters=' \\t\\r\\n' -> func(->Text?))"},
            {"bytes", "Textヽutf8_bytes", "func(text:Text -> [Byte])"},
            {"caseless_equals", "Textヽequal_ignoring_case", "func(a,b:Text, language='C' -> Bool)"},
            {"codepoint_names", "Textヽcodepoint_names", "func(text:Text -> [Text])"},
            {"ends_with", "Textヽends_with", "func(text,suffix:Text, remainder:&Text? = none -> Bool)"},
            {"from", "Textヽfrom", "func(text:Text, first:Int -> Text)"},
            {"from_bytes", "Textヽfrom_bytes", "func(bytes:[Byte] -> Text?)"},
            {"from_c_string", "Textヽfrom_str", "func(str:CString -> Text?)"},
            {"from_codepoint_names", "Textヽfrom_codepoint_names", "func(codepoint_names:[Text] -> Text?)"},
            {"from_codepoints", "Textヽfrom_codepoints", "func(codepoints:[Int32] -> Text)"},
            {"from_text", "Pathヽfrom_text", "func(text:Text -> Path)"},
            {"has", "Textヽhas", "func(text:Text, target:Text -> Bool)"},
            {"join", "Textヽjoin", "func(glue:Text, pieces:[Text] -> Text)"},
            {"layout", "Textヽlayout", "func(text:Text -> Text)"},
            {"left_pad", "Textヽleft_pad", "func(text:Text, count:Int, pad=' ', language='C' -> Text)"},
            {"lines", "Textヽlines", "func(text:Text -> [Text])"},
            {"lower", "Textヽlower", "func(text:Text, language='C' -> Text)"},
            {"memory_size", "Textヽmemory_size", "func(text:Text -> Int)"},
            {"middle_pad", "Textヽmiddle_pad", "func(text:Text, count:Int, pad=' ', language='C' -> Text)"},
            {"quoted", "Textヽquoted", "func(text:Text, color=no, quotation_mark='\"' -> Text)"},
            {"repeat", "Textヽrepeat", "func(text:Text, count:Int -> Text)"},
            {"replace", "Textヽreplace", "func(text:Text, target:Text, replacement:Text -> Text)"},
            {"reversed", "Textヽreversed", "func(text:Text -> Text)"},
            {"right_pad", "Textヽright_pad", "func(text:Text, count:Int, pad=' ', language='C' -> Text)"},
            {"slice", "Textヽslice", "func(text:Text, from=1, to=-1 -> Text)"},
            {"split", "Textヽsplit", "func(text:Text, delimiter='' -> [Text])"},
            {"split_any", "Textヽsplit_any", "func(text:Text, delimiters=' \\t\\r\\n' -> [Text])"},
            {"starts_with", "Textヽstarts_with", "func(text,prefix:Text, remainder:&Text? = none -> Bool)"},
            {"title", "Textヽtitle", "func(text:Text, language='C' -> Text)"},
            {"to", "Textヽto", "func(text:Text, last:Int -> Text)"},
            {"translate", "Textヽtranslate", "func(text:Text, translations:{Text=Text} -> Text)"},
            {"trim", "Textヽtrim", "func(text:Text, to_trim=\" \t\r\n\", left=yes, right=yes -> Text)"},
            {"upper", "Textヽupper", "func(text:Text, language='C' -> Text)"},
            {"utf32_codepoints", "Textヽutf32_codepoints", "func(text:Text -> [Int32])"},
            {"width", "Textヽwidth", "func(text:Text, language='C' -> Int)"},
            {"without_prefix", "Textヽwithout_prefix", "func(text,prefix:Text -> Text)"},
            {"without_suffix", "Textヽwithout_suffix", "func(text,suffix:Text -> Text)"},
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
        Tableヽstr_set(env->globals, global_types[i].name, binding);
        Tableヽstr_set(env->types, global_types[i].name, global_types[i].type);
    }

    for (size_t i = 0; i < sizeof(global_types)/sizeof(global_types[0]); i++) {
        binding_t *type_binding = Tableヽstr_get(*env->globals, global_types[i].name);
        assert(type_binding);
        env_t *ns_env = Match(type_binding->type, TypeInfoType)->env;
        for (int64_t j = 0; j < global_types[i].namespace.length; j++) {
            ns_entry_t *entry = global_types[i].namespace.data + j*global_types[i].namespace.stride;
            type_t *type = parse_type_string(ns_env, entry->type_str);
            if (!type) compiler_err(NULL, NULL, NULL, "Couldn't parse type string: ", entry->type_str);
            if (type->tag == ClosureType) type = Match(type, ClosureType)->fn;
            set_binding(ns_env, entry->name, type, Textヽfrom_str(entry->code));
        }
    }


    // Conversion constructors:
#define ADD_CONSTRUCTORS(type_name, ...) do {\
    env_t *ns_env = namespace_env(env, type_name); \
    struct { const char *c_name, *type_str; } constructor_infos[] = {__VA_ARGS__}; \
    for (size_t i = 0; i < sizeof(constructor_infos)/sizeof(constructor_infos[0]); i++) { \
        type_t *t = parse_type_string(ns_env, constructor_infos[i].type_str); \
        Listヽinsert(&ns_env->namespace->constructors, \
                     ((binding_t[1]){{.code=Textヽfrom_str(constructor_infos[i].c_name), \
                      .type=Match(t, ClosureType)->fn}}), I(0), sizeof(binding_t)); \
    } \
} while (0)

    ADD_CONSTRUCTORS("Bool",
                     {"Boolヽfrom_byte", "func(b:Byte -> Bool)"},
                     {"Boolヽfrom_int8", "func(i:Int8 -> Bool)"},
                     {"Boolヽfrom_int16", "func(i:Int16 -> Bool)"},
                     {"Boolヽfrom_int32", "func(i:Int32 -> Bool)"},
                     {"Boolヽfrom_int64", "func(i:Int64 -> Bool)"},
                     {"Boolヽfrom_int", "func(i:Int -> Bool)"});
    ADD_CONSTRUCTORS("Byte",
                     {"Byteヽfrom_bool", "func(b:Bool -> Byte)"},
                     {"Byteヽfrom_int8", "func(i:Int8 -> Byte)"},
                     {"Byteヽfrom_int16", "func(i:Int16, truncate=no -> Byte)"},
                     {"Byteヽfrom_int32", "func(i:Int32, truncate=no -> Byte)"},
                     {"Byteヽfrom_int64", "func(i:Int64, truncate=no -> Byte)"},
                     {"Byteヽfrom_int", "func(i:Int, truncate=no -> Byte)"});
    ADD_CONSTRUCTORS("Int",
                     {"Intヽfrom_bool", "func(b:Bool -> Int)"},
                     {"Intヽfrom_byte", "func(b:Byte -> Int)"},
                     {"Intヽfrom_int8", "func(i:Int8 -> Int)"},
                     {"Intヽfrom_int16", "func(i:Int16 -> Int)"},
                     {"Intヽfrom_int32", "func(i:Int32 -> Int)"},
                     {"Intヽfrom_int64", "func(i:Int64 -> Int)"},
                     {"Intヽfrom_num", "func(n:Num, truncate=no -> Int)"},
                     {"Intヽfrom_num32", "func(n:Num32, truncate=no -> Int)"});
    ADD_CONSTRUCTORS("Int64",
                     {"Int64ヽfrom_bool", "func(b:Bool -> Int64)"},
                     {"Int64ヽfrom_byte", "func(b:Byte -> Int64)"},
                     {"Int64ヽfrom_int8", "func(i:Int8 -> Int64)"},
                     {"Int64ヽfrom_int16", "func(i:Int16 -> Int64)"},
                     {"Int64ヽfrom_int32", "func(i:Int32 -> Int64)"},
                     {"Int64ヽfrom_int", "func(i:Int, truncate=no -> Int64)"},
                     {"Int64ヽfrom_num", "func(n:Num, truncate=no -> Int64)"},
                     {"Int64ヽfrom_num32", "func(n:Num32, truncate=no -> Int64)"});
    ADD_CONSTRUCTORS("Int32",
                     {"Int32ヽfrom_bool", "func(b:Bool -> Int32)"},
                     {"Int32ヽfrom_byte", "func(b:Byte -> Int32)"},
                     {"Int32ヽfrom_int8", "func(i:Int8 -> Int32)"},
                     {"Int32ヽfrom_int16", "func(i:Int16 -> Int32)"},
                     {"Int32ヽfrom_int64", "func(i:Int64, truncate=no -> Int32)"},
                     {"Int32ヽfrom_int", "func(i:Int, truncate=no -> Int32)"},
                     {"Int32ヽfrom_num", "func(n:Num, truncate=no -> Int32)"},
                     {"Int32ヽfrom_num32", "func(n:Num32, truncate=no -> Int32)"});
    ADD_CONSTRUCTORS("Int16",
                     {"Int16ヽfrom_bool", "func(b:Bool -> Int16)"},
                     {"Int16ヽfrom_byte", "func(b:Byte -> Int16)"},
                     {"Int16ヽfrom_int8", "func(i:Int8 -> Int16)"},
                     {"Int16ヽfrom_int32", "func(i:Int32, truncate=no -> Int16)"},
                     {"Int16ヽfrom_int64", "func(i:Int64, truncate=no -> Int16)"},
                     {"Int16ヽfrom_int", "func(i:Int, truncate=no -> Int16)"},
                     {"Int16ヽfrom_num", "func(n:Num, truncate=no -> Int16)"},
                     {"Int16ヽfrom_num32", "func(n:Num32, truncate=no -> Int16)"});
    ADD_CONSTRUCTORS("Int8",
                     {"Int8ヽfrom_bool", "func(b:Bool -> Int8)"},
                     {"Int8ヽfrom_byte", "func(b:Byte -> Int8)"},
                     {"Int8ヽfrom_int16", "func(i:Int16, truncate=no -> Int8)"},
                     {"Int8ヽfrom_int32", "func(i:Int32, truncate=no -> Int8)"},
                     {"Int8ヽfrom_int64", "func(i:Int64, truncate=no -> Int8)"},
                     {"Int8ヽfrom_int", "func(i:Int, truncate=no -> Int8)"},
                     {"Int8ヽfrom_num", "func(n:Num, truncate=no -> Int8)"},
                     {"Int8ヽfrom_num32", "func(n:Num32, truncate=no -> Int8)"});
    ADD_CONSTRUCTORS("Num",
                     {"Numヽfrom_bool", "func(b:Bool -> Num)"},
                     {"Numヽfrom_byte", "func(b:Byte -> Num)"},
                     {"Numヽfrom_int8", "func(i:Int8 -> Num)"},
                     {"Numヽfrom_int16", "func(i:Int16 -> Num)"},
                     {"Numヽfrom_int32", "func(i:Int32 -> Num)"},
                     {"Numヽfrom_int64", "func(i:Int64, truncate=no -> Num)"},
                     {"Numヽfrom_int", "func(i:Int, truncate=no -> Num)"},
                     {"Numヽfrom_num32", "func(n:Num32 -> Num)"});
    ADD_CONSTRUCTORS("Num32",
                     {"Num32ヽfrom_bool", "func(b:Bool -> Num32)"},
                     {"Num32ヽfrom_byte", "func(b:Byte -> Num32)"},
                     {"Num32ヽfrom_int8", "func(i:Int8 -> Num32)"},
                     {"Num32ヽfrom_int16", "func(i:Int16 -> Num32)"},
                     {"Num32ヽfrom_int32", "func(i:Int32, truncate=no -> Num32)"},
                     {"Num32ヽfrom_int64", "func(i:Int64, truncate=no -> Num32)"},
                     {"Num32ヽfrom_int", "func(i:Int, truncate=no -> Num32)"},
                     {"Num32ヽfrom_num", "func(n:Num -> Num32)"});
    ADD_CONSTRUCTORS("Path",
                     {"Pathヽescape_text", "func(text:Text -> Path)"},
                     {"Pathヽescape_path", "func(path:Path -> Path)"},
                     {"Intヽvalue_as_text", "func(i:Int -> Path)"});
    ADD_CONSTRUCTORS("CString", {"Textヽas_c_string", "func(text:Text -> CString)"});
#undef ADD_CONSTRUCTORS

    set_binding(namespace_env(env, "Path"), "from_text",
                NewFunctionType(PATH_TYPE, {.name="text", .type=TEXT_TYPE}),
                Text("Pathヽfrom_text"));

    struct {
        const char *name, *code, *type_str;
    } global_vars[] = {
        {"USE_COLOR", "USE_COLOR", "Bool"},
        {"TOMO_VERSION", "TOMO_VERSION_TEXT", "Text"},
        {"say", "say", "func(text:Text, newline=yes)"},
        {"print", "say", "func(text:Text, newline=yes)"},
        {"getenv", "getenv_text", "func(name:Text -> Text?)"},
        {"setenv", "setenv_text", "func(name:Text, value:Text -> Text?)"},
        {"ask", "ask", "func(prompt:Text, bold=yes, force_tty=yes -> Text?)"},
        {"exit", "tomo_exit", "func(message:Text?=none, code=Int32(1) -> Abort)"},
        {"fail", "fail_text", "func(message:Text -> Abort)"},
        {"sleep", "sleep_num", "func(seconds:Num)"},
    };

    for (size_t i = 0; i < sizeof(global_vars)/sizeof(global_vars[0]); i++) {
        type_t *type = parse_type_string(env, global_vars[i].type_str);
        if (!type) compiler_err(NULL, NULL, NULL, "Couldn't parse type string for ", global_vars[i].name, ": ", global_vars[i].type_str);
        if (type->tag == ClosureType) type = Match(type, ClosureType)->fn;
        Tableヽstr_set(env->globals, global_vars[i].name, new(binding_t, .type=type, .code=Textヽfrom_str(global_vars[i].code)));
    }

    _global_env = env;
    return env;
}

env_t *load_module_env(env_t *env, ast_t *ast)
{
    const char *name = ast->file->filename;
    env_t *cached = Tableヽstr_get(*env->imports, name);
    if (cached) return cached;
    env_t *module_env = fresh_scope(env);
    module_env->code = new(compilation_unit_t);
    module_env->namespace_bindings = module_env->locals;
    module_env->id_suffix = get_id_suffix(ast->file->filename);

    Tableヽstr_set(module_env->imports, name, module_env);

    ast_list_t *statements = Match(ast, Block)->statements;
    visit_topologically(statements, (Closure_t){.fn=(void*)prebind_statement, .userdata=module_env});
    visit_topologically(statements, (Closure_t){.fn=(void*)bind_statement, .userdata=module_env});

    return module_env;
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
        Tableヽstr_set(env->locals, tag->name, b);
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
            set_binding(scope, vars[0], item_t, USER_ID(vars[0]));
        } else if (num_vars == 2) {
            set_binding(scope, vars[0], INT_TYPE, USER_ID(vars[0]));
            set_binding(scope, vars[1], item_t, USER_ID(vars[1]));
        }
        return scope;
    }
    case SetType: {
        if (for_->vars) {
            if (for_->vars->next)
                code_err(for_->vars->next->ast, "This is too many variables for this loop");
            type_t *item_type = Match(iter_t, SetType)->item_type;
            const char *name = Match(for_->vars->ast, Var)->name;
            set_binding(scope, name, item_type, USER_ID(name));
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
            set_binding(scope, vars[0], key_t, USER_ID(vars[0]));
        } else if (num_vars == 2) {
            set_binding(scope, vars[0], key_t, USER_ID(vars[0]));
            type_t *value_t = Match(iter_t, TableType)->value_type;
            set_binding(scope, vars[1], value_t, USER_ID(vars[1]));
        }
        return scope;
    }
    case BigIntType: {
        if (for_->vars) {
            if (for_->vars->next)
                code_err(for_->vars->next->ast, "This is too many variables for this loop");
            const char *var = Match(for_->vars->ast, Var)->name;
            set_binding(scope, var, INT_TYPE, USER_ID(var));
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
            set_binding(scope, var, non_opt_type, USER_ID(var));
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
        binding_t *b = get_binding(env, Textヽas_c_string(type_to_text(t)));
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
    return Tableヽstr_get(*env->locals, name);
}

binding_t *get_namespace_binding(env_t *env, ast_t *self, const char *name)
{
    type_t *self_type = get_type(env, self);
    if (!self_type)
        code_err(self, "I couldn't get this type");
    env_t *ns_env = get_namespace_by_type(env, self_type);
    return ns_env ? get_binding(ns_env, name) : NULL;
}

PUREFUNC binding_t *get_constructor(env_t *env, type_t *t, arg_ast_t *args, bool allow_underscores)
{
    env_t *type_env = get_namespace_by_type(env, t);
    if (!type_env) return NULL;
    List_t constructors = type_env->namespace->constructors;
    // Prioritize exact matches:
    for (int64_t i = constructors.length-1; i >= 0; i--) {
        binding_t *b = constructors.data + i*constructors.stride;
        DeclareMatch(fn, b->type, FunctionType);
        if (!allow_underscores) {
            for (arg_t *arg = fn->args; arg; arg = arg->next)
                if (arg->name[0] == '_')
                    goto next_constructor;
        }
        if (type_eq(fn->ret, t) && is_valid_call(env, fn->args, args, false))
            return b;
      next_constructor: continue;
    }
    // Fall back to promotion:
    for (int64_t i = constructors.length-1; i >= 0; i--) {
        binding_t *b = constructors.data + i*constructors.stride;
        DeclareMatch(fn, b->type, FunctionType);
        if (!allow_underscores) {
            for (arg_t *arg = fn->args; arg; arg = arg->next)
                if (arg->name[0] == '_')
                    goto next_constructor2;
        }
        if (type_eq(fn->ret, t) && is_valid_call(env, fn->args, args, true))
            return b;
      next_constructor2: continue;
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

void set_binding(env_t *env, const char *name, type_t *type, Text_t code)
{
    assert(name);
    Tableヽstr_set(env->locals, name, new(binding_t, .type=type, .code=code));
}

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
