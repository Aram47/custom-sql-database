#include "parser/token.h"

namespace db {

Token::Token(TokenType type, const std::string &lexeme, int line, int column)
    : type_(type), lexeme_(lexeme), line_(line), column_(column) {}

TokenType Token::get_type() const { return type_; }

const std::string &Token::get_lexeme() const { return lexeme_; }

int Token::get_line() const { return line_; }

int Token::get_column() const { return column_; }

std::string Token::to_string() const {
  return token_type_to_string(type_) + " '" + lexeme_ + "'";
}

std::string Token::token_type_to_string(TokenType type) {
  switch (type) {
    case TokenType::SELECT:
      return "SELECT";
    case TokenType::INSERT:
      return "INSERT";
    case TokenType::UPDATE:
      return "UPDATE";
    case TokenType::DELETE:
      return "DELETE";
    case TokenType::CREATE:
      return "CREATE";
    case TokenType::TABLE:
      return "TABLE";
    case TokenType::FROM:
      return "FROM";
    case TokenType::WHERE:
      return "WHERE";
    case TokenType::AND:
      return "AND";
    case TokenType::OR:
      return "OR";
    case TokenType::NOT:
      return "NOT";
    case TokenType::JOIN:
      return "JOIN";
    case TokenType::INNER:
      return "INNER";
    case TokenType::LEFT:
      return "LEFT";
    case TokenType::RIGHT:
      return "RIGHT";
    case TokenType::ON:
      return "ON";
    case TokenType::GROUP:
      return "GROUP";
    case TokenType::BY:
      return "BY";
    case TokenType::HAVING:
      return "HAVING";
    case TokenType::ORDER:
      return "ORDER";
    case TokenType::ASC:
      return "ASC";
    case TokenType::DESC:
      return "DESC";
    case TokenType::VALUES:
      return "VALUES";
    case TokenType::SET:
      return "SET";
    case TokenType::NULL_KW:
      return "NULL";
    case TokenType::AS:
      return "AS";
    case TokenType::DISTINCT:
      return "DISTINCT";
    case TokenType::COUNT:
      return "COUNT";
    case TokenType::SUM:
      return "SUM";
    case TokenType::AVG:
      return "AVG";
    case TokenType::MIN:
      return "MIN";
    case TokenType::MAX:
      return "MAX";
    case TokenType::EQUAL:
      return "=";
    case TokenType::NOT_EQUAL:
      return "!=";
    case TokenType::LESS:
      return "<";
    case TokenType::LESS_EQUAL:
      return "<=";
    case TokenType::GREATER:
      return ">";
    case TokenType::GREATER_EQUAL:
      return ">=";
    case TokenType::PLUS:
      return "+";
    case TokenType::MINUS:
      return "-";
    case TokenType::MULTIPLY:
      return "*";
    case TokenType::DIVIDE:
      return "/";
    case TokenType::MODULO:
      return "%";
    case TokenType::LPAREN:
      return "(";
    case TokenType::RPAREN:
      return ")";
    case TokenType::COMMA:
      return ",";
    case TokenType::DOT:
      return ".";
    case TokenType::SEMICOLON:
      return ";";
    case TokenType::ASTERISK:
      return "*";
    case TokenType::IDENTIFIER:
      return "IDENTIFIER";
    case TokenType::NUMBER:
      return "NUMBER";
    case TokenType::STRING:
      return "STRING";
    case TokenType::END_OF_INPUT:
      return "EOF";
    case TokenType::UNKNOWN:
      return "UNKNOWN";
    default:
      return "UNKNOWN";
  }
}

}  // namespace db
