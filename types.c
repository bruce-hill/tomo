// Logic for handling type_t types
#include <gc/cord.h>
#include <stdint.h>
#include <signal.h>
#include <limits.h>
#include <math.h>

#include "builtins/table.h"
#include "types.h"
#include "builtins/util.h"

CORD type_to_cord(type_t *t) {
    switch (t->tag) {
        case UnknownType: return "???";
        case AbortType: return "Abort";
        case ReturnType: {
            type_t *ret = Match(t, ReturnType)->ret;
            return CORD_all("Return(", ret ? type_to_cord(ret) : "Void", ")");
        }
        case VoidType: return "Void";
        case MemoryType: return "Memory";
        case BoolType: return "Bool";
        case CStringType: return "CString";
        case TextType: return Match(t, TextType)->lang ? Match(t, TextType)->lang : "Text";
        case BigIntType: return "Int";
        case IntType: return CORD_asprintf("Int%d", Match(t, IntType)->bits);
        case NumType: return Match(t, NumType)->bits == TYPE_NBITS32 ? "Num32" : "Num";
        case ArrayType: {
            auto array = Match(t, ArrayType);
            return CORD_asprintf("[%r]", type_to_cord(array->item_type));
        }
        case ChannelType: {
            auto array = Match(t, ChannelType);
            return CORD_asprintf("||%r", type_to_cord(array->item_type));
        }
        case TableType: {
            auto table = Match(t, TableType);
            return CORD_asprintf("{%r:%r}", type_to_cord(table->key_type), type_to_cord(table->value_type));
        }
        case SetType: {
            auto set = Match(t, SetType);
            return CORD_asprintf("{%r}", type_to_cord(set->item_type));
        }
        case ClosureType: {
            return type_to_cord(Match(t, ClosureType)->fn);
        }
        case FunctionType: {
            CORD c = "func(";
            auto fn = Match(t, FunctionType);
            for (arg_t *arg = fn->args; arg; arg = arg->next) {
                c = CORD_cat(c, type_to_cord(arg->type));
                if (arg->next) c = CORD_cat(c, ", ");
            }
            if (fn->ret && fn->ret->tag != VoidType)
                c = CORD_all(c, ")->", type_to_cord(fn->ret));
            else
                c = CORD_all(c, ")");
            return c;
        }
        case StructType: {
            auto struct_ = Match(t, StructType);
            return struct_->name;
        }
        case PointerType: {
            auto ptr = Match(t, PointerType);
            CORD sigil = ptr->is_stack ? "&" : "@";
            if (ptr->is_readonly) sigil = CORD_cat(sigil, "%");
            return CORD_all(sigil, type_to_cord(ptr->pointed), ptr->is_optional ? "?" : CORD_EMPTY);
        }
        case EnumType: {
            auto tagged = Match(t, EnumType);
            return tagged->name;
        }
        case TypeInfoType: {
            return CORD_all("TypeInfo(", Match(t, TypeInfoType)->name, ")");
        }
        case ModuleType: {
            return CORD_all("Module(", Match(t, ModuleType)->name, ")");
        }
        default: {
            raise(SIGABRT);
            return CORD_asprintf("Unknown type: %d", t->tag);
        }
    }
}

int printf_pointer_size(const struct printf_info *info, size_t n, int argtypes[n], int sizes[n])
{
    if (n < 1) return -1;
    (void)info;
    argtypes[0] = PA_POINTER;
    sizes[0] = sizeof(void*);
    return 1;
}

int printf_type(FILE *stream, const struct printf_info *info, const void *const args[])
{
    (void)info;
    type_t *t = *(type_t**)args[0];
    if (!t) return fputs("(null)", stream);
    return CORD_put(type_to_cord(t), stream);
}

bool type_eq(type_t *a, type_t *b)
{
    if (a == b) return true;
    if (a->tag != b->tag) return false;
    return (CORD_cmp(type_to_cord(a), type_to_cord(b)) == 0);
}

