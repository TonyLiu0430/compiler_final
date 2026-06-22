#pragma once

#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "base/literal.hpp"
#include "parser/parser.h"
#include "parser/reflect_dispatch.hpp"
#include "semantic/constant_evaluator.hpp"
#include "semantic/symbol.hpp"
#include "semantic/type_layout.hpp"

namespace c9ay::semantic {

class Compile_error : public std::runtime_error {
public:
    Compile_error(const std::string &message)
        : std::runtime_error(message) {}
};

class Semantic_analyzer {
    struct Scope {
        std::unordered_map<std::string, Symbol *> symbols;
        std::unordered_map<std::string, Type_ptr> types;
    };

    struct Switch_context {
        std::unordered_set<long long> cases;
        bool has_default = false;
    };

    Semantic_result result;
    std::vector<Scope> scopes;
    std::vector<Switch_context> switches;
    Type_ptr void_type;
    Type_ptr character_type;
    Type_ptr integer_type;
    Type_ptr current_return_type;
    Type_layout type_layout;
    int loop_depth = 0;

    [[noreturn]] static void error(std::string_view message) {
        throw Compile_error("compile error: " + std::string(message));
    }

    void push_scope() {
        scopes.emplace_back();
    }

    void pop_scope() {
        scopes.pop_back();
    }

    Type_ptr find_type(std::string_view name) {
        for (int i = static_cast<int>(scopes.size()) - 1; i >= 0; i--) {
            auto found = scopes[i].types.find(std::string(name));
            if (found != scopes[i].types.end()) return found->second;
        }
        return nullptr;
    }

    Symbol *find_symbol(std::string_view name) {
        for (int i = static_cast<int>(scopes.size()) - 1; i >= 0; i--) {
            auto found = scopes[i].symbols.find(std::string(name));
            if (found != scopes[i].symbols.end()) return found->second;
        }
        return nullptr;
    }

    Type_ptr base_type(
        lexer::Token token,
        bool is_const = false) {
        Type_ptr type = find_type(token.raw);
        if (!type) {
            error("unknown type '" + std::string(token.raw) + "'");
        }
        return qualify(type, is_const);
    }

    Symbol *make_symbol(
        Symbol::Kind kind,
        std::string_view name,
        Type_ptr type,
        const parser::Node *declaration) {
        auto symbol = std::make_unique<Symbol>();
        symbol->kind = kind;
        symbol->name = std::string(name);
        symbol->type = std::move(type);
        symbol->declaration = declaration;
        auto raw = symbol.get();
        result.symbols.push_back(std::move(symbol));
        return raw;
    }

    Symbol *insert_symbol(
        Symbol *symbol,
        bool allow_compatible_function = false) {
        auto &scope = scopes.back().symbols;
        auto found = scope.find(symbol->name);
        if (found != scope.end()) {
            if (allow_compatible_function &&
                found->second->kind == Symbol::Kind::FUNCTION &&
                symbol->kind == Symbol::Kind::FUNCTION &&
                same_type(found->second->type, symbol->type)) {
                return found->second;
            }
            error("redefinition of '" + symbol->name + "'");
        }
        scope[symbol->name] = symbol;
        return symbol;
    }

    std::optional<constant::Integer_value> evaluate_constant(
        const parser::Expression &expression) {
        Constant_evaluator evaluator(
            [this](const parser::Type_query_expression &query) {
                auto value = type_query_value(query);
                if (!value) {
                    return std::optional<constant::Integer_value>();
                }
                return std::optional<constant::Integer_value>(
                    constant::Integer_value{*value});
            });
        return evaluator.evaluate_expression(expression);
    }

    std::optional<long long> array_size(
        const parser::Expression *expression) {
        if (!expression) return std::nullopt;
        auto value = evaluate_constant(*expression);
        if (!value || value->value <= 0) {
            error("array size must be a positive integer constant");
        }
        return value->value;
    }

    Type_ptr type_name(const parser::Type_name &node) {
        Type_ptr type = base_type(node.type, node.is_const);
        if (!node.declarator) return type;

        for (int i = 0;
             i < node.declarator->pointer_depth;
             i++) {
            type = make_pointer(type);
        }
        for (int i =
                 static_cast<int>(
                     node.declarator->array_dimensions.size()) - 1;
             i >= 0;
             i--) {
            type = make_array(
                type,
                array_size(
                    node.declarator->array_dimensions[i].get()));
        }
        return type;
    }

