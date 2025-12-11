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
    if (!has_refcounts(t) || !can_be_mutated(env, ast)) {
        return compile_to_type(env, ast, t);
    }

    // When using a struct as a value, we need to increment the refcounts of the inner fields as well:
    if (t->tag == StructType) {
        // If the struct is non-idempotent, we have to stash it in a local var first
        if (is_idempotent(ast)) {
            Text_t code = Texts("((", compile_type(t), "){");
            for (arg_t *field = Match(t, StructType)->fields; field; field = field->next) {
                Text_t val = compile_maybe_incref(env, WrapAST(ast, FieldAccess, .fielded = ast, .field = field->name),
                                                  get_arg_type(env, field));
                code = Texts(code, val);
                if (field->next) code = Texts(code, ", ");
            }
            return Texts(code, "})");
        } else {
            static int64_t tmp_index = 1;
            Text_t tmp_name = Texts("_tmp", tmp_index);
            tmp_index += 1;
            Text_t code = Texts("({ ", compile_declaration(t, tmp_name), " = ", compile_to_type(env, ast, t), "; ",
                                "((", compile_type(t), "){");
            ast_t *tmp = WrapLiteralCode(ast, tmp_name, .type = t);
            for (arg_t *field = Match(t, StructType)->fields; field; field = field->next) {
                Text_t val = compile_maybe_incref(env, WrapAST(ast, FieldAccess, .fielded = tmp, .field = field->name),
                                                  get_arg_type(env, field));
                code = Texts(code, val);
                if (field->next) code = Texts(code, ", ");
            }
            return Texts(code, "}); })");
        }
    } else if (t->tag == ListType && ast->tag != List && can_be_mutated(env, ast) && type_eq(get_type(env, ast), t)) {
        return Texts("LIST_COPY(", compile_to_type(env, ast, t), ")");
    } else if (t->tag == TableType && ast->tag != Table && can_be_mutated(env, ast) && type_eq(get_type(env, ast), t)) {
        return Texts("TABLE_COPY(", compile_to_type(env, ast, t), ")");
    }
    return compile_to_type(env, ast, t);
}

public
Text_t compile_empty(type_t *t) {
    if (t == NULL) compiler_err(NULL, NULL, NULL, "I can't compile a value with no type");

    if (t->tag == OptionalType) return compile_none(t);

    if (t == PATH_TYPE) return Text("NONE_PATH");

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
    case ListType: return Text("EMPTY_LIST");
    case TableType: return Text("EMPTY_TABLE");
    case TextType: return Text("EMPTY_TEXT");
    case CStringType: return Text("\"\"");
    case PointerType: {
        DeclareMatch(ptr, t, PointerType);
        Text_t empty_pointed = compile_empty(ptr->pointed);
        return empty_pointed.length == 0 ? EMPTY_TEXT
                                         : Texts(ptr->is_stack ? Text("stack(") : Text("heap("), empty_pointed, ")");
    }
    case FloatType: {
        return Match(t, FloatType)->bits == TYPE_NBITS32 ? Text("F32(0.0f)") : Text("F64(0.0)");
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
        type_t *type = Match(ast, None)->type;
        if (type == NULL) code_err(ast, "I can't figure out what this `none`'s type is!");
        return compile_none(non_optional(type));
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
        // return Text$from_str(String(hex_double(Match(ast, Num)->n)));
        Text_t original = Text$from_str(String(string_slice(ast->start, (size_t)(ast->end - ast->start))));
        // Text_t roundtrip = Text$from_str(String(Match(ast, Num)->n));
        // original = Text$replace(original, Text("_"), EMPTY_TEXT);
        // original = Text$without_suffix(original, Text("."));
        // if (Text$equal_values(original, roundtrip)) {
        //     return Texts("Real$from_float64(", Text$from_str(String(hex_double(Match(ast, Num)->n))), ")");
        // } else {
        return Texts("Real$parse(Text(\"", original, "\"), NULL)");
        // }
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
        else if (t->tag == TableType) return Texts("((", compile(env, value), ").entries.length == 0)");
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

        if (t->tag == IntType || t->tag == FloatType) return Texts("-(", compile(env, value), ")");

        code_err(ast, "I don't know how to get the negative value of type ", type_to_text(t));
    }
    case HeapAllocate:
    case StackReference: return compile_typed_allocation(env, ast, get_type(env, ast));
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
        else if (key_t->tag == IntType || key_t->tag == FloatType || key_t->tag == BoolType || key_t->tag == PointerType
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
        if (!list->items) return Text("EMPTY_LIST");

        type_t *list_type = get_type(env, ast);
        return compile_typed_list(env, ast, list_type);
    }
    case Table: {
        DeclareMatch(table, ast, Table);
        if (!table->entries) {
            Text_t code = Text("((Table_t){.entries=EMPTY_LIST");
            if (table->fallback) code = Texts(code, ", .fallback=heap(", compile(env, table->fallback), ")");
            return Texts(code, "})");
        }

        type_t *table_type = get_type(env, ast);
        return compile_typed_table(env, ast, table_type);
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
    case TableEntry: code_err(ast, "Table entries should not be compiled directly");
    case Declare:
    case Assign:
    case UPDATE_CASES:
    case For:
    case While:
    case Repeat:
    case StructDef:
    case LangDef:
    case EnumDef:
    case FunctionDef:
    case ConvertDef:
    case Skip:
    case Stop:
    case Pass:
    case Return:
    case DebugLog:
    case Assert: code_err(ast, "This is not a valid expression");
    case Unknown:
    default: code_err(ast, "Unknown AST: ", ast_to_sexp_str(ast));
    }
    return EMPTY_TEXT;
}
