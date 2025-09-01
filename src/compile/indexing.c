// This file defines how to compile indexing like `list[i]` or `ptr[]`

#include "../ast.h"
#include "../config.h"
#include "../environment.h"
#include "../stdlib/text.h"
#include "../stdlib/util.h"
#include "../typecheck.h"
#include "compilation.h"

public
Text_t compile_indexing(env_t *env, ast_t *ast) {
    DeclareMatch(indexing, ast, Index);
    type_t *indexed_type = get_type(env, indexing->indexed);
    if (!indexing->index) {
        if (indexed_type->tag != PointerType)
            code_err(ast, "Only pointers can use the '[]' operator to "
                          "dereference "
                          "the entire value.");
        DeclareMatch(ptr, indexed_type, PointerType);
        if (ptr->pointed->tag == ListType) {
            return Texts("*({ List_t *list = ", compile(env, indexing->indexed), "; LIST_INCREF(*list); list; })");
        } else if (ptr->pointed->tag == TableType || ptr->pointed->tag == SetType) {
            return Texts("*({ Table_t *t = ", compile(env, indexing->indexed), "; TABLE_INCREF(*t); t; })");
        } else {
            return Texts("*(", compile(env, indexing->indexed), ")");
        }
    }

    type_t *container_t = value_type(indexed_type);
    type_t *index_t = get_type(env, indexing->index);
    if (container_t->tag == ListType) {
        if (index_t->tag != IntType && index_t->tag != BigIntType && index_t->tag != ByteType)
            code_err(indexing->index, "Lists can only be indexed by integers, not ", type_to_str(index_t));
        type_t *item_type = Match(container_t, ListType)->item_type;
        Text_t list = compile_to_pointer_depth(env, indexing->indexed, 0, false);
        file_t *f = indexing->index->file;
        Text_t index_code =
            indexing->index->tag == Int
                ? compile_int_to_type(env, indexing->index, Type(IntType, .bits = TYPE_IBITS64))
                : (index_t->tag == BigIntType ? Texts("Int64$from_int(", compile(env, indexing->index), ", no)")
                                              : Texts("(Int64_t)(", compile(env, indexing->index), ")"));
        if (indexing->unchecked)
            return Texts("List_get_unchecked(", compile_type(item_type), ", ", list, ", ", index_code, ")");
        else
            return Texts("List_get(", compile_type(item_type), ", ", list, ", ", index_code, ", ",
                         (int64_t)(indexing->index->start - f->text), ", ", (int64_t)(indexing->index->end - f->text),
                         ")");
    } else if (container_t->tag == TableType) {
        DeclareMatch(table_type, container_t, TableType);
        if (indexing->unchecked) code_err(ast, "Table indexes cannot be unchecked");
        if (table_type->default_value) {
            return Texts("Table$get_or_default(", compile_to_pointer_depth(env, indexing->indexed, 0, false), ", ",
                         compile_type(table_type->key_type), ", ", compile_type(table_type->value_type), ", ",
                         compile(env, indexing->index), ", ",
                         compile_to_type(env, table_type->default_value, table_type->value_type), ", ",
                         compile_type_info(container_t), ")");
        } else {
            return Texts("Table$get_optional(", compile_to_pointer_depth(env, indexing->indexed, 0, false), ", ",
                         compile_type(table_type->key_type), ", ", compile_type(table_type->value_type), ", ",
                         compile(env, indexing->index),
                         ", "
                         "_, ",
                         promote_to_optional(table_type->value_type, Text("(*_)")), ", ",
                         compile_none(table_type->value_type), ", ", compile_type_info(container_t), ")");
        }
    } else if (container_t->tag == TextType) {
        return Texts("Text$cluster(", compile_to_pointer_depth(env, indexing->indexed, 0, false), ", ",
                     compile_to_type(env, indexing->index, Type(BigIntType)), ")");
    } else {
        code_err(ast, "Indexing is not supported for type: ", type_to_str(container_t));
    }
}
