// Logic for getting a type from an AST node
#include <gc.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <sys/stat.h>

#include "ast.h"
#include "environment.h"
#include "parse.h"
#include "typecheck.h"
#include "types.h"
#include "builtins/util.h"

type_t *parse_type_ast(env_t *env, type_ast_t *ast)
{
    switch (ast->tag) {
    case VarTypeAST: {
        const char *name = Match(ast, VarTypeAST)->name;
        type_t *t = Table$str_get(*env->types, name);
        if (t) return t;
        while (strchr(name, '.')) {
            char *module_name = heap_strn(name, strcspn(name, "."));
            binding_t *b = get_binding(env, module_name);
            if (!b || b->type->tag != ModuleType)
                code_err(ast, "I don't know a module with the name '%s'", module_name);

            env_t *imported = Table$str_get(*env->imports, Match(b->type, ModuleType)->name);
            assert(imported);
            env = imported;
            name = strchr(name, '.') + 1;
            t = Table$str_get(*env->types, name);
            if (t) return t;
        }
        code_err(ast, "I don't know a type with the name '%s'", name);
    }
    case PointerTypeAST: {
        auto ptr = Match(ast, PointerTypeAST);
        type_t *pointed_t = parse_type_ast(env, ptr->pointed);
        if (pointed_t->tag == VoidType)
            code_err(ast, "Void pointers are not supported. You probably meant 'Memory' instead of 'Void'");
        return Type(PointerType, .is_optional=ptr->is_optional, .pointed=pointed_t, .is_stack=ptr->is_stack, .is_readonly=ptr->is_readonly);
    }
    case ArrayTypeAST: {
        type_ast_t *item_type = Match(ast, ArrayTypeAST)->item;
        type_t *item_t = parse_type_ast(env, item_type);
        if (!item_t) code_err(item_type, "I can't figure out what this type is.");
        if (has_stack_memory(item_t))
            code_err(item_type, "Arrays can't have stack references because the array may outlive the stack frame.");
        return Type(ArrayType, .item_type=item_t);
    }
    case TableTypeAST: {
        type_ast_t *key_type_ast = Match(ast, TableTypeAST)->key;
        type_t *key_type = parse_type_ast(env, key_type_ast);
        if (!key_type) code_err(key_type_ast, "I can't figure out what type this is.");
        if (has_stack_memory(key_type))
            code_err(key_type_ast, "Tables can't have stack references because the array may outlive the stack frame.");
        type_ast_t *val_type_ast = Match(ast, TableTypeAST)->value;
        type_t *val_type = parse_type_ast(env, val_type_ast);
        if (!val_type) code_err(val_type_ast, "I can't figure out what type this is.");
        if (has_stack_memory(val_type))
            code_err(val_type_ast, "Tables can't have stack references because the array may outlive the stack frame.");
        return Type(TableType, .key_type=key_type, .value_type=val_type);
    }
    case FunctionTypeAST: {
        auto fn = Match(ast, FunctionTypeAST);
        type_t *ret_t = fn->ret ? parse_type_ast(env, fn->ret) : Type(VoidType);
        if (has_stack_memory(ret_t))
            code_err(fn->ret, "Functions are not allowed to return stack references, because the reference may no longer exist on the stack.");
        arg_t *type_args = NULL;
        for (arg_ast_t *arg = fn->args; arg; arg = arg->next) {
            type_args = new(arg_t, .name=arg->name, .next=type_args);
            if (arg->type) {
                type_args->type = parse_type_ast(env, arg->type);
            } else {
                type_args->default_val = arg->value;
                type_args->type = get_type(env, arg->value);
            }
        }
        REVERSE_LIST(type_args);
        return Type(ClosureType, Type(FunctionType, .args=type_args, .ret=ret_t));
    }
    case UnknownTypeAST: code_err(ast, "I don't know how to get this type");
    }
    code_err(ast, "I don't know how to get this type");
}

type_t *get_math_type(env_t *env, ast_t *ast, type_t *lhs_t, type_t *rhs_t)
{
    (void)env;
    if (lhs_t->tag == IntType && rhs_t->tag == IntType)
        return Match(lhs_t, IntType)->bits >= Match(rhs_t, IntType)->bits ? lhs_t : rhs_t;
    else if (lhs_t->tag == NumType && rhs_t->tag == NumType)
        return Match(lhs_t, NumType)->bits >= Match(rhs_t, NumType)->bits ? lhs_t : rhs_t;
    else if (lhs_t->tag == NumType && rhs_t->tag == IntType)
        return lhs_t;
    else if (rhs_t->tag == NumType && lhs_t->tag == IntType)
        return rhs_t;

    code_err(ast, "Math operations between %T and %T are not supported", lhs_t, rhs_t);
}

