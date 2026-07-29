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
    case TokenType::DROP:
      return "DROP";
    case TokenType::ALTER:
      return "ALTER";
    case TokenType::ADD:
      return "ADD";
    case TokenType::COLUMN:
      return "COLUMN";
    case TokenType::RENAME:
      return "RENAME";
    case TokenType::TO:
      return "TO";
    case TokenType::CONSTRAINT:
      return "CONSTRAINT";
    case TokenType::PRIMARY:
      return "PRIMARY";
    case TokenType::KEY:
      return "KEY";
    case TokenType::UNIQUE:
      return "UNIQUE";
    case TokenType::BETWEEN:
      return "BETWEEN";
    case TokenType::TABLE:
      return "TABLE";
    case TokenType::VIEW:
      return "VIEW";
    case TokenType::IF:
      return "IF";
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
    case TokenType::FULL:
      return "FULL";
    case TokenType::OUTER:
      return "OUTER";
    case TokenType::CROSS:
      return "CROSS";
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
    case TokenType::TRUE_KW:
      return "TRUE";
    case TokenType::FALSE_KW:
      return "FALSE";
    case TokenType::AS:
      return "AS";
    case TokenType::DISTINCT:
      return "DISTINCT";
    case TokenType::INDEX:
      return "INDEX";
    case TokenType::REFERENCES:
      return "REFERENCES";
    case TokenType::FOREIGN:
      return "FOREIGN";
    case TokenType::CASCADE:
      return "CASCADE";
    case TokenType::RESTRICT:
      return "RESTRICT";
    case TokenType::BEGIN:
      return "BEGIN";
    case TokenType::COMMIT:
      return "COMMIT";
    case TokenType::ROLLBACK:
      return "ROLLBACK";
    case TokenType::PREPARE:
      return "PREPARE";
    case TokenType::EXECUTE:
      return "EXECUTE";
    case TokenType::DEALLOCATE:
      return "DEALLOCATE";
    case TokenType::IN:
      return "IN";
    case TokenType::EXISTS:
      return "EXISTS";
    case TokenType::DEFAULT:
      return "DEFAULT";
    case TokenType::VACUUM:
      return "VACUUM";
    case TokenType::EXPLAIN:
      return "EXPLAIN";
    case TokenType::CHECK:
      return "CHECK";
    case TokenType::UNION:
      return "UNION";
    case TokenType::INTERSECT:
      return "INTERSECT";
    case TokenType::EXCEPT:
      return "EXCEPT";
    case TokenType::ALL:
      return "ALL";
    case TokenType::OVER:
      return "OVER";
    case TokenType::PARTITION:
      return "PARTITION";
    case TokenType::RANGE:
      return "RANGE";
    case TokenType::HASH:
      return "HASH";
    case TokenType::OF:
      return "OF";
    case TokenType::FOR:
      return "FOR";
    case TokenType::WITH:
      return "WITH";
    case TokenType::MODULUS:
      return "MODULUS";
    case TokenType::REMAINDER:
      return "REMAINDER";
    case TokenType::FUNCTION:
      return "FUNCTION";
    case TokenType::RETURNS:
      return "RETURNS";
    case TokenType::RETURN:
      return "RETURN";
    case TokenType::PROCEDURE:
      return "PROCEDURE";
    case TokenType::CALL:
      return "CALL";
    case TokenType::TRIGGER:
      return "TRIGGER";
    case TokenType::BEFORE:
      return "BEFORE";
    case TokenType::AFTER:
      return "AFTER";
    case TokenType::EACH:
      return "EACH";
    case TokenType::ROW:
      return "ROW";
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
    case TokenType::PARAMETER:
      return "PARAMETER";
    case TokenType::IDENTIFIER:
      return "IDENTIFIER";
    case TokenType::NUMBER:
      return "NUMBER";
    case TokenType::STRING:
      return "STRING";
    case TokenType::DOLLAR_QUOTED_STRING:
      return "DOLLAR_QUOTED_STRING";
    case TokenType::END_OF_INPUT:
      return "EOF";
    case TokenType::UNKNOWN:
      return "UNKNOWN";
    default:
      return "UNKNOWN";
  }
}

}  // namespace db