    std::optional<long long> type_query_value(
        const parser::Type_query_expression &node) {
        auto cached = result.constant(node);
        if (cached) return cached;

        Type_ptr type;
        if (node.type) {
            type = type_name(*node.type);
        }
        else {
            type = analyze_expression(*node.operand).type;
        }

        std::optional<long long> value =
            node.op.match<lexer::token_type::K_ALIGNOF>()
                ? type_layout.alignment_of(type)
                : type_layout.size_of(type);
        if (!value) {
            error(
                std::string(node.op.raw) +
                " requires a complete object type");
        }
        result.constants[&node] = *value;
        return value;
    }

    Type_ptr declarator_type(
        Type_ptr base,
        const parser::Declarator &declarator) {
        Type_ptr type = std::move(base);
        for (int i = 0; i < declarator.pointer_depth; i++) {
            type = make_pointer(type);
        }
        for (int i =
                 static_cast<int>(declarator.array_dimensions.size()) - 1;
             i >= 0;
             i--) {
            type = make_array(
                type,
                array_size(declarator.array_dimensions[i].get()));
        }

        if (declarator.is_function) {
            std::vector<Type_ptr> parameters;
            bool void_parameter_list =
                declarator.parameters.size() == 1 &&
                !declarator.parameters[0]->declarator &&
                is_void(base_type(
                    declarator.parameters[0]->specifiers.type,
                    declarator.parameters[0]->specifiers.is_const));

            if (!void_parameter_list) {
                for (auto &parameter : declarator.parameters) {
                    auto parameter_base = base_type(
                        parameter->specifiers.type,
                        parameter->specifiers.is_const);
                    Type_ptr parameter_type = parameter_base;
                    if (parameter->declarator) {
                        parameter_type = declarator_type(
                            parameter_base,
                            *parameter->declarator);
                    }
                    if (as_type<Array_type>(parameter_type) ||
                        as_type<Function_type>(parameter_type)) {
                        parameter_type = parameter_type->decay();
                    }
                    if (is_void(parameter_type)) {
                        error("parameter has incomplete type void");
                    }
                    parameters.push_back(parameter_type);
                }
            }
            type = make_function(type, std::move(parameters));
        }
        if (declarator.nested) {
            type = declarator_type(type, *declarator.nested);
        }
        return type;
    }

    static bool assignable(Type_ptr lhs, Type_ptr rhs) {
        if (as_type<Array_type>(rhs) ||
            as_type<Function_type>(rhs)) {
            rhs = rhs->decay();
        }
        if (same_type(lhs, rhs)) return true;
        if (lhs->is_integer() && rhs->is_integer()) return true;
        auto lhs_pointer = as_type<Pointer_type>(lhs);
        auto rhs_pointer = as_type<Pointer_type>(rhs);
        if (lhs_pointer && rhs_pointer) {
            if (!lhs_pointer->element->is_const &&
                rhs_pointer->element->is_const) {
                return false;
            }
            if (is_void(lhs_pointer->element) ||
                is_void(rhs_pointer->element)) {
                return true;
            }
            auto lhs_element = lhs_pointer->element->clone();
            auto rhs_element = rhs_pointer->element->clone();
            lhs_element->is_const = false;
            rhs_element->is_const = false;
            return same_type(lhs_element, rhs_element);
        }
        return false;
    }

    bool is_null_pointer_constant(
        const parser::Expression &expression) {
        auto value = evaluate_constant(expression);
        return value && value->value == 0;
    }

    static bool is_object_pointer(const Type_ptr &type) {
        auto pointer = as_type<Pointer_type>(type);
        return pointer &&
               pointer->element &&
               pointer->element->is_complete();
    }

    Expression_info analyze_expression(const parser::Expression &node) {
        auto found = result.expressions.find(&node);
        if (found != result.expressions.end()) return found->second;

        auto info = parser::reflect::dispatch<Expression_info>(
            node,
            [this](const auto &expression) {
                return analyze_expression_node(expression);
            });
        result.expressions[&node] = info;
        return info;
    }

