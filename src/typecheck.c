// Logic for getting a type from an AST node
#include <ctype.h>
#include <gc.h>
#include <glob.h>
#include <signal.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "ast.h"
#include "cordhelpers.h"
#include "environment.h"
#include "parse.h"
#include "stdlib/paths.h"
#include "stdlib/tables.h"
#include "stdlib/text.h"
#include "stdlib/util.h"
#include "typecheck.h"
#include "types.h"

type_t *parse_type_ast(env_t *env, type_ast_t *ast)
{
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wswitch-default"
#endif
    switch (ast->tag) {
    case VarTypeAST: {
        const char *name = Match(ast, VarTypeAST)->name;
        type_t *t = Table$str_get(*env->types, name);
        if (t) return t;
        while (strchr(name, '.')) {
            char *module_name = GC_strndup(name, strcspn(name, "."));
            binding_t *b = get_binding(env, module_name);
            if (!b || b->type->tag != ModuleType)
                code_err(ast, "I don't know a module with the name '", module_name, "'");

            env_t *imported = Table$str_get(*env->imports, Match(b->type, ModuleType)->name);
            assert(imported);
            env = imported;
            name = strchr(name, '.') + 1;
            t = Table$str_get(*env->types, name);
            if (t) return t;
        }
        code_err(ast, "I don't know a type with the name '", name, "'");
    }
    case PointerTypeAST: {
        DeclareMatch(ptr, ast, PointerTypeAST);
        type_t *pointed_t = parse_type_ast(env, ptr->pointed);
        if (pointed_t->tag == VoidType)
            code_err(ast, "Void pointers are not supported. You probably meant 'Memory' instead of 'Void'");
        return Type(PointerType, .pointed=pointed_t, .is_stack=ptr->is_stack);
    }
    case ListTypeAST: {
        type_ast_t *item_type = Match(ast, ListTypeAST)->item;
        type_t *item_t = parse_type_ast(env, item_type);
        if (!item_t) code_err(item_type, "I can't figure out what this type is.");
        if (has_stack_memory(item_t))
            code_err(item_type, "Lists can't have stack references because the list may outlive the stack frame.");
        if (type_size(item_t) > LIST_MAX_STRIDE)
            code_err(ast, "This list holds items that take up ", (uint64_t)type_size(item_t),
                     " bytes, but the maximum supported size is ", LIST_MAX_STRIDE, " bytes. Consider using a list of pointers instead.");
        return Type(ListType, .item_type=item_t);
    }
    case SetTypeAST: {
        type_ast_t *item_type = Match(ast, SetTypeAST)->item;
        type_t *item_t = parse_type_ast(env, item_type);
        if (!item_t) code_err(item_type, "I can't figure out what this type is.");
        if (has_stack_memory(item_t))
            code_err(item_type, "Sets can't have stack references because the list may outlive the stack frame.");
        if (type_size(item_t) > LIST_MAX_STRIDE)
            code_err(ast, "This set holds items that take up ", (uint64_t)type_size(item_t),
                     " bytes, but the maximum supported size is ", LIST_MAX_STRIDE, " bytes. Consider using an set of pointers instead.");
        return Type(SetType, .item_type=item_t);
    }
    case TableTypeAST: {
        DeclareMatch(table_type, ast, TableTypeAST);
        type_ast_t *key_type_ast = table_type->key;
        type_t *key_type = parse_type_ast(env, key_type_ast);
        if (!key_type) code_err(key_type_ast, "I can't figure out what type this is.");
        if (has_stack_memory(key_type))
            code_err(key_type_ast, "Tables can't have stack references because the list may outlive the stack frame.");

        type_t *val_type = parse_type_ast(env, table_type->value);
        if (!val_type) code_err(table_type->value, "I can't figure out what type this is.");
        if (has_stack_memory(val_type))
            code_err(table_type->value, "Tables can't have stack references because the list may outlive the stack frame.");
        else if (val_type->tag == OptionalType)
            code_err(ast, "Tables with optional-typed values are not currently supported");

        return Type(TableType, .key_type=key_type, .value_type=val_type, .env=env, .default_value=table_type->default_value);
    }
    case FunctionTypeAST: {
        DeclareMatch(fn, ast, FunctionTypeAST);
        type_t *ret_t = fn->ret ? parse_type_ast(env, fn->ret) : Type(VoidType);
        if (has_stack_memory(ret_t))
            code_err(fn->ret, "Functions are not allowed to return stack references, because the reference may no longer exist on the stack.");
        arg_t *type_args = NULL;
        for (arg_ast_t *arg = fn->args; arg; arg = arg->next) {
            type_args = new(arg_t, .name=arg->name, .next=type_args);
            if (arg->type)
                type_args->type = parse_type_ast(env, arg->type);
            else if (arg->value)
                type_args->type = get_type(env, arg->value);

            if (arg->value)
                type_args->default_val = arg->value;
        }
        REVERSE_LIST(type_args);
        return Type(ClosureType, Type(FunctionType, .args=type_args, .ret=ret_t));
    }
    case OptionalTypeAST: {
        DeclareMatch(opt, ast, OptionalTypeAST);
        type_t *t = parse_type_ast(env, opt->type);
        if (t->tag == VoidType || t->tag == AbortType || t->tag == ReturnType)
            code_err(ast, "Optional ", type_to_str(t), " types are not supported.");
        else if (t->tag == OptionalType)
            code_err(ast, "Nested optional types are not currently supported");
        return Type(OptionalType, .type=t);
    }
    case UnknownTypeAST: code_err(ast, "I don't know how to get this type");
    }
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
    errx(1, "Unreachable");
    return NULL;
}

// static PUREFUNC bool risks_zero_or_inf(ast_t *ast)
// {
//     switch (ast->tag) {
//     case Int: {
//         const char *str = Match(ast, Int)->str;
//         OptionalInt_t int_val = Int$from_str(str);
//         return (int_val.small == 0x1); // zero
//     }
//     case Num: {
//         return Match(ast, Num)->n == 0.0;
//     }
//     case BINOP_CASES: {
//         binary_operands_t binop = BINARY_OPERANDS(ast);
//         if (ast->tag == Multiply || ast->tag == Divide || ast->tag == Min || ast->tag == Max)
//             return risks_zero_or_inf(binop.lhs) || risks_zero_or_inf(binop.rhs);
//         else
//             return true;
//     }
//     default: return true;
//     }
// }

PUREFUNC type_t *get_math_type(env_t *env, ast_t *ast, type_t *lhs_t, type_t *rhs_t)
{
    (void)env;
    switch (compare_precision(lhs_t, rhs_t)) {
    case NUM_PRECISION_EQUAL: case NUM_PRECISION_MORE: return lhs_t;
    case NUM_PRECISION_LESS: return rhs_t;
    default: code_err(ast, "Math operations between ", type_to_str(lhs_t), " and ", type_to_str(rhs_t), " are not supported");
    }
    return NULL;
}

static env_t *load_module(env_t *env, ast_t *module_ast)
{
    DeclareMatch(use, module_ast, Use);
    switch (use->what) {
    case USE_LOCAL: {
        Path_t source_path = Path$from_str(module_ast->file->filename);
        Path_t source_dir = Path$parent(source_path);
        Path_t used_path = Path$resolved(Path$from_str(use->path), source_dir);

        if (!Path$exists(used_path))
            code_err(module_ast, "No such file exists: ", quoted(use->path));

        env_t *module_env = Table$str_get(*env->imports, String(used_path));
        if (module_env)
            return module_env;

        ast_t *ast = parse_file(String(used_path), NULL);
        if (!ast) print_err("Could not compile file ", used_path);
        return load_module_env(env, ast);
    }
    case USE_MODULE: {
        glob_t tm_files;
        if (glob(String(TOMO_HOME"/installed/", use->path, "/[!._0-9]*.tm"), GLOB_TILDE, NULL, &tm_files) != 0)
            code_err(module_ast, "Could not find library");

        env_t *module_env = fresh_scope(env);
        Table$str_set(env->imports, use->path, module_env);
        char *libname_id = String(use->path);
        for (char *p = libname_id; *p; p++) {
            if (!isalnum(*p) && *p != '_')
                *p = '_';
        }
        module_env->libname = libname_id;
        for (size_t i = 0; i < tm_files.gl_pathc; i++) {
            const char *filename = tm_files.gl_pathv[i];
            ast_t *ast = parse_file(filename, NULL);
            if (!ast) print_err("Could not compile file ", filename);
            env_t *module_file_env = fresh_scope(module_env);
            char *file_prefix = file_base_id(filename);
            module_file_env->namespace = new(namespace_t, .name=file_prefix);
            env_t *subenv = load_module_env(module_file_env, ast);
            for (int64_t j = 0; j < subenv->locals->entries.length; j++) {
                struct {
                    const char *name; binding_t *binding;
                } *entry = subenv->locals->entries.data + j*subenv->locals->entries.stride;
                Table$str_set(module_env->locals, entry->name, entry->binding);
            }
        }
        globfree(&tm_files);
        return module_env;
    }
    default: return NULL;
    }
}

