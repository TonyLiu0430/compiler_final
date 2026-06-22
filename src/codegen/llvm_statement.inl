inline void LLVM_codegen::statement(
    const parser::Statement &node) {
    if (is_terminated(builder.GetInsertBlock())) return;

    parser::reflect::dispatch<void>(
        node,
        [this](const auto &statement) {
            statement_node(statement);
        });
}

inline void LLVM_codegen::statement_node(
    const parser::Block &node) {
    push_scope();
    for (auto &child : node.statements) {
        statement(*child);
    }
    pop_scope();
}

inline void LLVM_codegen::statement_node(
    const parser::Declvariable &node) {
    declaration(node);
}

inline void LLVM_codegen::statement_node(
    const parser::Struct_definition &) {}

inline void LLVM_codegen::statement_node(
    const parser::Expression_statement &node) {
    if (node.expression) {
        expression(*node.expression);
    }
}

inline void LLVM_codegen::statement_node(
    const parser::Return_statement &node) {
    if (current_function->getReturnType()->isVoidTy()) {
        builder.CreateRetVoid();
        return;
    }

    builder.CreateRet(
        node.expression
            ? convert(
                  expression(*node.expression),
                  current_return_type)
            : convert(integer(0), current_return_type));
}

inline void LLVM_codegen::statement_node(
    const parser::If_statement &node) {
    auto condition = as_condition(expression(*node.condition));
    auto then_block = llvm::BasicBlock::Create(
        context,
        "if.then",
        current_function);
    auto else_block = node.else_statement
        ? llvm::BasicBlock::Create(
              context,
              "if.else",
              current_function)
        : nullptr;
    auto merge_block = llvm::BasicBlock::Create(
        context,
        "if.end",
        current_function);

    builder.CreateCondBr(
        condition,
        then_block,
        else_block ? else_block : merge_block);

    builder.SetInsertPoint(then_block);
    statement(*node.then_statement);
    if (!is_terminated(builder.GetInsertBlock())) {
        builder.CreateBr(merge_block);
    }

    if (else_block) {
        builder.SetInsertPoint(else_block);
        statement(*node.else_statement);
        if (!is_terminated(builder.GetInsertBlock())) {
            builder.CreateBr(merge_block);
        }
    }

    builder.SetInsertPoint(merge_block);
}

inline void LLVM_codegen::statement_node(
    const parser::Switch_statement &node) {
    auto body = dynamic_cast<const parser::Block *>(node.body.get());
    if (!body) unsupported("switch body without block");

    auto end_block = llvm::BasicBlock::Create(
        context,
        "switch.end",
        current_function);
    auto default_block = end_block;
    auto condition = as_integer(expression(*node.condition));
    auto switch_instruction = builder.CreateSwitch(
        condition,
        end_block);

    struct Segment {
        llvm::BasicBlock *block;
        const parser::Statement *first;
        int begin_index;
        int end_index;
    };
    std::vector<Segment> segments;

    int index = 0;
    while (index < static_cast<int>(body->statements.size()) &&
           !dynamic_cast<const parser::Case_statement *>(
               body->statements[index].get()) &&
           !dynamic_cast<const parser::Default_statement *>(
               body->statements[index].get())) {
        index++;
    }

    while (index < static_cast<int>(body->statements.size())) {
        const parser::Statement *label =
            body->statements[index].get();
        auto block = llvm::BasicBlock::Create(
            context,
            "switch.case",
            current_function);
        const parser::Statement *payload = label;

        while (1) {
            if (auto case_statement =
                    dynamic_cast<const parser::Case_statement *>(
                        payload)) {
                auto value = constant_value(
                    *case_statement->value);
                if (!value) {
                    unsupported("non-constant switch case");
                }
                switch_instruction->addCase(
                    llvm::cast<llvm::ConstantInt>(
                        integer(value->value)),
                    block);
                payload = case_statement->statement.get();
                continue;
            }
            if (auto default_statement =
                    dynamic_cast<const parser::Default_statement *>(
                        payload)) {
                default_block = block;
                payload = default_statement->statement.get();
                continue;
            }
            break;
        }

        int next = index + 1;
        while (next < static_cast<int>(body->statements.size()) &&
               !dynamic_cast<const parser::Case_statement *>(
                   body->statements[next].get()) &&
               !dynamic_cast<const parser::Default_statement *>(
                   body->statements[next].get())) {
            next++;
        }
        segments.push_back({
            block,
            payload,
            index + 1,
            next
        });
        index = next;
    }

    switch_instruction->setDefaultDest(default_block);
    break_targets.push_back(end_block);
    for (int i = 0; i < static_cast<int>(segments.size()); i++) {
        builder.SetInsertPoint(segments[i].block);
        statement(*segments[i].first);
        for (int child = segments[i].begin_index;
             child < segments[i].end_index;
             child++) {
            statement(*body->statements[child]);
        }

        if (!is_terminated(builder.GetInsertBlock())) {
            builder.CreateBr(
                i + 1 < static_cast<int>(segments.size())
                    ? segments[i + 1].block
                    : end_block);
        }
    }
    break_targets.pop_back();
    builder.SetInsertPoint(end_block);
}

