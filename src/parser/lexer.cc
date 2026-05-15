#include "parser/lexer.h"

#include <algorithm>
#include <cctype>

namespace db {

Lexer::Lexer(const std::string &input)
    : input_(input),
      pos_(0),
      line_(1),
      column_(1),
      token_index_(0),
      tokenized_(false) {}

Token Lexer::next_token() {
  if (!tokenized_) {
    tokenize();
    tokenized_ = true;
  }

  if (token_index_ < tokens_.size()) {
    return tokens_[token_index_++];
  }
  return Token(TokenType::END_OF_INPUT, "", line_, column_);
}

Token Lexer::peek_token(int offset) {
  if (!tokenized_) {
    tokenize();
    tokenized_ = true;
  }

  if (token_index_ + offset < tokens_.size()) {
    return tokens_[token_index_ + offset];
  }
  return Token(TokenType::END_OF_INPUT, "", line_, column_);
}

bool Lexer::has_more_tokens() const {
  if (!tokenized_) return !input_.empty();
  return token_index_ < tokens_.size();
}

std::vector<Token> Lexer::get_all_tokens() {
  if (!tokenized_) {
    tokenize();
    tokenized_ = true;
  }
  return tokens_;
}

void Lexer::tokenize() {
  tokens_.clear();

  while (!is_at_end()) {
    skip_whitespace();

    if (is_at_end()) break;

    // Check for comments
    if (current_char() == '-' && peek_char(1) == '-') {
      skip_comment();
      continue;
    }

    Token token = scan_token();
    if (token.get_type() != TokenType::UNKNOWN || !token.get_lexeme().empty()) {
      tokens_.push_back(token);
    }
  }

  tokens_.push_back(Token(TokenType::END_OF_INPUT, "", line_, column_));
}

Token Lexer::scan_token() {
  char c = current_char();
  advance();

  // String literals
  if (c == '\'' || c == '"') {
    return scan_string(c);
  }

  // Numbers
  if (is_digit(c)) {
    pos_--;  // Back up to reprocess the digit
    return scan_number();
  }

  // Identifiers and keywords
  if (is_alpha(c) || c == '_') {
    pos_--;  // Back up
    return scan_identifier();
  }

  // Operators and delimiters
  switch (c) {
    case '=':
      return make_token(TokenType::EQUAL, "=");
    case '<':
      if (current_char() == '=') {
        advance();
        return make_token(TokenType::LESS_EQUAL, "<=");
      } else if (current_char() == '>') {
        advance();
        return make_token(TokenType::NOT_EQUAL, "<>");
      }
      return make_token(TokenType::LESS, "<");
    case '>':
      if (current_char() == '=') {
        advance();
        return make_token(TokenType::GREATER_EQUAL, ">=");
      }
      return make_token(TokenType::GREATER, ">");
    case '!':
      if (current_char() == '=') {
        advance();
        return make_token(TokenType::NOT_EQUAL, "!=");
      }
      break;
    case '+':
      return make_token(TokenType::PLUS, "+");
    case '-':
      return make_token(TokenType::MINUS, "-");
    case '*':
      return make_token(TokenType::ASTERISK, "*");
    case '/':
      return make_token(TokenType::DIVIDE, "/");
    case '%':
      return make_token(TokenType::MODULO, "%");
    case '(':
      return make_token(TokenType::LPAREN, "(");
    case ')':
      return make_token(TokenType::RPAREN, ")");
    case ',':
      return make_token(TokenType::COMMA, ",");
    case '.':
      return make_token(TokenType::DOT, ".");
    case ';':
      return make_token(TokenType::SEMICOLON, ";");
  }

  return Token(TokenType::UNKNOWN, std::string(1, c), line_, column_);
}

Token Lexer::scan_string(char quote) {
  std::string value;
  int start_line = line_;
  int start_col = column_;

  while (!is_at_end() && current_char() != quote) {
    if (current_char() == '\\') {
      advance();
      if (!is_at_end()) {
        switch (current_char()) {
          case 'n':
            value += '\n';
            break;
          case 't':
            value += '\t';
            break;
          case 'r':
            value += '\r';
            break;
          case '\\':
            value += '\\';
            break;
          case '"':
            value += '"';
            break;
          case '\'':
            value += '\'';
            break;
          default:
            value += current_char();
        }
        advance();
      }
    } else {
      if (current_char() == '\n') {
        line_++;
        column_ = 0;
      }
      value += current_char();
      advance();
    }
  }

  if (!is_at_end() && current_char() == quote) {
    advance();  // Consume closing quote
  }

  return Token(TokenType::STRING, value, start_line, start_col);
}

Token Lexer::scan_number() {
  std::string number;
  int start_col = column_;

  while (!is_at_end() && is_digit(current_char())) {
    number += current_char();
    advance();
  }

  // Check for decimal point
  if (!is_at_end() && current_char() == '.' && is_digit(peek_char(1))) {
    number += current_char();
    advance();
    while (!is_at_end() && is_digit(current_char())) {
      number += current_char();
      advance();
    }
  }

  // Check for scientific notation
  if (!is_at_end() && (current_char() == 'e' || current_char() == 'E')) {
    number += current_char();
    advance();
    if (!is_at_end() && (current_char() == '+' || current_char() == '-')) {
      number += current_char();
      advance();
    }
    while (!is_at_end() && is_digit(current_char())) {
      number += current_char();
      advance();
    }
  }

  return Token(TokenType::NUMBER, number, line_, start_col);
}

Token Lexer::scan_identifier() {
  std::string identifier;
  int start_col = column_;

  while (!is_at_end() && is_alpha_numeric(current_char())) {
    identifier += current_char();
    advance();
  }

  TokenType type = get_keyword_token_type(identifier);
  return Token(type, identifier, line_, start_col);
}

Token Lexer::make_token(TokenType type, const std::string &lexeme) {
  return Token(type, lexeme, line_,
               column_ - static_cast<int>(lexeme.length()));
}

char Lexer::current_char() const {
  if (is_at_end()) return '\0';
  return input_[pos_];
}

char Lexer::peek_char(size_t offset) const {
  if (pos_ + offset >= input_.length()) return '\0';
  return input_[pos_ + offset];
}

void Lexer::advance() {
  if (!is_at_end()) {
    if (input_[pos_] == '\n') {
      line_++;
      column_ = 1;
    } else {
      column_++;
    }
    pos_++;
  }
}

void Lexer::skip_whitespace() {
  while (!is_at_end() && std::isspace(current_char())) {
    advance();
  }
}

void Lexer::skip_comment() {
  while (!is_at_end() && current_char() != '\n') {
    advance();
  }
}

bool Lexer::is_at_end() const { return pos_ >= input_.length(); }

bool Lexer::is_digit(char c) const { return c >= '0' && c <= '9'; }

bool Lexer::is_alpha(char c) const {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

bool Lexer::is_alpha_numeric(char c) const {
  return is_alpha(c) || is_digit(c);
}

TokenType Lexer::get_keyword_token_type(const std::string &keyword) const {
  std::string upper = keyword;
  std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);

  if (upper == "SELECT") return TokenType::SELECT;
  if (upper == "INSERT") return TokenType::INSERT;
  if (upper == "UPDATE") return TokenType::UPDATE;
  if (upper == "DELETE") return TokenType::DELETE;
  if (upper == "CREATE") return TokenType::CREATE;
  if (upper == "TABLE") return TokenType::TABLE;
  if (upper == "FROM") return TokenType::FROM;
  if (upper == "WHERE") return TokenType::WHERE;
  if (upper == "AND") return TokenType::AND;
  if (upper == "OR") return TokenType::OR;
  if (upper == "NOT") return TokenType::NOT;
  if (upper == "JOIN") return TokenType::JOIN;
  if (upper == "INNER") return TokenType::INNER;
  if (upper == "LEFT") return TokenType::LEFT;
  if (upper == "RIGHT") return TokenType::RIGHT;
  if (upper == "ON") return TokenType::ON;
  if (upper == "GROUP") return TokenType::GROUP;
  if (upper == "BY") return TokenType::BY;
  if (upper == "HAVING") return TokenType::HAVING;
  if (upper == "ORDER") return TokenType::ORDER;
  if (upper == "ASC") return TokenType::ASC;
  if (upper == "DESC") return TokenType::DESC;
  if (upper == "VALUES") return TokenType::VALUES;
  if (upper == "SET") return TokenType::SET;
  if (upper == "NULL") return TokenType::NULL_KW;
  if (upper == "AS") return TokenType::AS;
  if (upper == "DISTINCT") return TokenType::DISTINCT;
  if (upper == "COUNT") return TokenType::COUNT;
  if (upper == "SUM") return TokenType::SUM;
  if (upper == "AVG") return TokenType::AVG;
  if (upper == "MIN") return TokenType::MIN;
  if (upper == "MAX") return TokenType::MAX;

  return TokenType::IDENTIFIER;
}

}  // namespace db