static env_t *load_module(env_t *env, ast_t *module_ast)
{
    if (module_ast->tag == Import) {
        auto import = Match(module_ast, Import);
        const char *name = file_base_name(import->path);
        env_t *module_env = Table$str_get(*env->imports, name);
        if (module_env)
            return module_env;

        const char *path = heap_strf("%s.tm", import->path);
        const char *resolved_path = resolve_path(path, module_ast->file->filename, module_ast->file->filename);
        if (!resolved_path)
            code_err(module_ast, "No such file exists: \"%s\"", path);

        ast_t *ast = parse_file(resolved_path, NULL);
        if (!ast) errx(1, "Could not compile file %s", resolved_path);
        return load_module_env(env, ast);
    } else if (module_ast->tag == Use) {
        const char *libname = Match(module_ast, Use)->name;
        const char *files_filename = heap_strf("%s/lib%s.files", libname, libname);
        const char *resolved_path = resolve_path(files_filename, module_ast->file->filename, getenv("TOMO_IMPORT_PATH"));
        if (!resolved_path)
            code_err(module_ast, "No such library exists: \"lib%s.files\"", libname);
        file_t *files_f = load_file(resolved_path);
        if (!files_f) errx(1, "Couldn't open file: %s", resolved_path);

        env_t *module_env = fresh_scope(env);
        Table$str_set(env->imports, libname, module_env);
        char *libname_id = heap_str(libname);
        for (char *c = libname_id; *c; c++) {
            if (!isalnum(*c) && *c != '_')
                *c = '_';
        }
        module_env->libname = new(CORD);
        *module_env->libname = (CORD)libname_id;
        for (int64_t i = 1; i <= files_f->num_lines; i++) {
            const char *line = get_line(files_f, i);
            line = heap_strn(line, strcspn(line, "\r\n"));
            if (!line || line[0] == '\0') continue;
            const char *tm_path = resolve_path(line, resolved_path, ".");
            if (!tm_path) errx(1, "Couldn't find library %s dependency: %s", libname, line);

            ast_t *ast = parse_file(tm_path, NULL);
            if (!ast) errx(1, "Could not compile file %s", tm_path);
            env_t *module_file_env = fresh_scope(module_env);
            char *file_prefix = heap_str(file_base_name(line));
            for (char *p = file_prefix; *p; p++) {
                if (!isalnum(*p) && *p != '_' && *p != '$')
                    *p = '_';
            }
            module_file_env->namespace = new(namespace_t, .name=file_prefix);
            env_t *subenv = load_module_env(module_file_env, ast);
            for (int64_t j = 0; j < subenv->locals->entries.length; j++) {
                struct {
                    const char *name; binding_t *binding;
                } *entry = subenv->locals->entries.data + j*subenv->locals->entries.stride;
                set_binding(module_env, entry->name, entry->binding);
            }
        }
        return module_env;
    } else {
        code_err(module_ast, "This is not a module import");
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
        auto def = Match(statement, StructDef);
        if (get_binding(env, def->name))
            code_err(statement, "A %T called '%s' has already been defined", get_binding(env, def->name)->type, def->name);

        env_t *ns_env = namespace_env(env, def->name);
        type_t *type = Type(StructType, .name=def->name, .opaque=true, .env=ns_env); // placeholder
        Table$str_set(env->types, def->name, type);
        set_binding(env, def->name, new(binding_t, .type=Type(TypeInfoType, .name=def->name, .type=type, .env=ns_env),
                                        .code=CORD_all(namespace_prefix(env->libname, env->namespace), def->name)));
        for (ast_list_t *stmt = def->namespace ? Match(def->namespace, Block)->statements : NULL; stmt; stmt = stmt->next)
            prebind_statement(ns_env, stmt->ast);
        break;
    }
    case EnumDef: {
        auto def = Match(statement, EnumDef);
        if (get_binding(env, def->name))
            code_err(statement, "A %T called '%s' has already been defined", get_binding(env, def->name)->type, def->name);

        env_t *ns_env = namespace_env(env, def->name);
        type_t *type = Type(EnumType, .name=def->name, .opaque=true, .env=ns_env); // placeholder
        Table$str_set(env->types, def->name, type);
        set_binding(env, def->name, new(binding_t, .type=Type(TypeInfoType, .name=def->name, .type=type, .env=ns_env),
                                        .code=CORD_all(namespace_prefix(env->libname, env->namespace), def->name)));
        for (ast_list_t *stmt = def->namespace ? Match(def->namespace, Block)->statements : NULL; stmt; stmt = stmt->next)
            prebind_statement(ns_env, stmt->ast);
        break;
    }
    case LangDef: {
        auto def = Match(statement, LangDef);
        if (get_binding(env, def->name))
            code_err(statement, "A %T called '%s' has already been defined", get_binding(env, def->name)->type, def->name);

        env_t *ns_env = namespace_env(env, def->name);
        type_t *type = Type(TextType, .lang=def->name, .env=ns_env);
        Table$str_set(env->types, def->name, type);
        set_binding(env, def->name, new(binding_t, .type=Type(TypeInfoType, .name=def->name, .type=type, .env=ns_env),
                                        .code=CORD_all(namespace_prefix(env->libname, env->namespace), def->name)));
        for (ast_list_t *stmt = def->namespace ? Match(def->namespace, Block)->statements : NULL; stmt; stmt = stmt->next)
            prebind_statement(ns_env, stmt->ast);
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
        auto decl = Match(statement, Declare);
        const char *name = Match(decl->var, Var)->name;
        if (get_binding(env, name))
            code_err(decl->var, "A %T called '%s' has already been defined", get_binding(env, name)->type, name);
        if (decl->value->tag == Use || decl->value->tag == Import) {
            if (decl->value->tag == Use && strncmp(Match(decl->value, Use)->name, "-l", 2) == 0)
                code_err(statement, "External library files specified with -l can't be assigned to a variable");
            (void)load_module(env, decl->value);
        } else {
            bind_statement(env, decl->value);
        }
        type_t *type = get_type(env, decl->value);
        CORD prefix = namespace_prefix(env->libname, env->namespace);
        CORD code = CORD_cat(prefix ? prefix : "$", name);
        set_binding(env, name, new(binding_t, .type=type, .code=code));
        break;
    }
    case FunctionDef: {
        auto def = Match(statement, FunctionDef);
        const char *name = Match(def->name, Var)->name;
        if (get_binding(env, name))
            code_err(def->name, "A %T called '%s' has already been defined", get_binding(env, name)->type, name);
        type_t *type = get_function_def_type(env, statement);
        CORD code = CORD_all(namespace_prefix(env->libname, env->namespace), name);
        set_binding(env, name, new(binding_t, .type=type, .code=code));
        break;
    }
    case StructDef: {
        auto def = Match(statement, StructDef);
        env_t *ns_env = namespace_env(env, def->name);
        type_t *type = Table$str_get(*env->types, def->name);
        if (!type) code_err(statement, "Couldn't find type!");
        assert(type);
        arg_t *fields = NULL;
        for (arg_ast_t *field_ast = def->fields; field_ast; field_ast = field_ast->next) {
            type_t *field_t = get_arg_ast_type(env, field_ast);
            if ((field_t->tag == StructType && Match(field_t, StructType)->opaque)
                || (field_t->tag == EnumType && Match(field_t, EnumType)->opaque)) {
                if (field_t == type)
                    code_err(field_ast->type, "This is a recursive struct that would be infinitely large. Maybe you meant to use an optional '@%T?' pointer instead?", type);
                else
                    code_err(field_ast->type, "I'm still in the process of defining the fields of %T, so I don't know how to use it as a member."
                             "\nTry using a @%T pointer for this field or moving the definition of %T before %T in the file.",
                             field_t, field_t, field_t, type);
            }
            fields = new(arg_t, .name=field_ast->name, .type=field_t, .default_val=field_ast->value, .next=fields);
        }
        REVERSE_LIST(fields);
        type->__data.StructType.fields = fields; // populate placeholder
        type->__data.StructType.opaque = false;
        
        for (ast_list_t *stmt = def->namespace ? Match(def->namespace, Block)->statements : NULL; stmt; stmt = stmt->next) {
            bind_statement(ns_env, stmt->ast);
        }
        break;
    }
    case EnumDef: {
        auto def = Match(statement, EnumDef);
        env_t *ns_env = namespace_env(env, def->name);
        type_t *type = Table$str_get(*env->types, def->name);
        assert(type);
        tag_t *tags = NULL;
        for (tag_ast_t *tag_ast = def->tags; tag_ast; tag_ast = tag_ast->next) {
            arg_t *fields = NULL;
            for (arg_ast_t *field_ast = tag_ast->fields; field_ast; field_ast = field_ast->next) {
                type_t *field_t = get_arg_ast_type(env, field_ast);
                if ((field_t->tag == StructType && Match(field_t, StructType)->opaque)
                    || (field_t->tag == EnumType && Match(field_t, EnumType)->opaque)) {
                    if (field_t == type)
                        code_err(field_ast->type, "This is a recursive enum that would be infinitely large. Maybe you meant to use an optional '@%T?' pointer instead?", type);
                    else
                        code_err(field_ast->type, "I'm still in the process of defining the fields of %T, so I don't know how to use it as a member."
                                 "\nTry using a @%T pointer for this field or moving the definition of %T before %T in the file.",
                                 field_t, field_t, field_t, type);
                }
                fields = new(arg_t, .name=field_ast->name, .type=field_t, .default_val=field_ast->value, .next=fields);
            }
            REVERSE_LIST(fields);
            env_t *member_ns = namespace_env(env, heap_strf("%s$%s", def->name, tag_ast->name));
            type_t *tag_type = Type(StructType, .name=heap_strf("%s$%s", def->name, tag_ast->name), .fields=fields, .env=member_ns);
            tags = new(tag_t, .name=tag_ast->name, .tag_value=tag_ast->value, .type=tag_type, .next=tags);
        }
        REVERSE_LIST(tags);
        type->__data.EnumType.tags = tags;
        type->__data.EnumType.opaque = false;

        for (tag_t *tag = tags; tag; tag = tag->next) {
            if (Match(tag->type, StructType)->fields) { // Constructor:
                type_t *constructor_t = Type(FunctionType, .args=Match(tag->type, StructType)->fields, .ret=type);
                set_binding(ns_env, tag->name, new(binding_t, .type=constructor_t, .code=CORD_all(namespace_prefix(env->libname, env->namespace), def->name, "$tagged$", tag->name)));
            } else { // Empty singleton value:
                CORD code = CORD_all("(", namespace_prefix(env->libname, env->namespace), def->name, "_t){", namespace_prefix(env->libname, env->namespace), def->name, "$tag$", tag->name, "}");
                set_binding(ns_env, tag->name, new(binding_t, .type=type, .code=code));
            }
            Table$str_set(env->types, heap_strf("%s$%s", def->name, tag->name), tag->type);
        }
        
        for (ast_list_t *stmt = def->namespace ? Match(def->namespace, Block)->statements : NULL; stmt; stmt = stmt->next) {
            bind_statement(ns_env, stmt->ast);
        }
        break;
    }
    case LangDef: {
        auto def = Match(statement, LangDef);
        env_t *ns_env = namespace_env(env, def->name);
        type_t *type = Type(TextType, .lang=def->name, .env=ns_env);
        Table$str_set(env->types, def->name, type);

        set_binding(ns_env, "from_unsafe_text",
                    new(binding_t, .type=Type(FunctionType, .args=new(arg_t, .name="text", .type=TEXT_TYPE), .ret=type),
                        .code=CORD_all("(", namespace_prefix(env->libname, env->namespace), def->name, "_t)")));
        set_binding(ns_env, "text_content",
                    new(binding_t, .type=Type(FunctionType, .args=new(arg_t, .name="text", .type=TEXT_TYPE), .ret=type),
                        .code="(Text_t)"));

        for (ast_list_t *stmt = def->namespace ? Match(def->namespace, Block)->statements : NULL; stmt; stmt = stmt->next)
            bind_statement(ns_env, stmt->ast);
        break;
    }
    case Use: case Import: {
        if (statement->tag == Use && strncmp(Match(statement, Use)->name, "-l", 2) == 0)
            break;

        env_t *module_env = load_module(env, statement);
        for (table_t *bindings = module_env->locals; bindings != module_env->globals; bindings = bindings->fallback) {
            for (int64_t i = 1; i <= Table$length(*bindings); i++) {
                struct {const char *name; binding_t *binding; } *entry = Table$entry(*bindings, i);
                if (entry->name[0] == '_' || streq(entry->name, "main"))
                    continue;
                binding_t *b = Table$str_get(*env->locals, entry->name);
                if (!b)
                    Table$str_set(env->locals, entry->name, entry->binding);
                else if (b != entry->binding)
                    code_err(statement, "This module imports a symbol called '%s', which would clobber another variable", entry->name);
            }
        }
        for (int64_t i = 1; i <= Table$length(*module_env->types); i++) {
            struct {const char *name; type_t *type; } *entry = Table$entry(*module_env->types, i);
            if (entry->name[0] == '_')
                continue;
            if (Table$str_get(*env->types, entry->name))
                continue;

//code_err(statement, "This module imports a type called '%s', which would clobber another type", entry->name);
            Table$str_set(env->types, entry->name, entry->type);
        }
        break;
    }
    case Extern: {
        auto ext = Match(statement, Extern);
        type_t *t = parse_type_ast(env, ext->type);
        if (t->tag == ClosureType)
            t = Match(t, ClosureType)->fn;
        set_binding(env, ext->name, new(binding_t, .type=t, .code=ext->name));
        break;
    }
    default: break;
    }
}

