#pragma once

#include <filesystem>
#include <cstdint>
#include <memory>
#include <mutex>
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
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/PassManager.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/Value.h>
#include <llvm/IR/Verifier.h>
#include <llvm/InitializePasses.h>
#include <llvm/PassRegistry.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Transforms/InstCombine/InstCombine.h>
#include <llvm/Transforms/Scalar.h>
#include <llvm/Transforms/Scalar/GVN.h>
#include <llvm/Transforms/Utils.h>

#include "base/literal.hpp"
#include "base/printf_format.hpp"
#include "base/string_hash.hpp"
#include "codegen/llvm_target.hpp"
#include "codegen/optimization.hpp"
#include "parser/parser.h"
#include "parser/reflect_dispatch.hpp"
#include "semantic/constant_evaluator.hpp"
#include "semantic/semantic_analyzer.hpp"
#include "semantic/type_dispatch.hpp"

namespace c9ay::codegen {

class LLVM_codegen {
    struct Loop_target {
        llvm::BasicBlock *break_target;
        llvm::BasicBlock *continue_target;
    };

    llvm::LLVMContext context;
    std::unique_ptr<llvm::Module> module;
    llvm::IRBuilder<> builder;
    using Value_scope = std::unordered_map<
        std::string,
        llvm::Value *,
        Transparent_string_hash,
        Transparent_string_equal>;
    std::vector<Value_scope> scopes;
    std::vector<Loop_target> loops;
    std::vector<llvm::BasicBlock *> break_targets;
    std::unordered_map<const semantic::Record_identity *, llvm::StructType *>
        record_types;
    llvm::Function *current_function = nullptr;
    semantic::Type_ptr current_return_type;
    semantic::Semantic_result semantic_result;
    Optimization_level optimization_level;

    [[noreturn]] static void unsupported(std::string_view message) {
        throw std::runtime_error(
            "LLVM codegen unsupported: " + std::string(message));
    }

    llvm::Type *type_of(const semantic::Type_ptr &type) {
        return semantic::reflect::dispatch<llvm::Type *>(
            *type,
            [this](const auto &type_node) {
                return type_node_of(type_node);
            });
    }

    llvm::Type *type_node_of(
        const semantic::Primitive_type &type) {
        if (type.is_void()) {
            return llvm::Type::getVoidTy(context);
        }
        if (type.is_floating()) {
            if (type.bit_width == 32) {
                return llvm::Type::getFloatTy(context);
            }
            if (type.bit_width == 64) {
                return llvm::Type::getDoubleTy(context);
            }
            if (type.bit_width == 80) {
                return llvm::Type::getX86_FP80Ty(context);
            }
            unsupported("floating point type");
        }
        return llvm::IntegerType::get(context, type.bit_width);
    }

    llvm::Type *type_node_of(
        const semantic::Pointer_type &) {
        return llvm::PointerType::get(context, 0);
    }

    llvm::Type *type_node_of(
        const semantic::Array_type &type) {
        if (!type.size) {
            unsupported("incomplete array type");
        }
        return llvm::ArrayType::get(
            type_of(type.element),
            *type.size);
    }

    llvm::Type *type_node_of(
        const semantic::Function_type &type) {
        std::vector<llvm::Type *> parameters;
        for (auto &parameter : type.parameters) {
            parameters.push_back(type_of(parameter));
        }
        return llvm::FunctionType::get(
            type_of(type.return_type),
            parameters,
            type.is_variadic);
    }

    llvm::Type *type_node_of(
        const semantic::Record_type &type) {
        auto found = record_types.find(type.identity.get());
        if (found != record_types.end()) return found->second;

        auto result = llvm::StructType::create(
            context,
            type.record_name);
        record_types[type.identity.get()] = result;
        if (type.defined) {
            std::vector<llvm::Type *> fields;
            for (auto &field : type.fields) {
                fields.push_back(type_of(field.type));
            }
            result->setBody(fields);
        }
        return result;
    }