    Expression_info analyze_expression_node(
        const parser::Primary_expression &node) {
        Expression_info info;
        if (node.token.type == lexer::token_type::NUMBER ||
            node.token.type == lexer::token_type::CHAR_CONSTANT) {
            info.type = integer_type;
        }
        else if (node.token.type == lexer::token_type::STRING_CONSTANT) {
            auto value = literal::decode_string(node.token.raw);
            if (!value) error("invalid string literal");
            info.type = make_array(
                character_type,
                static_cast<long long>(value->size() + 1));
            info.is_lvalue = true;
        }
        else {
            auto symbol = find_symbol(node.token.raw);
            if (!symbol) {
                error(
                    "use of undeclared identifier '" +
                    std::string(node.token.raw) + "'");
            }
            info.type = symbol->type;
            info.symbol = symbol;
            info.is_lvalue =
                symbol->kind == Symbol::Kind::VARIABLE ||
                symbol->kind == Symbol::Kind::PARAMETER;
        }
        return info;
    }

    Expression_info analyze_expression_node(
        const parser::Prefix_expression &node) {
        auto operand = analyze_expression(*node.operand);
        Expression_info info;
        if (node.op.raw == "&") {
            if (!operand.is_lvalue &&
                !as_type<Function_type>(operand.type)) {
                error("address-of operand is not an lvalue");
            }
            info.type = make_pointer(operand.type);
        }
        else if (node.op.raw == "*") {
            auto type = as_type<Array_type>(operand.type)
                ? operand.type->decay()
                : operand.type;
            auto pointer = as_type<Pointer_type>(type);
            if (!pointer) {
                error("dereference operand is not a pointer");
            }
            info.type = pointer->element;
            info.is_lvalue =
                !as_type<Function_type>(info.type);
        }
        else if (node.op.raw == "++" || node.op.raw == "--") {
            if (!operand.is_lvalue ||
                operand.type->is_const ||
                !operand.type->is_scalar()) {
                error("increment operand is not a modifiable scalar lvalue");
            }
            if (as_type<Pointer_type>(operand.type) &&
                !is_object_pointer(operand.type)) {
                error("pointer arithmetic requires complete object type");
            }
            info.type = operand.type;
        }
        else {
            if (!operand.type->is_integer()) {
                error("unary operator requires integer operand");
            }
            info.type = integer_type;
        }
        return info;
    }

    Expression_info analyze_expression_node(
        const parser::Postfix_expression &node) {
        auto operand = analyze_expression(*node.operand);
        if (!operand.is_lvalue || operand.type->is_const ||
            !operand.type->is_scalar()) {
            error("postfix operand is not a modifiable scalar lvalue");
        }
        if (as_type<Pointer_type>(operand.type) &&
            !is_object_pointer(operand.type)) {
            error("pointer arithmetic requires complete object type");
        }
        return Expression_info{operand.type};
    }