void prebind_statement(env_t *env, ast_t *statement)
{
    switch (statement->tag) {
    case DocTest: {
        prebind_statement(env, Match(statement, DocTest)->expr);
        break;
    }
    case StructDef: {
        DeclareMatch(def, statement, StructDef);
        if (get_binding(env, def->name))
            code_err(statement, "A ", type_to_str(get_binding(env, def->name)->type), " called ", quoted(def->name), " has already been defined");

        env_t *ns_env = namespace_env(env, def->name);
        type_t *type = Type(StructType, .name=def->name, .opaque=true, .external=def->external, .env=ns_env); // placeholder
        Table$str_set(env->types, def->name, type);
        set_binding(env, def->name, Type(TypeInfoType, .name=def->name, .type=type, .env=ns_env),
                    CORD_all(namespace_prefix(env, env->namespace), def->name, "$$info"));
        for (ast_list_t *stmt = def->namespace ? Match(def->namespace, Block)->statements : NULL; stmt; stmt = stmt->next)
            prebind_statement(ns_env, stmt->ast);
        break;
    }
    case EnumDef: {
        DeclareMatch(def, statement, EnumDef);
        if (get_binding(env, def->name))
            code_err(statement, "A ", type_to_str(get_binding(env, def->name)->type), " called ", quoted(def->name), " has already been defined");

        env_t *ns_env = namespace_env(env, def->name);
        type_t *type = Type(EnumType, .name=def->name, .opaque=true, .env=ns_env); // placeholder
        Table$str_set(env->types, def->name, type);
        set_binding(env, def->name, Type(TypeInfoType, .name=def->name, .type=type, .env=ns_env),
                    CORD_all(namespace_prefix(env, env->namespace), def->name, "$$info"));
        for (ast_list_t *stmt = def->namespace ? Match(def->namespace, Block)->statements : NULL; stmt; stmt = stmt->next)
            prebind_statement(ns_env, stmt->ast);
        break;
    }
    case LangDef: {
        DeclareMatch(def, statement, LangDef);
        if (get_binding(env, def->name))
            code_err(statement, "A ", type_to_str(get_binding(env, def->name)->type), " called ", quoted(def->name), " has already been defined");

        env_t *ns_env = namespace_env(env, def->name);
        type_t *type = Type(TextType, .lang=def->name, .env=ns_env);
        Table$str_set(env->types, def->name, type);
        set_binding(env, def->name, Type(TypeInfoType, .name=def->name, .type=type, .env=ns_env),
                    CORD_all(namespace_prefix(env, env->namespace), def->name, "$$info"));
        for (ast_list_t *stmt = def->namespace ? Match(def->namespace, Block)->statements : NULL; stmt; stmt = stmt->next)
            prebind_statement(ns_env, stmt->ast);
        break;
    }
    case Extend: {
        DeclareMatch(extend, statement, Extend);
        env_t *ns_env = namespace_env(env, extend->name);
        env_t *extended = new(env_t);
        *extended = *ns_env;
        extended->locals = new(Table_t, .fallback=env->locals);
        extended->namespace_bindings = new(Table_t, .fallback=env->namespace_bindings);
        extended->libname = env->libname;
        for (ast_list_t *stmt = extend->body ? Match(extend->body, Block)->statements : NULL; stmt; stmt = stmt->next)
            prebind_statement(extended, stmt->ast);
        List_t new_bindings = extended->locals->entries;
        for (int64_t i = 0; i < new_bindings.length; i++) {
            struct { const char *name; binding_t *binding; } *entry = new_bindings.data + i*new_bindings.stride;
            binding_t *clobbered = Table$str_get(*ns_env->locals, entry->name);
            if (clobbered && !type_eq(clobbered->type, entry->binding->type))
                code_err(statement, "This `extend` block overwrites the binding for ", quoted(entry->name),
                         " in the original namespace (with type ", type_to_str(clobbered->type), ") with a new binding with type ",
                         type_to_str(entry->binding->type));
            Table$str_set(ns_env->locals, entry->name, entry->binding);
        }
        break;
    }
    default: break;
    }
}

void bind_statement(env_t *env, ast_t *statement)
{
    switch (statement->tag) {
    case DocTest: {
        bind_statement(env, Match(statement, DocTest)->expr);
        break;
    }
    case Declare: {
        DeclareMatch(decl, statement, Declare);
        const char *name = Match(decl->var, Var)->name;
        if (streq(name, "_")) // Explicit discard
            return;
        if (get_binding(env, name))
            code_err(decl->var, "A ", type_to_str(get_binding(env, name)->type), " called ", quoted(name), " has already been defined");
        if (decl->value)
            bind_statement(env, decl->value);
        type_t *type = decl->type ? parse_type_ast(env, decl->type) : get_type(env, decl->value);
        if (!type)
            code_err(statement, "I couldn't figure out the type of this value");
        if (type->tag == FunctionType)
            type = Type(ClosureType, type);
        CORD prefix = namespace_prefix(env, env->namespace);
        CORD code = CORD_cat(prefix ? prefix : "$", name);
        set_binding(env, name, type, code);
        break;
    }
    case FunctionDef: {
        DeclareMatch(def, statement, FunctionDef);
        const char *name = Match(def->name, Var)->name;
        type_t *type = get_function_def_type(env, statement);
        CORD code = CORD_all(namespace_prefix(env, env->namespace), name);
        set_binding(env, name, type, code);
        break;
    }
    case ConvertDef: {
        type_t *type = get_function_def_type(env, statement);
        type_t *ret_t = Match(type, FunctionType)->ret;
        const char *name = get_type_name(ret_t);
        if (!name)
            code_err(statement, "Conversions are only supported for text, struct, and enum types, not ", type_to_str(ret_t));

        CORD code = CORD_asprintf("%r%r$%ld", namespace_prefix(env, env->namespace), name,
                                  get_line_number(statement->file, statement->start));
        binding_t binding = {.type=type, .code=code};
        env_t *type_ns = get_namespace_by_type(env, ret_t);
        List$insert(&type_ns->namespace->constructors, &binding, I(0), sizeof(binding));
        break;
    }
    case StructDef: {
        DeclareMatch(def, statement, StructDef);
        env_t *ns_env = namespace_env(env, def->name);
        type_t *type = Table$str_get(*env->types, def->name);
        if (!type) code_err(statement, "Couldn't find type!");
        assert(type);
        if (!def->opaque) {
            arg_t *fields = NULL;
            for (arg_ast_t *field_ast = def->fields; field_ast; field_ast = field_ast->next) {
                type_t *field_t = get_arg_ast_type(env, field_ast);
                type_t *non_opt_field_t = field_t->tag == OptionalType ? Match(field_t, OptionalType)->type : field_t;
                if ((non_opt_field_t->tag == StructType && Match(non_opt_field_t, StructType)->opaque)
                    || (non_opt_field_t->tag == EnumType && Match(non_opt_field_t, EnumType)->opaque)) {

                    file_t *file = NULL;
                    const char *start = NULL, *end = NULL;
                    if (field_ast->type) {
                        file = field_ast->type->file, start = field_ast->type->start, end = field_ast->type->end;
                    } else if (field_ast->value) {
                        file = field_ast->value->file, start = field_ast->value->start, end = field_ast->value->end;
                    }
                    if (non_opt_field_t == type)
                        compiler_err(file, start, end, "This is a recursive struct that would be infinitely large. Maybe you meant to use an optional '@", type_to_str(type), "?' pointer instead?");
                    else if (non_opt_field_t->tag == StructType && Match(non_opt_field_t, StructType)->external)
                        compiler_err(file, start, end, "This is an opaque externally defined struct.\n"
                                     "I can't use it as a member without knowing what its fields are.\n"
                                     "Either specify its fields and remove the `opaque` qualifier, or use something like a @", type_to_str(non_opt_field_t), " pointer.");
                    else
                        compiler_err(file, start, end, "I'm still in the process of defining the fields of ", type_to_str(field_t), ", so I don't know how to use it as a member."
                                     "\nTry using a @", type_to_str(field_t), " pointer for this field.");
                }
                fields = new(arg_t, .name=field_ast->name, .type=field_t, .default_val=field_ast->value, .next=fields);
            }
            REVERSE_LIST(fields);
            type->__data.StructType.fields = fields; // populate placeholder
            type->__data.StructType.opaque = false;
        }

        for (ast_list_t *stmt = def->namespace ? Match(def->namespace, Block)->statements : NULL; stmt; stmt = stmt->next)
            bind_statement(ns_env, stmt->ast);
        break;
    }
    case EnumDef: {
        DeclareMatch(def, statement, EnumDef);
        env_t *ns_env = namespace_env(env, def->name);
        type_t *type = Table$str_get(*env->types, def->name);
        assert(type);
        tag_t *tags = NULL;
        int64_t next_tag = 1;
        for (tag_ast_t *tag_ast = def->tags; tag_ast; tag_ast = tag_ast->next) {
            arg_t *fields = NULL;
            for (arg_ast_t *field_ast = tag_ast->fields; field_ast; field_ast = field_ast->next) {
                type_t *field_t = get_arg_ast_type(env, field_ast);
                type_t *non_opt_field_t = field_t->tag == OptionalType ? Match(field_t, OptionalType)->type : field_t;
                if ((non_opt_field_t->tag == StructType && Match(non_opt_field_t, StructType)->opaque)
                    || (non_opt_field_t->tag == EnumType && Match(non_opt_field_t, EnumType)->opaque)) {
                    file_t *file = NULL;
                    const char *start = NULL, *end = NULL;
                    if (field_ast->type) {
                        file = field_ast->type->file, start = field_ast->type->start, end = field_ast->type->end;
                    } else if (field_ast->value) {
                        file = field_ast->value->file, start = field_ast->value->start, end = field_ast->value->end;
                    }
                    if (non_opt_field_t == type)
                        compiler_err(file, start, end, "This is a recursive enum that would be infinitely large. Maybe you meant to use an optional '@", type_to_str(type), "?' pointer instead?");
                    else if (non_opt_field_t->tag == StructType && Match(non_opt_field_t, StructType)->external)
                        compiler_err(file, start, end, "This is an opaque externally defined struct.\n"
                                     "I can't use it as a member without knowing what its fields are.\n"
                                     "Either specify its fields and remove the `opaque` qualifier, or use something like a @", type_to_str(non_opt_field_t), " pointer.");
                    else
                        compiler_err(file, start, end, "I'm still in the process of defining the fields of ", type_to_str(field_t),
                                     ", so I don't know how to use it as a member."
                                     "\nTry using a @", type_to_str(field_t), " pointer for this field.");
                }
                fields = new(arg_t, .name=field_ast->name, .type=field_t, .default_val=field_ast->value, .next=fields);
            }
            REVERSE_LIST(fields);
            env_t *member_ns = namespace_env(env, String(def->name, "$", tag_ast->name));
            type_t *tag_type = Type(StructType, .name=String(def->name, "$", tag_ast->name), .fields=fields, .env=member_ns);
            tags = new(tag_t, .name=tag_ast->name, .tag_value=(next_tag++), .type=tag_type, .next=tags);
        }
        REVERSE_LIST(tags);
        type->__data.EnumType.tags = tags;
        type->__data.EnumType.opaque = false;

        for (tag_t *tag = tags; tag; tag = tag->next) {
            if (Match(tag->type, StructType)->fields) { // Constructor:
                type_t *constructor_t = Type(FunctionType, .args=Match(tag->type, StructType)->fields, .ret=type);
                set_binding(ns_env, tag->name, constructor_t, CORD_all(namespace_prefix(env, env->namespace), def->name, "$tagged$", tag->name));
            } else { // Empty singleton value:
                CORD code = CORD_all("((", namespace_prefix(env, env->namespace), def->name, "$$type){", namespace_prefix(env, env->namespace), def->name, "$tag$", tag->name, "})");
                set_binding(ns_env, tag->name, type, code);
            }
            Table$str_set(env->types, String(def->name, "$", tag->name), tag->type);
        }
        
        for (ast_list_t *stmt = def->namespace ? Match(def->namespace, Block)->statements : NULL; stmt; stmt = stmt->next) {
            bind_statement(ns_env, stmt->ast);
        }
        break;
    }
    case LangDef: {
        DeclareMatch(def, statement, LangDef);
        env_t *ns_env = namespace_env(env, def->name);
        type_t *type = Type(TextType, .lang=def->name, .env=ns_env);
        Table$str_set(env->types, def->name, type);

        set_binding(ns_env, "from_text", NewFunctionType(type, {.name="text", .type=TEXT_TYPE}),
                    CORD_all("(", namespace_prefix(env, env->namespace), def->name, "$$type)"));

        for (ast_list_t *stmt = def->namespace ? Match(def->namespace, Block)->statements : NULL; stmt; stmt = stmt->next)
            bind_statement(ns_env, stmt->ast);
        break;
    }
    case Extend: {
        DeclareMatch(extend, statement, Extend);
        env_t *ns_env = namespace_env(env, extend->name);
        env_t *extended = new(env_t);
        *extended = *ns_env;
        extended->locals = new(Table_t, .fallback=env->locals);
        extended->namespace_bindings = new(Table_t, .fallback=env->namespace_bindings);
        extended->libname = env->libname;
        for (ast_list_t *stmt = extend->body ? Match(extend->body, Block)->statements : NULL; stmt; stmt = stmt->next)
            bind_statement(extended, stmt->ast);
        List_t new_bindings = extended->locals->entries;
        for (int64_t i = 0; i < new_bindings.length; i++) {
            struct { const char *name; binding_t *binding; } *entry = new_bindings.data + i*new_bindings.stride;
            binding_t *clobbered = Table$str_get(*ns_env->locals, entry->name);
            if (clobbered && !type_eq(clobbered->type, entry->binding->type))
                code_err(statement, "This `extend` block overwrites the binding for ", quoted(entry->name),
                         " in the original namespace (with type ", type_to_str(clobbered->type), ") with a new binding with type ",
                         type_to_str(entry->binding->type));
            Table$str_set(ns_env->locals, entry->name, entry->binding);
        }
        break;
    }
    case Use: {
        env_t *module_env = load_module(env, statement);
        if (!module_env) break;
        for (Table_t *bindings = module_env->locals; bindings != module_env->globals; bindings = bindings->fallback) {
            List_t entries = bindings->entries;
            for (int64_t i = 0; i < entries.length; i++) {
                struct { const char *name; binding_t *binding; } *entry = entries.data + entries.stride*i;
                if (entry->name[0] == '_' || streq(entry->name, "main"))
                    continue;
                binding_t *b = Table$str_get(*env->locals, entry->name);
                if (!b)
                    Table$str_set(env->locals, entry->name, entry->binding);
                else if (b != entry->binding)
                    code_err(statement, "This module imports a symbol called '", entry->name, "', which would clobber another variable");
            }
        }
        for (int64_t i = 0; i < module_env->types->entries.length; i++) {
            struct { const char *name; type_t *type; } *entry = module_env->types->entries.data + module_env->types->entries.stride*i;
            if (entry->name[0] == '_')
                continue;
            if (Table$str_get(*env->types, entry->name))
                continue;

            Table$str_set(env->types, entry->name, entry->type);
        }

        ast_t *var = Match(statement, Use)->var;
        if (var) {
            type_t *type = get_type(env, statement);
            assert(type);
            set_binding(env, Match(var, Var)->name, type, CORD_EMPTY);
        }
        break;
    }
    case Extern: {
        DeclareMatch(ext, statement, Extern);
        type_t *t = parse_type_ast(env, ext->type);
        if (t->tag == ClosureType)
            t = Match(t, ClosureType)->fn;
        set_binding(env, ext->name, t, ext->name);
        break;
    }
    default: break;
    }
}

