#include "parser/lexer.h"

#include "gtest/gtest.h"
#include "parser/token.h"

namespace db {
namespace {

TEST(LexerTest, KeywordsAndIdentifiers) {
  Lexer lexer("SELECT id FROM users");
  auto tokens = lexer.get_all_tokens();
  ASSERT_GE(tokens.size(), 5u);
  EXPECT_EQ(tokens[0].get_type(), TokenType::SELECT);
  EXPECT_EQ(tokens[1].get_type(), TokenType::IDENTIFIER);
  EXPECT_EQ(tokens[1].get_lexeme(), "id");
  EXPECT_EQ(tokens[2].get_type(), TokenType::FROM);
  EXPECT_EQ(tokens[3].get_type(), TokenType::IDENTIFIER);
  EXPECT_EQ(tokens[3].get_lexeme(), "users");
  EXPECT_EQ(tokens[4].get_type(), TokenType::END_OF_INPUT);
}

TEST(LexerTest, NumbersStringsOperators) {
  Lexer lexer("WHERE x = 42 AND y <> 3.5 AND msg = 'hi'");
  auto tokens = lexer.get_all_tokens();
  ASSERT_GE(tokens.size(), 13u);
  EXPECT_EQ(tokens[0].get_type(), TokenType::WHERE);
  EXPECT_EQ(tokens[1].get_type(), TokenType::IDENTIFIER);
  EXPECT_EQ(tokens[2].get_type(), TokenType::EQUAL);
  EXPECT_EQ(tokens[3].get_type(), TokenType::NUMBER);
  EXPECT_EQ(tokens[3].get_lexeme(), "42");
  EXPECT_EQ(tokens[4].get_type(), TokenType::AND);
  EXPECT_EQ(tokens[5].get_type(), TokenType::IDENTIFIER);
  EXPECT_EQ(tokens[6].get_type(), TokenType::NOT_EQUAL);
  EXPECT_EQ(tokens[7].get_type(), TokenType::NUMBER);
  EXPECT_EQ(tokens[8].get_type(), TokenType::AND);
  EXPECT_EQ(tokens[11].get_type(), TokenType::STRING);
  EXPECT_EQ(tokens[11].get_lexeme(), "hi");
}

TEST(LexerTest, LineCommentSkipped) {
  Lexer lexer("SELECT a -- comment\nFROM t");
  auto tokens = lexer.get_all_tokens();
  ASSERT_GE(tokens.size(), 4u);
  EXPECT_EQ(tokens[0].get_type(), TokenType::SELECT);
  EXPECT_EQ(tokens[1].get_lexeme(), "a");
  EXPECT_EQ(tokens[2].get_type(), TokenType::FROM);
  EXPECT_EQ(tokens[3].get_lexeme(), "t");
}

}  // namespace
}  // namespace db
