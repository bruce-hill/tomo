// A Read-Evaluate-Print-Loop
#include <dlfcn.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "stdlib/tomo.h"
#include "stdlib/util.h"
#include "stdlib/print.h"
#include "typecheck.h"
#include "parse.h"

typedef union {
    int8_t i8;
    int16_t i16;
    int32_t i32;
    int64_t i64;
    Int_t integer;
    double n64;
    float n32;
} number_t;

static jmp_buf on_err;

static void run(env_t *env, ast_t *ast);
static void eval(env_t *env, ast_t *ast, void *dest);

typedef struct {
    type_t *type;
    void *value;
} repl_binding_t;

static PUREFUNC repl_binding_t *get_repl_binding(env_t *env, const char *name)
{
    repl_binding_t *b = Table$str_get(*env->locals, name);
    return b;
}

void repl(void)
{
    env_t *env = global_env(true);
    size_t buf_size = 0;
    char *line = NULL;
    ssize_t len = 0;

    if (setjmp(on_err))
        printf("\n");

    printf("\x1b[33;1m>>\x1b[m ");
    fflush(stdout);

    while ((len=getline(&line, &buf_size, stdin)) >= 0) {
        if (len > 1) {
            char *code = line;
            if (starts_with(line, "if ") || starts_with(line, "for ") || starts_with(line, "while ")
                || starts_with(line, "func ") || starts_with(line, "struct ") || starts_with(line, "lang ")) {
                printf("\x1b[33;1m..\x1b[m ");
                fflush(stdout);
                code = GC_strdup(line);
                while ((len=getline(&line, &buf_size, stdin)) >= 0) {
                    if (len == 1) break;
                    code = String(code, line);
                    printf("\x1b[33;1m..\x1b[m ");
                    fflush(stdout);
                }
            } else {
                code = String("func main(): >> ", code);
            }
            ast_t *ast = parse_file(String("<code>", code), &on_err);
            ast_t *doctest = Match(Match(Match(ast, Block)->statements->ast, FunctionDef)->body, Block)->statements->ast;
            if (doctest->tag == DocTest) doctest->__data.DocTest.skip_source = 1;
            run(env, doctest);
        }
        printf("\x1b[33;1m>>\x1b[m ");
        fflush(stdout);
    }
    if (line) free(line);
    printf("\n");
}

// __attribute__((noreturn, format(printf, 2, 3)))
// static void repl_err(ast_t *node, const char *fmt, ...)
// {
//     bool color = isatty(STDERR_FILENO) && !getenv("NO_COLOR");
//     if (color)
//         fputs("\x1b[31;7;1m", stderr);
//     if (node)
//         fprintf(stderr, "%s:%ld.%ld: ", node->file->relative_filename, get_line_number(node->file, node->start),
//                 get_line_column(node->file, node->start));
//     va_list args;
//     va_start(args, fmt);
//     vfprintf(stderr, fmt, args);
//     va_end(args);
//     if (color)
//         fputs(" \x1b[m", stderr);
//     fputs("\n\n", stderr);
//     if (node)
//         highlight_error(node->file, node->start, node->end, "\x1b[31;1m", 2, color);

    // longjmp(on_err, 1);
// }