type_t *get_function_def_type(env_t *env, ast_t *ast)
{
    arg_ast_t *arg_asts = ast->tag == FunctionDef ? Match(ast, FunctionDef)->args : Match(ast, ConvertDef)->args;
    type_ast_t *ret_type = ast->tag == FunctionDef ? Match(ast, FunctionDef)->ret_type : Match(ast, ConvertDef)->ret_type;
    arg_t *args = NULL;
    env_t *scope = fresh_scope(env);
    for (arg_ast_t *arg = arg_asts; arg; arg = arg->next) {
        type_t *t = arg->type ? parse_type_ast(env, arg->type) : get_type(env, arg->value);
        args = new(arg_t, .name=arg->name, .type=t, .default_val=arg->value, .next=args);
        set_binding(scope, arg->name, t, CORD_EMPTY);
    }
    REVERSE_LIST(args);

    type_t *ret = ret_type ? parse_type_ast(scope, ret_type) : Type(VoidType);
    if (has_stack_memory(ret))
        code_err(ast, "Functions can't return stack references because the reference may outlive its stack frame.");
    return Type(FunctionType, .args=args, .ret=ret);
}

type_t *get_method_type(env_t *env, ast_t *self, const char *name)
{
    binding_t *b = get_namespace_binding(env, self, name);
    if (!b || !b->type)
        code_err(self, "No such method: ", type_to_str(get_type(env, self)), ".", name, "(...)");
    return b->type;
}

env_t *when_clause_scope(env_t *env, type_t *subject_t, when_clause_t *clause)
{
    if (clause->pattern->tag == Var || subject_t->tag != EnumType)
        return env;

    if (clause->pattern->tag != FunctionCall || Match(clause->pattern, FunctionCall)->fn->tag != Var)
        code_err(clause->pattern, "I only support variables and constructors for pattern matching ", type_to_str(subject_t), " types in a 'when' block"); 

    DeclareMatch(fn, clause->pattern, FunctionCall);
    const char *tag_name = Match(fn->fn, Var)->name;
    type_t *tag_type = NULL;
    tag_t * const tags = Match(subject_t, EnumType)->tags;
    for (tag_t *tag = tags; tag; tag = tag->next) {
        if (streq(tag->name, tag_name)) {
            tag_type = tag->type;
            break;
        }
    }

    if (!tag_type)
        code_err(clause->pattern, "There is no tag ", quoted(tag_name), " for the type ", type_to_str(subject_t));

    if (!fn->args)
        return env;

    env_t *scope = fresh_scope(env);
    DeclareMatch(tag_struct, tag_type, StructType);
    if (fn->args && !fn->args->next && tag_struct->fields && tag_struct->fields->next) {
        if (fn->args->value->tag != Var)
            code_err(fn->args->value, "I expected a variable here");
        set_binding(scope, Match(fn->args->value, Var)->name, tag_type, CORD_EMPTY);
        return scope;
    }

    arg_t *field = tag_struct->fields;
    for (arg_ast_t *var = fn->args; var || field; var = var ? var->next : var) {
        if (!var)
            code_err(clause->pattern, "The field ", type_to_str(subject_t), ".", tag_name, ".", field->name, " wasn't accounted for");
        if (!field)
            code_err(var->value, "This is one more field than ", type_to_str(subject_t), " has");
        if (var->value->tag != Var)
            code_err(var->value, "I expected this to be a plain variable so I could bind it to a value");
        if (!streq(Match(var->value, Var)->name, "_"))
            set_binding(scope, Match(var->value, Var)->name, field->type, CORD_EMPTY);
        field = field->next;
    }
    return scope;
}

type_t *get_clause_type(env_t *env, type_t *subject_t, when_clause_t *clause)
{
    env_t *scope = when_clause_scope(env, subject_t, clause);
    return get_type(scope, clause->body);
}

