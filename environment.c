// Logic for the environmental context information during compilation
// (variable bindings, code sections, etc.)
#include <stdlib.h>
#include <signal.h>

#include "environment.h"
#include "builtins/table.h"
#include "builtins/text.h"
#include "typecheck.h"
#include "builtins/util.h"

type_t *TEXT_TYPE = NULL;

env_t *new_compilation_unit(void)
{
    env_t *env = new(env_t);
    env->code = new(compilation_unit_t);
    env->types = new(table_t);
    env->globals = new(table_t);
    env->locals = new(table_t, .fallback=env->globals);
    env->imports = new(table_t);

    if (!TEXT_TYPE)
        TEXT_TYPE = Type(TextType, .env=namespace_env(env, "Text"));

    struct {
        const char *name;
        binding_t binding;
    } global_vars[] = {
        {"say", {.code="say", .type=Type(FunctionType, .args=new(arg_t, .name="text", .type=TEXT_TYPE), .ret=Type(VoidType))}},
        {"fail", {.code="fail", .type=Type(FunctionType, .args=new(arg_t, .name="message", .type=TEXT_TYPE), .ret=Type(AbortType))}},
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

    struct {
        const char *name;
        type_t *type;
        CORD typename;
        CORD struct_val;
        array_t namespace;
    } global_types[] = {
        {"Void", Type(VoidType), "Void_t", "$Void", {}},
        {"Memory", Type(MemoryType), "Memory_t", "$Memory", {}},
        {"Bool", Type(BoolType), "Bool_t", "$Bool", TypedArray(ns_entry_t,
            {"from_text", "Bool$from_text", "func(text:Text, success=!Bool)->Bool"},
        )},
        {"Int", Type(IntType, .bits=64), "Int_t", "$Int", TypedArray(ns_entry_t,
            {"format", "Int$format", "func(i:Int, digits=0)->Text"},
            {"hex", "Int$hex", "func(i:Int, digits=0, uppercase=yes, prefix=yes)->Text"},
            {"octal", "Int$octal", "func(i:Int, digits=0, prefix=yes)->Text"},
            {"random", "Int$random", "func(min=0, max=0xffffffff)->Int"},
            {"from_text", "Int$from_text", "func(text:Text, the_rest=!Text)->Int"},
            {"bits", "Int$bits", "func(x:Int)->[Bool]"},
            {"abs", "labs", "func(i:Int)->Int"},
            {"min", "Int$min", "Int"},
            {"max", "Int$max", "Int"},
        )},
        {"Int32", Type(IntType, .bits=32), "Int32_t", "$Int32", TypedArray(ns_entry_t,
            {"format", "Int32$format", "func(i:Int32, digits=0)->Text"},
            {"hex", "Int32$hex", "func(i:Int32, digits=0, uppercase=yes, prefix=yes)->Text"},
            {"octal", "Int32$octal", "func(i:Int32, digits=0, prefix=yes)->Text"},
            {"random", "Int32$random", "func(min=0, max=0xffffffff)->Int32"},
            {"from_text", "Int$from_text", "func(text:Text, the_rest=!Text)->Int32"},
            {"bits", "Int32$bits", "func(x:Int32)->[Bool]"},
            {"abs", "abs", "func(i:Int32)->Int32"},
            {"min", "Int32$min", "Int32"},
            {"max", "Int32$max", "Int32"},
        )},
        {"Int16", Type(IntType, .bits=16), "Int16_t", "$Int16", TypedArray(ns_entry_t,
            {"format", "Int16$format", "func(i:Int16, digits=0)->Text"},
            {"hex", "Int16$hex", "func(i:Int16, digits=0, uppercase=yes, prefix=yes)->Text"},
            {"octal", "Int16$octal", "func(i:Int16, digits=0, prefix=yes)->Text"},
            {"random", "Int16$random", "func(min=0, max=0xffffffff)->Int16"},
            {"from_text", "Int$from_text", "func(text:Text, the_rest=!Text)->Int16"},
            {"bits", "Int16$bits", "func(x:Int16)->[Bool]"},
            {"abs", "abs", "func(i:Int16)->Int16"},
            {"min", "Int16$min", "Int16"},
            {"max", "Int16$max", "Int16"},
        )},
        {"Int8", Type(IntType, .bits=8), "Int8_t", "$Int8", TypedArray(ns_entry_t,
            {"format", "Int8$format", "func(i:Int8, digits=0)->Text"},
            {"hex", "Int8$hex", "func(i:Int8, digits=0, uppercase=yes, prefix=yes)->Text"},
            {"octal", "Int8$octal", "func(i:Int8, digits=0, prefix=yes)->Text"},
            {"random", "Int8$random", "func(min=0, max=0xffffffff)->Int8"},
            {"from_text", "Int$from_text", "func(text:Text, the_rest=!Text)->Int8"},
            {"bits", "Int8$bits", "func(x:Int8)->[Bool]"},
            {"abs", "abs", "func(i:Int8)->Int8"},
            {"min", "Int8$min", "Int8"},
            {"max", "Int8$max", "Int8"},
        )},
#define C(name) {#name, "M_"#name, "Num"}
#define F(name) {#name, #name, "func(n:Num)->Num"}
#define F2(name) {#name, #name, "func(x:Num, y:Num)->Num"}
        {"Num", Type(NumType, .bits=64), "Num_t", "$Num", TypedArray(ns_entry_t,
            {"near", "Num$near", "func(x:Num, y:Num, ratio=1e-9, min_epsilon=1e-9)->Bool"},
            {"format", "Num$format", "func(n:Num, precision=0)->Text"},
            {"scientific", "Num$scientific", "func(n:Num, precision=0)->Text"},
            {"nan", "Num$nan", "func(tag=\"\")->Num"},
            {"isinf", "Num$isinf", "func(n:Num)->Bool"},
            {"isfinite", "Num$isfinite", "func(n:Num)->Bool"},
            {"isnan", "Num$isnan", "func(n:Num)->Bool"},
            C(2_SQRTPI), C(E), C(PI_2), C(2_PI), C(1_PI), C(LN10), C(LN2), C(LOG2E),
            C(PI), C(PI_4), C(SQRT2), C(SQRT1_2),
            {"INF", "INFINITY", "Num"},
            {"TAU", "(2.*M_PI)", "Num"},
            {"random", "Num$random", "func()->Num"},
            {"from_text", "Num$from_text", "func(text:Text, the_rest=!Text)->Num"},
            {"abs", "fabs", "func(n:Num)->Num"},
            F(acos), F(acosh), F(asin), F(asinh), F(atan), F(atanh), F(cbrt), F(ceil), F(cos), F(cosh), F(erf), F(erfc),
            F(exp), F(exp2), F(expm1), F(floor), F(j0), F(j1), F(log), F(log10), F(log1p), F(log2), F(logb),
            F(rint), F(round), F(significand), F(sin), F(sinh), F(sqrt),
            F(tan), F(tanh), F(tgamma), F(trunc), F(y0), F(y1),
            F2(atan2), F2(copysign), F2(fdim), F2(hypot), F2(nextafter), F2(pow), F2(remainder),
        )},
#undef F2
#undef F
#undef C
#define C(name) {#name, "(Num32_t)(M_"#name")", "Num32"}
#define F(name) {#name, #name"f", "func(n:Num32)->Num32"}
#define F2(name) {#name, #name"f", "func(x:Num32, y:Num32)->Num32"}
        {"Num32", Type(NumType, .bits=32), "Num32_t", "$Num32", TypedArray(ns_entry_t,
            {"near", "Num32$near", "func(x:Num32, y:Num32, ratio=1e-9f32, min_epsilon=1e-9f32)->Bool"},
            {"format", "Num32$format", "func(n:Num32, precision=0)->Text"},
            {"scientific", "Num32$scientific", "func(n:Num32, precision=0)->Text"},
            {"nan", "Num32$nan", "func(tag=\"\")->Num32"},
            {"isinf", "Num32$isinf", "func(n:Num32)->Bool"},
            {"isfinite", "Num32$isfinite", "func(n:Num32)->Bool"},
            {"isnan", "Num32$isnan", "func(n:Num32)->Bool"},
            C(2_SQRTPI), C(E), C(PI_2), C(2_PI), C(1_PI), C(LN10), C(LN2), C(LOG2E),
            C(PI), C(PI_4), C(SQRT2), C(SQRT1_2),
            {"INF", "(Num32_t)(INFINITY)", "Num32"},
            {"TAU", "(Num32_t)(2.f*M_PI)", "Num32"},
            {"random", "Num32$random", "func()->Num32"},
            {"from_text", "Num32$from_text", "func(text:Text, the_rest=!Text)->Num32"},
            {"abs", "fabsf", "func(n:Num32)->Num32"},
            F(acos), F(acosh), F(asin), F(asinh), F(atan), F(atanh), F(cbrt), F(ceil), F(cos), F(cosh), F(erf), F(erfc),
            F(exp), F(exp2), F(expm1), F(floor), F(j0), F(j1), F(log), F(log10), F(log1p), F(log2), F(logb),
            F(rint), F(round), F(significand), F(sin), F(sinh), F(sqrt),
            F(tan), F(tanh), F(tgamma), F(trunc), F(y0), F(y1),
            F2(atan2), F2(copysign), F2(fdim), F2(hypot), F2(nextafter), F2(pow), F2(remainder),
        )},
#undef F2
#undef F
#undef C
        {"Text", TEXT_TYPE, "Text_t", "$Text", TypedArray(ns_entry_t,
            {"quoted", "Text$quoted", "func(text:Text, color=no)->Text"},
            {"upper", "Text$upper", "func(text:Text)->Text"},
            {"lower", "Text$lower", "func(text:Text)->Text"},
            {"title", "Text$title", "func(text:Text)->Text"},
            // {"has", "Text$has", "func(text:Text, target:Text, where=ANYWHERE)->Bool"},
            // {"without", "Text$without", "func(text:Text, target:Text, where=ANYWHERE)->Text"},
            // {"trimmed", "Text$without", "func(text:Text, skip:Text, where=ANYWHERE)->Text"},
            {"title", "Text$title", "func(text:Text)->Text"},
            // {"find", "Text$find", "func(text:Text, pattern:Text)->FindResult"},
            {"replace", "Text$replace", "func(text:Text, pattern:Text, replacement:Text, limit=Int.max)->Text"},
            {"split", "Text$split", "func(text:Text, split:Text)->[Text]"},
            {"join", "Text$join", "func(glue:Text, pieces:[Text])->Text"},
            {"clusters", "Text$clusters", "func(text:Text)->[Text]"},
            {"codepoints", "Text$codepoints", "func(text:Text)->[Int32]"},
            {"bytes", "Text$bytes", "func(text:Text)->[Int8]"},
            {"num_clusters", "Text$num_clusters", "func(text:Text)->Int"},
            {"num_codepoints", "Text$num_codepoints", "func(text:Text)->Int"},
            {"num_bytes", "Text$num_bytes", "func(text:Text)->Int"},
            {"character_names", "Text$character_names", "func(text:Text)->[Text]"},
        )},
    };

    for (size_t i = 0; i < sizeof(global_types)/sizeof(global_types[0]); i++) {
        env_t *ns_env = global_types[i].type == TEXT_TYPE ? Match(TEXT_TYPE, TextType)->env : namespace_env(env, global_types[i].name);
        binding_t *binding = new(binding_t, .type=Type(TypeInfoType, .name=global_types[i].name, .type=global_types[i].type, .env=ns_env));
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
            binding_t *b = new(binding_t, .code=entry->code, .type=type);
            set_binding(ns_env, entry->name, b);
        }
    }

    return env;
}

