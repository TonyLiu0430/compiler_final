#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <string>
#include <string_view>
#include <vector>

#include "parser/parser.h"

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

    CHECK(declaration->initializer != nullptr);
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
