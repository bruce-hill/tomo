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

void repl(void)
{
    env_t *env = new_compilation_unit(NULL);
    void *dl = dlopen("libtomo.so", RTLD_LAZY);
    if (!dl) errx(1, "I couldn't find libtomo.so in your library paths");

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
                    code = heap_strf("%s%s", code, line);
                    printf("\x1b[33;1m..\x1b[m ");
                    fflush(stdout);
                }
            } else {
                code = heap_strf("func main(): >> %s", code);
            }
            ast_t *ast = parse_file(heap_strf("<code>%s", code), &on_err);
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

__attribute__((noreturn, format(printf, 2, 3)))
static void repl_err(ast_t *node, const char *fmt, ...)
{
    bool color = isatty(STDERR_FILENO) && !getenv("NO_COLOR");
    if (color)
        fputs("\x1b[31;7;1m", stderr);
    if (node)
        fprintf(stderr, "%s:%ld.%ld: ", node->file->relative_filename, get_line_number(node->file, node->start),
                get_line_column(node->file, node->start));
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    if (color)
        fputs(" \x1b[m", stderr);
    fputs("\n\n", stderr);
    if (node)
        highlight_error(node->file, node->start, node->end, "\x1b[31;1m", 2, color);

    longjmp(on_err, 1);
}

const TypeInfo_t *type_to_type_info(type_t *t)
{
    switch (t->tag) {
    case AbortType: return &Abort$info;
    case ReturnType: errx(1, "Shouldn't be getting a typeinfo for ReturnType");
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
        default: errx(1, "Invalid bits");
        }
    case NumType:
        switch (Match(t, NumType)->bits) {
        case TYPE_NBITS64: return &Num$info;
        case TYPE_NBITS32: return &Num32$info;
        default: errx(1, "Invalid bits");
        }
    case TextType: return &Text$info;
    case ArrayType: {
        const TypeInfo_t *item_info = type_to_type_info(Match(t, ArrayType)->item_type);
        const TypeInfo_t array_info = {.size=sizeof(Array_t), .align=__alignof__(Array_t),
            .tag=ArrayInfo, .ArrayInfo.item=item_info};
        return memcpy(GC_MALLOC(sizeof(TypeInfo_t)), &array_info, sizeof(TypeInfo_t));
    }
    case TableType: {
        const TypeInfo_t *key_info = type_to_type_info(Match(t, TableType)->key_type);
        const TypeInfo_t *value_info = type_to_type_info(Match(t, TableType)->value_type);
        const TypeInfo_t table_info = {
            .size=sizeof(Table_t), .align=__alignof__(Table_t),
            .tag=TableInfo, .TableInfo.key=key_info, .TableInfo.value=value_info};
        return memcpy(GC_MALLOC(sizeof(TypeInfo_t)), &table_info, sizeof(TypeInfo_t));
    }
    case PointerType: {
        auto ptr = Match(t, PointerType);
        const char *sigil = ptr->is_view ? "&" : "@";
        const TypeInfo_t *pointed_info = type_to_type_info(ptr->pointed);
        const TypeInfo_t pointer_info = {.size=sizeof(void*), .align=__alignof__(void*),
            .tag=PointerInfo, .PointerInfo={.sigil=sigil, .pointed=pointed_info}};
        return memcpy(GC_MALLOC(sizeof(TypeInfo_t)), &pointer_info, sizeof(TypeInfo_t));
    }
    default: errx(1, "Unsupported type: %T", t);
    }
}

static void *get_address(env_t *env, ast_t *ast)
{
    switch (ast->tag) {
    case Var: {
        binding_t *b = get_binding(env, Match(ast, Var)->name);
        if (!b) repl_err(ast, "No such variable");
        return b->value;
    }
    default: errx(1, "Address not implemented for %W", ast);
    }
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
        case TYPE_IBITS64: return Int64_to_Int((int64_t)num.i64);
        case TYPE_IBITS32: return Int32_to_Int(num.i32);
        case TYPE_IBITS16: return Int16_to_Int(num.i16);
        case TYPE_IBITS8: return Int8_to_Int(num.i8);
        default: errx(1, "Invalid int bits");
        }
    }
    default: repl_err(NULL, "Cannot convert to integer");
    }
}

