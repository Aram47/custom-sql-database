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

TEST(LexerTest, TypeAndDialectKeywords) {
  Lexer lexer(
      "FLOAT BOOLEAN DATE UUID VARCHAR INTEGER REAL TEXT BOOL INNER JOIN");
  auto tokens = lexer.get_all_tokens();
  ASSERT_GE(tokens.size(), 12u);
  EXPECT_EQ(tokens[0].get_type(), TokenType::IDENTIFIER);
  EXPECT_EQ(tokens[0].get_lexeme(), "FLOAT");
  EXPECT_EQ(tokens[1].get_lexeme(), "BOOLEAN");
  EXPECT_EQ(tokens[2].get_lexeme(), "DATE");
  EXPECT_EQ(tokens[3].get_lexeme(), "UUID");
  EXPECT_EQ(tokens[4].get_lexeme(), "VARCHAR");
  EXPECT_EQ(tokens[5].get_lexeme(), "INTEGER");
  EXPECT_EQ(tokens[6].get_lexeme(), "REAL");
  EXPECT_EQ(tokens[7].get_lexeme(), "TEXT");
  EXPECT_EQ(tokens[8].get_lexeme(), "BOOL");
  EXPECT_EQ(tokens[9].get_type(), TokenType::INNER);
  EXPECT_EQ(tokens[10].get_type(), TokenType::JOIN);
  EXPECT_EQ(tokens[11].get_type(), TokenType::END_OF_INPUT);
}

TEST(LexerTest, FullOuterCrossKeywords) {
  Lexer lexer("FULL OUTER CROSS");
  auto tokens = lexer.get_all_tokens();
  ASSERT_GE(tokens.size(), 4u);
  EXPECT_EQ(tokens[0].get_type(), TokenType::FULL);
  EXPECT_EQ(tokens[1].get_type(), TokenType::OUTER);
  EXPECT_EQ(tokens[2].get_type(), TokenType::CROSS);
  EXPECT_EQ(tokens[3].get_type(), TokenType::END_OF_INPUT);
}

TEST(LexerTest, DdlAndBetweenKeywords) {
  Lexer lexer("DROP ALTER ADD COLUMN RENAME TO PRIMARY KEY UNIQUE BETWEEN");
  auto tokens = lexer.get_all_tokens();
  ASSERT_GE(tokens.size(), 11u);
  EXPECT_EQ(tokens[0].get_type(), TokenType::DROP);
  EXPECT_EQ(tokens[1].get_type(), TokenType::ALTER);
  EXPECT_EQ(tokens[2].get_type(), TokenType::ADD);
  EXPECT_EQ(tokens[3].get_type(), TokenType::COLUMN);
  EXPECT_EQ(tokens[4].get_type(), TokenType::RENAME);
  EXPECT_EQ(tokens[5].get_type(), TokenType::TO);
  EXPECT_EQ(tokens[6].get_type(), TokenType::PRIMARY);
  EXPECT_EQ(tokens[7].get_type(), TokenType::KEY);
  EXPECT_EQ(tokens[8].get_type(), TokenType::UNIQUE);
  EXPECT_EQ(tokens[9].get_type(), TokenType::BETWEEN);
  EXPECT_EQ(tokens[10].get_type(), TokenType::END_OF_INPUT);
}

TEST(LexerTest, ExplainKeyword) {
  Lexer lexer("EXPLAIN SELECT");
  auto tokens = lexer.get_all_tokens();
  ASSERT_GE(tokens.size(), 3u);
  EXPECT_EQ(tokens[0].get_type(), TokenType::EXPLAIN);
  EXPECT_EQ(tokens[1].get_type(), TokenType::SELECT);
  EXPECT_EQ(tokens[2].get_type(), TokenType::END_OF_INPUT);
}

}  // namespace
}  // namespace db
