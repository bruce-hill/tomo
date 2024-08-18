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
type_t *RANGE_TYPE = NULL;
public type_t *THREAD_TYPE = NULL;

env_t *new_compilation_unit(CORD *libname)
{
    env_t *env = new(env_t);
    env->code = new(compilation_unit_t);
    env->types = new(table_t);
    env->globals = new(table_t);
    env->locals = new(table_t, .fallback=env->globals);
    env->imports = new(table_t);
    env->libname = libname;

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

    type_t *where;
    {
        env_t *where_env = namespace_env(env, "Where");
        type_t *anywhere = Type(StructType, .name="Anywhere");
        type_t *start = Type(StructType, .name="Start");
        type_t *end = Type(StructType, .name="End");
        where = Type(EnumType, .name="Where", .env=where_env,
                     .tags=new(tag_t, .name="Anywhere", .tag_value=0, .type=anywhere,
                               .next=new(tag_t, .name="Start", .tag_value=0, .type=start,
                                         .next=new(tag_t, .name="End", .tag_value=0, .type=end))));
        set_binding(where_env, "Anywhere", new(binding_t, .type=where, .code="Where$tagged$Anywhere"));
        set_binding(where_env, "Start", new(binding_t, .type=where, .code="Where$tagged$Start"));
        set_binding(where_env, "End", new(binding_t, .type=where, .code="Where$tagged$End"));
    }

    {
        env_t *range_env = namespace_env(env, "Range");
        RANGE_TYPE = Type(
            StructType, .name="Range", .env=range_env,
            .fields=new(arg_t, .name="first", .type=INT_TYPE,
              .next=new(arg_t, .name="last", .type=INT_TYPE,
              .next=new(arg_t, .name="step", .type=INT_TYPE, .default_val=FakeAST(Int, .str="1")))));
    }

    {
        env_t *thread_env = namespace_env(env, "Thread");
        THREAD_TYPE = Type(StructType, .name="Thread", .env=thread_env, .opaque=true);
    }

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
            {"from_text", "Bool$from_text", "func(text:Text, success=!&Bool)->Bool"},
            {"random", "Bool$random", "func(p=0.5)->Bool"},
        )},
        {"Int", Type(BigIntType), "Int_t", "$Int", TypedArray(ns_entry_t,
            {"format", "Int$format", "func(i:Int, digits=0)->Text"},
            {"hex", "Int$hex", "func(i:Int, digits=0, uppercase=yes, prefix=yes)->Text"},
            {"octal", "Int$octal", "func(i:Int, digits=0, prefix=yes)->Text"},
            {"random", "Int$random", "func(min:Int, max:Int)->Int"},
            {"from_text", "Int$from_text", "func(text:Text, the_rest=!&Text)->Int"},
            {"to", "Int$to", "func(from:Int,to:Int)->Range"},
            {"plus", "Int$plus", "func(x:Int,y:Int)->Int"},
            {"minus", "Int$minus", "func(x:Int,y:Int)->Int"},
            {"times", "Int$times", "func(x:Int,y:Int)->Int"},
            {"divided_by", "Int$divided_by", "func(x:Int,y:Int)->Int"},
            {"modulo", "Int$modulo", "func(x:Int,y:Int)->Int"},
            {"modulo1", "Int$modulo1", "func(x:Int,y:Int)->Int"},
            {"left_shifted", "Int$left_shifted", "func(x:Int,y:Int)->Int"},
            {"right_shifted", "Int$right_shifted", "func(x:Int,y:Int)->Int"},
            {"bit_and", "Int$bit_and", "func(x:Int,y:Int)->Int"},
            {"bit_or", "Int$bit_or", "func(x:Int,y:Int)->Int"},
            {"bit_xor", "Int$bit_xor", "func(x:Int,y:Int)->Int"},
            {"negative", "Int$negative", "func(x:Int)->Int"},
            {"negated", "Int$negated", "func(x:Int)->Int"},
            {"abs", "Int$abs", "func(x:Int)->Int"},
            {"sqrt", "Int$sqrt", "func(x:Int)->Int"},
            {"power", "Int$power", "func(base:Int,exponent:Int)->Int"},
            {"is_prime", "Int$is_prime", "func(x:Int,reps=50)->Bool"},
            {"next_prime", "Int$next_prime", "func(x:Int)->Int"},
            {"prev_prime", "Int$prev_prime", "func(x:Int)->Int"},
        )},
        {"Int64", Type(IntType, .bits=TYPE_IBITS64), "Int64_t", "$Int64", TypedArray(ns_entry_t,
            {"format", "Int64$format", "func(i:Int64, digits=0)->Text"},
            {"hex", "Int64$hex", "func(i:Int64, digits=0, uppercase=yes, prefix=yes)->Text"},
            {"octal", "Int64$octal", "func(i:Int64, digits=0, prefix=yes)->Text"},
            {"random", "Int64$random", "func(min=-0x8000000000000000_i64, max=0x7FFFFFFFFFFFFFFF_i64)->Int64"},
            {"from_text", "Int64$from_text", "func(text:Text, the_rest=!&Text)->Int64"},
            {"bits", "Int64$bits", "func(x:Int64)->[Bool]"},
            {"abs", "labs", "func(i:Int64)->Int64"},
            {"min", "Int64$min", "Int64"},
            {"max", "Int64$max", "Int64"},
            {"to", "Int64$to", "func(from:Int64,to:Int64)->Range"},
            {"divided_by", "Int64$divided_by", "func(x:Int64,y:Int64)->Int64"},
            {"modulo", "Int64$modulo", "func(x:Int64,y:Int64)->Int64"},
            {"modulo1", "Int64$modulo1", "func(x:Int64,y:Int64)->Int64"},
        )},
        {"Int32", Type(IntType, .bits=TYPE_IBITS32), "Int32_t", "$Int32", TypedArray(ns_entry_t,
            {"format", "Int32$format", "func(i:Int32, digits=0)->Text"},
            {"hex", "Int32$hex", "func(i:Int32, digits=0, uppercase=yes, prefix=yes)->Text"},
            {"octal", "Int32$octal", "func(i:Int32, digits=0, prefix=yes)->Text"},
            {"random", "Int32$random", "func(min=-0x80000000_i32, max=0x7FFFFFFF_i32)->Int32"},
            {"from_text", "Int$from_text", "func(text:Text, the_rest=!&Text)->Int32"},
            {"bits", "Int32$bits", "func(x:Int32)->[Bool]"},
            {"abs", "abs", "func(i:Int32)->Int32"},
            {"min", "Int32$min", "Int32"},
            {"max", "Int32$max", "Int32"},
            {"to", "Int32$to", "func(from:Int32,to:Int32)->Range"},
            {"divided_by", "Int32$divided_by", "func(x:Int32,y:Int32)->Int32"},
            {"modulo", "Int32$modulo", "func(x:Int32,y:Int32)->Int32"},
            {"modulo1", "Int32$modulo1", "func(x:Int32,y:Int32)->Int32"},
        )},
        {"Int16", Type(IntType, .bits=TYPE_IBITS16), "Int16_t", "$Int16", TypedArray(ns_entry_t,
            {"format", "Int16$format", "func(i:Int16, digits=0)->Text"},
            {"hex", "Int16$hex", "func(i:Int16, digits=0, uppercase=yes, prefix=yes)->Text"},
            {"octal", "Int16$octal", "func(i:Int16, digits=0, prefix=yes)->Text"},
            {"random", "Int16$random", "func(min=-0x8000_i16, max=0x7FFF_i16)->Int16"},
            {"from_text", "Int$from_text", "func(text:Text, the_rest=!&Text)->Int16"},
            {"bits", "Int16$bits", "func(x:Int16)->[Bool]"},
            {"abs", "abs", "func(i:Int16)->Int16"},
            {"min", "Int16$min", "Int16"},
            {"max", "Int16$max", "Int16"},
            {"to", "Int16$to", "func(from:Int16,to:Int16)->Range"},
            {"divided_by", "Int16$divided_by", "func(x:Int16,y:Int16)->Int16"},
            {"modulo", "Int16$modulo", "func(x:Int16,y:Int16)->Int16"},
            {"modulo1", "Int16$modulo1", "func(x:Int16,y:Int16)->Int16"},
        )},
        {"Int8", Type(IntType, .bits=TYPE_IBITS8), "Int8_t", "$Int8", TypedArray(ns_entry_t,
            {"format", "Int8$format", "func(i:Int8, digits=0)->Text"},
            {"hex", "Int8$hex", "func(i:Int8, digits=0, uppercase=yes, prefix=yes)->Text"},
            {"octal", "Int8$octal", "func(i:Int8, digits=0, prefix=yes)->Text"},
            {"random", "Int8$random", "func(min=-0x80_i8, max=0x7F_i8)->Int8"},
            {"from_text", "Int$from_text", "func(text:Text, the_rest=!&Text)->Int8"},
            {"bits", "Int8$bits", "func(x:Int8)->[Bool]"},
            {"abs", "abs", "func(i:Int8)->Int8"},
            {"min", "Int8$min", "Int8"},
            {"max", "Int8$max", "Int8"},
            {"to", "Int8$to", "func(from:Int8,to:Int8)->Range"},
            {"divided_by", "Int8$divided_by", "func(x:Int8,y:Int8)->Int8"},
            {"modulo", "Int8$modulo", "func(x:Int8,y:Int8)->Int8"},
            {"modulo1", "Int8$modulo1", "func(x:Int8,y:Int8)->Int8"},
        )},
