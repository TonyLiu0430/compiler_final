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
#include "codegen/llvm_target.hpp"
#include "parser/parser.h"
#include "parser/reflect_dispatch.hpp"
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

        auto linkage = specifiers.is_static
            ? llvm::Function::InternalLinkage
            : llvm::Function::ExternalLinkage;
        return llvm::Function::Create(
            function_type,
            linkage,
            name,
            module.get());
    }

    llvm::Value *expression(const parser::Expression &node);
    llvm::Value *expression_node(const parser::Primary_expression &node);
    llvm::Value *expression_node(const parser::Prefix_expression &node);
    llvm::Value *expression_node(const parser::Postfix_expression &node);
    llvm::Value *expression_node(const parser::Binary_expression &node);
    llvm::Value *expression_node(const parser::Call_expression &node);
    llvm::Value *expression_node(const parser::Subscript_expression &node);
    llvm::Value *expression_node(const parser::Member_expression &node);
    llvm::Value *expression_node(const parser::Conditional_expression &node);
    llvm::Value *expression_node(const parser::Cast_expression &node);
    llvm::Value *expression_node(const parser::Error_expression &node);
    llvm::Value *expression_node(const parser::Expression &node);

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

    void statement(const parser::Statement &node);
    void statement_node(const parser::Block &node);
    void statement_node(const parser::Declvariable &node);
    void statement_node(const parser::Expression_statement &node);
    void statement_node(const parser::Return_statement &node);
    void statement_node(const parser::If_statement &node);
    void statement_node(const parser::Switch_statement &node);
    void statement_node(const parser::While_statement &node);
    void statement_node(const parser::Do_while_statement &node);
    void statement_node(const parser::For_statement &node);
    void statement_node(const parser::Break_statement &node);
    void statement_node(const parser::Continue_statement &node);
    void statement_node(const parser::Case_statement &node);
    void statement_node(const parser::Default_statement &node);
    void statement_node(const parser::Statement &node);

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

#include "codegen/llvm_expression.inl"
#include "codegen/llvm_statement.inl"

}  // namespace c9ay::codegen
