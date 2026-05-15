#pragma once

#include <memory>
#include <vector>

#include "parser/ast.h"
#include "parser/lexer.h"
#include "parser/token.h"
#include "utils/exceptions.h"

namespace db {

class Parser {
 public:
  explicit Parser(const std::string &sql);
  explicit Parser(std::vector<Token> token_list);

  // Parse different statement types
  std::shared_ptr<SelectStatement> parse_select_statement();
  std::shared_ptr<InsertStatement> parse_insert_statement();
  std::shared_ptr<UpdateStatement> parse_update_statement();
  std::shared_ptr<DeleteStatement> parse_delete_statement();
  std::shared_ptr<CreateTableStatement> parse_create_table_statement();

  // Generic statement parser
  std::variant<
      std::shared_ptr<SelectStatement>, std::shared_ptr<InsertStatement>,
      std::shared_ptr<UpdateStatement>, std::shared_ptr<DeleteStatement>,
      std::shared_ptr<CreateTableStatement>>
  parse_statement();

 private:
  std::vector<Token> tokens_;
  size_t current_{};

  // Token management
  Token peek() const;
  Token peek_ahead(size_t offset) const;
  Token advance();
  bool check(TokenType type) const;
  bool match(TokenType type);
  bool match(const std::vector<TokenType> &types);
  Token consume(TokenType type, const std::string &message);
  bool is_at_end() const;

  // Expression parsing
  ExpressionPtr parse_expression();
  ExpressionPtr parse_or_expression();
  ExpressionPtr parse_and_expression();
  ExpressionPtr parse_not_expression();
  ExpressionPtr parse_comparison_expression();
  ExpressionPtr parse_additive_expression();
  ExpressionPtr parse_multiplicative_expression();
  ExpressionPtr parse_unary_expression();
  ExpressionPtr parse_primary_expression();
  ExpressionPtr parse_identifier_or_function();

  // Statement parsing helpers
  std::shared_ptr<SelectStatement> parse_select_clause();
  std::shared_ptr<InsertStatement> parse_insert_clause();
  std::shared_ptr<UpdateStatement> parse_update_clause();
  std::shared_ptr<DeleteStatement> parse_delete_clause();
  std::shared_ptr<CreateTableStatement> parse_create_table_clause();

  // Utility parsing methods
  ColumnDefinition parse_column_definition();

  // Error handling
  std::string get_error_message(const std::string &message) const;
};

}  // namespace db