    llvm::Type *type_node_of(const semantic::Type &) {
        unsupported("semantic type");
    }

    llvm::Value *integer(long long value) {
        return llvm::ConstantInt::get(
            type_of(semantic_result.integer_type),
            value,
            true);
    }

    llvm::Value *integer(
        const semantic::Type_ptr &type,
        long long value) {
        auto primitive =
            semantic::as_type<semantic::Primitive_type>(type);
        return llvm::ConstantInt::get(
            type_of(type),
            value,
            !primitive || primitive->is_signed);
    }

    llvm::Value *floating(
        const semantic::Type_ptr &type,
        std::string_view raw) {
        if (!raw.empty() &&
            (raw.back() == 'f' || raw.back() == 'F' ||
             raw.back() == 'l' || raw.back() == 'L')) {
            raw.remove_suffix(1);
        }
        return llvm::ConstantFP::get(
            type_of(type),
            llvm::StringRef(raw.data(), raw.size()));
    }

    llvm::Value *index(int value) {
        return llvm::ConstantInt::get(
            llvm::Type::getInt32Ty(context),
            value,
            true);
    }

    llvm::Value *as_integer(llvm::Value *value) {
        if (!value) unsupported("void value used as expression");
        auto target = type_of(semantic_result.integer_type);
        if (value->getType() == target) return value;
        if (value->getType()->isIntegerTy()) {
            return builder.CreateIntCast(
                value,
                target,
                true,
                "integer.promotion");
        }
        unsupported("non-integer expression");
    }

    llvm::Value *as_index(
        llvm::Value *value,
        const semantic::Type_ptr &source = nullptr) {
        if (!value || !value->getType()->isIntegerTy()) {
            unsupported("non-integer index");
        }
        auto primitive =
            semantic::as_type<semantic::Primitive_type>(source);
        return builder.CreateIntCast(
            value,
            type_of(semantic_result.ptrdiff_type),
            !primitive || primitive->is_signed,
            "index");
    }

