inline llvm::Value *LLVM_codegen::expression(
    const parser::Expression &node) {
    return parser::reflect::dispatch<llvm::Value *>(
        node,
        [this](const auto &expression) {
            return expression_node(expression);
        });
}

inline llvm::Value *LLVM_codegen::expression_node(
    const parser::Primary_expression &node) {
    if (node.token.type == lexer::token_type::NUMBER ||
        node.token.type == lexer::token_type::CHAR_CONSTANT) {
        auto value = semantic::Constant_evaluator::evaluate(node);
        if (!value) unsupported("numeric literal");
        return integer(value->value);
    }

    if (node.token.type == lexer::token_type::STRING_CONSTANT) {
        return string_pointer(node.token.raw);
    }

    if (node.token.type != lexer::token_type::IDENTIFIER) {
        unsupported("primary expression");
    }

    auto address = find_address(node.token.raw);
    if (!address) {
        if (auto function = module->getFunction(
                llvm::StringRef(
                    node.token.raw.data(),
                    node.token.raw.size()))) {
            return function;
        }
        throw std::runtime_error(
            "unknown identifier: " +
            std::string(node.token.raw));
    }

    llvm::Type *type = nullptr;
    if (auto alloca = llvm::dyn_cast<llvm::AllocaInst>(address)) {
        type = alloca->getAllocatedType();
    }
    else if (auto global =
                 llvm::dyn_cast<llvm::GlobalVariable>(address)) {
        type = global->getValueType();
    }
    if (!type) unsupported("identifier load");

    auto semantic_type = semantic_result.info(node).type;
    if (semantic_type->kind == semantic::Type::Kind::ARRAY_TYPE) {
        return builder.CreateInBoundsGEP(
            type,
            address,
            {integer(0), integer(0)},
            "array.decay");
    }
    return builder.CreateLoad(
        type,
        address,
        llvm::StringRef(
            node.token.raw.data(),
            node.token.raw.size()));
}

inline llvm::Value *LLVM_codegen::expression_node(
    const parser::Prefix_expression &node) {
    if (node.op.raw == "++" || node.op.raw == "--") {
        auto address = lvalue(*node.operand);
        auto old = expression(*node.operand);
        auto type = semantic_result.info(*node.operand).type;
        llvm::Value *value = nullptr;
        if (type->kind == semantic::Type::Kind::POINTER_TYPE) {
            auto index = node.op.raw == "++"
                ? integer(1)
                : integer(-1);
            value = builder.CreateGEP(
                type_of(type->element),
                old,
                index,
                "pointer.increment");
        }
        else {
            value = node.op.raw == "++"
                ? builder.CreateAdd(
                      as_i32(old),
                      integer(1),
                      "increment")
                : builder.CreateSub(
                      as_i32(old),
                      integer(1),
                      "decrement");
            value = convert(value, type);
        }
        builder.CreateStore(value, address);
        return value;
    }

    if (node.op.raw == "&") {
        return lvalue(*node.operand);
    }
    if (node.op.raw == "*") {
        auto address = lvalue(node);
        auto type = semantic_result.info(node).type;
        if (type->kind == semantic::Type::Kind::ARRAY_TYPE) {
            return builder.CreateInBoundsGEP(
                type_of(type),
                address,
                {integer(0), integer(0)},
                "array.decay");
        }
        return builder.CreateLoad(
            type_of(type),
            address,
            "dereference");
    }

    auto operand = as_i32(expression(*node.operand));
    if (node.op.raw == "+") return operand;
    if (node.op.raw == "-") {
        return builder.CreateNeg(operand, "negative");
    }
    if (node.op.raw == "!") {
        return as_i32(builder.CreateNot(
            as_condition(operand),
            "logical_not"));
    }
    if (node.op.raw == "~") {
        return builder.CreateNot(operand, "bit_not");
    }
    unsupported("prefix operator");
}