type_t *get_function_def_type(env_t *env, ast_t *ast)
{
    auto fn = Match(ast, FunctionDef);
    arg_t *args = NULL;
    env_t *scope = fresh_scope(env);
    for (arg_ast_t *arg = fn->args; arg; arg = arg->next) {
        type_t *t = arg->type ? parse_type_ast(env, arg->type) : get_type(env, arg->value);
        args = new(arg_t, .name=arg->name, .type=t, .default_val=arg->value, .next=args);
        set_binding(scope, arg->name, new(binding_t, .type=t));
    }
    REVERSE_LIST(args);

    type_t *ret = fn->ret_type ? parse_type_ast(scope, fn->ret_type) : Type(VoidType);
    if (has_stack_memory(ret))
        code_err(ast, "Functions can't return stack references because the reference may outlive its stack frame.");
    return Type(FunctionType, .args=args, .ret=ret);
}

type_t *get_method_type(env_t *env, ast_t *self, const char *name)
{
    binding_t *b = get_namespace_binding(env, self, name);
    if (!b || !b->type)
        code_err(self, "No such method: %T:%s(...)", get_type(env, self), name);
    return b->type;
}

type_t *get_type(env_t *env, ast_t *ast)
{
    if (!ast) return NULL;
    switch (ast->tag) {
    case Nil: {
        type_t *t = parse_type_ast(env, Match(ast, Nil)->type);
        if (t->tag != PointerType)
            code_err(ast, "This type is not a pointer type, so it doesn't work with a '!' nil expression");
        auto ptr = Match(t, PointerType);
        return Type(PointerType, .is_optional=true, .pointed=ptr->pointed, .is_stack=ptr->is_stack, .is_readonly=ptr->is_readonly);
    }
    case Bool: {
        return Type(BoolType);
    }
    case Int: {
        auto i = Match(ast, Int);
        return Type(IntType, .bits=i->bits);
    }
    case Num: {
        auto n = Match(ast, Num);
        return Type(NumType, .bits=n->bits);
    }
    case HeapAllocate: {
        type_t *pointed = get_type(env, Match(ast, HeapAllocate)->value);
        if (has_stack_memory(pointed))
            code_err(ast, "Stack references cannot be moved to the heap because they may outlive the stack frame they were created in.");
        return Type(PointerType, .is_optional=false, .pointed=pointed);
    }
    case StackReference: {
        // Supported:
        //   &variable
        //   &struct_variable.field.(...)
        //   &struct_ptr.field.(...)
        // Not supported:
        //   &ptr[]
        //   &list[index]
        //   &table[key]
        //   &(expression).field
        //   &(expression)
        //   &optional_struct_ptr.field
        ast_t *value = Match(ast, StackReference)->value;
        if (value->tag == Var)
            return Type(PointerType, .pointed=get_type(env, value), .is_stack=true);

        if (value->tag == FieldAccess) {
            ast_t *base = value;
            while (base->tag == FieldAccess)
                base = Match(base, FieldAccess)->fielded;

            type_t *ref_type = get_type(env, value);
            type_t *base_type = get_type(env, base);
            if (base_type->tag == PointerType) {
                auto ptr = Match(base_type, PointerType);
                if (ptr->is_optional)
                    code_err(base, "This value might be null, so it can't be safely dereferenced");
                return Type(PointerType, .pointed=ref_type, .is_stack=ptr->is_stack, .is_readonly=ptr->is_readonly);
            } else if (base->tag == Var) {
                return Type(PointerType, .pointed=ref_type, .is_stack=true);
            }
        }

        code_err(ast, "'&' stack references can only be used on variables or fields of variables");
    }
    case Optional: {
        ast_t *value = Match(ast, Optional)->value;
        type_t *t = get_type(env, value);
        if (t->tag != PointerType)
            code_err(ast, "This value is not a pointer, it has type %T, so it can't be optional", t);
        auto ptr = Match(t, PointerType);
        if (ptr->is_optional)
            code_err(ast, "This value is already optional, it can't be converted to optional");
        return Type(PointerType, .pointed=ptr->pointed, .is_optional=true, .is_stack=ptr->is_stack, .is_readonly=ptr->is_readonly);
    }
    case TextLiteral: return TEXT_TYPE;
    case TextJoin: {
        const char *lang = Match(ast, TextJoin)->lang;
        return lang ? Match(get_binding(env, lang)->type, TypeInfoType)->type : TEXT_TYPE;
    }
    case Var: {
        auto var = Match(ast, Var);
        binding_t *b = get_binding(env, var->name);
        if (b) return b->type;
        code_err(ast, "I don't know what \"%s\" refers to", var->name);
    }
    case Array: {
        auto array = Match(ast, Array);
        type_t *item_type = NULL;
        if (array->type) {
            item_type = parse_type_ast(env, array->type);
        } else if (array->items) {
            for (ast_list_t *item = array->items; item; item = item->next) {
                ast_t *item_ast = item->ast;
                env_t *scope = env;
                while (item_ast->tag == Comprehension) {
                    auto comp = Match(item_ast, Comprehension);
                    scope = for_scope(
                        scope, FakeAST(For, .iter=comp->iter, .vars=comp->vars));
                    item_ast = comp->expr;
                }
                type_t *t2 = get_type(scope, item_ast);
                type_t *merged = item_type ? type_or_type(item_type, t2) : t2;
                if (!merged)
                    code_err(item->ast,
                             "This array item has type %T, which is different from earlier array items which have type %T",
                             t2,  item_type);
                item_type = merged;
            }
        } else {
            code_err(ast, "I can't figure out what type this array has because it has no members or explicit type");
        }
        if (has_stack_memory(item_type))
            code_err(ast, "Arrays cannot hold stack references, because the array may outlive the stack frame the reference was created in.");
        return Type(ArrayType, .item_type=item_type);
    }
    case Table: {
        auto table = Match(ast, Table);
        type_t *key_type = NULL, *value_type = NULL;
        if (table->key_type && table->value_type) {
            key_type = parse_type_ast(env, table->key_type);
            value_type = parse_type_ast(env, table->value_type);
        } else {
            if (table->default_value)
                value_type = get_type(env, table->default_value);
            for (ast_list_t *entry = table->entries; entry; entry = entry->next) {
                ast_t *entry_ast = entry->ast;
                env_t *scope = env;
                while (entry_ast->tag == Comprehension) {
                    auto comp = Match(entry_ast, Comprehension);
                    scope = for_scope(
                        scope, FakeAST(For, .iter=comp->iter, .vars=comp->vars));
                    entry_ast = comp->expr;
                }

                auto e = Match(entry_ast, TableEntry);
                type_t *key_t = get_type(scope, e->key);
                type_t *value_t = get_type(scope, e->value);

                type_t *key_merged = key_type ? type_or_type(key_type, key_t) : key_t;
                if (!key_merged)
                    code_err(entry->ast,
                             "This table entry has type %T, which is different from earlier table entries which have type %T",
                             key_t, key_type);
                key_type = key_merged;

                type_t *val_merged = value_type ? type_or_type(value_type, value_t) : value_t;
                if (!val_merged)
                    code_err(entry->ast,
                             "This table entry has type %T, which is different from earlier table entries which have type %T",
                             value_t, value_type);
                value_type = val_merged;
            }
        }
        if (has_stack_memory(key_type) || has_stack_memory(value_type))
            code_err(ast, "Tables cannot hold stack references because the table may outlive the reference's stack frame.");
        return Type(TableType, .key_type=key_type, .value_type=value_type);
    }
    case TableEntry: {
        code_err(ast, "Table entries should not be typechecked directly");
    }
    case Comprehension: {
        auto comp = Match(ast, Comprehension);
        env_t *scope = for_scope(env, FakeAST(For, .iter=comp->iter, .vars=comp->vars));
        if (comp->expr->tag == Comprehension) {
            return get_type(scope, comp->expr);
        } else if (comp->expr->tag == TableEntry) {
            auto e = Match(comp->expr, TableEntry);
            return Type(TableType, .key_type=get_type(scope, e->key), .value_type=get_type(scope, e->value));
        } else {
            return Type(ArrayType, .item_type=get_type(scope, comp->expr));
        }
    }
    case FieldAccess: {
        auto access = Match(ast, FieldAccess);
        type_t *fielded_t = get_type(env, access->fielded);
        if (fielded_t->tag == ModuleType) {
            const char *name = Match(fielded_t, ModuleType)->name;
            env_t *module_env = Table$str_get(*env->imports, name);
            if (!module_env) code_err(access->fielded, "I couldn't find the environment for the module %s", name);
            return get_type(module_env, WrapAST(ast, Var, access->field));
        } else if (fielded_t->tag == TypeInfoType) {
            auto info = Match(fielded_t, TypeInfoType);
            assert(info->env);
            binding_t *b = get_binding(info->env, access->field);
            if (!b) code_err(ast, "I couldn't find the field '%s' on this type", access->field);
            return b->type;
        }
        // TODO: support automatically generating closures for methods like in python (foo.method -> func(f:Foo, ...) f:method(...))
        type_t *field_t = get_field_type(fielded_t, access->field);
        if (!field_t)
            code_err(ast, "%T objects don't have a field called '%s'", fielded_t, access->field);
        return field_t;
    }
    case Index: {
        auto indexing = Match(ast, Index);
        type_t *indexed_t = get_type(env, indexing->indexed);
        if (indexed_t->tag == PointerType && !indexing->index) {
            auto ptr = Match(indexed_t, PointerType);
            if (ptr->is_optional)
                code_err(ast, "You're attempting to dereference a pointer whose type indicates it could be nil");
            return ptr->pointed;
        }

        type_t *value_t = value_type(indexed_t);
        switch (value_t->tag) {
        case ArrayType: {
            if (!indexing->index) return indexed_t;
            type_t *index_t = get_type(env, indexing->index);
            switch (index_t->tag) {
            case IntType:
                return Match(value_t, ArrayType)->item_type;
            default: code_err(indexing->index, "I only know how to index lists using integers, not %T", index_t);
            }
        }
        case TableType: {
            return Match(value_t, TableType)->value_type;
        }
        // TODO: support ranges like (99..123)[5]
        // TODO: support slicing arrays like ([1,2,3,4])[2..10]
        default: {
            code_err(ast, "I don't know how to index %T values", indexed_t);
        }
        }
    }
    case FunctionCall: {
        auto call = Match(ast, FunctionCall);
        type_t *fn_type_t = get_type(env, call->fn);
        if (!fn_type_t)
            code_err(call->fn, "I couldn't find this function");

        if (fn_type_t->tag == TypeInfoType) {
            type_t *t = Match(fn_type_t, TypeInfoType)->type;
            if (t->tag == StructType || t->tag == IntType || t->tag == NumType || t->tag == TextType || t->tag == CStringType)
                return t; // Constructor
            code_err(call->fn, "This is not a type that has a constructor");
        }
        if (fn_type_t->tag == ClosureType)
            fn_type_t = Match(fn_type_t, ClosureType)->fn;
        if (fn_type_t->tag != FunctionType)
            code_err(call->fn, "This isn't a function, it's a %T", fn_type_t);
        auto fn_type = Match(fn_type_t, FunctionType);
        return fn_type->ret;
    }
    case MethodCall: {
        auto call = Match(ast, MethodCall);
        type_t *self_value_t = value_type(get_type(env, call->self));
        switch (self_value_t->tag) {
        case ArrayType: {
            if (streq(call->name, "insert")) return Type(VoidType);
            else if (streq(call->name, "insert_all")) return Type(VoidType);
            else if (streq(call->name, "remove")) return Type(VoidType);
            else if (streq(call->name, "sort")) return Type(VoidType);
            else if (streq(call->name, "sorted")) return self_value_t;
            else if (streq(call->name, "shuffle")) return Type(VoidType);
            else if (streq(call->name, "random"))
                return Type(PointerType, .pointed=Match(self_value_t, ArrayType)->item_type, .is_optional=true, .is_readonly=true);
            else if (streq(call->name, "sample")) return self_value_t;
            else if (streq(call->name, "clear")) return Type(VoidType);
            else if (streq(call->name, "from")) return self_value_t;
            else if (streq(call->name, "to")) return self_value_t;
            else if (streq(call->name, "by")) return self_value_t;
            else if (streq(call->name, "reversed")) return self_value_t;
            else if (streq(call->name, "heapify")) return Type(VoidType);
            else if (streq(call->name, "heap_push")) return Type(VoidType);
            else if (streq(call->name, "heap_pop")) return Match(self_value_t, ArrayType)->item_type;
            else if (streq(call->name, "pairs")) {
                type_t *ref_t = Type(PointerType, .pointed=Match(self_value_t, ArrayType)->item_type, .is_stack=true);
                arg_t *args = new(arg_t, .name="x", .type=ref_t, .next=new(arg_t, .name="y", .type=ref_t));
                return Type(ClosureType, .fn=Type(FunctionType, .args=args, .ret=Type(BoolType)));
            } else code_err(ast, "There is no '%s' method for arrays", call->name);
        }
        case TableType: {
            auto table = Match(self_value_t, TableType);
            if (streq(call->name, "get")) return Type(PointerType, .pointed=table->value_type, .is_readonly=true, .is_optional=true);
            else if (streq(call->name, "set")) return Type(VoidType);
            else if (streq(call->name, "remove")) return Type(VoidType);
            else if (streq(call->name, "clear")) return Type(VoidType);
            else code_err(ast, "There is no '%s' method for tables", call->name);
        }
        default: {
            type_t *fn_type_t = get_method_type(env, call->self, call->name);
            if (!fn_type_t)
                code_err(ast, "No such method!");
            if (fn_type_t->tag != FunctionType)
                code_err(ast, "This isn't a method, it's a %T", fn_type_t);
            auto fn_type = Match(fn_type_t, FunctionType);
            return fn_type->ret;
        }
        }
    }
    case Block: {
        auto block = Match(ast, Block);
        ast_list_t *last = block->statements;
        if (!last)
            return Type(VoidType);
        while (last->next)
            last = last->next;

        // Early out if the type is knowable without any context from the block:
        switch (last->ast->tag) {
        case UpdateAssign: case Assign: case Declare: case FunctionDef: case StructDef: case EnumDef: case LangDef:
            return Type(VoidType);
        default: break;
        }

        env_t *block_env = fresh_scope(env);
        for (ast_list_t *stmt = block->statements; stmt; stmt = stmt->next) {
            bind_statement(block_env, stmt->ast);
        }
        return get_type(block_env, last->ast);
    }
    case Extern: {
        return parse_type_ast(env, Match(ast, Extern)->type);
    }
    case Declare: case Assign: case DocTest: case LinkerDirective: {
        return Type(VoidType);
    }
    case Import: {
        const char *path = Match(ast, Import)->path;
        return Type(ModuleType, file_base_name(path));
    }
    case Use: {
        return Type(ModuleType, Match(ast, Use)->name);
    }
    case Return: {
        ast_t *val = Match(ast, Return)->value;
        // Support unqualified enum return values:
        if (env->fn_ctx && env->fn_ctx->return_type->tag == EnumType) {
            env = fresh_scope(env);
            auto enum_ = Match(env->fn_ctx->return_type, EnumType);
            env_t *ns_env = enum_->env;
            for (tag_t *tag = enum_->tags; tag; tag = tag->next) {
                if (get_binding(env, tag->name))
                    continue;
                binding_t *b = get_binding(ns_env, tag->name);
                assert(b);
                set_binding(env, tag->name, b);
            }
        }
        return Type(ReturnType, .ret=(val ? get_type(env, val) : Type(VoidType)));
    }
    case Stop: case Skip: case PrintStatement: {
        return Type(AbortType);
    }
    case Pass: case Defer: return Type(VoidType);
    case Length: return Type(IntType, .bits=64);
    case Negative: {
        ast_t *value = Match(ast, Negative)->value;
        type_t *t = get_type(env, value);
        if (t->tag == IntType || t->tag == NumType)
            return t;

        binding_t *b = get_namespace_binding(env, value, "__negative");
        if (b && b->type->tag == FunctionType) {
            auto fn = Match(b->type, FunctionType);
            if (fn->args && can_promote(t, get_arg_type(env, fn->args)))
                return fn->ret;
        }

        code_err(ast, "I don't know how to get the negative value of type %T", t);
    }
    case Not: {
        type_t *t = get_type(env, Match(ast, Not)->value);
        if (t->tag == IntType || t->tag == NumType || t->tag == BoolType)
            return t;
        if (t->tag == PointerType && Match(t, PointerType)->is_optional)
            return Type(BoolType);
        code_err(ast, "I only know how to get 'not' of boolean, numeric, and optional pointer types, not %T", t);
    }
    case BinaryOp: {
        auto binop = Match(ast, BinaryOp);
        type_t *lhs_t = get_type(env, binop->lhs),
               *rhs_t = get_type(env, binop->rhs);

        // Check for a binop method like __add() etc:
        const char *method_name = binop_method_names[binop->op];
        if (method_name) {
            for (int64_t n = 1; ; ) {
                binding_t *b = get_namespace_binding(env, binop->lhs, method_name);
                if (b && b->type->tag == FunctionType) {
                    auto fn = Match(b->type, FunctionType);
                    if (fn->args && fn->args->next && can_promote(lhs_t, get_arg_type(env, fn->args))
                        && can_promote(rhs_t, get_arg_type(env, fn->args->next)))
                        return fn->ret;
                }
                binding_t *b2 = get_namespace_binding(env, binop->rhs, method_name);
                if (b2 && b2->type->tag == FunctionType) {
                    auto fn = Match(b2->type, FunctionType);
                    if (fn->args && fn->args->next && can_promote(lhs_t, get_arg_type(env, fn->args))
                        && can_promote(rhs_t, get_arg_type(env, fn->args->next)))
                        return fn->ret;
                }
                if (!b && !b2) break;

                // If we found __foo, but it didn't match the types, check for
                // __foo2, __foo3, etc. until we stop finding methods with that name.
                method_name = heap_strf("%s%ld", binop_method_names[binop->op], ++n);
            }
        }

        switch (binop->op) {
        case BINOP_AND: {
            if (lhs_t->tag == BoolType && rhs_t->tag == BoolType) {
                return lhs_t;
            } else if (lhs_t->tag == BoolType && (rhs_t->tag == AbortType || rhs_t->tag == ReturnType)) {
                return lhs_t;
            } else if (rhs_t->tag == AbortType || rhs_t->tag == ReturnType) {
                return lhs_t;
            } else if (lhs_t->tag == PointerType && rhs_t->tag == PointerType) {
                auto lhs_ptr = Match(lhs_t, PointerType);
                auto rhs_ptr = Match(rhs_t, PointerType);
                if (type_eq(lhs_ptr->pointed, rhs_ptr->pointed))
                    return Type(PointerType, .pointed=lhs_ptr->pointed, .is_optional=lhs_ptr->is_optional || rhs_ptr->is_optional,
                                .is_readonly=lhs_ptr->is_readonly || rhs_ptr->is_readonly);
            } else if (lhs_t->tag == IntType && rhs_t->tag == IntType) {
                return get_math_type(env, ast, lhs_t, rhs_t);
            }
            code_err(ast, "I can't figure out the type of this `and` expression because the left side is a %T, but the right side is a %T",
                         lhs_t, rhs_t);
        }
        case BINOP_OR: {
            if (lhs_t->tag == BoolType && rhs_t->tag == BoolType) {
                return lhs_t;
            } else if (lhs_t->tag == BoolType && (rhs_t->tag == AbortType || rhs_t->tag == ReturnType)) {
                return lhs_t;
            } else if (lhs_t->tag == IntType && rhs_t->tag == IntType) {
                return get_math_type(env, ast, lhs_t, rhs_t);
            } else if (lhs_t->tag == PointerType) {
                auto lhs_ptr = Match(lhs_t, PointerType);
                if (rhs_t->tag == AbortType || rhs_t->tag == ReturnType) {
                    return Type(PointerType, .pointed=lhs_ptr->pointed, .is_optional=false, .is_readonly=lhs_ptr->is_readonly);
                } else if (rhs_t->tag == PointerType) {
                    auto rhs_ptr = Match(rhs_t, PointerType);
                    if (type_eq(rhs_ptr->pointed, lhs_ptr->pointed))
                        return Type(PointerType, .pointed=lhs_ptr->pointed, .is_optional=lhs_ptr->is_optional && rhs_ptr->is_optional,
                                    .is_readonly=lhs_ptr->is_readonly || rhs_ptr->is_readonly);
                }
            }
            code_err(ast, "I can't figure out the type of this `or` expression because the left side is a %T, but the right side is a %T",
                         lhs_t, rhs_t);
        }
        case BINOP_XOR: {
            if (lhs_t->tag == BoolType && rhs_t->tag == BoolType) {
                return lhs_t;
            } else if (lhs_t->tag == IntType && rhs_t->tag == IntType) {
                return get_math_type(env, ast, lhs_t, rhs_t);
            }

            code_err(ast, "I can't figure out the type of this `xor` expression because the left side is a %T, but the right side is a %T",
                         lhs_t, rhs_t);
        }
        case BINOP_CONCAT: {
            if (!type_eq(lhs_t, rhs_t))
                code_err(ast, "The type on the left side of this concatenation doesn't match the right side: %T vs. %T",
                             lhs_t, rhs_t);
            if (lhs_t->tag == ArrayType || lhs_t->tag == TextType)
                return lhs_t;

            code_err(ast, "Only array/text value types support concatenation, not %T", lhs_t);
        }
        case BINOP_EQ: case BINOP_NE: case BINOP_LT: case BINOP_LE: case BINOP_GT: case BINOP_GE: {
            if (!can_promote(lhs_t, rhs_t) && !can_promote(rhs_t, lhs_t))
                code_err(ast, "I can't compare these two different types: %T vs %T", lhs_t, rhs_t);
            return Type(BoolType);
        }
        case BINOP_CMP:
            return Type(IntType, .bits=32);
        case BINOP_POWER: {
            type_t *result = get_math_type(env, ast, lhs_t, rhs_t);
            if (result->tag == NumType)
                return result;
            return Type(NumType, .bits=64);
        }
        default: {
            return get_math_type(env, ast, lhs_t, rhs_t);
        }
        }
    }

    case Reduction: {
        auto reduction = Match(ast, Reduction);
        type_t *iter_t = get_type(env, reduction->iter);

        type_t *value_t;
        type_t *iter_value_t = value_type(iter_t);
        switch (iter_value_t->tag) {
        case IntType: value_t = iter_value_t; break;
        case ArrayType: value_t = Match(iter_value_t, ArrayType)->item_type; break;
        case TableType: value_t = Match(iter_value_t, TableType)->key_type; break;
        case FunctionType: case ClosureType: {
            auto fn = iter_value_t->tag == ClosureType ?
                Match(Match(iter_value_t, ClosureType)->fn, FunctionType) : Match(iter_value_t, FunctionType);
            if (!fn->args || fn->args->next)
                code_err(reduction->iter, "I expected this iterable to have exactly one argument, not %T", iter_value_t);
            type_t *arg_type = get_arg_type(env, fn->args);
            if (arg_type->tag != PointerType)
                code_err(reduction->iter, "I expected this iterable to have exactly one stack reference argument, not %T", arg_type);
            auto ptr = Match(arg_type, PointerType);
            if (!ptr->is_stack || ptr->is_optional || ptr->is_readonly)
                code_err(reduction->iter, "I expected this iterable to have exactly one stack reference argument, not %T", arg_type);
            value_t = ptr->pointed;
            break;
        }
        default: code_err(reduction->iter, "I don't know how to do a reduction over %T values", iter_t);
        }

        env_t *scope = fresh_scope(env);
        set_binding(scope, "$reduction", new(binding_t, .type=value_t, .code="reduction"));
        set_binding(scope, "$iter_value", new(binding_t, .type=value_t, .code="iter_value"));
        type_t *t = get_type(scope, reduction->combination);
        if (!reduction->fallback)
            return t;
        type_t *fallback_t = get_type(env, reduction->fallback);
        if (fallback_t->tag == AbortType || fallback_t->tag == ReturnType)
            return t;
        else if (can_promote(fallback_t, t))
            return t;
        else if (can_promote(t, fallback_t))
            return fallback_t;
        else
            return NULL;
    }

    case UpdateAssign:
        return Type(VoidType);

    case Min: case Max: {
        // Unsafe! These types *should* have the same fields and this saves a lot of duplicate code:
        ast_t *lhs = ast->__data.Min.lhs, *rhs = ast->__data.Min.rhs;
        // Okay safe again

        type_t *lhs_t = get_type(env, lhs), *rhs_t = get_type(env, rhs);
        type_t *t = type_or_type(lhs_t, rhs_t);
        if (!t)
            code_err(ast, "The two sides of this operation are not compatible: %T vs %T", lhs_t, rhs_t);
        return t;
    }

    case Lambda: {
        auto lambda = Match(ast, Lambda);
        arg_t *args = NULL;
        env_t *scope = fresh_scope(env); // For now, just use closed variables in scope normally
        for (arg_ast_t *arg = lambda->args; arg; arg = arg->next) {
            type_t *t = arg->type ? parse_type_ast(env, arg->type) : get_type(env, arg->value);
            args = new(arg_t, .name=arg->name, .type=t, .next=args);
            set_binding(scope, arg->name, new(binding_t, .type=t));
        }
        REVERSE_LIST(args);

        type_t *ret = get_type(scope, lambda->body);
        assert(ret);
        if (ret->tag == ReturnType)
            ret = Match(ret, ReturnType)->ret;

        if (has_stack_memory(ret))
            code_err(ast, "Functions can't return stack references because the reference may outlive its stack frame.");
        return Type(ClosureType, Type(FunctionType, .args=args, .ret=ret));
    }

    case FunctionDef: case StructDef: case EnumDef: case LangDef: {
        return Type(VoidType);
    }

    case If: {
        auto if_ = Match(ast, If);
        type_t *true_t = get_type(env, if_->body);
        if (if_->else_body) {
            type_t *false_t = get_type(env, if_->else_body);
            type_t *t_either = type_or_type(true_t, false_t);
            if (!t_either)
                code_err(if_->else_body,
                         "I was expecting this block to have a %T value (based on earlier clauses), but it actually has a %T value.",
                         true_t, false_t);
            return t_either;
        } else {
            return Type(VoidType);
        }
    }

    case When: {
        auto when = Match(ast, When);
        type_t *subject_t = get_type(env, when->subject);
        type_t *overall_t = NULL;
        if (subject_t->tag == PointerType) {
            if (!Match(subject_t, PointerType)->is_optional)
                code_err(when->subject, "This %T pointer type is not optional, so this 'when' statement is tautological", subject_t);

            bool handled_at = false;
            for (when_clause_t *clause = when->clauses; clause; clause = clause->next) {
                const char *tag_name = Match(clause->tag_name, Var)->name;
                if (!streq(tag_name, "@"))
                    code_err(clause->tag_name, "'when' clauses on optional pointers only support @var, not tags like '%s'", tag_name);
                if (handled_at)
                    code_err(clause->tag_name, "This 'when' statement has already handled the case of non-null pointers!");
                handled_at = true;

                assert(clause->args);
                env_t *scope = fresh_scope(env);
                auto ptr = Match(subject_t, PointerType);
                set_binding(scope, Match(clause->args->ast, Var)->name,
                            new(binding_t, .type=Type(PointerType, .pointed=ptr->pointed, .is_stack=ptr->is_stack, .is_readonly=ptr->is_readonly)));

                type_t *clause_type = get_type(scope, clause->body);
                type_t *merged = type_or_type(overall_t, clause_type);
                if (!merged)
                    code_err(clause->body, "The type of this branch is %T, which conflicts with the earlier branch type of %T",
                             clause_type, overall_t);
                overall_t = merged;
            }
            if (!handled_at)
                code_err(ast, "This 'when' statement doesn't handle non-null pointers");
            if (!when->else_body)
                code_err(ast, "This 'when' statement doesn't handle null pointers");
            return overall_t;
        }

        if (subject_t->tag != EnumType)
            code_err(when->subject, "'when' statements are only for enum types and optional pointers, not %T", subject_t);

        tag_t * const tags = Match(subject_t, EnumType)->tags;

        typedef struct match_s {
            const char *name;
            type_t *type;
            bool handled;
            struct match_s *next;
        } match_t;
        match_t *matches = NULL;
        for (tag_t *tag = tags; tag; tag = tag->next)
            matches = new(match_t, .name=tag->name, .type=tag->type, .next=matches);

        for (when_clause_t *clause = when->clauses; clause; clause = clause->next) {
            const char *tag_name = Match(clause->tag_name, Var)->name;
            type_t *tag_type = NULL;
            CORD valid_tags = CORD_EMPTY;
            for (match_t *m = matches; m; m = m->next) {
                if (streq(m->name, tag_name)) {
                    if (m->handled)
                        code_err(clause->tag_name, "This tag was already handled earlier");
                    m->handled = true;
                    tag_type = m->type;
                    break;
                }
                if (valid_tags) valid_tags = CORD_cat(valid_tags, ", ");
                valid_tags = CORD_cat(valid_tags, m->name);
            }

            if (!tag_type)
                code_err(clause->tag_name, "There is no tag '%s' for the type %T (valid tags: %s)",
                         tag_name, subject_t, CORD_to_char_star(valid_tags));

            env_t *scope = env;
            auto tag_struct = Match(tag_type, StructType);
            if (clause->args && !clause->args->next && tag_struct->fields && tag_struct->fields->next) {
                scope = fresh_scope(scope);
                set_binding(scope, Match(clause->args->ast, Var)->name, new(binding_t, .type=tag_type));
            } else if (clause->args) {
                scope = fresh_scope(scope);
                ast_list_t *var = clause->args;
                arg_t *field = tag_struct->fields;
                while (var || field) {
                    if (!var)
                        code_err(clause->tag_name, "The field %T.%s.%s wasn't accounted for", subject_t, tag_name, field->name);
                    if (!field)
                        code_err(var->ast, "This is one more field than %T has", subject_t);
                    set_binding(scope, Match(var->ast, Var)->name, new(binding_t, .type=field->type));
                    var = var->next;
                    field = field->next;
                }
            }
            type_t *clause_type = get_type(scope, clause->body);
            type_t *merged = type_or_type(overall_t, clause_type);
            if (!merged)
                code_err(clause->body, "The type of this branch is %T, which conflicts with the earlier branch type of %T",
                         clause_type, overall_t);
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
                         "I was expecting this block to have a %T value (based on earlier clauses), but it actually has a %T value.",
                         overall_t, else_t);
            return merged;
        } else {
            CORD unhandled = CORD_EMPTY;
            for (match_t *m = matches; m; m = m->next) {
                if (!m->handled)
                    unhandled = unhandled ? CORD_all(unhandled, ", ", m->name) : m->name;
            }
            if (unhandled)
                code_err(ast, "This 'when' statement doesn't handle the tag(s): %s", CORD_to_const_char_star(unhandled));
            return overall_t;
        }
    }

    case While: case For: return Type(VoidType);
    case InlineCCode: {
        type_ast_t *type_ast = Match(ast, InlineCCode)->type;
        return type_ast ? parse_type_ast(env, type_ast) : Type(VoidType);
    }
    case Unknown: code_err(ast, "I can't figure out the type of: %W", ast);
    }
    code_err(ast, "I can't figure out the type of: %W", ast);
}

