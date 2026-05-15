#pragma once

#include <memory>
#include <string>
#include <vector>

#include "parser/token.h"
#include "utils/exceptions.h"

namespace db {

class Lexer {
 public:
  explicit Lexer(const std::string &input);

  Token next_token();
  Token peek_token(int offset = 0);
  bool has_more_tokens() const;

  std::vector<Token> get_all_tokens();

 private:
  std::string input_;
  size_t pos_;
  int line_;
  int column_;
  std::vector<Token> tokens_;
  size_t token_index_;
  bool tokenized_;

  void tokenize();
  Token scan_token();
  Token scan_string(char quote);
  Token scan_number();
  Token scan_identifier();
  Token make_token(TokenType type, const std::string &lexeme);

  char current_char() const;
  char peek_char(size_t offset = 1) const;
  void advance();
  void skip_whitespace();
  void skip_comment();

  bool is_at_end() const;
  bool is_digit(char c) const;
  bool is_alpha(char c) const;
  bool is_alpha_numeric(char c) const;

  TokenType get_keyword_token_type(const std::string &keyword) const;
};

}  // namespace db
