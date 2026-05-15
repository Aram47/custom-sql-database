#include "parser/parser.h"

#include <algorithm>
#include <stdexcept>

namespace db {

Parser::Parser(const std::string &sql) : current_(0) {
  Lexer lexer(sql);
  tokens_ = lexer.get_all_tokens();
}

Parser::Parser(std::vector<Token> token_list)
    : tokens_(std::move(token_list)), current_(0) {}

Token Parser::peek() const {
  if (is_at_end()) return Token(TokenType::END_OF_INPUT, "", 0, 0);
  return tokens_[current_];
}

Token Parser::peek_ahead(size_t offset) const {
  if (current_ + offset >= tokens_.size())
    return Token(TokenType::END_OF_INPUT, "", 0, 0);
  return tokens_[current_ + offset];
}

Token Parser::advance() {
  if (!is_at_end()) current_++;
  return tokens_[current_ - 1];
}

bool Parser::check(TokenType type) const {
  if (is_at_end()) return false;
  return peek().get_type() == type;
}

bool Parser::match(TokenType type) {
  if (check(type)) {
    advance();
    return true;
  }
  return false;
}

bool Parser::match(const std::vector<TokenType> &types) {
  for (TokenType type : types) {
    if (check(type)) {
      advance();
      return true;
    }
  }
  return false;
}

Token Parser::consume(TokenType type, const std::string &message) {
  if (check(type)) return advance();
  throw ParseException(get_error_message(message));
}

bool Parser::is_at_end() const {
  if (current_ >= tokens_.size()) return true;
  return tokens_[current_].get_type() == TokenType::END_OF_INPUT;
}

std::variant<std::shared_ptr<SelectStatement>, std::shared_ptr<InsertStatement>,
             std::shared_ptr<UpdateStatement>, std::shared_ptr<DeleteStatement>,
             std::shared_ptr<CreateTableStatement>>
Parser::parse_statement() {
  if (check(TokenType::SELECT)) {
    return parse_select_statement();
  } else if (check(TokenType::INSERT)) {
    return parse_insert_statement();
  } else if (check(TokenType::UPDATE)) {
    return parse_update_statement();
  } else if (check(TokenType::DELETE)) {
    return parse_delete_statement();
  } else if (check(TokenType::CREATE)) {
    return parse_create_table_statement();
  }
  throw ParseException("Unknown statement");
}

std::shared_ptr<SelectStatement> Parser::parse_select_statement() {
  consume(TokenType::SELECT, "Expected SELECT");
  auto stmt = std::make_shared<SelectStatement>();

  // Check for DISTINCT
  if (match(TokenType::DISTINCT)) {
    stmt->set_distinct(true);
  }

  // Parse select columns
  do {
    auto expr = parse_expression();
    std::string alias;
    if (match(TokenType::AS)) {
      alias =
          consume(TokenType::IDENTIFIER, "Expected identifier").get_lexeme();
    }
    stmt->add_select_column(expr, alias);
  } while (match(TokenType::COMMA));

  // Parse FROM clause
  if (match(TokenType::FROM)) {
    std::string table =
        consume(TokenType::IDENTIFIER, "Expected table name").get_lexeme();
    std::string alias = table;
    if (match(TokenType::AS)) {
      alias = consume(TokenType::IDENTIFIER, "Expected alias").get_lexeme();
    }
    stmt->set_from_table(table, alias);

    // Parse JOINs
    while (check(TokenType::INNER) || check(TokenType::LEFT) ||
           check(TokenType::RIGHT) || check(TokenType::JOIN)) {
      std::string joinType;
      if (match(TokenType::INNER)) {
        joinType = "INNER";
      } else if (match(TokenType::LEFT)) {
        joinType = "LEFT";
      } else if (match(TokenType::RIGHT)) {
        joinType = "RIGHT";
      } else {
        joinType = "INNER";
      }
      consume(TokenType::JOIN, "Expected JOIN");

      std::string joinTable =
          consume(TokenType::IDENTIFIER, "Expected table name").get_lexeme();
      std::string joinAlias = joinTable;
      if (match(TokenType::AS)) {
        joinAlias =
            consume(TokenType::IDENTIFIER, "Expected alias").get_lexeme();
      }

      ExpressionPtr condition;
      if (match(TokenType::ON)) {
        condition = parse_expression();
      }

      stmt->add_join(joinType, joinTable, joinAlias, condition);
    }
  }

  // Parse WHERE clause
  if (match(TokenType::WHERE)) {
    stmt->set_where_condition(parse_expression());
  }

  // Parse GROUP BY clause
  if (match(TokenType::GROUP)) {
    consume(TokenType::BY, "Expected BY");
    do {
      stmt->add_group_by_column(parse_expression());
    } while (match(TokenType::COMMA));
  }

  // Parse HAVING clause
  if (match(TokenType::HAVING)) {
    stmt->set_having_condition(parse_expression());
  }

  // Parse ORDER BY clause
  if (match(TokenType::ORDER)) {
    consume(TokenType::BY, "Expected BY");
    do {
      auto expr = parse_expression();
      bool ascending = !match(TokenType::DESC);
      if (!ascending) {
        // Already consumed DESC
      } else {
        match(TokenType::ASC);  // Optional ASC
      }
      stmt->add_order_by_column(expr, ascending);
    } while (match(TokenType::COMMA));
  }

  // Parse LIMIT clause
  if (check(TokenType::IDENTIFIER) && peek().get_lexeme() == "LIMIT") {
    advance();
    if (check(TokenType::NUMBER)) {
      stmt->set_limit(std::stoi(advance().get_lexeme()));
    }
  }

  // Parse OFFSET clause
  if (check(TokenType::IDENTIFIER) && peek().get_lexeme() == "OFFSET") {
    advance();
    if (check(TokenType::NUMBER)) {
      stmt->set_offset(std::stoi(advance().get_lexeme()));
    }
  }

  return stmt;
}

std::shared_ptr<InsertStatement> Parser::parse_insert_statement() {
  consume(TokenType::INSERT, "Expected INSERT");
  consume(TokenType::IDENTIFIER, "Expected INTO");  // INTO

  std::string table =
      consume(TokenType::IDENTIFIER, "Expected table name").get_lexeme();
  auto stmt = std::make_shared<InsertStatement>(table);

  // Parse column list
  if (match(TokenType::LPAREN)) {
    do {
      stmt->add_column(
          consume(TokenType::IDENTIFIER, "Expected column name").get_lexeme());
    } while (match(TokenType::COMMA));
    consume(TokenType::RPAREN, "Expected )");
  }

  // Parse VALUES
  consume(TokenType::VALUES, "Expected VALUES");

  do {
    consume(TokenType::LPAREN, "Expected (");
    std::vector<Value> values;
    do {
      if (match(TokenType::NULL_KW)) {
        values.push_back(Value());
      } else if (check(TokenType::NUMBER)) {
        std::string numStr = advance().get_lexeme();
        if (numStr.find('.') != std::string::npos) {
          values.push_back(Value(std::stod(numStr)));
        } else {
          values.push_back(Value(static_cast<int64_t>(std::stoll(numStr))));
        }
      } else if (check(TokenType::STRING)) {
        values.push_back(Value(advance().get_lexeme()));
      } else {
        throw ParseException("Expected value in INSERT");
      }
    } while (match(TokenType::COMMA));
    consume(TokenType::RPAREN, "Expected )");
    stmt->add_values(values);
  } while (match(TokenType::COMMA));

  return stmt;
}

std::shared_ptr<UpdateStatement> Parser::parse_update_statement() {
  consume(TokenType::UPDATE, "Expected UPDATE");

  std::string table =
      consume(TokenType::IDENTIFIER, "Expected table name").get_lexeme();
  auto stmt = std::make_shared<UpdateStatement>(table);

  consume(TokenType::SET, "Expected SET");

  do {
    std::string column =
        consume(TokenType::IDENTIFIER, "Expected column name").get_lexeme();
    consume(TokenType::EQUAL, "Expected =");
    auto value = parse_expression();
    stmt->add_set_clause(column, value);
  } while (match(TokenType::COMMA));

  if (match(TokenType::WHERE)) {
    stmt->set_where_condition(parse_expression());
  }

  return stmt;
}

std::shared_ptr<DeleteStatement> Parser::parse_delete_statement() {
  consume(TokenType::DELETE, "Expected DELETE");
  consume(TokenType::FROM, "Expected FROM");

  std::string table =
      consume(TokenType::IDENTIFIER, "Expected table name").get_lexeme();
  auto stmt = std::make_shared<DeleteStatement>(table);

  if (match(TokenType::WHERE)) {
    stmt->set_where_condition(parse_expression());
  }

  return stmt;
}

std::shared_ptr<CreateTableStatement> Parser::parse_create_table_statement() {
  consume(TokenType::CREATE, "Expected CREATE");
  consume(TokenType::TABLE, "Expected TABLE");

  std::string table =
      consume(TokenType::IDENTIFIER, "Expected table name").get_lexeme();
  auto stmt = std::make_shared<CreateTableStatement>(table);

  consume(TokenType::LPAREN, "Expected (");

  do {
    ColumnDefinition col = parse_column_definition();
    stmt->add_column(col);
  } while (match(TokenType::COMMA));

  consume(TokenType::RPAREN, "Expected )");

  return stmt;
}

ColumnDefinition Parser::parse_column_definition() {
  std::string name =
      consume(TokenType::IDENTIFIER, "Expected column name").get_lexeme();
  std::string type =
      consume(TokenType::IDENTIFIER, "Expected type").get_lexeme();

  bool not_null = false;
  bool primary_key = false;
  bool unique = false;

  while (check(TokenType::NOT) || check(TokenType::IDENTIFIER)) {
    if (match(TokenType::NOT)) {
      consume(TokenType::NULL_KW, "Expected NULL");
      not_null = true;
    } else if (check(TokenType::IDENTIFIER)) {
      if (peek().get_lexeme() == "PRIMARY") {
        advance();
        consume(TokenType::IDENTIFIER, "Expected KEY");  // "KEY"
        primary_key = true;
      } else if (peek().get_lexeme() == "UNIQUE") {
        advance();
        unique = true;
      } else {
        break;
      }
    } else {
      break;
    }
  }

  return ColumnDefinition(name, type, not_null, primary_key, unique);
}

ExpressionPtr Parser::parse_expression() { return parse_or_expression(); }

ExpressionPtr Parser::parse_or_expression() {
  auto expr = parse_and_expression();

  while (match(TokenType::OR)) {
    auto right = parse_and_expression();
    expr = std::make_shared<BinaryOpExpression>(
        expr, BinaryOpExpression::Operator::OR, right);
  }

  return expr;
}

ExpressionPtr Parser::parse_and_expression() {
  auto expr = parse_not_expression();

  while (match(TokenType::AND)) {
    auto right = parse_not_expression();
    expr = std::make_shared<BinaryOpExpression>(
        expr, BinaryOpExpression::Operator::AND, right);
  }

  return expr;
}

ExpressionPtr Parser::parse_not_expression() {
  if (match(TokenType::NOT)) {
    auto expr = parse_not_expression();
    return std::make_shared<UnaryOpExpression>(UnaryOpExpression::Operator::NOT,
                                               expr);
  }

  return parse_comparison_expression();
}

ExpressionPtr Parser::parse_comparison_expression() {
  auto expr = parse_additive_expression();

  if (match(TokenType::EQUAL)) {
    auto right = parse_additive_expression();
    expr = std::make_shared<BinaryOpExpression>(
        expr, BinaryOpExpression::Operator::EQ, right);
  } else if (match(TokenType::NOT_EQUAL)) {
    auto right = parse_additive_expression();
    expr = std::make_shared<BinaryOpExpression>(
        expr, BinaryOpExpression::Operator::NE, right);
  } else if (match(TokenType::LESS)) {
    auto right = parse_additive_expression();
    expr = std::make_shared<BinaryOpExpression>(
        expr, BinaryOpExpression::Operator::LT, right);
  } else if (match(TokenType::LESS_EQUAL)) {
    auto right = parse_additive_expression();
    expr = std::make_shared<BinaryOpExpression>(
        expr, BinaryOpExpression::Operator::LE, right);
  } else if (match(TokenType::GREATER)) {
    auto right = parse_additive_expression();
    expr = std::make_shared<BinaryOpExpression>(
        expr, BinaryOpExpression::Operator::GT, right);
  } else if (match(TokenType::GREATER_EQUAL)) {
    auto right = parse_additive_expression();
    expr = std::make_shared<BinaryOpExpression>(
        expr, BinaryOpExpression::Operator::GE, right);
  }

  return expr;
}

ExpressionPtr Parser::parse_additive_expression() {
  auto expr = parse_multiplicative_expression();

  while (true) {
    if (match(TokenType::PLUS)) {
      auto right = parse_multiplicative_expression();
      expr = std::make_shared<BinaryOpExpression>(
          expr, BinaryOpExpression::Operator::PLUS, right);
    } else if (match(TokenType::MINUS)) {
      auto right = parse_multiplicative_expression();
      expr = std::make_shared<BinaryOpExpression>(
          expr, BinaryOpExpression::Operator::MINUS, right);
    } else {
      break;
    }
  }

  return expr;
}

ExpressionPtr Parser::parse_multiplicative_expression() {
  auto expr = parse_unary_expression();

  while (true) {
    if (match(TokenType::MULTIPLY)) {
      auto right = parse_unary_expression();
      expr = std::make_shared<BinaryOpExpression>(
          expr, BinaryOpExpression::Operator::MUL, right);
    } else if (match(TokenType::DIVIDE)) {
      auto right = parse_unary_expression();
      expr = std::make_shared<BinaryOpExpression>(
          expr, BinaryOpExpression::Operator::DIV, right);
    } else if (match(TokenType::MODULO)) {
      auto right = parse_unary_expression();
      expr = std::make_shared<BinaryOpExpression>(
          expr, BinaryOpExpression::Operator::MOD, right);
    } else {
      break;
    }
  }

  return expr;
}

ExpressionPtr Parser::parse_unary_expression() {
  if (match(TokenType::MINUS)) {
    auto expr = parse_unary_expression();
    return std::make_shared<UnaryOpExpression>(
        UnaryOpExpression::Operator::MINUS, expr);
  }

  return parse_primary_expression();
}

ExpressionPtr Parser::parse_primary_expression() {
  if (match(TokenType::NULL_KW)) {
    return std::make_shared<LiteralExpression>(Value());
  }

  if (check(TokenType::NUMBER)) {
    std::string numStr = advance().get_lexeme();
    if (numStr.find('.') != std::string::npos) {
      return std::make_shared<LiteralExpression>(Value(std::stod(numStr)));
    } else {
      return std::make_shared<LiteralExpression>(
          Value(static_cast<int64_t>(std::stoll(numStr))));
    }
  }

  if (check(TokenType::STRING)) {
    return std::make_shared<LiteralExpression>(Value(advance().get_lexeme()));
  }

  if (match(TokenType::LPAREN)) {
    auto expr = parse_expression();
    consume(TokenType::RPAREN, "Expected )");
    return expr;
  }

  if (check(TokenType::COUNT) || check(TokenType::SUM) ||
      check(TokenType::AVG) || check(TokenType::MIN) ||
      check(TokenType::MAX)) {
    return parse_identifier_or_function();
  }

  if (check(TokenType::IDENTIFIER) || check(TokenType::ASTERISK)) {
    return parse_identifier_or_function();
  }

  throw ParseException("Expected expression");
}

ExpressionPtr Parser::parse_identifier_or_function() {
  std::string name = advance().get_lexeme();

  // Check for function call
  if (check(TokenType::LPAREN)) {
    advance();
    std::vector<ExpressionPtr> args;
    if (!check(TokenType::RPAREN)) {
      do {
        args.push_back(parse_expression());
      } while (match(TokenType::COMMA));
    }
    consume(TokenType::RPAREN, "Expected )");
    return std::make_shared<FunctionCallExpression>(name, args);
  }

  // Check for column reference (table.column)
  if (match(TokenType::DOT)) {
    std::string column = advance().get_lexeme();
    return std::make_shared<ColumnRefExpression>(name, column);
  }

  // Just a column reference
  if (name == "*") {
    return std::make_shared<IdentifierExpression>("*");
  }

  return std::make_shared<ColumnRefExpression>(name);
}

std::string Parser::get_error_message(const std::string &message) const {
  std::string msg = message + " at line " + std::to_string(peek().get_line()) +
                    ", column " + std::to_string(peek().get_column()) +
                    " (token: '" + peek().get_lexeme() + "')";
  return msg;
}

}  // namespace db