inline llvm::Value *LLVM_codegen::expression_node(
    const parser::Postfix_expression &node) {
    auto address = lvalue(*node.operand);
    auto old = expression(*node.operand);
    auto type = semantic_result.info(*node.operand).type;
    llvm::Value *value = nullptr;
    if (type->kind == semantic::Type::Kind::POINTER_TYPE) {
        auto index = node.op.raw == "++"
            ? integer(1)
            : integer(-1);
        value = builder.CreateGEP(
            type_of(type->element),
            old,
            index,
            "pointer.increment");
    }
    else {
        value = node.op.raw == "++"
            ? builder.CreateAdd(
                  as_i32(old),
                  integer(1),
                  "increment")
            : builder.CreateSub(
                  as_i32(old),
                  integer(1),
                  "decrement");
        value = convert(value, type);
    }
    builder.CreateStore(value, address);
    return old;
}

inline llvm::Value *LLVM_codegen::expression_node(
    const parser::Binary_expression &node) {
    std::string_view op = node.op.raw;
    if (op == "=" ||
        op == "+=" || op == "-=" ||
        op == "*=" || op == "/=" || op == "%=" ||
        op == "<<=" || op == ">>=" ||
        op == "&=" || op == "^=" || op == "|=") {
        auto address = lvalue(*node.lhs);
        auto lhs_type = semantic_result.info(*node.lhs).type;
        auto rhs = expression(*node.rhs);
        if (op == "=") {
            rhs = convert(rhs, lhs_type);
        }
        else {
            auto lhs = expression(*node.lhs);
            std::string_view base = op.substr(0, op.size() - 1);
            if ((base == "+" || base == "-") &&
                lhs_type->kind ==
                    semantic::Type::Kind::POINTER_TYPE) {
                auto index = as_i32(rhs);
                if (base == "-") {
                    index = builder.CreateNeg(index);
                }
                rhs = builder.CreateGEP(
                    type_of(lhs_type->element),
                    lhs,
                    index);
            }
            else {
                lhs = as_i32(lhs);
                rhs = as_i32(rhs);
                if (base == "+") rhs = builder.CreateAdd(lhs, rhs);
                else if (base == "-") rhs = builder.CreateSub(lhs, rhs);
                else if (base == "*") rhs = builder.CreateMul(lhs, rhs);
                else if (base == "/") rhs = builder.CreateSDiv(lhs, rhs);
                else if (base == "%") rhs = builder.CreateSRem(lhs, rhs);
                else if (base == "<<") rhs = builder.CreateShl(lhs, rhs);
                else if (base == ">>") rhs = builder.CreateAShr(lhs, rhs);
                else if (base == "&") rhs = builder.CreateAnd(lhs, rhs);
                else if (base == "^") rhs = builder.CreateXor(lhs, rhs);
                else if (base == "|") rhs = builder.CreateOr(lhs, rhs);
            }
        }
        rhs = convert(rhs, lhs_type);
        builder.CreateStore(rhs, address);
        return rhs;
    }

    if (op == ",") {
        expression(*node.lhs);
        return expression(*node.rhs);
    }

    if (op == "&&" || op == "||") {
        auto function = builder.GetInsertBlock()->getParent();
        auto lhs = as_condition(expression(*node.lhs));
        auto lhs_block = builder.GetInsertBlock();
        auto rhs_block = llvm::BasicBlock::Create(
            context,
            "logical.rhs",
            function);
        auto merge_block = llvm::BasicBlock::Create(
            context,
            "logical.end",
            function);

        if (op == "&&") {
            builder.CreateCondBr(lhs, rhs_block, merge_block);
        }
        else {
            builder.CreateCondBr(lhs, merge_block, rhs_block);
        }

        builder.SetInsertPoint(rhs_block);
        auto rhs = as_condition(expression(*node.rhs));
        rhs_block = builder.GetInsertBlock();
        builder.CreateBr(merge_block);

        builder.SetInsertPoint(merge_block);
        auto phi = builder.CreatePHI(
            llvm::Type::getInt1Ty(context),
            2,
            "logical");
        phi->addIncoming(
            llvm::ConstantInt::getBool(context, op == "||"),
            lhs_block);
        phi->addIncoming(rhs, rhs_block);
        return as_i32(phi);
    }

    auto lhs_type = semantic_result.info(*node.lhs).type;
    auto rhs_type = semantic_result.info(*node.rhs).type;
    if ((op == "+" || op == "-") &&
        lhs_type->kind == semantic::Type::Kind::POINTER_TYPE &&
        rhs_type->is_integer()) {
        auto pointer = expression(*node.lhs);
        auto index = as_i32(expression(*node.rhs));
        if (op == "-") {
            index = builder.CreateNeg(index, "negative.index");
        }
        return builder.CreateGEP(
            type_of(lhs_type->element),
            pointer,
            index,
            "pointer.offset");
    }

    if (op == "+" &&
        lhs_type->is_integer() &&
        rhs_type->kind == semantic::Type::Kind::POINTER_TYPE) {
        return builder.CreateGEP(
            type_of(rhs_type->element),
            expression(*node.rhs),
            as_i32(expression(*node.lhs)),
            "pointer.offset");
    }

    if (op == "-" &&
        lhs_type->kind == semantic::Type::Kind::POINTER_TYPE &&
        rhs_type->kind == semantic::Type::Kind::POINTER_TYPE) {
        auto integer_type = llvm::Type::getInt64Ty(context);
        auto lhs = builder.CreatePtrToInt(
            expression(*node.lhs),
            integer_type);
        auto rhs = builder.CreatePtrToInt(
            expression(*node.rhs),
            integer_type);
        auto bytes = builder.CreateSub(lhs, rhs, "pointer.bytes");
        auto element_size = llvm::ConstantExpr::getSizeOf(
            type_of(lhs_type->element));
        auto distance = builder.CreateSDiv(
            bytes,
            element_size,
            "pointer.distance");
        return builder.CreateTrunc(
            distance,
            llvm::Type::getInt32Ty(context));
    }

    if ((op == "==" || op == "!=" ||
         op == "<" || op == "<=" ||
         op == ">" || op == ">=") &&
        (lhs_type->kind == semantic::Type::Kind::POINTER_TYPE ||
         rhs_type->kind == semantic::Type::Kind::POINTER_TYPE)) {
        auto lhs = expression(*node.lhs);
        auto rhs = expression(*node.rhs);
        if (lhs_type->kind == semantic::Type::Kind::POINTER_TYPE &&
            rhs_type->is_integer()) {
            rhs = convert(rhs, lhs_type);
        }
        else if (rhs_type->kind ==
                     semantic::Type::Kind::POINTER_TYPE &&
                 lhs_type->is_integer()) {
            lhs = convert(lhs, rhs_type);
        }
        llvm::Value *comparison = nullptr;
        if (op == "==") comparison = builder.CreateICmpEQ(lhs, rhs);
        else if (op == "!=") comparison = builder.CreateICmpNE(lhs, rhs);
        else if (op == "<") comparison = builder.CreateICmpULT(lhs, rhs);
        else if (op == "<=") comparison = builder.CreateICmpULE(lhs, rhs);
        else if (op == ">") comparison = builder.CreateICmpUGT(lhs, rhs);
        else comparison = builder.CreateICmpUGE(lhs, rhs);
        return as_i32(comparison);
    }

    auto lhs = as_i32(expression(*node.lhs));
    auto rhs = as_i32(expression(*node.rhs));
    if (op == "+") return builder.CreateAdd(lhs, rhs, "add");
    if (op == "-") return builder.CreateSub(lhs, rhs, "subtract");
    if (op == "*") return builder.CreateMul(lhs, rhs, "multiply");
    if (op == "/") return builder.CreateSDiv(lhs, rhs, "divide");
    if (op == "%") return builder.CreateSRem(lhs, rhs, "remainder");
    if (op == "<<") return builder.CreateShl(lhs, rhs, "shift_left");
    if (op == ">>") return builder.CreateAShr(lhs, rhs, "shift_right");
    if (op == "&") return builder.CreateAnd(lhs, rhs, "bit_and");
    if (op == "^") return builder.CreateXor(lhs, rhs, "bit_xor");
    if (op == "|") return builder.CreateOr(lhs, rhs, "bit_or");
    if (op == "==") return as_i32(builder.CreateICmpEQ(lhs, rhs));
    if (op == "!=") return as_i32(builder.CreateICmpNE(lhs, rhs));
    if (op == "<") return as_i32(builder.CreateICmpSLT(lhs, rhs));
    if (op == "<=") return as_i32(builder.CreateICmpSLE(lhs, rhs));
    if (op == ">") return as_i32(builder.CreateICmpSGT(lhs, rhs));
    if (op == ">=") return as_i32(builder.CreateICmpSGE(lhs, rhs));
    unsupported("binary operator");
}

