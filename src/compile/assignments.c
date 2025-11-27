// This file defines how to compile assignments

#include "../ast.h"
#include "../environment.h"
#include "../stdlib/datatypes.h"
#include "../stdlib/text.h"
#include "../stdlib/util.h"
#include "../typecheck.h"
#include "compilation.h"

public
Text_t compile_update_assignment(env_t *env, ast_t *ast) {
    if (!is_update_assignment(ast)) code_err(ast, "This is not an update assignment");

    binary_operands_t update = UPDATE_OPERANDS(ast);

    type_t *lhs_t = get_type(env, update.lhs);

    bool needs_idemotency_fix = !is_idempotent(update.lhs);
    Text_t lhs = needs_idemotency_fix ? Text("(*lhs)") : compile_lvalue(env, update.lhs);

    Text_t update_assignment = EMPTY_TEXT;
    switch (ast->tag) {
    case PlusUpdate: {
        if (lhs_t->tag == IntType || lhs_t->tag == NumType || lhs_t->tag == ByteType)
            update_assignment = Texts(lhs, " += ", compile_to_type(env, update.rhs, lhs_t), ";");
        break;
    }
    case MinusUpdate: {
        if (lhs_t->tag == IntType || lhs_t->tag == NumType || lhs_t->tag == ByteType)
            update_assignment = Texts(lhs, " -= ", compile_to_type(env, update.rhs, lhs_t), ";");
        break;
    }
    case MultiplyUpdate: {
        if (lhs_t->tag == IntType || lhs_t->tag == NumType || lhs_t->tag == ByteType)
            update_assignment = Texts(lhs, " *= ", compile_to_type(env, update.rhs, lhs_t), ";");
        break;
    }
    case DivideUpdate: {
        if (lhs_t->tag == IntType || lhs_t->tag == NumType || lhs_t->tag == ByteType)
            update_assignment = Texts(lhs, " /= ", compile_to_type(env, update.rhs, lhs_t), ";");
        break;
    }
    case LeftShiftUpdate: {
        if (lhs_t->tag == IntType || lhs_t->tag == ByteType)
            update_assignment = Texts(lhs, " <<= ", compile_to_type(env, update.rhs, lhs_t), ";");
        break;
    }
    case RightShiftUpdate: {
        if (lhs_t->tag == IntType || lhs_t->tag == ByteType)
            update_assignment = Texts(lhs, " >>= ", compile_to_type(env, update.rhs, lhs_t), ";");
        break;
    }
    case AndUpdate: {
        if (lhs_t->tag == BoolType)
            update_assignment =
                Texts("if (", lhs, ") ", lhs, " = ", compile_to_type(env, update.rhs, Type(BoolType)), ";");
        break;
    }
    case OrUpdate: {
        if (lhs_t->tag == BoolType)
            update_assignment =
                Texts("if (!", lhs, ") ", lhs, " = ", compile_to_type(env, update.rhs, Type(BoolType)), ";");
        break;
    }
    default: break;
    }

    if (update_assignment.length == 0) {
        ast_t *binop = new (ast_t);
        *binop = *ast;
        binop->tag = binop_tag(binop->tag);
        if (needs_idemotency_fix) binop->__data.Plus.lhs = LiteralCode(Text("*lhs"), .type = lhs_t);
        update_assignment = Texts(lhs, " = ", compile_to_type(env, binop, lhs_t), ";");
    }

    if (needs_idemotency_fix)
        return Texts("{ ", compile_declaration(Type(PointerType, .pointed = lhs_t), Text("lhs")), " = &",
                     compile_lvalue(env, update.lhs), "; ", update_assignment, "; }");
    else return update_assignment;
}

public
Text_t compile_assignment(env_t *env, ast_t *target, Text_t value) {
    return Texts(compile_lvalue(env, target), " = ", value);
}

