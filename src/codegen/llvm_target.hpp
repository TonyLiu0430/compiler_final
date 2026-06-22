#pragma once

#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Module.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Support/CodeGen.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/Program.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/TargetParser/Host.h>

#include "codegen/optimization.hpp"

namespace c9ay::codegen {

class LLVM_target {
    static void initialize() {
        static std::once_flag once;
        std::call_once(once, [] {
            if (llvm::InitializeNativeTarget()) {
                throw std::runtime_error(
                    "failed to initialize native LLVM target");
            }
            if (llvm::InitializeNativeTargetAsmPrinter()) {
                throw std::runtime_error(
                    "failed to initialize native LLVM asm printer");
            }
        });
    }

public:
    static std::unique_ptr<llvm::TargetMachine> create(
        llvm::Module &module,
        Optimization_level optimization_level) {
        initialize();

        std::string triple_name =
            llvm::sys::getDefaultTargetTriple();
        llvm::Triple triple(triple_name);
        std::string error;
        const llvm::Target *target =
            llvm::TargetRegistry::lookupTarget(triple, error);
        if (!target) {
            throw std::runtime_error(
                "LLVM target lookup failed: " + error);
        }

        llvm::TargetOptions options;
        auto machine = std::unique_ptr<llvm::TargetMachine>(
            target->createTargetMachine(
                triple,
                "generic",
                "",
                options,
                llvm::Reloc::PIC_,
                std::nullopt,
                llvm_codegen_level(optimization_level)));
        if (!machine) {
            throw std::runtime_error(
                "failed to create LLVM target machine");
        }

        module.setTargetTriple(triple);
        module.setDataLayout(machine->createDataLayout());
        return machine;
    }

    static void emit_object(
        llvm::Module &module,
        const std::filesystem::path &path,
        Optimization_level optimization_level) {
        auto machine = create(module, optimization_level);

        std::error_code error;
        llvm::raw_fd_ostream output(
            path.string(),
            error,
            llvm::sys::fs::OF_None);
        if (error) {
            throw std::runtime_error(
                "cannot open object output: " + error.message());
        }

        llvm::legacy::PassManager pass;
        if (machine->addPassesToEmitFile(
                pass,
                output,
                nullptr,
                llvm::CodeGenFileType::ObjectFile)) {
            throw std::runtime_error(
                "LLVM target cannot emit object files");
        }

        pass.run(module);
        output.flush();
    }

    static void link_executable(
        const std::filesystem::path &object,
        const std::filesystem::path &executable,
        const std::vector<std::filesystem::path> &libraries = {}) {
        auto gcc = llvm::sys::findProgramByName("gcc");
        if (!gcc) {
            throw std::runtime_error(
                "cannot find MinGW gcc linker in PATH");
        }

        std::vector<std::string> arguments_storage = {
            *gcc,
            object.string()
        };
        for (auto &library : libraries) {
            arguments_storage.push_back(library.string());
        }
        arguments_storage.push_back("-o");
        arguments_storage.push_back(executable.string());
        std::vector<llvm::StringRef> arguments;
        for (auto &argument : arguments_storage) {
            arguments.emplace_back(argument);
        }

        std::string error;
        int result = llvm::sys::ExecuteAndWait(
            *gcc,
            arguments,
            std::nullopt,
            {},
            0,
            0,
            &error);
        if (result != 0) {
            throw std::runtime_error(
                "linker failed: " + error);
        }
    }
};

}  // namespace c9ay::codegen