    Expression_info analyze_expression_node(
        const parser::Binary_expression &node) {
        auto lhs = analyze_expression(*node.lhs);
        auto rhs = analyze_expression(*node.rhs);
        std::string_view op = node.op.raw;
        Expression_info info;

        if (op == "=") {
            if (!lhs.is_lvalue || lhs.type->is_const) {
                error("assignment target is not a modifiable lvalue");
            }
            if (!assignable(lhs.type, rhs.type) &&
                !(as_type<Pointer_type>(lhs.type) &&
                  is_null_pointer_constant(*node.rhs))) {
                error(
                    "cannot assign '" + rhs.type->name() +
                    "' to '" + lhs.type->name() + "'");
            }
            info.type = lhs.type;
        }
        else if (op.ends_with("=") &&
                 op != "==" && op != "!=" &&
                 op != "<=" && op != ">=") {
            if (!lhs.is_lvalue || lhs.type->is_const) {
                error("assignment target is not a modifiable lvalue");
            }
            std::string_view base = op.substr(0, op.size() - 1);
            if (as_type<Pointer_type>(lhs.type)) {
                if ((base != "+" && base != "-") ||
                    !rhs.type->is_integer() ||
                    !is_object_pointer(lhs.type)) {
                    error("invalid compound assignment to pointer");
                }
            }
            else if (!lhs.type->is_integer() ||
                     !rhs.type->is_integer()) {
                error("compound assignment requires integer operands");
            }
            info.type = lhs.type;
        }
        else if (op == ",") {
            info.type = rhs.type;
            info.is_lvalue = rhs.is_lvalue;
        }
        else if ((op == "+" || op == "-") &&
                 as_type<Pointer_type>(lhs.type) &&
                 rhs.type->is_integer()) {
            if (!is_object_pointer(lhs.type)) {
                error("pointer arithmetic requires complete object type");
            }
            info.type = lhs.type;
        }
        else if (op == "+" &&
                 lhs.type->is_integer() &&
                 as_type<Pointer_type>(rhs.type)) {
            if (!is_object_pointer(rhs.type)) {
                error("pointer arithmetic requires complete object type");
            }
            info.type = rhs.type;
        }
        else if (op == "-" &&
                 as_type<Pointer_type>(lhs.type) &&
                 as_type<Pointer_type>(rhs.type)) {
            if (!assignable(lhs.type, rhs.type) ||
                !is_object_pointer(lhs.type) ||
                !is_object_pointer(rhs.type)) {
                error("pointer subtraction requires compatible types");
            }
            info.type = integer_type;
        }
        else if (op == "&&" || op == "||") {
            if (!lhs.type->is_scalar() || !rhs.type->is_scalar()) {
                error("logical operator requires scalar operands");
            }
            info.type = integer_type;
        }
        else if (op == "==" || op == "!=" ||
                 op == "<" || op == "<=" ||
                 op == ">" || op == ">=") {
            bool integers =
                lhs.type->is_integer() && rhs.type->is_integer();
            bool pointers =
                as_type<Pointer_type>(lhs.type) &&
                as_type<Pointer_type>(rhs.type) &&
                assignable(lhs.type, rhs.type);
            bool null_pointer =
                as_type<Pointer_type>(lhs.type) &&
                is_null_pointer_constant(*node.rhs) ||
                as_type<Pointer_type>(rhs.type) &&
                is_null_pointer_constant(*node.lhs);
            if (!integers && !pointers && !null_pointer) {
                error("comparison requires compatible scalar operands");
            }
            info.type = integer_type;
        }
        else {
            if (!lhs.type->is_integer() || !rhs.type->is_integer()) {
                error("binary operator requires integer operands");
            }
            info.type = integer_type;
        }
        return info;
    }

    Expression_info analyze_expression_node(
        const parser::Call_expression &node) {
        auto callee = analyze_expression(*node.callee);
        Type_ptr function_type = callee.type;
        if (auto pointer = as_type<Pointer_type>(function_type)) {
            function_type = pointer->element;
        }
        auto function = as_type<Function_type>(function_type);
        if (!function) {
            error("called expression is not a function");
        }
        if (node.arguments.size() != function->parameters.size()) {
            error("wrong number of function arguments");
        }
        for (int i = 0; i < static_cast<int>(node.arguments.size()); i++) {
            auto argument = analyze_expression(*node.arguments[i]);
            if (!assignable(
                    function->parameters[i],
                    argument.type) &&
                !(as_type<Pointer_type>(function->parameters[i]) &&
                  is_null_pointer_constant(*node.arguments[i]))) {
                error("incompatible function argument");
            }
        }
        return Expression_info{function->return_type};
    }

    Expression_info analyze_expression_node(
        const parser::Subscript_expression &node) {
        auto object = analyze_expression(*node.object);
        auto index = analyze_expression(*node.index);
        auto object_type = as_type<Array_type>(object.type)
            ? object.type->decay()
            : object.type;
        auto pointer = as_type<Pointer_type>(object_type);
        if (!pointer ||
            !index.type->is_integer() ||
            !is_object_pointer(object_type)) {
            error("invalid array subscript");
        }
        return Expression_info{pointer->element, true};
    }

    Expression_info analyze_expression_node(
        const parser::Member_expression &node) {
        auto object = analyze_expression(*node.object);
        Type_ptr record_type = object.type;
        bool is_lvalue = object.is_lvalue;

        if (node.op.raw == "->") {
            auto pointer = as_type<Pointer_type>(record_type);
            if (!pointer) {
                error("'->' requires pointer to struct");
            }
            record_type = pointer->element;
            is_lvalue = true;
        }

        auto record = as_type<Record_type>(record_type);
        if (!record || !record->defined) {
            error("member access requires complete struct type");
        }
        int index = record->field_index(node.member.raw);
        if (index < 0) {
            error(
                "struct '" + record->record_name +
                "' has no member '" +
                std::string(node.member.raw) + "'");
        }
        return Expression_info{
            record->fields[index].type,
            is_lvalue
        };
    }

