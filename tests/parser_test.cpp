#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <llvm/Support/Program.h>

#include "codegen/llvm_codegen.hpp"
#include "parser/parser.h"
#include "preprocessor/preprocessor.hpp"
#include "semantic/constant_evaluator.hpp"
#include "semantic/semantic_analyzer.hpp"

using namespace c9ay;

TEST_CASE("diagnostic renders GCC style source ranges") {
    std::string source = "int main() {\n\treturn missing;\n}\n";
    Reader reader("diagnostic.c", source);
    int begin = static_cast<int>(source.find("missing"));
    reader.report_error(
        "use of undeclared identifier 'missing'",
        begin,
        begin + 7);

    auto output = reader.diagnostic().format_all();
    CHECK(output.find(
              "diagnostic.c:2:9: error: "
              "use of undeclared identifier 'missing'") !=
          std::string::npos);
    CHECK(output.find("\treturn missing;") !=
          std::string::npos);
    CHECK(output.find("\t       ^~~~~~~") !=
          std::string::npos);
}

TEST_CASE("reader clones share one diagnostic engine") {
    std::string source = "bad";
    Reader reader("shared.c", source);
    auto clone = reader.clone();
    clone.report_error("first error", 0, 3);

    CHECK(reader.diagnostic().has_error());
    CHECK(reader.diagnostic().errors() == 1);
}

TEST_CASE("operator trie recognizes every C symbolic operator") {
    std::vector<std::string_view> operators = {
        "[", "]", "(", ")", "++", "--", ".", "->",
        "+", "-", "*", "/", "%", "!", "~",
        "<<", ">>", "<", ">", "<=", ">=", "==", "!=",
        "&", "^", "|", "&&", "||",
        "=", "*=", "/=", "%=", "+=", "-=",
        "<<=", ">>=", "&=", "^=", "|=",
        "?", ":", ","
    };

    for (auto op : operators) {
        std::string source(op);
        Reader reader("operator.c", source);
        auto token = lexer::impl::next_token(reader);

        CHECK(token.type == lexer::token_type::OPERATOR);
        CHECK(token.raw == op);
    }
}

TEST_CASE("operator trie uses longest match") {
    std::string source = "<<=x >>=y ->z";
    Reader reader("operator.c", source);

    CHECK(lexer::impl::next_token(reader).raw == "<<=");
    CHECK(lexer::impl::next_token(reader).raw == "x");
    CHECK(lexer::impl::next_token(reader).raw == ">>=");
    CHECK(lexer::impl::next_token(reader).raw == "y");
    CHECK(lexer::impl::next_token(reader).raw == "->");
    CHECK(lexer::impl::next_token(reader).raw == "z");
}

TEST_CASE("structural punctuators stay punctuators") {
    std::string source = "{}#;";
    Reader reader("operator.c", source);

    for (char expected : source) {
        auto token = lexer::impl::next_token(reader);
        CHECK(token.type == lexer::token_type::PUNCTUATOR);
        CHECK(token.raw == std::string_view(&expected, 1));
    }
}

TEST_CASE("sizeof and alignof parse as type query operators") {
    std::string source =
        "sizeof(int) + sizeof(value) + alignof(int)";
    Reader reader("expression.c", source);
    lexer::LexerMgr mgr(reader);
    auto lexer = mgr.get_lexer();

    auto expression = parser::Expression::parse(lexer);
    REQUIRE(expression != nullptr);
    auto binary = dynamic_cast<parser::Binary_expression *>(expression.get());
    REQUIRE(binary != nullptr);
    CHECK(binary->op.raw == "+");
    CHECK(dynamic_cast<parser::Type_query_expression *>(
              binary->rhs.get()) != nullptr);
    auto left = dynamic_cast<parser::Binary_expression *>(
        binary->lhs.get());
    REQUIRE(left != nullptr);
    auto type_size = dynamic_cast<parser::Type_query_expression *>(
        left->lhs.get());
    auto expression_size =
        dynamic_cast<parser::Type_query_expression *>(
            left->rhs.get());
    REQUIRE(type_size != nullptr);
    REQUIRE(expression_size != nullptr);
    CHECK(type_size->type != nullptr);
    CHECK(type_size->operand == nullptr);
    CHECK(expression_size->type == nullptr);
    CHECK(expression_size->operand != nullptr);
}

TEST_CASE("Pratt parser respects C precedence") {
    std::string source = "a = b ? c + d * e : f, g";
    Reader reader("expression.c", source);
    lexer::LexerMgr mgr(reader);
    auto lexer = mgr.get_lexer();

    auto expression = parser::Expression::parse(lexer);
    REQUIRE(expression != nullptr);
    auto comma = dynamic_cast<parser::Binary_expression *>(expression.get());
    REQUIRE(comma != nullptr);
    CHECK(comma->op.raw == ",");

    auto assignment = dynamic_cast<parser::Binary_expression *>(comma->lhs.get());
    REQUIRE(assignment != nullptr);
    CHECK(assignment->op.raw == "=");

    auto conditional = dynamic_cast<parser::Conditional_expression *>(assignment->rhs.get());
    REQUIRE(conditional != nullptr);

    auto addition =
        dynamic_cast<parser::Binary_expression *>(conditional->true_expression.get());
    REQUIRE(addition != nullptr);
    CHECK(addition->op.raw == "+");

    auto multiplication = dynamic_cast<parser::Binary_expression *>(addition->rhs.get());
    REQUIRE(multiplication != nullptr);
    CHECK(multiplication->op.raw == "*");
}

TEST_CASE("expression recovery preserves its synchronization token") {
    std::string source = "x + ;";
    Reader reader("expression.c", source);
    lexer::LexerMgr mgr(reader);
    auto lexer = mgr.get_lexer();

    auto expression = parser::Expression::match(lexer);
    REQUIRE(expression != nullptr);
    CHECK(expression->error_occur);

    REQUIRE(lexer.has_next());
    CHECK(lexer.peek_next().match<lexer::token_type::PUNCTUATOR>(';'));
}

TEST_CASE("expression recovery keeps parsing later statements") {
    std::string source = R"(
        {
            x = 1 + ;
            y = 2;
            if (x > ) y = 3;
            y = 4;
        }
    )";

    Reader reader("recovery.c", source);
    lexer::LexerMgr mgr(reader);
    auto lexer = mgr.get_lexer();
    auto block = parser::Block::match(lexer);

    REQUIRE(block != nullptr);
    REQUIRE(block->statements.size() == 4);
    CHECK(block->error_occur);

    auto first =
        dynamic_cast<parser::Expression_statement *>(block->statements[0].get());
    auto second =
        dynamic_cast<parser::Expression_statement *>(block->statements[1].get());
    auto if_statement =
        dynamic_cast<parser::If_statement *>(block->statements[2].get());
    auto fourth =
        dynamic_cast<parser::Expression_statement *>(block->statements[3].get());

    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);
    REQUIRE(if_statement != nullptr);
    REQUIRE(fourth != nullptr);

    CHECK(first->error_occur);
    CHECK_FALSE(second->error_occur);
    CHECK(if_statement->error_occur);
    CHECK(if_statement->condition->error_occur);
    CHECK_FALSE(fourth->error_occur);
}