type_t *get_type(env_t *env, ast_t *ast)
{
    if (!ast) return NULL;
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wswitch-default"
#endif
    switch (ast->tag) {
    case None: {
        return Type(OptionalType, .type=NULL);
    }
    case Bool: {
        return Type(BoolType);
    }
    case Int: {
        return Type(BigIntType);
    }
    case Num: {
        return Type(NumType, .bits=TYPE_NBITS64);
    }
    case HeapAllocate: {
        type_t *pointed = get_type(env, Match(ast, HeapAllocate)->value);
        if (has_stack_memory(pointed))
            code_err(ast, "Stack references cannot be moved to the heap because they may outlive the stack frame they were created in.");
        return Type(PointerType, .pointed=pointed);
    }
    case StackReference: {
        // Supported:
        //   &variable
        //   &struct_variable.field.(...)
        //   &struct_ptr.field.(...)
        //   &[10, 20, 30]; &{key:value}; &{10, 20, 30}
        //   &Foo(...)
        //   &(expression)
        // Not supported:
        //   &ptr[]
        //   &list[index]
        //   &table[key]
        //   &(expression).field
        //   &optional_struct_ptr.field
        ast_t *value = Match(ast, StackReference)->value;
        switch (value->tag) {
        case FieldAccess: {
            ast_t *base = value;
            while (base->tag == FieldAccess)
                base = Match(base, FieldAccess)->fielded;

            type_t *ref_type = get_type(env, value);
            type_t *base_type = get_type(env, base);
            if (base_type->tag == OptionalType) {
                code_err(base, "This value might be null, so it can't be safely dereferenced");
            } else if (base_type->tag == PointerType) {
                DeclareMatch(ptr, base_type, PointerType);
                return Type(PointerType, .pointed=ref_type, .is_stack=ptr->is_stack);
            } else if (base->tag == Var) {
                return Type(PointerType, .pointed=ref_type, .is_stack=true);
            }
            code_err(ast, "'&' stack references can only be used on the fields of pointers and local variables");
        }
        case Index:
            code_err(ast, "'&' stack references are not supported for list or table indexing");
        default:
            return Type(PointerType, .pointed=get_type(env, value), .is_stack=true);
        }
    }
    case Optional: {
        ast_t *value = Match(ast, Optional)->value;
        type_t *t = get_type(env, value);
        if (t->tag == OptionalType)
            code_err(ast, "This value is already optional, it can't be converted to optional");
        return Type(OptionalType, .type=t);
    }
    case NonOptional: {
        ast_t *value = Match(ast, NonOptional)->value;
        type_t *t = get_type(env, value);
        if (t->tag != OptionalType)
            code_err(value, "This value is not optional. Only optional values can use the '!' operator.");
        return Match(t, OptionalType)->type;
    }
    case TextLiteral: return TEXT_TYPE;
    case Path: return PATH_TYPE;
    case TextJoin: {
        const char *lang = Match(ast, TextJoin)->lang;
        if (lang) {
            binding_t *b = get_binding(env, lang);
            if (!b || b->type->tag != TypeInfoType || Match(b->type, TypeInfoType)->type->tag != TextType)
                code_err(ast, "There is no text language called '", lang, "'");
            return Match(b->type, TypeInfoType)->type;
        } else {
            return TEXT_TYPE;
        }
    }
    case Var: {
        DeclareMatch(var, ast, Var);
        binding_t *b = get_binding(env, var->name);
        if (b) return b->type;
        code_err(ast, "I don't know what ", quoted(var->name), " refers to");
    }
    case List: {
        DeclareMatch(list, ast, List);
        type_t *item_type = NULL;
        for (ast_list_t *item = list->items; item; item = item->next) {
            ast_t *item_ast = item->ast;
            env_t *scope = env;
            while (item_ast->tag == Comprehension) {
                DeclareMatch(comp, item_ast, Comprehension);
                scope = for_scope(
                    scope, FakeAST(For, .iter=comp->iter, .vars=comp->vars));
                item_ast = comp->expr;
            }
            type_t *t2 = get_type(scope, item_ast);
            type_t *merged = item_type ? type_or_type(item_type, t2) : t2;
            if (!merged)
                code_err(item->ast,
                         "This list item has type ", type_to_str(t2),
                         ", which is different from earlier list items which have type ", type_to_str(item_type));
            item_type = merged;
        }

        if (item_type && has_stack_memory(item_type))
            code_err(ast, "Lists cannot hold stack references, because the list may outlive the stack frame the reference was created in.");

        return Type(ListType, .item_type=item_type);
    }
    case Set: {
        DeclareMatch(set, ast, Set);
        type_t *item_type = NULL;
        for (ast_list_t *item = set->items; item; item = item->next) {
            ast_t *item_ast = item->ast;
            env_t *scope = env;
            while (item_ast->tag == Comprehension) {
                DeclareMatch(comp, item_ast, Comprehension);
                scope = for_scope(
                    scope, FakeAST(For, .iter=comp->iter, .vars=comp->vars));
                item_ast = comp->expr;
            }

            type_t *this_item_type = get_type(scope, item_ast);
            type_t *item_merged = type_or_type(item_type, this_item_type);
            if (!item_merged)
                code_err(item_ast,
                         "This set item has type ", type_to_str(this_item_type),
                         ", which is different from earlier set items which have type ", type_to_str(item_type));
            item_type = item_merged;
        }

        if (item_type && has_stack_memory(item_type))
            code_err(ast, "Sets cannot hold stack references because the set may outlive the reference's stack frame.");

        return Type(SetType, .item_type=item_type);
    }
    case Table: {
        DeclareMatch(table, ast, Table);
        type_t *key_type = NULL, *value_type = NULL;
        for (ast_list_t *entry = table->entries; entry; entry = entry->next) {
            ast_t *entry_ast = entry->ast;
            env_t *scope = env;
            while (entry_ast->tag == Comprehension) {
                DeclareMatch(comp, entry_ast, Comprehension);
                scope = for_scope(
                    scope, FakeAST(For, .iter=comp->iter, .vars=comp->vars));
                entry_ast = comp->expr;
            }

            DeclareMatch(e, entry_ast, TableEntry);
            type_t *key_t = get_type(scope, e->key);
            type_t *value_t = get_type(scope, e->value);

            type_t *key_merged = key_type ? type_or_type(key_type, key_t) : key_t;
            if (!key_merged)
                code_err(entry->ast,
                         "This table entry has type ", type_to_str(key_t),
                         ", which is different from earlier table entries which have type ", type_to_str(key_type));
            key_type = key_merged;

            type_t *val_merged = value_type ? type_or_type(value_type, value_t) : value_t;
            if (!val_merged)
                code_err(entry->ast,
                         "This table entry has type ", type_to_str(value_t),
                         ", which is different from earlier table entries which have type ", type_to_str(value_type));
            value_type = val_merged;
        }

        if ((key_type && has_stack_memory(key_type)) || (value_type && has_stack_memory(value_type)))
            code_err(ast, "Tables cannot hold stack references because the table may outlive the reference's stack frame.");

        return Type(TableType, .key_type=key_type, .value_type=value_type, .default_value=table->default_value, .env=env);
    }
    case TableEntry: {
        code_err(ast, "Table entries should not be typechecked directly");
    }
    case Comprehension: {
        DeclareMatch(comp, ast, Comprehension);
        env_t *scope = for_scope(env, FakeAST(For, .iter=comp->iter, .vars=comp->vars));
        if (comp->expr->tag == Comprehension) {
            return get_type(scope, comp->expr);
        } else if (comp->expr->tag == TableEntry) {
            DeclareMatch(e, comp->expr, TableEntry);
            return Type(TableType, .key_type=get_type(scope, e->key), .value_type=get_type(scope, e->value), .env=env);
        } else {
            return Type(ListType, .item_type=get_type(scope, comp->expr));
        }
    }
    case FieldAccess: {
        DeclareMatch(access, ast, FieldAccess);
        type_t *fielded_t = get_type(env, access->fielded);
        if (fielded_t->tag == ModuleType) {
            const char *name = Match(fielded_t, ModuleType)->name;
            env_t *module_env = Table$str_get(*env->imports, name);
            if (!module_env) code_err(access->fielded, "I couldn't find the environment for the module ", name);
            return get_type(module_env, WrapAST(ast, Var, access->field));
        } else if (fielded_t->tag == TypeInfoType) {
            DeclareMatch(info, fielded_t, TypeInfoType);
            assert(info->env);
            binding_t *b = get_binding(info->env, access->field);
            if (!b) code_err(ast, "I couldn't find the field '", access->field, "' on this type");
            return b->type;
        }
        type_t *field_t = get_field_type(fielded_t, access->field);
        if (!field_t)
            code_err(ast, type_to_str(fielded_t), " objects don't have a field called '", access->field, "'");
        return field_t;
    }
    case Index: {
        DeclareMatch(indexing, ast, Index);
        type_t *indexed_t = get_type(env, indexing->indexed);
        if (indexed_t->tag == OptionalType && !indexing->index)
            code_err(ast, "You're attempting to dereference a value whose type indicates it could be null");

        if (indexed_t->tag == PointerType && !indexing->index)
            return Match(indexed_t, PointerType)->pointed;

        type_t *value_t = value_type(indexed_t);
        if (value_t->tag == ListType) {
            if (!indexing->index) return indexed_t;
            type_t *index_t = get_type(env, indexing->index);
            if (index_t->tag == IntType || index_t->tag == BigIntType || index_t->tag == ByteType)
                return Match(value_t, ListType)->item_type;
            code_err(indexing->index, "I only know how to index lists using integers, not ", type_to_str(index_t));
        } else if (value_t->tag == TableType) {
            DeclareMatch(table_type, value_t, TableType);
            if (table_type->default_value)
                return table_type->value_type;
            return Type(OptionalType, table_type->value_type);
        } else if (value_t->tag == TextType) {
            return value_t;
        } else {
            code_err(ast, "I don't know how to index ", type_to_str(indexed_t), " values");
        }
    }
    case FunctionCall: {
        DeclareMatch(call, ast, FunctionCall);
        type_t *fn_type_t = get_type(env, call->fn);
        if (!fn_type_t)
            code_err(call->fn, "I couldn't find this function");

        if (fn_type_t->tag == TypeInfoType) {
            type_t *t = Match(fn_type_t, TypeInfoType)->type;

            binding_t *constructor = get_constructor(env, t, call->args);
            if (constructor)
                return t;
            else if (t->tag == StructType || t->tag == IntType || t->tag == BigIntType || t->tag == NumType
                || t->tag == ByteType || t->tag == TextType || t->tag == CStringType)
                return t; // Constructor
            code_err(call->fn, "This is not a type that has a constructor");
        }
        if (fn_type_t->tag == ClosureType)
            fn_type_t = Match(fn_type_t, ClosureType)->fn;
        if (fn_type_t->tag != FunctionType)
            code_err(call->fn, "This isn't a function, it's a ", type_to_str(fn_type_t));
        DeclareMatch(fn_type, fn_type_t, FunctionType);
        return fn_type->ret;
    }
    case MethodCall: {
        DeclareMatch(call, ast, MethodCall);

        if (streq(call->name, "serialized")) // Data serialization
            return Type(ListType, Type(ByteType));

        type_t *self_value_t = get_type(env, call->self);
        if (!self_value_t) code_err(call->self, "Couldn't get the type of this value");
        self_value_t = value_type(self_value_t);

        if (self_value_t->tag == TypeInfoType || self_value_t->tag == ModuleType) {
            return get_type(env, WrapAST(ast, FunctionCall, .fn=WrapAST(call->self, FieldAccess, .fielded=call->self, .field=call->name),
                                        .args=call->args));
        }

        switch (self_value_t->tag) {
        case ListType: {
            type_t *item_type = Match(self_value_t, ListType)->item_type;
            if (streq(call->name, "binary_search")) return INT_TYPE;
            else if (streq(call->name, "by")) return self_value_t;
            else if (streq(call->name, "clear")) return Type(VoidType);
            else if (streq(call->name, "counts")) return Type(TableType, .key_type=item_type, .value_type=INT_TYPE);
            else if (streq(call->name, "find")) return Type(OptionalType, .type=INT_TYPE);
            else if (streq(call->name, "first")) return Type(OptionalType, .type=INT_TYPE);
            else if (streq(call->name, "from")) return self_value_t;
            else if (streq(call->name, "has")) return Type(BoolType);
            else if (streq(call->name, "heap_pop")) return Type(OptionalType, .type=item_type);
            else if (streq(call->name, "heap_push")) return Type(VoidType);
            else if (streq(call->name, "heapify")) return Type(VoidType);
            else if (streq(call->name, "insert")) return Type(VoidType);
            else if (streq(call->name, "insert_all")) return Type(VoidType);
            else if (streq(call->name, "pop")) return Type(OptionalType, .type=item_type);
            else if (streq(call->name, "random")) return item_type;
            else if (streq(call->name, "remove_at")) return Type(VoidType);
            else if (streq(call->name, "remove_item")) return Type(VoidType);
            else if (streq(call->name, "reversed")) return self_value_t;
            else if (streq(call->name, "sample")) return self_value_t;
            else if (streq(call->name, "shuffle")) return Type(VoidType);
            else if (streq(call->name, "shuffled")) return self_value_t;
            else if (streq(call->name, "slice")) return self_value_t;
            else if (streq(call->name, "sort")) return Type(VoidType);
            else if (streq(call->name, "sorted")) return self_value_t;
            else if (streq(call->name, "to")) return self_value_t;
            else if (streq(call->name, "unique")) return Type(SetType, .item_type=item_type);
            else code_err(ast, "There is no '", call->name, "' method for lists");
        }
        case SetType: {
            if (streq(call->name, "add")) return Type(VoidType);
            else if (streq(call->name, "add_all")) return Type(VoidType);
            else if (streq(call->name, "clear")) return Type(VoidType);
            else if (streq(call->name, "has")) return Type(BoolType);
            else if (streq(call->name, "is_subset_of")) return Type(BoolType);
            else if (streq(call->name, "is_superset_of")) return Type(BoolType);
            else if (streq(call->name, "overlap")) return self_value_t;
            else if (streq(call->name, "remove")) return Type(VoidType);
            else if (streq(call->name, "remove_all")) return Type(VoidType);
            else if (streq(call->name, "with")) return self_value_t;
            else if (streq(call->name, "without")) return self_value_t;
            else code_err(ast, "There is no '", call->name, "' method for sets");
        }
        case TableType: {
            DeclareMatch(table, self_value_t, TableType);
            if (streq(call->name, "clear")) return Type(VoidType);
            else if (streq(call->name, "get")) return Type(OptionalType, .type=table->value_type);
            else if (streq(call->name, "get_or_set")) return table->value_type;
            else if (streq(call->name, "has")) return Type(BoolType);
            else if (streq(call->name, "remove")) return Type(VoidType);
            else if (streq(call->name, "set")) return Type(VoidType);
            else if (streq(call->name, "sorted")) return self_value_t;
            code_err(ast, "There is no '", call->name, "' method for ", type_to_str(self_value_t), " tables");
        }
        default: {
            type_t *field_type = get_field_type(self_value_t, call->name);
            if (field_type && field_type->tag == ClosureType)
                field_type = Match(field_type, ClosureType)->fn;
            if (field_type && field_type->tag == FunctionType)
                return Match(field_type, FunctionType)->ret;
            type_t *fn_type_t = get_method_type(env, call->self, call->name);
            if (!fn_type_t)
                code_err(ast, "No such method!");
            if (fn_type_t->tag != FunctionType)
                code_err(ast, "This isn't a method, it's a ", type_to_str(fn_type_t));
            DeclareMatch(fn_type, fn_type_t, FunctionType);
            return fn_type->ret;
        }
        }
    }
    case Block: {
        DeclareMatch(block, ast, Block);
        ast_list_t *last = block->statements;
        if (!last)
            return Type(VoidType);
        while (last->next)
            last = last->next;

        // Early out if the type is knowable without any context from the block:
        switch (last->ast->tag) {
        case UPDATE_CASES: case Assign: case Declare: case FunctionDef: case ConvertDef: case StructDef: case EnumDef: case LangDef: case Extend:
            return Type(VoidType);
        default: break;
        }

        env_t *block_env = fresh_scope(env);
        for (ast_list_t *stmt = block->statements; stmt; stmt = stmt->next) {
            prebind_statement(block_env, stmt->ast);
        }
        for (ast_list_t *stmt = block->statements; stmt; stmt = stmt->next) {
            bind_statement(block_env, stmt->ast);
            if (stmt->next) { // Check for unreachable code:
                if (stmt->ast->tag == Return)
                    code_err(stmt->ast, "This statement will always return, so the rest of the code in this block is unreachable!");
                type_t *statement_type = get_type(block_env, stmt->ast);
                if (statement_type && statement_type->tag == AbortType && stmt->next)
                    code_err(stmt->ast, "This statement will always abort, so the rest of the code in this block is unreachable!");
            }
        }
        return get_type(block_env, last->ast);
    }
    case Extern: {
        return parse_type_ast(env, Match(ast, Extern)->type);
    }
    case Declare: case Assign: case UPDATE_CASES: case DocTest: {
        return Type(VoidType);
    }
    case Use: {
        switch (Match(ast, Use)->what) {
        case USE_LOCAL: {
            Path_t source_path = Path$from_str(ast->file->filename);
            Path_t source_dir = Path$parent(source_path);
            Path_t used_path = Path$resolved(Path$from_str(Match(ast, Use)->path), source_dir);
            return Type(ModuleType, Path$as_c_string(used_path));
        }
        default:
            return Type(ModuleType, Match(ast, Use)->path);
        }
    }
    case Return: {
        ast_t *val = Match(ast, Return)->value;
        if (env->fn_ret)
            env = with_enum_scope(env, env->fn_ret);
        return Type(ReturnType, .ret=(val ? get_type(env, val) : Type(VoidType)));
    }
    case Stop: case Skip: {
        return Type(AbortType);
    }
    case Pass: case Defer: return Type(VoidType);
    case Negative: {
        ast_t *value = Match(ast, Negative)->value;
        type_t *t = get_type(env, value);
        if (t->tag == IntType || t->tag == NumType)
            return t;

        binding_t *b = get_namespace_binding(env, value, "negative");
        if (b && b->type->tag == FunctionType) {
            DeclareMatch(fn, b->type, FunctionType);
            if (fn->args && type_eq(t, get_arg_type(env, fn->args)) && type_eq(t, fn->ret))
                return t;
        }

        code_err(ast, "I don't know how to get the negative value of type ", type_to_str(t));
    }
    case Not: {
        type_t *t = get_type(env, Match(ast, Not)->value);
        if (t->tag == IntType || t->tag == NumType || t->tag == BoolType)
            return t;
        if (t->tag == OptionalType)
            return Type(BoolType);

        ast_t *value = Match(ast, Not)->value;
        binding_t *b = get_namespace_binding(env, value, "negated");
        if (b && b->type->tag == FunctionType) {
            DeclareMatch(fn, b->type, FunctionType);
            if (fn->args && type_eq(t, get_arg_type(env, fn->args)) && type_eq(t, fn->ret))
                return t;
        }
        code_err(ast, "I only know how to get 'not' of boolean, numeric, and optional pointer types, not ", type_to_str(t));
    }
    case Or: {
        binary_operands_t binop = BINARY_OPERANDS(ast);
        type_t *lhs_t = get_type(env, binop.lhs);
        type_t *rhs_t = get_type(env, binop.rhs);

        if (binop.lhs->tag == Int && is_int_type(rhs_t))
            return rhs_t;
        else if (binop.rhs->tag == Int && is_int_type(lhs_t))
            return lhs_t;

        // `opt? or (x == y)` / `(x == y) or opt?` is a boolean conditional:
        if ((lhs_t->tag == OptionalType && rhs_t->tag == BoolType)
            || (lhs_t->tag == BoolType && rhs_t->tag == OptionalType)) {
            return Type(BoolType);
        }

        if (type_eq(lhs_t, rhs_t)) {
            binding_t *b = get_metamethod_binding(env, ast->tag, binop.lhs, binop.rhs, lhs_t);
            if (b) return lhs_t;
        }

        if (lhs_t->tag == OptionalType) {
            if (rhs_t->tag == OptionalType) {
                type_t *result = most_complete_type(lhs_t, rhs_t);
                if (result == NULL)
                    code_err(ast, "I could not determine the type of ", type_to_str(lhs_t), " `or` ", type_to_str(rhs_t));
                return result;
            } else if (rhs_t->tag == AbortType || rhs_t->tag == ReturnType) {
                return Match(lhs_t, OptionalType)->type;
            }
            type_t *non_opt = Match(lhs_t, OptionalType)->type;
            non_opt = most_complete_type(non_opt, rhs_t);
            if (non_opt != NULL)
                return non_opt;
        } else if ((is_numeric_type(lhs_t) || lhs_t->tag == BoolType)
                   && (is_numeric_type(rhs_t) || rhs_t->tag == BoolType)
                   && lhs_t->tag != NumType && rhs_t->tag != NumType) {
            if (can_promote(rhs_t, lhs_t))
                return lhs_t;
            else if (can_promote(lhs_t, rhs_t))
                return rhs_t;
        } else if (lhs_t->tag == SetType && rhs_t->tag == SetType && type_eq(lhs_t, rhs_t)) {
            return lhs_t;
        }
        code_err(ast, "I couldn't figure out how to do `or` between ", type_to_str(lhs_t), " and ", type_to_str(rhs_t));
    }
    case And: {
        binary_operands_t binop = BINARY_OPERANDS(ast);
        type_t *lhs_t = get_type(env, binop.lhs);
        type_t *rhs_t = get_type(env, binop.rhs);

        if (binop.lhs->tag == Int && is_int_type(rhs_t))
            return rhs_t;
        else if (binop.rhs->tag == Int && is_int_type(lhs_t))
            return lhs_t;

        // `and` between optionals/bools is a boolean expression like `if opt? and opt?:` or `if x > 0 and opt?:`
        if ((lhs_t->tag == OptionalType || lhs_t->tag == BoolType)
            && (rhs_t->tag == OptionalType || rhs_t->tag == BoolType)) {
            return Type(BoolType);
        }

        if (type_eq(lhs_t, rhs_t)) {
            binding_t *b = get_metamethod_binding(env, ast->tag, binop.lhs, binop.rhs, lhs_t);
            if (b) return lhs_t;
        }

        // Bitwise AND:
        if ((is_numeric_type(lhs_t) || lhs_t->tag == BoolType)
            && (is_numeric_type(rhs_t) || rhs_t->tag == BoolType)
            && lhs_t->tag != NumType && rhs_t->tag != NumType) {
            if (can_promote(rhs_t, lhs_t))
                return lhs_t;
            else if (can_promote(lhs_t, rhs_t))
                return rhs_t;
        } else if (lhs_t->tag == SetType && rhs_t->tag == SetType && type_eq(lhs_t, rhs_t)) {
            return lhs_t;
        }
        code_err(ast, "I couldn't figure out how to do `and` between ", type_to_str(lhs_t), " and ", type_to_str(rhs_t));
    }
    case Xor: {
        binary_operands_t binop = BINARY_OPERANDS(ast);
        type_t *lhs_t = get_type(env, binop.lhs);
        type_t *rhs_t = get_type(env, binop.rhs);

        if (binop.lhs->tag == Int && is_int_type(rhs_t))
            return rhs_t;
        else if (binop.rhs->tag == Int && is_int_type(lhs_t))
            return lhs_t;

        // `xor` between optionals/bools is a boolean expression like `if opt? xor opt?:` or `if x > 0 xor opt?:`
        if ((lhs_t->tag == OptionalType || lhs_t->tag == BoolType)
            && (rhs_t->tag == OptionalType || rhs_t->tag == BoolType)) {
            return Type(BoolType);
        }

        if (type_eq(lhs_t, rhs_t)) {
            binding_t *b = get_metamethod_binding(env, ast->tag, binop.lhs, binop.rhs, lhs_t);
            if (b) return lhs_t;
        }

        // Bitwise XOR:
        if ((is_numeric_type(lhs_t) || lhs_t->tag == BoolType)
            && (is_numeric_type(rhs_t) || rhs_t->tag == BoolType)
            && lhs_t->tag != NumType && rhs_t->tag != NumType) {
            if (can_promote(rhs_t, lhs_t))
                return lhs_t;
            else if (can_promote(lhs_t, rhs_t))
                return rhs_t;
        } else if (lhs_t->tag == SetType && rhs_t->tag == SetType && type_eq(lhs_t, rhs_t)) {
            return lhs_t;
        }
        code_err(ast, "I couldn't figure out how to do `xor` between ", type_to_str(lhs_t), " and ", type_to_str(rhs_t));
    }
    case Compare:
    case Equals: case NotEquals: case LessThan: case LessThanOrEquals: case GreaterThan: case GreaterThanOrEquals: {
        binary_operands_t binop = BINARY_OPERANDS(ast);
        type_t *lhs_t = get_type(env, binop.lhs);
        type_t *rhs_t = get_type(env, binop.rhs);

        if ((binop.lhs->tag == Int && is_numeric_type(rhs_t))
            || (binop.rhs->tag == Int && is_numeric_type(lhs_t))
            || can_promote(rhs_t, lhs_t)
            || can_promote(lhs_t, rhs_t))
            return ast->tag == Compare ? Type(IntType, .bits=TYPE_IBITS32) : Type(BoolType);

        code_err(ast, "I don't know how to compare ", type_to_str(lhs_t), " and ", type_to_str(rhs_t));
    }
    case Power: case Multiply: case Divide: case Mod: case Mod1: case Plus: case Minus: case LeftShift:
    case UnsignedLeftShift: case RightShift: case UnsignedRightShift: {
        binary_operands_t binop = BINARY_OPERANDS(ast);
        type_t *lhs_t = get_type(env, binop.lhs);
        type_t *rhs_t = get_type(env, binop.rhs);

        if (ast->tag == LeftShift || ast->tag == UnsignedLeftShift || ast->tag == RightShift || ast->tag == UnsignedRightShift) {
            if (!is_int_type(rhs_t))
                code_err(binop.rhs, "I only know how to do bit shifting by integer amounts, not ", type_to_str(rhs_t));
        }

        if (is_numeric_type(lhs_t) && binop.rhs->tag == Int) {
            return lhs_t;
        } else if (is_numeric_type(rhs_t) && binop.lhs->tag == Int) {
            return rhs_t;
        } else {
            switch (compare_precision(lhs_t, rhs_t)) {
            case NUM_PRECISION_LESS: return rhs_t;
            case NUM_PRECISION_MORE: return lhs_t;
            case NUM_PRECISION_EQUAL: return lhs_t;
            default: {
                if (can_compile_to_type(env, binop.rhs, lhs_t)) {
                    return lhs_t;
                } else if (can_compile_to_type(env, binop.lhs, rhs_t)) {
                    return rhs_t;
                }
                break;
            }
            }
        }

        if (ast->tag == Multiply && is_numeric_type(lhs_t)) {
            binding_t *b = get_namespace_binding(env, binop.rhs, "scaled_by");
            if (b && b->type->tag == FunctionType) {
                DeclareMatch(fn, b->type, FunctionType);
                if (type_eq(fn->ret, rhs_t)) {
                    arg_ast_t *args = new(arg_ast_t, .value=binop.rhs, .next=new(arg_ast_t, .value=binop.lhs));
                    if (is_valid_call(env, fn->args, args, true))
                        return rhs_t;
                }
            }
        } else if (ast->tag == Multiply && is_numeric_type(rhs_t)) {
            binding_t *b = get_namespace_binding(env, binop.lhs, "scaled_by");
            if (b && b->type->tag == FunctionType) {
                DeclareMatch(fn, b->type, FunctionType);
                if (type_eq(fn->ret, lhs_t)) {
                    arg_ast_t *args = new(arg_ast_t, .value=binop.lhs, .next=new(arg_ast_t, .value=binop.rhs));
                    if (is_valid_call(env, fn->args, args, true))
                        return lhs_t;
                }
            }
        } else if ((ast->tag == Divide || ast->tag == Mod || ast->tag == Mod1) && is_numeric_type(rhs_t)) {
            binding_t *b = get_namespace_binding(env, binop.lhs, binop_method_name(ast->tag));
            if (b && b->type->tag == FunctionType) {
                DeclareMatch(fn, b->type, FunctionType);
                if (type_eq(fn->ret, lhs_t)) {
                    arg_ast_t *args = new(arg_ast_t, .value=binop.lhs, .next=new(arg_ast_t, .value=binop.rhs));
                    if (is_valid_call(env, fn->args, args, true))
                        return lhs_t;
                }
            }
        }

        type_t *overall_t = (can_promote(rhs_t, lhs_t) ? lhs_t : (can_promote(lhs_t, rhs_t) ? rhs_t : NULL));
        if (overall_t == NULL)
            code_err(ast, "I don't know how to do math operations between ", type_to_str(lhs_t), " and ", type_to_str(rhs_t));

        binding_t *b = get_metamethod_binding(env, ast->tag, binop.lhs, binop.rhs, overall_t);
        if (b) return overall_t;

        if (is_numeric_type(lhs_t) && is_numeric_type(rhs_t))
            return overall_t;

        code_err(ast, "I don't know how to do math operations between ", type_to_str(lhs_t), " and ", type_to_str(rhs_t));
    }
    case Concat: {
        binary_operands_t binop = BINARY_OPERANDS(ast);
        type_t *lhs_t = get_type(env, binop.lhs);
        type_t *rhs_t = get_type(env, binop.rhs);

        type_t *overall_t = (can_promote(rhs_t, lhs_t) ? lhs_t : (can_promote(lhs_t, rhs_t) ? rhs_t : NULL));
        if (overall_t == NULL)
            code_err(ast, "I don't know how to do operations between ", type_to_str(lhs_t), " and ", type_to_str(rhs_t));

        binding_t *b = get_metamethod_binding(env, ast->tag, binop.lhs, binop.rhs, overall_t);
        if (b) return overall_t;

        if (overall_t->tag == ListType || overall_t->tag == SetType || overall_t->tag == TextType)
            return overall_t;

        code_err(ast, "I don't know how to do concatenation between ", type_to_str(lhs_t), " and ", type_to_str(rhs_t));
    }

    case Reduction: {
        DeclareMatch(reduction, ast, Reduction);
        type_t *iter_t = get_type(env, reduction->iter);

        if (reduction->op == Equals || reduction->op == NotEquals || reduction->op == LessThan
            || reduction->op == LessThanOrEquals || reduction->op == GreaterThan || reduction->op == GreaterThanOrEquals)
            return Type(OptionalType, .type=Type(BoolType));

        type_t *iterated = get_iterated_type(iter_t);
        if (!iterated)
            code_err(reduction->iter, "I don't know how to do a reduction over ", type_to_str(iter_t), " values");
        if (reduction->key && !(reduction->op == Min || reduction->op == Max)) {
            env_t *item_scope = fresh_scope(env);
            set_binding(item_scope, "$", iterated, CORD_EMPTY);
            iterated = get_type(item_scope, reduction->key);
        }
        return iterated->tag == OptionalType ? iterated : Type(OptionalType, .type=iterated);
    }

    case Min: case Max: {
        // Unsafe! These types *should* have the same fields and this saves a lot of duplicate code:
        ast_t *lhs = ast->__data.Min.lhs, *rhs = ast->__data.Min.rhs;
        // Okay safe again

        type_t *lhs_t = get_type(env, lhs), *rhs_t = get_type(env, rhs);
        type_t *t = type_or_type(lhs_t, rhs_t);
        if (!t)
            code_err(ast, "The two sides of this operation are not compatible: ", type_to_str(lhs_t), " vs ", type_to_str(rhs_t));
        return t;
    }

    case Lambda: {
        DeclareMatch(lambda, ast, Lambda);
        arg_t *args = NULL;
        env_t *scope = fresh_scope(env); // For now, just use closed variables in scope normally
        for (arg_ast_t *arg = lambda->args; arg; arg = arg->next) {
            type_t *t = get_arg_ast_type(env, arg);
            args = new(arg_t, .name=arg->name, .type=t, .next=args);
            set_binding(scope, arg->name, t, CORD_EMPTY);
        }
        REVERSE_LIST(args);

        type_t *ret = get_type(scope, lambda->body);
        if (ret->tag == ReturnType)
            ret = Match(ret, ReturnType)->ret;
        if (ret->tag == AbortType)
            ret = Type(VoidType);

        if (ret->tag == OptionalType && !Match(ret, OptionalType)->type)
            code_err(lambda->body, "This function doesn't return a specific optional type");

        if (lambda->ret_type) {
            type_t *declared = parse_type_ast(env, lambda->ret_type);
            if (can_promote(ret, declared))
                ret = declared;
            else
                code_err(ast, "This function was declared to return a value of type ", type_to_str(declared),
                         ", but actually returns a value of type ", type_to_str(ret));
        }

        if (has_stack_memory(ret))
            code_err(ast, "Functions can't return stack references because the reference may outlive its stack frame.");
        return Type(ClosureType, Type(FunctionType, .args=args, .ret=ret));
    }

    case FunctionDef: case ConvertDef: case StructDef: case EnumDef: case LangDef: case Extend: {
        return Type(VoidType);
    }

    case If: {
        DeclareMatch(if_, ast, If);
        if (!if_->else_body)
            return Type(VoidType);

        env_t *truthy_scope = env;
        env_t *falsey_scope = env;
        if (if_->condition->tag == Declare) {
            DeclareMatch(decl, if_->condition, Declare);
            type_t *condition_type = decl->type ? parse_type_ast(env, decl->type) : get_type(env, decl->value);
            const char *varname = Match(decl->var, Var)->name;
            if (streq(varname, "_"))
                code_err(if_->condition, "To use `if var := ...:`, you must choose a real variable name, not `_`");

            truthy_scope = fresh_scope(env);
            if (condition_type->tag == OptionalType)
                set_binding(truthy_scope, varname,
                            Match(condition_type, OptionalType)->type, CORD_EMPTY);
            else
                set_binding(truthy_scope, varname, condition_type, CORD_EMPTY);
        } else if (if_->condition->tag == Var) {
            type_t *condition_type = get_type(env, if_->condition);
            if (condition_type->tag == OptionalType) {
                truthy_scope = fresh_scope(env);
                const char *varname = Match(if_->condition, Var)->name;
                set_binding(truthy_scope, varname,
                            Match(condition_type, OptionalType)->type, CORD_EMPTY);
            }
        }

        type_t *true_t = get_type(truthy_scope, if_->body);
        type_t *false_t = get_type(falsey_scope, if_->else_body);
        type_t *t_either = type_or_type(true_t, false_t);
        if (!t_either)
            code_err(if_->else_body,
                     "I was expecting this block to have a ", type_to_str(true_t),
                     " value (based on earlier clauses), but it actually has a ", type_to_str(false_t), " value.");
        return t_either;
    }

    case When: {
        DeclareMatch(when, ast, When);
        type_t *subject_t = get_type(env, when->subject);
        if (subject_t->tag != EnumType) {
            type_t *t = NULL;
            for (when_clause_t *clause = when->clauses; clause; clause = clause->next) {
                t = type_or_type(t, get_type(env, clause->body));
            }
            if (when->else_body)
                t = type_or_type(t, get_type(env, when->else_body));
            else if (t && t->tag != OptionalType)
                t = Type(OptionalType, .type=t);
            return t;
        }

        type_t *overall_t = NULL;
        tag_t * const tags = Match(subject_t, EnumType)->tags;

        typedef struct match_s {
            tag_t *tag;
            bool handled;
            struct match_s *next;
        } match_t;
        match_t *matches = NULL;
        for (tag_t *tag = tags; tag; tag = tag->next)
            matches = new(match_t, .tag=tag, .handled=false, .next=matches);

        for (when_clause_t *clause = when->clauses; clause; clause = clause->next) {
            const char *tag_name;
            if (clause->pattern->tag == Var)
                tag_name = Match(clause->pattern, Var)->name;
            else if (clause->pattern->tag == FunctionCall && Match(clause->pattern, FunctionCall)->fn->tag == Var)
                tag_name = Match(Match(clause->pattern, FunctionCall)->fn, Var)->name;
            else
                code_err(clause->pattern, "This is not a valid pattern for a ", type_to_str(subject_t), " enum");

            CORD valid_tags = CORD_EMPTY;
            for (match_t *m = matches; m; m = m->next) {
                if (streq(m->tag->name, tag_name)) {
                    if (m->handled)
                        code_err(clause->pattern, "This tag was already handled earlier");
                    m->handled = true;
                    goto found_matching_tag;
                }
                if (valid_tags) valid_tags = CORD_cat(valid_tags, ", ");
                valid_tags = CORD_cat(valid_tags, m->tag->name);
            }

            code_err(clause->pattern, "There is no tag '", tag_name,
                     "' for the type ", type_to_str(subject_t), " (valid tags: ", CORD_to_char_star(valid_tags), ")");
          found_matching_tag:;
        }

        for (when_clause_t *clause = when->clauses; clause; clause = clause->next) {
            env_t *clause_scope = when_clause_scope(env, subject_t, clause);
            type_t *clause_type = get_type(clause_scope, clause->body);
            type_t *merged = type_or_type(overall_t, clause_type);
            if (!merged)
                code_err(clause->body, "The type of this branch is ", type_to_str(clause_type),
                         ", which conflicts with the earlier branch type of ", type_to_str(overall_t));
            overall_t = merged;
        }

        if (when->else_body) {
            bool any_unhandled = false;
            for (match_t *m = matches; m; m = m->next) {
                if (!m->handled) {
                    any_unhandled = true;
                    break;
                }
            }
            // HACK: `while when ...` is handled by the parser adding an implicit
            // `else: stop`, which has an empty source code span.
            if (!any_unhandled && when->else_body->end > when->else_body->start)
                code_err(when->else_body, "This 'else' block will never run because every tag is handled");

            type_t *else_t = get_type(env, when->else_body);
            type_t *merged = type_or_type(overall_t, else_t);
            if (!merged)
                code_err(when->else_body,
                         "I was expecting this block to have a ", type_to_str(overall_t),
                         " value (based on earlier clauses), but it actually has a ", type_to_str(else_t), " value.");
            return merged;
        } else {
            CORD unhandled = CORD_EMPTY;
            for (match_t *m = matches; m; m = m->next) {
                if (!m->handled)
                    unhandled = unhandled ? CORD_all(unhandled, ", ", m->tag->name) : m->tag->name;
            }
            if (unhandled)
                code_err(ast, "This 'when' statement doesn't handle the tags: ", CORD_to_const_char_star(unhandled));
            return overall_t;
        }
    }

    case While: case Repeat: case For: return Type(VoidType);
    case InlineCCode: {
        DeclareMatch(inline_code, ast, InlineCCode);
        if (inline_code->type)
            return inline_code->type;
        type_ast_t *type_ast = inline_code->type_ast;
        return type_ast ? parse_type_ast(env, type_ast) : Type(VoidType);
    }
    case Unknown: code_err(ast, "I can't figure out the type of: ", ast_to_xml_str(ast));
    case Deserialize: return parse_type_ast(env, Match(ast, Deserialize)->type);
    case ExplicitlyTyped: return Match(ast, ExplicitlyTyped)->type;
    }
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
    code_err(ast, "I can't figure out the type of: ", ast_to_xml_str(ast));
    return NULL;
}

