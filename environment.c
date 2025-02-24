// Logic for the environmental context information during compilation
// (variable bindings, code sections, etc.)
#include <stdlib.h>
#include <signal.h>

#include "cordhelpers.h"
#include "environment.h"
#include "stdlib/datatypes.h"
#include "stdlib/tables.h"
#include "stdlib/text.h"
#include "stdlib/util.h"
#include "typecheck.h"

type_t *TEXT_TYPE = NULL;
type_t *MATCH_TYPE = NULL;
type_t *RNG_TYPE = NULL;
public type_t *THREAD_TYPE = NULL;

env_t *new_compilation_unit(CORD libname)
{
    env_t *env = new(env_t);
    env->code = new(compilation_unit_t);
    env->types = new(Table_t);
    env->globals = new(Table_t);
    env->locals = new(Table_t, .fallback=env->globals);
    env->imports = new(Table_t);

    if (!TEXT_TYPE)
        TEXT_TYPE = Type(TextType, .env=namespace_env(env, "Text"));

    struct {
        const char *name;
        binding_t binding;
    } global_vars[] = {
        {"say", {.code="say", .type=Type(
                    FunctionType, .args=new(arg_t, .name="text", .type=TEXT_TYPE,
                                            .next=new(arg_t, .name="newline", .type=Type(BoolType), .default_val=FakeAST(Bool, true))),
                    .ret=Type(VoidType))}},
        {"ask", {.code="ask", .type=Type(
                    FunctionType, .args=new(arg_t, .name="prompt", .type=TEXT_TYPE,
                                            .next=new(arg_t, .name="bold", .type=Type(BoolType),
                                                      .default_val=FakeAST(Bool, true),
                                                      .next=new(arg_t, .name="force_tty", .type=Type(BoolType),
                                                                .default_val=FakeAST(Bool, true)))),
                    .ret=Type(OptionalType, TEXT_TYPE))}},
        {"exit", {.code="tomo_exit",
                     .type=Type(FunctionType, .args=new(
                             arg_t, .name="message", .type=Type(OptionalType, .type=Type(TextType)),
                             .default_val=FakeAST(None, .type=new(type_ast_t, .tag=VarTypeAST, .__data.VarTypeAST.name="Text")),
                             .next=new(arg_t, .name="code", .type=Type(IntType, .bits=TYPE_IBITS32),
                                       .default_val=FakeAST(InlineCCode, .code="1", .type=Type(IntType, .bits=TYPE_IBITS32)))),
                         .ret=Type(AbortType))}},
        {"fail", {.code="fail", .type=Type(FunctionType, .args=new(arg_t, .name="message", .type=Type(CStringType)), .ret=Type(AbortType))}},
        {"sleep", {.code="sleep_num", .type=Type(FunctionType, .args=new(arg_t, .name="seconds", .type=Type(NumType, .bits=TYPE_NBITS64)), .ret=Type(VoidType))}},
        {"now", {.code="Moment$now", .type=Type(FunctionType, .args=NULL, .ret=Type(MomentType))}},
        {"USE_COLOR", {.code="USE_COLOR", .type=Type(BoolType)}},
    };

    for (size_t i = 0; i < sizeof(global_vars)/sizeof(global_vars[0]); i++) {
        binding_t *b = new(binding_t);
        *b = global_vars[i].binding;
        Table$str_set(env->globals, global_vars[i].name, b);
    }

    typedef struct {
        const char *name, *code, *type_str;
    } ns_entry_t;

    {
        env_t *match_env = namespace_env(env, "Match");
        MATCH_TYPE = Type(
            StructType, .name="Match", .env=match_env,
            .fields=new(arg_t, .name="text", .type=TEXT_TYPE,
              .next=new(arg_t, .name="index", .type=INT_TYPE,
              .next=new(arg_t, .name="captures", .type=Type(ArrayType, .item_type=TEXT_TYPE)))));
    }

    {
        env_t *thread_env = namespace_env(env, "Thread");
        THREAD_TYPE = Type(StructType, .name="Thread", .env=thread_env, .opaque=true);
    }

    {
        env_t *rng_env = namespace_env(env, "RNG");
        RNG_TYPE = Type(
            StructType, .name="RNG", .env=rng_env,
            .fields=new(arg_t, .name="state", .type=Type(PointerType, .pointed=Type(MemoryType))));
    }

    struct {
        const char *name;
        type_t *type;
        CORD typename;
        CORD typeinfo;
        Array_t namespace;
    } global_types[] = {
        {"Void", Type(VoidType), "Void_t", "Void$info", {}},
        {"Memory", Type(MemoryType), "Memory_t", "Memory$info", {}},
        {"Bool", Type(BoolType), "Bool_t", "Bool$info", TypedArray(ns_entry_t,
            {"parse", "Bool$parse", "func(text:Text -> Bool?)"},
        )},
        {"Byte", Type(ByteType), "Byte_t", "Byte$info", TypedArray(ns_entry_t,
            {"max", "Byte$max", "Byte"},
            {"hex", "Byte$hex", "func(byte:Byte, uppercase=yes, prefix=no -> Text)"},
            {"min", "Byte$min", "Byte"},
        )},
        {"Int", Type(BigIntType), "Int_t", "Int$info", TypedArray(ns_entry_t,
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
            {"prev_prime", "Int$prev_prime", "func(x:Int -> Int)"},
            {"right_shifted", "Int$right_shifted", "func(x,y:Int -> Int)"},
            {"sqrt", "Int$sqrt", "func(x:Int -> Int?)"},
            {"times", "Int$times", "func(x,y:Int -> Int)"},
            {"to", "Int$to", "func(first:Int,last:Int,step=none:Int -> func(->Int?))"},
        )},
        {"Int64", Type(IntType, .bits=TYPE_IBITS64), "Int64_t", "Int64$info", TypedArray(ns_entry_t,
            {"abs", "labs", "func(i:Int64 -> Int64)"},
            {"bits", "Int64$bits", "func(x:Int64 -> [Bool])"},
            {"clamped", "Int64$clamped", "func(x,low,high:Int64 -> Int64)"},
            {"divided_by", "Int64$divided_by", "func(x,y:Int64 -> Int64)"},
            {"format", "Int64$format", "func(i:Int64, digits=0 -> Text)"},
            {"gcd", "Int64$gcd", "func(x,y:Int64 -> Int64)"},
            {"parse", "Int64$parse", "func(text:Text -> Int64?)"},
            {"hex", "Int64$hex", "func(i:Int64, digits=0, uppercase=yes, prefix=yes -> Text)"},
            {"max", "Int64$max", "Int64"},
            {"min", "Int64$min", "Int64"},
            {"modulo", "Int64$modulo", "func(x,y:Int64 -> Int64)"},
            {"modulo1", "Int64$modulo1", "func(x,y:Int64 -> Int64)"},
            {"octal", "Int64$octal", "func(i:Int64, digits=0, prefix=yes -> Text)"},
            {"onward", "Int64$onward", "func(first:Int64,step=Int64(1) -> func(->Int64?))"},
            {"to", "Int64$to", "func(first:Int64,last:Int64,step=none:Int64 -> func(->Int64?))"},
            {"unsigned_left_shifted", "Int64$unsigned_left_shifted", "func(x:Int64,y:Int64 -> Int64)"},
            {"unsigned_right_shifted", "Int64$unsigned_right_shifted", "func(x:Int64,y:Int64 -> Int64)"},
            {"wrapping_minus", "Int64$wrapping_minus", "func(x:Int64,y:Int64 -> Int64)"},
            {"wrapping_plus", "Int64$wrapping_plus", "func(x:Int64,y:Int64 -> Int64)"},
        )},
        {"Int32", Type(IntType, .bits=TYPE_IBITS32), "Int32_t", "Int32$info", TypedArray(ns_entry_t,
            {"abs", "abs", "func(i:Int32 -> Int32)"},
            {"bits", "Int32$bits", "func(x:Int32 -> [Bool])"},
            {"clamped", "Int32$clamped", "func(x,low,high:Int32 -> Int32)"},
            {"divided_by", "Int32$divided_by", "func(x,y:Int32 -> Int32)"},
            {"format", "Int32$format", "func(i:Int32, digits=0 -> Text)"},
            {"gcd", "Int32$gcd", "func(x,y:Int32 -> Int32)"},
            {"parse", "Int32$parse", "func(text:Text -> Int32?)"},
            {"hex", "Int32$hex", "func(i:Int32, digits=0, uppercase=yes, prefix=yes -> Text)"},
            {"max", "Int32$max", "Int32"},
            {"min", "Int32$min", "Int32"},
            {"modulo", "Int32$modulo", "func(x,y:Int32 -> Int32)"},
            {"modulo1", "Int32$modulo1", "func(x,y:Int32 -> Int32)"},
            {"octal", "Int32$octal", "func(i:Int32, digits=0, prefix=yes -> Text)"},
            {"onward", "Int32$onward", "func(first:Int32,step=Int32(1) -> func(->Int32?))"},
            {"to", "Int32$to", "func(first:Int32,last:Int32,step=none:Int32 -> func(->Int32?))"},
            {"unsigned_left_shifted", "Int32$unsigned_left_shifted", "func(x:Int32,y:Int32 -> Int32)"},
            {"unsigned_right_shifted", "Int32$unsigned_right_shifted", "func(x:Int32,y:Int32 -> Int32)"},
            {"wrapping_minus", "Int32$wrapping_minus", "func(x:Int32,y:Int32 -> Int32)"},
            {"wrapping_plus", "Int32$wrapping_plus", "func(x:Int32,y:Int32 -> Int32)"},
        )},
        {"Int16", Type(IntType, .bits=TYPE_IBITS16), "Int16_t", "Int16$info", TypedArray(ns_entry_t,
            {"abs", "abs", "func(i:Int16 -> Int16)"},
            {"bits", "Int16$bits", "func(x:Int16 -> [Bool])"},
            {"clamped", "Int16$clamped", "func(x,low,high:Int16 -> Int16)"},
            {"divided_by", "Int16$divided_by", "func(x,y:Int16 -> Int16)"},
            {"format", "Int16$format", "func(i:Int16, digits=0 -> Text)"},
            {"gcd", "Int16$gcd", "func(x,y:Int16 -> Int16)"},
            {"parse", "Int16$parse", "func(text:Text -> Int16?)"},
            {"hex", "Int16$hex", "func(i:Int16, digits=0, uppercase=yes, prefix=yes -> Text)"},
            {"max", "Int16$max", "Int16"},
            {"min", "Int16$min", "Int16"},
            {"modulo", "Int16$modulo", "func(x,y:Int16 -> Int16)"},
            {"modulo1", "Int16$modulo1", "func(x,y:Int16 -> Int16)"},
            {"octal", "Int16$octal", "func(i:Int16, digits=0, prefix=yes -> Text)"},
            {"onward", "Int16$onward", "func(first:Int16,step=Int16(1) -> func(->Int16?))"},
            {"to", "Int16$to", "func(first:Int16,last:Int16,step=none:Int16 -> func(->Int16?))"},
            {"unsigned_left_shifted", "Int16$unsigned_left_shifted", "func(x:Int16,y:Int16 -> Int16)"},
            {"unsigned_right_shifted", "Int16$unsigned_right_shifted", "func(x:Int16,y:Int16 -> Int16)"},
            {"wrapping_minus", "Int16$wrapping_minus", "func(x:Int16,y:Int16 -> Int16)"},
            {"wrapping_plus", "Int16$wrapping_plus", "func(x:Int16,y:Int16 -> Int16)"},
        )},
        {"Int8", Type(IntType, .bits=TYPE_IBITS8), "Int8_t", "Int8$info", TypedArray(ns_entry_t,
            {"abs", "abs", "func(i:Int8 -> Int8)"},
            {"bits", "Int8$bits", "func(x:Int8 -> [Bool])"},
            {"clamped", "Int8$clamped", "func(x,low,high:Int8 -> Int8)"},
            {"divided_by", "Int8$divided_by", "func(x,y:Int8 -> Int8)"},
            {"format", "Int8$format", "func(i:Int8, digits=0 -> Text)"},
            {"gcd", "Int8$gcd", "func(x,y:Int8 -> Int8)"},
            {"parse", "Int8$parse", "func(text:Text -> Int8?)"},
            {"hex", "Int8$hex", "func(i:Int8, digits=0, uppercase=yes, prefix=yes -> Text)"},
            {"max", "Int8$max", "Int8"},
            {"min", "Int8$min", "Int8"},
            {"modulo", "Int8$modulo", "func(x,y:Int8 -> Int8)"},
            {"modulo1", "Int8$modulo1", "func(x,y:Int8 -> Int8)"},
            {"octal", "Int8$octal", "func(i:Int8, digits=0, prefix=yes -> Text)"},
            {"onward", "Int8$onward", "func(first:Int8,step=Int8(1) -> func(->Int8?))"},
            {"to", "Int8$to", "func(first:Int8,last:Int8,step=none:Int8 -> func(->Int8?))"},
            {"unsigned_left_shifted", "Int8$unsigned_left_shifted", "func(x:Int8,y:Int8 -> Int8)"},
            {"unsigned_right_shifted", "Int8$unsigned_right_shifted", "func(x:Int8,y:Int8 -> Int8)"},
            {"wrapping_minus", "Int8$wrapping_minus", "func(x:Int8,y:Int8 -> Int8)"},
            {"wrapping_plus", "Int8$wrapping_plus", "func(x:Int8,y:Int8 -> Int8)"},
        )},
#define C(name) {#name, "M_"#name, "Num"}
#define F(name) {#name, #name, "func(n:Num -> Num)"}
#define F_opt(name) {#name, #name, "func(n:Num -> Num?)"}
#define F2(name) {#name, #name, "func(x,y:Num -> Num)"}
        {"Num", Type(NumType, .bits=TYPE_NBITS64), "Num_t", "Num$info", TypedArray(ns_entry_t,
            {"near", "Num$near", "func(x,y:Num, ratio=1e-9, min_epsilon=1e-9 -> Bool)"},
            {"clamped", "Num$clamped", "func(x,low,high:Num -> Num)"},
            {"format", "Num$format", "func(n:Num, precision=0 -> Text)"},
            {"scientific", "Num$scientific", "func(n:Num,precision=0 -> Text)"},
            {"isinf", "Num$isinf", "func(n:Num -> Bool)"},
            {"isfinite", "Num$isfinite", "func(n:Num -> Bool)"},
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
        {"Num32", Type(NumType, .bits=TYPE_NBITS32), "Num32_t", "Num32$info", TypedArray(ns_entry_t,
            {"near", "Num32$near", "func(x,y:Num32, ratio=Num32(1e-9), min_epsilon=Num32(1e-9) -> Bool)"},
            {"clamped", "Num32$clamped", "func(x,low,high:Num32 -> Num32)"},
            {"format", "Num32$format", "func(n:Num32, precision=0 -> Text)"},
            {"scientific", "Num32$scientific", "func(n:Num32, precision=0 -> Text)"},
            {"isinf", "Num32$isinf", "func(n:Num32 -> Bool)"},
            {"isfinite", "Num32$isfinite", "func(n:Num32 -> Bool)"},
            C(2_SQRTPI), C(E), C(PI_2), C(2_PI), C(1_PI), C(LN10), C(LN2), C(LOG2E),
            C(PI), C(PI_4), C(SQRT2), C(SQRT1_2),
            {"INF", "(Num32_t)(INFINITY)", "Num32"},
            {"TAU", "(Num32_t)(2.f*M_PI)", "Num32"},
            {"mix", "Num32$mix", "func(amount,x,y:Num32 -> Num32)"},
            {"parse", "Num32$parse", "func(text:Text -> Num32?)"},
            {"abs", "fabsf", "func(n:Num32 -> Num32)"},
            F_opt(acos), F_opt(acosh), F_opt(asin), F(asinh), F(atan), F_opt(atanh),
            F(cbrt), F(ceil), F_opt(cos), F(cosh), F(erf), F(erfc),
            F(exp), F(exp2), F(expm1), F(floor), F(j0), F(j1), F_opt(log), F_opt(log10), F_opt(log1p),
            F_opt(log2), F(logb), F(rint), F(round), F(significand), F_opt(sin), F(sinh), F_opt(sqrt),
            F_opt(tan), F(tanh), F_opt(tgamma), F(trunc), F_opt(y0), F_opt(y1),
            F2(atan2), F2(copysign), F2(fdim), F2(hypot), F2(nextafter),
        )},
        {"CString", Type(CStringType), "char*", "CString$info", TypedArray(ns_entry_t,
            {"as_text", "CString$as_text_simple", "func(str:CString -> Text)"},
        )},
#undef F2
#undef F_opt
#undef F
#undef C
        {"Match", MATCH_TYPE, "Match_t", "Match", TypedArray(ns_entry_t,
             // No methods
        )},
        {"Pattern", Type(TextType, .lang="Pattern", .env=namespace_env(env, "Pattern")), "Pattern_t", "Pattern$info", TypedArray(ns_entry_t,
            {"escape_int", "Int$value_as_text", "func(i:Int -> Pattern)"},
            {"escape_text", "Pattern$escape_text", "func(text:Text -> Pattern)"},
        )},
        {"Moment", Type(MomentType), "Moment_t", "Moment", TypedArray(ns_entry_t,
            // Used as a default for functions below:
            {"now", "Moment$now", "func(->Moment)"},

            {"after", "Moment$after", "func(moment:Moment,seconds,minutes,hours=0.0,days,weeks,months,years=0,timezone=none:Text -> Moment)"},
            {"date", "Moment$date", "func(moment:Moment,timezone=none:Text -> Text)"},
            {"day_of_month", "Moment$day_of_month", "func(moment:Moment,timezone=none:Text -> Int)"},
            {"day_of_week", "Moment$day_of_week", "func(moment:Moment,timezone=none:Text -> Int)"},
            {"day_of_year", "Moment$day_of_year", "func(moment:Moment,timezone=none:Text -> Int)"},
            {"format", "Moment$format", "func(moment:Moment,format=\"%Y-%m-%dT%H:%M:%S%z\",timezone=none:Text -> Text)"},
            {"from_unix_timestamp", "Moment$from_unix_timestamp", "func(timestamp:Int64 -> Moment)"},
            {"get_local_timezone", "Moment$get_local_timezone", "func(->Text)"},
            {"hour", "Moment$hour", "func(moment:Moment,timezone=none:Text -> Int)"},
            {"hours_till", "Moment$hours_till", "func(now,then:Moment -> Num)"},
            {"minute", "Moment$minute", "func(moment:Moment,timezone=none:Text -> Int)"},
            {"minutes_till", "Moment$minutes_till", "func(now,then:Moment -> Num)"},
            {"month", "Moment$month", "func(moment:Moment,timezone=none:Text -> Int)"},
            {"microsecond", "Moment$microsecond", "func(moment:Moment,timezone=none:Text -> Int)"},
            {"new", "Moment$new", "func(year,month,day:Int,hour,minute=0,second=0.0,timezone=none:Text -> Moment)"},
            {"parse", "Moment$parse", "func(text:Text, format=\"%Y-%m-%dT%H:%M:%S%z\" -> Moment?)"},
            {"relative", "Moment$relative", "func(moment:Moment,relative_to=Moment.now(),timezone=none:Text -> Text)"},
            {"second", "Moment$second", "func(moment:Moment,timezone=none:Text -> Int)"},
            {"seconds_till", "Moment$seconds_till", "func(now:Moment,then:Moment -> Num)"},
            {"set_local_timezone", "Moment$set_local_timezone", "func(timezone=none:Text)"},
            {"time", "Moment$time", "func(moment:Moment,seconds=no,am_pm=yes,timezone=none:Text -> Text)"},
            {"unix_timestamp", "Moment$unix_timestamp", "func(moment:Moment -> Int64)"},
            {"year", "Moment$year", "func(moment:Moment,timezone=none:Text -> Int)"},
        )},
        {"Path", Type(TextType, .lang="Path", .env=namespace_env(env, "Path")), "Text_t", "Text$info", TypedArray(ns_entry_t,
            {"append", "Path$append", "func(path:Path, text:Text, permissions=Int32(0o644))"},
            {"append_bytes", "Path$append_bytes", "func(path:Path, bytes:[Byte], permissions=Int32(0o644))"},
            {"base_name", "Path$base_name", "func(path:Path -> Text)"},
            {"by_line", "Path$by_line", "func(path:Path -> func(->Text?)?)"},
            {"children", "Path$children", "func(path:Path, include_hidden=no -> [Path])"},
            {"create_directory", "Path$create_directory", "func(path:Path, permissions=Int32(0o755))"},
            {"escape_int", "Int$value_as_text", "func(i:Int -> Path)"},
            {"escape_path", "Path$escape_path", "func(path:Path -> Path)"},
            {"escape_text", "Path$escape_text", "func(text:Text -> Path)"},
            {"exists", "Path$exists", "func(path:Path -> Bool)"},
            {"extension", "Path$extension", "func(path:Path, full=yes -> Text)"},
            {"files", "Path$children", "func(path:Path, include_hidden=no -> [Path])"},
            {"glob", "Path$glob", "func(path:Path -> [Path])"},
            {"is_directory", "Path$is_directory", "func(path:Path, follow_symlinks=yes -> Bool)"},
            {"is_file", "Path$is_file", "func(path:Path, follow_symlinks=yes -> Bool)"},
            {"is_pipe", "Path$is_pipe", "func(path:Path, follow_symlinks=yes -> Bool)"},
            {"is_socket", "Path$is_socket", "func(path:Path, follow_symlinks=yes -> Bool)"},
            {"is_symlink", "Path$is_symlink", "func(path:Path -> Bool)"},
            {"parent", "Path$parent", "func(path:Path -> Path)"},
            {"read", "Path$read", "func(path:Path -> Text?)"},
            {"read_bytes", "Path$read_bytes", "func(path:Path, limit=none:Int -> [Byte]?)"},
            {"relative", "Path$relative", "func(path:Path, relative_to=(./) -> Path)"},
            {"remove", "Path$remove", "func(path:Path, ignore_missing=no)"},
            {"resolved", "Path$resolved", "func(path:Path, relative_to=(./) -> Path)"},
            {"subdirectories", "Path$children", "func(path:Path, include_hidden=no -> [Path])"},
            {"unique_directory", "Path$unique_directory", "func(path:Path -> Path)"},
            {"write", "Path$write", "func(path:Path, text:Text, permissions=Int32(0o644))"},
            {"write_bytes", "Path$write_bytes", "func(path:Path, bytes:[Byte], permissions=Int32(0o644))"},
            {"write_unique", "Path$write_unique", "func(path:Path, text:Text -> Path)"},
            {"write_unique_bytes", "Path$write_unique_bytes", "func(path:Path, bytes:[Byte] -> Path)"},

            {"modified", "Path$modified", "func(path:Path, follow_symlinks=yes -> Moment?)"},
            {"accessed", "Path$accessed", "func(path:Path, follow_symlinks=yes -> Moment?)"},
            {"changed", "Path$changed", "func(path:Path, follow_symlinks=yes -> Moment?)"},

            // Text methods:
            {"ends_with", "Text$ends_with", "func(path:Path, suffix:Text -> Bool)"},
            {"has", "Text$has", "func(path:Path, pattern:Pattern -> Bool)"},
            {"matches", "Text$matches", "func(path:Path, pattern:Pattern -> [Text]?)"},
            {"replace", "Text$replace", "func(path:Path, pattern:Pattern, replacement:Text, backref=$/\\/, recursive=yes -> Path)"},
            {"replace_all", "Text$replace_all", "func(path:Path, replacements:{Pattern,Text}, backref=$/\\/, recursive=yes -> Path)"},
            {"starts_with", "Text$starts_with", "func(path:Path, prefix:Text -> Bool)"},
        )},
        // RNG must come after Path so we can read bytes from /dev/urandom
        {"RNG", RNG_TYPE, "RNG_t", "RNG", TypedArray(ns_entry_t,
            {"bool", "RNG$bool", "func(rng:RNG, p=0.5 -> Bool)"},
            {"byte", "RNG$byte", "func(rng:RNG -> Byte)"},
            {"bytes", "RNG$bytes", "func(rng:RNG, count:Int -> [Byte])"},
            {"copy", "RNG$copy", "func(rng:RNG -> RNG)"},
            {"int", "RNG$int", "func(rng:RNG, min,max:Int -> Int)"},
            {"int16", "RNG$int16", "func(rng:RNG, min=Int16.min, max=Int16.max -> Int16)"},
            {"int32", "RNG$int32", "func(rng:RNG, min=Int32.min, max=Int32.max -> Int32)"},
            {"int64", "RNG$int64", "func(rng:RNG, min=Int64.min, max=Int64.max -> Int64)"},
            {"int8", "RNG$int8", "func(rng:RNG, min=Int8.min, max=Int8.max -> Int8)"},
            {"new", "RNG$new", "func(seed=(/dev/urandom):read_bytes(40)! -> RNG)"},
            {"num", "RNG$num", "func(rng:RNG, min=0.0, max=1.0 -> Num)"},
            {"num32", "RNG$num32", "func(rng:RNG, min=Num32(0.0), max=Num32(1.0) -> Num32)"},
            {"set_seed", "RNG$set_seed", "func(rng:RNG, seed:[Byte])"},
        )},
        {"Shell", Type(TextType, .lang="Shell", .env=namespace_env(env, "Shell")), "Shell_t", "Shell$info", TypedArray(ns_entry_t,
            {"by_line", "Shell$by_line", "func(command:Shell -> func(->Text?)?)"},
            {"escape_int", "Int$value_as_text", "func(i:Int -> Shell)"},
            {"escape_text", "Shell$escape_text", "func(text:Text -> Shell)"},
            {"escape_text_array", "Shell$escape_text_array", "func(texts:[Text] -> Shell)"},
            {"execute", "Shell$execute", "func(command:Shell -> Int32)"},
            {"run_bytes", "Shell$run", "func(command:Shell -> [Byte]?)"},
            {"run", "Shell$run", "func(command:Shell -> Text?)"},
        )},
        {"Text", TEXT_TYPE, "Text_t", "Text$info", TypedArray(ns_entry_t,
            {"as_c_string", "Text$as_c_string", "func(text:Text -> CString)"},
            {"at", "Text$cluster", "func(text:Text, index:Int -> Text)"},
            {"by_line", "Text$by_line", "func(text:Text -> func(->Text?))"},
            {"by_match", "Text$by_match", "func(text:Text, pattern:Pattern -> func(->Match?))"},
            {"by_split", "Text$by_split", "func(text:Text, pattern=$Pattern'' -> func(->Text?))"},
            {"bytes", "Text$utf8_bytes", "func(text:Text -> [Byte])"},
            {"codepoint_names", "Text$codepoint_names", "func(text:Text -> [Text])"},
            {"ends_with", "Text$ends_with", "func(text,suffix:Text -> Bool)"},
            {"each", "Text$each", "func(text:Text, pattern:Pattern, fn:func(match:Match))"},
            {"find", "Text$find", "func(text:Text, pattern:Pattern, start=1 -> Match?)"},
            {"find_all", "Text$find_all", "func(text:Text, pattern:Pattern -> [Match])"},
            {"from", "Text$from", "func(text:Text, first:Int -> Text)"},
            {"from_bytes", "Text$from_bytes", "func(bytes:[Byte] -> Text?)"},
            {"from_c_string", "Text$from_str", "func(str:CString -> Text?)"},
            {"from_codepoint_names", "Text$from_codepoint_names", "func(codepoint_names:[Text] -> Text?)"},
            {"from_codepoints", "Text$from_codepoints", "func(codepoints:[Int32] -> Text)"},
            {"without_escaping", "Path$cleanup", "func(text:Text -> Path)"},
            {"has", "Text$has", "func(text:Text, pattern:Pattern -> Bool)"},
            {"join", "Text$join", "func(glue:Text, pieces:[Text] -> Text)"},
            {"lines", "Text$lines", "func(text:Text -> [Text])"},
            {"lower", "Text$lower", "func(text:Text -> Text)"},
            {"map", "Text$map", "func(text:Text, pattern:Pattern, fn:func(match:Match -> Text) -> Text)"},
            {"matches", "Text$matches", "func(text:Text, pattern:Pattern -> [Text]?)"},
            {"quoted", "Text$quoted", "func(text:Text, color=no -> Text)"},
            {"repeat", "Text$repeat", "func(text:Text, count:Int -> Text)"},
            {"replace", "Text$replace", "func(text:Text, pattern:Pattern, replacement:Text, backref=$/\\/, recursive=yes -> Text)"},
            {"replace_all", "Text$replace_all", "func(text:Text, replacements:{Pattern,Text}, backref=$/\\/, recursive=yes -> Text)"},
            {"reversed", "Text$reversed", "func(text:Text -> Text)"},
            {"slice", "Text$slice", "func(text:Text, from=1, to=-1 -> Text)"},
            {"split", "Text$split", "func(text:Text, pattern=$Pattern'' -> [Text])"},
            {"starts_with", "Text$starts_with", "func(text,prefix:Text -> Bool)"},
            {"title", "Text$title", "func(text:Text -> Text)"},
            {"to", "Text$to", "func(text:Text, last:Int -> Text)"},
            {"trim", "Text$trim", "func(text:Text, pattern=$/{whitespace}/, trim_left=yes, trim_right=yes -> Text)"},
            {"upper", "Text$upper", "func(text:Text -> Text)"},
            {"utf32_codepoints", "Text$utf32_codepoints", "func(text:Text -> [Int32])"},
        )},
        {"Thread", THREAD_TYPE, "Thread_t", "Thread", TypedArray(ns_entry_t,
            {"new", "Thread$new", "func(fn:func() -> Thread)"},
            {"cancel", "Thread$cancel", "func(thread:Thread)"},
            {"join", "Thread$join", "func(thread:Thread)"},
            {"detach", "Thread$detach", "func(thread:Thread)"},
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
            if (!type) compiler_err(NULL, NULL, NULL, "Couldn't parse type string: %s", entry->type_str);
            if (type->tag == ClosureType) type = Match(type, ClosureType)->fn;
            set_binding(ns_env, entry->name, type, entry->code);
        }
    }

    // Conversion constructors:
#define ADD_CONSTRUCTOR(ns_env, identifier, typestr) Array$insert(&ns_env->namespace->constructors, ((binding_t[1]){{.code=identifier, .type=Match(parse_type_string(ns_env, typestr), ClosureType)->fn}}), I(0), sizeof(binding_t))
    {
        env_t *ns_env = namespace_env(env, "Pattern");
        ADD_CONSTRUCTOR(ns_env, "Pattern$escape_text", "func(text:Text -> Pattern)");
        ADD_CONSTRUCTOR(ns_env, "Int$value_as_text", "func(i:Int -> Pattern)");
    }
    {
        env_t *ns_env = namespace_env(env, "Path");
        ADD_CONSTRUCTOR(ns_env, "Path$escape_text", "func(text:Text -> Path)");
        ADD_CONSTRUCTOR(ns_env, "Path$escape_path", "func(path:Path -> Path)");
        ADD_CONSTRUCTOR(ns_env, "Int$value_as_text", "func(i:Int -> Path)");
    }
    {
        env_t *ns_env = namespace_env(env, "Shell");
        ADD_CONSTRUCTOR(ns_env, "Shell$escape_text", "func(text:Text -> Shell)");
        ADD_CONSTRUCTOR(ns_env, "Shell$escape_text", "func(path:Path -> Shell)");
        ADD_CONSTRUCTOR(ns_env, "Shell$escape_text_array", "func(texts:[Text] -> Shell)");
        ADD_CONSTRUCTOR(ns_env, "Shell$escape_text_array", "func(paths:[Path] -> Shell)");
        ADD_CONSTRUCTOR(ns_env, "Int$value_as_text", "func(i:Int -> Shell)");
    }
#undef ADD_CONSTRUCTOR

    set_binding(namespace_env(env, "Shell"), "without_escaping",
                Type(FunctionType, .args=new(arg_t, .name="text", .type=TEXT_TYPE),
                     .ret=Type(TextType, .lang="Shell", .env=namespace_env(env, "Shell"))),
                "(Shell_t)");

    set_binding(namespace_env(env, "Path"), "without_escaping",
                Type(FunctionType, .args=new(arg_t, .name="text", .type=TEXT_TYPE),
                     .ret=Type(TextType, .lang="Path", .env=namespace_env(env, "Path"))),
                "Path$cleanup");


    set_binding(namespace_env(env, "Pattern"), "without_escaping",
                Type(FunctionType, .args=new(arg_t, .name="text", .type=TEXT_TYPE),
                     .ret=Type(TextType, .lang="Pattern", .env=namespace_env(env, "Pattern"))),
                "(Pattern_t)");

    Table$str_set(env->globals, "random", new(binding_t, .type=RNG_TYPE, .code="default_rng"));

    env_t *lib_env = fresh_scope(env);
    lib_env->libname = libname;
    return lib_env;
}

CORD namespace_prefix(env_t *env, namespace_t *ns)
{
    CORD prefix = CORD_EMPTY;
    for (; ns; ns = ns->parent)
        prefix = CORD_all(ns->name, "$", prefix);
    if (env->libname)
        prefix = CORD_all("_$", env->libname, "$", prefix);
    else
        prefix = CORD_all("_$", prefix);
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

env_t *for_scope(env_t *env, ast_t *ast)
{
    auto for_ = Match(ast, For);
    type_t *iter_t = value_type(get_type(env, for_->iter));
    env_t *scope = fresh_scope(env);

    switch (iter_t->tag) {
    case ArrayType: {
        type_t *item_t = Match(iter_t, ArrayType)->item_type;
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
        auto fn = iter_t->tag == ClosureType ? Match(Match(iter_t, ClosureType)->fn, FunctionType) : Match(iter_t, FunctionType);
        // if (fn->ret->tag != OptionalType)
        //     code_err(for_->iter, "Iterator functions must return an optional type, not %T", fn->ret);

        if (for_->vars) {
            if (for_->vars->next)
                code_err(for_->vars->next->ast, "This is too many variables for this loop");
            const char *var = Match(for_->vars->ast, Var)->name;
            type_t *non_opt_type = fn->ret->tag == OptionalType ? Match(fn->ret, OptionalType)->type : fn->ret;
            set_binding(scope, var, non_opt_type, CORD_cat("_$", var));
        }
        return scope;
    }
    default: code_err(for_->iter, "Iteration is not implemented for type: %T", iter_t);
    }
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
    type_t *cls_type = value_type(self_type);
    switch (cls_type->tag) {
    case ArrayType: return NULL;
    case TableType: return NULL;
    case CStringType: case MomentType:
    case BoolType: case IntType: case BigIntType: case NumType: case ByteType: {
        binding_t *b = get_binding(env, CORD_to_const_char_star(type_to_cord(cls_type)));
        assert(b);
        return get_binding(Match(b->type, TypeInfoType)->env, name);
    }
    case TextType: {
        auto text = Match(cls_type, TextType);
        env_t *text_env = text->env ? text->env : namespace_env(env, text->lang ? text->lang : "Text");
        assert(text_env);
        return get_binding(text_env, name);
    }
    case StructType: {
        auto struct_ = Match(cls_type, StructType);
        return struct_->env ? get_binding(struct_->env, name) : NULL;
    }
    case EnumType: {
        auto enum_ = Match(cls_type, EnumType);
        return enum_->env ? get_binding(enum_->env, name) : NULL;
    }
    case TypeInfoType: {
        auto info = Match(cls_type, TypeInfoType);
        return info->env ? get_binding(info->env, name) : NULL;
    }
    default: break;
    }
    return NULL;
}

PUREFUNC binding_t *get_constructor(env_t *env, type_t *t, arg_ast_t *args)
{
    const char *type_name;
    t = value_type(t);
    switch (t->tag) {
    case TextType: {
        type_name = Match(t, TextType)->lang;
        if (type_name == NULL) type_name = "Text";
        break;
    }
    case StructType: {
        type_name = Match(t, StructType)->name;
        break;
    }
    case EnumType: {
        type_name = Match(t, EnumType)->name;
        break;
    }
    default: {
        type_name = NULL;
        break;
    }
    }

    if (!type_name)
        return NULL;

    binding_t *typeinfo = get_binding(env, type_name);
    assert(typeinfo && typeinfo->type->tag == TypeInfoType);
    env_t *type_env = Match(typeinfo->type, TypeInfoType)->env;
    Array_t constructors = type_env->namespace->constructors;
    // Prioritize exact matches:
    for (int64_t i = 0; i < constructors.length; i++) {
        binding_t *b = constructors.data + i*constructors.stride;
        auto fn = Match(b->type, FunctionType);
        if (is_valid_call(env, fn->args, args, false))
            return b;
    }
    // Fall back to promotion:
    for (int64_t i = 0; i < constructors.length; i++) {
        binding_t *b = constructors.data + i*constructors.stride;
        auto fn = Match(b->type, FunctionType);
        if (is_valid_call(env, fn->args, args, true))
            return b;
    }
    return NULL;
}

void set_binding(env_t *env, const char *name, type_t *type, CORD code)
{
    assert(name);
    Table$str_set(env->locals, name, new(binding_t, .type=type, .code=code));
}

__attribute__((format(printf, 4, 5)))
_Noreturn void compiler_err(file_t *f, const char *start, const char *end, const char *fmt, ...)
{
    if (isatty(STDERR_FILENO) && !getenv("NO_COLOR"))
        fputs("\x1b[31;7;1m", stderr);
    if (f && start && end)
        fprintf(stderr, "%s:%ld.%ld: ", f->relative_filename, get_line_number(f, start),
                get_line_column(f, start));
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    if (isatty(STDERR_FILENO) && !getenv("NO_COLOR"))
        fputs(" \x1b[m", stderr);
    fputs("\n\n", stderr);
    if (f && start && end)
        highlight_error(f, start, end, "\x1b[31;1m", 2, isatty(STDERR_FILENO) && !getenv("NO_COLOR"));

    if (getenv("TOMO_STACKTRACE"))
        print_stack_trace(stderr, 1, 3);

    raise(SIGABRT);
    exit(1);
}

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