TEST_CASE("block parses declaration if while and do while") {
    std::string source = R"(
        {
            int x = 1 + 2 * 3;
            if (x > 3) {
                x = x - 1;
            }
            else
                x = 0;

            while (x < 10)
                x = x + 1;

            do {
                x--;
            } while (x > 0);
        }
    )";

    Reader reader("statement.c", source);
    lexer::LexerMgr mgr(reader);
    auto lexer = mgr.get_lexer();
    auto block = parser::Block::match(lexer);

    REQUIRE(block != nullptr);
    REQUIRE(block->statements.size() == 4);

    auto declaration =
        dynamic_cast<parser::Declvariable *>(block->statements[0].get());
    auto if_statement =
        dynamic_cast<parser::If_statement *>(block->statements[1].get());
    auto while_statement =
        dynamic_cast<parser::While_statement *>(block->statements[2].get());
    auto do_while_statement =
        dynamic_cast<parser::Do_while_statement *>(block->statements[3].get());

    REQUIRE(declaration != nullptr);
    REQUIRE(if_statement != nullptr);
    REQUIRE(while_statement != nullptr);
    REQUIRE(do_while_statement != nullptr);

    REQUIRE(declaration->declarators.size() == 1);
    CHECK(declaration->declarators[0]->initializer != nullptr);
    CHECK(if_statement->condition != nullptr);
    CHECK(if_statement->then_statement != nullptr);
    CHECK(if_statement->else_statement != nullptr);
    CHECK(while_statement->condition != nullptr);
    CHECK(while_statement->body != nullptr);
    CHECK(do_while_statement->condition != nullptr);
    CHECK(do_while_statement->body != nullptr);
}

TEST_CASE("for statement supports declaration expression and empty clauses") {
    std::string source = R"(
        {
            for (int i = 0; i < 10; i++) {
                continue;
            }
            for (i = 0; i < 3; i = i + 1)
                break;
            for (;;)
                return;
        }
    )";

    Reader reader("for.c", source);
    lexer::LexerMgr mgr(reader);
    auto lexer = mgr.get_lexer();
    auto block = parser::Block::match(lexer);

    REQUIRE(block != nullptr);
    REQUIRE(block->statements.size() == 3);

    auto first =
        dynamic_cast<parser::For_statement *>(block->statements[0].get());
    auto second =
        dynamic_cast<parser::For_statement *>(block->statements[1].get());
    auto third =
        dynamic_cast<parser::For_statement *>(block->statements[2].get());

    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);
    REQUIRE(third != nullptr);

    CHECK(dynamic_cast<parser::Declvariable *>(first->initializer.get()) != nullptr);
    CHECK(first->condition != nullptr);
    CHECK(first->iteration != nullptr);
    CHECK(dynamic_cast<parser::Block *>(first->body.get()) != nullptr);

    CHECK(dynamic_cast<parser::Expression_statement *>(second->initializer.get()) != nullptr);
    CHECK(second->condition != nullptr);
    CHECK(second->iteration != nullptr);
    CHECK(dynamic_cast<parser::Break_statement *>(second->body.get()) != nullptr);

    CHECK(third->initializer == nullptr);
    CHECK(third->condition == nullptr);
    CHECK(third->iteration == nullptr);
    CHECK(dynamic_cast<parser::Return_statement *>(third->body.get()) != nullptr);
}

TEST_CASE("return break and continue parse as jump statements") {
    std::string source = R"(
        {
            return value + 1;
            return;
            break;
            continue;
        }
    )";

    Reader reader("jump.c", source);
    lexer::LexerMgr mgr(reader);
    auto lexer = mgr.get_lexer();
    auto block = parser::Block::match(lexer);

    REQUIRE(block != nullptr);
    REQUIRE(block->statements.size() == 4);

    auto return_value =
        dynamic_cast<parser::Return_statement *>(block->statements[0].get());
    auto return_void =
        dynamic_cast<parser::Return_statement *>(block->statements[1].get());

    REQUIRE(return_value != nullptr);
    REQUIRE(return_void != nullptr);
    CHECK(return_value->expression != nullptr);
    CHECK(return_void->expression == nullptr);
    CHECK(dynamic_cast<parser::Break_statement *>(block->statements[2].get()) != nullptr);
    CHECK(dynamic_cast<parser::Continue_statement *>(block->statements[3].get()) != nullptr);
}

TEST_CASE("switch parses case default and fallthrough statements") {
    std::string source = R"(
        {
            switch (value) {
                case 0:
                    value = 1;
                    break;
                case 1:
                case 2:
                    return value;
                default:
                    value = 0;
            }
        }
    )";

    Reader reader("switch.c", source);
    lexer::LexerMgr mgr(reader);
    auto lexer = mgr.get_lexer();
    auto block = parser::Block::match(lexer);

    REQUIRE(block != nullptr);
    REQUIRE(block->statements.size() == 1);

    auto switch_statement =
        dynamic_cast<parser::Switch_statement *>(block->statements[0].get());
    REQUIRE(switch_statement != nullptr);
    REQUIRE(switch_statement->condition != nullptr);

    auto &switch_body = *switch_statement->body;
    REQUIRE(switch_body.sections.size() == 3);

    REQUIRE(switch_body.sections[0]->labels.size() == 1);
    REQUIRE(switch_body.sections[0]->statements.size() == 2);
    CHECK(!switch_body.sections[0]->labels[0]->is_default());
    CHECK(dynamic_cast<parser::Expression_statement *>(
              switch_body.sections[0]->statements[0].get()) != nullptr);
    CHECK(dynamic_cast<parser::Break_statement *>(
              switch_body.sections[0]->statements[1].get()) != nullptr);

    REQUIRE(switch_body.sections[1]->labels.size() == 2);
    REQUIRE(switch_body.sections[1]->statements.size() == 1);
    CHECK(!switch_body.sections[1]->labels[0]->is_default());
    CHECK(!switch_body.sections[1]->labels[1]->is_default());
    CHECK(dynamic_cast<parser::Return_statement *>(
              switch_body.sections[1]->statements[0].get()) != nullptr);

    REQUIRE(switch_body.sections[2]->labels.size() == 1);
    REQUIRE(switch_body.sections[2]->statements.size() == 1);
    CHECK(switch_body.sections[2]->labels[0]->is_default());
    CHECK(dynamic_cast<parser::Expression_statement *>(
              switch_body.sections[2]->statements[0].get()) != nullptr);
}

TEST_CASE("typedef is parsed as a simplified declaration specifier") {
    std::string source = R"(
        {
            typedef int Integer;
            Integer value = 3;
        }
    )";

    Reader reader("typedef.c", source);
    lexer::LexerMgr mgr(reader);
    auto lexer = mgr.get_lexer();
    auto block = parser::Block::match(lexer);

    REQUIRE(block != nullptr);
    REQUIRE(block->statements.size() == 2);

    auto typedef_declaration =
        dynamic_cast<parser::Declvariable *>(block->statements[0].get());
    auto variable_declaration =
        dynamic_cast<parser::Declvariable *>(block->statements[1].get());

    REQUIRE(typedef_declaration != nullptr);
    REQUIRE(variable_declaration != nullptr);
    CHECK(typedef_declaration->is_typedef);
    CHECK(typedef_declaration->type.raw == "int");
    REQUIRE(typedef_declaration->declarators.size() == 1);
    CHECK(typedef_declaration->declarators[0]->declarator->name.raw == "Integer");
    CHECK_FALSE(variable_declaration->is_typedef);
    CHECK(variable_declaration->type.raw == "Integer");
}

