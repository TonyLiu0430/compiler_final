#pragma once

#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/Value.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/raw_ostream.h>

#include "base/literal.hpp"
#include "parser/parser.h"
#include "codegen/llvm_target.hpp"
#include "semantic/constant_evaluator.hpp"
#include "semantic/semantic_analyzer.hpp"

namespace c9ay::codegen {

class LLVM_codegen {
    struct Loop_target {
        llvm::BasicBlock *break_target;
        llvm::BasicBlock *continue_target;
    };

    llvm::LLVMContext context;
    std::unique_ptr<llvm::Module> module;
    llvm::IRBuilder<> builder;
    std::vector<std::unordered_map<std::string, llvm::Value *>> scopes;
    std::vector<Loop_target> loops;
    std::vector<llvm::BasicBlock *> break_targets;
    llvm::Function *current_function = nullptr;
    semantic::Type_ptr current_return_type;
    semantic::Semantic_result semantic_result;

    [[noreturn]] static void unsupported(std::string_view message) {
        throw std::runtime_error(
            "LLVM codegen unsupported: " + std::string(message));
    }

    llvm::Type *type_of(lexer::Token type, int pointer_depth = 0) {
        llvm::Type *result = nullptr;
        if (type.raw == "void") {
            result = llvm::Type::getVoidTy(context);
        }
        else if (type.raw == "int") {
            result = llvm::Type::getInt32Ty(context);
        }
        else {
            unsupported(
                "type " + std::string(type.raw));
        }

        for (int i = 0; i < pointer_depth; i++) {
            result = llvm::PointerType::get(context, 0);
        }
        return result;
    }

    llvm::Type *type_of(const semantic::Type_ptr &type) {
        using Kind = semantic::Type::Kind;
        if (type->kind == Kind::VOID_TYPE) {
            return llvm::Type::getVoidTy(context);
        }
        if (type->kind == Kind::CHAR_TYPE) {
            return llvm::Type::getInt8Ty(context);
        }
        if (type->kind == Kind::INT_TYPE) {
            return llvm::Type::getInt32Ty(context);
        }
        if (type->kind == Kind::POINTER_TYPE) {
            return llvm::PointerType::get(context, 0);
        }
        if (type->kind == Kind::ARRAY_TYPE) {
            if (!type->array_size) {
                unsupported("incomplete array type");
            }
            return llvm::ArrayType::get(
                type_of(type->element),
                *type->array_size);
        }
        if (type->kind == Kind::FUNCTION_TYPE) {
            std::vector<llvm::Type *> parameters;
            for (auto &parameter : type->parameters) {
                parameters.push_back(type_of(parameter));
            }
            return llvm::FunctionType::get(
                type_of(type->return_type),
                parameters,
                false);
        }
        unsupported("semantic type");
    }

    llvm::Value *integer(long long value) {
        return llvm::ConstantInt::get(
            llvm::Type::getInt32Ty(context),
            value,
            true);
    }

    llvm::Value *as_i32(llvm::Value *value) {
        if (!value) unsupported("void value used as expression");
        if (value->getType()->isIntegerTy(32)) return value;
        if (value->getType()->isIntegerTy(1)) {
            return builder.CreateZExt(
                value,
                llvm::Type::getInt32Ty(context),
                "bool");
        }
        if (value->getType()->isIntegerTy(8)) {
            return builder.CreateSExt(
                value,
                llvm::Type::getInt32Ty(context),
                "char");
        }
        unsupported("non-integer expression");
    }

    llvm::Value *convert(
        llvm::Value *value,
        const semantic::Type_ptr &target) {
        if (!value) unsupported("void value used as expression");
        auto target_type = type_of(target);
        if (value->getType() == target_type) return value;

        if (target->is_integer() &&
            value->getType()->isIntegerTy()) {
            return builder.CreateIntCast(
                value,
                target_type,
                true,
                "integer.conversion");
        }
        if (target->kind == semantic::Type::Kind::POINTER_TYPE &&
            value->getType()->isIntegerTy()) {
            return builder.CreateIntToPtr(
                value,
                target_type,
                "pointer.conversion");
        }
        if (target->is_integer() &&
            value->getType()->isPointerTy()) {
            return builder.CreatePtrToInt(
                value,
                target_type,
                "integer.conversion");
        }
        if (target->kind == semantic::Type::Kind::POINTER_TYPE &&
            value->getType()->isPointerTy()) {
            return value;
        }
        unsupported("type conversion");
    }