bool type_is_a(type_t *t, type_t *req)
{
    if (type_eq(t, req)) return true;
    if (t->tag == PointerType && req->tag == PointerType) {
        auto t_ptr = Match(t, PointerType);
        auto req_ptr = Match(req, PointerType);
        if (type_eq(t_ptr->pointed, req_ptr->pointed))
            return (!t_ptr->is_stack && !t_ptr->is_optional && req_ptr->is_stack)
                || (!t_ptr->is_stack && req_ptr->is_optional);
    }
    return false;
}

static type_t *non_optional(type_t *t)
{
    if (t->tag != PointerType) return t;
    auto ptr = Match(t, PointerType);
    return ptr->is_optional ? Type(PointerType, .is_optional=false, .pointed=ptr->pointed) : t;
}

type_t *value_type(type_t *t)
{
    while (t->tag == PointerType)
        t = Match(t, PointerType)->pointed;
    return t;
}

type_t *type_or_type(type_t *a, type_t *b)
{
    if (!a) return b;
    if (!b) return a;
    if (type_is_a(b, a)) return a;
    if (type_is_a(a, b)) return b;
    if (a->tag == ReturnType && b->tag == ReturnType)
        return Type(ReturnType, .ret=type_or_type(Match(a, ReturnType)->ret, Match(b, ReturnType)->ret));
    if (a->tag == AbortType || a->tag == ReturnType) return non_optional(b);
    if (b->tag == AbortType || b->tag == ReturnType) return non_optional(a);
    if ((a->tag == IntType || a->tag == NumType) && (b->tag == IntType || b->tag == NumType)) {
        switch (compare_precision(a, b)) {
        case NUM_PRECISION_EQUAL: case NUM_PRECISION_MORE: return a;
        case NUM_PRECISION_LESS: return b;
        default: return NULL;
        }
        return NULL;
    }
    return NULL;
}

static inline double type_min_magnitude(type_t *t)
{
    switch (t->tag) {
    case BoolType: return (double)false;
    case BigIntType: return -1./0.;
    case IntType: {
        switch (Match(t, IntType)->bits) {
        case TYPE_IBITS8: return (double)INT8_MIN;
        case TYPE_IBITS16: return (double)INT16_MIN;
        case TYPE_IBITS32: return (double)INT32_MIN;
        case TYPE_IBITS64: return (double)INT64_MIN;
        default: errx(1, "Invalid integer bit size");
        }
    }
    case NumType: return -1./0.;
    default: return NAN;
    }
}

static inline double type_max_magnitude(type_t *t)
{
    switch (t->tag) {
    case BoolType: return (double)true;
    case BigIntType: return 1./0.;
    case IntType: {
        switch (Match(t, IntType)->bits) {
        case TYPE_IBITS8: return (double)INT8_MAX;
        case TYPE_IBITS16: return (double)INT16_MAX;
        case TYPE_IBITS32: return (double)INT32_MAX;
        case TYPE_IBITS64: return (double)INT64_MAX;
        default: errx(1, "Invalid integer bit size");
        }
    }
    case NumType: return 1./0.;
    default: return NAN;
    }
}

precision_cmp_e compare_precision(type_t *a, type_t *b)
{
    double a_min = type_min_magnitude(a),
           b_min = type_min_magnitude(b),
           a_max = type_max_magnitude(a),
           b_max = type_max_magnitude(b);

    if (isnan(a_min) || isnan(b_min) || isnan(a_max) || isnan(b_max))
        return NUM_PRECISION_INCOMPARABLE;
    else if (a_min == b_min && a_max == b_max) return NUM_PRECISION_EQUAL;
    else if (a_min <= b_min && b_max <= a_max) return NUM_PRECISION_MORE;
    else if (b_min <= a_min && a_max <= b_max) return NUM_PRECISION_LESS;
    else return NUM_PRECISION_INCOMPARABLE;
}