TEST_CASE("pointer and array declarators preserve their structure") {
    std::string source = R"(
        {
            int *pointer;
            int **double_pointer;
            int values[10];
            int matrix[3][4];
            int *pointer_table[8];
            int incomplete[];
        }
    )";

    Reader reader("declarator.c", source);
    lexer::LexerMgr mgr(reader);
    auto lexer = mgr.get_lexer();
    auto block = parser::Block::match(lexer);

    REQUIRE(block != nullptr);
    REQUIRE(block->statements.size() == 6);

    auto pointer =
        dynamic_cast<parser::Declvariable *>(block->statements[0].get());
    auto double_pointer =
        dynamic_cast<parser::Declvariable *>(block->statements[1].get());
    auto values =
        dynamic_cast<parser::Declvariable *>(block->statements[2].get());
    auto matrix =
        dynamic_cast<parser::Declvariable *>(block->statements[3].get());
    auto pointer_table =
        dynamic_cast<parser::Declvariable *>(block->statements[4].get());
    auto incomplete =
        dynamic_cast<parser::Declvariable *>(block->statements[5].get());

    REQUIRE(pointer != nullptr);
    REQUIRE(double_pointer != nullptr);
    REQUIRE(values != nullptr);
    REQUIRE(matrix != nullptr);
    REQUIRE(pointer_table != nullptr);
    REQUIRE(incomplete != nullptr);

    auto &pointer_declarator = pointer->declarators[0]->declarator;
    auto &double_pointer_declarator = double_pointer->declarators[0]->declarator;
    auto &values_declarator = values->declarators[0]->declarator;
    auto &matrix_declarator = matrix->declarators[0]->declarator;
    auto &pointer_table_declarator = pointer_table->declarators[0]->declarator;
    auto &incomplete_declarator = incomplete->declarators[0]->declarator;

    CHECK(pointer_declarator->pointer_depth == 1);
    CHECK(pointer_declarator->array_dimensions.empty());
    CHECK(double_pointer_declarator->pointer_depth == 2);

    CHECK(values_declarator->pointer_depth == 0);
    REQUIRE(values_declarator->array_dimensions.size() == 1);
    CHECK(values_declarator->array_dimensions[0] != nullptr);

    REQUIRE(matrix_declarator->array_dimensions.size() == 2);
    CHECK(matrix_declarator->array_dimensions[0] != nullptr);
    CHECK(matrix_declarator->array_dimensions[1] != nullptr);

    CHECK(pointer_table_declarator->pointer_depth == 1);
    REQUIRE(pointer_table_declarator->array_dimensions.size() == 1);

    REQUIRE(incomplete_declarator->array_dimensions.size() == 1);
    CHECK(incomplete_declarator->array_dimensions[0] == nullptr);
}

TEST_CASE("initializer lists support nesting and trailing commas") {
    std::string source = R"(
        {
            int values[3] = {1, 2, 3};
            int matrix[2][2] = {{1, 2}, {3, 4},};
            int scalar = 7;
        }
    )";

    Reader reader("initializer.c", source);
    lexer::LexerMgr mgr(reader);
    auto lexer = mgr.get_lexer();
    auto block = parser::Block::match(lexer);

    REQUIRE(block != nullptr);
    REQUIRE(block->statements.size() == 3);

    auto values =
        dynamic_cast<parser::Declvariable *>(block->statements[0].get());
    auto matrix =
        dynamic_cast<parser::Declvariable *>(block->statements[1].get());
    auto scalar =
        dynamic_cast<parser::Declvariable *>(block->statements[2].get());

    REQUIRE(values != nullptr);
    REQUIRE(matrix != nullptr);
    REQUIRE(scalar != nullptr);

    auto values_list =
        dynamic_cast<parser::Initializer_list *>(
            values->declarators[0]->initializer.get());
    auto matrix_list =
        dynamic_cast<parser::Initializer_list *>(
            matrix->declarators[0]->initializer.get());
    auto scalar_expression =
        dynamic_cast<parser::Expression_initializer *>(
            scalar->declarators[0]->initializer.get());

    REQUIRE(values_list != nullptr);
    REQUIRE(matrix_list != nullptr);
    REQUIRE(scalar_expression != nullptr);
    CHECK(values_list->elements.size() == 3);
    REQUIRE(matrix_list->elements.size() == 2);
    CHECK(dynamic_cast<parser::Initializer_list *>(
              matrix_list->elements[0].get()) != nullptr);
    CHECK(dynamic_cast<parser::Initializer_list *>(
              matrix_list->elements[1].get()) != nullptr);
    CHECK(scalar_expression->expression != nullptr);
}

TEST_CASE("one declaration contains independent C declarators") {
    std::string source = R"(
        {
            int *p, a, b;
            int *left = 1, values[3] = {1, 2, 3}, **right;
        }
    )";

    Reader reader("multi-declarator.c", source);
    lexer::LexerMgr mgr(reader);
    auto lexer = mgr.get_lexer();
    auto block = parser::Block::match(lexer);

    REQUIRE(block != nullptr);
    REQUIRE(block->statements.size() == 2);

    auto first =
        dynamic_cast<parser::Declvariable *>(block->statements[0].get());
    auto second =
        dynamic_cast<parser::Declvariable *>(block->statements[1].get());

    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);
    REQUIRE(first->declarators.size() == 3);
    REQUIRE(second->declarators.size() == 3);

    CHECK(first->declarators[0]->declarator->name.raw == "p");
    CHECK(first->declarators[0]->declarator->pointer_depth == 1);
    CHECK(first->declarators[1]->declarator->name.raw == "a");
    CHECK(first->declarators[1]->declarator->pointer_depth == 0);
    CHECK(first->declarators[2]->declarator->name.raw == "b");
    CHECK(first->declarators[2]->declarator->pointer_depth == 0);

    CHECK(second->declarators[0]->declarator->name.raw == "left");
    CHECK(second->declarators[0]->declarator->pointer_depth == 1);
    CHECK(second->declarators[0]->initializer != nullptr);

    CHECK(second->declarators[1]->declarator->name.raw == "values");
    CHECK(second->declarators[1]->declarator->pointer_depth == 0);
    REQUIRE(second->declarators[1]->declarator->array_dimensions.size() == 1);
    CHECK(dynamic_cast<parser::Initializer_list *>(
              second->declarators[1]->initializer.get()) != nullptr);

    CHECK(second->declarators[2]->declarator->name.raw == "right");
    CHECK(second->declarators[2]->declarator->pointer_depth == 2);
    CHECK(second->declarators[2]->initializer == nullptr);
}

TEST_CASE("function declarations reuse the declarator parser") {
    std::string source = R"(
        {
            int declared(int value, int *buffer);
        }
    )";

    Reader reader("function-declaration.c", source);
    lexer::LexerMgr mgr(reader);
    auto lexer = mgr.get_lexer();
    auto block = parser::Block::match(lexer);

    REQUIRE(block != nullptr);
    REQUIRE(block->statements.size() == 1);

    auto declaration =
        dynamic_cast<parser::Declvariable *>(block->statements[0].get());
    REQUIRE(declaration != nullptr);
    REQUIRE(declaration->declarators.size() == 1);

    auto &function = declaration->declarators[0]->declarator;
    CHECK(function->name.raw == "declared");
    CHECK(function->is_function);
    REQUIRE(function->parameters.size() == 2);
    REQUIRE(function->parameters[0]->declarator != nullptr);
    REQUIRE(function->parameters[1]->declarator != nullptr);
    CHECK(function->parameters[0]->declarator->name.raw == "value");
    CHECK(function->parameters[0]->declarator->pointer_depth == 0);
    CHECK(function->parameters[1]->declarator->name.raw == "buffer");
    CHECK(function->parameters[1]->declarator->pointer_depth == 1);
}