#define C(name) {#name, "M_"#name, "Num"}
#define F(name) {#name, #name, "func(n:Num)->Num"}
#define F2(name) {#name, #name, "func(x:Num, y:Num)->Num"}
        {"Num", Type(NumType, .bits=TYPE_NBITS64), "Num_t", "$Num", TypedArray(ns_entry_t,
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
            {"mix", "Num$mix", "func(amount:Num, x:Num, y:Num)->Num"},
            {"from_text", "Num$from_text", "func(text:Text, the_rest=!&Text)->Num"},
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
        {"Num32", Type(NumType, .bits=TYPE_NBITS32), "Num32_t", "$Num32", TypedArray(ns_entry_t,
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
            {"mix", "Num32$mix", "func(amount:Num32, x:Num32, y:Num32)->Num32"},
            {"from_text", "Num32$from_text", "func(text:Text, the_rest=!&Text)->Num32"},
            {"abs", "fabsf", "func(n:Num32)->Num32"},
            F(acos), F(acosh), F(asin), F(asinh), F(atan), F(atanh), F(cbrt), F(ceil), F(cos), F(cosh), F(erf), F(erfc),
            F(exp), F(exp2), F(expm1), F(floor), F(j0), F(j1), F(log), F(log10), F(log1p), F(log2), F(logb),
            F(rint), F(round), F(significand), F(sin), F(sinh), F(sqrt),
            F(tan), F(tanh), F(tgamma), F(trunc), F(y0), F(y1),
            F2(atan2), F2(copysign), F2(fdim), F2(hypot), F2(nextafter), F2(pow), F2(remainder),
        )},
        {"CString", Type(CStringType), "char*", "$CString", TypedArray(ns_entry_t,
            {"as_text", "CORD_from_char_star", "func(str:CString)->Text"},
        )},
#undef F2
#undef F
#undef C
        {"Where", where, "Where_t", "Where", {}},
        {"Range", RANGE_TYPE, "Range_t", "Range", TypedArray(ns_entry_t,
            {"reversed", "Range$reversed", "func(range:Range)->Range"},
            {"by", "Range$by", "func(range:Range, step:Int)->Range"},
        )},
        {"Text", TEXT_TYPE, "Text_t", "$Text", TypedArray(ns_entry_t,
            // {"find", "Text$find", "func(text:Text, pattern:Text)->FindResult"},
            {"as_c_string", "CORD_to_char_star", "func(text:Text)->CString"},
            {"bytes", "Text$bytes", "func(text:Text)->[Int8]"},
            {"character_names", "Text$character_names", "func(text:Text)->[Text]"},
            {"clusters", "Text$clusters", "func(text:Text)->[Text]"},
            {"codepoints", "Text$codepoints", "func(text:Text)->[Int32]"},
            {"from_c_string", "CORD_from_char_star", "func(str:CString)->Text"},
            {"has", "Text$has", "func(text:Text, target:Text, where=Where.Anywhere)->Bool"},
            {"join", "Text$join", "func(glue:Text, pieces:[Text])->Text"},
            {"lower", "Text$lower", "func(text:Text)->Text"},
            {"num_bytes", "Text$num_bytes", "func(text:Text)->Int"},
            {"num_clusters", "Text$num_clusters", "func(text:Text)->Int"},
            {"num_codepoints", "Text$num_codepoints", "func(text:Text)->Int"},
            {"quoted", "Text$quoted", "func(text:Text, color=no)->Text"},
            {"replace", "Text$replace", "func(text:Text, pattern:Text, replacement:Text, limit=-1)->Text"},
            {"split", "Text$split", "func(text:Text, split:Text)->[Text]"},
            {"title", "Text$title", "func(text:Text)->Text"},
            {"title", "Text$title", "func(text:Text)->Text"},
            {"trimmed", "Text$trimmed", "func(text:Text, trim=\" {\\n\\r\\t}\", where=Where.Anywhere)->Text"},
            {"upper", "Text$upper", "func(text:Text)->Text"},
            {"without", "Text$without", "func(text:Text, target:Text, where=Where.Anywhere)->Text"},
        )},
        {"Thread", THREAD_TYPE, "pthread_t*", "Thread", TypedArray(ns_entry_t,
            {"new", "Thread$new", "func(fn:func()->Void)->Thread"},
            {"cancel", "Thread$cancel", "func(thread:Thread)->Void"},
            {"join", "Thread$join", "func(thread:Thread)->Void"},
            {"detach", "Thread$detach", "func(thread:Thread)->Void"},
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

CORD namespace_prefix(CORD *libname, namespace_t *ns)
{
    CORD prefix = CORD_EMPTY;
    for (; ns; ns = ns->parent)
        prefix = CORD_all(ns->name, "$", prefix);
    if (libname && *libname)
        prefix = CORD_all(*libname, "$", prefix);
    return prefix;
}

env_t *load_module_env(env_t *env, ast_t *ast)
{
    const char *name = file_base_name(ast->file->filename);
    env_t *cached = Table$str_get(*env->imports, name);
    if (cached) return cached;
    env_t *module_env = fresh_scope(env);
    module_env->code = new(compilation_unit_t);
    module_env->namespace = new(namespace_t, .name=name); 
    Table$str_set(module_env->imports, name, module_env);

    for (ast_list_t *stmt = Match(ast, Block)->statements; stmt; stmt = stmt->next)
        prebind_statement(module_env, stmt->ast);

    for (ast_list_t *stmt = Match(ast, Block)->statements; stmt; stmt = stmt->next)
        bind_statement(module_env, stmt->ast);

    return module_env;
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

    if (iter_t == RANGE_TYPE) {
        if (for_->vars) {
            if (for_->vars->next)
                code_err(for_->vars->next->ast, "This is too many variables for this loop");
            const char *var = Match(for_->vars->ast, Var)->name;
            set_binding(scope, var, new(binding_t, .type=INT_TYPE, .code=CORD_cat("$", var)));
        }
        return scope;
    }

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
            set_binding(scope, vars[0], new(binding_t, .type=item_t, .code=CORD_cat("$", vars[0])));
        } else if (num_vars == 2) {
            set_binding(scope, vars[0], new(binding_t, .type=INT_TYPE, .code=CORD_cat("$", vars[0])));
            set_binding(scope, vars[1], new(binding_t, .type=item_t, .code=CORD_cat("$", vars[1])));
        }
        return scope;
    }
    case SetType: {
        if (for_->vars) {
            if (for_->vars->next)
                code_err(for_->vars->next->ast, "This is too many variables for this loop");
            type_t *item_type = Match(iter_t, SetType)->item_type;
            const char *name = Match(for_->vars->ast, Var)->name;
            set_binding(scope, name, new(binding_t, .type=item_type, .code=CORD_cat("$", name)));
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
            set_binding(scope, vars[0], new(binding_t, .type=key_t, .code=CORD_cat("$", vars[0])));
        } else if (num_vars == 2) {
            set_binding(scope, vars[0], new(binding_t, .type=key_t, .code=CORD_cat("$", vars[0])));
            type_t *value_t = Match(iter_t, TableType)->value_type;
            set_binding(scope, vars[1], new(binding_t, .type=value_t, .code=CORD_cat("$", vars[1])));
        }
        return scope;
    }
    case BigIntType: {
        if (for_->vars) {
            if (for_->vars->next)
                code_err(for_->vars->next->ast, "This is too many variables for this loop");
            const char *var = Match(for_->vars->ast, Var)->name;
            set_binding(scope, var, new(binding_t, .type=INT_TYPE, .code=CORD_cat("$", var)));
        }
        return scope;
    }
    case FunctionType: case ClosureType: {
        auto fn = iter_t->tag == ClosureType ? Match(Match(iter_t, ClosureType)->fn, FunctionType) : Match(iter_t, FunctionType);
        if (fn->ret->tag != EnumType)
            code_err(for_->iter, "Iterator functions must return an enum with a Done and Next field");
        auto iter_enum = Match(fn->ret, EnumType);
        type_t *next_type = NULL;
        for (tag_t *tag = iter_enum->tags; tag; tag = tag->next) {
            if (streq(tag->name, "Done")) {
                if (Match(tag->type, StructType)->fields)
                    code_err(for_->iter, "This iterator function returns an enum with a Done field that has values, when none are allowed");
            } else if (streq(tag->name, "Next")) {
                next_type = tag->type;
            } else {
                code_err(for_->iter, "This iterator function returns an enum with a value that isn't Done or Next: %s", tag->name);
            }
        }

        if (!next_type)
            code_err(for_->iter, "This iterator function returns an enum that doesn't have a Next field");

        arg_t *iter_field = Match(next_type, StructType)->fields;
        for (ast_list_t *var = for_->vars; var; var = var->next) {
            if (!iter_field)
                code_err(var->ast, "This is one variable too many for this iterator, which returns a %T", fn->ret);
            const char *name = Match(var->ast, Var)->name;
            type_t *t = get_arg_type(env, iter_field);
            set_binding(scope, name, new(binding_t, .type=t, .code=CORD_cat("cur.$Next.$", iter_field->name)));
            iter_field = iter_field->next;
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
    ns_env->locals = new(table_t, .fallback=env->locals);
    ns_env->namespace = new(namespace_t, .name=namespace_name, .parent=env->namespace);
    return ns_env;
}

binding_t *get_binding(env_t *env, const char *name)
{
    binding_t *b = Table$str_get(*env->locals, name);
    if (!b) {
        for (fn_ctx_t *fn_ctx = env->fn_ctx; fn_ctx; fn_ctx = fn_ctx->parent) {
            if (!fn_ctx->closure_scope) continue;
            b = Table$str_get(*fn_ctx->closure_scope, name);
            if (b) {
                Table$str_set(env->fn_ctx->closed_vars, name, b);
                binding_t *b2 = new(binding_t, .type=b->type, .code=CORD_all("userdata->", name));
                Table$str_set(env->locals, name, b2);
                return b2;
            }
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
    case ArrayType: return NULL;
    case TableType: return NULL;
    case BoolType: case IntType: case BigIntType: case NumType: {
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
        highlight_error(f, start, end, "\x1b[31;1m", 2, isatty(STDERR_FILENO) && !getenv("NO_COLOR"));

    raise(SIGABRT);
    exit(1);
}

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