bool has_heap_memory(type_t *t)
{
    switch (t->tag) {
    case ArrayType: return true;
    case ChannelType: return true;
    case TableType: return true;
    case SetType: return true;
    case PointerType: return true;
    case BigIntType: return true;
    case StructType: {
        for (arg_t *field = Match(t, StructType)->fields; field; field = field->next) {
            if (has_heap_memory(field->type))
                return true;
        }
        return false;
    }
    case EnumType: {
        for (tag_t *tag = Match(t, EnumType)->tags; tag; tag = tag->next) {
            if (tag->type && has_heap_memory(tag->type))
                return true;
        }
        return false;
    }
    default: return false;
    }
}

bool can_send_over_channel(type_t *t)
{
    switch (t->tag) {
    case ArrayType: return true;
    case ChannelType: return true;
    case TableType: return true;
    case PointerType: return false;
    case StructType: {
        for (arg_t *field = Match(t, StructType)->fields; field; field = field->next) {
            if (!can_send_over_channel(field->type))
                return false;
        }
        return true;
    }
    case EnumType: {
        for (tag_t *tag = Match(t, EnumType)->tags; tag; tag = tag->next) {
            if (tag->type && !can_send_over_channel(tag->type))
                return false;
        }
        return true;
    }
    default: return true;
    }
}

bool has_stack_memory(type_t *t)
{
    switch (t->tag) {
    case PointerType: return Match(t, PointerType)->is_stack;
    default: return false;
    }
}

bool can_promote(type_t *actual, type_t *needed)
{
    // No promotion necessary:
    if (type_eq(actual, needed))
        return true;

    if (actual->tag == NumType && needed->tag == IntType)
        return false;

    if (actual->tag == IntType && (needed->tag == NumType || needed->tag == BigIntType))
        return true;

    if (actual->tag == BigIntType && needed->tag == NumType)
        return true;

    if (actual->tag == IntType && needed->tag == IntType) {
        auto cmp = compare_precision(actual, needed);
        return cmp == NUM_PRECISION_EQUAL || cmp == NUM_PRECISION_LESS;
    }

    // Text to C String
    if (actual->tag == TextType && !Match(actual, TextType)->lang && needed->tag == CStringType)
        return true;

    // Automatic dereferencing:
    if (actual->tag == PointerType && !Match(actual, PointerType)->is_optional
        && can_promote(Match(actual, PointerType)->pointed, needed))
        return true;

    // Optional promotion:
    if (needed->tag == PointerType && actual->tag == PointerType) {
        auto needed_ptr = Match(needed, PointerType);
        auto actual_ptr = Match(actual, PointerType);
        if (needed_ptr->pointed->tag != MemoryType && !type_eq(needed_ptr->pointed, actual_ptr->pointed))
            // Can't use @Foo for a function that wants @Baz
            // But you *can* use @Foo for a function that wants @Memory
            return false;
        else if (actual_ptr->is_stack && !needed_ptr->is_stack)
            // Can't use &x for a function that wants a @Foo or ?Foo
            return false;
        else if (actual_ptr->is_optional && !needed_ptr->is_optional)
            // Can't use !Foo for a function that wants @Foo
            return false;
        else if (actual_ptr->is_readonly && !needed_ptr->is_readonly)
            // Can't use pointer to readonly data when we need a pointer that can write to the data
            return false;
        else
            return true;
    }

    if (needed->tag == ClosureType && actual->tag == FunctionType)
        return can_promote(actual, Match(needed, ClosureType)->fn);

    if (needed->tag == ClosureType && actual->tag == ClosureType)
        return can_promote(Match(actual, ClosureType)->fn, Match(needed, ClosureType)->fn);

    if (actual->tag == FunctionType && needed->tag == FunctionType) {
        for (arg_t *actual_arg = Match(actual, FunctionType)->args, *needed_arg = Match(needed, FunctionType)->args;
             actual_arg || needed_arg; actual_arg = actual_arg->next, needed_arg = needed_arg->next) {
            if (!actual_arg || !needed_arg) return false;
            if (type_eq(actual_arg->type, needed_arg->type)) continue;
            if (actual_arg->type->tag == PointerType && needed_arg->type->tag == PointerType
                && can_promote(actual_arg->type, needed_arg->type))
                continue;
            return false;
        }
        type_t *actual_ret = Match(actual, FunctionType)->ret;
        if (!actual_ret) actual_ret = Type(VoidType);
        type_t *needed_ret = Match(needed, FunctionType)->ret;
        if (!needed_ret) needed_ret = Type(VoidType);

        return (
            (type_eq(actual_ret, needed_ret))
            || (actual_ret->tag == PointerType && needed_ret->tag == PointerType
                && can_promote(actual_ret, needed_ret)));
    }

    // Set -> Array promotion
    if (needed->tag == ArrayType && actual->tag == SetType
        && type_eq(Match(needed, ArrayType)->item_type, Match(actual, SetType)->item_type))
        return true;

    return false;
}

