// This file defines logic for compiling expressions

#include "expressions.h"
#include "../ast.h"
#include "../config.h"
#include "../environment.h"
#include "../stdlib/text.h"
#include "../stdlib/util.h"
#include "../typecheck.h"
#include "compilation.h"

public
Text_t compile_maybe_incref(env_t *env, ast_t *ast, type_t *t) {
    if (is_idempotent(ast) && can_be_mutated(env, ast)) {
        if (t->tag == ListType) return Texts("LIST_COPY(", compile_to_type(env, ast, t), ")");
        else if (t->tag == TableType || t->tag == SetType)
            return Texts("TABLE_COPY(", compile_to_type(env, ast, t), ")");
    }
    return compile_to_type(env, ast, t);
}

public
Text_t compile_empty(type_t *t) {
    if (t == NULL) compiler_err(NULL, NULL, NULL, "I can't compile a value with no type");

    if (t->tag == OptionalType) return compile_none(t);

    if (t == PATH_TYPE) return Text("NONE_PATH");
    else if (t == PATH_TYPE_TYPE) return Text("((OptionalPathType_t){})");

    switch (t->tag) {
    case BigIntType: return Text("I(0)");
    case IntType: {
        switch (Match(t, IntType)->bits) {
        case TYPE_IBITS8: return Text("I8(0)");
        case TYPE_IBITS16: return Text("I16(0)");
        case TYPE_IBITS32: return Text("I32(0)");
        case TYPE_IBITS64: return Text("I64(0)");
        default: errx(1, "Invalid integer bit size");
        }
        break;
    }
    case ByteType: return Text("((Byte_t)0)");
    case BoolType: return Text("((Bool_t)no)");
    case ListType: return Text("((List_t){})");
    case TableType:
    case SetType: return Text("((Table_t){})");
    case TextType: return Text("Text(\"\")");
    case CStringType: return Text("\"\"");
    case PointerType: {
        DeclareMatch(ptr, t, PointerType);
        Text_t empty_pointed = compile_empty(ptr->pointed);
        return empty_pointed.length == 0 ? EMPTY_TEXT
                                         : Texts(ptr->is_stack ? Text("stack(") : Text("heap("), empty_pointed, ")");
    }
    case NumType: {
        return Match(t, NumType)->bits == TYPE_NBITS32 ? Text("N32(0.0f)") : Text("N64(0.0)");
    }
    case StructType: return compile_empty_struct(t);
    case EnumType: return compile_empty_enum(t);
    default: return EMPTY_TEXT;
    }
    return EMPTY_TEXT;
}

