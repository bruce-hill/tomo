// Logic for handling type_t types
#include <gc/cord.h>
#include <stdint.h>
#include <signal.h>
#include <limits.h>
#include <math.h>

#include "builtins/table.h"
#include "types.h"
#include "util.h"

CORD type_to_cord(type_t *t) {
    switch (t->tag) {
        case UnknownType: return "???";
        case AbortType: return "Abort";
        case VoidType: return "Void";
        case MemoryType: return "Memory";
        case BoolType: return "Bool";
        case StringType: return "Str";
        case IntType: return Match(t, IntType)->bits == 64 ? "Int" : CORD_asprintf("Int%ld", Match(t, IntType)->bits);
        case NumType: return Match(t, NumType)->bits == 64 ? "Num" : CORD_asprintf("Num%ld", Match(t, NumType)->bits);
        case ArrayType: {
            auto array = Match(t, ArrayType);
            return CORD_asprintf("[%r]", type_to_cord(array->item_type));
        }
        case TableType: {
            auto table = Match(t, TableType);
            return CORD_asprintf("{%r=>%r}", type_to_cord(table->key_type), type_to_cord(table->value_type));
        }
        case FunctionType: {
            CORD c = "func(";
            auto fn = Match(t, FunctionType);
            for (arg_t *arg = fn->args; arg; arg = arg->next) {
                c = CORD_cat(c, type_to_cord(arg->type));
                if (arg->next) c = CORD_cat(c, ", ");
            }
            c = CORD_cat(c, ")->");
            c = CORD_cat(c, type_to_cord(fn->ret));
            return c;
        }
        case StructType: {
            auto struct_ = Match(t, StructType);
            return struct_->name;
        }
        case PointerType: {
            auto ptr = Match(t, PointerType);
            CORD sigil = ptr->is_stack ? "&" : (ptr->is_optional ? "?" : "@");
            if (ptr->is_readonly) sigil = CORD_cat(sigil, "(readonly)");
            return CORD_cat(sigil, type_to_cord(ptr->pointed));
        }
        case EnumType: {
            auto tagged = Match(t, EnumType);
            return tagged->name;
        }
        case TypeInfoType: {
            return CORD_all("TypeInfo(", Match(t, TypeInfoType)->name, ")");
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
    if (a->tag == AbortType) return non_optional(b);
    if (b->tag == AbortType) return non_optional(a);
    if ((a->tag == IntType || a->tag == NumType) && (b->tag == IntType || b->tag == NumType)) {
        switch (compare_precision(a, b)) {
        case NUM_PRECISION_EQUAL: case NUM_PRECISION_MORE: return a;
        case NUM_PRECISION_LESS: return b;
        case NUM_PRECISION_INCOMPARABLE: {
            if (a->tag == IntType && b->tag == IntType && Match(a, IntType)->bits < 64)
                return Type(IntType, .bits=Match(a, IntType)->bits * 2);
            return NULL;
        }
        }
        return NULL;
    }
    return NULL;
}

static inline double type_min_magnitude(type_t *t)
{
    switch (t->tag) {
    case BoolType: return (double)false;
    case IntType: {
        switch (Match(t, IntType)->bits) {
        case 8: return (double)INT8_MIN;
        case 16: return (double)INT16_MIN;
        case 32: return (double)INT32_MIN;
        case 64: return (double)INT64_MIN;
        default: return NAN;
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
    case IntType: {
        switch (Match(t, IntType)->bits) {
        case 8: return (double)INT8_MAX;
        case 16: return (double)INT16_MAX;
        case 32: return (double)INT32_MAX;
        case 64: return (double)INT64_MAX;
        default: return NAN;
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

bool is_orderable(type_t *t)
{
    switch (t->tag) {
    case ArrayType: return is_orderable(Match(t, ArrayType)->item_type);
    case PointerType: case FunctionType: case TableType: return false;
    case StructType: {
        for (arg_t *field = Match(t, StructType)->fields; field; field = field->next) {
            if (!is_orderable(field->type))
                return false;
        }
        return true;
    }
    case EnumType: {
        for (tag_t *tag = Match(t, EnumType)->tags; tag; tag = tag->next) {
            if (tag->type && !is_orderable(tag->type))
                return false;
        }
        return true;
    }
    default: return true;
    }
}

bool has_heap_memory(type_t *t)
{
    switch (t->tag) {
    case ArrayType: return true;
    case TableType: return true;
    case PointerType: return true;
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

    if ((actual->tag == IntType || actual->tag == NumType)
        && (needed->tag == IntType || needed->tag == NumType)) {
        auto cmp = compare_precision(actual, needed);
        return cmp == NUM_PRECISION_EQUAL || cmp == NUM_PRECISION_LESS;
    }

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

    // Function promotion:
    if (needed->tag == FunctionType && actual->tag == FunctionType) {
        auto needed_fn = Match(needed, FunctionType);
        auto actual_fn = Match(actual, FunctionType);
        for (arg_t *needed_arg = needed_fn->args, *actual_arg = actual_fn->args;
             needed_arg || actual_arg;
             needed_arg = needed_arg->next, actual_arg = actual_arg->next) {

            if (!needed_arg || !actual_arg)
                return false;

            if (!type_eq(needed_arg->type, actual_arg->type))
                return false;
        }
        return true;
    }

    if (actual->tag == StructType) {
        auto actual_struct = Match(actual, StructType);
        auto needed_struct = Match(needed, StructType);
        // TODO: allow promoting with uninitialized or extraneous values?
        for (arg_t *needed_field = needed_struct->fields, *actual_field = actual_struct->fields;
             needed_field || actual_field;
             needed_field = needed_field->next, actual_field = actual_field->next) {

            if (!needed_field || !actual_field)
                return false;

            // TODO: check field names??
            if (!can_promote(actual_field->type, needed_field->type))
                return false;
        }
        return true;
    }

    return false;
}

bool can_leave_uninitialized(type_t *t)
{
    switch (t->tag) {
    case PointerType: return Match(t, PointerType)->is_optional;
    case ArrayType: case IntType: case NumType: case BoolType:
        return true;
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
        case TableType: {
            auto table = Match(t, TableType);
            return _can_have_cycles(table->key_type, seen) || _can_have_cycles(table->value_type, seen);
        }
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

type_t *replace_type(type_t *t, type_t *target, type_t *replacement)
{
    if (type_eq(t, target))
        return replacement;

#define COPY(t) memcpy(GC_MALLOC(sizeof(type_t)), (t), sizeof(type_t))
#define REPLACED_MEMBER(t, tag, member) ({ t = memcpy(GC_MALLOC(sizeof(type_t)), (t), sizeof(type_t)); Match((struct type_s*)(t), tag)->member = replace_type(Match((t), tag)->member, target, replacement); t; })
    switch (t->tag) {
        case ArrayType: return REPLACED_MEMBER(t, ArrayType, item_type);
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
    case UnknownType: case AbortType: case VoidType: return 0;
    case MemoryType: errx(1, "Memory has undefined type size");
    case BoolType: return sizeof(bool);
    case IntType: return Match(t, IntType)->bits/8;
    case NumType: return Match(t, NumType)->bits/8;
    case StringType: return sizeof(CORD);
    case ArrayType: return sizeof(array_t);
    case TableType: return sizeof(table_t);
    case FunctionType: return sizeof(void*);
    case ClosureType: return sizeof(struct {void *fn, *userdata;});
    case PointerType: return sizeof(void*);
    case StructType: {
        size_t size = 0;
        for (arg_t *field = Match(t, StructType)->fields; field; field = field->next) {
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
    }
    errx(1, "This should not be reachable");
}

size_t type_align(type_t *t)
{
    switch (t->tag) {
    case UnknownType: case AbortType: case VoidType: return 0;
    case MemoryType: errx(1, "Memory has undefined type alignment");
    case BoolType: return __alignof__(bool);
    case IntType: return Match(t, IntType)->bits/8;
    case NumType: return Match(t, NumType)->bits/8;
    case StringType: return __alignof__(CORD);
    case ArrayType: return __alignof__(array_t);
    case TableType: return __alignof__(table_t);
    case FunctionType: return __alignof__(void*);
    case ClosureType: return __alignof__(void*);
    case PointerType: return __alignof__(void*);
    case StructType: {
        size_t align = 0;
        for (arg_t *field = Match(t, StructType)->fields; field; field = field->next) {
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
    }
    errx(1, "This should not be reachable");
}

type_t *get_field_type(type_t *t, const char *field_name)
{
    t = value_type(t);
    switch (t->tag) {
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
                return Type(PointerType, .pointed=tag->type, .is_optional=true, .is_readonly=true);
        }
        return NULL;
    }
    case TableType: {
        if (streq(field_name, "keys"))
            return Type(ArrayType, Match(t, TableType)->key_type);
        else if (streq(field_name, "values"))
            return Type(ArrayType, Match(t, TableType)->value_type);
        else if (streq(field_name, "default"))
            return Type(PointerType, .pointed=Match(t, TableType)->value_type, .is_readonly=true, .is_optional=true);
        else if (streq(field_name, "fallback"))
            return Type(PointerType, .pointed=t, .is_readonly=true, .is_optional=true);
        return NULL;
    }
    default: return NULL;
    }
}

type_t *iteration_key_type(type_t *iterable)
{
    switch (iterable->tag) {
    case IntType: case ArrayType: return Type(IntType, .bits=64);
    case TableType: return Match(iterable, TableType)->key_type;
    default: return NULL;
    }
}

type_t *iteration_value_type(type_t *iterable)
{
    switch (iterable->tag) {
    case IntType: return iterable;
    case ArrayType: return Match(iterable, ArrayType)->item_type;
    case TableType: return Match(iterable, TableType)->value_type;
    default: return NULL;
    }
}

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