const TypeInfo_t *type_to_type_info(type_t *t)
{
    switch (t->tag) {
    case AbortType: return &Abort$info;
    case ReturnType: print_err("Shouldn't be getting a typeinfo for ReturnType");
    case VoidType: return &Void$info;
    case MemoryType: return &Memory$info;
    case BoolType: return &Bool$info;
    case BigIntType: return &Int$info;
    case IntType:
        switch (Match(t, IntType)->bits) {
        case TYPE_IBITS64: return &Int64$info;
        case TYPE_IBITS32: return &Int32$info;
        case TYPE_IBITS16: return &Int16$info;
        case TYPE_IBITS8: return &Int8$info;
        default: print_err("Invalid bits");
        }
    case NumType:
        switch (Match(t, NumType)->bits) {
        case TYPE_NBITS64: return &Num$info;
        case TYPE_NBITS32: return &Num32$info;
        default: print_err("Invalid bits");
        }
    case TextType: return &Text$info;
    case ListType: {
        const TypeInfo_t *item_info = type_to_type_info(Match(t, ListType)->item_type);
        TypeInfo_t list_info = *List$info(item_info);
        return memcpy(GC_MALLOC(sizeof(TypeInfo_t)), &list_info, sizeof(TypeInfo_t));
    }
    case TableType: {
        const TypeInfo_t *key_info = type_to_type_info(Match(t, TableType)->key_type);
        const TypeInfo_t *value_info = type_to_type_info(Match(t, TableType)->value_type);
        const TypeInfo_t table_info = *Table$info(key_info, value_info);
        return memcpy(GC_MALLOC(sizeof(TypeInfo_t)), &table_info, sizeof(TypeInfo_t));
    }
    case PointerType: {
        DeclareMatch(ptr, t, PointerType);
        CORD sigil = ptr->is_stack ? "&" : "@";
        const TypeInfo_t *pointed_info = type_to_type_info(ptr->pointed);
        const TypeInfo_t pointer_info = *Pointer$info(sigil, pointed_info);
        return memcpy(GC_MALLOC(sizeof(TypeInfo_t)), &pointer_info, sizeof(TypeInfo_t));
    }
    default: print_err("Unsupported type: ", type_to_str(t));
    }
    return NULL;
}

static PUREFUNC void *get_address(env_t *env, ast_t *ast)
{
    switch (ast->tag) {
    case Var: {
        repl_binding_t *b = get_repl_binding(env, Match(ast, Var)->name);
        if (!b) print_err("No such variable");
        return b->value;
    }
    default: print_err("Address not implemented for ", ast_to_xml_str(ast));
    }
    return NULL;
}

static Int_t ast_to_int(env_t *env, ast_t *ast)
{
    type_t *t = get_type(env, ast);
    switch (t->tag) {
    case BigIntType: {
        number_t num;
        eval(env, ast, &num);
        return num.integer;
    }
    case IntType: {
        number_t num;
        eval(env, ast, &num);
        switch (Match(t, IntType)->bits) {
        case TYPE_IBITS64: return Int$from_int64((int64_t)num.i64);
        case TYPE_IBITS32: return Int$from_int32(num.i32);
        case TYPE_IBITS16: return Int$from_int16(num.i16);
        case TYPE_IBITS8: return Int$from_int8(num.i8);
        default: print_err("Invalid int bits");
        }
    }
    default: print_err("Cannot convert to integer");
    }
    return I(0);
}

// static double ast_to_num(env_t *env, ast_t *ast)
// {
//     type_t *t = get_type(env, ast);
//     switch (t->tag) {
//     case BigIntType: case IntType: {
//         number_t num;
//         eval(env, ast, &num);
//         if (t->tag == BigIntType)
//             return Num$from_int(num.integer, false);
//         switch (Match(t, IntType)->bits) {
//         case TYPE_IBITS64: return Num$from_int64(num.i64, false);
//         case TYPE_IBITS32: return Num$from_int32(num.i32);
//         case TYPE_IBITS16: return Num$from_int16(num.i16);
//         case TYPE_IBITS8: return Num$from_int8(num.i8);
//         default: print_err("Invalid int bits");
//         }
//     }
//     case NumType: {
//         number_t num;
//         eval(env, ast, &num);
//         return Match(t, NumType)->bits == TYPE_NBITS32 ? (double)num.n32 : (double)num.n64;
//     }
//     default: print_err("Cannot convert to number");
//     }
// }

static Text_t obj_to_text(type_t *t, const void *obj, bool use_color)
{
    const TypeInfo_t *info = type_to_type_info(t);
    return generic_as_text(obj, use_color, info);
}