Text_t compile(env_t *env, ast_t *ast) {
    switch (ast->tag) {
    case None: {
        code_err(ast, "I can't figure out what this `none`'s type is!");
    }
    case Bool: return Match(ast, Bool)->b ? Text("yes") : Text("no");
    case Var: {
        binding_t *b = get_binding(env, Match(ast, Var)->name);
        if (b) return b->code.length > 0 ? b->code : Texts("_$", Match(ast, Var)->name);
        // return Texts("_$", Match(ast, Var)->name);
        code_err(ast, "I don't know of any variable by this name");
    }
    case Int: return compile_int(ast);
    case Num: {
        return Text$from_str(String(hex_double(Match(ast, Num)->n)));
    }
    case Not: {
        ast_t *value = Match(ast, Not)->value;
        type_t *t = get_type(env, value);

        binding_t *b = get_namespace_binding(env, value, "negated");
        if (b && b->type->tag == FunctionType) {
            DeclareMatch(fn, b->type, FunctionType);
            if (fn->args && can_compile_to_type(env, value, get_arg_type(env, fn->args)))
                return Texts(b->code, "(", compile_arguments(env, ast, fn->args, new (arg_ast_t, .value = value)), ")");
        }

        if (t->tag == BoolType) return Texts("!(", compile(env, value), ")");
        else if (t->tag == IntType || t->tag == ByteType) return Texts("~(", compile(env, value), ")");
        else if (t->tag == ListType) return Texts("((", compile(env, value), ").length == 0)");
        else if (t->tag == SetType || t->tag == TableType)
            return Texts("((", compile(env, value), ").entries.length == 0)");
        else if (t->tag == TextType) return Texts("(", compile(env, value), ".length == 0)");
        else if (t->tag == OptionalType) return check_none(t, compile(env, value));

        code_err(ast, "I don't know how to negate values of type ", type_to_text(t));
    }
    case Negative: {
        ast_t *value = Match(ast, Negative)->value;
        type_t *t = get_type(env, value);
        binding_t *b = get_namespace_binding(env, value, "negative");
        if (b && b->type->tag == FunctionType) {
            DeclareMatch(fn, b->type, FunctionType);
            if (fn->args && can_compile_to_type(env, value, get_arg_type(env, fn->args)))
                return Texts(b->code, "(", compile_arguments(env, ast, fn->args, new (arg_ast_t, .value = value)), ")");
        }

        if (t->tag == IntType || t->tag == NumType) return Texts("-(", compile(env, value), ")");

        code_err(ast, "I don't know how to get the negative value of type ", type_to_text(t));
    }
    case HeapAllocate:
    case StackReference: return compile_typed_allocation(env, ast, get_type(env, ast));
    case Optional: return compile_optional(env, ast);
    case NonOptional: return compile_non_optional(env, ast);
    case Power:
    case Multiply:
    case Divide:
    case Mod:
    case Mod1:
    case Plus:
    case Minus:
    case Concat:
    case LeftShift:
    case UnsignedLeftShift:
    case RightShift:
    case UnsignedRightShift:
    case And:
    case Or:
    case Xor: return compile_binary_op(env, ast);
    case Equals:
    case NotEquals:
    case LessThan:
    case LessThanOrEquals:
    case GreaterThan:
    case GreaterThanOrEquals:
    case Compare: return compile_comparison(env, ast);
    case TextLiteral:
    case TextJoin: return compile_text_ast(env, ast);
    case Path: {
        return Texts("Path(", compile_text_literal(Text$from_str(Match(ast, Path)->path)), ")");
    }
    case Block: return compile_block_expression(env, ast);
    case Min:
    case Max: {
        type_t *t = get_type(env, ast);
        ast_t *key = ast->tag == Min ? Match(ast, Min)->key : Match(ast, Max)->key;
        ast_t *lhs = ast->tag == Min ? Match(ast, Min)->lhs : Match(ast, Max)->lhs;
        ast_t *rhs = ast->tag == Min ? Match(ast, Min)->rhs : Match(ast, Max)->rhs;
        const char *key_name = ast->tag == Min ? "_min_" : "_max_";
        if (key == NULL) key = FakeAST(Var, key_name);

        env_t *expr_env = fresh_scope(env);
        set_binding(expr_env, key_name, t, Text("ternary$lhs"));
        Text_t lhs_key = compile(expr_env, key);

        set_binding(expr_env, key_name, t, Text("ternary$rhs"));
        Text_t rhs_key = compile(expr_env, key);

        type_t *key_t = get_type(expr_env, key);
        Text_t comparison;
        if (key_t->tag == BigIntType)
            comparison =
                Texts("(Int$compare_value(", lhs_key, ", ", rhs_key, ")", (ast->tag == Min ? "<=" : ">="), "0)");
        else if (key_t->tag == IntType || key_t->tag == NumType || key_t->tag == BoolType || key_t->tag == PointerType
                 || key_t->tag == ByteType)
            comparison = Texts("((", lhs_key, ")", (ast->tag == Min ? "<=" : ">="), "(", rhs_key, "))");
        else
            comparison = Texts("generic_compare(stack(", lhs_key, "), stack(", rhs_key, "), ", compile_type_info(key_t),
                               ")", (ast->tag == Min ? "<=" : ">="), "0");

        return Texts("({\n", compile_type(t), " ternary$lhs = ", compile(env, lhs),
                     ", ternary$rhs = ", compile(env, rhs), ";\n", comparison,
                     " ? ternary$lhs : ternary$rhs;\n"
                     "})");
    }
    case List: {
        DeclareMatch(list, ast, List);
        if (!list->items) return Text("(List_t){.length=0}");

        type_t *list_type = get_type(env, ast);
        return compile_typed_list(env, ast, list_type);
    }
    case Table: {
        DeclareMatch(table, ast, Table);
        if (!table->entries) {
            Text_t code = Text("((Table_t){");
            if (table->fallback) code = Texts(code, ".fallback=heap(", compile(env, table->fallback), ")");
            return Texts(code, "})");
        }

        type_t *table_type = get_type(env, ast);
        return compile_typed_table(env, ast, table_type);
    }
    case Set: {
        DeclareMatch(set, ast, Set);
        if (!set->items) return Text("((Table_t){})");

        type_t *set_type = get_type(env, ast);
        return compile_typed_set(env, ast, set_type);
    }
    case Comprehension: {
        ast_t *base = Match(ast, Comprehension)->expr;
        while (base->tag == Comprehension)
            base = Match(ast, Comprehension)->expr;
        if (base->tag == TableEntry) return compile(env, WrapAST(ast, Table, .entries = new (ast_list_t, .ast = ast)));
        else return compile(env, WrapAST(ast, List, .items = new (ast_list_t, .ast = ast)));
    }
    case Lambda: return compile_lambda(env, ast);
    case MethodCall: return compile_method_call(env, ast);
    case FunctionCall: return compile_function_call(env, ast);
    case Deserialize: {
        ast_t *value = Match(ast, Deserialize)->value;
        type_t *value_type = get_type(env, value);
        if (!type_eq(value_type, Type(ListType, Type(ByteType))))
            code_err(value, "This value should be a list of bytes, not a ", type_to_text(value_type));
        type_t *t = parse_type_ast(env, Match(ast, Deserialize)->type);
        return Texts("({ ", compile_declaration(t, Text("deserialized")),
                     ";\n"
                     "generic_deserialize(",
                     compile(env, value), ", &deserialized, ", compile_type_info(t),
                     ");\n"
                     "deserialized; })");
    }
    case ExplicitlyTyped: {
        return compile_to_type(env, Match(ast, ExplicitlyTyped)->ast, get_type(env, ast));
    }
    case When: return compile_when_expression(env, ast);
    case If: return compile_if_expression(env, ast);
    case Reduction: return compile_reduction(env, ast);
    case FieldAccess: return compile_field_access(env, ast);
    case Index: return compile_indexing(env, ast, false);
    case InlineCCode: {
        type_t *t = get_type(env, ast);
        if (Match(ast, InlineCCode)->type_ast != NULL) return Texts("({", compile_statement(env, ast), "; })");
        else if (t->tag == VoidType) return Texts("{\n", compile_statement(env, ast), "\n}");
        else return compile_statement(env, ast);
    }
    case Use: code_err(ast, "Compiling 'use' as expression!");
    case Defer: code_err(ast, "Compiling 'defer' as expression!");
    case Extern: code_err(ast, "Externs are not supported as expressions");
    case TableEntry: code_err(ast, "Table entries should not be compiled directly");
    case Declare:
    case Assign:
    case UPDATE_CASES:
    case For:
    case While:
    case Repeat:
    case StructDef:
    case LangDef:
    case Extend:
    case EnumDef:
    case FunctionDef:
    case ConvertDef:
    case Skip:
    case Stop:
    case Pass:
    case Return:
    case DocTest:
    case Assert: code_err(ast, "This is not a valid expression");
    case Unknown:
    default: code_err(ast, "Unknown AST: ", ast_to_sexp_str(ast));
    }
    return EMPTY_TEXT;
}
