#pragma once

#include <llvm/Support/CodeGen.h>

namespace c9ay::codegen {

enum class Optimization_level {
    O0,
    O1,
    O2,
    O3
};

inline llvm::CodeGenOptLevel llvm_codegen_level(
    Optimization_level level) {
    switch (level) {
        case Optimization_level::O0:
            return llvm::CodeGenOptLevel::None;
        case Optimization_level::O1:
            return llvm::CodeGenOptLevel::Less;
        case Optimization_level::O2:
            return llvm::CodeGenOptLevel::Default;
        case Optimization_level::O3:
            return llvm::CodeGenOptLevel::Aggressive;
    }
    return llvm::CodeGenOptLevel::None;
}

}  // namespace c9ay::codegen