void run(env_t *env, ast_t *ast)
{
    switch (ast->tag) {
    case Declare: {
        DeclareMatch(decl, ast, Declare);
        const char *name = Match(decl->var, Var)->name;
        type_t *type = get_type(env, decl->value);
        repl_binding_t *binding = new(repl_binding_t, .type=type, .value=GC_MALLOC(type_size(type)));
        eval(env, decl->value, binding->value);
        Table$str_set(env->locals, name, binding);
        break;
    }
    case Assign: {
        DeclareMatch(assign, ast, Assign);
        for (ast_list_t *val = assign->values, *target = assign->targets; val && target; val = val->next, target = target->next) {
            type_t *t_target = get_type(env, target->ast);
            type_t *t_val = get_type(env, val->ast);
            if (!type_eq(t_val, t_target))
                print_err("This value has type ", type_to_str(t_val), " but I expected a ", type_to_str(t_target));

            if (!can_be_mutated(env, target->ast)) {
                if (target->ast->tag == Index || target->ast->tag == FieldAccess) {
                    // ast_t *subject = target->ast->tag == Index ? Match(target->ast, Index)->indexed : Match(target->ast, FieldAccess)->fielded;
                    print_err("This is an immutable value, you can't assign to it");
                } else {
                    print_err("This is a value of type ", type_to_str(get_type(env, target->ast)), " and can't be assigned to");
                }
            }
        }
        for (ast_list_t *val = assign->values, *target = assign->targets; val && target; val = val->next, target = target->next) {
            switch (target->ast->tag) {
            case Var: {
                void *dest = get_address(env, target->ast);
                eval(env, val->ast, dest);
                break;
            }
            // case Index: {
            //     DeclareMatch(index, target->ast, Index);
            //     type_t *obj_t = get_type(env, index->indexed);
            //     TypeInfo_t *table_info = type_to_type_info(t);
            // }
            default: print_err("Assignment not implemented: ", ast_to_xml_str(target->ast));
            }
        }
        break;
    }
    case DocTest: {
        DeclareMatch(doctest, ast, DocTest);
        type_t *t = get_type(env, doctest->expr);
        size_t size = t ? type_size(t) : 0;
        if (size == 0) {
            run(env, doctest->expr);
        } else {
            void *value = GC_MALLOC(size);
            eval(env, doctest->expr, value);
            Text_t text = obj_to_text(t, value, true);
            print("= ", text, " \x1b[2m: ", type_to_str(t), "\x1b[m");
            fflush(stdout);
        }
        break;
    }
    case Block: {
        DeclareMatch(block, ast, Block);
        for (ast_list_t *stmt = block->statements; stmt; stmt = stmt->next) {
            run(env, stmt->ast);
        }
        break;
    }
    // UpdateAssign,
    // FunctionDef,
    // For, While, If, When,
    // Skip, Stop, Pass,
    // Return,
    // Extern,
    // StructDef, EnumDef, LangDef,
    // DocTest,
    // Use,
    // LinkerDirective,
    // InlineCCode,
    case If: {
        DeclareMatch(if_, ast, If);
        bool condition;
        type_t *cond_t = get_type(env, if_->condition);
        assert(cond_t->tag == BoolType);
        eval(env, if_->condition, &condition);
        if (condition) {
            run(env, if_->body);
        } else if (if_->else_body) {
            run(env, if_->else_body);
        }
        break;
    }
    case While: {
        DeclareMatch(while_, ast, While);
        bool condition;
        type_t *cond_t = get_type(env, while_->condition);
        assert(cond_t->tag == BoolType);
        for (;;) {
            eval(env, while_->condition, &condition);
            if (!condition) break;
            run(env, while_->body);
        }
        break;
    }
    // case For: {
    //     DeclareMatch(for_, ast, For);
    // }
    default: {
        eval(env, ast, NULL);
        break;
    }
    }
}

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstack-protector"
#endif
void eval(env_t *env, ast_t *ast, void *dest)
{
    type_t *t = get_type(env, ast);
    size_t size = type_size(t);
    switch (ast->tag) {
    case None:
        if (dest) *(void**)dest = 0;
        break;
    case Bool:
        if (dest) *(bool*)dest = Match(ast, Bool)->b;
        break;
    case Var: {
        if (!dest) return;
        repl_binding_t *b = get_repl_binding(env, Match(ast, Var)->name);
        if (!b)
            print_err("No such variable: ", Match(ast, Var)->name);
        memcpy(dest, b->value, size);
        break;
    }
    case Int: {
        if (!dest) return;
        *(Int_t*)dest = Int$parse(Text$from_str(Match(ast, Int)->str)); break;
        break;
    }
    case Num: {
        if (!dest) return;
        *(double*)dest = Match(ast, Num)->n; break;
        break;
    }
    case TextLiteral:
        if (dest) *(CORD*)dest = Match(ast, TextLiteral)->cord;
        break;
    case TextJoin: {
        CORD ret = CORD_EMPTY;
        for (ast_list_t *chunk = Match(ast, TextJoin)->children; chunk; chunk = chunk->next) {
            type_t *chunk_t = get_type(env, chunk->ast);
            if (chunk_t->tag == TextType) {
                CORD c;
                eval(env, chunk->ast, &c);
                ret = CORD_cat(ret, c);
            } else {
                size_t chunk_size = type_size(chunk_t);
                char buf[chunk_size];
                eval(env, chunk->ast, buf);
                ret = CORD_cat(ret, Text$as_c_string(obj_to_text(chunk_t, buf, false)));
            }
        }
        if (dest) *(CORD*)dest = ret;
        break;
    }
    case Index: {
        DeclareMatch(index, ast, Index);
        type_t *indexed_t = get_type(env, index->indexed);
        // type_t *index_t = get_type(env, index->index);
        switch (indexed_t->tag) {
        case ListType: {
            List_t list;
            eval(env, index->indexed, &list);
            int64_t raw_index = Int64$from_int(ast_to_int(env, index->index), false);
            int64_t index_int = raw_index;
            if (index_int < 1) index_int = list.length + index_int + 1;
            if (index_int < 1 || index_int > list.length)
                print_err(raw_index,
                         " is an invalid index for a list with length ", (int64_t)list.length);
            size_t item_size = type_size(Match(indexed_t, ListType)->item_type);
            memcpy(dest, list.data + list.stride*(index_int-1), item_size);
            break;
        }
        case TableType: {
            Table_t table;
            eval(env, index->indexed, &table);
            type_t *key_type = Match(indexed_t, TableType)->key_type;
            size_t key_size = type_size(key_type);
            char key_buf[key_size];
            eval(env, index->index, key_buf);
            const TypeInfo_t *table_info = type_to_type_info(indexed_t);
            memcpy(dest, Table$get(table, key_buf, table_info), key_size);
            break;
        }
        case PointerType: {
            DeclareMatch(ptr, indexed_t, PointerType);
            size_t pointed_size = type_size(ptr->pointed);
            void *pointer;
            eval(env, index->indexed, &pointer);
            memcpy(dest, pointer, pointed_size);
            break;
        }
        default: print_err("Indexing is not supported for ", type_to_str(indexed_t));
        }
        break;
    }
    case List: {
        assert(t->tag == ListType);
        List_t list = {};
        size_t item_size = type_size(Match(t, ListType)->item_type);
        char item_buf[item_size];
        for (ast_list_t *item = Match(ast, List)->items; item; item = item->next) {
            eval(env, item->ast, item_buf);
            List$insert(&list, item_buf, I(0), (int64_t)type_size(Match(t, ListType)->item_type));
        }
        memcpy(dest, &list, sizeof(List_t));
        break;
    }
    case Table: {
        assert(t->tag == TableType);
        DeclareMatch(table_ast, ast, Table);
        Table_t table = {};
        size_t key_size = type_size(Match(t, TableType)->key_type);
        size_t value_size = type_size(Match(t, TableType)->value_type);
        char key_buf[key_size];
        char value_buf[value_size];
        const TypeInfo_t *table_info = type_to_type_info(t);
        assert(table_info->tag == TableInfo);
        for (ast_list_t *entry = table_ast->entries; entry; entry = entry->next) {
            DeclareMatch(e, entry->ast, TableEntry);
            eval(env, e->key, key_buf);
            eval(env, e->value, value_buf);
            Table$set(&table, key_buf, value_buf, table_info);
        }
        if (table_ast->fallback)
            eval(env, table_ast->fallback, &table.fallback);
        memcpy(dest, &table, sizeof(Table_t));
        break;
    }
    case Block: {
        DeclareMatch(block, ast, Block);
        for (ast_list_t *stmt = block->statements; stmt; stmt = stmt->next) {
            if (stmt->next)
                run(env, stmt->ast);
            else
                eval(env, stmt->ast, dest);
        }
        break;
    }
    default:
        print_err("Eval not implemented for ", ast_to_xml_str(ast));
    }
}
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