    llvm::Value *convert(
        llvm::Value *value,
        const semantic::Type_ptr &target,
        const semantic::Type_ptr &source = nullptr) {
        if (!value) unsupported("void value used as expression");
        auto target_type = type_of(target);
        if (value->getType() == target_type) return value;

        if (target->is_integer() &&
            value->getType()->isIntegerTy()) {
            auto primitive =
                semantic::as_type<semantic::Primitive_type>(source);
            return builder.CreateIntCast(
                value,
                target_type,
                !primitive || primitive->is_signed,
                "integer.conversion");
        }
        if (target->is_floating() &&
            value->getType()->isFloatingPointTy()) {
            return builder.CreateFPCast(
                value,
                target_type,
                "floating.conversion");
        }
        if (target->is_floating() &&
            value->getType()->isIntegerTy()) {
            auto primitive =
                semantic::as_type<semantic::Primitive_type>(source);
            return primitive && !primitive->is_signed
                ? builder.CreateUIToFP(
                      value,
                      target_type,
                      "floating.conversion")
                : builder.CreateSIToFP(
                      value,
                      target_type,
                      "floating.conversion");
        }
        if (target->is_integer() &&
            value->getType()->isFloatingPointTy()) {
            auto primitive =
                semantic::as_type<semantic::Primitive_type>(target);
            return primitive && !primitive->is_signed
                ? builder.CreateFPToUI(
                      value,
                      target_type,
                      "integer.conversion")
                : builder.CreateFPToSI(
                      value,
                      target_type,
                      "integer.conversion");
        }
        if (semantic::as_type<semantic::Pointer_type>(target) &&
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
        if (semantic::as_type<semantic::Pointer_type>(target) &&
            value->getType()->isPointerTy()) {
            return value;
        }
        unsupported("type conversion");
    }

    semantic::Type_ptr common_arithmetic_type(
        const semantic::Type_ptr &lhs,
        const semantic::Type_ptr &rhs) {
        auto lhs_primitive =
            semantic::as_type<semantic::Primitive_type>(lhs);
        auto rhs_primitive =
            semantic::as_type<semantic::Primitive_type>(rhs);
        if (!lhs_primitive || !rhs_primitive) {
            unsupported("arithmetic semantic type");
        }
        if (lhs_primitive->is_floating() ||
            rhs_primitive->is_floating()) {
            if (!lhs_primitive->is_floating()) return rhs;
            if (!rhs_primitive->is_floating()) return lhs;
        }
        if (lhs_primitive->rank != rhs_primitive->rank) {
            return lhs_primitive->rank > rhs_primitive->rank
                ? lhs
                : rhs;
        }
        if (lhs_primitive->is_signed != rhs_primitive->is_signed) {
            return lhs_primitive->is_signed ? rhs : lhs;
        }
        return lhs;
    }

    llvm::Constant *string_initializer(
        const semantic::Type_ptr &type,
        std::string_view raw) {
        auto value = literal::decode_string(raw);
        auto array = semantic::as_type<semantic::Array_type>(type);
        if (!value ||
            !array ||
            !semantic::is_character(array->element) ||
            !array->size) {
            unsupported("string initializer");
        }

        std::vector<llvm::Constant *> elements;
        for (int i = 0; i < static_cast<int>(*array->size); i++) {
            unsigned char ch = 0;
            if (i < static_cast<int>(value->size())) {
                ch = static_cast<unsigned char>((*value)[i]);
            }
            elements.push_back(llvm::ConstantInt::get(
                type_of(array->element),
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

        auto zero = llvm::cast<llvm::Constant>(index(0));
        std::vector<llvm::Constant *> indices = {zero, zero};
        return llvm::ConstantExpr::getInBoundsGetElementPtr(
            data->getType(),
            global,
            indices);
    }

    llvm::FunctionCallee runtime_function(
        std::string_view name,
        llvm::Type *return_type,
        std::vector<llvm::Type *> parameters) {
        return module->getOrInsertFunction(
            llvm::StringRef(name.data(), name.size()),
            llvm::FunctionType::get(
                return_type,
                parameters,
                false));
    }

    llvm::Value *builtin_printf(
        const parser::Call_expression &node) {
        auto format =
            static_cast<const parser::Primary_expression *>(
                node.arguments[0].get());
        auto decoded =
            literal::decode_string(format->token.raw);
        if (!decoded) unsupported("printf format string");
        auto parsed = builtin::parse_printf_format(*decoded);
        if (!parsed) unsupported("printf format");

        auto void_type = llvm::Type::getVoidTy(context);
        auto pointer_type = llvm::PointerType::get(context, 0);
        auto write_text = runtime_function(
            "write_text",
            void_type,
            {pointer_type});
        auto write_char = runtime_function(
            "write_char",
            void_type,
            {type_of(semantic_result.character_type)});
        auto print_signed = runtime_function(
            "print_long_long",
            void_type,
            {type_of(semantic_result.ptrdiff_type)});
        auto print_unsigned = runtime_function(
            "print_unsigned_long_long",
            void_type,
            {type_of(semantic_result.size_type)});
        auto print_float = runtime_function(
            "print_float",
            void_type,
            {type_of(semantic_result.double_type)});

        int argument_index = 1;
        for (auto &part : parsed->parts) {
            if (part.kind == builtin::Printf_part_kind::TEXT) {
                if (part.text.empty()) continue;
                std::string raw = "\"";
                for (char ch : part.text) {
                    if (ch == '\n') raw += "\\n";
                    else if (ch == '\t') raw += "\\t";
                    else if (ch == '\f') raw += "\\f";
                    else if (ch == '"' || ch == '\\') {
                        raw.push_back('\\');
                        raw.push_back(ch);
                    }
                    else {
                        raw.push_back(ch);
                    }
                }
                raw.push_back('"');
                builder.CreateCall(
                    write_text,
                    {string_pointer(raw)});
                continue;
            }

            auto &argument = *node.arguments[argument_index++];
            auto value = expression(argument);
            auto source = semantic_result.info(argument).type;
            if (part.kind == builtin::Printf_part_kind::STRING) {
                builder.CreateCall(write_text, {value});
            }
            else if (
                part.kind ==
                builtin::Printf_part_kind::CHARACTER) {
                builder.CreateCall(
                    write_char,
                    {convert(
                        value,
                        semantic_result.character_type,
                        source)});
            }
            else if (
                part.kind ==
                builtin::Printf_part_kind::SIGNED_INTEGER) {
                builder.CreateCall(
                    print_signed,
                    {convert(
                        value,
                        semantic_result.ptrdiff_type,
                        source)});
            }
            else if (
                part.kind ==
                builtin::Printf_part_kind::UNSIGNED_INTEGER) {
                builder.CreateCall(
                    print_unsigned,
                    {convert(
                        value,
                        semantic_result.size_type,
                        source)});
            }
            else {
                builder.CreateCall(
                    print_float,
                    {convert(
                        value,
                        semantic_result.double_type,
                        source)});
            }
        }
        return integer(0);
    }

    llvm::Value *as_condition(llvm::Value *value) {
        if (value->getType()->isPointerTy()) {
            return builder.CreateICmpNE(
                value,
                llvm::ConstantPointerNull::get(
                    llvm::cast<llvm::PointerType>(value->getType())),
                "condition");
        }
        if (value->getType()->isFloatingPointTy()) {
            return builder.CreateFCmpONE(
                value,
                llvm::ConstantFP::getZero(value->getType()),
                "condition");
        }
        value = as_integer(value);
        return builder.CreateICmpNE(value, integer(0), "condition");
    }

    std::optional<constant::Integer_value> constant_value(
        const parser::Expression &expression) {
        auto value = semantic_result.constant(expression);
        if (value) {
            return constant::Integer_value{*value};
        }
        return semantic::Constant_evaluator::evaluate(expression);
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
        scopes.back().insert_or_assign(
            std::string(name),
            address);
    }

    std::string static_local_name(
        const semantic::Symbol &symbol,
        std::string_view name) const {
        std::string result;
        if (current_function) {
            result += current_function->getName().str();
            result += ".";
        }
        result += std::string(name);
        result += ".static.";
        result += std::to_string(
            reinterpret_cast<std::uintptr_t>(&symbol));
        return result;
    }

    llvm::Value *find_address(std::string_view name) {
        for (int i = static_cast<int>(scopes.size()) - 1; i >= 0; i--) {
            auto found = scopes[i].find(name);
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
            auto index = as_index(
                this->expression(*subscript->index),
                semantic_result.info(*subscript->index).type);
            auto element_type =
                semantic_result.info(expression).type;
            return builder.CreateGEP(
                type_of(element_type),
                object,
                index,
                "subscript.address");
        }

        if (auto member =
                dynamic_cast<const parser::Member_expression *>(
                    &expression)) {
            auto object_type =
                semantic_result.info(*member->object).type;
            semantic::Type_ptr record_type = object_type;
            llvm::Value *object_address = nullptr;

            if (member->op.raw == "->") {
                auto pointer =
                    semantic::as_type<semantic::Pointer_type>(
                        object_type);
                if (!pointer) unsupported("member pointer type");
                record_type = pointer->element;
                object_address = this->expression(*member->object);
            }
            else {
                object_address = lvalue(*member->object);
            }

            auto record =
                semantic::as_type<semantic::Record_type>(record_type);
            if (!record) unsupported("member record type");
            int field_index =
                record->field_index(member->member.raw);
            if (field_index < 0) unsupported("member name");
            return builder.CreateStructGEP(
                type_of(record_type),
                object_address,
                field_index,
                "member.address");
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
            !semantic::as_type<semantic::Function_type>(
                symbol->type)) {
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
    llvm::Value *expression_node(
        const parser::Type_query_expression &node);
    llvm::Value *expression_node(const parser::Error_expression &node);
    llvm::Value *expression_node(const parser::Expression &node);

    void store_initializer(
        const parser::Initializer &initializer,
        const semantic::Type_ptr &type,
        llvm::Value *address) {
        if (auto expression_initializer =
                dynamic_cast<const parser::Expression_initializer *>(
                    &initializer)) {
            if (semantic::as_type<semantic::Array_type>(type)) {
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
            auto source_type =
                semantic_result.info(
                    *expression_initializer->expression).type;
            value = convert(value, type, source_type);
            builder.CreateStore(value, address);
            return;
        }

        auto list = dynamic_cast<const parser::Initializer_list *>(
            &initializer);
        if (!list) {
            unsupported("initializer");
        }
        auto array = semantic::as_type<semantic::Array_type>(type);
        auto record = semantic::as_type<semantic::Record_type>(type);
        if (!array && !record) {
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
        if (record) {
            for (int i = 0;
                 i < static_cast<int>(list->elements.size());
                 i++) {
                auto field_address = builder.CreateStructGEP(
                    type_of(type),
                    address,
                    i,
                    "initializer.field");
                store_initializer(
                    *list->elements[i],
                    record->fields[i].type,
                    field_address);
            }
            return;
        }
        for (int i = 0; i < static_cast<int>(list->elements.size()); i++) {
            auto element_address = builder.CreateInBoundsGEP(
                type_of(type),
                address,
                {index(0), index(i)},
                "initializer.element");
            store_initializer(
                *list->elements[i],
                array->element,
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
            if (symbol->has_static_storage) {
                llvm::Constant *initializer =
                    llvm::Constant::getNullValue(type);
                if (init_declarator->initializer) {
                    initializer = constant_initializer(
                        *init_declarator->initializer,
                        symbol->type);
                }
                auto name =
                    static_local_name(*symbol, declarator.name.raw);
                auto global = new llvm::GlobalVariable(
                    *module,
                    type,
                    node.is_const,
                    llvm::GlobalValue::InternalLinkage,
                    initializer,
                    name);
                declare(declarator.name.raw, global);
                continue;
            }
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
    void statement_node(const parser::Struct_definition &node);
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
    void statement_node(const parser::Statement &node);

    llvm::Constant *constant_initializer(
        const parser::Initializer &initializer,
        const semantic::Type_ptr &type) {
        if (auto expression_initializer =
                dynamic_cast<const parser::Expression_initializer *>(
                    &initializer)) {
            if (semantic::as_type<semantic::Array_type>(type)) {
                auto primary =
                    dynamic_cast<const parser::Primary_expression *>(
                        expression_initializer->expression.get());
                if (primary &&
                    primary->token.type ==
                        lexer::token_type::STRING_CONSTANT) {
                    return string_initializer(type, primary->token.raw);
                }
            }

            if (auto pointer =
                    semantic::as_type<semantic::Pointer_type>(type)) {
                if (auto primary =
                        dynamic_cast<const parser::Primary_expression *>(
                            expression_initializer->expression.get())) {
                    if (primary->token.type ==
                            lexer::token_type::STRING_CONSTANT &&
                        semantic::is_character(pointer->element)) {
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

            if (type->is_floating()) {
                auto primary =
                    dynamic_cast<const parser::Primary_expression *>(
                        expression_initializer->expression.get());
                if (!primary ||
                    primary->token.type != lexer::token_type::NUMBER) {
                    unsupported(
                        "non-constant floating global initializer");
                }
                auto source_type =
                    semantic_result.info(*primary).type;
                auto value = llvm::cast<llvm::Constant>(
                    floating(source_type, primary->token.raw));
                return llvm::cast<llvm::Constant>(
                    convert(value, type, source_type));
            }

            auto value = constant_value(
                *expression_initializer->expression);
            if (!value) {
                unsupported("non-constant global initializer");
            }
            if (semantic::as_type<semantic::Pointer_type>(type)) {
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
        auto array = semantic::as_type<semantic::Array_type>(type);
        auto record = semantic::as_type<semantic::Record_type>(type);
        if (!array && !record) {
            if (list->elements.empty()) {
                return llvm::Constant::getNullValue(type_of(type));
            }
            return constant_initializer(*list->elements[0], type);
        }

        std::vector<llvm::Constant *> elements;
        if (record) {
            for (int i = 0;
                 i < static_cast<int>(list->elements.size());
                 i++) {
                elements.push_back(constant_initializer(
                    *list->elements[i],
                    record->fields[i].type));
            }
            for (int i = static_cast<int>(elements.size());
                 i < static_cast<int>(record->fields.size());
                 i++) {
                elements.push_back(
                    llvm::Constant::getNullValue(
                        type_of(record->fields[i].type)));
            }
            return llvm::ConstantStruct::get(
                llvm::cast<llvm::StructType>(type_of(type)),
                elements);
        }
        for (auto &element : list->elements) {
            elements.push_back(
                constant_initializer(*element, array->element));
        }
        while (static_cast<int>(elements.size()) <
               static_cast<int>(*array->size)) {
            elements.push_back(
                llvm::Constant::getNullValue(
                    type_of(array->element)));
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
                    node.type_name,
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
            node.type_name,
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
        auto function_type =
            semantic::as_type<semantic::Function_type>(
                semantic_result.symbol(
                    *node.declarator)->type);
        if (!function_type) unsupported("function definition type");
        current_return_type = function_type->return_type;
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

    void optimize() {
        if (optimization_level == Optimization_level::O0) return;

        static std::once_flag pass_initialization;
        std::call_once(pass_initialization, [] {
            auto &registry = *llvm::PassRegistry::getPassRegistry();
            llvm::initializeCore(registry);
            llvm::initializeAnalysis(registry);
            llvm::initializeTransformUtils(registry);
            llvm::initializeScalarOpts(registry);
            llvm::initializeInstCombine(registry);
        });

        llvm::legacy::PassManager passes;
        passes.add(llvm::createPromoteMemoryToRegisterPass());
        passes.add(llvm::createSROAPass());
        passes.add(llvm::createInstructionCombiningPass());
        passes.add(llvm::createReassociatePass());
        passes.add(llvm::createCFGSimplificationPass());

        if (optimization_level == Optimization_level::O2 ||
            optimization_level == Optimization_level::O3) {
            passes.add(llvm::createGVNPass());
            passes.add(llvm::createDeadStoreEliminationPass());
            passes.add(llvm::createLICMPass());
            passes.add(llvm::createInstructionCombiningPass());
            passes.add(llvm::createCFGSimplificationPass());
        }

        if (optimization_level == Optimization_level::O3) {
            passes.add(llvm::createLoopUnrollPass(3));
            passes.add(llvm::createInstructionCombiningPass());
            passes.add(llvm::createGVNPass());
        }

        passes.add(llvm::createDeadCodeEliminationPass());
        passes.run(*module);
    }

public:
    LLVM_codegen(
        std::string_view module_name,
        Optimization_level _optimization_level =
            Optimization_level::O0)
        : module(std::make_unique<llvm::Module>(
              llvm::StringRef(module_name.data(), module_name.size()),
              context)),
          builder(context),
          optimization_level(_optimization_level) {}

    llvm::Module &generate(
        const parser::Program &program,
        Diagnostic *diagnostics = nullptr) {
        semantic::Semantic_analyzer analyzer;
        semantic_result = analyzer.analyze(program, diagnostics);

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
        optimize();
        if (llvm::verifyModule(*module, &llvm::errs())) {
            throw std::runtime_error(
                "LLVM optimization produced invalid module");
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
        LLVM_target::emit_object(
            *module,
            path,
            optimization_level);
    }

    void emit_executable(const std::filesystem::path &path) {
        auto object = path;
        object += ".o";
        emit_object(object);
        LLVM_target::link_executable(
            object,
            path,
            {C9AY_RUNTIME_OBJECT});
        std::filesystem::remove(object);
    }
};

#include "codegen/llvm_expression.inl"
#include "codegen/llvm_statement.inl"

}  // namespace c9ay::codegen