    Expression_info analyze_expression_node(
        const parser::Conditional_expression &node) {
        auto condition = analyze_expression(*node.condition);
        auto true_info = analyze_expression(*node.true_expression);
        auto false_info = analyze_expression(*node.false_expression);
        if (!condition.type->is_scalar()) {
            error("conditional expression requires scalar condition");
        }
        if (as_type<Pointer_type>(true_info.type) &&
            is_null_pointer_constant(*node.false_expression)) {
            return Expression_info{true_info.type};
        }
        if (as_type<Pointer_type>(false_info.type) &&
            is_null_pointer_constant(*node.true_expression)) {
            return Expression_info{false_info.type};
        }
        if (!assignable(true_info.type, false_info.type) &&
            !assignable(false_info.type, true_info.type)) {
            error("conditional branches have incompatible types");
        }
        return Expression_info{
            true_info.type,
            true_info.is_lvalue && false_info.is_lvalue
        };
    }

    Expression_info analyze_expression_node(
        const parser::Cast_expression &node) {
        analyze_expression(*node.operand);
        return Expression_info{type_name(*node.type)};
    }

    Expression_info analyze_expression_node(
        const parser::Type_query_expression &node) {
        type_query_value(node);
        return Expression_info{integer_type};
    }

    Expression_info analyze_expression_node(
        const parser::Expression &) {
        error("invalid expression node");
    }

    Expression_info analyze_expression_node(
        const parser::Error_expression &) {
        error("cannot analyze erroneous expression");
    }

    Symbol *declare_item(
        const parser::Declvariable &declaration,
        const parser::Init_declarator &item,
        bool global) {
        auto base = base_type(declaration.type, declaration.is_const);
        auto type = declarator_type(base, *item.declarator);
        Symbol::Kind kind = declaration.is_typedef
            ? Symbol::Kind::TYPEDEF_NAME
            : as_type<Function_type>(type)
                ? Symbol::Kind::FUNCTION
                : Symbol::Kind::VARIABLE;

        auto array = as_type<Array_type>(type);
        if (kind == Symbol::Kind::VARIABLE &&
            array &&
            !array->size &&
            item.initializer) {
            if (auto list =
                    dynamic_cast<const parser::Initializer_list *>(
                        item.initializer.get())) {
                array->size =
                    static_cast<long long>(list->elements.size());
            }
            else if (auto expression_initializer =
                         dynamic_cast<
                             const parser::Expression_initializer *>(
                             item.initializer.get())) {
                auto primary =
                    dynamic_cast<const parser::Primary_expression *>(
                        expression_initializer->expression.get());
                if (primary &&
                    primary->token.type ==
                        lexer::token_type::STRING_CONSTANT &&
                    is_character(array->element)) {
                    auto value =
                        literal::decode_string(primary->token.raw);
                    if (!value) error("invalid string literal");
                    array->size =
                        static_cast<long long>(value->size() + 1);
                }
            }
        }

        if (kind == Symbol::Kind::VARIABLE && !type->is_complete()) {
            error(
                "variable '" + std::string(item.declarator->name.raw) +
                "' has incomplete type '" + type->name() + "'");
        }

        auto symbol = make_symbol(
            kind,
            item.declarator->name.raw,
            type,
            item.declarator.get());

        if (kind == Symbol::Kind::TYPEDEF_NAME) {
            auto name = std::string(item.declarator->name.raw);
            if (scopes.back().types.contains(name) ||
                scopes.back().symbols.contains(name)) {
                error("redefinition of '" + name + "'");
            }
            scopes.back().types[name] = type;
        }
        else {
            symbol = insert_symbol(
                symbol,
                global && kind == Symbol::Kind::FUNCTION);
        }

        result.declarators[item.declarator.get()] = symbol;
        return symbol;
    }

