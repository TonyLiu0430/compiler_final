#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <string>
#include <string_view>
#include <vector>

#include "codegen/llvm_codegen.hpp"
#include "parser/parser.h"
#include "preprocessor/preprocessor.hpp"
#include "semantic/constant_evaluator.hpp"
#include "semantic/semantic_analyzer.hpp"

using namespace c9ay;

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

TEST_CASE("sizeof and alignof stay identifier calls") {
    std::string source = "sizeof(x) + alignof(y)";
    Reader reader("expression.c", source);
    lexer::LexerMgr mgr(reader);
    auto lexer = mgr.get_lexer();

    auto expression = parser::Expression::parse(lexer);
    REQUIRE(expression != nullptr);
    auto binary = dynamic_cast<parser::Binary_expression *>(expression.get());
    REQUIRE(binary != nullptr);
    CHECK(binary->op.raw == "+");
    CHECK(dynamic_cast<parser::Call_expression *>(binary->lhs.get()) != nullptr);
    CHECK(dynamic_cast<parser::Call_expression *>(binary->rhs.get()) != nullptr);
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

    auto switch_body =
        dynamic_cast<parser::Block *>(switch_statement->body.get());
    REQUIRE(switch_body != nullptr);
    REQUIRE(switch_body->statements.size() == 4);

    auto case_zero =
        dynamic_cast<parser::Case_statement *>(switch_body->statements[0].get());
    auto case_one =
        dynamic_cast<parser::Case_statement *>(switch_body->statements[2].get());
    auto default_statement =
        dynamic_cast<parser::Default_statement *>(switch_body->statements[3].get());

    REQUIRE(case_zero != nullptr);
    REQUIRE(case_one != nullptr);
    REQUIRE(default_statement != nullptr);
    CHECK(dynamic_cast<parser::Expression_statement *>(case_zero->statement.get()) != nullptr);
    CHECK(dynamic_cast<parser::Case_statement *>(case_one->statement.get()) != nullptr);
    CHECK(dynamic_cast<parser::Expression_statement *>(default_statement->statement.get()) != nullptr);
    CHECK(dynamic_cast<parser::Break_statement *>(switch_body->statements[1].get()) != nullptr);
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
        std::filesystem::path(__FILE__).parent_path() / "fixtures";
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
