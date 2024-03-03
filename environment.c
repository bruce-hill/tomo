
#include <stdlib.h>
#include <signal.h>

#include "environment.h"
#include "builtins/table.h"
#include "builtins/string.h"
#include "typecheck.h"
#include "util.h"

typedef struct {
    const char *name;
    binding_t binding;
} ns_entry_t;

// static type_t *namespace_type(const char *name, table_t *ns)
// {
//     arg_t *fields = NULL;
//     for (int64_t i = Table_length(ns); i >= 1; i--) {
//         struct {const char *name; binding_t *binding; } *entry = Table_entry(ns, i);
//         fields = new(arg_t, .next=fields, .name=entry->name, .type=entry->binding->type);
//     }
//     name = heap_strf("%s_namespace", name);
//     return Type(StructType, .name=name, .fields=fields);
// }

env_t *new_compilation_unit(void)
{
    env_t *env = new(env_t);
    env->code = new(compilation_unit_t);
    env->types = new(table_t);
    env->type_namespaces = new(table_t);
    env->globals = new(table_t);
    env->locals = new(table_t, .fallback=env->globals);

    struct {
        const char *name;
        binding_t binding;
    } global_vars[] = {
        {"say", {.code="say", .type=Type(FunctionType, .args=new(arg_t, .name="text", .type=Type(StringType)), .ret=Type(VoidType))}},
        {"fail", {.code="fail", .type=Type(FunctionType, .args=new(arg_t, .name="message", .type=Type(StringType)), .ret=Type(AbortType))}},
        {"USE_COLOR", {.code="USE_COLOR", .type=Type(BoolType)}},
    };

    for (size_t i = 0; i < sizeof(global_vars)/sizeof(global_vars[0]); i++) {
        binding_t *b = new(binding_t);
        *b = global_vars[i].binding;
        Table_str_set(env->globals, global_vars[i].name, b);
    }

    typedef struct {
        const char *name, *code, *type_str;
    } ns_entry_t;

    struct {
        const char *name;
        type_t *type;
        CORD typename;
        CORD struct_val;
        array_t namespace;
    } global_types[] = {
        {"Bool", Type(BoolType), "Bool_t", "Bool", {}},
        {"Int", Type(IntType, .bits=64), "Int_t", "Int", $TypedArray(ns_entry_t,
            {"format", "Int__format", "func(i:Int, digits=0)->Str"},
            {"hex", "Int__hex", "func(i:Int, digits=0, uppercase=yes, prefix=yes)->Str"},
            {"octal", "Int__octal", "func(i:Int, digits=0, prefix=yes)->Str"},
            {"random", "Int__random", "func(min=0, max=0xffffffff)->Int"},
            {"bits", "Int__bits", "func(x:Int)->[Bool]"},
            {"abs", "Int__abs", "func(i:Int)->Int"},
            {"min", "Int__min", "Int"},
            {"max", "Int__max", "Int"},
        )},
        {"Int32", Type(IntType, .bits=32), "Int32_t", "Int32", $TypedArray(ns_entry_t,
            {"format", "Int32__format", "func(i:Int32, digits=0)->Str"},
            {"hex", "Int32__hex", "func(i:Int32, digits=0, uppercase=yes, prefix=yes)->Str"},
            {"octal", "Int32__octal", "func(i:Int32, digits=0, prefix=yes)->Str"},
            {"random", "Int32__random", "func(min=0, max=0xffffffff)->Int32"},
            {"bits", "Int32__bits", "func(x:Int32)->[Bool]"},
            {"abs", "Int32__abs", "func(i:Int32)->Int32"},
            {"min", "Int32__min", "Int32"},
            {"max", "Int32__max", "Int32"},
        )},
        {"Int16", Type(IntType, .bits=16), "Int16_t", "Int16", $TypedArray(ns_entry_t,
            {"format", "Int16__format", "func(i:Int16, digits=0)->Str"},
            {"hex", "Int16__hex", "func(i:Int16, digits=0, uppercase=yes, prefix=yes)->Str"},
            {"octal", "Int16__octal", "func(i:Int16, digits=0, prefix=yes)->Str"},
            {"random", "Int16__random", "func(min=0, max=0xffffffff)->Int16"},
            {"bits", "Int16__bits", "func(x:Int16)->[Bool]"},
            {"abs", "Int16__abs", "func(i:Int16)->Int16"},
            {"min", "Int16__min", "Int16"},
            {"max", "Int16__max", "Int16"},
        )},
        {"Int8", Type(IntType, .bits=8), "Int8_t", "Int8", $TypedArray(ns_entry_t,
            {"format", "Int8__format", "func(i:Int8, digits=0)->Str"},
            {"hex", "Int8__hex", "func(i:Int8, digits=0, uppercase=yes, prefix=yes)->Str"},
            {"octal", "Int8__octal", "func(i:Int8, digits=0, prefix=yes)->Str"},
            {"random", "Int8__random", "func(min=0, max=0xffffffff)->Int8"},
            {"bits", "Int8__bits", "func(x:Int8)->[Bool]"},
            {"abs", "Int8__abs", "func(i:Int8)->Int8"},
            {"min", "Int8__min", "Int8"},
            {"max", "Int8__max", "Int8"},
        )},
#define C(name) {#name, "Num__"#name, "Num"}
#define F(name) {#name, "Num__"#name, "func(n:Num)->Num"}
#define F2(name) {#name, "Num__"#name, "func(x:Num, y:Num)->Num"}
        {"Num", Type(NumType, .bits=64), "Num_t", "Num", $TypedArray(ns_entry_t,
            {"near", "Num__near", "func(x:Num, y:Num, ratio=1e-9, min_epsilon=1e-9)->Bool"},
            {"format", "Num__format", "func(n:Num, precision=0)->Str"},
            {"scientific", "Num__scientific", "func(n:Num, precision=0)->Str"},
            {"nan", "Num__nan", "func(tag=\"\")->Num"},
            {"isinf", "Num__isinf", "func(n:Num)->Bool"},
            {"isfinite", "Num__isfinite", "func(n:Num)->Bool"},
            {"isnan", "Num__isnan", "func(n:Num)->Bool"},
            C(2_SQRTPI), C(E), C(PI_2), C(2_PI), C(1_PI), C(LN10), C(LN2), C(LOG2E),
            C(PI), C(PI_4), C(SQRT2), C(SQRT1_2), C(INF), C(TAU),
            {"random", "Num__random", "func()->Num"},
            F(abs), F(acos), F(acosh), F(asin), F(asinh), F(atan), F(atanh), F(cbrt), F(ceil), F(cos), F(cosh), F(erf), F(erfc),
            F(exp), F(exp2), F(expm1), F(floor), F(j0), F(j1), F(log), F(log10), F(log1p), F(log2), F(logb),
            F(rint), F(round), F(significand), F(sin), F(sinh), F(sqrt),
            F(tan), F(tanh), F(tgamma), F(trunc), F(y0), F(y1),
            F2(atan2), F2(copysign), F2(fdim), F2(hypot), F2(nextafter), F2(pow), F2(remainder),
        )},
#undef F2
#undef F
#undef C
#define C(name) {#name, "Num32__"#name, "Num32"}
#define F(name) {#name, "Num32__"#name, "func(n:Num32)->Num32"}
#define F2(name) {#name, "Num32__"#name, "func(x:Num32, y:Num32)->Num32"}
        {"Num32", Type(NumType, .bits=32), "Num32_t", "Num32", $TypedArray(ns_entry_t,
            {"near", "Num32__near", "func(x:Num32, y:Num32, ratio=1e-9f32, min_epsilon=1e-9f32)->Bool"},
            {"format", "Num32__format", "func(n:Num32, precision=0)->Str"},
            {"scientific", "Num32__scientific", "func(n:Num32, precision=0)->Str"},
            {"nan", "Num32__nan", "func(tag=\"\")->Num32"},
            {"isinf", "Num32__isinf", "func(n:Num32)->Bool"},
            {"isfinite", "Num32__isfinite", "func(n:Num32)->Bool"},
            {"isnan", "Num32__isnan", "func(n:Num32)->Bool"},
            C(2_SQRTPI), C(E), C(PI_2), C(2_PI), C(1_PI), C(LN10), C(LN2), C(LOG2E),
            C(PI), C(PI_4), C(SQRT2), C(SQRT1_2), C(INF), C(TAU),
            {"random", "Num32__random", "func()->Num32"},
            F(abs), F(acos), F(acosh), F(asin), F(asinh), F(atan), F(atanh), F(cbrt), F(ceil), F(cos), F(cosh), F(erf), F(erfc),
            F(exp), F(exp2), F(expm1), F(floor), F(j0), F(j1), F(log), F(log10), F(log1p), F(log2), F(logb),
            F(rint), F(round), F(significand), F(sin), F(sinh), F(sqrt),
            F(tan), F(tanh), F(tgamma), F(trunc), F(y0), F(y1),
            F2(atan2), F2(copysign), F2(fdim), F2(hypot), F2(nextafter), F2(pow), F2(remainder),
        )},
#undef F2
#undef F
#undef C
        {"Str", Type(StringType), "Str_t", "Str", $TypedArray(ns_entry_t,
            {"quoted", "Str__quoted", "func(s:Str, color=no)->Str"},
            {"upper", "Str__upper", "func(s:Str)->Str"},
            {"lower", "Str__lower", "func(s:Str)->Str"},
            {"title", "Str__title", "func(s:Str)->Str"},
            // {"has", "Str__has", "func(s:Str, target:Str, where=ANYWHERE)->Bool"},
            // {"without", "Str__without", "func(s:Str, target:Str, where=ANYWHERE)->Str"},
            // {"trimmed", "Str__without", "func(s:Str, skip:Str, where=ANYWHERE)->Str"},
            {"title", "Str__title", "func(s:Str)->Str"},
            // {"find", "Str__find", "func(s:Str, pattern:Str)->FindResult"},
            {"replace", "Str__replace", "func(s:Str, pattern:Str, replacement:Str, limit=Int.max)->Str"},
            {"split", "Str__split", "func(s:Str, split:Str)->[Str]"},
            {"join", "Str__join", "func(glue:Str, pieces:[Str])->Str"},
        )},
    };

    for (size_t i = 0; i < sizeof(global_types)/sizeof(global_types[0]); i++) {
        binding_t *binding = new(binding_t, .type=Type(TypeInfoType, .name=global_types[i].name, .type=global_types[i].type));
        Table_str_set(env->globals, global_types[i].name, binding);
        Table_str_set(env->types, global_types[i].name, global_types[i].type);
    }

    for (size_t i = 0; i < sizeof(global_types)/sizeof(global_types[0]); i++) {
        table_t *namespace = new(table_t);
        $ARRAY_FOREACH(global_types[i].namespace, j, ns_entry_t, entry, {
            type_t *type = parse_type_string(env, entry.type_str);
            binding_t *b = new(binding_t, .code=entry.code, .type=type);
            Table_str_set(namespace, entry.name, b);
            // printf("Bound %s:%s -> %T\n", global_types[i].name, entry.name, b->type);
        }, {})
        Table_str_set(env->type_namespaces, global_types[i].name, namespace);
    }

    return env;
}