    void analyze_initializer(
        const parser::Initializer &initializer,
        Type_ptr target) {
        if (auto expression_initializer =
                dynamic_cast<const parser::Expression_initializer *>(
                    &initializer)) {
            auto array = as_type<Array_type>(target);
            if (array && is_character(array->element)) {
                auto primary =
                    dynamic_cast<const parser::Primary_expression *>(
                        expression_initializer->expression.get());
                if (primary &&
                    primary->token.type ==
                        lexer::token_type::STRING_CONSTANT) {
                    auto value =
                        literal::decode_string(primary->token.raw);
                    if (!value) error("invalid string literal");
                    if (array->size &&
                        static_cast<long long>(value->size()) >
                            *array->size) {
                        error("initializer string is too long");
                    }
                    analyze_expression(
                        *expression_initializer->expression);
                    return;
                }
            }

            auto value = analyze_expression(
                *expression_initializer->expression);
            if (!assignable(target, value.type) &&
                !(as_type<Pointer_type>(target) &&
                  is_null_pointer_constant(
                      *expression_initializer->expression))) {
                error("incompatible initializer");
            }
            return;
        }

        auto list = dynamic_cast<const parser::Initializer_list *>(
            &initializer);
        if (!list) {
            error("invalid initializer");
        }
        auto array = as_type<Array_type>(target);
        auto record = as_type<Record_type>(target);
        if (!array && !record) {
            if (list->elements.size() > 1) {
                error("too many initializer elements");
            }
            if (!list->elements.empty()) {
                analyze_initializer(*list->elements[0], target);
            }
            return;
        }
        if (record) {
            if (!record->defined) {
                error("initializer requires complete struct type");
            }
            if (list->elements.size() > record->fields.size()) {
                error("too many initializer elements");
            }
            for (int i = 0;
                 i < static_cast<int>(list->elements.size());
                 i++) {
                analyze_initializer(
                    *list->elements[i],
                    record->fields[i].type);
            }
            return;
        }
        if (array->size &&
            list->elements.size() > *array->size) {
            error("too many initializer elements");
        }
        for (auto &element : list->elements) {
            analyze_initializer(*element, array->element);
        }
    }

    void analyze_declaration(
        const parser::Declvariable &declaration,
        bool global = false) {
        for (auto &item : declaration.declarators) {
            auto symbol = declare_item(declaration, *item, global);
            if (item->initializer) {
                if (symbol->kind == Symbol::Kind::TYPEDEF_NAME ||
                    symbol->kind == Symbol::Kind::FUNCTION) {
                    error("invalid initializer");
                }
                analyze_initializer(*item->initializer, symbol->type);
            }
        }
    }

    void analyze_struct_definition(
        const parser::Struct_definition &definition) {
        auto name = std::string(definition.name.raw);
        if (scopes.back().types.contains(name) ||
            scopes.back().symbols.contains(name)) {
            error("redefinition of '" + name + "'");
        }

        auto record = std::make_shared<Record_type>(name);
        scopes.back().types[name] = record;

        std::unordered_set<std::string> field_names;
        for (auto &declaration : definition.fields) {
            if (declaration->is_static ||
                declaration->is_typedef) {
                error("invalid struct field specifier");
            }
            auto base = base_type(
                declaration->type,
                declaration->is_const);
            for (auto &item : declaration->declarators) {
                if (item->initializer) {
                    error("struct field cannot have initializer");
                }
                auto type = declarator_type(
                    base,
                    *item->declarator);
                if (as_type<Function_type>(type)) {
                    error("struct field cannot have function type");
                }
                if (!type->is_complete()) {
                    error(
                        "struct field '" +
                        std::string(item->declarator->name.raw) +
                        "' has incomplete type");
                }
                auto field_name =
                    std::string(item->declarator->name.raw);
                if (!field_names.insert(field_name).second) {
                    error(
                        "duplicate struct member '" +
                        field_name + "'");
                }
                record->fields.push_back({
                    std::move(field_name),
                    std::move(type)
                });
            }
        }
        record->defined = true;
    }