    llvm::Constant *string_initializer(
        const semantic::Type_ptr &type,
        std::string_view raw) {
        auto value = literal::decode_string(raw);
        if (!value ||
            type->kind != semantic::Type::Kind::ARRAY_TYPE ||
            type->element->kind != semantic::Type::Kind::CHAR_TYPE ||
            !type->array_size) {
            unsupported("string initializer");
        }

        std::vector<llvm::Constant *> elements;
        for (int i = 0; i < static_cast<int>(*type->array_size); i++) {
            unsigned char ch = 0;
            if (i < static_cast<int>(value->size())) {
                ch = static_cast<unsigned char>((*value)[i]);
            }
            elements.push_back(llvm::ConstantInt::get(
                llvm::Type::getInt8Ty(context),
                ch));
        }
        return llvm::ConstantArray::get(
            llvm::cast<llvm::ArrayType>(type_of(type)),
            elements);
    }

    llvm::Constant *string_pointer(std::string_view raw) {
        auto value = literal::decode_string(raw);
        if (!value) unsupported("string literal");

        auto data = llvm::ConstantDataArray::getString(
            context,
            *value,
            true);
        auto global = new llvm::GlobalVariable(
            *module,
            data->getType(),
            true,
            llvm::GlobalValue::PrivateLinkage,
            data,
            "string");
        global->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);

        llvm::Constant *zero = llvm::ConstantInt::get(
            llvm::Type::getInt32Ty(context),
            0);
        std::vector<llvm::Constant *> indices = {zero, zero};
        return llvm::ConstantExpr::getInBoundsGetElementPtr(
            data->getType(),
            global,
            indices);
    }

    llvm::Value *as_condition(llvm::Value *value) {
        if (value->getType()->isPointerTy()) {
            return builder.CreateICmpNE(
                value,
                llvm::ConstantPointerNull::get(
                    llvm::cast<llvm::PointerType>(value->getType())),
                "condition");
        }
        value = as_i32(value);
        return builder.CreateICmpNE(value, integer(0), "condition");
    }

    static bool is_terminated(llvm::BasicBlock *block) {
        return !block->empty() && block->back().isTerminator();
    }

    llvm::AllocaInst *create_entry_alloca(
        llvm::Function *function,
        llvm::Type *type,
        std::string_view name) {
        llvm::IRBuilder<> entry_builder(
            &function->getEntryBlock(),
            function->getEntryBlock().begin());
        return entry_builder.CreateAlloca(
            type,
            nullptr,
            llvm::StringRef(name.data(), name.size()));
    }

    void push_scope() {
        scopes.emplace_back();
    }

    void pop_scope() {
        scopes.pop_back();
    }

    void declare(std::string_view name, llvm::Value *address) {
        if (scopes.empty()) push_scope();
        scopes.back()[std::string(name)] = address;
    }

    llvm::Value *find_address(std::string_view name) {
        for (int i = static_cast<int>(scopes.size()) - 1; i >= 0; i--) {
            auto found = scopes[i].find(std::string(name));
            if (found != scopes[i].end()) return found->second;
        }

        if (auto global = module->getNamedGlobal(
                llvm::StringRef(name.data(), name.size()))) {
            return global;
        }
        return nullptr;
    }

    llvm::Value *lvalue(const parser::Expression &expression) {
        if (auto prefix =
                dynamic_cast<const parser::Prefix_expression *>(&expression)) {
            if (prefix->op.raw == "*") {
                return this->expression(*prefix->operand);
            }
        }

        if (auto subscript =
                dynamic_cast<const parser::Subscript_expression *>(&expression)) {
            auto object = this->expression(*subscript->object);
            auto index = as_i32(this->expression(*subscript->index));
            auto element_type =
                semantic_result.info(expression).type;
            return builder.CreateGEP(
                type_of(element_type),
                object,
                index,
                "subscript.address");
        }

        auto primary =
            dynamic_cast<const parser::Primary_expression *>(&expression);
        if (!primary ||
            primary->token.type != lexer::token_type::IDENTIFIER) {
            unsupported("assignment target");
        }

        auto address = find_address(primary->token.raw);
        if (!address) {
            throw std::runtime_error(
                "unknown variable: " + std::string(primary->token.raw));
        }
        return address;
    }