bool can_leave_uninitialized(type_t *t)
{
    switch (t->tag) {
    case PointerType: return Match(t, PointerType)->is_optional;
    case ArrayType: case IntType: case NumType: case BoolType:
        return true;
    case ChannelType: return false;
    case StructType: {
        for (arg_t *field = Match(t, StructType)->fields; field; field = field->next) {
            if (!can_leave_uninitialized(field->type))
                return false;
        }
        return true;
    }
    case EnumType: {
        for (tag_t *tag = Match(t, EnumType)->tags; tag; tag = tag->next) {
            if (tag->type && !can_leave_uninitialized(tag->type))
                return false;
        }
        return true;
    }
    default: return false;
    }
}

static bool _can_have_cycles(type_t *t, table_t *seen)
{
    switch (t->tag) {
        case ArrayType: return _can_have_cycles(Match(t, ArrayType)->item_type, seen);
        case ChannelType: return _can_have_cycles(Match(t, ChannelType)->item_type, seen);
        case TableType: {
            auto table = Match(t, TableType);
            return _can_have_cycles(table->key_type, seen) || _can_have_cycles(table->value_type, seen);
        }
        case SetType: return _can_have_cycles(Match(t, SetType)->item_type, seen);
        case StructType: {
            for (arg_t *field = Match(t, StructType)->fields; field; field = field->next) {
                if (_can_have_cycles(field->type, seen))
                    return true;
            }
            return false;
        }
        case PointerType: return _can_have_cycles(Match(t, PointerType)->pointed, seen);
        case EnumType: {
            for (tag_t *tag = Match(t, EnumType)->tags; tag; tag = tag->next) {
                if (tag->type && _can_have_cycles(tag->type, seen))
                    return true;
            }
            return false;
        }
        default: return false;
    }
}

bool can_have_cycles(type_t *t)
{
    table_t seen = {0};
    return _can_have_cycles(t, &seen);
}

bool is_int_type(type_t *t)
{
    return t->tag == IntType || t->tag == BigIntType;
}

bool is_numeric_type(type_t *t)
{
    return t->tag == IntType || t->tag == BigIntType || t->tag == NumType;
}

