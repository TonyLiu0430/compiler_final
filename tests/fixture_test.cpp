#include "doctest.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "codegen/llvm_codegen.hpp"
#include "parser/parser.h"
#include "preprocessor/preprocessor.hpp"
#include "semantic/semantic_analyzer.hpp"

using namespace c9ay;

namespace fixture_test {

struct Expectation {
    int errors = -1;
    int externals = -1;
    int main_statements = -1;
    std::vector<std::string> messages;
};

std::string read(const std::filesystem::path &path) {
    std::ifstream file(path, std::ios::binary);
    return {
        std::istreambuf_iterator<char>(file),
        std::istreambuf_iterator<char>()
    };
}

Expectation expectation(std::string_view source) {
    Expectation result;
    std::istringstream lines{std::string(source)};
    std::string line;
    while (std::getline(lines, line)) {
        if (!line.starts_with("// EXPECT-")) continue;
        auto separator = line.find(':');
        if (separator == std::string::npos) continue;
        auto key = line.substr(3, separator - 3);
        auto value = line.substr(separator + 1);
        while (!value.empty() && value.front() == ' ') {
            value.erase(value.begin());
        }

        if (key == "EXPECT-ERRORS") {
            result.errors = std::stoi(value);
        }
        else if (key == "EXPECT-EXTERNALS") {
            result.externals = std::stoi(value);
        }
        else if (key == "EXPECT-MAIN-STATEMENTS") {
            result.main_statements = std::stoi(value);
        }
        else if (key == "EXPECT") {
            result.messages.push_back(std::move(value));
        }
    }
    return result;
}

std::vector<std::filesystem::path> cases(
    std::string_view directory) {
    std::vector<std::filesystem::path> result;
    auto root =
        std::filesystem::path(C9AY_TEST_DIRECTORY) / directory;
    for (auto &entry :
         std::filesystem::recursive_directory_iterator(root)) {
        if (entry.is_regular_file() &&
            entry.path().extension() == ".c") {
            result.push_back(entry.path());
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

struct Compilation {
    std::string source;
    std::optional<Reader> reader;
    std::unique_ptr<parser::Program> program;

    Compilation(
        const std::filesystem::path &path,
        bool run_codegen) {
        auto raw_source = read(path);
        Reader source_reader(path.string(), raw_source);
        preprocessor::Preprocessor preprocessor;
        preprocessor.add_include_path(path.parent_path());
        preprocessor.add_include_path(C9AY_INCLUDE_DIR);
        auto processed = preprocessor.process_mapped(
            source_reader,
            path);
        source = std::move(processed.text);
        reader.emplace(
            path.string(),
            source,
            processed.source_map);
        lexer::LexerMgr manager(*reader);
        auto lexer = manager.get_lexer();
        program = parser::Program::match(lexer);
        if (!reader->diagnostic().has_error()) {
            try {
                if (run_codegen) {
                    codegen::LLVM_codegen codegen(path.string());
                    codegen.generate(
                        *program,
                        &reader->diagnostic());
                }
                else {
                    semantic::Semantic_analyzer analyzer;
                    analyzer.analyze(
                        *program,
                        &reader->diagnostic());
                }
            }
            catch (const semantic::Compile_error &) {}
        }
    }
};

int main_statement_count(const parser::Program &program) {
    for (auto &external : program.external_declarations) {
        auto function =
            dynamic_cast<const parser::Function_definition *>(
                external.get());
        if (function &&
            function->declarator->name.raw == "main") {
            return static_cast<int>(
                function->body->statements.size());
        }
    }
    return -1;
}

}  // namespace fixture_test

TEST_CASE("compile success fixture suite") {
    auto cases = fixture_test::cases("compile_success");
    REQUIRE(cases.size() >= 10);
    for (auto &path : cases) {
        CAPTURE(path.string());
        fixture_test::Compilation compilation(path, true);
        INFO(compilation.reader->diagnostic().format_all());
        CHECK_FALSE(compilation.reader->diagnostic().has_error());
        CHECK_FALSE(compilation.program->error_occur);
    }
}

TEST_CASE("compile failure fixture suite") {
    auto cases = fixture_test::cases("compile_failure");
    REQUIRE(cases.size() >= 20);
    for (auto &path : cases) {
        CAPTURE(path.string());
        auto source = fixture_test::read(path);
        auto expected = fixture_test::expectation(source);
        REQUIRE(expected.errors >= 1);

        fixture_test::Compilation compilation(path, false);
        auto &diagnostic = compilation.reader->diagnostic();
        auto output = diagnostic.format_all();
        INFO(output);
        CHECK(diagnostic.errors() == expected.errors);
        for (auto &message : expected.messages) {
            CHECK(output.find(message) != std::string::npos);
        }
    }
}

TEST_CASE("panic recovery fixture suite") {
    auto cases = fixture_test::cases("panic_recovery");
    REQUIRE(cases.size() >= 6);
    for (auto &path : cases) {
        CAPTURE(path.string());
        auto source = fixture_test::read(path);
        auto expected = fixture_test::expectation(source);
        REQUIRE(expected.errors >= 1);

        fixture_test::Compilation compilation(path, false);
        auto &diagnostic = compilation.reader->diagnostic();
        auto output = diagnostic.format_all();
        INFO(output);
        CHECK(diagnostic.errors() == expected.errors);
        for (auto &message : expected.messages) {
            CHECK(output.find(message) != std::string::npos);
        }
        if (expected.externals >= 0) {
            CHECK(
                static_cast<int>(
                    compilation.program->external_declarations.size()) ==
                expected.externals);
        }
        if (expected.main_statements >= 0) {
            CHECK(
                fixture_test::main_statement_count(
                    *compilation.program) ==
                expected.main_statements);
        }
    }
}
