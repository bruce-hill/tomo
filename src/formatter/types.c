// Logic for formatting types

#include "../ast.h"
#include "../stdlib/datatypes.h"
#include "../stdlib/stdlib.h"
#include "../stdlib/text.h"
#include "args.h"
#include "formatter.h"

Text_t format_type(type_ast_t *type) {
    switch (type->tag) {
    case VarTypeAST: return Text$from_str(Match(type, VarTypeAST)->name);
    case PointerTypeAST: {
        DeclareMatch(ptr, type, PointerTypeAST);
        return Texts(ptr->is_stack ? Text("&") : Text("@"), format_type(ptr->pointed));
    }
    case ListTypeAST: {
        return Texts("[", format_type(Match(type, ListTypeAST)->item), "]");
    }
    case SetTypeAST: {
        return Texts("|", format_type(Match(type, SetTypeAST)->item), "|");
    }
    case TableTypeAST: {
        DeclareMatch(table, type, TableTypeAST);
        Text_t code = Texts("{", format_type(table->key), "=", format_type(table->value));
        if (table->default_value) {
            OptionalText_t val = format_inline_code(table->default_value, (Table_t){});
            assert(val.length >= 0);
            code = Texts(code, "; default=", val);
        }
        return Texts(code, "}");
    }
    case FunctionTypeAST: {
        DeclareMatch(func, type, FunctionTypeAST);
        Text_t code = Texts("func(", format_inline_args(func->args, (Table_t){}));
        if (func->ret) code = Texts(code, func->args ? Text(" -> ") : Text("-> "), format_type(func->ret));
        return Texts(code, ")");
    }
    case OptionalTypeAST: {
        return Texts(format_type(Match(type, OptionalTypeAST)->type), "?");
    }
    case UnknownTypeAST:
    default: fail("Invalid Type AST");
    }
}