static double ast_to_num(env_t *env, ast_t *ast)
{
    type_t *t = get_type(env, ast);
    switch (t->tag) {
    case BigIntType: case IntType: {
        number_t num;
        eval(env, ast, &num);
        if (t->tag == BigIntType)
            return Int_to_Num(num.integer);
        switch (Match(t, IntType)->bits) {
        case TYPE_IBITS64: return (double)num.i64;
        case TYPE_IBITS32: return (double)num.i32;
        case TYPE_IBITS16: return (double)num.i16;
        case TYPE_IBITS8: return (double)num.i8;
        default: errx(1, "Invalid int bits");
        }
    }
    case NumType: {
        number_t num;
        eval(env, ast, &num);
        return Match(t, NumType)->bits == TYPE_NBITS32 ? (double)num.n32 : (double)num.n64;
    }
    default: repl_err(NULL, "Cannot convert to number");
    }
}

static Text_t obj_to_text(type_t *t, const void *obj, bool use_color)
{
    const TypeInfo_t *info = type_to_type_info(t);
    return generic_as_text(obj, use_color, info);
}

void run(env_t *env, ast_t *ast)
{
    switch (ast->tag) {
    case Declare: {
        auto decl = Match(ast, Declare);
        const char *name = Match(decl->var, Var)->name;
        type_t *type = get_type(env, decl->value);
        binding_t *binding = new(binding_t, .type=type, .value=GC_MALLOC(type_size(type)));
        eval(env, decl->value, binding->value);
        set_binding(env, name, binding);
        break;
    }
    case Assign: {
        auto assign = Match(ast, Assign);
        int64_t n = 0;
        for (ast_list_t *t = assign->targets; t; t = t->next)
            ++n;
        for (ast_list_t *val = assign->values, *target = assign->targets; val && target; val = val->next, target = target->next) {
            type_t *t_target = get_type(env, target->ast);
            type_t *t_val = get_type(env, val->ast);
            if (!type_eq(t_val, t_target))
                repl_err(target->ast, "This value has type %T but I expected a %T", t_val, t_target);

            if (!can_be_mutated(env, target->ast)) {
                if (target->ast->tag == Index || target->ast->tag == FieldAccess) {
                    ast_t *subject = target->ast->tag == Index ? Match(target->ast, Index)->indexed : Match(target->ast, FieldAccess)->fielded;
                    repl_err(subject, "This is an immutable value, you can't assign to it");
                } else {
                    repl_err(target->ast, "This is a value of type %T and can't be assigned to", get_type(env, target->ast));
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
            //     auto index = Match(target->ast, Index);
            //     type_t *obj_t = get_type(env, index->indexed);
            //     TypeInfo_t *table_info = type_to_type_info(t);
            // }
            default: errx(1, "Assignment not implemented: %W", target->ast);
            }
        }
        break;
    }
    case DocTest: {
        auto doctest = Match(ast, DocTest);
        type_t *t = get_type(env, doctest->expr);
        size_t size = t ? type_size(t) : 0;
        if (size == 0) {
            run(env, doctest->expr);
        } else {
            void *value = GC_MALLOC(size);
            eval(env, doctest->expr, value);
            Text_t text = obj_to_text(t, value, true);
            printf("= %k \x1b[2m: %T\x1b[m\n", &text, t);
            fflush(stdout);
        }
        break;
    }
    case Block: {
        auto block = Match(ast, Block);
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
        auto if_ = Match(ast, If);
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
        auto while_ = Match(ast, While);
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
    //     auto for_ = Match(ast, For);
    // }
    default: {
        eval(env, ast, NULL);
        break;
    }
    }
}

#pragma GCC diagnostic ignored "-Wstack-protector"
void eval(env_t *env, ast_t *ast, void *dest)
{
    type_t *t = get_type(env, ast);
    size_t size = type_size(t);
    switch (ast->tag) {
    case Null:
        if (dest) *(void**)dest = 0;
        break;
    case Bool:
        if (dest) *(bool*)dest = Match(ast, Bool)->b;
        break;
    case Var: {
        if (!dest) return;
        binding_t *b = get_binding(env, Match(ast, Var)->name);
        if (!b)
            repl_err(ast, "No such variable: %s", Match(ast, Var)->name);
        memcpy(dest, b->value, size);
        break;
    }
    case Int: {
        if (!dest) return;
        switch (Match(ast, Int)->bits) {
        case 0: *(Int_t*)dest = Int$from_text(Text$from_str(Match(ast, Int)->str)); break;
        case 64: *(int64_t*)dest = Int64$from_text(Text$from_str(Match(ast, Int)->str)).i; break;
        case 32: *(int32_t*)dest = Int32$from_text(Text$from_str(Match(ast, Int)->str)).i; break;
        case 16: *(int16_t*)dest = Int16$from_text(Text$from_str(Match(ast, Int)->str)).i; break;
        case 8: *(int8_t*)dest = Int8$from_text(Text$from_str(Match(ast, Int)->str)).i; break;
        default: errx(1, "Invalid int bits: %ld", Match(ast, Int)->bits);
        }
        break;
    }
    case Num: {
        if (!dest) return;
        switch (Match(ast, Num)->bits) {
        case 0: case 64: *(double*)dest = Match(ast, Num)->n; break;
        case 32: *(float*)dest = Match(ast, Num)->n; break;
        default: errx(1, "Invalid num bits: %ld", Match(ast, Num)->bits);
        }
        break;
    }
    case TextLiteral:
        if (dest) *(Text_t*)dest = Match(ast, TextLiteral)->text;
        break;
    case TextJoin: {
        Text_t ret = Text("");
        for (ast_list_t *chunk = Match(ast, TextJoin)->children; chunk; chunk = chunk->next) {
            type_t *chunk_t = get_type(env, chunk->ast);
            if (chunk_t->tag == TextType) {
                Text_t chunk_text;
                eval(env, chunk->ast, &chunk_text);
                ret = Texts(ret, chunk_text);
            } else {
                size_t chunk_size = type_size(chunk_t);
                char buf[chunk_size];
                eval(env, chunk->ast, buf);
                ret = Texts(ret, obj_to_text(chunk_t, buf, false));
            }
        }
        if (dest) *(Text_t*)dest = ret;
        break;
    }
    case BinaryOp: {
        auto binop = Match(ast, BinaryOp);
        if (t->tag == IntType || t->tag == BigIntType) {
#define CASE_OP(OP_NAME, method_name) case BINOP_##OP_NAME: {\
        Int_t lhs = ast_to_int(env, binop->lhs); \
        Int_t rhs = ast_to_int(env, binop->rhs); \
        Int_t result = Int$ ## method_name (lhs, rhs); \
        if (t->tag == BigIntType) {\
            *(Int_t*)dest = result; \
            return; \
        } \
        switch (Match(t, IntType)->bits) { \
        case 64: *(int64_t*)dest = Int_to_Int64(result, false); return; \
        case 32: *(int32_t*)dest = Int_to_Int32(result, false); return; \
        case 16: *(int16_t*)dest = Int_to_Int16(result, false); return; \
        case 8: *(int8_t*)dest = Int_to_Int8(result, false); return; \
        default: errx(1, "Invalid int bits"); \
        } \
        break; \
    }
            switch (binop->op) {
            CASE_OP(MULT, times) CASE_OP(DIVIDE, divided_by) CASE_OP(PLUS, plus) CASE_OP(MINUS, minus)
            CASE_OP(RSHIFT, right_shifted) CASE_OP(LSHIFT, left_shifted)
            CASE_OP(MOD, modulo) CASE_OP(MOD1, modulo1)
            CASE_OP(AND, bit_and) CASE_OP(OR, bit_or) CASE_OP(XOR, bit_xor)
            default: break;
            }
#undef CASE_OP
        } else if (t->tag == NumType) {
#define CASE_OP(OP_NAME, C_OP) case BINOP_##OP_NAME: {\
        double lhs = ast_to_num(env, binop->lhs); \
        double rhs = ast_to_num(env, binop->rhs); \
        if (Match(t, NumType)->bits == 64) \
            *(double*)dest = (double)(lhs C_OP rhs); \
        else \
            *(float*)dest = (float)(lhs C_OP rhs); \
        return; \
    }
            switch (binop->op) {
            CASE_OP(MULT, *) CASE_OP(DIVIDE, /) CASE_OP(PLUS, +) CASE_OP(MINUS, -)
            default: break;
            }
#undef CASE_OP
        }
        switch (binop->op) {
        case BINOP_EQ: case BINOP_NE: case BINOP_LT: case BINOP_LE: case BINOP_GT: case BINOP_GE: {
            type_t *t_lhs = get_type(env, binop->lhs);
            if (!type_eq(t_lhs, get_type(env, binop->rhs)))
                repl_err(ast, "Comparisons between different types aren't supported");
            const TypeInfo_t *info = type_to_type_info(t_lhs);
            size_t value_size = type_size(t_lhs);
            char lhs[value_size], rhs[value_size];
            eval(env, binop->lhs, lhs);
            eval(env, binop->rhs, rhs);
            int cmp = generic_compare(lhs, rhs, info);
            switch (binop->op) {
            case BINOP_EQ: *(bool*)dest = (cmp == 0); break;
            case BINOP_NE: *(bool*)dest = (cmp != 0); break;
            case BINOP_GT: *(bool*)dest = (cmp > 0); break;
            case BINOP_GE: *(bool*)dest = (cmp >= 0); break;
            case BINOP_LT: *(bool*)dest = (cmp < 0); break;
            case BINOP_LE: *(bool*)dest = (cmp <= 0); break;
            default: break;
            }
            break;
        }
        default: errx(1, "Binary op not implemented for %T: %W", t, ast);
        }
        break;
    }
    case Index: {
        auto index = Match(ast, Index);
        type_t *indexed_t = get_type(env, index->indexed);
        // type_t *index_t = get_type(env, index->index);
        switch (indexed_t->tag) {
        case ArrayType: {
            Array_t arr;
            eval(env, index->indexed, &arr);
            int64_t raw_index = Int_to_Int64(ast_to_int(env, index->index), false);
            int64_t index_int = raw_index;
            if (index_int < 1) index_int = arr.length + index_int + 1;
            if (index_int < 1 || index_int > arr.length)
                repl_err(index->index, "%ld is an invalid index for an array with length %ld",
                         raw_index, arr.length);
            size_t item_size = type_size(Match(indexed_t, ArrayType)->item_type);
            memcpy(dest, arr.data + arr.stride*(index_int-1), item_size);
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
            auto ptr = Match(indexed_t, PointerType);
            size_t pointed_size = type_size(ptr->pointed);
            void *pointer;
            eval(env, index->indexed, &pointer);
            memcpy(dest, pointer, pointed_size);
            break;
        }
        default: errx(1, "Indexing is not supported for %T", indexed_t);
        }
        break;
    }
    case Array: {
        assert(t->tag == ArrayType);
        Array_t arr = {};
        size_t item_size = type_size(Match(t, ArrayType)->item_type);
        char item_buf[item_size] = {};
        for (ast_list_t *item = Match(ast, Array)->items; item; item = item->next) {
            eval(env, item->ast, item_buf);
            Array$insert(&arr, item_buf, I(0), (int64_t)type_size(Match(t, ArrayType)->item_type));
        }
        memcpy(dest, &arr, sizeof(Array_t));
        break;
    }
    case Table: {
        assert(t->tag == TableType);
        auto table_ast = Match(ast, Table);
        Table_t table = {};
        size_t key_size = type_size(Match(t, TableType)->key_type);
        size_t value_size = type_size(Match(t, TableType)->value_type);
        char key_buf[key_size] = {};
        char value_buf[value_size] = {};
        const TypeInfo_t *table_info = type_to_type_info(t);
        assert(table_info->tag == TableInfo);
        for (ast_list_t *entry = table_ast->entries; entry; entry = entry->next) {
            auto e = Match(entry->ast, TableEntry);
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
        auto block = Match(ast, Block);
        for (ast_list_t *stmt = block->statements; stmt; stmt = stmt->next) {
            if (stmt->next)
                run(env, stmt->ast);
            else
                eval(env, stmt->ast, dest);
        }
        break;
    }
    default:
        errx(1, "Eval not implemented for %W", ast);
    }
}

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
