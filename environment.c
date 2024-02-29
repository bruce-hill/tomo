
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
        {"Int", Type(IntType, .bits=64), "Int_t", "Int", {}},
        {"Int32", Type(IntType, .bits=32), "Int32_t", "Int32", {}},
        {"Int16", Type(IntType, .bits=16), "Int16_t", "Int16", {}},
        {"Int8", Type(IntType, .bits=8), "Int8_t", "Int8", {}},
        {"Num", Type(NumType, .bits=64), "Num_t", "Num", {}},
        {"Num32", Type(NumType, .bits=32), "Num32_t", "Num32", {}},
        {"Str", Type(StringType), "Str_t", "Str", $TypedArray(ns_entry_t,
            {"quoted", "Str__quoted", "func(s:Str, color=no)->Str"}
        )},
    };

    for (size_t i = 0; i < sizeof(global_types)/sizeof(global_types[0]); i++) {
        binding_t *binding = new(binding_t, .type=Type(TypeInfoType));
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
