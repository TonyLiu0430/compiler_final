#include <iostream>

// #define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
// #include "doctest.h"
#include <fstream>
#include <filesystem>

#include "codegen/llvm_codegen.hpp"
#include "lexer/lexer.hpp"
#include "parser/parser.h"
#include "preprocessor/preprocessor.hpp"

using namespace std;
using namespace c9ay;

std::string read_whole_file_text(const std::string& path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("Cannot open file");
    }

    std::string content;
    content.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());

    return content;
}

int main(int argc, char **argv) {
    try {
        std::filesystem::path path =
            R"(E:\CODE_programming\compiler\test\2.c)";
        std::filesystem::path output;
        bool emit_llvm = false;
        bool compile_only = false;

        for (int i = 1; i < argc; i++) {
            std::string_view argument = argv[i];
            if (argument == "--emit-llvm") {
                emit_llvm = true;
            }
            else if (argument == "-c") {
                compile_only = true;
            }
            else if (argument == "-o") {
                if (i + 1 >= argc) {
                    throw std::runtime_error(
                        "missing output path after -o");
                }
                output = argv[++i];
            }
            else {
                path = argv[i];
            }
        }

        preprocessor::Preprocessor preprocessor;
        std::string content = preprocessor.process_file(path);
        std::string path_text = path.string();
        Reader reader(path_text, content);
        lexer::LexerMgr manager(reader);
        auto lexer = manager.get_lexer();
        auto program = parser::Program::match(lexer);
        codegen::LLVM_codegen codegen(path_text);
        codegen.generate(*program);

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
    } catch (std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}

// TEST_CASE("test lexer") {
//     SUBCASE("1") {
//         c9ay::Reader reader("1.c", "'H'");
//         reader.next_char();
//         CHECK(match<token_type::CHAR_CONSTANT>(reader).raw == "'H'");
//     }
//     SUBCASE("2") {
//         c9ay::Reader reader("2.c", "'\\n'");
//         reader.next_char();
//         CHECK(match<token_type::CHAR_CONSTANT>(reader).raw == "'\\n'");
//     }
//     SUBCASE("3") {
//         c9ay::Reader reader("3.c", "'\\\\'");
//         reader.next_char();
//         CHECK(match<token_type::CHAR_CONSTANT>(reader).raw == "'\\\\'");
//     }
// }
