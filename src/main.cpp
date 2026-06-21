#include <iostream>

// #define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
// #include "doctest.h"
#include <fstream>

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
        std::string path =
            argc > 1
                ? argv[1]
                : R"(E:\CODE_programming\compiler\test\2.c)";
        preprocessor::Preprocessor preprocessor;
        std::string content = preprocessor.process_file(path);
        Reader reader(path, content);
        lexer::LexerMgr manager(reader);
        auto lexer = manager.get_lexer();
        auto program = parser::Program::match(lexer);
        codegen::LLVM_codegen codegen(path);
        codegen.generate(*program);
        std::print("{}", codegen.ir());
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
