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
    const parser::Expression_statement &node) {
    if (node.expression) {
        analyze_expression(*node.expression);
    }
}

inline void Semantic_analyzer::analyze_statement_node(
    const parser::Return_statement &node) {
    if (current_return_type->kind == Type::Kind::VOID_TYPE) {
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
        !(current_return_type->kind == Type::Kind::POINTER_TYPE &&
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
    analyze_statement(*node.body);
    switches.pop_back();
}

inline void Semantic_analyzer::analyze_statement_node(
    const parser::Case_statement &node) {
    if (switches.empty()) error("case outside switch");

    auto value = Constant_evaluator::evaluate(*node.value);
    if (!value) {
        error("case value is not an integer constant");
    }
    if (!switches.back().cases.insert(value->value).second) {
        error("duplicate case value");
    }
    analyze_expression(*node.value);
    analyze_statement(*node.statement);
}

inline void Semantic_analyzer::analyze_statement_node(
    const parser::Default_statement &node) {
    if (switches.empty()) error("default outside switch");
    if (switches.back().has_default) {
        error("multiple default labels in one switch");
    }
    switches.back().has_default = true;
    analyze_statement(*node.statement);
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