type_t *replace_type(type_t *t, type_t *target, type_t *replacement)
{
    if (type_eq(t, target))
        return replacement;

#define COPY(t) memcpy(GC_MALLOC(sizeof(type_t)), (t), sizeof(type_t))
#define REPLACED_MEMBER(t, tag, member) ({ t = memcpy(GC_MALLOC(sizeof(type_t)), (t), sizeof(type_t)); Match((struct type_s*)(t), tag)->member = replace_type(Match((t), tag)->member, target, replacement); t; })
    switch (t->tag) {
        case ArrayType: return REPLACED_MEMBER(t, ArrayType, item_type);
        case SetType: return REPLACED_MEMBER(t, SetType, item_type);
        case ChannelType: return REPLACED_MEMBER(t, ChannelType, item_type);
        case TableType: {
            t = REPLACED_MEMBER(t, TableType, key_type);
            t = REPLACED_MEMBER(t, TableType, value_type);
            return t;
        }
        case FunctionType: {
            auto fn = Match(t, FunctionType);
            t = REPLACED_MEMBER(t, FunctionType, ret);
            arg_t *args = LIST_MAP(fn->args, old_arg, .type=replace_type(old_arg->type, target, replacement));
            Match((struct type_s*)t, FunctionType)->args = args;
            return t;
        }
        case StructType: {
            auto struct_ = Match(t, StructType);
            arg_t *fields = LIST_MAP(struct_->fields, field, .type=replace_type(field->type, target, replacement));
            t = COPY(t);
            Match((struct type_s*)t, StructType)->fields = fields;
            return t;
        }
        case PointerType: return REPLACED_MEMBER(t, PointerType, pointed);
        case EnumType: {
            auto tagged = Match(t, EnumType);
            tag_t *tags = LIST_MAP(tagged->tags, tag, .type=replace_type(tag->type, target, replacement));
            t = COPY(t);
            Match((struct type_s*)t, EnumType)->tags = tags;
            return t;
        }
        default: return t;
    }
#undef COPY
#undef REPLACED_MEMBER
}

size_t type_size(type_t *t)
{
    switch (t->tag) {
    case UnknownType: case AbortType: case ReturnType: case VoidType: return 0;
    case MemoryType: errx(1, "Memory has undefined type size");
    case BoolType: return sizeof(bool);
    case CStringType: return sizeof(char*);
    case BigIntType: return sizeof(Int_t);
    case IntType: {
        switch (Match(t, IntType)->bits) {
        case TYPE_IBITS64: return sizeof(int64_t);
        case TYPE_IBITS32: return sizeof(int32_t);
        case TYPE_IBITS16: return sizeof(int16_t);
        case TYPE_IBITS8: return sizeof(int8_t);
        default: errx(1, "Invalid integer bit size");
        }
    }
    case NumType: return Match(t, NumType)->bits == TYPE_NBITS64 ? sizeof(double) : sizeof(float);
    case TextType: return sizeof(Text_t);
    case ArrayType: return sizeof(Array_t);
    case SetType: return sizeof(table_t);
    case ChannelType: return sizeof(channel_t*);
    case TableType: return sizeof(table_t);
    case FunctionType: return sizeof(void*);
    case ClosureType: return sizeof(struct {void *fn, *userdata;});
    case PointerType: return sizeof(void*);
    case StructType: {
        arg_t *fields = Match(t, StructType)->fields;
        size_t size = t->tag == StructType ? 0 : sizeof(void*);
        for (arg_t *field = fields; field; field = field->next) {
            type_t *field_type = field->type;
            if (field_type->tag == BoolType) {
                size += 1; // Bit packing
            } else {
                size_t align = type_align(field_type);
                if (align > 1 && size % align > 0)
                    size += align - (size % align); // Padding
                size += type_size(field_type);
            }
        }
        return size;
    }
    case EnumType: {
        size_t max_align = 0;
        size_t max_size = 0;
        for (tag_t *tag = Match(t, EnumType)->tags; tag; tag = tag->next) {
            size_t align = type_align(tag->type);
            if (align > max_align) max_align = align;
            size_t size = type_size(tag->type);
            if (size > max_size) max_size = size;
        }
        size_t size = sizeof(UnknownType); // generic enum
        if (max_align > 1 && size % max_align > 0)
            size += max_align - (size % max_align);
        size += max_size;
        return size;
    }
    case TypeInfoType: return sizeof(TypeInfo);
    case ModuleType: return 0;
    }
    errx(1, "This should not be reachable");
}