    void analyze_statement(const parser::Statement &node);
    void analyze_statement_node(const parser::Block &node);
    void analyze_statement_node(
        const parser::Struct_definition &node);
    void analyze_statement_node(const parser::Declvariable &node);
    void analyze_statement_node(const parser::Expression_statement &node);
    void analyze_statement_node(const parser::Return_statement &node);
    void analyze_statement_node(const parser::If_statement &node);
    void analyze_statement_node(const parser::Switch_statement &node);
    void analyze_statement_node(const parser::Case_statement &node);
    void analyze_statement_node(const parser::Default_statement &node);
    void analyze_statement_node(const parser::While_statement &node);
    void analyze_statement_node(const parser::Do_while_statement &node);
    void analyze_statement_node(const parser::For_statement &node);
    void analyze_statement_node(const parser::Break_statement &node);
    void analyze_statement_node(const parser::Continue_statement &node);
    void analyze_statement_node(const parser::Statement &node);

    void predeclare_global(const parser::Node &node) {
        if (auto definition =
                dynamic_cast<const parser::Struct_definition *>(&node)) {
            analyze_struct_definition(*definition);
            return;
        }
        if (auto declaration =
                dynamic_cast<const parser::Declvariable *>(&node)) {
            analyze_declaration(*declaration, true);
            return;
        }

        auto function =
            dynamic_cast<const parser::Function_definition *>(&node);
        if (!function) return;
        auto base = base_type(
            function->specifiers.type,
            function->specifiers.is_const);
        auto type = declarator_type(base, *function->declarator);
        auto symbol = make_symbol(
            Symbol::Kind::FUNCTION,
            function->declarator->name.raw,
            type,
            function->declarator.get());

        auto existing = find_symbol(symbol->name);
        if (existing) {
            if (existing->kind != Symbol::Kind::FUNCTION ||
                !same_type(existing->type, symbol->type) ||
                existing->is_defined) {
                error("conflicting definition of '" + symbol->name + "'");
            }
            existing->is_defined = true;
            result.declarators[function->declarator.get()] = existing;
        }
        else {
            symbol->is_defined = true;
            insert_symbol(symbol);
            result.declarators[function->declarator.get()] = symbol;
        }
    }

    void analyze_function(const parser::Function_definition &function) {
        auto symbol = result.symbol(*function.declarator);
        auto function_type = as_type<Function_type>(symbol->type);
        if (!function_type) error("definition does not have function type");
        current_return_type = function_type->return_type;

        push_scope();
        int parameter_index = 0;
        bool void_parameter_list =
            function.declarator->parameters.size() == 1 &&
            !function.declarator->parameters[0]->declarator &&
            is_void(base_type(
                function.declarator->parameters[0]->specifiers.type,
                function.declarator->parameters[0]->specifiers.is_const));
        for (auto &parameter : function.declarator->parameters) {
            if (!parameter->declarator) {
                if (void_parameter_list) continue;
                error("function definition parameter requires a name");
            }
            auto parameter_type =
                function_type->parameters[parameter_index++];
            auto parameter_symbol = make_symbol(
                Symbol::Kind::PARAMETER,
                parameter->declarator->name.raw,
                parameter_type,
                parameter->declarator.get());
            insert_symbol(parameter_symbol);
            result.declarators[parameter->declarator.get()] =
                parameter_symbol;
        }
        analyze_statement(*function.body);
        pop_scope();
        current_return_type.reset();
    }

public:
    Semantic_result analyze(const parser::Program &program) {
        result = Semantic_result();
        scopes.clear();
        push_scope();

        void_type = std::make_shared<Primitive_type>(
            "void",
            Primitive_type::Category::VOID_TYPE,
            0,
            false);
        character_type = std::make_shared<Primitive_type>(
            "char",
            Primitive_type::Category::CHARACTER,
            8,
            true);
        integer_type = std::make_shared<Primitive_type>(
            "int",
            Primitive_type::Category::INTEGER,
            32,
            true);
        result.void_type = void_type;
        result.character_type = character_type;
        result.integer_type = integer_type;
        scopes.back().types[void_type->name()] = void_type;
        scopes.back().types[character_type->name()] = character_type;
        scopes.back().types[integer_type->name()] = integer_type;

        for (auto &external : program.external_declarations) {
            predeclare_global(*external);
            if (auto function =
                    dynamic_cast<const parser::Function_definition *>(
                        external.get())) {
                analyze_function(*function);
            }
        }

        pop_scope();
        return std::move(result);
    }
};

#include "semantic/statement_analyzer.inl"

}  // namespace c9ay::semantic