public
Text_t compile_assignment_statement(env_t *env, ast_t *ast) {
    DeclareMatch(assign, ast, Assign);
    // Single assignment, no temp vars needed:
    if (assign->targets && !assign->targets->next) {
        type_t *lhs_t = get_type(env, assign->targets->ast);
        if (assign->targets->ast->tag == Index && lhs_t->tag == OptionalType
            && (value_type(get_type(env, Match(assign->targets->ast, Index)->indexed))->tag == TableType
                || value_type(get_type(env, Match(assign->targets->ast, Index)->indexed))->tag == ListType))
            lhs_t = Match(lhs_t, OptionalType)->type;
        if (has_stack_memory(lhs_t))
            code_err(ast, "Stack references cannot be assigned to "
                          "variables because the "
                          "variable's scope may outlive the scope of the "
                          "stack memory.");
        env_t *val_env = with_enum_scope(env, lhs_t);
        Text_t val = compile_maybe_incref(val_env, assign->values->ast, lhs_t);
        return Texts(compile_assignment(env, assign->targets->ast, val), ";\n");
    }

    Text_t code = Text("{ // Assignment\n");
    int64_t i = 1;
    for (ast_list_t *value = assign->values, *target = assign->targets; value && target;
         value = value->next, target = target->next) {
        type_t *lhs_t = get_type(env, target->ast);
        if (target->ast->tag == Index && lhs_t->tag == OptionalType
            && (value_type(get_type(env, Match(target->ast, Index)->indexed))->tag == TableType
                || value_type(get_type(env, Match(target->ast, Index)->indexed))->tag == ListType))
            lhs_t = Match(lhs_t, OptionalType)->type;
        if (has_stack_memory(lhs_t))
            code_err(ast, "Stack references cannot be assigned to "
                          "variables because the "
                          "variable's scope may outlive the scope of the "
                          "stack memory.");
        env_t *val_env = with_enum_scope(env, lhs_t);
        Text_t val = compile_maybe_incref(val_env, value->ast, lhs_t);
        code = Texts(code, compile_type(lhs_t), " $", i, " = ", val, ";\n");
        i += 1;
    }
    i = 1;
    for (ast_list_t *target = assign->targets; target; target = target->next) {
        code = Texts(code, compile_assignment(env, target->ast, Texts("$", i)), ";\n");
        i += 1;
    }
    return Texts(code, "\n}");
}

public
Text_t compile_lvalue(env_t *env, ast_t *ast) {
    if (!can_be_mutated(env, ast)) {
        if (ast->tag == Index) {
            ast_t *subject = Match(ast, Index)->indexed;
            code_err(subject, "This is an immutable value, you can't mutate "
                              "its contents");
        } else if (ast->tag == FieldAccess) {
            ast_t *subject = Match(ast, FieldAccess)->fielded;
            type_t *t = get_type(env, subject);
            code_err(subject, "This is an immutable ", type_to_text(t), " value, you can't assign to its fields");
        } else {
            code_err(ast, "This is a value of type ", type_to_text(get_type(env, ast)),
                     " and can't be used as an assignment target");
        }
    }

    if (ast->tag == Index) {
        DeclareMatch(index, ast, Index);
        type_t *container_t = get_type(env, index->indexed);
        if (container_t->tag == OptionalType)
            code_err(index->indexed, "This value might be none, so it can't be "
                                     "safely used as an assignment target");

        if (!index->index && container_t->tag == PointerType) return compile(env, ast);

        container_t = value_type(container_t);
        type_t *index_t = get_type(env, index->index);
        if (container_t->tag == ListType) {
            if (!index->index) code_err(ast, "This list needs an index");
            Text_t target_code = compile_to_pointer_depth(env, index->indexed, 1, false);
            type_t *item_type = Match(container_t, ListType)->item_type;
            Text_t index_code =
                index->index->tag == Int
                    ? compile_int_to_type(env, index->index, Type(IntType, .bits = TYPE_IBITS64))
                    : (index_t->tag == BigIntType ? Texts("Int64$from_int(", compile(env, index->index), ", no)")
                                                  : Texts("(Int64_t)(", compile(env, index->index), ")"));
            return Texts("List_lvalue(", compile_type(item_type), ", ", target_code, ", ", index_code, ", ",
                         (int64_t)(ast->start - ast->file->text), ", ", (int64_t)(ast->end - ast->file->text), ")");
        } else if (container_t->tag == TableType) {
            if (!index->index) code_err(ast, "This table needs an index");
            DeclareMatch(table_type, container_t, TableType);
            if (table_type->default_value) {
                type_t *value_type = get_type(env, table_type->default_value);
                return Texts("*Table$get_or_setdefault(", compile_to_pointer_depth(env, index->indexed, 1, false), ", ",
                             compile_type(table_type->key_type), ", ", compile_type(value_type), ", ",
                             compile_maybe_incref(env, index->index, table_type->key_type), ", ",
                             compile_maybe_incref(env, table_type->default_value, table_type->value_type), ", ",
                             compile_type_info(container_t), ")");
            }
            return Texts("*(", compile_type(Type(PointerType, table_type->value_type)), ")Table$reserve(",
                         compile_to_pointer_depth(env, index->indexed, 1, false), ", ", "stack(",
                         compile_maybe_incref(env, index->index, table_type->key_type), ")", ", NULL,",
                         compile_type_info(container_t), ")");
        } else {
            code_err(ast, "I don't know how to assign to this target");
        }
    } else if (ast->tag == Var || ast->tag == FieldAccess || ast->tag == InlineCCode) {
        return compile(env, ast);
    } else {
        code_err(ast, "I don't know how to assign to this");
    }
    return EMPTY_TEXT;
}