size_t type_align(type_t *t)
{
    switch (t->tag) {
    case UnknownType: case AbortType: case ReturnType: case VoidType: return 0;
    case MemoryType: errx(1, "Memory has undefined type alignment");
    case BoolType: return __alignof__(bool);
    case CStringType: return __alignof__(char*);
    case BigIntType: return __alignof__(Int_t);
    case IntType: {
        switch (Match(t, IntType)->bits) {
        case TYPE_IBITS64: return __alignof__(int64_t);
        case TYPE_IBITS32: return __alignof__(int32_t);
        case TYPE_IBITS16: return __alignof__(int16_t);
        case TYPE_IBITS8: return __alignof__(int8_t);
        default: return 0;
        }
    }
    case NumType: return Match(t, NumType)->bits == TYPE_NBITS64 ? __alignof__(double) : __alignof__(float);
    case TextType: return __alignof__(Text_t);
    case SetType: return __alignof__(table_t);
    case ArrayType: return __alignof__(Array_t);
    case ChannelType: return __alignof__(channel_t*);
    case TableType: return __alignof__(table_t);
    case FunctionType: return __alignof__(void*);
    case ClosureType: return __alignof__(struct {void *fn, *userdata;});
    case PointerType: return __alignof__(void*);
    case StructType: {
        arg_t *fields = Match(t, StructType)->fields;
        size_t align = t->tag == StructType ? 0 : sizeof(void*);
        for (arg_t *field = fields; field; field = field->next) {
            size_t field_align = type_align(field->type);
            if (field_align > align) align = field_align;
        }
        return align;
    }
    case EnumType: {
        size_t align = __alignof__(UnknownType);
        for (tag_t *tag = Match(t, EnumType)->tags; tag; tag = tag->next) {
            size_t tag_align = type_align(tag->type);
            if (tag_align > align) align = tag_align;
        }
        return align;
    }
    case TypeInfoType: return __alignof__(TypeInfo);
    case ModuleType: return 0;
    }
    errx(1, "This should not be reachable");
}

size_t padded_type_size(type_t *t)
{
    size_t size = type_size(t);
    size_t align = type_align(t);
    if (align > 1 && size % align > 0)
        size += align - (size % align); // Padding
    return size;
}

type_t *get_field_type(type_t *t, const char *field_name)
{
    t = value_type(t);
    switch (t->tag) {
    case PointerType:
        return get_field_type(Match(t, PointerType)->pointed, field_name);
    case TextType: {
        if (Match(t, TextType)->lang && streq(field_name, "text_content"))
            return Type(TextType);
        else if (streq(field_name, "length")) return INT_TYPE;
        return NULL;
    }
    case StructType: {
        auto struct_t = Match(t, StructType);
        for (arg_t *field = struct_t->fields; field; field = field->next) {
            if (streq(field->name, field_name))
                return field->type;
        }
        return NULL;
    }
    case EnumType: {
        auto e = Match(t, EnumType);
        for (tag_t *tag = e->tags; tag; tag = tag->next) {
            if (streq(field_name, tag->name))
                return Type(BoolType);
        }
        return NULL;
    }
    case SetType: {
        if (streq(field_name, "length"))
            return INT_TYPE;
        else if (streq(field_name, "items"))
            return Type(ArrayType, .item_type=Match(t, SetType)->item_type);
        return NULL;
    }
    case TableType: {
        if (streq(field_name, "length"))
            return INT_TYPE;
        else if (streq(field_name, "keys"))
            return Type(ArrayType, Match(t, TableType)->key_type);
        else if (streq(field_name, "values"))
            return Type(ArrayType, Match(t, TableType)->value_type);
        else if (streq(field_name, "default"))
            return Type(PointerType, .pointed=Match(t, TableType)->value_type, .is_readonly=true, .is_optional=true);
        else if (streq(field_name, "fallback"))
            return Type(PointerType, .pointed=t, .is_readonly=true, .is_optional=true);
        return NULL;
    }
    case ArrayType: {
        if (streq(field_name, "length")) return INT_TYPE;
        return NULL;
    }
    case ChannelType: {
        if (streq(field_name, "max_size")) return INT_TYPE;
        return NULL;
    }
    default: return NULL;
    }
}

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