inline llvm::Value *LLVM_codegen::expression_node(
    const parser::Call_expression &node) {
    auto callee_type = semantic_result.info(*node.callee).type;
    if (callee_type->kind == semantic::Type::Kind::POINTER_TYPE) {
        callee_type = callee_type->element;
    }
    auto function_type = llvm::cast<llvm::FunctionType>(
        type_of(callee_type));
    auto callee = expression(*node.callee);

    std::vector<llvm::Value *> arguments;
    int argument_index = 0;
    for (auto &argument : node.arguments) {
        auto value = expression(*argument);
        value = convert(
            value,
            callee_type->parameters[argument_index++]);
        arguments.push_back(value);
    }
    return builder.CreateCall(
        function_type,
        callee,
        arguments,
        function_type->getReturnType()->isVoidTy() ? "" : "call");
}

inline llvm::Value *LLVM_codegen::expression_node(
    const parser::Subscript_expression &node) {
    auto address = lvalue(node);
    auto type = semantic_result.info(node).type;
    if (type->kind == semantic::Type::Kind::ARRAY_TYPE) {
        return builder.CreateInBoundsGEP(
            type_of(type),
            address,
            {integer(0), integer(0)},
            "array.decay");
    }
    return builder.CreateLoad(
        type_of(type),
        address,
        "subscript");
}

