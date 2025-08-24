// Type infos and methods for Pointer types
#include <err.h>
#include <gc.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/param.h>

#include "integers.h"
#include "metamethods.h"
#include "tables.h"
#include "text.h"
#include "types.h"
#include "util.h"

public
Text_t Pointer$as_text(const void *x, bool colorize, const TypeInfo_t *type) {
    __typeof(type->PointerInfo) ptr_info = type->PointerInfo;
    if (!x) {
        Text_t typename = generic_as_text(NULL, false, ptr_info.pointed);
        if (colorize) return Text$concat(Text("\x1b[34;1m"), Text$from_str(ptr_info.sigil), typename, Text("\x1b[m"));
        else return Text$concat(Text$from_str(ptr_info.sigil), typename);
    }
    const void *ptr = *(const void **)x;
    if (!ptr) {
        Text_t typename = generic_as_text(NULL, false, ptr_info.pointed);
        if (colorize) return Text$concat(Text("\x1b[34;1m!"), typename, Text("\x1b[m"));
        else return Text$concat(Text("!"), typename);
    }

    static const void *root = NULL;
    static Table_t pending = {};
    bool top_level = (root == NULL);

    // Check for recursive references, so if `x.foo = x`, then it prints as
    // `@Foo{foo=@~1}` instead of overflowing the stack:
    if (top_level) {
        root = ptr;
    } else if (ptr == root) {
        Text_t text = Texts(Text$from_str(ptr_info.sigil), Text("~1"));
        return colorize ? Texts(Text("\x1b[34;1m"), text, Text("\x1b[m")) : text;
    } else {
        TypeInfo_t rec_table = *Table$info(type, &Int64$info);
        int64_t *id = Table$get(pending, x, &rec_table);
        if (id) {
            Text_t text = Texts(Text$from_str(ptr_info.sigil), Int64$as_text(id, false, &Int64$info));
            return colorize ? Texts(Text("\x1b[34;1m"), text, Text("\x1b[m")) : text;
        }
        int64_t next_id = pending.entries.length + 2;
        Table$set(&pending, x, &next_id, &rec_table);
    }

    Text_t pointed = generic_as_text(ptr, colorize, ptr_info.pointed);

    if (top_level) {
        pending = (Table_t){}; // Restore
        root = NULL;
    }

    Text_t text;
    if (colorize) text = Text$concat(Text("\x1b[34;1m"), Text$from_str(ptr_info.sigil), Text("\x1b[m"), pointed);
    else text = Text$concat(Text$from_str(ptr_info.sigil), pointed);
    return text;
}

PUREFUNC public int32_t Pointer$compare(const void *x, const void *y, const TypeInfo_t *info) {
    (void)info;
    const void *xp = *(const void **)x, *yp = *(const void **)y;
    return (xp > yp) - (xp < yp);
}

PUREFUNC public bool Pointer$equal(const void *x, const void *y, const TypeInfo_t *info) {
    (void)info;
    const void *xp = *(const void **)x, *yp = *(const void **)y;
    return xp == yp;
}

PUREFUNC public bool Pointer$is_none(const void *x, const TypeInfo_t *info) {
    (void)info;
    return *(void **)x == NULL;
}

public
void Pointer$serialize(const void *obj, FILE *out, Table_t *pointers, const TypeInfo_t *type) {
    void *ptr = *(void **)obj;
    assert(ptr != NULL);

    const TypeInfo_t ptr_to_int_table = {.size = sizeof(Table_t),
                                         .align = __alignof__(Table_t),
                                         .tag = TableInfo,
                                         .TableInfo.key = type,
                                         .TableInfo.value = &Int64$info};

    int64_t *id_ptr = Table$get(*pointers, &ptr, &ptr_to_int_table);
    int64_t id;
    if (id_ptr) {
        id = *id_ptr;
    } else {
        id = pointers->entries.length + 1;
        Table$set(pointers, &ptr, &id, &ptr_to_int_table);
    }

    Int64$serialize(&id, out, pointers, &Int64$info);

    if (!id_ptr) _serialize(ptr, out, pointers, type->PointerInfo.pointed);
}

public
void Pointer$deserialize(FILE *in, void *outval, List_t *pointers, const TypeInfo_t *type) {
    int64_t id = 0;
    Int64$deserialize(in, &id, pointers, &Int64$info);
    assert(id != 0);

    if (id > pointers->length) {
        void *obj = GC_MALLOC((size_t)type->PointerInfo.pointed->size);
        List$insert(pointers, &obj, I(0), sizeof(void *));
        _deserialize(in, obj, pointers, type->PointerInfo.pointed);
        *(void **)outval = obj;
    } else {
        *(void **)outval = *(void **)(pointers->data + (id - 1) * pointers->stride);
    }
}
