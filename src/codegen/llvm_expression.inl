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
    if (node.token.type == lexer::token_type::NUMBER) {
        auto type = semantic_result.info(node).type;
        if (type->is_floating()) {
            return floating(type, node.token.raw);
        }
        auto value = semantic::Constant_evaluator::evaluate(node);
        if (!value) unsupported("numeric literal");
        return integer(type, value->value);
    }

    if (node.token.type == lexer::token_type::CHAR_CONSTANT) {
        auto value = semantic::Constant_evaluator::evaluate(node);
        if (!value) unsupported("character literal");
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
    if (semantic::as_type<semantic::Array_type>(semantic_type)) {
        return builder.CreateInBoundsGEP(
            type,
            address,
            {index(0), index(0)},
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
        if (auto pointer =
                semantic::as_type<semantic::Pointer_type>(type)) {
            auto index = node.op.raw == "++"
                ? integer(1)
                : integer(-1);
            value = builder.CreateGEP(
                type_of(pointer->element),
                old,
                index,
                "pointer.increment");
        }
        else {
            if (type->is_floating()) {
                auto one = llvm::ConstantFP::get(
                    type_of(type),
                    1.0);
                value = node.op.raw == "++"
                    ? builder.CreateFAdd(old, one, "increment")
                    : builder.CreateFSub(old, one, "decrement");
            }
            else {
                value = node.op.raw == "++"
                    ? builder.CreateAdd(
                          old,
                          integer(type, 1),
                          "increment")
                    : builder.CreateSub(
                          old,
                          integer(type, 1),
                          "decrement");
            }
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
        if (semantic::as_type<semantic::Array_type>(type)) {
            return builder.CreateInBoundsGEP(
                type_of(type),
                address,
                {index(0), index(0)},
                "array.decay");
        }
        return builder.CreateLoad(
            type_of(type),
            address,
            "dereference");
    }

    auto operand = expression(*node.operand);
    if (node.op.raw == "+") return operand;
    if (node.op.raw == "-") {
        return operand->getType()->isFloatingPointTy()
            ? builder.CreateFNeg(operand, "negative")
            : builder.CreateNeg(operand, "negative");
    }
    if (node.op.raw == "!") {
        return as_integer(builder.CreateNot(
            as_condition(operand),
            "logical_not"));
    }
    if (node.op.raw == "~") {
        return builder.CreateNot(
            operand,
            "bit_not");
    }
    unsupported("prefix operator");
}

inline llvm::Value *LLVM_codegen::expression_node(
    const parser::Postfix_expression &node) {
    auto address = lvalue(*node.operand);
    auto old = expression(*node.operand);
    auto type = semantic_result.info(*node.operand).type;
    llvm::Value *value = nullptr;
    if (auto pointer =
            semantic::as_type<semantic::Pointer_type>(type)) {
        auto index = node.op.raw == "++"
            ? integer(1)
            : integer(-1);
        value = builder.CreateGEP(
            type_of(pointer->element),
            old,
            index,
            "pointer.increment");
    }
    else {
        if (type->is_floating()) {
            auto one = llvm::ConstantFP::get(
                type_of(type),
                1.0);
            value = node.op.raw == "++"
                ? builder.CreateFAdd(old, one, "increment")
                : builder.CreateFSub(old, one, "decrement");
        }
        else {
            value = node.op.raw == "++"
                ? builder.CreateAdd(
                      old,
                      integer(type, 1),
                      "increment")
                : builder.CreateSub(
                      old,
                      integer(type, 1),
                      "decrement");
        }
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
        auto rhs_type = semantic_result.info(*node.rhs).type;
        auto rhs = expression(*node.rhs);
        if (op == "=") {
            rhs = convert(rhs, lhs_type, rhs_type);
        }
        else {
            auto lhs = expression(*node.lhs);
            std::string_view base = op.substr(0, op.size() - 1);
            if (auto pointer =
                    semantic::as_type<semantic::Pointer_type>(lhs_type);
                (base == "+" || base == "-") && pointer) {
                auto index = as_index(rhs, rhs_type);
                if (base == "-") {
                    index = builder.CreateNeg(index);
                }
                rhs = builder.CreateGEP(
                    type_of(pointer->element),
                    lhs,
                    index);
            }
            else {
                auto operation_type =
                    common_arithmetic_type(lhs_type, rhs_type);
                lhs = convert(lhs, operation_type, lhs_type);
                rhs = convert(rhs, operation_type, rhs_type);
                if (operation_type->is_floating()) {
                    if (base == "+") rhs = builder.CreateFAdd(lhs, rhs);
                    else if (base == "-") rhs = builder.CreateFSub(lhs, rhs);
                    else if (base == "*") rhs = builder.CreateFMul(lhs, rhs);
                    else if (base == "/") rhs = builder.CreateFDiv(lhs, rhs);
                }
                else {
                    auto primitive =
                        semantic::as_type<semantic::Primitive_type>(
                            operation_type);
                    if (base == "+") rhs = builder.CreateAdd(lhs, rhs);
                    else if (base == "-") rhs = builder.CreateSub(lhs, rhs);
                    else if (base == "*") rhs = builder.CreateMul(lhs, rhs);
                    else if (base == "/") {
                        rhs = primitive->is_signed
                            ? builder.CreateSDiv(lhs, rhs)
                            : builder.CreateUDiv(lhs, rhs);
                    }
                    else if (base == "%") {
                        rhs = primitive->is_signed
                            ? builder.CreateSRem(lhs, rhs)
                            : builder.CreateURem(lhs, rhs);
                    }
                    else if (base == "<<") rhs = builder.CreateShl(lhs, rhs);
                    else if (base == ">>") {
                        rhs = primitive->is_signed
                            ? builder.CreateAShr(lhs, rhs)
                            : builder.CreateLShr(lhs, rhs);
                    }
                    else if (base == "&") rhs = builder.CreateAnd(lhs, rhs);
                    else if (base == "^") rhs = builder.CreateXor(lhs, rhs);
                    else if (base == "|") rhs = builder.CreateOr(lhs, rhs);
                }
            }
        }
        rhs = convert(rhs, lhs_type, rhs_type);
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
        return as_integer(phi);
    }

    auto lhs_type = semantic_result.info(*node.lhs).type;
    auto rhs_type = semantic_result.info(*node.rhs).type;
    auto lhs_pointer =
        semantic::as_type<semantic::Pointer_type>(lhs_type);
    auto rhs_pointer =
        semantic::as_type<semantic::Pointer_type>(rhs_type);
    if ((op == "+" || op == "-") &&
        lhs_pointer &&
        rhs_type->is_integer()) {
        auto pointer = expression(*node.lhs);
        auto index = as_index(
            expression(*node.rhs),
            rhs_type);
        if (op == "-") {
            index = builder.CreateNeg(index, "negative.index");
        }
        return builder.CreateGEP(
            type_of(lhs_pointer->element),
            pointer,
            index,
            "pointer.offset");
    }

    if (op == "+" &&
        lhs_type->is_integer() &&
        rhs_pointer) {
        return builder.CreateGEP(
            type_of(rhs_pointer->element),
            expression(*node.rhs),
            as_index(
                expression(*node.lhs),
                lhs_type),
            "pointer.offset");
    }

    if (op == "-" &&
        lhs_pointer &&
        rhs_pointer) {
        auto integer_type =
            type_of(semantic_result.ptrdiff_type);
        auto lhs = builder.CreatePtrToInt(
            expression(*node.lhs),
            integer_type);
        auto rhs = builder.CreatePtrToInt(
            expression(*node.rhs),
            integer_type);
        auto bytes = builder.CreateSub(lhs, rhs, "pointer.bytes");
        auto element_size = llvm::ConstantExpr::getSizeOf(
            type_of(lhs_pointer->element));
        auto distance = builder.CreateSDiv(
            bytes,
            element_size,
            "pointer.distance");
        return builder.CreateIntCast(
            distance,
            type_of(semantic_result.ptrdiff_type),
            true);
    }

    if ((op == "==" || op == "!=" ||
         op == "<" || op == "<=" ||
         op == ">" || op == ">=") &&
        (lhs_pointer || rhs_pointer)) {
        auto lhs = expression(*node.lhs);
        auto rhs = expression(*node.rhs);
        if (lhs_pointer &&
            rhs_type->is_integer()) {
            rhs = convert(rhs, lhs_type);
        }
        else if (rhs_pointer &&
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
        return as_integer(comparison);
    }

    auto operation_type =
        common_arithmetic_type(lhs_type, rhs_type);
    auto lhs = convert(
        expression(*node.lhs),
        operation_type,
        lhs_type);
    auto rhs = convert(
        expression(*node.rhs),
        operation_type,
        rhs_type);
    if (operation_type->is_floating()) {
        if (op == "+") return builder.CreateFAdd(lhs, rhs, "add");
        if (op == "-") return builder.CreateFSub(lhs, rhs, "subtract");
        if (op == "*") return builder.CreateFMul(lhs, rhs, "multiply");
        if (op == "/") return builder.CreateFDiv(lhs, rhs, "divide");
        if (op == "==") return as_integer(builder.CreateFCmpOEQ(lhs, rhs));
        if (op == "!=") return as_integer(builder.CreateFCmpUNE(lhs, rhs));
        if (op == "<") return as_integer(builder.CreateFCmpOLT(lhs, rhs));
        if (op == "<=") return as_integer(builder.CreateFCmpOLE(lhs, rhs));
        if (op == ">") return as_integer(builder.CreateFCmpOGT(lhs, rhs));
        if (op == ">=") return as_integer(builder.CreateFCmpOGE(lhs, rhs));
    }
    else {
        auto primitive =
            semantic::as_type<semantic::Primitive_type>(
                operation_type);
        if (op == "+") return builder.CreateAdd(lhs, rhs, "add");
        if (op == "-") return builder.CreateSub(lhs, rhs, "subtract");
        if (op == "*") return builder.CreateMul(lhs, rhs, "multiply");
        if (op == "/") {
            return primitive->is_signed
                ? builder.CreateSDiv(lhs, rhs, "divide")
                : builder.CreateUDiv(lhs, rhs, "divide");
        }
        if (op == "%") {
            return primitive->is_signed
                ? builder.CreateSRem(lhs, rhs, "remainder")
                : builder.CreateURem(lhs, rhs, "remainder");
        }
        if (op == "<<") return builder.CreateShl(lhs, rhs, "shift_left");
        if (op == ">>") {
            return primitive->is_signed
                ? builder.CreateAShr(lhs, rhs, "shift_right")
                : builder.CreateLShr(lhs, rhs, "shift_right");
        }
        if (op == "&") return builder.CreateAnd(lhs, rhs, "bit_and");
        if (op == "^") return builder.CreateXor(lhs, rhs, "bit_xor");
        if (op == "|") return builder.CreateOr(lhs, rhs, "bit_or");
        if (op == "==") return as_integer(builder.CreateICmpEQ(lhs, rhs));
        if (op == "!=") return as_integer(builder.CreateICmpNE(lhs, rhs));
        if (op == "<") {
            return as_integer(
                primitive->is_signed
                    ? builder.CreateICmpSLT(lhs, rhs)
                    : builder.CreateICmpULT(lhs, rhs));
        }
        if (op == "<=") {
            return as_integer(
                primitive->is_signed
                    ? builder.CreateICmpSLE(lhs, rhs)
                    : builder.CreateICmpULE(lhs, rhs));
        }
        if (op == ">") {
            return as_integer(
                primitive->is_signed
                    ? builder.CreateICmpSGT(lhs, rhs)
                    : builder.CreateICmpUGT(lhs, rhs));
        }
        if (op == ">=") {
            return as_integer(
                primitive->is_signed
                    ? builder.CreateICmpSGE(lhs, rhs)
                    : builder.CreateICmpUGE(lhs, rhs));
        }
    }
    unsupported("binary operator");
}

inline llvm::Value *LLVM_codegen::expression_node(
    const parser::Call_expression &node) {
    auto builtin_name =
        dynamic_cast<const parser::Primary_expression *>(
            node.callee.get());
    if (builtin_name &&
        builtin_name->token.type ==
            lexer::token_type::IDENTIFIER &&
        builtin_name->token.raw == "printf") {
        return builtin_printf(node);
    }

    auto callee_type = semantic_result.info(*node.callee).type;
    if (auto pointer =
            semantic::as_type<semantic::Pointer_type>(callee_type)) {
        callee_type = pointer->element;
    }
    auto semantic_function =
        semantic::as_type<semantic::Function_type>(callee_type);
    if (!semantic_function) unsupported("call target type");
    auto function_type = llvm::cast<llvm::FunctionType>(
        type_of(callee_type));
    auto callee = expression(*node.callee);

    std::vector<llvm::Value *> arguments;
    int argument_index = 0;
    for (auto &argument : node.arguments) {
        auto value = expression(*argument);
        if (argument_index <
            static_cast<int>(semantic_function->parameters.size())) {
            value = convert(
                value,
                semantic_function->parameters[argument_index],
                semantic_result.info(*argument).type);
        }
        argument_index++;
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
    if (semantic::as_type<semantic::Array_type>(type)) {
        return builder.CreateInBoundsGEP(
            type_of(type),
            address,
            {index(0), index(0)},
            "array.decay");
    }
    return builder.CreateLoad(
        type_of(type),
        address,
        "subscript");
}

inline llvm::Value *LLVM_codegen::expression_node(
    const parser::Member_expression &node) {
    auto object_info = semantic_result.info(*node.object);
    if (node.op.raw == "." && !object_info.is_lvalue) {
        auto record =
            semantic::as_type<semantic::Record_type>(object_info.type);
        if (!record) unsupported("member record value");
        int field_index = record->field_index(node.member.raw);
        if (field_index < 0) unsupported("member name");
        return builder.CreateExtractValue(
            expression(*node.object),
            field_index,
            "member");
    }

    auto address = lvalue(node);
    auto type = semantic_result.info(node).type;
    if (semantic::as_type<semantic::Array_type>(type)) {
        return builder.CreateInBoundsGEP(
            type_of(type),
            address,
            {index(0), index(0)},
            "member.array.decay");
    }
    return builder.CreateLoad(
        type_of(type),
        address,
        "member");
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
    if (!semantic::is_void(result_type)) {
        true_value = convert(
            true_value,
            result_type,
            semantic_result.info(*node.true_expression).type);
    }
    true_block = builder.GetInsertBlock();
    builder.CreateBr(merge_block);

    builder.SetInsertPoint(false_block);
    auto false_value = expression(*node.false_expression);
    if (!semantic::is_void(result_type)) {
        false_value = convert(
            false_value,
            result_type,
            semantic_result.info(*node.false_expression).type);
    }
    false_block = builder.GetInsertBlock();
    builder.CreateBr(merge_block);

    builder.SetInsertPoint(merge_block);
    if (semantic::is_void(result_type)) {
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
    if (semantic::is_void(target)) {
        return nullptr;
    }
    return convert(
        value,
        target,
        semantic_result.info(*node.operand).type);
}

inline llvm::Value *LLVM_codegen::expression_node(
    const parser::Type_query_expression &node) {
    auto value = semantic_result.constant(node);
    if (!value) unsupported("type query constant");
    return integer(
        semantic_result.info(node).type,
        *value);
}

inline llvm::Value *LLVM_codegen::expression_node(
    const parser::Error_expression &) {
    unsupported("erroneous expression");
}

inline llvm::Value *LLVM_codegen::expression_node(
    const parser::Expression &) {
    unsupported("expression node");
}
