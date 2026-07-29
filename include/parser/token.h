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
  DROP,
  ALTER,
  ADD,
  COLUMN,
  RENAME,
  TO,
  CONSTRAINT,
  PRIMARY,
  KEY,
  UNIQUE,
  BETWEEN,
  TABLE,
  VIEW,
  IF,
  FROM,
  WHERE,
  AND,
  OR,
  NOT,
  JOIN,
  INNER,
  LEFT,
  RIGHT,
  FULL,
  OUTER,
  CROSS,
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
  TRUE_KW,
  FALSE_KW,
  AS,
  DISTINCT,
  INDEX,
  REFERENCES,
  FOREIGN,
  CASCADE,
  RESTRICT,
  BEGIN,
  COMMIT,
  ROLLBACK,
  PREPARE,
  EXECUTE,
  DEALLOCATE,
  IN,
  EXISTS,
  DEFAULT,
  VACUUM,
  EXPLAIN,
  CHECK,
  UNION,
  INTERSECT,
  EXCEPT,
  ALL,
  OVER,
  PARTITION,
  RANGE,
  HASH,
  OF,
  FOR,
  WITH,
  MODULUS,
  REMAINDER,
  FUNCTION,
  RETURNS,
  RETURN,
  PROCEDURE,
  CALL,
  TRIGGER,
  BEFORE,
  AFTER,
  EACH,
  ROW,

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
  PARAMETER,

  // Literals
  IDENTIFIER,
  NUMBER,
  STRING,
  DOLLAR_QUOTED_STRING,

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
