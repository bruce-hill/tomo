// This file defines logic for compiling expressions

#include "expressions.h"
#include "../ast.h"
#include "../config.h"
#include "../environment.h"
#include "../stdlib/tables.h"
#include "../stdlib/text.h"
#include "../stdlib/util.h"
#include "../typecheck.h"
#include "binops.h"
#include "blocks.h"
#include "declarations.h"
#include "enums.h"
#include "functions.h"
#include "integers.h"
#include "lists.h"
#include "optionals.h"
#include "pointers.h"
#include "promotions.h"
#include "sets.h"
#include "statements.h"
#include "structs.h"
#include "tables.h"
#include "text.h"
#include "types.h"

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

        code_err(ast, "I don't know how to negate values of type ", type_to_str(t));
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

        code_err(ast, "I don't know how to get the negative value of type ", type_to_str(t));
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
    case Xor: {
        return compile_binary_op(env, ast);
    }
    case Equals:
    case NotEquals: {
        binary_operands_t binop = BINARY_OPERANDS(ast);

        type_t *lhs_t = get_type(env, binop.lhs);
        type_t *rhs_t = get_type(env, binop.rhs);
        type_t *operand_t;
        if (binop.lhs->tag == Int && is_numeric_type(rhs_t)) {
            operand_t = rhs_t;
        } else if (binop.rhs->tag == Int && is_numeric_type(lhs_t)) {
            operand_t = lhs_t;
        } else if (can_compile_to_type(env, binop.rhs, lhs_t)) {
            operand_t = lhs_t;
        } else if (can_compile_to_type(env, binop.lhs, rhs_t)) {
            operand_t = rhs_t;
        } else {
            code_err(ast, "I can't do comparisons between ", type_to_str(lhs_t), " and ", type_to_str(rhs_t));
        }

        Text_t lhs, rhs;
        lhs = compile_to_type(env, binop.lhs, operand_t);
        rhs = compile_to_type(env, binop.rhs, operand_t);

        switch (operand_t->tag) {
        case BigIntType:
            return Texts(ast->tag == Equals ? EMPTY_TEXT : Text("!"), "Int$equal_value(", lhs, ", ", rhs, ")");
        case BoolType:
        case ByteType:
        case IntType:
        case NumType:
        case PointerType:
        case FunctionType: return Texts("(", lhs, ast->tag == Equals ? " == " : " != ", rhs, ")");
        default:
            return Texts(ast->tag == Equals ? EMPTY_TEXT : Text("!"), "generic_equal(stack(", lhs, "), stack(", rhs,
                         "), ", compile_type_info(operand_t), ")");
        }
    }
    case LessThan:
    case LessThanOrEquals:
    case GreaterThan:
    case GreaterThanOrEquals:
    case Compare: {
        binary_operands_t cmp = BINARY_OPERANDS(ast);

        type_t *lhs_t = get_type(env, cmp.lhs);
        type_t *rhs_t = get_type(env, cmp.rhs);
        type_t *operand_t;
        if (cmp.lhs->tag == Int && is_numeric_type(rhs_t)) {
            operand_t = rhs_t;
        } else if (cmp.rhs->tag == Int && is_numeric_type(lhs_t)) {
            operand_t = lhs_t;
        } else if (can_compile_to_type(env, cmp.rhs, lhs_t)) {
            operand_t = lhs_t;
        } else if (can_compile_to_type(env, cmp.lhs, rhs_t)) {
            operand_t = rhs_t;
        } else {
            code_err(ast, "I can't do comparisons between ", type_to_str(lhs_t), " and ", type_to_str(rhs_t));
        }

        Text_t lhs = compile_to_type(env, cmp.lhs, operand_t);
        Text_t rhs = compile_to_type(env, cmp.rhs, operand_t);

        if (ast->tag == Compare)
            return Texts("generic_compare(stack(", lhs, "), stack(", rhs, "), ", compile_type_info(operand_t), ")");

        const char *op = binop_operator(ast->tag);
        switch (operand_t->tag) {
        case BigIntType: return Texts("(Int$compare_value(", lhs, ", ", rhs, ") ", op, " 0)");
        case BoolType:
        case ByteType:
        case IntType:
        case NumType:
        case PointerType:
        case FunctionType: return Texts("(", lhs, " ", op, " ", rhs, ")");
        default:
            return Texts("(generic_compare(stack(", lhs, "), stack(", rhs, "), ", compile_type_info(operand_t), ") ",
                         op, " 0)");
        }
    }
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
        const char *key_name = "$";
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
            code_err(value, "This value should be a list of bytes, not a ", type_to_str(value_type));
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
    case When: {
        DeclareMatch(original, ast, When);
        ast_t *when_var = WrapAST(ast, Var, .name = "when");
        when_clause_t *new_clauses = NULL;
        type_t *subject_t = get_type(env, original->subject);
        for (when_clause_t *clause = original->clauses; clause; clause = clause->next) {
            type_t *clause_type = get_clause_type(env, subject_t, clause);
            if (clause_type->tag == AbortType || clause_type->tag == ReturnType) {
                new_clauses =
                    new (when_clause_t, .pattern = clause->pattern, .body = clause->body, .next = new_clauses);
            } else {
                ast_t *assign = WrapAST(clause->body, Assign, .targets = new (ast_list_t, .ast = when_var),
                                        .values = new (ast_list_t, .ast = clause->body));
                new_clauses = new (when_clause_t, .pattern = clause->pattern, .body = assign, .next = new_clauses);
            }
        }
        REVERSE_LIST(new_clauses);
        ast_t *else_body = original->else_body;
        if (else_body) {
            type_t *clause_type = get_type(env, else_body);
            if (clause_type->tag != AbortType && clause_type->tag != ReturnType) {
                else_body = WrapAST(else_body, Assign, .targets = new (ast_list_t, .ast = when_var),
                                    .values = new (ast_list_t, .ast = else_body));
            }
        }

        type_t *t = get_type(env, ast);
        env_t *when_env = fresh_scope(env);
        set_binding(when_env, "when", t, Text("when"));
        return Texts("({ ", compile_declaration(t, Text("when")), ";\n",
                     compile_statement(when_env, WrapAST(ast, When, .subject = original->subject,
                                                         .clauses = new_clauses, .else_body = else_body)),
                     "when; })");
    }
    case If: {
    }
    case Reduction: {
        DeclareMatch(reduction, ast, Reduction);
        ast_e op = reduction->op;

        type_t *iter_t = get_type(env, reduction->iter);
        type_t *item_t = get_iterated_type(iter_t);
        if (!item_t)
            code_err(reduction->iter, "I couldn't figure out how to iterate over this type: ", type_to_str(iter_t));

        static int64_t next_id = 1;
        ast_t *item = FakeAST(Var, String("$it", next_id++));
        ast_t *body = LiteralCode(Text("{}")); // placeholder
        ast_t *loop = FakeAST(For, .vars = new (ast_list_t, .ast = item), .iter = reduction->iter, .body = body);
        env_t *body_scope = for_scope(env, loop);
        if (op == Equals || op == NotEquals || op == LessThan || op == LessThanOrEquals || op == GreaterThan
            || op == GreaterThanOrEquals) {
            // Chained comparisons like ==, <, etc.
            type_t *item_value_type = item_t;
            ast_t *item_value = item;
            if (reduction->key) {
                set_binding(body_scope, "$", item_t, compile(body_scope, item));
                item_value = reduction->key;
                item_value_type = get_type(body_scope, reduction->key);
            }

            Text_t code = Texts("({ // Reduction:\n", compile_declaration(item_value_type, Text("prev")),
                                ";\n"
                                "OptionalBool_t result = NONE_BOOL;\n");

            ast_t *comparison = new (ast_t, .file = ast->file, .start = ast->start, .end = ast->end, .tag = op,
                                     .__data.Plus.lhs = LiteralCode(Text("prev"), .type = item_value_type),
                                     .__data.Plus.rhs = item_value);
            body->__data.InlineCCode.chunks = new (
                ast_list_t, .ast = FakeAST(TextLiteral, Texts("if (result == NONE_BOOL) {\n"
                                                              "    prev = ",
                                                              compile(body_scope, item_value),
                                                              ";\n"
                                                              "    result = yes;\n"
                                                              "} else {\n"
                                                              "    if (",
                                                              compile(body_scope, comparison), ") {\n",
                                                              "        prev = ", compile(body_scope, item_value), ";\n",
                                                              "    } else {\n"
                                                              "        result = no;\n",
                                                              "        break;\n", "    }\n", "}\n")));
            code = Texts(code, compile_statement(env, loop), "\nresult;})");
            return code;
        } else if (op == Min || op == Max) {
            // Min/max:
            Text_t superlative = op == Min ? Text("min") : Text("max");
            Text_t code = Texts("({ // Reduction:\n", compile_declaration(item_t, superlative),
                                ";\n"
                                "Bool_t has_value = no;\n");

            Text_t item_code = compile(body_scope, item);
            ast_e cmp_op = op == Min ? LessThan : GreaterThan;
            if (reduction->key) {
                env_t *key_scope = fresh_scope(env);
                set_binding(key_scope, "$", item_t, item_code);
                type_t *key_type = get_type(key_scope, reduction->key);
                Text_t superlative_key = op == Min ? Text("min_key") : Text("max_key");
                code = Texts(code, compile_declaration(key_type, superlative_key), ";\n");

                ast_t *comparison = new (ast_t, .file = ast->file, .start = ast->start, .end = ast->end, .tag = cmp_op,
                                         .__data.Plus.lhs = LiteralCode(Text("key"), .type = key_type),
                                         .__data.Plus.rhs = LiteralCode(superlative_key, .type = key_type));

                body->__data.InlineCCode.chunks = new (
                    ast_list_t, .ast = FakeAST(TextLiteral, Texts(compile_declaration(key_type, Text("key")), " = ",
                                                                  compile(key_scope, reduction->key), ";\n",
                                                                  "if (!has_value || ", compile(body_scope, comparison),
                                                                  ") {\n"
                                                                  "    ",
                                                                  superlative, " = ", compile(body_scope, item),
                                                                  ";\n"
                                                                  "    ",
                                                                  superlative_key,
                                                                  " = key;\n"
                                                                  "    has_value = yes;\n"
                                                                  "}\n")));
            } else {
                ast_t *comparison =
                    new (ast_t, .file = ast->file, .start = ast->start, .end = ast->end, .tag = cmp_op,
                         .__data.Plus.lhs = item, .__data.Plus.rhs = LiteralCode(superlative, .type = item_t));
                body->__data.InlineCCode.chunks = new (
                    ast_list_t, .ast = FakeAST(TextLiteral, Texts("if (!has_value || ", compile(body_scope, comparison),
                                                                  ") {\n"
                                                                  "    ",
                                                                  superlative, " = ", compile(body_scope, item),
                                                                  ";\n"
                                                                  "    has_value = yes;\n"
                                                                  "}\n")));
            }

            code = Texts(code, compile_statement(env, loop), "\nhas_value ? ", promote_to_optional(item_t, superlative),
                         " : ", compile_none(item_t), ";})");
            return code;
        } else {
            // Accumulator-style reductions like +, ++, *, etc.
            type_t *reduction_type = Match(get_type(env, ast), OptionalType)->type;
            ast_t *item_value = item;
            if (reduction->key) {
                set_binding(body_scope, "$", item_t, compile(body_scope, item));
                item_value = reduction->key;
            }

            Text_t code = Texts("({ // Reduction:\n", compile_declaration(reduction_type, Text("reduction")),
                                ";\n"
                                "Bool_t has_value = no;\n");

            // For the special case of (or)/(and), we need to early out if we
            // can:
            Text_t early_out = EMPTY_TEXT;
            if (op == Compare) {
                if (reduction_type->tag != IntType || Match(reduction_type, IntType)->bits != TYPE_IBITS32)
                    code_err(ast, "<> reductions are only supported for Int32 "
                                  "values");
            } else if (op == And) {
                if (reduction_type->tag == BoolType) early_out = Text("if (!reduction) break;");
                else if (reduction_type->tag == OptionalType)
                    early_out = Texts("if (", check_none(reduction_type, Text("reduction")), ") break;");
            } else if (op == Or) {
                if (reduction_type->tag == BoolType) early_out = Text("if (reduction) break;");
                else if (reduction_type->tag == OptionalType)
                    early_out = Texts("if (!", check_none(reduction_type, Text("reduction")), ") break;");
            }

            ast_t *combination = new (ast_t, .file = ast->file, .start = ast->start, .end = ast->end, .tag = op,
                                      .__data.Plus.lhs = LiteralCode(Text("reduction"), .type = reduction_type),
                                      .__data.Plus.rhs = item_value);
            body->__data.InlineCCode.chunks =
                new (ast_list_t,
                     .ast = FakeAST(TextLiteral, Texts("if (!has_value) {\n"
                                                       "    reduction = ",
                                                       compile(body_scope, item_value),
                                                       ";\n"
                                                       "    has_value = yes;\n"
                                                       "} else {\n"
                                                       "    reduction = ",
                                                       compile(body_scope, combination), ";\n", early_out, "}\n")));

            code = Texts(code, compile_statement(env, loop), "\nhas_value ? ",
                         promote_to_optional(reduction_type, Text("reduction")), " : ", compile_none(reduction_type),
                         ";})");
            return code;
        }
    }
    case FieldAccess: {
        DeclareMatch(f, ast, FieldAccess);
        type_t *fielded_t = get_type(env, f->fielded);
        type_t *value_t = value_type(fielded_t);
        switch (value_t->tag) {
        case TypeInfoType: {
            DeclareMatch(info, value_t, TypeInfoType);
            if (f->field[0] == '_') {
                if (!type_eq(env->current_type, info->type))
                    code_err(ast, "Fields that start with underscores are not "
                                  "accessible "
                                  "on types outside of the type definition.");
            }
            binding_t *b = get_binding(info->env, f->field);
            if (!b) code_err(ast, "I couldn't find the field '", f->field, "' on this type");
            if (b->code.length == 0) code_err(ast, "I couldn't figure out how to compile this field");
            return b->code;
        }
        case TextType: {
            const char *lang = Match(value_t, TextType)->lang;
            if (lang && streq(f->field, "text")) {
                Text_t text = compile_to_pointer_depth(env, f->fielded, 0, false);
                return Texts("((Text_t)", text, ")");
            } else if (streq(f->field, "length")) {
                return Texts("Int$from_int64((", compile_to_pointer_depth(env, f->fielded, 0, false), ").length)");
            }
            code_err(ast, "There is no '", f->field, "' field on ", type_to_str(value_t), " values");
        }
        case StructType: {
            return compile_struct_field_access(env, ast);
        }
        case EnumType: {
            return compile_enum_field_access(env, ast);
        }
        case ListType: {
            if (streq(f->field, "length"))
                return Texts("Int$from_int64((", compile_to_pointer_depth(env, f->fielded, 0, false), ").length)");
            code_err(ast, "There is no ", f->field, " field on lists");
        }
        case SetType: {
            if (streq(f->field, "items"))
                return Texts("LIST_COPY((", compile_to_pointer_depth(env, f->fielded, 0, false), ").entries)");
            else if (streq(f->field, "length"))
                return Texts("Int$from_int64((", compile_to_pointer_depth(env, f->fielded, 0, false),
                             ").entries.length)");
            code_err(ast, "There is no '", f->field, "' field on sets");
        }
        case TableType: {
            if (streq(f->field, "length")) {
                return Texts("Int$from_int64((", compile_to_pointer_depth(env, f->fielded, 0, false),
                             ").entries.length)");
            } else if (streq(f->field, "keys")) {
                return Texts("LIST_COPY((", compile_to_pointer_depth(env, f->fielded, 0, false), ").entries)");
            } else if (streq(f->field, "values")) {
                DeclareMatch(table, value_t, TableType);
                Text_t offset = Texts("offsetof(struct { ", compile_declaration(table->key_type, Text("k")), "; ",
                                      compile_declaration(table->value_type, Text("v")), "; }, v)");
                return Texts("({ List_t *entries = &(", compile_to_pointer_depth(env, f->fielded, 0, false),
                             ").entries;\n"
                             "LIST_INCREF(*entries);\n"
                             "List_t values = *entries;\n"
                             "values.data += ",
                             offset,
                             ";\n"
                             "values; })");
            } else if (streq(f->field, "fallback")) {
                return Texts("({ Table_t *_fallback = (", compile_to_pointer_depth(env, f->fielded, 0, false),
                             ").fallback; _fallback ? *_fallback : NONE_TABLE; })");
            }
            code_err(ast, "There is no '", f->field, "' field on tables");
        }
        case ModuleType: {
            const char *name = Match(value_t, ModuleType)->name;
            env_t *module_env = Table$str_get(*env->imports, name);
            return compile(module_env, WrapAST(ast, Var, f->field));
        }
        default: code_err(ast, "Field accesses are not supported on ", type_to_str(fielded_t), " values");
        }
    }
    case Index: {
        DeclareMatch(indexing, ast, Index);
        type_t *indexed_type = get_type(env, indexing->indexed);
        if (!indexing->index) {
            if (indexed_type->tag != PointerType)
                code_err(ast, "Only pointers can use the '[]' operator to "
                              "dereference "
                              "the entire value.");
            DeclareMatch(ptr, indexed_type, PointerType);
            if (ptr->pointed->tag == ListType) {
                return Texts("*({ List_t *list = ", compile(env, indexing->indexed), "; LIST_INCREF(*list); list; })");
            } else if (ptr->pointed->tag == TableType || ptr->pointed->tag == SetType) {
                return Texts("*({ Table_t *t = ", compile(env, indexing->indexed), "; TABLE_INCREF(*t); t; })");
            } else {
                return Texts("*(", compile(env, indexing->indexed), ")");
            }
        }

        type_t *container_t = value_type(indexed_type);
        type_t *index_t = get_type(env, indexing->index);
        if (container_t->tag == ListType) {
            if (index_t->tag != IntType && index_t->tag != BigIntType && index_t->tag != ByteType)
                code_err(indexing->index, "Lists can only be indexed by integers, not ", type_to_str(index_t));
            type_t *item_type = Match(container_t, ListType)->item_type;
            Text_t list = compile_to_pointer_depth(env, indexing->indexed, 0, false);
            file_t *f = indexing->index->file;
            Text_t index_code =
                indexing->index->tag == Int
                    ? compile_int_to_type(env, indexing->index, Type(IntType, .bits = TYPE_IBITS64))
                    : (index_t->tag == BigIntType ? Texts("Int64$from_int(", compile(env, indexing->index), ", no)")
                                                  : Texts("(Int64_t)(", compile(env, indexing->index), ")"));
            if (indexing->unchecked)
                return Texts("List_get_unchecked(", compile_type(item_type), ", ", list, ", ", index_code, ")");
            else
                return Texts("List_get(", compile_type(item_type), ", ", list, ", ", index_code, ", ",
                             String((int64_t)(indexing->index->start - f->text)), ", ",
                             String((int64_t)(indexing->index->end - f->text)), ")");
        } else if (container_t->tag == TableType) {
            DeclareMatch(table_type, container_t, TableType);
            if (indexing->unchecked) code_err(ast, "Table indexes cannot be unchecked");
            if (table_type->default_value) {
                return Texts("Table$get_or_default(", compile_to_pointer_depth(env, indexing->indexed, 0, false), ", ",
                             compile_type(table_type->key_type), ", ", compile_type(table_type->value_type), ", ",
                             compile(env, indexing->index), ", ",
                             compile_to_type(env, table_type->default_value, table_type->value_type), ", ",
                             compile_type_info(container_t), ")");
            } else {
                return Texts("Table$get_optional(", compile_to_pointer_depth(env, indexing->indexed, 0, false), ", ",
                             compile_type(table_type->key_type), ", ", compile_type(table_type->value_type), ", ",
                             compile(env, indexing->index),
                             ", "
                             "_, ",
                             promote_to_optional(table_type->value_type, Text("(*_)")), ", ",
                             compile_none(table_type->value_type), ", ", compile_type_info(container_t), ")");
            }
        } else if (container_t->tag == TextType) {
            return Texts("Text$cluster(", compile_to_pointer_depth(env, indexing->indexed, 0, false), ", ",
                         compile_to_type(env, indexing->index, Type(BigIntType)), ")");
        } else {
            code_err(ast, "Indexing is not supported for type: ", type_to_str(container_t));
        }
    }
    case InlineCCode: {
        type_t *t = get_type(env, ast);
        if (t->tag == VoidType) return Texts("{\n", compile_statement(env, ast), "\n}");
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
