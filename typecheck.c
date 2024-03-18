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
        type_t *t = Table_str_get(*env->types, name);
        if (t) return t;
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
        type_t *ret_t = parse_type_ast(env, fn->ret);
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

void bind_statement(env_t *env, ast_t *statement)
{
    switch (statement->tag) {
    case DocTest: {
        bind_statement(env, Match(statement, DocTest)->expr);
        break;
    }
    case Declare: {
        auto decl = Match(statement, Declare);
        type_t *type = get_type(env, decl->value);
        const char *name = Match(decl->var, Var)->name;
        CORD code = CORD_cat(env->scope_prefix, name);
        set_binding(env, name, new(binding_t, .type=type, .code=code));
        break;
    }
    case FunctionDef: {
        auto def = Match(statement, FunctionDef);
        type_t *type = get_function_def_type(env, statement);
        const char *name = Match(def->name, Var)->name;
        CORD code = CORD_all(env->scope_prefix, name);
        set_binding(env, name, new(binding_t, .type=type, .code=code));
        break;
    }
    case StructDef: {
        auto def = Match(statement, StructDef);

        env_t *ns_env = namespace_env(env, def->name);

        arg_t *fields = NULL;
        type_t *type = Type(StructType, .name=def->name, .fields=fields, .opaque=true); // placeholder
        Table_str_set(env->types, def->name, type);
        for (arg_ast_t *field_ast = def->fields; field_ast; field_ast = field_ast->next) {
            type_t *field_t = get_arg_ast_type(env, field_ast);
            if ((field_t->tag == StructType && Match(field_t, StructType)->opaque)
                || (field_t->tag == EnumType && Match(field_t, EnumType)->opaque))
                code_err(field_ast->type, "This type is recursive and would create an infinitely sized struct. Try using a pointer.");
            fields = new(arg_t, .name=field_ast->name, .type=field_t, .default_val=field_ast->value, .next=fields);
        }
        REVERSE_LIST(fields);
        type->__data.StructType.fields = fields; // populate placeholder
        type->__data.StructType.opaque = false;
        
        for (ast_list_t *stmt = def->namespace ? Match(def->namespace, Block)->statements : NULL; stmt; stmt = stmt->next) {
            bind_statement(ns_env, stmt->ast);
        }

        type_t *typeinfo_type = Type(TypeInfoType, .name=def->name, .type=type);
        Table_str_set(env->globals, def->name, new(binding_t, .type=typeinfo_type, .code=CORD_all(env->file_prefix, def->name)));
        break;
    }
    case EnumDef: {
        auto def = Match(statement, EnumDef);

        env_t *ns_env = namespace_env(env, def->name);

        tag_t *tags = NULL;
        type_t *type = Type(EnumType, .name=def->name, .tags=tags, .opaque=true); // placeholder
        Table_str_set(env->types, def->name, type);
        for (tag_ast_t *tag_ast = def->tags; tag_ast; tag_ast = tag_ast->next) {
            arg_t *fields = NULL;
            for (arg_ast_t *field_ast = tag_ast->fields; field_ast; field_ast = field_ast->next) {
                type_t *field_t = get_arg_ast_type(env, field_ast);
                if ((field_t->tag == StructType && Match(field_t, StructType)->opaque)
                    || (field_t->tag == EnumType && Match(field_t, EnumType)->opaque))
                    code_err(field_ast->type, "This type is recursive and would create an infinitely sized struct. Try using a pointer.");
                fields = new(arg_t, .name=field_ast->name, .type=field_t, .default_val=field_ast->value, .next=fields);
            }
            REVERSE_LIST(fields);
            type_t *tag_type = Type(StructType, .name=heap_strf("%s$%s", def->name, tag_ast->name), .fields=fields);
            tags = new(tag_t, .name=tag_ast->name, .tag_value=tag_ast->value, .type=tag_type, .next=tags);
        }
        REVERSE_LIST(tags);
        type->__data.EnumType.tags = tags;
        type->__data.EnumType.opaque = false;

        for (tag_t *tag = tags; tag; tag = tag->next) {
            if (Match(tag->type, StructType)->fields) { // Constructor:
                type_t *constructor_t = Type(FunctionType, .args=Match(tag->type, StructType)->fields, .ret=type);
                set_binding(ns_env, tag->name, new(binding_t, .type=constructor_t, .code=CORD_all(env->file_prefix, def->name, "$tagged$", tag->name)));
            } else { // Empty singleton value:
                set_binding(ns_env, tag->name, new(binding_t, .type=type, .code=CORD_all(env->file_prefix, def->name, "$tagged$", tag->name)));
            }
            Table_str_set(env->types, heap_strf("%s$%s", def->name, tag->name), tag->type);
        }
        
        type_t *typeinfo_type = Type(TypeInfoType, .name=def->name, .type=type);
        Table_str_set(env->globals, def->name, new(binding_t, .type=typeinfo_type));

        for (ast_list_t *stmt = def->namespace ? Match(def->namespace, Block)->statements : NULL; stmt; stmt = stmt->next) {
            bind_statement(ns_env, stmt->ast);
        }

        break;
    }
    case LangDef: {
        auto def = Match(statement, LangDef);

        type_t *type = Type(TextType, .lang=def->name);
        Table_str_set(env->types, def->name, type);
        env_t *ns_env = namespace_env(env, def->name);

        set_binding(ns_env, "from_unsafe_text",
                    new(binding_t, .type=Type(FunctionType, .args=new(arg_t, .name="text", .type=Type(TextType)), .ret=type),
                        .code=CORD_all("(", env->file_prefix, def->name, "_t)")));
        set_binding(ns_env, "text_content",
                    new(binding_t, .type=Type(FunctionType, .args=new(arg_t, .name="text", .type=Type(TextType)), .ret=type),
                        .code="(Text_t)"));

        for (ast_list_t *stmt = def->namespace ? Match(def->namespace, Block)->statements : NULL; stmt; stmt = stmt->next)
            bind_statement(ns_env, stmt->ast);

        type_t *typeinfo_type = Type(TypeInfoType, .name=def->name, .type=type);
        Table_str_set(env->globals, def->name, new(binding_t, .type=typeinfo_type));
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
        code_err(self, "No such method: %s", name);
    return b->type;
}

type_t *get_type(env_t *env, ast_t *ast)
{
    if (!ast) return NULL;
    switch (ast->tag) {
    case Nil: {
        type_t *pointed = parse_type_ast(env, Match(ast, Nil)->type);
        return Type(PointerType, .is_optional=true, .pointed=pointed);
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
    case TextLiteral: return Type(TextType);
    case TextJoin: {
        return Type(TextType, .lang=Match(ast, TextJoin)->lang);
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
                    scope = for_scope(scope, FakeAST(For, .iter=comp->iter, .index=comp->key, .value=comp->value));
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
                    scope = for_scope(scope, FakeAST(For, .iter=comp->iter, .index=comp->key, .value=comp->value));
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
        env_t *scope = for_scope(env, FakeAST(For, .iter=comp->iter, .index=comp->key, .value=comp->value));
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
        if (fielded_t->tag == TypeInfoType) {
            auto info = Match(fielded_t, TypeInfoType);
            table_t *namespace = Table_str_get(*env->type_namespaces, info->name);
            if (!namespace) code_err(access->fielded, "I couldn't find a namespace for this type");
            binding_t *b = Table_str_get(*namespace, access->field);
            if (!b) code_err(ast, "I couldn't find the field '%s' on this type", access->field);
            return b->type;
        }
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
        if (call->extern_return_type)
            return parse_type_ast(env, call->extern_return_type);
        type_t *fn_type_t = get_type(env, call->fn);
        if (!fn_type_t)
            code_err(call->fn, "I couldn't find this function");

        if (fn_type_t->tag == TypeInfoType) {
            type_t *t = Match(fn_type_t, TypeInfoType)->type;
            if (t->tag == StructType)
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
            else if (streq(call->name, "shuffle")) return Type(VoidType);
            else if (streq(call->name, "random"))
                return Type(PointerType, .pointed=Match(self_value_t, ArrayType)->item_type, .is_optional=true, .is_readonly=true);
            else if (streq(call->name, "clear")) return Type(VoidType);
            else if (streq(call->name, "slice")) return self_value_t;
            else code_err(ast, "There is no '%s' method for arrays", call->name);
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
        type_t *t = parse_type_ast(env, Match(ast, Extern)->type);
        return Match(ast, Extern)->address ? Type(PointerType, .pointed=t, .is_optional=false) : t;
    }
    case Declare: case Assign: case DocTest: case LinkerDirective: {
        return Type(VoidType);
    }
    case Use: {
        const char *path = Match(ast, Use)->path;
        return Type(PointerType, .pointed=get_file_type(env, path));
    }
    case Return: case Stop: case Skip: {
        return Type(AbortType);
    }
    case Pass: return Type(VoidType);
    case Length: return Type(IntType, .bits=64);
    case Negative: {
        type_t *t = get_type(env, Match(ast, Negative)->value);
        if (t->tag == IntType || t->tag == NumType)
            return t;
        code_err(ast, "I only know how to get negatives of numeric types, not %T", t);
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

        switch (binop->op) {
        case BINOP_AND: {
            if (lhs_t->tag == BoolType && rhs_t->tag == BoolType) {
                return lhs_t;
            } else if (lhs_t->tag == BoolType && rhs_t->tag == AbortType) {
                return lhs_t;
            } else if (rhs_t->tag == AbortType) {
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
            } else if (lhs_t->tag == BoolType && rhs_t->tag == AbortType) {
                return lhs_t;
            } else if (lhs_t->tag == IntType && rhs_t->tag == IntType) {
                return get_math_type(env, ast, lhs_t, rhs_t);
            } else if (lhs_t->tag == PointerType) {
                auto lhs_ptr = Match(lhs_t, PointerType);
                if (rhs_t->tag == AbortType) {
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
        type_t *value_t = iteration_value_type(iter_t);
        env_t *scope = fresh_scope(env);
        set_binding(scope, "$reduction", new(binding_t, .type=value_t, .code="$reduction"));
        set_binding(scope, "$iter_value", new(binding_t, .type=value_t, .code="$iter_value"));
        type_t *t = get_type(scope, reduction->combination);
        if (!reduction->fallback)
            return t;
        type_t *fallback_t = get_type(env, reduction->fallback);
        if (fallback_t->tag == AbortType)
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
        if (has_stack_memory(ret))
            code_err(ast, "Functions can't return stack references because the reference may outlive its stack frame.");
        return Type(ClosureType, Type(FunctionType, .args=args, .ret=ret));
    }

    case FunctionDef: case StructDef: case EnumDef: case LangDef: {
        return Type(VoidType);
    }

    case If: {
        auto if_ = Match(ast, If);
        type_t *true_t;
        if (if_->condition->tag == Declare) {
            auto decl = Match(if_->condition, Declare);
            env_t *scope = fresh_scope(env);
            type_t *var_t = get_type(env, decl->value);
            if (var_t->tag == PointerType) {
                auto ptr = Match(var_t, PointerType);
                var_t = Type(PointerType, .pointed=ptr->pointed, .is_optional=false, .is_stack=ptr->is_stack, .is_readonly=ptr->is_readonly);
            }
            CORD var = Match(decl->var, Var)->name;
            set_binding(scope, CORD_to_const_char_star(var), new(binding_t, .type=var_t));
            true_t = get_type(scope, if_->body);
        } else {
            true_t = get_type(env, if_->body);
        }

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
        if (subject_t->tag != EnumType)
            code_err(when->subject, "'when' statements are only for enum types, not %T", subject_t);

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

        type_t *overall_t = NULL;
        for (when_clause_t *clause = when->clauses; clause; clause = clause->next) {
            const char *tag_name = Match(clause->tag_name, Var)->name;
            type_t *tag_type = NULL;
            for (match_t *m = matches; m; m = m->next) {
                if (streq(m->name, tag_name)) {
                    if (m->handled)
                        code_err(clause->tag_name, "This tag was already handled earlier");
                    m->handled = true;
                    tag_type = m->type;
                    break;
                }
            }

            if (!tag_type)
                code_err(clause->tag_name, "This is not a valid tag for the type %T", subject_t);

            env_t *scope = env;
            if (clause->var) {
                scope = fresh_scope(scope);
                set_binding(scope, Match(clause->var, Var)->name, new(binding_t, .type=tag_type));
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
            if (!any_unhandled)
                code_err(when->else_body, "This 'else' block will never run because every tag is handled");

            type_t *else_t = get_type(env, when->else_body);
            type_t *merged = type_or_type(overall_t, else_t);
            if (!merged)
                code_err(when->else_body,
                         "I was expecting this block to have a %T value (based on earlier clauses), but it actually has a %T value.",
                         overall_t, else_t);
            // return merged;
            return Type(VoidType);
        } else {
            CORD unhandled = CORD_EMPTY;
            for (match_t *m = matches; m; m = m->next) {
                if (!m->handled)
                    unhandled = unhandled ? CORD_all(unhandled, ", ", m->name) : m->name;
            }
            if (unhandled)
                code_err(ast, "This 'when' statement doesn't handle the tag(s): %s", CORD_to_const_char_star(unhandled));
            // return overall_t;
            return Type(VoidType);
        }
    }

    case While: case For: return Type(VoidType);
    case InlineCCode: return Type(VoidType);
    case Unknown: code_err(ast, "I can't figure out the type of: %W", ast);
    }
    code_err(ast, "I can't figure out the type of: %s", ast_to_str(ast));
}

bool is_discardable(env_t *env, ast_t *ast)
{
    switch (ast->tag) {
    case UpdateAssign: case Assign: case Declare: case FunctionDef: case StructDef: case EnumDef: case LangDef: case Use:
        return true;
    default: break;
    }
    type_t *t = get_type(env, ast);
    return (t->tag == VoidType || t->tag == AbortType);
}

type_t *get_file_type(env_t *env, const char *path)
{
    // auto info = get_file_info(env, path);
    file_t *f = load_file(path);
    ast_t *ast = parse_file(f, NULL);
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

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
