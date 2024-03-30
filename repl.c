
#include <dlfcn.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "builtins/tomo.h"
#include "typecheck.h"
#include "parse.h"

typedef union {
    int8_t i8;
    int16_t i16;
    int32_t i32;
    int64_t i64;
    double n64;
    float n32;
} number_t;

static jmp_buf on_err;

void run(env_t *env, ast_t *ast);
void eval(env_t *env, ast_t *ast, void *dest);

void repl(void)
{
    env_t *env = new_compilation_unit();
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
        file_t *f = spoof_file("<repl>", heap_strf(">> %s", line));
        ast_t *ast = parse_file(f, &on_err);
        ast = WrapAST(ast, DocTest, .expr=ast, .skip_source=true);
        run(env, ast);
        printf("\x1b[33;1m>>\x1b[m ");
        fflush(stdout);
    }
    if (line) free(line);
    printf("\n");
}

__attribute__((noreturn))
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
        fprint_span(stderr, node->file, node->start, node->end, "\x1b[31;1m", 2, color);

    longjmp(on_err, 1);
}

const TypeInfo *type_to_type_info(type_t *t)
{
    switch (t->tag) {
    case IntType:
        switch (Match(t, IntType)->bits) {
        case 0: case 64: return &$Int;
        case 32: return &$Int32;
        case 16: return &$Int16;
        case 8: return &$Int8;
        default: errx(1, "Invalid bits");
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

static int64_t ast_to_int(env_t *env, ast_t *ast)
{
    type_t *t = get_type(env, ast);
    switch (t->tag) {
    case IntType: {
        number_t num;
        eval(env, ast, &num);
        switch (Match(t, IntType)->bits) {
        case 0: case 64: return (int64_t)num.i64;
        case 32: return (int64_t)num.i32;
        case 16: return (int64_t)num.i16;
        case 8: return (int64_t)num.i8;
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
    case IntType: {
        number_t num;
        eval(env, ast, &num);
        switch (Match(t, IntType)->bits) {
        case 0: case 64: return (double)num.i64;
        case 32: return (double)num.i32;
        case 16: return (double)num.i16;
        case 8: return (double)num.i8;
        default: errx(1, "Invalid int bits");
        }
    }
    case NumType: {
        number_t num;
        eval(env, ast, &num);
        return Match(t, NumType)->bits == 32 ? (double)num.n32 : (double)num.n64;
    }
    default: repl_err(NULL, "Cannot convert to number");
    }
}

static CORD obj_to_text(type_t *t, const void *obj, bool use_color)
{
#define C(code, fmt) (use_color ? "\x1b[" code "m" fmt "\x1b[m" : fmt)
    switch (t->tag) {
    case MemoryType: return "<Memory>";
    case BoolType: return *(bool*)obj ? C("35", "yes") : C("35", "no");
    case IntType: {
        switch (Match(t, IntType)->bits) {
        case 0: case 64: return CORD_asprintf(C("35", "%ld"), *(int64_t*)obj);
        case 32: return CORD_asprintf(C("35", "%d"), *(int32_t*)obj);
        case 16: return CORD_asprintf(C("35", "%d"), *(int16_t*)obj);
        case 8: return CORD_asprintf(C("35", "%d"), *(int8_t*)obj);
        default: errx(1, "Invalid bits: %ld", Match(t, IntType)->bits);
        }
    }
    case NumType: {
        switch (Match(t, NumType)->bits) {
        case 0: case 64: return CORD_asprintf(C("35", "%g"), *(double*)obj);
        case 32: return CORD_asprintf(C("35", "%g"), *(float*)obj);
        default: errx(1, "Invalid bits: %ld", Match(t, NumType)->bits);
        }
    }
    case TextType: return Text$quoted(*(CORD*)obj, use_color);
    case ArrayType: {
        type_t *item_t = Match(t, ArrayType)->item_type;
        CORD ret = "[";
        const array_t *arr = obj;
        for (int64_t i = 0; i < arr->length; i++) {
            if (i > 0) ret = CORD_cat(ret, ", ");
            ret = CORD_cat(ret, obj_to_text(item_t, arr->data + i*arr->stride, use_color));
        }
        return CORD_cat(ret, "]");
    }
    case TableType: {
        type_t *key_t = Match(t, TableType)->key_type;
        type_t *value_t = Match(t, TableType)->value_type;
        CORD ret = "{";
        const table_t *table = obj;
        size_t value_offset = type_size(key_t);
        if (type_align(value_t) > 1 && value_offset % type_align(value_t))
            value_offset += type_align(value_t) - (value_offset % type_align(value_t)); // padding
        for (int64_t i = 0; i < table->entries.length; i++) {
            if (i > 0) ret = CORD_cat(ret, ", ");
            ret = CORD_all(ret, obj_to_text(key_t, table->entries.data + i*table->entries.stride, use_color), "=>",
                           obj_to_text(value_t, table->entries.data + i*table->entries.stride + value_offset, use_color));
        }
        if (table->fallback)
            ret = CORD_all(ret, "; ", obj_to_text(t, table->fallback, use_color));
        if (table->default_value)
            ret = CORD_all(ret, "; ", obj_to_text(value_t, table->default_value, use_color));
        return CORD_cat(ret, "}");
    }
    case PointerType: {
        const void *p = *(const void**)obj;
        auto ptr = Match(t, PointerType);
        if (!p) return CORD_cat("!", type_to_cord(ptr->pointed));
        CORD sigil = ptr->is_stack ? "&" : (ptr->is_optional ? "?" : "@");
        if (ptr->is_readonly) sigil = CORD_cat(sigil, "(readonly)");
        return CORD_all(sigil, obj_to_text(ptr->pointed, p, use_color));
    }
    case StructType: {
        errx(1, "Struct strings not implemented yet");
    }
    case EnumType: {
        errx(1, "Enum strings not implemented yet");
    }
    default: return type_to_cord(t);
    }
}

void run(env_t *env, ast_t *ast)
{
    switch (ast->tag) {
    case Declare: {
        auto decl = Match(ast, Declare);
        const char *name = Match(ast, Var)->name;
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
            //     TypeInfo *table_info = type_to_type_info(t);
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
            CORD c = obj_to_text(t, value, true);
            printf("= %s \x1b[2m: %T\n", CORD_to_const_char_star(c), t);
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
    // Block,
    // For, While, If, When,
    // Skip, Stop, Pass,
    // Return,
    // Extern,
    // StructDef, EnumDef, LangDef,
    // DocTest,
    // Use,
    // LinkerDirective,
    // InlineCCode,
    default: {
        eval(env, ast, NULL);
        break;
    }
    }
}

void eval(env_t *env, ast_t *ast, void *dest)
{
    type_t *t = get_type(env, ast);
    size_t size = type_size(t);
    switch (ast->tag) {
    case Nil:
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
        case 0: case 64: *(int64_t*)dest = Match(ast, Int)->i; break;
        case 32: *(int32_t*)dest = Match(ast, Int)->i; break;
        case 16: *(int16_t*)dest = Match(ast, Int)->i; break;
        case 8: *(int8_t*)dest = Match(ast, Int)->i; break;
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
                ret = CORD_cat(ret, obj_to_text(chunk_t, buf, false));
            }
        }
        if (dest) *(CORD*)dest = ret;
        break;
    }
#define CASE_OP(OP_NAME, C_OP) case BINOP_##OP_NAME: {\
    if (t->tag == IntType) { \
        int64_t lhs = ast_to_int(env, binop->lhs); \
        int64_t rhs = ast_to_int(env, binop->rhs); \
        switch (Match(t, IntType)->bits) { \
        case 64: *(int64_t*)dest = lhs C_OP rhs; break; \
        case 32: *(int32_t*)dest = (int32_t)(lhs C_OP rhs); break; \
        case 16: *(int16_t*)dest = (int16_t)(lhs C_OP rhs); break; \
        case 8: *(int8_t*)dest = (int8_t)(lhs C_OP rhs); break; \
        default: errx(1, "Invalid int bits"); \
        } \
    } else if (t->tag == NumType) { \
        double lhs = ast_to_num(env, binop->lhs); \
        double rhs = ast_to_num(env, binop->rhs); \
        if (Match(t, NumType)->bits == 64) \
            *(double*)dest = (double)(lhs C_OP rhs); \
        else \
            *(float*)dest = (float)(lhs C_OP rhs); \
    } else { \
        errx(1, "Binary ops are not yet supported for %W", ast); \
    } \
    break; \
}
    case BinaryOp: {
        auto binop = Match(ast, BinaryOp);
        switch (binop->op) {
        CASE_OP(MULT, *) CASE_OP(DIVIDE, /) CASE_OP(PLUS, +)
        CASE_OP(MINUS, -)  CASE_OP(EQ, ==)
        CASE_OP(NE, !=) CASE_OP(LT, <) CASE_OP(LE, <=) CASE_OP(GT, >) CASE_OP(GE, >=)
        default: errx(1, "Binary op not implemented: %W");
        }
        break;
    }
    default:
        errx(1, "Eval not implemented for %W", ast);
    }
}

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