PUREFUNC bool is_discardable(env_t *env, ast_t *ast)
{
    switch (ast->tag) {
    case UPDATE_CASES: case Assign: case Declare: case FunctionDef: case ConvertDef: case StructDef: case EnumDef:
    case LangDef: case Use: case Extend:
        return true;
    default: break;
    }
    type_t *t = get_type(env, ast);
    return (t->tag == VoidType || t->tag == AbortType || t->tag == ReturnType);
}

type_t *get_arg_ast_type(env_t *env, arg_ast_t *arg)
{
    assert(arg->type || arg->value);
    if (arg->type)
        return parse_type_ast(env, arg->type);
    return get_type(env, arg->value);
}

type_t *get_arg_type(env_t *env, arg_t *arg)
{
    assert(arg->type || arg->default_val);
    if (arg->type) return arg->type;
    return get_type(env, arg->default_val);
}

Table_t *get_arg_bindings(env_t *env, arg_t *spec_args, arg_ast_t *call_args, bool promotion_allowed)
{
    Table_t used_args = {};

    // Populate keyword args:
    for (arg_ast_t *call_arg = call_args; call_arg; call_arg = call_arg->next) {
        if (!call_arg->name) continue;

        type_t *call_type = get_arg_ast_type(env, call_arg);
        for (arg_t *spec_arg = spec_args; spec_arg; spec_arg = spec_arg->next) {
            if (!streq(call_arg->name, spec_arg->name)) continue;
            type_t *spec_type = get_arg_type(env, spec_arg);
            type_t *complete_call_type = is_incomplete_type(call_type) ? most_complete_type(call_type, spec_type) : call_type;
            if (!complete_call_type) return NULL;
            if (!(type_eq(complete_call_type, spec_type) || (promotion_allowed && can_promote(complete_call_type, spec_type))
                  || (promotion_allowed && call_arg->value->tag == Int && is_numeric_type(spec_type))
                  || (promotion_allowed && call_arg->value->tag == Num && spec_type->tag == NumType)))
                return NULL;
            Table$str_set(&used_args, call_arg->name, call_arg);
            goto next_call_arg;
        }
        return NULL;
      next_call_arg:;
    }

    arg_ast_t *unused_args = call_args;
    for (arg_t *spec_arg = spec_args; spec_arg; spec_arg = spec_arg->next) {
        arg_ast_t *keyworded = Table$str_get(used_args, spec_arg->name);
        if (keyworded) continue;

        type_t *spec_type = get_arg_type(env, spec_arg);
        for (; unused_args; unused_args = unused_args->next) {
            if (unused_args->name) continue; // Already handled the keyword args
            type_t *call_type = get_arg_ast_type(env, unused_args);
            type_t *complete_call_type = is_incomplete_type(call_type) ? most_complete_type(call_type, spec_type) : call_type;
            if (!complete_call_type) return NULL;
            if (!(type_eq(complete_call_type, spec_type) || (promotion_allowed && can_promote(complete_call_type, spec_type))
                  || (promotion_allowed && unused_args->value->tag == Int && is_numeric_type(spec_type))
                  || (promotion_allowed && unused_args->value->tag == Num && spec_type->tag == NumType)))
                return NULL; // Positional arg trying to fill in 
            Table$str_set(&used_args, spec_arg->name, unused_args);
            unused_args = unused_args->next;
            goto found_it;
        }

        if (spec_arg->default_val)
            goto found_it;

        return NULL;
      found_it: continue;
    }

    while (unused_args && unused_args->name)
        unused_args = unused_args->next;

    if (unused_args != NULL)
        return NULL;

    Table_t *ret = new(Table_t);
    *ret = used_args;
    return ret;
}

