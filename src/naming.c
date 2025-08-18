// Logic for converting user's Tomo names into valid C identifiers

#include <ctype.h>
#include <string.h>
#include <sys/stat.h>

#include "environment.h"
#include "stdlib/paths.h"
#include "stdlib/print.h"
#include "stdlib/text.h"

public const Text_t SEP = {
    .length=1,
    .tag=TEXT_GRAPHEMES,
    .depth=0,
    .graphemes=(int32_t[1]){12541 /* U+30FD KATAKANA ITERATION MARK */},
};

public const Text_t ID_PREFIX = {
    .length=1,
    .tag=TEXT_GRAPHEMES,
    .depth=0,
    .graphemes=(int32_t[1]){12295 /* U+3007 IDEOGRAPHIC NUMBER ZERO */},
};

public const Text_t INTERNAL_PREFIX = {
    .length=1,
    .tag=TEXT_GRAPHEMES,
    .depth=0,
    .graphemes=(int32_t[1]){12293 /* U+3005 IDEOGRAPHIC ITERATION MARK */},
};

static const char *c_keywords[] = { // Maintain sorted order:
    "_Alignas", "_Alignof", "_Atomic", "_BitInt", "_Bool", "_Complex", "_Decimal128", "_Decimal32", "_Decimal64", "_Generic",
    "_Imaginary", "_Noreturn", "_Static_assert", "_Thread_local",
    "alignas", "alignof", "auto", "bool", "break", "case", "char", "const", "constexpr", "continue", "default", "do", "double",
    "else", "enum", "extern", "false", "float", "for", "goto", "if", "inline", "int", "long", "nullptr", "register", "restrict",
    "return", "short", "signed", "sizeof", "static", "static_assert", "struct", "switch", "thread_local", "true", "typedef",
    "typeof", "typeof_unqual", "union", "unsigned", "void", "volatile", "while",
};

static CONSTFUNC bool is_keyword(const char *word, size_t len) {
    int64_t lo = 0, hi = sizeof(c_keywords)/sizeof(c_keywords[0])-1;
    while (lo <= hi) {
        int64_t mid = (lo + hi) / 2;
        int32_t cmp = strncmp(word, c_keywords[mid], len);
        if (cmp == 0)
            return true;
        else if (cmp > 0)
            lo = mid + 1;
        else if (cmp < 0)
            hi = mid - 1;
    }
    return false;
}

public Text_t valid_c_name(const char *name)
{
    size_t len = strlen(name);
    size_t trailing_underscores = 0;
    while (trailing_underscores < len && name[len-1-trailing_underscores] == '_')
        trailing_underscores += 1;
    if (is_keyword(name, len-trailing_underscores)) {
        return Texts(Textヽfrom_str(name), Text("_"));
    }
    return Textヽfrom_str(name);
}

public Text_t CONSTFUNC namespace_name(env_t *env, namespace_t *ns, Text_t name)
{
    for (; ns; ns = ns->parent)
        name = Texts(ns->name, SEP, name);
    if (env->id_suffix.length > 0)
        name = Texts(name, env->id_suffix);
    return name;
}

public Text_t get_id_suffix(const char *filename)
{
    assert(filename);
    Path_t path = Pathヽfrom_str(filename);
    Path_t build_dir = Pathヽsibling(path, Text(".build"));
    if (mkdir(Pathヽas_c_string(build_dir), 0755) != 0) {
        if (!Pathヽis_directory(build_dir, true))
            err(1, "Could not make .build directory");
    }
    Path_t id_file = Pathヽchild(build_dir, Texts(Pathヽbase_name(path), Textヽfrom_str(".id")));
    OptionalText_t id = Pathヽread(id_file);
    if (id.length < 0) err(1, "Could not read ID file: ", id_file);
    return Texts(SEP, id);
}

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
