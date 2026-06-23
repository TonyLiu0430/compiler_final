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
                  current_return_type,
                  semantic_result.info(*node.expression).type)
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
    auto end_block = llvm::BasicBlock::Create(
        context,
        "switch.end",
        current_function);
    auto default_block = end_block;
    auto condition = expression(*node.condition);
    auto condition_type =
        semantic_result.info(*node.condition).type;
    auto switch_instruction = builder.CreateSwitch(
        condition,
        end_block);

    std::vector<llvm::BasicBlock *> section_blocks;
    for (auto &section : node.body->sections) {
        auto section_block = llvm::BasicBlock::Create(
            context,
            "switch.case",
            current_function);
        section_blocks.push_back(section_block);

        for (auto &label : section->labels) {
            if (label->is_default()) {
                default_block = section_block;
            }
            else {
                auto value = constant_value(*label->value);
                if (!value) {
                    unsupported("non-constant switch case");
                }
                switch_instruction->addCase(
                    llvm::cast<llvm::ConstantInt>(
                        integer(condition_type, value->value)),
                    section_block);
            }
        }
    }

    switch_instruction->setDefaultDest(default_block);
    push_scope();
    break_targets.push_back(end_block);
    for (int i = 0;
         i < static_cast<int>(node.body->sections.size());
         i++) {
        builder.SetInsertPoint(section_blocks[i]);
        for (auto &statement :
             node.body->sections[i]->statements) {
            this->statement(*statement);
        }

        if (!is_terminated(builder.GetInsertBlock())) {
            builder.CreateBr(
                i + 1 < static_cast<int>(section_blocks.size())
                    ? section_blocks[i + 1]
                    : end_block);
        }
    }
    break_targets.pop_back();
    pop_scope();
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
    const parser::Statement &) {
    unsupported("statement node");
}
