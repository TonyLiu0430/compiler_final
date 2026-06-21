#pragma once

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

#include "parser/parser.h"
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
    llvm::Function *current_function = nullptr;

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

        if (pointer_depth != 0) {
            unsupported("pointer type");
        }
        return result;
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

    llvm::Value *as_condition(llvm::Value *value) {
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
        if (declarator.pointer_depth != 0 ||
            !declarator.array_dimensions.empty()) {
            unsupported("function pointer or array return type");
        }

        std::vector<llvm::Type *> parameters;
        bool void_parameter_list =
            declarator.parameters.size() == 1 &&
            declarator.parameters[0]->specifiers.type.raw == "void" &&
            !declarator.parameters[0]->declarator;
        for (auto &parameter : declarator.parameters) {
            if (void_parameter_list) break;
            int pointer_depth =
                parameter->declarator
                    ? parameter->declarator->pointer_depth
                    : 0;
            parameters.push_back(
                type_of(parameter->specifiers.type, pointer_depth));
        }

        auto function_type = llvm::FunctionType::get(
            type_of(specifiers.type),
            parameters,
            false);
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
                auto value = prefix->op.raw == "++"
                    ? builder.CreateAdd(as_i32(old), integer(1), "increment")
                    : builder.CreateSub(as_i32(old), integer(1), "decrement");
                builder.CreateStore(value, address);
                return value;
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
            unsupported("prefix pointer operator");
        }

        if (auto postfix =
                dynamic_cast<const parser::Postfix_expression *>(&node)) {
            auto address = lvalue(*postfix->operand);
            auto old = as_i32(expression(*postfix->operand));
            auto value = postfix->op.raw == "++"
                ? builder.CreateAdd(old, integer(1), "increment")
                : builder.CreateSub(old, integer(1), "decrement");
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
                auto rhs = as_i32(expression(*binary->rhs));
                if (op != "=") {
                    auto lhs = as_i32(expression(*binary->lhs));
                    std::string_view base = op.substr(0, op.size() - 1);
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
            auto callee_primary =
                dynamic_cast<const parser::Primary_expression *>(
                    call->callee.get());
            if (!callee_primary) unsupported("indirect function call");

            auto function = module->getFunction(
                llvm::StringRef(
                    callee_primary->token.raw.data(),
                    callee_primary->token.raw.size()));
            if (!function) {
                throw std::runtime_error(
                    "unknown function: " +
                    std::string(callee_primary->token.raw));
            }

            if (call->arguments.size() != function->arg_size()) {
                throw std::runtime_error(
                    "wrong argument count: " +
                    std::string(callee_primary->token.raw));
            }

            std::vector<llvm::Value *> arguments;
            for (auto &argument : call->arguments) {
                arguments.push_back(as_i32(expression(*argument)));
            }
            return builder.CreateCall(
                function,
                arguments,
                function->getReturnType()->isVoidTy() ? "" : "call");
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
            auto true_value = as_i32(
                expression(*conditional->true_expression));
            true_block = builder.GetInsertBlock();
            builder.CreateBr(merge_block);

            builder.SetInsertPoint(false_block);
            auto false_value = as_i32(
                expression(*conditional->false_expression));
            false_block = builder.GetInsertBlock();
            builder.CreateBr(merge_block);

            builder.SetInsertPoint(merge_block);
            auto phi = builder.CreatePHI(
                llvm::Type::getInt32Ty(context),
                2,
                "conditional");
            phi->addIncoming(true_value, true_block);
            phi->addIncoming(false_value, false_block);
            return phi;
        }

        if (auto cast =
                dynamic_cast<const parser::Cast_expression *>(&node)) {
            if (cast->type->declarator &&
                cast->type->declarator->pointer_depth != 0) {
                unsupported("pointer cast");
            }
            return as_i32(expression(*cast->operand));
        }

        unsupported("expression node");
    }

    void declaration(const parser::Declvariable &node) {
        if (node.is_typedef) return;

        for (auto &init_declarator : node.declarators) {
            auto &declarator = *init_declarator->declarator;
            if (declarator.is_function) continue;
            if (declarator.pointer_depth != 0 ||
                !declarator.array_dimensions.empty()) {
                unsupported("pointer or array variable");
            }

            llvm::Type *type = type_of(node.type);
            auto alloca = create_entry_alloca(
                current_function,
                type,
                declarator.name.raw);
            declare(declarator.name.raw, alloca);

            if (init_declarator->initializer) {
                auto initializer =
                    dynamic_cast<const parser::Expression_initializer *>(
                        init_declarator->initializer.get());
                if (!initializer) unsupported("initializer list");
                builder.CreateStore(
                    as_i32(expression(*initializer->expression)),
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
                        ? as_i32(expression(*return_node->expression))
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
            builder.SetInsertPoint(body_block);
            statement(*while_node->body);
            if (!is_terminated(builder.GetInsertBlock())) {
                builder.CreateBr(condition_block);
            }
            loops.pop_back();

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
            builder.SetInsertPoint(body_block);
            statement(*do_node->body);
            if (!is_terminated(builder.GetInsertBlock())) {
                builder.CreateBr(condition_block);
            }
            loops.pop_back();

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
            builder.SetInsertPoint(body_block);
            statement(*for_node->body);
            if (!is_terminated(builder.GetInsertBlock())) {
                builder.CreateBr(iteration_block);
            }
            loops.pop_back();

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
            if (loops.empty()) unsupported("break outside loop");
            builder.CreateBr(loops.back().break_target);
            return;
        }

        if (dynamic_cast<const parser::Continue_statement *>(&node)) {
            if (loops.empty()) unsupported("continue outside loop");
            builder.CreateBr(loops.back().continue_target);
            return;
        }

        unsupported("statement node");
    }

    void global_declaration(const parser::Declvariable &node) {
        if (node.is_typedef) return;

        for (auto &init_declarator : node.declarators) {
            auto &declarator = *init_declarator->declarator;
            if (declarator.is_function) {
                parser::Declaration_specifiers specifiers{
                    node.type,
                    node.is_const,
                    node.is_static,
                    node.is_typedef
                };
                declare_function(specifiers, declarator);
                continue;
            }
            if (declarator.pointer_depth != 0 ||
                !declarator.array_dimensions.empty()) {
                unsupported("global pointer or array");
            }

            llvm::Constant *initializer =
                llvm::ConstantInt::get(type_of(node.type), 0);
            if (init_declarator->initializer) {
                auto expression_initializer =
                    dynamic_cast<const parser::Expression_initializer *>(
                        init_declarator->initializer.get());
                if (!expression_initializer) {
                    unsupported("global initializer list");
                }
                auto value = semantic::Constant_evaluator::evaluate(
                    *expression_initializer->expression);
                if (!value) {
                    unsupported("non-constant global initializer");
                }
                initializer = llvm::ConstantInt::get(
                    type_of(node.type),
                    value->value,
                    true);
            }

            new llvm::GlobalVariable(
                *module,
                type_of(node.type),
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
                builder.CreateRet(integer(0));
            }
        }

        pop_scope();
        if (llvm::verifyFunction(*function, &llvm::errs())) {
            throw std::runtime_error(
                "invalid LLVM function: " +
                std::string(node.declarator->name.raw));
        }
        current_function = nullptr;
    }

public:
    LLVM_codegen(std::string_view module_name)
        : module(std::make_unique<llvm::Module>(
              llvm::StringRef(module_name.data(), module_name.size()),
              context)),
          builder(context) {}

    llvm::Module &generate(const parser::Program &program) {
        semantic::Semantic_analyzer analyzer;
        analyzer.analyze(program);

        for (auto &external : program.external_declarations) {
            if (auto declaration =
                    dynamic_cast<const parser::Declvariable *>(
                        external.get())) {
                global_declaration(*declaration);
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
};

}  // namespace c9ay::codegen