TEST_CASE("program parses function definitions and global declarations") {
    std::string source = R"(
        int global_value;
        int declared(int value);

        int add(int left, int right) {
            return left + right;
        }

        static int *identity(int *value) {
            int *local = value;
            return local;
        }
    )";

    Reader reader("program.c", source);
    lexer::LexerMgr mgr(reader);
    auto lexer = mgr.get_lexer();
    auto program = parser::Program::match(lexer);

    REQUIRE(program != nullptr);
    REQUIRE(program->external_declarations.size() == 4);

    auto global =
        dynamic_cast<parser::Declvariable *>(
            program->external_declarations[0].get());
    auto prototype =
        dynamic_cast<parser::Declvariable *>(
            program->external_declarations[1].get());
    auto add =
        dynamic_cast<parser::Function_definition *>(
            program->external_declarations[2].get());
    auto identity =
        dynamic_cast<parser::Function_definition *>(
            program->external_declarations[3].get());

    REQUIRE(global != nullptr);
    REQUIRE(prototype != nullptr);
    REQUIRE(add != nullptr);
    REQUIRE(identity != nullptr);

    CHECK(prototype->declarators[0]->declarator->is_function);

    CHECK(add->specifiers.type.raw == "int");
    CHECK(add->declarator->name.raw == "add");
    CHECK(add->declarator->is_function);
    CHECK(add->declarator->parameters.size() == 2);
    REQUIRE(add->body != nullptr);
    CHECK(add->body->statements.size() == 1);
    CHECK(dynamic_cast<parser::Return_statement *>(
              add->body->statements[0].get()) != nullptr);

    CHECK(identity->specifiers.is_static);
    CHECK(identity->declarator->name.raw == "identity");
    CHECK(identity->declarator->pointer_depth == 1);
    REQUIRE(identity->body != nullptr);
    CHECK(identity->body->statements.size() == 2);
}

TEST_CASE("cast expression keeps its abstract declarator") {
    std::string source = "(const int **)value";
    Reader reader("cast.c", source);
    lexer::LexerMgr mgr(reader);
    auto lexer = mgr.get_lexer();

    auto expression = parser::Expression::parse(lexer);
    auto cast = dynamic_cast<parser::Cast_expression *>(expression.get());

    REQUIRE(cast != nullptr);
    REQUIRE(cast->type != nullptr);
    CHECK(cast->type->type.raw == "int");
    CHECK(cast->type->is_const);
    REQUIRE(cast->type->declarator != nullptr);
    CHECK(cast->type->declarator->pointer_depth == 2);
}

TEST_CASE("abstract declarator supports pointer and array suffixes") {
    std::string source = "int *[3][]";
    Reader reader("type-name.c", source);
    lexer::LexerMgr mgr(reader);
    auto lexer = mgr.get_lexer();

    auto type = parser::Type_name::try_match(lexer);

    REQUIRE(type != nullptr);
    REQUIRE(type->declarator != nullptr);
    CHECK(type->declarator->pointer_depth == 1);
    REQUIRE(type->declarator->array_dimensions.size() == 2);
    CHECK(type->declarator->array_dimensions[0] != nullptr);
    CHECK(type->declarator->array_dimensions[1] == nullptr);
}

TEST_CASE("preprocessor expands macros and selects conditional branches") {
    std::string source = R"(
        #define VALUE 3
        #define ADD(left, right) ((left) + (right))
        #define REMOVED 1
        #undef REMOVED
        #if defined(VALUE) && VALUE == 3
        int value = ADD(VALUE, 2);
        #else
        int wrong;
        #endif
        #ifdef REMOVED
        int also_wrong;
        #endif
    )";

    preprocessor::Preprocessor preprocessor;
    std::string result = preprocessor.process(source, "macro.c");

    CHECK(result.find("int value = ((3) + (2));") != std::string::npos);
    CHECK(result.find("int wrong;") == std::string::npos);
    CHECK(result.find("int also_wrong;") == std::string::npos);
}

TEST_CASE("preprocessor expansion follows scanner token boundaries") {
    std::string source = R"(
        #define NAME value
        char *text = "NAME";
        int NAME = 1; // NAME
    )";

    preprocessor::Preprocessor preprocessor;
    std::string result = preprocessor.process(source, "scanner.c");

    CHECK(result.find("\"NAME\"") != std::string::npos);
    CHECK(result.find("int value = 1;") != std::string::npos);
    CHECK(result.find("// NAME") == std::string::npos);
}

TEST_CASE("preprocessor handles undef and include guards") {
    preprocessor::Preprocessor preprocessor;
    auto fixture =
        std::filesystem::path(C9AY_TEST_DIRECTORY) / "fixtures";
    preprocessor.add_include_path(fixture);

    std::string source = R"(
        #include "preprocessor_value.h"
        #include "preprocessor_value.h"
        int value = INCLUDED_VALUE;
    )";
    std::string result = preprocessor.process(
        source,
        fixture / "translation-unit.c");

    CHECK(result.find("int included_declaration;") != std::string::npos);
    CHECK(result.find(
              "int included_declaration;",
              result.find("int included_declaration;") + 1) ==
          std::string::npos);
    CHECK(result.find("int value = 9;") != std::string::npos);
}

TEST_CASE("source map preserves locations across includes") {
    auto fixture =
        std::filesystem::path(C9AY_TEST_DIRECTORY) / "fixtures";
    auto main_file = fixture / "mapped-main.c";
    std::string source = R"(#include "preprocessor_value.h"
int main() {
    return missing;
}
)";

    Reader source_reader(main_file.string(), source);
    preprocessor::Preprocessor preprocessor;
    preprocessor.add_include_path(fixture);
    auto processed = preprocessor.process_mapped(
        source_reader,
        main_file);
    Reader reader(
        main_file.string(),
        processed.text,
        processed.source_map);
    lexer::LexerMgr mgr(reader);
    auto lexer = mgr.get_lexer();
    auto program = parser::Program::match(lexer);

    REQUIRE(program != nullptr);
    REQUIRE_FALSE(reader.diagnostic().has_error());

    semantic::Semantic_analyzer analyzer;
    CHECK_THROWS_AS(
        analyzer.analyze(*program, &reader.diagnostic()),
        semantic::Compile_error);
    auto output = reader.diagnostic().format_all();
    INFO(output);
    CHECK(output.find("mapped-main.c:3:12: error:") !=
          std::string::npos);
    CHECK(output.find("return missing;") != std::string::npos);
}

TEST_CASE("source map reports errors inside included files") {
    auto fixture =
        std::filesystem::path(C9AY_TEST_DIRECTORY) / "fixtures";
    auto main_file = fixture / "include-error-main.c";
    std::string source = R"(#include "diagnostic_invalid.h"
int main() {
    return 0;
}
)";

    Reader source_reader(main_file.string(), source);
    preprocessor::Preprocessor preprocessor;
    preprocessor.add_include_path(fixture);
    auto processed = preprocessor.process_mapped(
        source_reader,
        main_file);
    Reader reader(
        main_file.string(),
        processed.text,
        processed.source_map);
    lexer::LexerMgr mgr(reader);
    auto lexer = mgr.get_lexer();
    parser::Program::match(lexer);

    REQUIRE(reader.diagnostic().has_error());
    auto output = reader.diagnostic().format_all();
    CHECK(output.find("In file included from") != std::string::npos);
    CHECK(output.find("include-error-main.c:1:1") !=
          std::string::npos);
    CHECK(output.find("diagnostic_invalid.h:1:14: error:") !=
          std::string::npos);
    CHECK(output.find("int broken = ;") != std::string::npos);
}

TEST_CASE("preprocessor supports stringify token paste and variadic macros") {
    std::string source = R"(
        #define STRINGIFY(value) #value
        #define CONCAT(left, right) left ## right
        #define CALL(function, ...) function(__VA_ARGS__)

        char *name = STRINGIFY(hello world);
        int CONCAT(my, Value) = 3;
        int result = CALL(add, 1, 2);
    )";

    preprocessor::Preprocessor preprocessor;
    std::string result = preprocessor.process(source, "macro-extra.c");

    CHECK(result.find("\"hello world\"") != std::string::npos);
    CHECK(result.find("int myValue = 3;") != std::string::npos);
    CHECK(result.find("int result = add(1, 2);") != std::string::npos);
}

