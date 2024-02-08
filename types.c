// Logic for handling type_t types
#include <gc/cord.h>
#include <stdint.h>
#include <limits.h>
#include <math.h>

#include "builtins/table.h"
#include "types.h"
#include "util.h"

static CORD type_to_cord(type_t *t) {
    switch (t->tag) {
        case UnknownType: return "???";
        case AbortType: return "Abort";
        case VoidType: return "Void";
        case MemoryType: return "Memory";
        case BoolType: return "Bool";
        case CharType: return "Char";
        case IntType: return CORD_asprintf("Int%ld", Match(t, IntType)->bits);
        case NumType: return CORD_asprintf("Num%ld", Match(t, NumType)->bits);
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
            CORD c = "struct(";
            int64_t i = 1;
            for (arg_t *field = struct_->fields; field; field = field->next) {
                const char *fname = field->name ? field->name : heap_strf("_%lu", i);
                ++i;
                if (fname && !streq(fname, heap_strf("_%lu", i+1)))
                    c = CORD_cat(CORD_cat(c, fname), ":");
                else
                    c = CORD_cat(c, ":");

                c = CORD_cat(c, type_to_cord(field->type));

                if (field->next) c = CORD_cat(c, ", ");
            }
            c = CORD_cat(c, ")");
            return c;
        }
        case PointerType: {
            auto ptr = Match(t, PointerType);
            CORD sigil = ptr->is_stack ? "&" : (ptr->is_optional ? "?" : "@");
            if (ptr->is_readonly) sigil = CORD_cat(sigil, "(readonly)");
            return CORD_cat(sigil, type_to_cord(ptr->pointed));
        }
        case EnumType: {
            auto tagged = Match(t, EnumType);

            CORD c = "enum(";
            int64_t next_tag = 0;
            for (tag_t *tag = tagged->tags; tag; tag = tag->next) {
                // name, tag_value, type
                c = CORD_cat(c, tag->name);
                if (tag->type) {
                    c = CORD_cat(c, "(");
                    auto struct_ = Match(tag->type, StructType);
                    int64_t i = 1;
                    for (arg_t *field = struct_->fields; field; field = field->next) {
                        if (field->name && !streq(field->name, heap_strf("_%lu", i)))
                            c = CORD_cat(CORD_cat(c, field->name), ":");

                        CORD fstr = type_to_cord(field->type);
                        c = CORD_cat(c, fstr);
                        if (field->next) c = CORD_cat(c, ",");
                        ++i;
                    }
                    c = CORD_cat(c, ")");
                }

                if (tag->tag_value != next_tag) {
                    CORD_sprintf(&c, "%r=%ld", c, tag->tag_value);
                    next_tag = tag->tag_value + 1;
                } else {
                    ++next_tag;
                }

                if (tag->next)
                    c = CORD_cat(c, "|");
            }
            c = CORD_cat(c, ")");
            return c;
        }
        case VariantType: {
            return Match(t, VariantType)->name;
        }
        case PlaceholderType: {
            return Match(t, PlaceholderType)->name;
        }
        case TypeInfoType: {
            return "TypeInfo";
        }
        default: {
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

const char *type_to_string(type_t *t) {
    return CORD_to_const_char_star(type_to_cord(t));
}

bool type_eq(type_t *a, type_t *b)
{
    if (a == b) return true;
    if (a->tag != b->tag) return false;
    if (a->tag == PlaceholderType) return a == b;
    return streq(type_to_string(a), type_to_string(b));
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

type_t *base_value_type(type_t *t)
{
    for (;;) {
        if (t->tag == PointerType)
            t = Match(t, PointerType)->pointed;
        else if (t->tag == VariantType)
            t = Match(t, VariantType)->variant_of;
        else break;
    }
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

bool is_integral(type_t *t)
{
    t = base_variant(t);
    return t->tag == IntType || t->tag == CharType;
}

bool is_floating_point(type_t *t)
{
    t = base_variant(t);
    return t->tag == NumType;
}

bool is_numeric(type_t *t)
{
    t = base_variant(t);
    return t->tag == IntType || t->tag == NumType;
}

static inline double type_min_magnitude(type_t *t)
{
    switch (t->tag) {
    case BoolType: return (double)false;
    case CharType: return (double)CHAR_MIN;
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
    case VariantType: return type_min_magnitude(Match(t, VariantType)->variant_of);
    default: return NAN;
    }
}

static inline double type_max_magnitude(type_t *t)
{
    switch (t->tag) {
    case BoolType: return (double)true;
    case CharType: return (double)CHAR_MAX;
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
    case VariantType: return type_max_magnitude(Match(t, VariantType)->variant_of);
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

    if (is_numeric(actual) && is_numeric(needed)) {
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

    // If we have a DSL, it should be possible to use it as a Str
    if (is_variant_of(actual, needed))
        return true;

    if (actual->tag == StructType && base_variant(needed)->tag == StructType) {
        auto actual_struct = Match(actual, StructType);
        auto needed_struct = Match(base_variant(needed), StructType);
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
    case ArrayType: case IntType: case NumType: case CharType: case BoolType:
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
        case VariantType: {
            const char *name = Match(t, VariantType)->name;
            if (name && Table_str_get(seen, name))
                return true;
            Table_str_set(seen, name, t);
            return _can_have_cycles(Match(t, VariantType)->variant_of, seen);
        }
        default: return false;
    }
}

bool can_have_cycles(type_t *t)
{
    table_t seen = {0};
    return _can_have_cycles(t, &seen);
}

type_t *table_entry_type(type_t *table_type)
{
    static table_t cache = {0};
    arg_t *fields = new(
        arg_t, .name="key",
        .type=Match(table_type, TableType)->key_type);
    fields->next = new(
        arg_t, .name="value",
        .type=Match(table_type, TableType)->value_type);
    type_t *t = Type(StructType, .fields=fields);
    type_t *cached = Table_str_get(&cache, type_to_string(t));
    if (cached) {
        return cached;
    } else {
        Table_str_set(&cache, type_to_string(t), t);
        return t;
    }
}

type_t *base_variant(type_t *t)
{
    while (t->tag == VariantType)
        t = Match(t, VariantType)->variant_of;
    return t;
}

bool is_variant_of(type_t *t, type_t *base)
{
    for (; t->tag == VariantType; t = Match(t, VariantType)->variant_of) {
        if (type_eq(Match(t, VariantType)->variant_of, base))
            return true;
    }
    return false;
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
        case VariantType: return REPLACED_MEMBER(t, VariantType, variant_of);
        default: return t;
    }
#undef COPY
#undef REPLACED_MEMBER
}

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
