#include <iostream>
#include <print>

// #define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
// #include "doctest.h"
#include <filesystem>
#include <fstream>

#include "codegen/llvm_codegen.hpp"
#include "lexer/lexer.hpp"
#include "parser/parser.h"
#include "preprocessor/preprocessor.hpp"

using namespace std;
using namespace c9ay;

std::string read_whole_file_text(const std::string &path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("Cannot open file");
    }

    std::string content;
    content.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());

    return content;
}

void print_usage() {
    std::cerr
        << "usage: compiler [options] <input.c>\n"
        << "options:\n"
        << "  -o <path>      write output to path\n"
        << "  -c             emit object file\n"
        << "  -E             run preprocessor only\n"
        << "  --emit-llvm    emit LLVM IR\n"
        << "  -O0|-O1|-O2|-O3 optimization level\n";
}

int main(int argc, char **argv) {
    try {
        std::filesystem::path path;
        std::filesystem::path output;
        bool emit_llvm = false;
        bool compile_only = false;
        bool preprocess_only = false;
        codegen::Optimization_level optimization_level = codegen::Optimization_level::O0;

        for (int i = 1; i < argc; i++) {
            std::string_view argument = argv[i];
            if (argument == "-h" || argument == "--help") {
                print_usage();
                return 0;
            }
            else if (argument == "--emit-llvm") {
                emit_llvm = true;
            }
            else if (argument == "-E") {
                preprocess_only = true;
            }
            else if (argument == "-c") {
                compile_only = true;
            }
            else if (argument == "-O0") {
                optimization_level = codegen::Optimization_level::O0;
            }
            else if (argument == "-O1") {
                optimization_level = codegen::Optimization_level::O1;
            }
            else if (argument == "-O2") {
                optimization_level = codegen::Optimization_level::O2;
            }
            else if (argument == "-O3") {
                optimization_level = codegen::Optimization_level::O3;
            }
            else if (argument == "-o") {
                if (i + 1 >= argc) {
                    throw std::runtime_error("missing output path after -o");
                }
                output = argv[++i];
            }
            else {
                if (!path.empty()) {
                    throw std::runtime_error("multiple input files are not supported");
                }
                path = argv[i];
            }
        }

        if (path.empty()) {
            print_usage();
            return 1;
        }

        std::string path_text = path.string();
        std::string raw_content = read_whole_file_text(path_text);
        Reader source_reader(path_text, raw_content);
        preprocessor::Preprocessor preprocessor;
        preprocessor.add_include_path(C9AY_INCLUDE_DIR);
        preprocessor::Preprocessed_source processed;
        try {
            processed = preprocessor.process_mapped(source_reader, path);
        } catch (const std::exception &) {
            source_reader.diagnostic().print(std::cerr);
            return 1;
        }
        if (preprocess_only) {
            if (output.empty()) {
                std::print("{}", processed.text);
            }
            else {
                std::ofstream file(output);
                file << processed.text;
            }
            return 0;
        }
        Reader reader(path_text, processed.text, processed.source_map);
        lexer::LexerMgr manager(reader);
        auto lexer = manager.get_lexer();
        auto program = parser::Program::match(lexer);
        if (reader.diagnostic().has_error()) {
            reader.diagnostic().print(std::cerr);
            return 1;
        }
        codegen::LLVM_codegen codegen(path_text, optimization_level);
        try {
            codegen.generate(*program, &reader.diagnostic());
        } catch (const semantic::Compile_error &) {
            reader.diagnostic().print(std::cerr);
            return 1;
        } catch (const std::exception &error) {
            reader.diagnostic().report(
                Diagnostic_level::FATAL_LEVEL, error.what(),
                {static_cast<int>(processed.text.size()), static_cast<int>(processed.text.size())});
            reader.diagnostic().print(std::cerr);
            return 1;
        }

        if (emit_llvm) {
            if (output.empty()) {
                std::print("{}", codegen.ir());
            }
            else {
                std::ofstream file(output);
                file << codegen.ir();
            }
        }
        else if (compile_only) {
            if (output.empty()) {
                output = path;
                output.replace_extension(".o");
            }
            codegen.emit_object(output);
        }
        else {
            if (output.empty()) {
                output = path;
                output.replace_extension(".exe");
            }
            codegen.emit_executable(output);
        }
    } catch (std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