TEST_CASE("AST constant evaluator follows expression precedence") {
    std::string source = "1 + 2 * 3 == 7 ? 40 + 2 : unknown";
    Reader reader("constant.c", source);
    lexer::LexerMgr mgr(reader);
    auto lexer = mgr.get_lexer();

    auto expression = parser::Expression::parse(lexer);
    REQUIRE(expression != nullptr);

    auto value = semantic::Constant_evaluator::evaluate(*expression);
    REQUIRE(value.has_value());
    CHECK(value->value == 42);
}

TEST_CASE("AST constant evaluator rejects runtime expressions") {
    std::string source = "value + 1";
    Reader reader("constant.c", source);
    lexer::LexerMgr mgr(reader);
    auto lexer = mgr.get_lexer();

    auto expression = parser::Expression::parse(lexer);
    REQUIRE(expression != nullptr);
    CHECK_FALSE(
        semantic::Constant_evaluator::evaluate(*expression).has_value());
}

TEST_CASE("AST constant evaluator short circuits and handles characters") {
    std::string source = "0 && unknown || '\\n' == 10";
    Reader reader("constant.c", source);
    lexer::LexerMgr mgr(reader);
    auto lexer = mgr.get_lexer();

    auto expression = parser::Expression::parse(lexer);
    REQUIRE(expression != nullptr);

    auto value = semantic::Constant_evaluator::evaluate(*expression);
    REQUIRE(value.has_value());
    CHECK(value->value == 1);
}

TEST_CASE("preprocessor constant expression uses shared integer operations") {
    auto value =
        preprocessor::Constant_expression("1 + 2 * 3 == 7 && !0")
            .evaluate();

    REQUIRE(value.has_value());
    CHECK(value->value == 1);
    CHECK_FALSE(
        preprocessor::Constant_expression("1 / 0")
            .evaluate()
            .has_value());
}

TEST_CASE("sizeof and alignof are semantic integer constants") {
    std::string source = R"(
        struct Pair {
            char tag;
            int value;
        };

        int integers[sizeof(int) * 2];
        char record_alignment[alignof(Pair)];
        char record_size[sizeof(Pair)];

        int main() {
            int value = 1;
            return sizeof(value++) +
                   sizeof(int *) +
                   sizeof(int[3]) +
                   alignof(Pair) +
                   sizeof(Pair) +
                   value;
        }
    )";

    Reader reader("type-query.c", source);
    lexer::LexerMgr mgr(reader);
    auto lexer = mgr.get_lexer();
    auto program = parser::Program::match(lexer);

    REQUIRE(program != nullptr);
    REQUIRE_FALSE(program->error_occur);

    semantic::Semantic_analyzer analyzer;
    CHECK_NOTHROW(analyzer.analyze(*program));

    codegen::LLVM_codegen codegen("type_query");
    CHECK_NOTHROW(codegen.generate(*program));
    auto ir = codegen.ir();
    CHECK(ir.find("@integers = global [8 x i32]") !=
          std::string::npos);
    CHECK(ir.find("@record_alignment = global [4 x i8]") !=
          std::string::npos);
    CHECK(ir.find("@record_size = global [8 x i8]") !=
          std::string::npos);
}

TEST_CASE("LLVM codegen emits verified IR for basic C") {
    std::string source = R"(
        int global = 2;

        int add(int left, int right) {
            int result = left + right;
            return result;
        }

        int main() {
            int value = add(20, 22);
            if (value == 42) {
                global += value;
            }

            while (global > 40) {
                global--;
                if (global == 42)
                    break;
            }
            return global;
        }
    )";

    Reader reader("codegen.c", source);
    lexer::LexerMgr mgr(reader);
    auto lexer = mgr.get_lexer();
    auto program = parser::Program::match(lexer);

    REQUIRE(program != nullptr);
    REQUIRE_FALSE(program->error_occur);

    codegen::LLVM_codegen codegen("codegen_test");
    codegen.generate(*program);
    std::string ir = codegen.ir();

    CHECK(ir.find("@global = global i32 2") != std::string::npos);
    CHECK(ir.find("define i32 @add") != std::string::npos);
    CHECK(ir.find("define i32 @main") != std::string::npos);
    CHECK(ir.find("call i32 @add") != std::string::npos);
    CHECK(ir.find("while.condition") != std::string::npos);
    CHECK(ir.find("ret i32") != std::string::npos);
}

TEST_CASE("LLVM optimization pipeline simplifies generated IR") {
    std::string source = R"(
        int calculate() {
            int left = 20;
            int right = 22;
            int result = left + right;
            return result;
        }
    )";

    Reader reader("optimization.c", source);
    lexer::LexerMgr mgr(reader);
    auto lexer = mgr.get_lexer();
    auto program = parser::Program::match(lexer);

    REQUIRE(program != nullptr);
    REQUIRE_FALSE(program->error_occur);

    codegen::LLVM_codegen unoptimized(
        "unoptimized",
        codegen::Optimization_level::O0);
    unoptimized.generate(*program);
    auto unoptimized_ir = unoptimized.ir();

    codegen::LLVM_codegen optimized(
        "optimized",
        codegen::Optimization_level::O2);
    optimized.generate(*program);
    auto optimized_ir = optimized.ir();

    CHECK(unoptimized_ir.find("alloca") != std::string::npos);
    CHECK(optimized_ir.find("alloca") == std::string::npos);
    CHECK(optimized_ir.find("ret i32 42") != std::string::npos);
}

TEST_CASE("LLVM codegen uses target width for pointer differences") {
    std::string source = R"(
        long long distance(int *begin, int *end) {
            return end - begin;
        }
    )";

    Reader reader("ptrdiff.c", source);
    lexer::LexerMgr mgr(reader);
    auto lexer = mgr.get_lexer();
    auto program = parser::Program::match(lexer);

    REQUIRE(program != nullptr);
    REQUIRE_FALSE(program->error_occur);

    codegen::LLVM_codegen codegen("ptrdiff");
    codegen.generate(*program);
    auto ir = codegen.ir();
    CHECK(ir.find("define i64 @distance") != std::string::npos);
    CHECK(ir.find("ret i64") != std::string::npos);
}

TEST_CASE("LLVM codegen lowers floating arithmetic and conversions") {
    std::string source = R"(
        double calculate(float left, double right) {
            return left * 2.0 + right;
        }

        int main() {
            return (int)calculate(10.0f, 22.0);
        }
    )";

    Reader reader("floating.c", source);
    lexer::LexerMgr mgr(reader);
    auto lexer = mgr.get_lexer();
    auto program = parser::Program::match(lexer);

    REQUIRE(program != nullptr);
    REQUIRE_FALSE(program->error_occur);

    codegen::LLVM_codegen codegen("floating");
    CHECK_NOTHROW(codegen.generate(*program));
    auto ir = codegen.ir();
    CHECK(ir.find("fpext float") != std::string::npos);
    CHECK(ir.find("fmul double") != std::string::npos);
    CHECK(ir.find("fadd double") != std::string::npos);
    CHECK(ir.find("fptosi double") != std::string::npos);
}

TEST_CASE("semantic analysis rejects unknown types before codegen") {
    std::string source = R"(
        Unknown global;

        int main() {
            return 0;
        }
    )";

    Reader reader("unknown-type.c", source);
    lexer::LexerMgr mgr(reader);
    auto lexer = mgr.get_lexer();
    auto program = parser::Program::match(lexer);

    REQUIRE(program != nullptr);

    semantic::Semantic_analyzer analyzer;
    CHECK_THROWS_WITH_AS(
        analyzer.analyze(*program),
        "compile error: unknown type 'Unknown'",
        semantic::Compile_error);
}

TEST_CASE("semantic analysis recognizes typedef names") {
    std::string source = R"(
        typedef int Integer;
        Integer global;

        int main() {
            typedef Integer LocalInteger;
            LocalInteger value = 42;
            return value;
        }
    )";

    Reader reader("typedef-semantic.c", source);
    lexer::LexerMgr mgr(reader);
    auto lexer = mgr.get_lexer();
    auto program = parser::Program::match(lexer);

    REQUIRE(program != nullptr);

    semantic::Semantic_analyzer analyzer;
    CHECK_NOTHROW(analyzer.analyze(*program));
}