env_t *global_scope(env_t *env)
{
    env_t *scope = new(env_t);
    *scope = *env;
    scope->locals = new(table_t, .fallback=env->globals);
    return scope;
}

env_t *fresh_scope(env_t *env)
{
    env_t *scope = new(env_t);
    *scope = *env;
    scope->locals = new(table_t, .fallback=env->locals);
    return scope;
}

env_t *for_scope(env_t *env, ast_t *ast)
{
    auto for_ = Match(ast, For);
    type_t *iter_t = get_type(env, for_->iter);
    env_t *scope = fresh_scope(env);
    const char *value = Match(for_->value, Var)->name;
    if (for_->index) {
        const char *index = Match(for_->index, Var)->name;
        switch (iter_t->tag) {
        case ArrayType: {
            type_t *item_t = Match(iter_t, ArrayType)->item_type;
            set_binding(scope, index, new(binding_t, .type=Type(IntType, .bits=64), .code=CORD_cat("$", index)));
            set_binding(scope, value, new(binding_t, .type=item_t, .code=CORD_cat("$", value)));
            return scope;
        }
        case TableType: {
            type_t *key_t = Match(iter_t, TableType)->key_type;
            type_t *value_t = Match(iter_t, TableType)->value_type;
            set_binding(scope, index, new(binding_t, .type=key_t, .code=CORD_cat("$", index)));
            set_binding(scope, value, new(binding_t, .type=value_t, .code=CORD_cat("$", value)));
            return scope;
        }
        case IntType: {
            set_binding(scope, index, new(binding_t, .type=Type(IntType, .bits=64), .code=CORD_cat("$", index)));
            set_binding(scope, value, new(binding_t, .type=iter_t, .code=CORD_cat("$", value)));
            return scope;
        }
        default: code_err(for_->iter, "Iteration is not implemented for type: %T", iter_t);
        }
    } else {
        switch (iter_t->tag) {
        case ArrayType: {
            type_t *item_t = Match(iter_t, ArrayType)->item_type;
            set_binding(scope, value, new(binding_t, .type=item_t, .code=CORD_cat("$", value)));
            return scope;
        }
        case TableType: {
            type_t *key_t = Match(iter_t, TableType)->key_type;
            set_binding(scope, value, new(binding_t, .type=key_t, .code=CORD_cat("$", value)));
            return scope;
        }
        case IntType: {
            set_binding(scope, value, new(binding_t, .type=iter_t, .code=CORD_cat("$", value)));
            return scope;
        }
        default: code_err(for_->iter, "Iteration is not implemented for type: %T", iter_t);
        }
    }
}

