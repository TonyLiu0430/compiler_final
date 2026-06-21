#pragma once

#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "parser/parser.h"

namespace c9ay::semantic {

class Compile_error : public std::runtime_error {
public:
    Compile_error(const std::string &message)
        : std::runtime_error(message) {}
};

class Semantic_analyzer {
    std::vector<std::unordered_set<std::string>> type_scopes;

    [[noreturn]] static void error(std::string_view message) {
        throw Compile_error(
            "compile error: " + std::string(message));
    }

    void push_scope() {
        type_scopes.emplace_back();
    }

    void pop_scope() {
        type_scopes.pop_back();
    }

    bool is_known_type(std::string_view name) {
        if (name == "int" || name == "void") return true;

        for (int i = static_cast<int>(type_scopes.size()) - 1; i >= 0; i--) {
            if (type_scopes[i].contains(std::string(name))) {
                return true;
            }
        }
        return false;
    }

    void require_type(lexer::Token type) {
        if (!is_known_type(type.raw)) {
            error("unknown type '" + std::string(type.raw) + "'");
        }
    }

    void add_typedef(std::string_view name) {
        type_scopes.back().insert(std::string(name));
    }

    void expression(const parser::Expression &node) {
        if (auto prefix =
                dynamic_cast<const parser::Prefix_expression *>(&node)) {
            expression(*prefix->operand);
            return;
        }
        if (auto binary =
                dynamic_cast<const parser::Binary_expression *>(&node)) {
            expression(*binary->lhs);
            expression(*binary->rhs);
            return;
        }
        if (auto postfix =
                dynamic_cast<const parser::Postfix_expression *>(&node)) {
            expression(*postfix->operand);
            return;
        }
        if (auto call =
                dynamic_cast<const parser::Call_expression *>(&node)) {
            expression(*call->callee);
            for (auto &argument : call->arguments) {
                expression(*argument);
            }
            return;
        }
        if (auto subscript =
                dynamic_cast<const parser::Subscript_expression *>(&node)) {
            expression(*subscript->object);
            expression(*subscript->index);
            return;
        }
        if (auto member =
                dynamic_cast<const parser::Member_expression *>(&node)) {
            expression(*member->object);
            return;
        }
        if (auto conditional =
                dynamic_cast<const parser::Conditional_expression *>(&node)) {
            expression(*conditional->condition);
            expression(*conditional->true_expression);
            expression(*conditional->false_expression);
            return;
        }
        if (auto cast =
                dynamic_cast<const parser::Cast_expression *>(&node)) {
            require_type(cast->type->type);
            expression(*cast->operand);
        }
    }

    void declarator(const parser::Declarator &node) {
        for (auto &dimension : node.array_dimensions) {
            if (dimension) expression(*dimension);
        }

        for (auto &parameter : node.parameters) {
            require_type(parameter->specifiers.type);
            if (parameter->declarator) {
                declarator(*parameter->declarator);
            }
        }
    }

    void initializer(const parser::Initializer &node) {
        if (auto expression_initializer =
                dynamic_cast<const parser::Expression_initializer *>(&node)) {
            expression(*expression_initializer->expression);
            return;
        }
        if (auto list =
                dynamic_cast<const parser::Initializer_list *>(&node)) {
            for (auto &element : list->elements) {
                initializer(*element);
            }
        }
    }

    void declaration(const parser::Declvariable &node) {
        require_type(node.type);

        if (node.type.raw == "void" && !node.is_typedef) {
            for (auto &item : node.declarators) {
                if (!item->declarator->is_function) {
                    error(
                        "variable '" +
                        std::string(item->declarator->name.raw) +
                        "' has incomplete type void");
                }
            }
        }

        for (auto &item : node.declarators) {
            declarator(*item->declarator);
            if (item->initializer) {
                initializer(*item->initializer);
            }
        }

        if (node.is_typedef) {
            for (auto &item : node.declarators) {
                add_typedef(item->declarator->name.raw);
            }
        }
    }

    void statement(const parser::Statement &node) {
        if (auto block = dynamic_cast<const parser::Block *>(&node)) {
            push_scope();
            for (auto &child : block->statements) {
                statement(*child);
            }
            pop_scope();
            return;
        }
        if (auto declaration_node =
                dynamic_cast<const parser::Declvariable *>(&node)) {
            declaration(*declaration_node);
            return;
        }
        if (auto expression_node =
                dynamic_cast<const parser::Expression_statement *>(&node)) {
            if (expression_node->expression) {
                expression(*expression_node->expression);
            }
            return;
        }
        if (auto return_node =
                dynamic_cast<const parser::Return_statement *>(&node)) {
            if (return_node->expression) expression(*return_node->expression);
            return;
        }
        if (auto if_node =
                dynamic_cast<const parser::If_statement *>(&node)) {
            expression(*if_node->condition);
            statement(*if_node->then_statement);
            if (if_node->else_statement) {
                statement(*if_node->else_statement);
            }
            return;
        }
        if (auto switch_node =
                dynamic_cast<const parser::Switch_statement *>(&node)) {
            expression(*switch_node->condition);
            statement(*switch_node->body);
            return;
        }
        if (auto case_node =
                dynamic_cast<const parser::Case_statement *>(&node)) {
            expression(*case_node->value);
            statement(*case_node->statement);
            return;
        }
        if (auto default_node =
                dynamic_cast<const parser::Default_statement *>(&node)) {
            statement(*default_node->statement);
            return;
        }
        if (auto while_node =
                dynamic_cast<const parser::While_statement *>(&node)) {
            expression(*while_node->condition);
            statement(*while_node->body);
            return;
        }
        if (auto do_node =
                dynamic_cast<const parser::Do_while_statement *>(&node)) {
            statement(*do_node->body);
            expression(*do_node->condition);
            return;
        }
        if (auto for_node =
                dynamic_cast<const parser::For_statement *>(&node)) {
            push_scope();
            if (for_node->initializer) {
                statement(*for_node->initializer);
            }
            if (for_node->condition) expression(*for_node->condition);
            if (for_node->iteration) expression(*for_node->iteration);
            statement(*for_node->body);
            pop_scope();
        }
    }

    void function(const parser::Function_definition &node) {
        require_type(node.specifiers.type);
        declarator(*node.declarator);

        push_scope();
        statement(*node.body);
        pop_scope();
    }

public:
    void analyze(const parser::Program &program) {
        type_scopes.clear();
        push_scope();

        for (auto &external : program.external_declarations) {
            if (auto declaration_node =
                    dynamic_cast<const parser::Declvariable *>(
                        external.get())) {
                declaration(*declaration_node);
            }
            else if (auto function_node =
                         dynamic_cast<const parser::Function_definition *>(
                             external.get())) {
                function(*function_node);
            }
        }

        pop_scope();
    }
};

}  // namespace c9ay::semantic