env_t *fresh_scope(env_t *env)
{
    env_t *scope = new(env_t);
    *scope = *env;
    scope->locals = new(table_t, .fallback=env->locals);
    return scope;
}

binding_t *get_binding(env_t *env, const char *name)
{
    return Table_str_get(env->locals, name);
}

binding_t *get_namespace_binding(env_t *env, ast_t *self, const char *name)
{
    type_t *self_type = get_type(env, self);
    if (!self_type)
        code_err(self, "I couldn't get this type");
    type_t *cls_type = value_type(self_type);
    switch (cls_type->tag) {
    case ArrayType: {
        errx(1, "Array methods not implemented");
    }
    case TableType: {
        errx(1, "Table methods not implemented");
    }
    case BoolType: case IntType: case NumType: case StringType: {
        table_t *ns = Table_str_get(env->type_namespaces, CORD_to_const_char_star(type_to_cord(cls_type)));
        if (!ns) {
            code_err(self, "No namespace found for this type!");
        }
        return Table_str_get(ns, name);
    }
    case TypeInfoType: case StructType: case EnumType: {
        const char *name;
        switch (cls_type->tag) {
        case TypeInfoType: name = Match(cls_type, TypeInfoType)->name; break;
        case StructType: name = Match(cls_type, StructType)->name; break;
        case EnumType: name = Match(cls_type, EnumType)->name; break;
        default: errx(1, "Unreachable");
        }

        table_t *namespace = Table_str_get(env->type_namespaces, name);
        if (!namespace) return NULL;
        return Table_str_get(namespace, name);
    }
    default: break;
    }
    code_err(self, "No such method!");
    return NULL;
}

void set_binding(env_t *env, const char *name, binding_t *binding)
{
    Table_str_set(env->locals, name, binding);
}

void compiler_err(file_t *f, const char *start, const char *end, const char *fmt, ...)
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
        fprint_span(stderr, f, start, end, "\x1b[31;1m", 2, isatty(STDERR_FILENO) && !getenv("NO_COLOR"));

    raise(SIGABRT);
    exit(1);
}

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