    llvm::Function *declare_function(
        const parser::Declaration_specifiers &specifiers,
        const parser::Declarator &declarator) {
        auto symbol = semantic_result.symbol(declarator);
        if (!symbol ||
            symbol->type->kind != semantic::Type::Kind::FUNCTION_TYPE) {
            unsupported("function semantic type");
        }
        auto function_type = llvm::cast<llvm::FunctionType>(
            type_of(symbol->type));
        auto name = llvm::StringRef(
            declarator.name.raw.data(),
            declarator.name.raw.size());

        if (auto function = module->getFunction(name)) {
            if (function->getFunctionType() != function_type) {
                throw std::runtime_error(
                    "conflicting function declaration: " +
                    std::string(declarator.name.raw));
            }
            return function;
        }

        return llvm::Function::Create(
            function_type,
            llvm::Function::ExternalLinkage,
            name,
            module.get());
    }

    llvm::Value *expression(const parser::Expression &node) {
        if (auto primary =
                dynamic_cast<const parser::Primary_expression *>(&node)) {
            if (primary->token.type == lexer::token_type::NUMBER ||
                primary->token.type == lexer::token_type::CHAR_CONSTANT) {
                auto value =
                    semantic::Constant_evaluator::evaluate(node);
                if (!value) unsupported("numeric literal");
                return integer(value->value);
            }

            if (primary->token.type == lexer::token_type::STRING_CONSTANT) {
                return string_pointer(primary->token.raw);
            }

            if (primary->token.type == lexer::token_type::IDENTIFIER) {
                auto address = find_address(primary->token.raw);
                if (!address) {
                    if (auto function = module->getFunction(
                            llvm::StringRef(
                                primary->token.raw.data(),
                                primary->token.raw.size()))) {
                        return function;
                    }
                    throw std::runtime_error(
                        "unknown identifier: " +
                        std::string(primary->token.raw));
                }
                auto pointer = llvm::cast<llvm::PointerType>(
                    address->getType());
                llvm::Type *type = nullptr;
                if (auto alloca =
                        llvm::dyn_cast<llvm::AllocaInst>(address)) {
                    type = alloca->getAllocatedType();
                }
                else if (auto global =
                             llvm::dyn_cast<llvm::GlobalVariable>(address)) {
                    type = global->getValueType();
                }
                if (!type || !pointer) unsupported("identifier load");
                auto semantic_type = semantic_result.info(node).type;
                if (semantic_type->kind ==
                    semantic::Type::Kind::ARRAY_TYPE) {
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
                        primary->token.raw.data(),
                        primary->token.raw.size()));
            }
            unsupported("primary expression");
        }

