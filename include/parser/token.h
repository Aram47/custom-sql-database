#pragma once

#include <string>
#include <vector>

namespace db {

enum class TokenType {
  // Keywords
  SELECT,
  INSERT,
  UPDATE,
  DELETE,
  CREATE,
  TABLE,
  FROM,
  WHERE,
  AND,
  OR,
  NOT,
  JOIN,
  INNER,
  LEFT,
  RIGHT,
  ON,
  GROUP,
  BY,
  HAVING,
  ORDER,
  ASC,
  DESC,
  VALUES,
  SET,
  NULL_KW,
  AS,
  DISTINCT,

  // Aggregate functions
  COUNT,
  SUM,
  AVG,
  MIN,
  MAX,

  // Operators
  EQUAL,
  NOT_EQUAL,
  LESS,
  LESS_EQUAL,
  GREATER,
  GREATER_EQUAL,
  PLUS,
  MINUS,
  MULTIPLY,
  DIVIDE,
  MODULO,

  // Delimiters
  LPAREN,
  RPAREN,
  COMMA,
  DOT,
  SEMICOLON,
  ASTERISK,

  // Literals
  IDENTIFIER,
  NUMBER,
  STRING,

  // End of input
  END_OF_INPUT,

  // Unknown
  UNKNOWN
};

class Token {
 public:
  Token(TokenType type, const std::string &lexeme, int line = 1,
        int column = 1);

  TokenType get_type() const;
  const std::string &get_lexeme() const;
  int get_line() const;
  int get_column() const;

  std::string to_string() const;

  static std::string token_type_to_string(TokenType type);

 private:
  TokenType type_;
  std::string lexeme_;
  int line_;
  int column_;
};

}  // namespace db