bool is_valid_call(env_t *env, arg_t *spec_args, arg_ast_t *call_args, bool promotion_allowed)
{
    Table_t *arg_bindings = get_arg_bindings(env, spec_args, call_args, promotion_allowed);
    return (arg_bindings != NULL);
}

PUREFUNC bool can_be_mutated(env_t *env, ast_t *ast)
{
    switch (ast->tag) {
    case Var: return true;
    case InlineCCode: return true;
    case FieldAccess: {
        DeclareMatch(access, ast, FieldAccess);
        type_t *fielded_type = get_type(env, access->fielded);
        if (fielded_type->tag == PointerType) {
            type_t *val = value_type(fielded_type);
            return val->tag == StructType;
        } else if (fielded_type->tag == StructType) {
            return can_be_mutated(env, access->fielded);
        } else {
            return false;
        }
    }
    case Index: {
        DeclareMatch(index, ast, Index);
        type_t *indexed_type = get_type(env, index->indexed);
        return (indexed_type->tag == PointerType);
    }
    default: return false;
    }
}

type_t *parse_type_string(env_t *env, const char *str)
{
    type_ast_t *ast = parse_type_str(str);
    return ast ? parse_type_ast(env, ast) : NULL;
}

PUREFUNC bool is_constant(env_t *env, ast_t *ast)
{
    switch (ast->tag) {
    case Bool: case Num: case None: return true;
    case Int: {
        DeclareMatch(info, ast, Int);
        Int_t int_val = Int$parse(Text$from_str(info->str));
        if (int_val.small == 0) return false; // Failed to parse
        return (Int$compare_value(int_val, I(BIGGEST_SMALL_INT)) <= 0);
    }
    case TextJoin: {
        DeclareMatch(text, ast, TextJoin);
        if (!text->children) return true; // Empty string, OK
        if (text->children->next) return false; // Concatenation, not constant
        return is_constant(env, text->children->ast);
    }
    case TextLiteral: {
        CORD literal = Match(ast, TextLiteral)->cord; 
        CORD_pos i;
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-conversion"
#endif
        CORD_FOR(i, literal) {
            if (!isascii(CORD_pos_fetch(i)))
                return false; // Non-ASCII requires grapheme logic, not constant
        }
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
        return true; // Literal ASCII string, OK
    }
    case Not: return is_constant(env, Match(ast, Not)->value);
    case Negative: return is_constant(env, Match(ast, Negative)->value);
    case BINOP_CASES: {
        binary_operands_t binop = BINARY_OPERANDS(ast);
        switch (ast->tag) {
        case Power: case Concat: case Min: case Max: case Compare:
            return false;
        default:
            return is_constant(env, binop.lhs) && is_constant(env, binop.rhs);
        }
    }
    case Use: return true;
    case FunctionCall: return false;
    case InlineCCode: return true;
    default: return false;
    }
}