inline llvm::Value *LLVM_codegen::expression_node(
    const parser::Member_expression &) {
    unsupported("member expression");
}

inline llvm::Value *LLVM_codegen::expression_node(
    const parser::Conditional_expression &node) {
    auto condition = as_condition(expression(*node.condition));
    auto function = builder.GetInsertBlock()->getParent();
    auto true_block = llvm::BasicBlock::Create(
        context,
        "conditional.true",
        function);
    auto false_block = llvm::BasicBlock::Create(
        context,
        "conditional.false",
        function);
    auto merge_block = llvm::BasicBlock::Create(
        context,
        "conditional.end",
        function);

    builder.CreateCondBr(condition, true_block, false_block);

    builder.SetInsertPoint(true_block);
    auto result_type = semantic_result.info(node).type;
    auto true_value = expression(*node.true_expression);
    if (result_type->kind != semantic::Type::Kind::VOID_TYPE) {
        true_value = convert(true_value, result_type);
    }
    true_block = builder.GetInsertBlock();
    builder.CreateBr(merge_block);

    builder.SetInsertPoint(false_block);
    auto false_value = expression(*node.false_expression);
    if (result_type->kind != semantic::Type::Kind::VOID_TYPE) {
        false_value = convert(false_value, result_type);
    }
    false_block = builder.GetInsertBlock();
    builder.CreateBr(merge_block);

    builder.SetInsertPoint(merge_block);
    if (result_type->kind == semantic::Type::Kind::VOID_TYPE) {
        return nullptr;
    }
    auto phi = builder.CreatePHI(
        type_of(result_type),
        2,
        "conditional");
    phi->addIncoming(true_value, true_block);
    phi->addIncoming(false_value, false_block);
    return phi;
}

inline llvm::Value *LLVM_codegen::expression_node(
    const parser::Cast_expression &node) {
    auto value = expression(*node.operand);
    auto target = semantic_result.info(node).type;
    if (target->kind == semantic::Type::Kind::VOID_TYPE) {
        return nullptr;
    }
    return convert(value, target);
}

inline llvm::Value *LLVM_codegen::expression_node(
    const parser::Error_expression &) {
    unsupported("erroneous expression");
}

inline llvm::Value *LLVM_codegen::expression_node(
    const parser::Expression &) {
    unsupported("expression node");
}