TEST_CASE("struct definitions provide named record types") {
    std::string source = R"(
        struct Pair {
            int first;
            int second;
        };

        Pair global = {1, 2};

        int sum(Pair *pair) {
            return pair->first + pair->second;
        }

        int main() {
            Pair local = {20, 22};
            local.first += global.first;
            return sum(&local);
        }
    )";

    Reader reader("struct.c", source);
    lexer::LexerMgr mgr(reader);
    auto lexer = mgr.get_lexer();
    auto program = parser::Program::match(lexer);

    REQUIRE(program != nullptr);
    REQUIRE_FALSE(program->error_occur);
    REQUIRE(program->external_declarations.size() == 4);

    auto definition =
        dynamic_cast<parser::Struct_definition *>(
            program->external_declarations[0].get());
    REQUIRE(definition != nullptr);
    CHECK(definition->name.raw == "Pair");
    REQUIRE(definition->fields.size() == 2);

    semantic::Semantic_analyzer analyzer;
    CHECK_NOTHROW(analyzer.analyze(*program));

    codegen::LLVM_codegen codegen("struct_codegen");
    CHECK_NOTHROW(codegen.generate(*program));
    auto ir = codegen.ir();
    CHECK(ir.find("%Pair = type { i32, i32 }") != std::string::npos);
    CHECK(ir.find("@global = global %Pair") != std::string::npos);
    CHECK(ir.find("getelementptr") != std::string::npos);
}

TEST_CASE("semantic analysis validates struct members") {
    std::string source = R"(
        struct Pair {
            int value;
        };

        int main() {
            Pair pair;
            return pair.missing;
        }
    )";

    Reader reader("invalid-struct-member.c", source);
    lexer::LexerMgr mgr(reader);
    auto lexer = mgr.get_lexer();
    auto program = parser::Program::match(lexer);

    REQUIRE(program != nullptr);
    REQUIRE_FALSE(program->error_occur);

    semantic::Semantic_analyzer analyzer;
    CHECK_THROWS_WITH_AS(
        analyzer.analyze(*program),
        "compile error: struct 'Pair' has no member 'missing'",
        semantic::Compile_error);
}

TEST_CASE("local struct definitions stay in block scope") {
    std::string source = R"(
        int main() {
            struct Local {
                int value;
            };
            Local local = {42};
            return local.value;
        }
    )";

    Reader reader("local-struct.c", source);
    lexer::LexerMgr mgr(reader);
    auto lexer = mgr.get_lexer();
    auto program = parser::Program::match(lexer);

    REQUIRE(program != nullptr);
    REQUIRE_FALSE(program->error_occur);

    codegen::LLVM_codegen codegen("local_struct");
    CHECK_NOTHROW(codegen.generate(*program));
    CHECK(codegen.ir().find("%Local = type { i32 }") !=
          std::string::npos);
}

TEST_CASE("LLVM codegen supports pointers arrays and initializer lists") {
    std::string source = R"(
        int values[4] = {10, 20, 30, 40};

        int main() {
            int local[3] = {1, 2, 3};
            int *pointer = &local[0];
            pointer = pointer + 1;
            *pointer += values[2];
            return local[1];
        }
    )";

    Reader reader("pointer-array.c", source);
    lexer::LexerMgr mgr(reader);
    auto lexer = mgr.get_lexer();
    auto program = parser::Program::match(lexer);

    REQUIRE(program != nullptr);
    REQUIRE_FALSE(program->error_occur);

    codegen::LLVM_codegen codegen("pointer_array");
    CHECK_NOTHROW(codegen.generate(*program));
    std::string ir = codegen.ir();

    CHECK(ir.find("@values = global [4 x i32]") != std::string::npos);
    CHECK(ir.find("getelementptr") != std::string::npos);
    CHECK(ir.find("alloca [3 x i32]") != std::string::npos);
}

TEST_CASE("LLVM codegen supports switch fallthrough and break") {
    std::string source = R"(
        int classify(int value) {
            int result = 0;
            switch (value) {
                case 1:
                    result = 10;
                    break;
                case 2:
                case 3:
                    result = 20;
                    break;
                default:
                    result = 30;
            }
            return result;
        }
    )";

    Reader reader("switch-codegen.c", source);
    lexer::LexerMgr mgr(reader);
    auto lexer = mgr.get_lexer();
    auto program = parser::Program::match(lexer);

    REQUIRE(program != nullptr);
    REQUIRE_FALSE(program->error_occur);

    codegen::LLVM_codegen codegen("switch_codegen");
    CHECK_NOTHROW(codegen.generate(*program));
    std::string ir = codegen.ir();

    CHECK(ir.find("switch i32") != std::string::npos);
    CHECK(ir.find("i32 1") != std::string::npos);
    CHECK(ir.find("i32 2") != std::string::npos);
    CHECK(ir.find("i32 3") != std::string::npos);
}

TEST_CASE("semantic analysis rejects undeclared identifiers and invalid control flow") {
    SUBCASE("undeclared identifier") {
        std::string source = "int main() { return missing; }";
        Reader reader("semantic.c", source);
        lexer::LexerMgr mgr(reader);
        auto lexer = mgr.get_lexer();
        auto program = parser::Program::match(lexer);

        semantic::Semantic_analyzer analyzer;
        CHECK_THROWS_AS(
            analyzer.analyze(*program),
            semantic::Compile_error);
    }

    SUBCASE("break outside loop") {
        std::string source = "int main() { break; return 0; }";
        Reader reader("semantic.c", source);
        lexer::LexerMgr mgr(reader);
        auto lexer = mgr.get_lexer();
        auto program = parser::Program::match(lexer);

        semantic::Semantic_analyzer analyzer;
        CHECK_THROWS_AS(
            analyzer.analyze(*program),
            semantic::Compile_error);
    }
}

TEST_CASE("nested declarators distinguish arrays from pointers to arrays") {
    std::string source = R"(
        int *array_of_pointers[4];
        int (*pointer_to_array)[4];
        int (*function_pointer)(int value);
    )";

    Reader reader("nested-declarator.c", source);
    lexer::LexerMgr mgr(reader);
    auto lexer = mgr.get_lexer();
    auto program = parser::Program::match(lexer);

    REQUIRE(program != nullptr);
    REQUIRE(program->external_declarations.size() == 3);

    semantic::Semantic_analyzer analyzer;
    auto result = analyzer.analyze(*program);

    auto array_declaration =
        dynamic_cast<parser::Declvariable *>(
            program->external_declarations[0].get());
    auto pointer_declaration =
        dynamic_cast<parser::Declvariable *>(
            program->external_declarations[1].get());
    auto function_pointer_declaration =
        dynamic_cast<parser::Declvariable *>(
            program->external_declarations[2].get());

    REQUIRE(array_declaration != nullptr);
    REQUIRE(pointer_declaration != nullptr);
    REQUIRE(function_pointer_declaration != nullptr);

    auto array_type = result.symbol(
        *array_declaration->declarators[0]->declarator)->type;
    auto pointer_type = result.symbol(
        *pointer_declaration->declarators[0]->declarator)->type;
    auto function_pointer_type = result.symbol(
        *function_pointer_declaration->declarators[0]->declarator)->type;

    auto array =
        semantic::as_type<semantic::Array_type>(array_type);
    auto pointer =
        semantic::as_type<semantic::Pointer_type>(pointer_type);
    auto function_pointer =
        semantic::as_type<semantic::Pointer_type>(
            function_pointer_type);

    REQUIRE(array != nullptr);
    CHECK(semantic::as_type<semantic::Pointer_type>(
              array->element) != nullptr);
    REQUIRE(pointer != nullptr);
    CHECK(semantic::as_type<semantic::Array_type>(
              pointer->element) != nullptr);
    REQUIRE(function_pointer != nullptr);
    CHECK(semantic::as_type<semantic::Function_type>(
              function_pointer->element) != nullptr);
}

