// This file defines logic for compiling expressions
#include "expressions.h"
#include "../ast.h"
#include "../config.h"
#include "../environment.h"
#include "../stdlib/reals.h"
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
    case RealType: return Text("Real$from_float64(0.0)");
    case StructType: return compile_empty_struct(t);
    case EnumType: return compile_empty_enum(t);
    default: return EMPTY_TEXT;
    }
    return EMPTY_TEXT;
}

Text_t compile_real(ast_t *ast, Real_t num) {
    if (!Real$is_boxed(num)) {
        return Texts("Real$from_float64(", Real$value_as_text(num), ")");
    }

    switch (Real$tag(num)) {
    case REAL_TAG_BIGINT: {
        Int_t *b = REAL_BIGINT(num);
        assert(b->small != 0);

        mpz_t i;
        mpz_init_set_int(i, *b);
        char *c_literal;
        gmp_asprintf(&c_literal, "%#Zd", i);

        if (mpz_cmp_si(i, INT64_MAX) <= 0 && mpz_cmp_si(i, INT64_MIN) >= 0) {
            return Texts("Real$from_int64(", c_literal, ")");
        } else {
            return Texts("Real$from_int(Int$from_str(\"", *b, "\"))");
        }
    }
    case REAL_TAG_RATIONAL: {
        rational_t *r = REAL_RATIONAL(num);

        // Check if denominator is 1 (integer)
        if (mpz_cmp_ui(mpq_denref(&r->value), 1) == 0) {
            Int_t b = Int$from_mpz(mpq_numref(&r->value));
            Real_t b_real;
            b_real.bigint = &b;
            b_real.bits |= REAL_TAG_BIGINT;
            return compile_real(ast, b_real);
        }

        mpz_t num_copy;
        mpz_init_set(num_copy, mpq_numref(&r->value));

        mpz_t den_copy;
        mpz_init_set(den_copy, mpq_denref(&r->value));

        if (mpz_cmp_si(mpq_numref(&r->value), INT64_MAX) <= 0 && mpz_cmp_si(mpq_numref(&r->value), INT64_MIN) >= 0
            && mpz_cmp_si(mpq_denref(&r->value), INT64_MAX) <= 0 && mpz_cmp_si(mpq_denref(&r->value), INT64_MIN) >= 0) {
            char *num_str = mpz_get_str(NULL, 10, mpq_numref(&r->value));
            char *den_str = mpz_get_str(NULL, 10, mpq_denref(&r->value));
            return Texts("Real$from_rational(", num_str, ", ", den_str, ")");
        } else {
            Int_t numerator = Int$from_mpz(mpq_numref(&r->value));
            Real_t num_real;
            num_real.bigint = &numerator;
            num_real.bits |= REAL_TAG_BIGINT;

            Int_t denominator = Int$from_mpz(mpq_denref(&r->value));
            Real_t den_real;
            den_real.bigint = &denominator;
            den_real.bits |= REAL_TAG_BIGINT;

            return Texts("Real$divided_by(", compile_real(ast, num_real), ", ", compile_real(ast, den_real), ")");
        }
    }
    case REAL_TAG_CONSTRUCTIVE: {
        code_err(ast, "Constructive reals can't be compiled yet");
    }
    case REAL_TAG_SYMBOLIC: {
        symbolic_t *s = REAL_SYMBOLIC(num);

#define BINOP(name) Texts("Real$" name "(", compile_real(ast, s->left), ", ", compile_real(ast, s->right), ")")
#define UNOP(name) Texts("Real$" name "(", compile_real(ast, s->left), ")")
        switch (s->op) {
        case SYM_INVALID: code_err(ast, "Invalid real number");
        case SYM_ADD: return BINOP("plus");
        case SYM_SUB: return BINOP("minus");
        case SYM_MUL: return BINOP("times");
        case SYM_DIV: return BINOP("divided_by");
        case SYM_MOD: return BINOP("mod");
        case SYM_SQRT: return UNOP("sqrt");
        case SYM_POW: return BINOP("power");
        case SYM_SIN: return UNOP("sin");
        case SYM_COS: return UNOP("cos");
        case SYM_TAN: return UNOP("tan");
        case SYM_ASIN: return UNOP("asin");
        case SYM_ACOS: return UNOP("acos");
        case SYM_ATAN: return UNOP("atan");
        case SYM_ATAN2: return BINOP("atan2");
        case SYM_EXP: return UNOP("exp");
        case SYM_LOG: return UNOP("log");
        case SYM_LOG10: return UNOP("log10");
        case SYM_ABS: return UNOP("abs");
        case SYM_FLOOR: return UNOP("floor");
        case SYM_CEIL: return UNOP("ceil");
        case SYM_PI: return Text("Real$pi");
        case SYM_TAU: return Text("Real$tau");
        case SYM_E: return Text("Real$e");
        default: code_err(ast, "Unsupported real type");
        }
    }
    default: code_err(ast, "Unsupported real type");
    }
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
    case Integer: return compile_int(ast);
    case Number: return compile_real(ast, Match(ast, Number)->n);
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

        if (t->tag == IntType || t->tag == FloatType || t->tag == RealType)
            return Texts("-(", compile(env, value), ")");

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
        else if (key_t->tag == RealType)
            comparison =
                Texts("(Real$compare_values(", lhs_key, ", ", rhs_key, ")", (ast->tag == Min ? "<=" : ">="), "0)");
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