bool is_discardable(env_t *env, ast_t *ast)
{
    switch (ast->tag) {
    case UpdateAssign: case Assign: case Declare: case FunctionDef: case StructDef: case EnumDef:
    case LangDef: case Use: case Import:
        return true;
    default: break;
    }
    type_t *t = get_type(env, ast);
    return (t->tag == VoidType || t->tag == AbortType || t->tag == ReturnType);
}

type_t *get_file_type(env_t *env, const char *path)
{
    ast_t *ast = parse_file(path, NULL);
    if (!ast) compiler_err(NULL, NULL, NULL, "Couldn't parse file: %s", path);

    arg_t *ns_fields = NULL;
    for (ast_list_t *stmts = Match(ast, Block)->statements; stmts; stmts = stmts->next) {
        ast_t *stmt = stmts->ast;
      doctest_inner:
        switch (stmt->tag) {
        case Declare: {
            auto decl = Match(stmt, Declare);
            const char *name = Match(decl->var, Var)->name;
            type_t *t = get_type(env, decl->value);
            ns_fields = new(arg_t, .name=name, .type=t, .next=ns_fields);
            break;
        }
        case FunctionDef: {
            type_t *t = get_function_def_type(env, stmt);
            const char *name = Match(Match(stmt, FunctionDef)->name, Var)->name;
            ns_fields = new(arg_t, .name=name, .type=t, .next=ns_fields);
            break;
        }
        case DocTest: {
            stmt = Match(stmt, DocTest)->expr;
            goto doctest_inner;
        }
        default: break;
        }
    }
    return Type(StructType, .name=path, .fields=ns_fields);
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

bool can_be_mutated(env_t *env, ast_t *ast)
{
    switch (ast->tag) {
    case Var: return true;
    case FieldAccess: {
        auto access = Match(ast, FieldAccess);
        type_t *fielded_type = get_type(env, access->fielded);
        if (fielded_type->tag == PointerType) {
            auto ptr = Match(fielded_type, PointerType);
            return !ptr->is_readonly;
        }
        return can_be_mutated(env, access->fielded);
    }
    case Index: {
        auto index = Match(ast, Index);
        type_t *indexed_type = get_type(env, index->indexed);
        if (indexed_type->tag == PointerType) {
            auto ptr = Match(indexed_type, PointerType);
            return !ptr->is_readonly;
        }
        return false;
    }
    default: return false;
    }
}

type_t *parse_type_string(env_t *env, const char *str)
{
    type_ast_t *ast = parse_type_str(str);
    return ast ? parse_type_ast(env, ast) : NULL;
}

bool is_constant(env_t *env, ast_t *ast)
{
    switch (ast->tag) {
    case Bool: case Int: case Num: case Nil: case TextLiteral: return true;
    case TextJoin: {
        auto text = Match(ast, TextJoin);
        return !text->children || !text->children->next;
    }
    case Not: return is_constant(env, Match(ast, Not)->value);
    case Negative: return is_constant(env, Match(ast, Negative)->value);
    case BinaryOp: {
        auto binop = Match(ast, BinaryOp);
        switch (binop->op) {
        case BINOP_UNKNOWN: case BINOP_POWER: case BINOP_CONCAT: case BINOP_MIN: case BINOP_MAX: case BINOP_CMP:
            return false;
        default:
            return is_constant(env, binop->lhs) && is_constant(env, binop->rhs);
        }
    }
    case Use: case Import: return true;
    case FunctionCall: {
        // Constructors are allowed:
        auto call = Match(ast, FunctionCall);
        if (call->fn->tag != Var) return false;
        binding_t *b = get_binding(env, Match(call->fn, Var)->name);
        if (b == NULL || b->type->tag != TypeInfoType) return false;
        for (arg_ast_t *arg = call->args; arg; arg = arg->next) {
            if (!is_constant(env, arg->value))
                return false;
        }
        return true;
    }
    case InlineCCode: return true;
    default: return false;
    }
}

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