        if (auto prefix =
                dynamic_cast<const parser::Prefix_expression *>(&node)) {
            if (prefix->op.raw == "++" || prefix->op.raw == "--") {
                auto address = lvalue(*prefix->operand);
                auto old = expression(*prefix->operand);
                auto type = semantic_result.info(*prefix->operand).type;
                llvm::Value *value = nullptr;
                if (type->kind == semantic::Type::Kind::POINTER_TYPE) {
                    auto index = prefix->op.raw == "++"
                        ? integer(1)
                        : integer(-1);
                    value = builder.CreateGEP(
                        type_of(type->element),
                        old,
                        index,
                        "pointer.increment");
                }
                else {
                    value = prefix->op.raw == "++"
                        ? builder.CreateAdd(as_i32(old), integer(1), "increment")
                        : builder.CreateSub(as_i32(old), integer(1), "decrement");
                    value = convert(value, type);
                }
                builder.CreateStore(value, address);
                return value;
            }

            if (prefix->op.raw == "&") {
                return lvalue(*prefix->operand);
            }
            if (prefix->op.raw == "*") {
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

            auto operand = as_i32(expression(*prefix->operand));
            if (prefix->op.raw == "+") return operand;
            if (prefix->op.raw == "-") {
                return builder.CreateNeg(operand, "negative");
            }
            if (prefix->op.raw == "!") {
                return as_i32(builder.CreateNot(
                    as_condition(operand),
                    "logical_not"));
            }
            if (prefix->op.raw == "~") {
                return builder.CreateNot(operand, "bit_not");
            }
            unsupported("prefix operator");
        }

        if (auto postfix =
                dynamic_cast<const parser::Postfix_expression *>(&node)) {
            auto address = lvalue(*postfix->operand);
            auto old = expression(*postfix->operand);
            auto type = semantic_result.info(*postfix->operand).type;
            llvm::Value *value = nullptr;
            if (type->kind == semantic::Type::Kind::POINTER_TYPE) {
                auto index = postfix->op.raw == "++"
                    ? integer(1)
                    : integer(-1);
                value = builder.CreateGEP(
                    type_of(type->element),
                    old,
                    index,
                    "pointer.increment");
            }
            else {
                value = postfix->op.raw == "++"
                    ? builder.CreateAdd(as_i32(old), integer(1), "increment")
                    : builder.CreateSub(as_i32(old), integer(1), "decrement");
                value = convert(value, type);
            }
            builder.CreateStore(value, address);
            return old;
        }

        if (auto binary =
                dynamic_cast<const parser::Binary_expression *>(&node)) {
            std::string_view op = binary->op.raw;
            if (op == "=" ||
                op == "+=" || op == "-=" ||
                op == "*=" || op == "/=" || op == "%=" ||
                op == "<<=" || op == ">>=" ||
                op == "&=" || op == "^=" || op == "|=") {
                auto address = lvalue(*binary->lhs);
                auto lhs_semantic_type =
                    semantic_result.info(*binary->lhs).type;
                auto rhs = expression(*binary->rhs);
                if (op == "=") {
                    rhs = convert(rhs, lhs_semantic_type);
                }
                else {
                    auto lhs = expression(*binary->lhs);
                    std::string_view base = op.substr(0, op.size() - 1);
                    if ((base == "+" || base == "-") &&
                        lhs_semantic_type->kind ==
                            semantic::Type::Kind::POINTER_TYPE) {
                        auto index = as_i32(rhs);
                        if (base == "-") {
                            index = builder.CreateNeg(index);
                        }
                        rhs = builder.CreateGEP(
                            type_of(lhs_semantic_type->element),
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
                rhs = convert(rhs, lhs_semantic_type);
                builder.CreateStore(rhs, address);
                return rhs;
            }

            if (op == ",") {
                expression(*binary->lhs);
                return expression(*binary->rhs);
            }

            if (op == "&&" || op == "||") {
                llvm::Function *function = builder.GetInsertBlock()->getParent();
                auto lhs = as_condition(expression(*binary->lhs));
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
                auto rhs = as_condition(expression(*binary->rhs));
                rhs_block = builder.GetInsertBlock();
                builder.CreateBr(merge_block);

                builder.SetInsertPoint(merge_block);
                auto phi = builder.CreatePHI(
                    llvm::Type::getInt1Ty(context),
                    2,
                    "logical");
                phi->addIncoming(
                    llvm::ConstantInt::getBool(
                        context,
                        op == "||"),
                    lhs_block);
                phi->addIncoming(rhs, rhs_block);
                return as_i32(phi);
            }

            auto lhs_type = semantic_result.info(*binary->lhs).type;
            auto rhs_type = semantic_result.info(*binary->rhs).type;
            if ((op == "+" || op == "-") &&
                lhs_type->kind ==
                    semantic::Type::Kind::POINTER_TYPE &&
                rhs_type->is_integer()) {
                auto pointer = expression(*binary->lhs);
                auto index = as_i32(expression(*binary->rhs));
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
                rhs_type->kind ==
                    semantic::Type::Kind::POINTER_TYPE) {
                return builder.CreateGEP(
                    type_of(rhs_type->element),
                    expression(*binary->rhs),
                    as_i32(expression(*binary->lhs)),
                    "pointer.offset");
            }

            if (op == "-" &&
                lhs_type->kind == semantic::Type::Kind::POINTER_TYPE &&
                rhs_type->kind == semantic::Type::Kind::POINTER_TYPE) {
                auto integer_type = llvm::Type::getInt64Ty(context);
                auto lhs = builder.CreatePtrToInt(
                    expression(*binary->lhs),
                    integer_type);
                auto rhs = builder.CreatePtrToInt(
                    expression(*binary->rhs),
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
                auto lhs = expression(*binary->lhs);
                auto rhs = expression(*binary->rhs);
                if (lhs_type->kind ==
                        semantic::Type::Kind::POINTER_TYPE &&
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

            auto lhs = as_i32(expression(*binary->lhs));
            auto rhs = as_i32(expression(*binary->rhs));
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

        if (auto call =
                dynamic_cast<const parser::Call_expression *>(&node)) {
            auto callee_type =
                semantic_result.info(*call->callee).type;
            if (callee_type->kind ==
                semantic::Type::Kind::POINTER_TYPE) {
                callee_type = callee_type->element;
            }
            auto function_type = llvm::cast<llvm::FunctionType>(
                type_of(callee_type));
            auto callee = expression(*call->callee);

            std::vector<llvm::Value *> arguments;
            int argument_index = 0;
            for (auto &argument : call->arguments) {
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

        if (dynamic_cast<const parser::Subscript_expression *>(&node)) {
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

        if (auto conditional =
                dynamic_cast<const parser::Conditional_expression *>(&node)) {
            auto condition = as_condition(
                expression(*conditional->condition));
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
            auto true_value = convert(
                expression(*conditional->true_expression),
                result_type);
            true_block = builder.GetInsertBlock();
            builder.CreateBr(merge_block);

            builder.SetInsertPoint(false_block);
            auto false_value = convert(
                expression(*conditional->false_expression),
                result_type);
            false_block = builder.GetInsertBlock();
            builder.CreateBr(merge_block);

            builder.SetInsertPoint(merge_block);
            auto phi = builder.CreatePHI(
                type_of(result_type),
                2,
                "conditional");
            phi->addIncoming(true_value, true_block);
            phi->addIncoming(false_value, false_block);
            return phi;
        }

        if (auto cast =
                dynamic_cast<const parser::Cast_expression *>(&node)) {
            auto value = expression(*cast->operand);
            auto target = semantic_result.info(node).type;
            auto source = semantic_result.info(*cast->operand).type;
            if (target->kind == semantic::Type::Kind::POINTER_TYPE &&
                source->is_integer()) {
                return builder.CreateIntToPtr(
                    as_i32(value),
                    type_of(target),
                    "pointer.cast");
            }
            if (target->is_integer() &&
                source->kind == semantic::Type::Kind::POINTER_TYPE) {
                return builder.CreatePtrToInt(
                    value,
                    type_of(target),
                    "integer.cast");
            }
            if (target->kind == semantic::Type::Kind::POINTER_TYPE &&
                source->kind == semantic::Type::Kind::POINTER_TYPE) {
                return value;
            }
            return as_i32(value);
        }

        unsupported("expression node");
    }

    void store_initializer(
        const parser::Initializer &initializer,
        const semantic::Type_ptr &type,
        llvm::Value *address) {
        if (auto expression_initializer =
                dynamic_cast<const parser::Expression_initializer *>(
                    &initializer)) {
            if (type->kind == semantic::Type::Kind::ARRAY_TYPE) {
                auto primary =
                    dynamic_cast<const parser::Primary_expression *>(
                        expression_initializer->expression.get());
                if (primary &&
                    primary->token.type ==
                        lexer::token_type::STRING_CONSTANT) {
                    builder.CreateStore(
                        string_initializer(type, primary->token.raw),
                        address);
                    return;
                }
            }

            auto value = expression(*expression_initializer->expression);
            value = convert(value, type);
            builder.CreateStore(value, address);
            return;
        }

        auto list = dynamic_cast<const parser::Initializer_list *>(
            &initializer);
        if (!list) {
            unsupported("initializer");
        }
        if (type->kind != semantic::Type::Kind::ARRAY_TYPE) {
            if (list->elements.empty()) {
                builder.CreateStore(
                    llvm::Constant::getNullValue(type_of(type)),
                    address);
            }
            else {
                store_initializer(
                    *list->elements[0],
                    type,
                    address);
            }
            return;
        }

        builder.CreateStore(
            llvm::Constant::getNullValue(type_of(type)),
            address);
        for (int i = 0; i < static_cast<int>(list->elements.size()); i++) {
            auto element_address = builder.CreateInBoundsGEP(
                type_of(type),
                address,
                {integer(0), integer(i)},
                "initializer.element");
            store_initializer(
                *list->elements[i],
                type->element,
                element_address);
        }
    }

    void declaration(const parser::Declvariable &node) {
        if (node.is_typedef) return;

        for (auto &init_declarator : node.declarators) {
            auto &declarator = *init_declarator->declarator;
            auto symbol = semantic_result.symbol(declarator);
            if (!symbol) unsupported("declaration symbol");
            if (symbol->kind == semantic::Symbol::Kind::FUNCTION) continue;
            llvm::Type *type = type_of(symbol->type);
            auto alloca = create_entry_alloca(
                current_function,
                type,
                declarator.name.raw);
            declare(declarator.name.raw, alloca);

            if (init_declarator->initializer) {
                store_initializer(
                    *init_declarator->initializer,
                    symbol->type,
                    alloca);
            }
        }
    }

    void statement(const parser::Statement &node) {
        if (is_terminated(builder.GetInsertBlock())) return;

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
            if (current_function->getReturnType()->isVoidTy()) {
                builder.CreateRetVoid();
            }
            else {
                builder.CreateRet(
                    return_node->expression
                        ? convert(
                              expression(*return_node->expression),
                              current_return_type)
                        : integer(0));
            }
            return;
        }

        if (auto if_node =
                dynamic_cast<const parser::If_statement *>(&node)) {
            auto condition = as_condition(
                expression(*if_node->condition));
            auto then_block = llvm::BasicBlock::Create(
                context,
                "if.then",
                current_function);
            auto else_block = if_node->else_statement
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
            statement(*if_node->then_statement);
            if (!is_terminated(builder.GetInsertBlock())) {
                builder.CreateBr(merge_block);
            }

            if (else_block) {
                builder.SetInsertPoint(else_block);
                statement(*if_node->else_statement);
                if (!is_terminated(builder.GetInsertBlock())) {
                    builder.CreateBr(merge_block);
                }
            }

            builder.SetInsertPoint(merge_block);
            return;
        }

        if (auto switch_node =
                dynamic_cast<const parser::Switch_statement *>(&node)) {
            auto body = dynamic_cast<const parser::Block *>(
                switch_node->body.get());
            if (!body) unsupported("switch body without block");

            auto end_block = llvm::BasicBlock::Create(
                context,
                "switch.end",
                current_function);
            auto default_block = end_block;
            auto condition = as_i32(
                expression(*switch_node->condition));
            auto switch_instruction = builder.CreateSwitch(
                condition,
                end_block);

            struct Segment {
                llvm::BasicBlock *block;
                const parser::Statement *first;
                int next_index;
            };
            std::vector<Segment> segments;

            for (int i = 0;
                 i < static_cast<int>(body->statements.size());) {
                const parser::Statement *label =
                    body->statements[i].get();
                if (!dynamic_cast<const parser::Case_statement *>(label) &&
                    !dynamic_cast<const parser::Default_statement *>(label)) {
                    unsupported("statement before first switch label");
                }

                auto block = llvm::BasicBlock::Create(
                    context,
                    "switch.case",
                    current_function);
                const parser::Statement *payload = label;

                while (1) {
                    if (auto case_statement =
                            dynamic_cast<const parser::Case_statement *>(
                                payload)) {
                        auto value =
                            semantic::Constant_evaluator::evaluate(
                                *case_statement->value);
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
                        switch_instruction->setDefaultDest(block);
                        payload = default_statement->statement.get();
                        continue;
                    }
                    break;
                }

                int next = i + 1;
                while (next < static_cast<int>(body->statements.size()) &&
                       !dynamic_cast<const parser::Case_statement *>(
                           body->statements[next].get()) &&
                       !dynamic_cast<const parser::Default_statement *>(
                           body->statements[next].get())) {
                    next++;
                }
                segments.push_back({block, payload, next});
                i = next;
            }

            switch_instruction->setDefaultDest(default_block);
            break_targets.push_back(end_block);
            for (int i = 0; i < static_cast<int>(segments.size()); i++) {
                builder.SetInsertPoint(segments[i].block);
                statement(*segments[i].first);

                int begin = i == 0 ? 1 : segments[i - 1].next_index + 1;
                int label_index =
                    i == 0 ? 0 : segments[i - 1].next_index;
                begin = label_index + 1;
                for (int index = begin;
                     index < segments[i].next_index;
                     index++) {
                    statement(*body->statements[index]);
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
            return;
        }

        if (auto while_node =
                dynamic_cast<const parser::While_statement *>(&node)) {
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
                as_condition(expression(*while_node->condition)),
                body_block,
                end_block);

            loops.push_back({end_block, condition_block});
            break_targets.push_back(end_block);
            builder.SetInsertPoint(body_block);
            statement(*while_node->body);
            if (!is_terminated(builder.GetInsertBlock())) {
                builder.CreateBr(condition_block);
            }
            loops.pop_back();
            break_targets.pop_back();

            builder.SetInsertPoint(end_block);
            return;
        }

        if (auto do_node =
                dynamic_cast<const parser::Do_while_statement *>(&node)) {
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
            statement(*do_node->body);
            if (!is_terminated(builder.GetInsertBlock())) {
                builder.CreateBr(condition_block);
            }
            loops.pop_back();
            break_targets.pop_back();

            builder.SetInsertPoint(condition_block);
            builder.CreateCondBr(
                as_condition(expression(*do_node->condition)),
                body_block,
                end_block);
            builder.SetInsertPoint(end_block);
            return;
        }

        if (auto for_node =
                dynamic_cast<const parser::For_statement *>(&node)) {
            push_scope();
            if (for_node->initializer) {
                statement(*for_node->initializer);
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
            if (for_node->condition) {
                builder.CreateCondBr(
                    as_condition(expression(*for_node->condition)),
                    body_block,
                    end_block);
            }
            else {
                builder.CreateBr(body_block);
            }

            loops.push_back({end_block, iteration_block});
            break_targets.push_back(end_block);
            builder.SetInsertPoint(body_block);
            statement(*for_node->body);
            if (!is_terminated(builder.GetInsertBlock())) {
                builder.CreateBr(iteration_block);
            }
            loops.pop_back();
            break_targets.pop_back();

            builder.SetInsertPoint(iteration_block);
            if (for_node->iteration) {
                expression(*for_node->iteration);
            }
            builder.CreateBr(condition_block);

            builder.SetInsertPoint(end_block);
            pop_scope();
            return;
        }

        if (dynamic_cast<const parser::Break_statement *>(&node)) {
            if (break_targets.empty()) unsupported("break outside loop or switch");
            builder.CreateBr(break_targets.back());
            return;
        }

        if (dynamic_cast<const parser::Continue_statement *>(&node)) {
            if (loops.empty()) unsupported("continue outside loop");
            builder.CreateBr(loops.back().continue_target);
            return;
        }

        unsupported("statement node");
    }

    llvm::Constant *constant_initializer(
        const parser::Initializer &initializer,
        const semantic::Type_ptr &type) {
        if (auto expression_initializer =
                dynamic_cast<const parser::Expression_initializer *>(
                    &initializer)) {
            if (type->kind == semantic::Type::Kind::ARRAY_TYPE) {
                auto primary =
                    dynamic_cast<const parser::Primary_expression *>(
                        expression_initializer->expression.get());
                if (primary &&
                    primary->token.type ==
                        lexer::token_type::STRING_CONSTANT) {
                    return string_initializer(type, primary->token.raw);
                }
            }

            if (type->kind == semantic::Type::Kind::POINTER_TYPE) {
                if (auto primary =
                        dynamic_cast<const parser::Primary_expression *>(
                            expression_initializer->expression.get())) {
                    if (primary->token.type ==
                            lexer::token_type::STRING_CONSTANT &&
                        type->element->kind ==
                            semantic::Type::Kind::CHAR_TYPE) {
                        return string_pointer(primary->token.raw);
                    }
                    if (primary->token.type ==
                        lexer::token_type::IDENTIFIER) {
                        auto function = module->getFunction(
                            llvm::StringRef(
                                primary->token.raw.data(),
                                primary->token.raw.size()));
                        if (function) return function;
                    }
                }
            }

            auto value = semantic::Constant_evaluator::evaluate(
                *expression_initializer->expression);
            if (!value) {
                unsupported("non-constant global initializer");
            }
            if (type->kind == semantic::Type::Kind::POINTER_TYPE) {
                if (value->value != 0) {
                    unsupported("non-null integer pointer initializer");
                }
                return llvm::ConstantPointerNull::get(
                    llvm::cast<llvm::PointerType>(type_of(type)));
            }
            return llvm::ConstantInt::get(
                type_of(type),
                value->value,
                true);
        }

        auto list = dynamic_cast<const parser::Initializer_list *>(
            &initializer);
        if (!list) {
            unsupported("global initializer");
        }
        if (type->kind != semantic::Type::Kind::ARRAY_TYPE) {
            if (list->elements.empty()) {
                return llvm::Constant::getNullValue(type_of(type));
            }
            return constant_initializer(*list->elements[0], type);
        }

        std::vector<llvm::Constant *> elements;
        for (auto &element : list->elements) {
            elements.push_back(
                constant_initializer(*element, type->element));
        }
        while (static_cast<int>(elements.size()) <
               static_cast<int>(*type->array_size)) {
            elements.push_back(
                llvm::Constant::getNullValue(type_of(type->element)));
        }
        return llvm::ConstantArray::get(
            llvm::cast<llvm::ArrayType>(type_of(type)),
            elements);
    }

    void global_declaration(const parser::Declvariable &node) {
        if (node.is_typedef) return;

        for (auto &init_declarator : node.declarators) {
            auto &declarator = *init_declarator->declarator;
            auto symbol = semantic_result.symbol(declarator);
            if (!symbol) unsupported("global declaration symbol");
            if (symbol->kind == semantic::Symbol::Kind::FUNCTION) {
                parser::Declaration_specifiers specifiers{
                    node.type,
                    node.is_const,
                    node.is_static,
                    node.is_typedef
                };
                declare_function(specifiers, declarator);
                continue;
            }

            llvm::Constant *initializer =
                llvm::Constant::getNullValue(type_of(symbol->type));
            if (init_declarator->initializer) {
                initializer = constant_initializer(
                    *init_declarator->initializer,
                    symbol->type);
            }

            new llvm::GlobalVariable(
                *module,
                type_of(symbol->type),
                node.is_const,
                node.is_static
                    ? llvm::GlobalValue::InternalLinkage
                    : llvm::GlobalValue::ExternalLinkage,
                initializer,
                llvm::StringRef(
                    declarator.name.raw.data(),
                    declarator.name.raw.size()));
        }
    }

    void predeclare_functions(const parser::Declvariable &node) {
        if (node.is_typedef) return;

        parser::Declaration_specifiers specifiers{
            node.type,
            node.is_const,
            node.is_static,
            node.is_typedef
        };
        for (auto &item : node.declarators) {
            auto symbol = semantic_result.symbol(*item->declarator);
            if (symbol &&
                symbol->kind == semantic::Symbol::Kind::FUNCTION) {
                declare_function(specifiers, *item->declarator);
            }
        }
    }

    void function_body(const parser::Function_definition &node) {
        auto function = declare_function(
            node.specifiers,
            *node.declarator);
        if (!function->empty()) {
            throw std::runtime_error(
                "function already defined: " +
                std::string(node.declarator->name.raw));
        }

        current_function = function;
        current_return_type = semantic_result.symbol(
            *node.declarator)->type->return_type;
        auto entry = llvm::BasicBlock::Create(
            context,
            "entry",
            function);
        builder.SetInsertPoint(entry);
        scopes.clear();
        loops.clear();
        push_scope();

        int parameter_index = 0;
        for (auto &argument : function->args()) {
            auto &parameter =
                node.declarator->parameters[parameter_index++];
            if (!parameter->declarator) {
                unsupported("unnamed function parameter");
            }
            auto name = parameter->declarator->name.raw;
            argument.setName(
                llvm::StringRef(name.data(), name.size()));
            auto alloca = create_entry_alloca(
                function,
                argument.getType(),
                name);
            builder.CreateStore(&argument, alloca);
            declare(name, alloca);
        }

        statement(*node.body);
        if (!is_terminated(builder.GetInsertBlock())) {
            if (function->getReturnType()->isVoidTy()) {
                builder.CreateRetVoid();
            }
            else {
                builder.CreateRet(
                    convert(integer(0), current_return_type));
            }
        }

        pop_scope();
        if (llvm::verifyFunction(*function, &llvm::errs())) {
            throw std::runtime_error(
                "invalid LLVM function: " +
                std::string(node.declarator->name.raw));
        }
        current_function = nullptr;
        current_return_type.reset();
    }

public:
    LLVM_codegen(std::string_view module_name)
        : module(std::make_unique<llvm::Module>(
              llvm::StringRef(module_name.data(), module_name.size()),
              context)),
          builder(context) {}

    llvm::Module &generate(const parser::Program &program) {
        semantic::Semantic_analyzer analyzer;
        semantic_result = analyzer.analyze(program);

        for (auto &external : program.external_declarations) {
            if (auto declaration =
                    dynamic_cast<const parser::Declvariable *>(
                        external.get())) {
                predeclare_functions(*declaration);
            }
            else if (auto function =
                         dynamic_cast<const parser::Function_definition *>(
                             external.get())) {
                declare_function(
                    function->specifiers,
                    *function->declarator);
            }
        }

        for (auto &external : program.external_declarations) {
            if (auto declaration =
                    dynamic_cast<const parser::Declvariable *>(
                        external.get())) {
                global_declaration(*declaration);
            }
        }

        for (auto &external : program.external_declarations) {
            if (auto function =
                    dynamic_cast<const parser::Function_definition *>(
                        external.get())) {
                function_body(*function);
            }
        }

        if (llvm::verifyModule(*module, &llvm::errs())) {
            throw std::runtime_error("invalid LLVM module");
        }
        return *module;
    }

    std::string ir() const {
        std::string result;
        llvm::raw_string_ostream stream(result);
        module->print(stream, nullptr);
        stream.flush();
        return result;
    }

    void emit_object(const std::filesystem::path &path) {
        LLVM_target::emit_object(*module, path);
    }

    void emit_executable(const std::filesystem::path &path) {
        auto object = path;
        object += ".o";
        emit_object(object);
        LLVM_target::link_executable(object, path);
        std::filesystem::remove(object);
    }
};

}  // namespace c9ay::codegen
