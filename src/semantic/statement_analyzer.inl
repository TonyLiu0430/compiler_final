inline void Semantic_analyzer::analyze_statement(
    const parser::Statement &node) {
    parser::reflect::dispatch<void>(
        node,
        [this](const auto &statement) {
            analyze_statement_node(statement);
        });
}

inline void Semantic_analyzer::analyze_statement_node(
    const parser::Block &node) {
    push_scope();
    for (auto &child : node.statements) {
        analyze_statement(*child);
    }
    pop_scope();
}

inline void Semantic_analyzer::analyze_statement_node(
    const parser::Declvariable &node) {
    analyze_declaration(node);
}

inline void Semantic_analyzer::analyze_statement_node(
    const parser::Struct_definition &node) {
    analyze_struct_definition(node);
}

inline void Semantic_analyzer::analyze_statement_node(
    const parser::Expression_statement &node) {
    if (node.expression) {
        analyze_expression(*node.expression);
    }
}

inline void Semantic_analyzer::analyze_statement_node(
    const parser::Return_statement &node) {
    if (is_void(current_return_type)) {
        if (node.expression) {
            error("void function should not return a value");
        }
        return;
    }

    if (!node.expression) {
        error("non-void function must return a value");
    }
    auto value = analyze_expression(*node.expression);
    if (!assignable(current_return_type, value.type) &&
        !(as_type<Pointer_type>(current_return_type) &&
          is_null_pointer_constant(*node.expression))) {
        error("return type mismatch");
    }
}

inline void Semantic_analyzer::analyze_statement_node(
    const parser::If_statement &node) {
    if (!analyze_expression(*node.condition).type->is_scalar()) {
        error("if condition must be scalar");
    }
    analyze_statement(*node.then_statement);
    if (node.else_statement) {
        analyze_statement(*node.else_statement);
    }
}

inline void Semantic_analyzer::analyze_statement_node(
    const parser::Switch_statement &node) {
    if (!analyze_expression(*node.condition).type->is_integer()) {
        error("switch condition must be integer");
    }

    switches.emplace_back();
    push_scope();
    for (auto &section : node.body->sections) {
        for (auto &label : section->labels) {
            if (label->is_default()) {
                if (switches.back().has_default) {
                    error("multiple default labels in one switch");
                }
                switches.back().has_default = true;
                continue;
            }

            auto value = evaluate_constant(*label->value);
            if (!value) {
                error("case value is not an integer constant");
            }
            if (!switches.back().cases.insert(value->value).second) {
                error("duplicate case value");
            }
            analyze_expression(*label->value);
        }
        for (auto &statement : section->statements) {
            analyze_statement(*statement);
        }
    }
    pop_scope();
    switches.pop_back();
}

inline void Semantic_analyzer::analyze_statement_node(
    const parser::While_statement &node) {
    if (!analyze_expression(*node.condition).type->is_scalar()) {
        error("while condition must be scalar");
    }
    loop_depth++;
    analyze_statement(*node.body);
    loop_depth--;
}

inline void Semantic_analyzer::analyze_statement_node(
    const parser::Do_while_statement &node) {
    loop_depth++;
    analyze_statement(*node.body);
    loop_depth--;
    if (!analyze_expression(*node.condition).type->is_scalar()) {
        error("do while condition must be scalar");
    }
}

inline void Semantic_analyzer::analyze_statement_node(
    const parser::For_statement &node) {
    push_scope();
    if (node.initializer) {
        analyze_statement(*node.initializer);
    }
    if (node.condition &&
        !analyze_expression(*node.condition).type->is_scalar()) {
        error("for condition must be scalar");
    }
    if (node.iteration) {
        analyze_expression(*node.iteration);
    }
    loop_depth++;
    analyze_statement(*node.body);
    loop_depth--;
    pop_scope();
}

inline void Semantic_analyzer::analyze_statement_node(
    const parser::Break_statement &) {
    if (loop_depth == 0 && switches.empty()) {
        error("break outside loop or switch");
    }
}

inline void Semantic_analyzer::analyze_statement_node(
    const parser::Continue_statement &) {
    if (loop_depth == 0) {
        error("continue outside loop");
    }
}

inline void Semantic_analyzer::analyze_statement_node(
    const parser::Statement &) {
    error("invalid statement node");
}