TEST_CASE("semantic array dimensions preserve C declarator order") {
    std::string source = "int matrix[3][4];";
    Reader reader("matrix.c", source);
    lexer::LexerMgr mgr(reader);
    auto lexer = mgr.get_lexer();
    auto program = parser::Program::match(lexer);

    semantic::Semantic_analyzer analyzer;
    auto result = analyzer.analyze(*program);
    auto declaration =
        dynamic_cast<parser::Declvariable *>(
            program->external_declarations[0].get());

    REQUIRE(declaration != nullptr);
    auto type = result.symbol(
        *declaration->declarators[0]->declarator)->type;
    auto outer = semantic::as_type<semantic::Array_type>(type);
    REQUIRE(outer != nullptr);
    REQUIRE(outer->size.has_value());
    CHECK(*outer->size == 3);
    auto inner =
        semantic::as_type<semantic::Array_type>(outer->element);
    REQUIRE(inner != nullptr);
    REQUIRE(inner->size.has_value());
    CHECK(*inner->size == 4);
}

TEST_CASE("typedef reuses the canonical semantic type object") {
    std::string source = R"(
        typedef int Integer;
        Integer value;
    )";

    Reader reader("typedef-identity.c", source);
    lexer::LexerMgr mgr(reader);
    auto lexer = mgr.get_lexer();
    auto program = parser::Program::match(lexer);

    REQUIRE(program != nullptr);
    REQUIRE_FALSE(program->error_occur);

    semantic::Semantic_analyzer analyzer;
    auto result = analyzer.analyze(*program);
    auto alias_declaration =
        dynamic_cast<parser::Declvariable *>(
            program->external_declarations[0].get());
    auto value_declaration =
        dynamic_cast<parser::Declvariable *>(
            program->external_declarations[1].get());

    REQUIRE(alias_declaration != nullptr);
    REQUIRE(value_declaration != nullptr);
    auto alias = result.symbol(
        *alias_declaration->declarators[0]->declarator);
    auto value = result.symbol(
        *value_declaration->declarators[0]->declarator);
    REQUIRE(alias != nullptr);
    REQUIRE(value != nullptr);
    CHECK(alias->type == result.integer_type);
    CHECK(value->type == alias->type);
}

TEST_CASE("record aliases preserve record type identity") {
    auto integer = std::make_shared<semantic::Primitive_type>(
        "int",
        semantic::Primitive_type::Category::INTEGER,
        32,
        true);
    auto record =
        std::make_shared<semantic::Record_type>("Pair");
    record->fields.push_back({"first", integer});
    record->fields.push_back({"second", integer});
    record->defined = true;

    semantic::Type_ptr alias = record;
    REQUIRE(alias == record);
    CHECK(alias->is_complete());
    CHECK(semantic::same_type(alias, record));
}

TEST_CASE("function pointer declarator is not a function definition") {
    std::string source = R"(
        int (*function)(int value) {
            return value;
        }
    )";

    Reader reader("function-pointer-definition.c", source);
    lexer::LexerMgr mgr(reader);
    auto lexer = mgr.get_lexer();
    auto program = parser::Program::match(lexer);

    REQUIRE(program != nullptr);
    CHECK(program->error_occur);
    for (auto &external : program->external_declarations) {
        CHECK(dynamic_cast<parser::Function_definition *>(
                  external.get()) == nullptr);
    }
}

TEST_CASE("LLVM codegen supports an indirect function pointer call") {
    std::string source = R"(
        int add_one(int value) {
            return value + 1;
        }

        int main() {
            int (*function)(int value) = add_one;
            return function(41);
        }
    )";

    Reader reader("function-pointer.c", source);
    lexer::LexerMgr mgr(reader);
    auto lexer = mgr.get_lexer();
    auto program = parser::Program::match(lexer);

    REQUIRE(program != nullptr);
    REQUIRE_FALSE(program->error_occur);

    codegen::LLVM_codegen codegen("function_pointer");
    CHECK_NOTHROW(codegen.generate(*program));
    CHECK(codegen.ir().find("call i32 %") != std::string::npos);
}

TEST_CASE("semantic analysis infers array size from initializer list") {
    std::string source = R"(
        int values[] = {1, 2, 3};
    )";

    Reader reader("inferred-array.c", source);
    lexer::LexerMgr mgr(reader);
    auto lexer = mgr.get_lexer();
    auto program = parser::Program::match(lexer);

    semantic::Semantic_analyzer analyzer;
    auto result = analyzer.analyze(*program);
    auto declaration =
        dynamic_cast<parser::Declvariable *>(
            program->external_declarations[0].get());

    REQUIRE(declaration != nullptr);
    auto type = result.symbol(
        *declaration->declarators[0]->declarator)->type;
    auto array = semantic::as_type<semantic::Array_type>(type);
    REQUIRE(array != nullptr);
    REQUIRE(array->size.has_value());
    CHECK(*array->size == 3);
}

TEST_CASE("semantic analysis rejects duplicate switch labels") {
    std::string source = R"(
        int main() {
            switch (1) {
                case 2:
                    break;
                case 2:
                    break;
            }
            return 0;
        }
    )";

    Reader reader("duplicate-case.c", source);
    lexer::LexerMgr mgr(reader);
    auto lexer = mgr.get_lexer();
    auto program = parser::Program::match(lexer);

    semantic::Semantic_analyzer analyzer;
    CHECK_THROWS_WITH_AS(
        analyzer.analyze(*program),
        "compile error: duplicate case value",
        semantic::Compile_error);
}

TEST_CASE("semantic analysis requires a prototype for forward calls") {
    SUBCASE("missing prototype") {
        std::string source = R"(
            int first() {
                return second();
            }

            int second() {
                return 42;
            }
        )";

        Reader reader("forward-call.c", source);
        lexer::LexerMgr mgr(reader);
        auto lexer = mgr.get_lexer();
        auto program = parser::Program::match(lexer);

        semantic::Semantic_analyzer analyzer;
        CHECK_THROWS_AS(
            analyzer.analyze(*program),
            semantic::Compile_error);
    }

    SUBCASE("prototype makes the call visible") {
        std::string source = R"(
            int second();

            int first() {
                return second();
            }

            int second() {
                return 42;
            }
        )";

        Reader reader("forward-call.c", source);
        lexer::LexerMgr mgr(reader);
        auto lexer = mgr.get_lexer();
        auto program = parser::Program::match(lexer);

        semantic::Semantic_analyzer analyzer;
        CHECK_NOTHROW(analyzer.analyze(*program));
    }
}

TEST_CASE("LLVM codegen converts chars pointers and global function pointers") {
    std::string source = R"(
        int add_one(int value) {
            return value + 1;
        }

        int (*selected)(int value) = add_one;

        int main() {
            char value = 41;
            int values[] = {10, 20};
            int *pointer = 0;
            pointer = &values[0];
            pointer++;
            return selected(value) + *pointer;
        }
    )";

    Reader reader("codegen-conversion.c", source);
    lexer::LexerMgr mgr(reader);
    auto lexer = mgr.get_lexer();
    auto program = parser::Program::match(lexer);

    codegen::LLVM_codegen codegen("codegen_conversion");
    CHECK_NOTHROW(codegen.generate(*program));
    std::string ir = codegen.ir();

    CHECK(ir.find("@selected = global ptr @add_one") != std::string::npos);
    CHECK(ir.find("alloca i8") != std::string::npos);
    CHECK(ir.find("alloca [2 x i32]") != std::string::npos);
}

