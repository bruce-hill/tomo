
#include <stdlib.h>

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

env_t *new_environment(void)
{
    env_t *env = new(env_t);

    struct {
        const char *name;
        binding_t binding;
    } global_vars[] = {
        {"say", {.code="say", .type=Type(FunctionType, .args=new(arg_t, .name="text", .type=Type(StringType)), .ret=Type(VoidType))}},
        {"fail", {.code="fail", .type=Type(FunctionType, .args=new(arg_t, .name="message", .type=Type(StringType)), .ret=Type(AbortType))}},
        {"USE_COLOR", {.code="USE_COLOR", .type=Type(BoolType)}},
    };

    for (size_t i = 0; i < sizeof(global_vars)/sizeof(global_vars[0]); i++) {
        Table_str_set(&env->globals, global_vars[i].name, &global_vars[i].binding);
    }

    struct {
        const char *name;
        type_t *type;
        CORD typename;
        CORD struct_val;
        table_t namespace;
    } global_types[] = {
        {"Bool", Type(BoolType), "Bool_t", "Bool", {}},
        {"Int", Type(IntType, .bits=64), "Int_t", "Int", Table_from_entries(*(array_t*)ARRAY(
            new(ns_entry_t, "min", {"Int.min", Type(IntType, .bits=64)}),
            new(ns_entry_t, "max", {"Int.max", Type(IntType, .bits=64)}),
        ), &StrToVoidStarTable_type)},
    };

    for (size_t i = 0; i < sizeof(global_types)/sizeof(global_types[0]); i++) {
        Table_str_set(&env->globals, global_types[i].name, namespace_type(&global_types[i].namespace));
        Table_str_set(&env->types, global_types[i].name, global_types[i].type);
    }

    return env;
}

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
