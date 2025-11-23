// Logic for converting user's Tomo names into valid C identifiers

#include <string.h>
#include <sys/stat.h>

#include "environment.h"
#include "stdlib/paths.h"
#include "stdlib/text.h"

static const char *c_keywords[] = {
    // Maintain sorted order:
    "_Alignas",
    "_Alignof",
    "_Atomic",
    "_BitInt",
    "_Bool",
    "_Complex",
    "_Decimal128",
    "_Decimal32",
    "_Decimal64",
    "_Generic",
    "_Imaginary",
    "_Noreturn",
    "_Static_assert",
    "_Thread_local",
    "alignas",
    "__alignof__",
    "auto",
    "bool",
    "break",
    "case",
    "char",
    "const",
    "constexpr",
    "continue",
    "default",
    "do",
    "double",
    "else",
    "enum",
    "extern",
    "false",
    "float",
    "for",
    "goto",
    "if",
    "inline",
    "int",
    "long",
    "nullptr",
    "register",
    "restrict",
    "return",
    "short",
    "signed",
    "sizeof",
    "static",
    "static_assert",
    "struct",
    "switch",
    "thread_local",
    "true",
    "typedef",
    "typeof",
    "typeof_unqual",
    "union",
    "unsigned",
    "void",
    "volatile",
    "while",
};

static CONSTFUNC bool is_keyword(const char *word, size_t len) {
    int64_t lo = 0, hi = sizeof(c_keywords) / sizeof(c_keywords[0]) - 1;
    while (lo <= hi) {
        int64_t mid = (lo + hi) / 2;
        int32_t cmp = strncmp(word, c_keywords[mid], len + 1);
        if (cmp == 0) return true;
        else if (cmp > 0) lo = mid + 1;
        else if (cmp < 0) hi = mid - 1;
    }
    return false;
}

public
Text_t valid_c_name(const char *name) {
    size_t len = strlen(name);
    size_t trailing_underscores = 0;
    while (trailing_underscores < len && name[len - 1 - trailing_underscores] == '_')
        trailing_underscores += 1;
    if (is_keyword(name, len - trailing_underscores)) {
        return Texts(Text$from_str(name), Text("_"));
    }
    return Text$from_str(name);
}

#include "stdlib/stdlib.h"
public
Text_t CONSTFUNC namespace_name(env_t *env, namespace_t *ns, Text_t name) {
    if (Text$has(name, Text("\n"))) fail("WTF??");
    for (; ns; ns = ns->parent) {
        if (strchr(ns->name, '\n')) fail("WTF");
        name = Texts(ns->name, "$", name);
    }
    if (env->id_suffix.length > 0) name = Texts(name, env->id_suffix);
    if (Text$has(env->id_suffix, Text("\n"))) fail("WTF?????");
    return name;
}

public
Text_t get_id_suffix(const char *filename) {
    assert(filename);
    Path_t path = Path$from_str(filename);
    Path_t build_dir = Path$sibling(path, Text(".build"));
    if (mkdir(Path$as_c_string(build_dir), 0755) != 0) {
        if (!Path$is_directory(build_dir, true)) err(1, "Could not make .build directory");
    }
    Path_t id_file = Path$child(build_dir, Texts(Path$base_name(path), Text$from_str(".id")));
    OptionalText_t id = Path$read(id_file);
    if (id.tag == TEXT_NONE) err(1, "Could not read ID file: %s", Path$as_c_string(id_file));
    id = Text$trim(id, Text(" \r\n"), true, true);
    return Texts("$", id);
}