TEST_CASE("string literals initialize character arrays and pointers") {
    std::string source = R"(
        char global_text[] = "A\n";
        char *global_pointer = "hello";

        int main() {
            char local[3] = "xy";
            return global_text[1] + local[0] + global_pointer[0];
        }
    )";

    Reader reader("string-initializer.c", source);
    lexer::LexerMgr mgr(reader);
    auto lexer = mgr.get_lexer();
    auto program = parser::Program::match(lexer);

    codegen::LLVM_codegen codegen("string_initializer");
    CHECK_NOTHROW(codegen.generate(*program));
    std::string ir = codegen.ir();

    CHECK(ir.find("@global_text = global [3 x i8]") !=
          std::string::npos);
    CHECK(ir.find("@global_pointer = global ptr") !=
          std::string::npos);
    CHECK(ir.find("alloca [3 x i8]") != std::string::npos);
}

TEST_CASE("preprocessor accepts an empty macro argument") {
    preprocessor::Preprocessor preprocessor;
    std::string result = preprocessor.process(
        "#define STRINGIFY(value) #value\n"
        "char *value = STRINGIFY();\n",
        "empty-argument.c");

    CHECK(result.find("char *value = \"\";") != std::string::npos);
}

TEST_CASE("preprocessor treats unknown condition identifiers as zero") {
    preprocessor::Preprocessor preprocessor;
    std::string result = preprocessor.process(
        "#if UNKNOWN + 1\n"
        "int selected;\n"
        "#else\n"
        "int wrong;\n"
        "#endif\n",
        "unknown-condition.c");

    CHECK(result.find("int selected;") != std::string::npos);
    CHECK(result.find("int wrong;") == std::string::npos);
}

TEST_CASE("LLVM codegen handles static functions and void expressions") {
    std::string source = R"(
        static int hidden() {
            return 1;
        }

        void left() {
            return;
        }

        void right() {
            return;
        }

        int main() {
            (void)hidden();
            1 ? (void)left() : (void)right();
            return 0;
        }
    )";

    Reader reader("void-expression.c", source);
    lexer::LexerMgr mgr(reader);
    auto lexer = mgr.get_lexer();
    auto program = parser::Program::match(lexer);

    codegen::LLVM_codegen codegen("void_expression");
    CHECK_NOTHROW(codegen.generate(*program));
    std::string ir = codegen.ir();

    CHECK(ir.find("define internal i32 @hidden") !=
          std::string::npos);
    CHECK(ir.find("conditional.true") != std::string::npos);
    CHECK(ir.find("phi void") == std::string::npos);
}

TEST_CASE("LLVM backend emits object and executable files") {
    std::string source = "int main() { return 0; }";
    Reader reader("emit.c", source);
    lexer::LexerMgr mgr(reader);
    auto lexer = mgr.get_lexer();
    auto program = parser::Program::match(lexer);

    auto directory = std::filesystem::temp_directory_path();
    auto object = directory / "c9ay-compiler-test.o";
    auto executable = directory / "c9ay-compiler-test.exe";
    std::filesystem::remove(object);
    std::filesystem::remove(executable);

    codegen::LLVM_codegen codegen("emit");
    codegen.generate(*program);

    CHECK_NOTHROW(codegen.emit_object(object));
    CHECK(std::filesystem::exists(object));
    CHECK(std::filesystem::file_size(object) != 0);

    CHECK_NOTHROW(codegen.emit_executable(executable));
    CHECK(std::filesystem::exists(executable));
    CHECK(std::filesystem::file_size(executable) != 0);

    std::filesystem::remove(object);
    std::filesystem::remove(executable);
}

TEST_CASE("LLVM codegen handles multidimensional arrays and scalar braces") {
    std::string source = R"(
        int matrix[2][3] = {
            {1, 2, 3},
            {4, 5, 6}
        };

        int main() {
            int value = {matrix[1][2]};
            int empty = {};
            return value + empty;
        }
    )";

    Reader reader("multidimensional.c", source);
    lexer::LexerMgr mgr(reader);
    auto lexer = mgr.get_lexer();
    auto program = parser::Program::match(lexer);

    codegen::LLVM_codegen codegen("multidimensional");
    CHECK_NOTHROW(codegen.generate(*program));
    std::string ir = codegen.ir();

    CHECK(ir.find("@matrix = global [2 x [3 x i32]]") !=
          std::string::npos);
    CHECK(ir.find("getelementptr") != std::string::npos);
}

TEST_CASE("null pointer constants work in calls and conditional expressions") {
    std::string source = R"(
        int read_or_zero(int *value) {
            return value ? *value : 0;
        }

        int main() {
            int value = 42;
            int *selected = 1 ? &value : 0;
            return read_or_zero(0) + *selected;
        }
    )";

    Reader reader("null-pointer.c", source);
    lexer::LexerMgr mgr(reader);
    auto lexer = mgr.get_lexer();
    auto program = parser::Program::match(lexer);

    codegen::LLVM_codegen codegen("null_pointer");
    CHECK_NOTHROW(codegen.generate(*program));
    CHECK(codegen.ir().find("ptr null") != std::string::npos);
}

TEST_CASE("compatible repeated function prototypes share one symbol") {
    std::string source = R"(
        int value(int input);
        int value(int input);

        int value(int input) {
            return input;
        }
    )";

    Reader reader("repeated-prototype.c", source);
    lexer::LexerMgr mgr(reader);
    auto lexer = mgr.get_lexer();
    auto program = parser::Program::match(lexer);

    semantic::Semantic_analyzer analyzer;
    CHECK_NOTHROW(analyzer.analyze(*program));

    codegen::LLVM_codegen codegen("repeated_prototype");
    CHECK_NOTHROW(codegen.generate(*program));
}

TEST_CASE("c9ay runtime prints observable console output") {
    std::string source = R"(
        #include <c9ay.h>

        int main() {
            print("text:");
            printInt(42);
            writeChar(' ');
            printInt(-7);
            writeChar(' ');
            printInt(-2147483647 - 1);
            printLine("");
            return 0;
        }
    )";

    preprocessor::Preprocessor preprocessor;
    preprocessor.add_include_path(C9AY_INCLUDE_DIR);
    std::string processed = preprocessor.process(
        source,
        "runtime-output.c");

    Reader reader("runtime-output.c", processed);
    lexer::LexerMgr mgr(reader);
    auto lexer = mgr.get_lexer();
    auto program = parser::Program::match(lexer);

    auto directory = std::filesystem::temp_directory_path();
    auto executable = directory / "c9ay-runtime-output.exe";
    auto output = directory / "c9ay-runtime-output.txt";
    auto error = directory / "c9ay-runtime-error.txt";
    std::filesystem::remove(executable);
    std::filesystem::remove(output);
    std::filesystem::remove(error);

    codegen::LLVM_codegen codegen("runtime_output");
    codegen.generate(*program);
    codegen.emit_executable(executable);

    std::string program_path = executable.string();
    std::string output_path = output.string();
    std::string error_path = error.string();
    std::vector<llvm::StringRef> arguments = {
        program_path
    };
    std::vector<std::optional<llvm::StringRef>> redirects = {
        std::nullopt,
        output_path,
        error_path
    };
    int result = llvm::sys::ExecuteAndWait(
        program_path,
        arguments,
        std::nullopt,
        redirects);

    CHECK(result == 0);
    std::ifstream output_file(output, std::ios::binary);
    std::string text{
        std::istreambuf_iterator<char>(output_file),
        std::istreambuf_iterator<char>()};
    CHECK(text == "text:42 -7 -2147483648\n");
    output_file.close();

    std::filesystem::remove(executable);
    std::filesystem::remove(output);
    std::filesystem::remove(error);
}