env_t *namespace_env(env_t *env, const char *namespace_name)
{
    binding_t *b = get_binding(env, namespace_name);
    if (b)
        return Match(b->type, TypeInfoType)->env;

    env_t *ns_env = new(env_t);
    *ns_env = *env;
    ns_env->locals = new(table_t, .fallback=env->locals);
    ns_env->scope_prefix = CORD_all(env->file_prefix, namespace_name, "$");
    return ns_env;
}

binding_t *get_binding(env_t *env, const char *name)
{
    binding_t *b = Table$str_get(*env->locals, name);
    if (!b && env->fn_ctx && env->fn_ctx->closure_scope) {
        b = Table$str_get(*env->fn_ctx->closure_scope, name);
        if (b) {
            Table$str_set(env->fn_ctx->closed_vars, name, b);
            return new(binding_t, .type=b->type, .code=CORD_all("userdata->", name));
        }
    }
    return b;
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
    case BoolType: case IntType: case NumType: {
        binding_t *b = get_binding(env, CORD_to_const_char_star(type_to_cord(cls_type)));
        assert(b);
        return get_binding(Match(b->type, TypeInfoType)->env, name);
    }
    case TextType: {
        auto text = Match(cls_type, TextType);
        assert(text->env);
        return get_binding(text->env, name);
    }
    case StructType: {
        auto struct_ = Match(cls_type, StructType);
        return struct_->env ? get_binding(struct_->env, name) : NULL;
    }
    case EnumType: {
        auto enum_ = Match(cls_type, StructType);
        return enum_->env ? get_binding(enum_->env, name) : NULL;
    }
    case TypeInfoType: {
        auto info = Match(cls_type, TypeInfoType);
        return info->env ? get_binding(info->env, name) : NULL;
    }
    default: break;
    }
    code_err(self, "No such method!");
    return NULL;
}

void set_binding(env_t *env, const char *name, binding_t *binding)
{
    if (name && binding)
        Table$str_set(env->locals, name, binding);
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
