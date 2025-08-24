// Logic for compiling new struct types defined in code
#include <gc.h>

#include "../ast.h"
#include "../compile.h"
#include "../environment.h"
#include "../naming.h"
#include "../stdlib/tables.h"
#include "../stdlib/text.h"
#include "../typecheck.h"

Text_t compile_struct_typeinfo(env_t *env, type_t *t, const char *name, arg_ast_t *fields, bool is_secret,
                               bool is_opaque) {
    Text_t typeinfo_name = namespace_name(env, env->namespace, Texts(name, "$$info"));
    Text_t type_code = Match(t, StructType)->external
                           ? Text$from_str(name)
                           : Texts("struct ", namespace_name(env, env->namespace, Texts(name, "$$struct")));

    int num_fields = 0;
    for (arg_ast_t *f = fields; f; f = f->next)
        num_fields += 1;
    const char *short_name = name;
    if (strchr(short_name, '$')) short_name = strrchr(short_name, '$') + 1;

    const char *metamethods = is_packed_data(t) ? "PackedData$metamethods" : "Struct$metamethods";
    Text_t typeinfo = Texts(
        "public const TypeInfo_t ", typeinfo_name, " = {.size=sizeof(", type_code, "), .align=__alignof__(", type_code,
        "), "
        ".metamethods=",
        metamethods,
        ", "
        ".tag=StructInfo, .StructInfo.name=\"",
        short_name, "\"", is_secret ? Text(", .StructInfo.is_secret=true") : EMPTY_TEXT,
        is_opaque ? Text(", .StructInfo.is_opaque=true") : EMPTY_TEXT, ", .StructInfo.num_fields=", String(num_fields));
    if (fields) {
        typeinfo = Texts(typeinfo, ", .StructInfo.fields=(NamedType_t[", String(num_fields), "]){");
        for (arg_ast_t *f = fields; f; f = f->next) {
            type_t *field_type = get_arg_ast_type(env, f);
            typeinfo = Texts(typeinfo, "{\"", f->name, "\", ", compile_type_info(field_type), "}");
            if (f->next) typeinfo = Texts(typeinfo, ", ");
        }
        typeinfo = Texts(typeinfo, "}");
    }
    return Texts(typeinfo, "};\n");
}

Text_t compile_struct_header(env_t *env, ast_t *ast) {
    DeclareMatch(def, ast, StructDef);
    Text_t typeinfo_name = namespace_name(env, env->namespace, Texts(def->name, "$$info"));
    Text_t type_code = def->external
                           ? Text$from_str(def->name)
                           : Texts("struct ", namespace_name(env, env->namespace, Texts(def->name, "$$struct")));

    Text_t fields = EMPTY_TEXT;
    for (arg_ast_t *field = def->fields; field; field = field->next) {
        type_t *field_t = get_arg_ast_type(env, field);
        type_t *check_for_opaque = non_optional(field_t);
        if (check_for_opaque->tag == StructType && Match(check_for_opaque, StructType)->opaque) {
            if (field->type)
                code_err(field->type, "This is an opaque type, so it can't be used as a struct field type");
            else if (field->value)
                code_err(field->value, "This is an opaque type, so it can't be used as a struct field type");
        }
        fields = Texts(fields, compile_declaration(field_t, valid_c_name(field->name)),
                       field_t->tag == BoolType ? Text(":1") : EMPTY_TEXT, ";\n");
    }
    Text_t struct_code = def->external ? EMPTY_TEXT : Texts(type_code, " {\n", fields, "};\n");
    type_t *t = Table$str_get(*env->types, def->name);

    Text_t unpadded_size =
        def->opaque ? Texts("sizeof(", type_code, ")") : Text$from_str(String((int64_t)unpadded_struct_size(t)));
    Text_t typeinfo_code = Texts("extern const TypeInfo_t ", typeinfo_name, ";\n");
    Text_t optional_code = EMPTY_TEXT;
    if (!def->opaque) {
        optional_code = Texts("DEFINE_OPTIONAL_TYPE(", compile_type(t), ", ", unpadded_size, ", ",
                              namespace_name(env, env->namespace, Texts("$Optional", def->name, "$$type")), ");\n");
    }
    return Texts(struct_code, optional_code, typeinfo_code);
}
// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