PUREFUNC bool can_compile_to_type(env_t *env, ast_t *ast, type_t *needed)
{
    if (is_incomplete_type(needed))
        return false;

    if (needed->tag == OptionalType && ast->tag == None)
        return true;

    needed = non_optional(needed);
    if (needed->tag == ListType && ast->tag == List) {
        type_t *item_type = Match(needed, ListType)->item_type;
        for (ast_list_t *item = Match(ast, List)->items; item; item = item->next) {
            if (!can_compile_to_type(env, item->ast, item_type))
                return false;
        }
        return true;
    } else if (needed->tag == SetType && ast->tag == Set) {
        type_t *item_type = Match(needed, SetType)->item_type;
        for (ast_list_t *item = Match(ast, Set)->items; item; item = item->next) {
            if (!can_compile_to_type(env, item->ast, item_type))
                return false;
        }
        return true;
    } else if (needed->tag == TableType && ast->tag == Table) {
        type_t *key_type = Match(needed, TableType)->key_type;
        type_t *value_type = Match(needed, TableType)->value_type;
        for (ast_list_t *entry = Match(ast, Table)->entries; entry; entry = entry->next) {
            if (entry->ast->tag != TableEntry)
                continue; // TODO: fix this
            DeclareMatch(e, entry->ast, TableEntry);
            if (!can_compile_to_type(env, e->key, key_type) || !can_compile_to_type(env, e->value, value_type))
                return false;
        }
        return true;
    } else if (needed->tag == PointerType) {
        DeclareMatch(ptr, needed, PointerType);
        if (ast->tag == HeapAllocate)
            return !ptr->is_stack && can_compile_to_type(env, Match(ast, HeapAllocate)->value, ptr->pointed);
        else if (ast->tag == StackReference)
            return ptr->is_stack && can_compile_to_type(env, Match(ast, StackReference)->value, ptr->pointed);
        else
            return can_promote(needed, get_type(env, ast));
    } else {
        return can_promote(needed, get_type(env, ast));
    }
}

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