inline void LLVM_codegen::statement_node(
    const parser::While_statement &node) {
    auto condition_block = llvm::BasicBlock::Create(
        context,
        "while.condition",
        current_function);
    auto body_block = llvm::BasicBlock::Create(
        context,
        "while.body",
        current_function);
    auto end_block = llvm::BasicBlock::Create(
        context,
        "while.end",
        current_function);

    builder.CreateBr(condition_block);
    builder.SetInsertPoint(condition_block);
    builder.CreateCondBr(
        as_condition(expression(*node.condition)),
        body_block,
        end_block);

    loops.push_back({end_block, condition_block});
    break_targets.push_back(end_block);
    builder.SetInsertPoint(body_block);
    statement(*node.body);
    if (!is_terminated(builder.GetInsertBlock())) {
        builder.CreateBr(condition_block);
    }
    loops.pop_back();
    break_targets.pop_back();
    builder.SetInsertPoint(end_block);
}

inline void LLVM_codegen::statement_node(
    const parser::Do_while_statement &node) {
    auto body_block = llvm::BasicBlock::Create(
        context,
        "do.body",
        current_function);
    auto condition_block = llvm::BasicBlock::Create(
        context,
        "do.condition",
        current_function);
    auto end_block = llvm::BasicBlock::Create(
        context,
        "do.end",
        current_function);

    builder.CreateBr(body_block);
    loops.push_back({end_block, condition_block});
    break_targets.push_back(end_block);
    builder.SetInsertPoint(body_block);
    statement(*node.body);
    if (!is_terminated(builder.GetInsertBlock())) {
        builder.CreateBr(condition_block);
    }
    loops.pop_back();
    break_targets.pop_back();

    builder.SetInsertPoint(condition_block);
    builder.CreateCondBr(
        as_condition(expression(*node.condition)),
        body_block,
        end_block);
    builder.SetInsertPoint(end_block);
}

inline void LLVM_codegen::statement_node(
    const parser::For_statement &node) {
    push_scope();
    if (node.initializer) {
        statement(*node.initializer);
    }

    auto condition_block = llvm::BasicBlock::Create(
        context,
        "for.condition",
        current_function);
    auto body_block = llvm::BasicBlock::Create(
        context,
        "for.body",
        current_function);
    auto iteration_block = llvm::BasicBlock::Create(
        context,
        "for.iteration",
        current_function);
    auto end_block = llvm::BasicBlock::Create(
        context,
        "for.end",
        current_function);

    builder.CreateBr(condition_block);
    builder.SetInsertPoint(condition_block);
    if (node.condition) {
        builder.CreateCondBr(
            as_condition(expression(*node.condition)),
            body_block,
            end_block);
    }
    else {
        builder.CreateBr(body_block);
    }

    loops.push_back({end_block, iteration_block});
    break_targets.push_back(end_block);
    builder.SetInsertPoint(body_block);
    statement(*node.body);
    if (!is_terminated(builder.GetInsertBlock())) {
        builder.CreateBr(iteration_block);
    }
    loops.pop_back();
    break_targets.pop_back();

    builder.SetInsertPoint(iteration_block);
    if (node.iteration) {
        expression(*node.iteration);
    }
    builder.CreateBr(condition_block);

    builder.SetInsertPoint(end_block);
    pop_scope();
}

inline void LLVM_codegen::statement_node(
    const parser::Break_statement &) {
    if (break_targets.empty()) {
        unsupported("break outside loop or switch");
    }
    builder.CreateBr(break_targets.back());
}

inline void LLVM_codegen::statement_node(
    const parser::Continue_statement &) {
    if (loops.empty()) unsupported("continue outside loop");
    builder.CreateBr(loops.back().continue_target);
}

inline void LLVM_codegen::statement_node(
    const parser::Case_statement &) {
    unsupported("case label outside switch block");
}

inline void LLVM_codegen::statement_node(
    const parser::Default_statement &) {
    unsupported("default label outside switch block");
}

inline void LLVM_codegen::statement_node(
    const parser::Statement &) {
    unsupported("statement node");
}
