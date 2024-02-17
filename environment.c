
#include <stdlib.h>
#include <signal.h>

#include "environment.h"
#include "builtins/table.h"
#include "builtins/string.h"
#include "util.h"

typedef struct {
    const char *name;
    binding_t binding;
} ns_entry_t;

static type_t *namespace_type(table_t *ns)
{
    arg_t *fields = NULL;
    for (int64_t i = Table_length(ns); i >= 1; i--) {
        struct {const char *name; binding_t binding; } *entry = Table_entry(ns, i);
        fields = new(arg_t, .next=fields, .name=entry->name, .type=entry->binding.type);
    }
    return Type(StructType, .fields=fields);
}

env_t *new_compilation_unit(void)
{
    env_t *env = new(env_t);
    env->code = new(compilation_unit_t);
    env->types = new(table_t);
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
        Table_str_set(env->globals, global_vars[i].name, &global_vars[i].binding);
    }

    struct {
        const char *name;
        type_t *type;
        CORD typename;
        CORD struct_val;
        table_t namespace;
    } global_types[] = {
        {"Bool", Type(BoolType), "Bool_t", "Bool", {}},
        {"Int", Type(IntType, .bits=64), "Int_t", "Int", {}},
    };

    for (size_t i = 0; i < sizeof(global_types)/sizeof(global_types[0]); i++) {
        Table_str_set(env->globals, global_types[i].name, namespace_type(&global_types[i].namespace));
        Table_str_set(env->types, global_types[i].name, global_types[i].type);
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
