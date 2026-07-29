#include "parser/parser.h"

#include <algorithm>
#include <cctype>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_set>

#include "types/data_type.h"

namespace db {

Parser::Parser(const std::string &sql) : current_(0), original_sql_(sql) {
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

size_t Parser::get_parameter_count() const {
  if (parameter_style_ == ParameterStyle::Dollar) {
    return max_dollar_index_;
  }
  return next_parameter_index_;
}

void Parser::validate_dollar_parameters() const {
  if (parameter_style_ != ParameterStyle::Dollar) {
    return;
  }
  if (max_dollar_index_ == 0) {
    return;
  }
  for (size_t i = 1; i <= max_dollar_index_; ++i) {
    if (dollar_indices_.count(i) == 0) {
      throw ParseException("Dollar parameters must be dense from $1 without gaps");
    }
  }
}

ParsedStatement Parser::parse_statement() {
  if (check(TokenType::EXPLAIN)) {
    return parse_explain_statement();
  }
  ParsedStatement result = parse_inner_statement();
  validate_dollar_parameters();
  return result;
}

ExpressionPtr Parser::parse_standalone_expression() {
  ExpressionPtr expr = parse_expression();
  match(TokenType::SEMICOLON);
  if (!is_at_end()) {
    throw ParseException(get_error_message("Unexpected tokens after expression"));
  }
  return expr;
}

ParsedStatement Parser::parse_inner_statement() {
  ParsedStatement result;
  if (check(TokenType::SELECT) || check(TokenType::LPAREN)) {
    result = parse_query_statement();
  } else if (check(TokenType::INSERT)) {
    result = parse_insert_statement();
  } else if (check(TokenType::UPDATE)) {
    result = parse_update_statement();
  } else if (check(TokenType::DELETE)) {
    result = parse_delete_statement();
  } else if (check(TokenType::CREATE)) {
    if (peek_ahead(1).get_type() == TokenType::INDEX) {
      result = parse_create_index_statement();
    } else if (peek_ahead(1).get_type() == TokenType::VIEW) {
      result = parse_create_view_statement();
    } else if (peek_ahead(1).get_type() == TokenType::FUNCTION) {
      result = parse_create_function_statement();
    } else if (peek_ahead(1).get_type() == TokenType::PROCEDURE) {
      result = parse_create_procedure_statement();
    } else if (peek_ahead(1).get_type() == TokenType::TRIGGER) {
      result = parse_create_trigger_statement();
    } else {
      result = parse_create_table_statement();
    }
  } else if (check(TokenType::DROP)) {
    if (peek_ahead(1).get_type() == TokenType::INDEX) {
      result = parse_drop_index_statement();
    } else if (peek_ahead(1).get_type() == TokenType::VIEW) {
      result = parse_drop_view_statement();
    } else if (peek_ahead(1).get_type() == TokenType::FUNCTION) {
      result = parse_drop_function_statement();
    } else if (peek_ahead(1).get_type() == TokenType::PROCEDURE) {
      result = parse_drop_procedure_statement();
    } else if (peek_ahead(1).get_type() == TokenType::TRIGGER) {
      result = parse_drop_trigger_statement();
    } else {
      result = parse_drop_table_statement();
    }
  } else if (check(TokenType::CALL)) {
    result = parse_call_statement();
  } else if (check(TokenType::SET)) {
    result = parse_set_new_statement();
  } else if (check(TokenType::ALTER)) {
    result = parse_alter_table_statement();
  } else if (check(TokenType::BEGIN)) {
    result = parse_begin_statement();
  } else if (check(TokenType::COMMIT)) {
    result = parse_commit_statement();
  } else if (check(TokenType::ROLLBACK)) {
    result = parse_rollback_statement();
  } else if (check(TokenType::PREPARE)) {
    result = parse_prepare_statement();
  } else if (check(TokenType::EXECUTE)) {
    result = parse_execute_statement();
  } else if (check(TokenType::DEALLOCATE)) {
    result = parse_deallocate_statement();
  } else if (check(TokenType::VACUUM)) {
    result = parse_vacuum_statement();
  } else {
    throw ParseException("Unknown statement");
  }
  return result;
}

std::shared_ptr<ExplainStatement> Parser::parse_explain_statement() {
  consume(TokenType::EXPLAIN, "Expected EXPLAIN");
  if (check(TokenType::EXPLAIN)) {
    throw ParseException("Nested EXPLAIN is not allowed");
  }
  if (is_at_end()) {
    throw ParseException("Expected statement after EXPLAIN");
  }
  ParsedStatement inner = parse_inner_statement();
  validate_dollar_parameters();
  return std::make_shared<ExplainStatement>(std::move(inner));
}

ExplainStatement::ExplainStatement(ParsedStatement inner)
    : inner_(std::move(inner)) {}

const ParsedStatement &ExplainStatement::get_inner() const { return inner_; }

namespace {

std::string statement_to_string(const ParsedStatement &stmt) {
  if (auto p = std::get_if<std::shared_ptr<SelectStatement>>(&stmt)) {
    return (*p)->to_string();
  }
  if (auto p = std::get_if<std::shared_ptr<SetOperationStatement>>(&stmt)) {
    return (*p)->to_string();
  }
  if (auto p = std::get_if<std::shared_ptr<InsertStatement>>(&stmt)) {
    return (*p)->to_string();
  }
  if (auto p = std::get_if<std::shared_ptr<UpdateStatement>>(&stmt)) {
    return (*p)->to_string();
  }
  if (auto p = std::get_if<std::shared_ptr<DeleteStatement>>(&stmt)) {
    return (*p)->to_string();
  }
  if (auto p = std::get_if<std::shared_ptr<CreateTableStatement>>(&stmt)) {
    return (*p)->to_string();
  }
  if (auto p = std::get_if<std::shared_ptr<DropTableStatement>>(&stmt)) {
    return (*p)->to_string();
  }
  if (auto p = std::get_if<std::shared_ptr<AlterTableStatement>>(&stmt)) {
    return (*p)->to_string();
  }
  if (auto p = std::get_if<std::shared_ptr<CreateIndexStatement>>(&stmt)) {
    return (*p)->to_string();
  }
  if (auto p = std::get_if<std::shared_ptr<DropIndexStatement>>(&stmt)) {
    return (*p)->to_string();
  }
  if (auto p = std::get_if<std::shared_ptr<CreateViewStatement>>(&stmt)) {
    return (*p)->to_string();
  }
  if (auto p = std::get_if<std::shared_ptr<DropViewStatement>>(&stmt)) {
    return (*p)->to_string();
  }
  if (auto p = std::get_if<std::shared_ptr<CreateFunctionStatement>>(&stmt)) {
    return (*p)->to_string();
  }
  if (auto p = std::get_if<std::shared_ptr<DropFunctionStatement>>(&stmt)) {
    return (*p)->to_string();
  }
  if (auto p = std::get_if<std::shared_ptr<CreateProcedureStatement>>(&stmt)) {
    return (*p)->to_string();
  }
  if (auto p = std::get_if<std::shared_ptr<DropProcedureStatement>>(&stmt)) {
    return (*p)->to_string();
  }
  if (auto p = std::get_if<std::shared_ptr<CallStatement>>(&stmt)) {
    return (*p)->to_string();
  }
  if (auto p = std::get_if<std::shared_ptr<CreateTriggerStatement>>(&stmt)) {
    return (*p)->to_string();
  }
  if (auto p = std::get_if<std::shared_ptr<DropTriggerStatement>>(&stmt)) {
    return (*p)->to_string();
  }
  if (auto p = std::get_if<std::shared_ptr<SetNewStatement>>(&stmt)) {
    return (*p)->to_string();
  }
  if (auto p = std::get_if<std::shared_ptr<BeginStatement>>(&stmt)) {
    return (*p)->to_string();
  }
  if (auto p = std::get_if<std::shared_ptr<CommitStatement>>(&stmt)) {
    return (*p)->to_string();
  }
  if (auto p = std::get_if<std::shared_ptr<RollbackStatement>>(&stmt)) {
    return (*p)->to_string();
  }
  if (auto p = std::get_if<std::shared_ptr<PrepareStatement>>(&stmt)) {
    return (*p)->to_string();
  }
  if (auto p = std::get_if<std::shared_ptr<ExecutePreparedStatement>>(&stmt)) {
    return (*p)->to_string();
  }
  if (auto p =
          std::get_if<std::shared_ptr<DeallocatePreparedStatement>>(&stmt)) {
    return (*p)->to_string();
  }
  if (auto p = std::get_if<std::shared_ptr<VacuumStatement>>(&stmt)) {
    return (*p)->to_string();
  }
  return "UNKNOWN";
}

}  // namespace

std::string ExplainStatement::to_string() const {
  return "EXPLAIN " + statement_to_string(inner_);
}

std::shared_ptr<SelectStatement> Parser::parse_select_core() {
  consume(TokenType::SELECT, "Expected SELECT");
  auto stmt = std::make_shared<SelectStatement>();
  if (match(TokenType::DISTINCT)) {
    stmt->set_distinct(true);
  }
  do {
    auto expr = parse_expression();
    std::string alias;
    if (match(TokenType::AS)) {
      alias =
          consume(TokenType::IDENTIFIER, "Expected identifier").get_lexeme();
    }
    stmt->add_select_column(expr, alias);
  } while (match(TokenType::COMMA));
  if (match(TokenType::FROM)) {
    std::string table =
        consume(TokenType::IDENTIFIER, "Expected table name").get_lexeme();
    std::string alias = table;
    if (match(TokenType::AS)) {
      alias = consume(TokenType::IDENTIFIER, "Expected alias").get_lexeme();
    }
    stmt->set_from_table(table, alias);
    while (check(TokenType::INNER) || check(TokenType::LEFT) ||
           check(TokenType::RIGHT) || check(TokenType::JOIN) ||
           check(TokenType::FULL) || check(TokenType::CROSS)) {
      std::string joinType;
      ExpressionPtr condition;
      if (match(TokenType::CROSS)) {
        consume(TokenType::JOIN, "Expected JOIN");
        joinType = "CROSS";
      } else if (match(TokenType::FULL)) {
        match(TokenType::OUTER);
        consume(TokenType::JOIN, "Expected JOIN");
        joinType = "FULL";
      } else {
        if (match(TokenType::INNER)) {
          joinType = "INNER";
        } else if (match(TokenType::LEFT)) {
          joinType = "LEFT";
          match(TokenType::OUTER);
        } else if (match(TokenType::RIGHT)) {
          joinType = "RIGHT";
          match(TokenType::OUTER);
        } else {
          joinType = "INNER";
        }
        consume(TokenType::JOIN, "Expected JOIN");
      }
      std::string joinTable =
          consume(TokenType::IDENTIFIER, "Expected table name").get_lexeme();
      std::string joinAlias = joinTable;
      if (match(TokenType::AS)) {
        joinAlias =
            consume(TokenType::IDENTIFIER, "Expected alias").get_lexeme();
      }
      if (joinType == "CROSS") {
        if (match(TokenType::ON)) {
          throw ParseException(
              get_error_message("CROSS JOIN cannot have ON clause"));
        }
      } else if (joinType == "FULL") {
        consume(TokenType::ON, "Expected ON clause for FULL JOIN");
        condition = parse_expression();
      } else {
        if (match(TokenType::ON)) {
          condition = parse_expression();
        }
      }
      stmt->add_join(joinType, joinTable, joinAlias, condition);
    }
  }
  if (match(TokenType::WHERE)) {
    stmt->set_where_condition(parse_expression());
  }
  if (match(TokenType::GROUP)) {
    consume(TokenType::BY, "Expected BY");
    do {
      stmt->add_group_by_column(parse_expression());
    } while (match(TokenType::COMMA));
  }
  if (match(TokenType::HAVING)) {
    stmt->set_having_condition(parse_expression());
  }
  return stmt;
}

void Parser::parse_order_limit_clauses(SelectStatement &stmt) {
  if (match(TokenType::ORDER)) {
    consume(TokenType::BY, "Expected BY");
    do {
      auto expr = parse_expression();
      bool ascending = !match(TokenType::DESC);
      if (ascending) {
        match(TokenType::ASC);
      }
      stmt.add_order_by_column(expr, ascending);
    } while (match(TokenType::COMMA));
  }
  if (check(TokenType::IDENTIFIER) && peek().get_lexeme() == "LIMIT") {
    advance();
    if (check(TokenType::NUMBER)) {
      stmt.set_limit(std::stoi(advance().get_lexeme()));
    }
  }
  if (check(TokenType::IDENTIFIER) && peek().get_lexeme() == "OFFSET") {
    advance();
    if (check(TokenType::NUMBER)) {
      stmt.set_offset(std::stoi(advance().get_lexeme()));
    }
  }
}

void Parser::parse_order_limit_clauses(SetOperationStatement &stmt) {
  if (match(TokenType::ORDER)) {
    consume(TokenType::BY, "Expected BY");
    do {
      auto expr = parse_expression();
      bool ascending = !match(TokenType::DESC);
      if (ascending) {
        match(TokenType::ASC);
      }
      stmt.add_order_by_column(expr, ascending);
    } while (match(TokenType::COMMA));
  }
  if (check(TokenType::IDENTIFIER) && peek().get_lexeme() == "LIMIT") {
    advance();
    if (check(TokenType::NUMBER)) {
      stmt.set_limit(std::stoi(advance().get_lexeme()));
    }
  }
  if (check(TokenType::IDENTIFIER) && peek().get_lexeme() == "OFFSET") {
    advance();
    if (check(TokenType::NUMBER)) {
      stmt.set_offset(std::stoi(advance().get_lexeme()));
    }
  }
}

bool Parser::match_set_operation(SetOperationKind &kind, bool &isAll) {
  if (match(TokenType::UNION)) {
    kind = SetOperationKind::Union;
    isAll = match(TokenType::ALL);
    return true;
  }
  if (match(TokenType::INTERSECT)) {
    kind = SetOperationKind::Intersect;
    isAll = false;
    if (match(TokenType::ALL)) {
      throw ParseException(
          get_error_message("INTERSECT ALL is not supported"));
    }
    return true;
  }
  if (match(TokenType::EXCEPT)) {
    kind = SetOperationKind::Except;
    isAll = false;
    if (match(TokenType::ALL)) {
      throw ParseException(get_error_message("EXCEPT ALL is not supported"));
    }
    return true;
  }
  return false;
}

SetOperationStatement::Operand Parser::parse_query_primary() {
  if (match(TokenType::LPAREN)) {
    SetOperationStatement::Operand inner = parse_query_inner();
    consume(TokenType::RPAREN, "Expected )");
    return inner;
  }
  return parse_select_core();
}

SetOperationStatement::Operand Parser::parse_query_inner() {
  SetOperationStatement::Operand left = parse_query_primary();
  SetOperationKind kind = SetOperationKind::Union;
  bool isAll = false;
  while (match_set_operation(kind, isAll)) {
    SetOperationStatement::Operand right = parse_query_primary();
    left = std::make_shared<SetOperationStatement>(std::move(left), kind,
                                                   std::move(right), isAll);
  }
  return left;
}

ParsedStatement Parser::parse_query_statement() {
  SetOperationStatement::Operand root = parse_query_inner();
  if (auto select = std::get_if<std::shared_ptr<SelectStatement>>(&root)) {
    parse_order_limit_clauses(**select);
    return *select;
  }
  auto setOp = std::get<std::shared_ptr<SetOperationStatement>>(root);
  parse_order_limit_clauses(*setOp);
  return setOp;
}

std::shared_ptr<SelectStatement> Parser::parse_select_statement() {
  auto stmt = parse_select_core();
  parse_order_limit_clauses(*stmt);
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
    std::vector<ExpressionPtr> values;
    do {
      values.push_back(parse_expression());
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
  if (match(TokenType::PARTITION)) {
    consume(TokenType::OF, "Expected OF after PARTITION");
    const std::string parent =
        consume(TokenType::IDENTIFIER, "Expected parent table name").get_lexeme();
    consume(TokenType::FOR, "Expected FOR");
    consume(TokenType::VALUES, "Expected VALUES");
    PartitionBound bound;
    if (match(TokenType::FROM)) {
      consume(TokenType::LPAREN, "Expected ( after FROM");
      ExpressionPtr minExpr = parse_expression();
      auto minLit = std::dynamic_pointer_cast<LiteralExpression>(minExpr);
      if (!minLit) {
        throw ParseException(
            get_error_message("Expected literal in FROM (...)"));
      }
      consume(TokenType::RPAREN, "Expected ) after FROM value");
      consume(TokenType::TO, "Expected TO");
      consume(TokenType::LPAREN, "Expected ( after TO");
      ExpressionPtr maxExpr = parse_expression();
      auto maxLit = std::dynamic_pointer_cast<LiteralExpression>(maxExpr);
      if (!maxLit) {
        throw ParseException(get_error_message("Expected literal in TO (...)"));
      }
      consume(TokenType::RPAREN, "Expected ) after TO value");
      RangePartitionBound range;
      range.minInclusive = minLit->get_value();
      range.maxExclusive = maxLit->get_value();
      bound.range = range;
    } else if (match(TokenType::WITH)) {
      consume(TokenType::LPAREN, "Expected ( after WITH");
      consume(TokenType::MODULUS, "Expected MODULUS");
      ExpressionPtr modulusExpr = parse_expression();
      auto modulusLit =
          std::dynamic_pointer_cast<LiteralExpression>(modulusExpr);
      if (!modulusLit || !modulusLit->get_value().is_int()) {
        throw ParseException(get_error_message("Expected integer MODULUS"));
      }
      consume(TokenType::COMMA, "Expected comma after MODULUS");
      consume(TokenType::REMAINDER, "Expected REMAINDER");
      ExpressionPtr remainderExpr = parse_expression();
      auto remainderLit =
          std::dynamic_pointer_cast<LiteralExpression>(remainderExpr);
      if (!remainderLit || !remainderLit->get_value().is_int()) {
        throw ParseException(get_error_message("Expected integer REMAINDER"));
      }
      consume(TokenType::RPAREN, "Expected ) after REMAINDER");
      HashPartitionBound hash;
      hash.modulus = modulusLit->get_value().as_int();
      hash.remainder = remainderLit->get_value().as_int();
      bound.hash = hash;
    } else {
      throw ParseException(
          get_error_message("Expected FROM or WITH after FOR VALUES"));
    }
    stmt->setPartitionOf(parent, bound);
    match(TokenType::SEMICOLON);
    return stmt;
  }
  consume(TokenType::LPAREN, "Expected (");
  do {
    if (check(TokenType::CHECK) ||
        (check(TokenType::CONSTRAINT) &&
         peek_ahead(1).get_type() == TokenType::IDENTIFIER &&
         peek_ahead(2).get_type() == TokenType::CHECK)) {
      std::string constraint_name;
      if (match(TokenType::CONSTRAINT)) {
        constraint_name =
            consume(TokenType::IDENTIFIER, "Expected constraint name")
                .get_lexeme();
      }
      stmt->add_check(parse_table_check_constraint(constraint_name));
    } else if (check(TokenType::FOREIGN) ||
               (check(TokenType::CONSTRAINT) &&
                peek_ahead(1).get_type() == TokenType::IDENTIFIER &&
                peek_ahead(2).get_type() == TokenType::FOREIGN)) {
      if (match(TokenType::CONSTRAINT)) {
        consume(TokenType::IDENTIFIER, "Expected constraint name");
      }
      stmt->add_foreign_key(parse_table_foreign_key());
    } else if (check(TokenType::PRIMARY) ||
               (check(TokenType::CONSTRAINT) &&
                peek_ahead(1).get_type() == TokenType::IDENTIFIER &&
                peek_ahead(2).get_type() == TokenType::PRIMARY)) {
      if (match(TokenType::CONSTRAINT)) {
        consume(TokenType::IDENTIFIER, "Expected constraint name");
      }
      consume(TokenType::PRIMARY, "Expected PRIMARY");
      consume(TokenType::KEY, "Expected KEY");
      if (!stmt->get_primary_key_columns().empty()) {
        throw ParseException(
            get_error_message("Multiple PRIMARY KEY definitions"));
      }
      consume(TokenType::LPAREN, "Expected (");
      std::vector<std::string> pk_columns;
      do {
        pk_columns.push_back(
            consume(TokenType::IDENTIFIER, "Expected column name").get_lexeme());
      } while (match(TokenType::COMMA));
      consume(TokenType::RPAREN, "Expected )");
      if (pk_columns.empty()) {
        throw ParseException(
            get_error_message("PRIMARY KEY must list at least one column"));
      }
      stmt->set_primary_key(std::move(pk_columns));
    } else if (check(TokenType::UNIQUE) ||
               (check(TokenType::CONSTRAINT) &&
                peek_ahead(1).get_type() == TokenType::IDENTIFIER &&
                peek_ahead(2).get_type() == TokenType::UNIQUE)) {
      std::string constraint_name;
      if (match(TokenType::CONSTRAINT)) {
        constraint_name =
            consume(TokenType::IDENTIFIER, "Expected constraint name")
                .get_lexeme();
      }
      consume(TokenType::UNIQUE, "Expected UNIQUE");
      consume(TokenType::LPAREN, "Expected (");
      std::vector<std::string> uq_columns;
      do {
        uq_columns.push_back(
            consume(TokenType::IDENTIFIER, "Expected column name").get_lexeme());
      } while (match(TokenType::COMMA));
      consume(TokenType::RPAREN, "Expected )");
      if (uq_columns.empty()) {
        throw ParseException(
            get_error_message("UNIQUE must list at least one column"));
      }
      stmt->add_unique(std::move(constraint_name), std::move(uq_columns));
    } else {
      ColumnDefinition col = parse_column_definition();
      stmt->add_column(col);
      if (col.has_references()) {
        ForeignKeyDefinition fk;
        fk.child_columns = {col.get_name()};
        fk.parent_table = col.get_references_table();
        fk.parent_columns = {col.get_references_column()};
        fk.on_delete = col.get_on_delete();
        fk.on_update = col.get_on_update();
        stmt->add_foreign_key(fk);
      }
      if (col.has_check_expression()) {
        CheckConstraintDefinition check;
        check.predicate = col.get_check_expression();
        check.expression_text = check.predicate->to_string();
        stmt->add_check(check);
      }
    }
  } while (match(TokenType::COMMA));
  consume(TokenType::RPAREN, "Expected )");
  if (match(TokenType::PARTITION)) {
    consume(TokenType::BY, "Expected BY after PARTITION");
    PartitionKind kind = PartitionKind::Range;
    if (match(TokenType::RANGE)) {
      kind = PartitionKind::Range;
    } else if (match(TokenType::HASH)) {
      kind = PartitionKind::Hash;
    } else {
      throw ParseException(
          get_error_message("Expected RANGE or HASH after PARTITION BY"));
    }
    consume(TokenType::LPAREN, "Expected ( after partition kind");
    const std::string keyColumn =
        consume(TokenType::IDENTIFIER, "Expected partition key column")
            .get_lexeme();
    consume(TokenType::RPAREN, "Expected ) after partition key");
    stmt->setPartitionBy(kind, keyColumn);
  }
  match(TokenType::SEMICOLON);
  return stmt;
}

std::shared_ptr<CreateIndexStatement> Parser::parse_create_index_statement() {
  consume(TokenType::CREATE, "Expected CREATE");
  consume(TokenType::INDEX, "Expected INDEX");
  std::string index_name =
      consume(TokenType::IDENTIFIER, "Expected index name").get_lexeme();
  consume(TokenType::ON, "Expected ON");
  std::string table_name =
      consume(TokenType::IDENTIFIER, "Expected table name").get_lexeme();
  consume(TokenType::LPAREN, "Expected (");
  std::vector<std::string> column_names;
  do {
    column_names.push_back(
        consume(TokenType::IDENTIFIER, "Expected column name").get_lexeme());
  } while (match(TokenType::COMMA));
  consume(TokenType::RPAREN, "Expected )");
  match(TokenType::SEMICOLON);
  return std::make_shared<CreateIndexStatement>(index_name, table_name,
                                                std::move(column_names));
}

namespace {

std::string trim_trailing_sql(std::string sql) {
  while (!sql.empty() &&
         (sql.back() == ';' ||
          std::isspace(static_cast<unsigned char>(sql.back())))) {
    sql.pop_back();
  }
  return sql;
}

std::string extract_sql_after_as(const std::string &original_sql) {
  if (original_sql.empty()) {
    throw ParseException("Statement requires original SQL text");
  }
  std::string upper = original_sql;
  std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
  const size_t as_pos = upper.find(" AS ");
  if (as_pos == std::string::npos) {
    throw ParseException("Expected AS clause");
  }
  return trim_trailing_sql(original_sql.substr(as_pos + 4));
}

}  // namespace

std::shared_ptr<CreateViewStatement> Parser::parse_create_view_statement() {
  consume(TokenType::CREATE, "Expected CREATE");
  consume(TokenType::VIEW, "Expected VIEW");
  std::string view_name =
      consume(TokenType::IDENTIFIER, "Expected view name").get_lexeme();
  consume(TokenType::AS, "Expected AS");
  std::string select_sql = extract_sql_after_as(original_sql_);
  auto select = parse_select_statement();
  match(TokenType::SEMICOLON);
  return std::make_shared<CreateViewStatement>(view_name, select_sql, select);
}

std::shared_ptr<DropViewStatement> Parser::parse_drop_view_statement() {
  consume(TokenType::DROP, "Expected DROP");
  consume(TokenType::VIEW, "Expected VIEW");
  bool if_exists = false;
  if (match(TokenType::IF)) {
    consume(TokenType::EXISTS, "Expected EXISTS after IF");
    if_exists = true;
  }
  std::string view_name =
      consume(TokenType::IDENTIFIER, "Expected view name").get_lexeme();
  match(TokenType::SEMICOLON);
  return std::make_shared<DropViewStatement>(view_name, if_exists);
}

std::vector<RoutineParamAst> Parser::parse_routine_param_list() {
  std::vector<RoutineParamAst> params;
  consume(TokenType::LPAREN, "Expected (");
  if (!check(TokenType::RPAREN)) {
    do {
      RoutineParamAst param;
      param.name =
          consume(TokenType::IDENTIFIER, "Expected parameter name").get_lexeme();
      param.type_name =
          consume(TokenType::IDENTIFIER, "Expected parameter type").get_lexeme();
      params.push_back(std::move(param));
    } while (match(TokenType::COMMA));
  }
  consume(TokenType::RPAREN, "Expected )");
  return params;
}

ExpressionPtr Parser::parse_function_body_expression() {
  match(TokenType::RETURN);
  ExpressionPtr body = parse_expression();
  match(TokenType::SEMICOLON);
  return body;
}

std::vector<std::string> Parser::split_sql_statements(
    const std::string &body) const {
  std::vector<std::string> statements;
  std::string current;
  int depth = 0;
  bool in_string = false;
  for (size_t i = 0; i < body.size(); ++i) {
    const char c = body[i];
    if (in_string) {
      current.push_back(c);
      if (c == '\'' && i + 1 < body.size() && body[i + 1] == '\'') {
        current.push_back(body[++i]);
      } else if (c == '\'') {
        in_string = false;
      }
      continue;
    }
    if (c == '\'') {
      in_string = true;
      current.push_back(c);
      continue;
    }
    if (c == '(') {
      ++depth;
    } else if (c == ')') {
      --depth;
    }
    if (c == ';' && depth == 0) {
      size_t start = 0;
      while (start < current.size() &&
             std::isspace(static_cast<unsigned char>(current[start]))) {
        ++start;
      }
      size_t end = current.size();
      while (end > start &&
             std::isspace(static_cast<unsigned char>(current[end - 1]))) {
        --end;
      }
      if (end > start) {
        statements.push_back(current.substr(start, end - start));
      }
      current.clear();
      continue;
    }
    current.push_back(c);
  }
  size_t start = 0;
  while (start < current.size() &&
         std::isspace(static_cast<unsigned char>(current[start]))) {
    ++start;
  }
  size_t end = current.size();
  while (end > start &&
         std::isspace(static_cast<unsigned char>(current[end - 1]))) {
    --end;
  }
  if (end > start) {
    statements.push_back(current.substr(start, end - start));
  }
  return statements;
}

std::string Parser::trim_original_sql() const {
  std::string sql = original_sql_;
  while (!sql.empty() &&
         (sql.back() == '\n' || sql.back() == '\r' || sql.back() == ' ' ||
          sql.back() == '\t')) {
    sql.pop_back();
  }
  return sql;
}

std::shared_ptr<CreateFunctionStatement>
Parser::parse_create_function_statement() {
  consume(TokenType::CREATE, "Expected CREATE");
  consume(TokenType::FUNCTION, "Expected FUNCTION");
  std::string name =
      consume(TokenType::IDENTIFIER, "Expected function name").get_lexeme();
  std::vector<RoutineParamAst> params = parse_routine_param_list();
  consume(TokenType::RETURNS, "Expected RETURNS");
  std::string return_type =
      consume(TokenType::IDENTIFIER, "Expected return type").get_lexeme();
  consume(TokenType::AS, "Expected AS");
  ExpressionPtr body;
  if (check(TokenType::DOLLAR_QUOTED_STRING)) {
    const std::string body_text = advance().get_lexeme();
    Parser body_parser(body_text);
    body = body_parser.parse_function_body_expression();
  } else {
    body = parse_expression();
  }
  match(TokenType::SEMICOLON);
  try {
    (void)string_to_data_type(return_type);
    for (const RoutineParamAst &param : params) {
      (void)string_to_data_type(param.type_name);
    }
  } catch (const std::exception &ex) {
    throw ParseException(get_error_message(ex.what()));
  }
  return std::make_shared<CreateFunctionStatement>(
      std::move(name), std::move(params), std::move(return_type),
      std::move(body), trim_original_sql());
}

std::shared_ptr<DropFunctionStatement> Parser::parse_drop_function_statement() {
  consume(TokenType::DROP, "Expected DROP");
  consume(TokenType::FUNCTION, "Expected FUNCTION");
  bool if_exists = false;
  if (match(TokenType::IF)) {
    consume(TokenType::EXISTS, "Expected EXISTS after IF");
    if_exists = true;
  }
  std::string name =
      consume(TokenType::IDENTIFIER, "Expected function name").get_lexeme();
  match(TokenType::SEMICOLON);
  return std::make_shared<DropFunctionStatement>(std::move(name), if_exists);
}

std::shared_ptr<CreateProcedureStatement>
Parser::parse_create_procedure_statement() {
  consume(TokenType::CREATE, "Expected CREATE");
  consume(TokenType::PROCEDURE, "Expected PROCEDURE");
  std::string name =
      consume(TokenType::IDENTIFIER, "Expected procedure name").get_lexeme();
  std::vector<RoutineParamAst> params = parse_routine_param_list();
  consume(TokenType::AS, "Expected AS");
  if (!check(TokenType::DOLLAR_QUOTED_STRING)) {
    throw ParseException(
        get_error_message("Expected $$ ... $$ body for CREATE PROCEDURE"));
  }
  const std::string body_text = advance().get_lexeme();
  std::vector<std::string> statements = split_sql_statements(body_text);
  if (statements.empty()) {
    throw ParseException(get_error_message("Procedure body is empty"));
  }
  for (const std::string &stmt_sql : statements) {
    Parser check_parser(stmt_sql);
    (void)check_parser.parse_statement();
  }
  match(TokenType::SEMICOLON);
  try {
    for (const RoutineParamAst &param : params) {
      (void)string_to_data_type(param.type_name);
    }
  } catch (const std::exception &ex) {
    throw ParseException(get_error_message(ex.what()));
  }
  return std::make_shared<CreateProcedureStatement>(
      std::move(name), std::move(params), std::move(statements),
      trim_original_sql());
}

std::shared_ptr<DropProcedureStatement>
Parser::parse_drop_procedure_statement() {
  consume(TokenType::DROP, "Expected DROP");
  consume(TokenType::PROCEDURE, "Expected PROCEDURE");
  bool if_exists = false;
  if (match(TokenType::IF)) {
    consume(TokenType::EXISTS, "Expected EXISTS after IF");
    if_exists = true;
  }
  std::string name =
      consume(TokenType::IDENTIFIER, "Expected procedure name").get_lexeme();
  match(TokenType::SEMICOLON);
  return std::make_shared<DropProcedureStatement>(std::move(name), if_exists);
}

std::shared_ptr<CallStatement> Parser::parse_call_statement() {
  consume(TokenType::CALL, "Expected CALL");
  std::string name =
      consume(TokenType::IDENTIFIER, "Expected procedure name").get_lexeme();
  consume(TokenType::LPAREN, "Expected (");
  std::vector<ExpressionPtr> args;
  if (!check(TokenType::RPAREN)) {
    do {
      args.push_back(parse_expression());
    } while (match(TokenType::COMMA));
  }
  consume(TokenType::RPAREN, "Expected )");
  match(TokenType::SEMICOLON);
  return std::make_shared<CallStatement>(std::move(name), std::move(args));
}

std::shared_ptr<CreateTriggerStatement>
Parser::parse_create_trigger_statement() {
  consume(TokenType::CREATE, "Expected CREATE");
  consume(TokenType::TRIGGER, "Expected TRIGGER");
  std::string name =
      consume(TokenType::IDENTIFIER, "Expected trigger name").get_lexeme();
  TriggerTiming timing;
  if (match(TokenType::BEFORE)) {
    timing = TriggerTiming::Before;
  } else if (match(TokenType::AFTER)) {
    timing = TriggerTiming::After;
  } else {
    throw ParseException(get_error_message("Expected BEFORE or AFTER"));
  }
  TriggerEvent event;
  if (match(TokenType::INSERT)) {
    event = TriggerEvent::Insert;
  } else if (match(TokenType::UPDATE)) {
    event = TriggerEvent::Update;
  } else if (match(TokenType::DELETE)) {
    event = TriggerEvent::Delete;
  } else {
    throw ParseException(
        get_error_message("Expected INSERT, UPDATE, or DELETE"));
  }
  consume(TokenType::ON, "Expected ON");
  std::string table_name =
      consume(TokenType::IDENTIFIER, "Expected table name").get_lexeme();
  consume(TokenType::FOR, "Expected FOR");
  consume(TokenType::EACH, "Expected EACH");
  consume(TokenType::ROW, "Expected ROW");
  consume(TokenType::EXECUTE, "Expected EXECUTE");
  if (!check(TokenType::DOLLAR_QUOTED_STRING)) {
    throw ParseException(
        get_error_message("Expected $$ ... $$ body for CREATE TRIGGER"));
  }
  const std::string body_text = advance().get_lexeme();
  std::vector<std::string> statements = split_sql_statements(body_text);
  if (statements.empty()) {
    throw ParseException(get_error_message("Trigger body is empty"));
  }
  for (const std::string &stmt_sql : statements) {
    Parser check_parser(stmt_sql);
    (void)check_parser.parse_statement();
  }
  match(TokenType::SEMICOLON);
  return std::make_shared<CreateTriggerStatement>(
      std::move(name), std::move(table_name), timing, event,
      std::move(statements), trim_original_sql());
}

std::shared_ptr<DropTriggerStatement> Parser::parse_drop_trigger_statement() {
  consume(TokenType::DROP, "Expected DROP");
  consume(TokenType::TRIGGER, "Expected TRIGGER");
  bool if_exists = false;
  if (match(TokenType::IF)) {
    consume(TokenType::EXISTS, "Expected EXISTS after IF");
    if_exists = true;
  }
  std::string name =
      consume(TokenType::IDENTIFIER, "Expected trigger name").get_lexeme();
  match(TokenType::SEMICOLON);
  return std::make_shared<DropTriggerStatement>(std::move(name), if_exists);
}

std::shared_ptr<SetNewStatement> Parser::parse_set_new_statement() {
  consume(TokenType::SET, "Expected SET");
  Token new_token =
      consume(TokenType::IDENTIFIER, "Expected NEW after SET");
  std::string new_lexeme = new_token.get_lexeme();
  for (char &c : new_lexeme) {
    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  }
  if (new_lexeme != "NEW") {
    throw ParseException(get_error_message("Expected NEW after SET"));
  }
  consume(TokenType::DOT, "Expected . after NEW");
  std::string column_name =
      consume(TokenType::IDENTIFIER, "Expected column name").get_lexeme();
  consume(TokenType::EQUAL, "Expected =");
  ExpressionPtr value = parse_expression();
  match(TokenType::SEMICOLON);
  return std::make_shared<SetNewStatement>(std::move(column_name),
                                           std::move(value));
}

ColumnDefinition Parser::parse_column_definition() {
  std::string name =
      consume(TokenType::IDENTIFIER, "Expected column name").get_lexeme();
  std::string type =
      consume(TokenType::IDENTIFIER, "Expected type").get_lexeme();
  bool not_null = false;
  bool primary_key = false;
  bool unique = false;
  std::optional<Value> default_value;
  std::string ref_table;
  std::string ref_column;
  ReferentialAction on_delete = ReferentialAction::Restrict;
  ReferentialAction on_update = ReferentialAction::Restrict;
  ExpressionPtr check_expression;
  while (check(TokenType::NOT) || check(TokenType::PRIMARY) ||
         check(TokenType::UNIQUE) || check(TokenType::REFERENCES) ||
         check(TokenType::DEFAULT) || check(TokenType::CHECK)) {
    if (match(TokenType::NOT)) {
      consume(TokenType::NULL_KW, "Expected NULL");
      not_null = true;
    } else if (match(TokenType::PRIMARY)) {
      consume(TokenType::KEY, "Expected KEY");
      primary_key = true;
    } else if (match(TokenType::UNIQUE)) {
      unique = true;
    } else if (match(TokenType::DEFAULT)) {
      if (match(TokenType::NULL_KW)) {
        default_value = Value();
      } else if (check(TokenType::NUMBER)) {
        std::string num_str = advance().get_lexeme();
        if (num_str.find('.') != std::string::npos) {
          default_value = Value(std::stod(num_str));
        } else {
          default_value = Value(static_cast<int64_t>(std::stoll(num_str)));
        }
      } else if (check(TokenType::STRING)) {
        default_value = Value(advance().get_lexeme());
      } else {
        throw ParseException(
            get_error_message("Expected literal after DEFAULT"));
      }
    } else if (match(TokenType::CHECK)) {
      consume(TokenType::LPAREN, "Expected (");
      check_expression = parse_expression();
      consume(TokenType::RPAREN, "Expected )");
    } else if (match(TokenType::REFERENCES)) {
      ref_table =
          consume(TokenType::IDENTIFIER, "Expected parent table").get_lexeme();
      consume(TokenType::LPAREN, "Expected (");
      ref_column =
          consume(TokenType::IDENTIFIER, "Expected parent column").get_lexeme();
      consume(TokenType::RPAREN, "Expected )");
      parse_referential_actions(on_delete, on_update);
    } else {
      break;
    }
  }
  ColumnDefinition col(name, type, not_null, primary_key, unique);
  if (default_value) {
    col.set_default_value(*default_value);
  }
  if (!ref_table.empty()) {
    col.set_references(ref_table, ref_column, on_delete, on_update);
  }
  if (check_expression) {
    col.set_check_expression(check_expression);
  }
  return col;
}

ReferentialAction Parser::parse_referential_action() {
  if (match(TokenType::CASCADE)) {
    return ReferentialAction::Cascade;
  }
  if (match(TokenType::RESTRICT)) {
    return ReferentialAction::Restrict;
  }
  if (match(TokenType::SET)) {
    if (match(TokenType::NULL_KW)) {
      return ReferentialAction::SetNull;
    }
    if (match(TokenType::DEFAULT)) {
      return ReferentialAction::SetDefault;
    }
    throw ParseException(
        get_error_message("Expected NULL or DEFAULT after SET"));
  }
  throw ParseException(get_error_message(
      "Expected CASCADE, RESTRICT, SET NULL, or SET DEFAULT"));
}

void Parser::parse_referential_actions(ReferentialAction &on_delete,
                                       ReferentialAction &on_update) {
  while (match(TokenType::ON)) {
    if (match(TokenType::DELETE)) {
      on_delete = parse_referential_action();
    } else if (match(TokenType::UPDATE)) {
      on_update = parse_referential_action();
    } else {
      throw ParseException(get_error_message("Expected DELETE or UPDATE after ON"));
    }
  }
}

ForeignKeyDefinition Parser::parse_table_foreign_key() {
  consume(TokenType::FOREIGN, "Expected FOREIGN");
  consume(TokenType::KEY, "Expected KEY");
  consume(TokenType::LPAREN, "Expected (");
  ForeignKeyDefinition fk;
  do {
    fk.child_columns.push_back(
        consume(TokenType::IDENTIFIER, "Expected column name").get_lexeme());
  } while (match(TokenType::COMMA));
  consume(TokenType::RPAREN, "Expected )");
  consume(TokenType::REFERENCES, "Expected REFERENCES");
  fk.parent_table =
      consume(TokenType::IDENTIFIER, "Expected parent table").get_lexeme();
  consume(TokenType::LPAREN, "Expected (");
  do {
    fk.parent_columns.push_back(
        consume(TokenType::IDENTIFIER, "Expected parent column").get_lexeme());
  } while (match(TokenType::COMMA));
  consume(TokenType::RPAREN, "Expected )");
  if (fk.child_columns.size() != fk.parent_columns.size()) {
    throw ParseException(get_error_message(
        "FOREIGN KEY child and parent column counts must match"));
  }
  parse_referential_actions(fk.on_delete, fk.on_update);
  return fk;
}

CheckConstraintDefinition Parser::parse_table_check_constraint(
    const std::string &constraint_name) {
  consume(TokenType::CHECK, "Expected CHECK");
  consume(TokenType::LPAREN, "Expected (");
  CheckConstraintDefinition check;
  check.name = constraint_name;
  check.predicate = parse_expression();
  check.expression_text = check.predicate->to_string();
  consume(TokenType::RPAREN, "Expected )");
  return check;
}

std::shared_ptr<DropTableStatement> Parser::parse_drop_table_statement() {
  consume(TokenType::DROP, "Expected DROP");
  consume(TokenType::TABLE, "Expected TABLE");
  std::string table =
      consume(TokenType::IDENTIFIER, "Expected table name").get_lexeme();
  match(TokenType::SEMICOLON);
  return std::make_shared<DropTableStatement>(table);
}

std::shared_ptr<DropIndexStatement> Parser::parse_drop_index_statement() {
  consume(TokenType::DROP, "Expected DROP");
  consume(TokenType::INDEX, "Expected INDEX");
  std::string index_name =
      consume(TokenType::IDENTIFIER, "Expected index name").get_lexeme();
  match(TokenType::SEMICOLON);
  return std::make_shared<DropIndexStatement>(index_name);
}

std::shared_ptr<BeginStatement> Parser::parse_begin_statement() {
  consume(TokenType::BEGIN, "Expected BEGIN");
  match(TokenType::SEMICOLON);
  return std::make_shared<BeginStatement>();
}

std::shared_ptr<CommitStatement> Parser::parse_commit_statement() {
  consume(TokenType::COMMIT, "Expected COMMIT");
  match(TokenType::SEMICOLON);
  return std::make_shared<CommitStatement>();
}

std::shared_ptr<RollbackStatement> Parser::parse_rollback_statement() {
  consume(TokenType::ROLLBACK, "Expected ROLLBACK");
  match(TokenType::SEMICOLON);
  return std::make_shared<RollbackStatement>();
}

std::shared_ptr<PrepareStatement> Parser::parse_prepare_statement() {
  consume(TokenType::PREPARE, "Expected PREPARE");
  std::string name =
      consume(TokenType::IDENTIFIER, "Expected statement name").get_lexeme();
  consume(TokenType::AS, "Expected AS");
  if (original_sql_.empty()) {
    throw ParseException("PREPARE requires original SQL text");
  }
  std::string sql = extract_sql_after_as(original_sql_);
  while (current_ < tokens_.size() &&
         tokens_[current_].get_type() != TokenType::END_OF_INPUT) {
    advance();
  }
  return std::make_shared<PrepareStatement>(name, sql);
}

std::shared_ptr<ExecutePreparedStatement> Parser::parse_execute_statement() {
  consume(TokenType::EXECUTE, "Expected EXECUTE");
  std::string name =
      consume(TokenType::IDENTIFIER, "Expected statement name").get_lexeme();
  std::vector<Value> arguments;
  if (match(TokenType::LPAREN)) {
    if (!check(TokenType::RPAREN)) {
      do {
        if (match(TokenType::NULL_KW)) {
          arguments.emplace_back();
        } else if (check(TokenType::NUMBER)) {
          std::string numStr = advance().get_lexeme();
          if (numStr.find('.') != std::string::npos) {
            arguments.emplace_back(std::stod(numStr));
          } else {
            arguments.emplace_back(static_cast<int64_t>(std::stoll(numStr)));
          }
        } else if (check(TokenType::STRING)) {
          arguments.emplace_back(advance().get_lexeme());
        } else {
          throw ParseException(
              get_error_message("Expected literal bind argument"));
        }
      } while (match(TokenType::COMMA));
    }
    consume(TokenType::RPAREN, "Expected )");
  }
  match(TokenType::SEMICOLON);
  return std::make_shared<ExecutePreparedStatement>(name, std::move(arguments));
}

std::shared_ptr<DeallocatePreparedStatement>
Parser::parse_deallocate_statement() {
  consume(TokenType::DEALLOCATE, "Expected DEALLOCATE");
  match(TokenType::PREPARE);
  std::string name =
      consume(TokenType::IDENTIFIER, "Expected statement name").get_lexeme();
  match(TokenType::SEMICOLON);
  return std::make_shared<DeallocatePreparedStatement>(name);
}

std::shared_ptr<VacuumStatement> Parser::parse_vacuum_statement() {
  consume(TokenType::VACUUM, "Expected VACUUM");
  std::string table_name;
  if (check(TokenType::IDENTIFIER)) {
    table_name = advance().get_lexeme();
  }
  match(TokenType::SEMICOLON);
  return std::make_shared<VacuumStatement>(table_name);
}

std::shared_ptr<AlterTableStatement> Parser::parse_alter_table_statement() {
  consume(TokenType::ALTER, "Expected ALTER");
  consume(TokenType::TABLE, "Expected TABLE");
  std::string table =
      consume(TokenType::IDENTIFIER, "Expected table name").get_lexeme();
  auto stmt = std::make_shared<AlterTableStatement>(table);
  AlterTableAction action;
  if (match(TokenType::ADD)) {
    if (match(TokenType::COLUMN)) {
      action.type = AlterTableActionType::AddColumn;
      action.column_def = parse_column_definition();
    } else if (match(TokenType::CONSTRAINT)) {
      action.check_name =
          consume(TokenType::IDENTIFIER, "Expected constraint name")
              .get_lexeme();
      consume(TokenType::CHECK, "Expected CHECK");
      consume(TokenType::LPAREN, "Expected (");
      action.type = AlterTableActionType::AddCheck;
      action.check_expression = parse_expression();
      consume(TokenType::RPAREN, "Expected )");
    } else if (match(TokenType::CHECK)) {
      consume(TokenType::LPAREN, "Expected (");
      action.type = AlterTableActionType::AddCheck;
      action.check_expression = parse_expression();
      consume(TokenType::RPAREN, "Expected )");
    } else if (match(TokenType::PRIMARY)) {
      consume(TokenType::KEY, "Expected KEY");
      consume(TokenType::LPAREN, "Expected (");
      action.type = AlterTableActionType::AddPrimaryKey;
      do {
        action.columns.push_back(
            consume(TokenType::IDENTIFIER, "Expected column name").get_lexeme());
      } while (match(TokenType::COMMA));
      consume(TokenType::RPAREN, "Expected )");
      if (action.columns.empty()) {
        throw ParseException(
            get_error_message("PRIMARY KEY must list at least one column"));
      }
      action.name = action.columns[0];
    } else if (match(TokenType::UNIQUE)) {
      consume(TokenType::LPAREN, "Expected (");
      action.type = AlterTableActionType::AddUnique;
      do {
        action.columns.push_back(
            consume(TokenType::IDENTIFIER, "Expected column name").get_lexeme());
      } while (match(TokenType::COMMA));
      consume(TokenType::RPAREN, "Expected )");
      if (action.columns.empty()) {
        throw ParseException(
            get_error_message("UNIQUE must list at least one column"));
      }
      action.name = action.columns[0];
    } else {
      throw ParseException(get_error_message(
          "Expected COLUMN, CHECK, PRIMARY KEY, or UNIQUE after ADD"));
    }
  } else if (match(TokenType::DROP)) {
    if (match(TokenType::COLUMN)) {
      action.type = AlterTableActionType::DropColumn;
      action.name =
          consume(TokenType::IDENTIFIER, "Expected column name").get_lexeme();
    } else if (match(TokenType::CHECK)) {
      action.type = AlterTableActionType::DropCheck;
      action.name =
          consume(TokenType::IDENTIFIER, "Expected CHECK constraint name")
              .get_lexeme();
    } else if (match(TokenType::PRIMARY)) {
      consume(TokenType::KEY, "Expected KEY");
      action.type = AlterTableActionType::DropPrimaryKey;
    } else if (match(TokenType::UNIQUE)) {
      consume(TokenType::LPAREN, "Expected (");
      action.type = AlterTableActionType::DropUnique;
      do {
        action.columns.push_back(
            consume(TokenType::IDENTIFIER, "Expected column name").get_lexeme());
      } while (match(TokenType::COMMA));
      consume(TokenType::RPAREN, "Expected )");
      if (action.columns.empty()) {
        throw ParseException(
            get_error_message("UNIQUE must list at least one column"));
      }
      action.name = action.columns[0];
    } else {
      throw ParseException(get_error_message(
          "Expected COLUMN, CHECK, PRIMARY KEY, or UNIQUE after DROP"));
    }
  } else if (match(TokenType::RENAME)) {
    if (match(TokenType::TO)) {
      action.type = AlterTableActionType::RenameTable;
      action.new_name =
          consume(TokenType::IDENTIFIER, "Expected new table name").get_lexeme();
    } else if (match(TokenType::COLUMN)) {
      action.type = AlterTableActionType::RenameColumn;
      action.name =
          consume(TokenType::IDENTIFIER, "Expected column name").get_lexeme();
      consume(TokenType::TO, "Expected TO");
      action.new_name =
          consume(TokenType::IDENTIFIER, "Expected new column name").get_lexeme();
    } else {
      throw ParseException(get_error_message("Expected TO or COLUMN after RENAME"));
    }
  } else if (match(TokenType::ALTER)) {
    consume(TokenType::COLUMN, "Expected COLUMN");
    action.name =
        consume(TokenType::IDENTIFIER, "Expected column name").get_lexeme();
    if (match(TokenType::SET)) {
      consume(TokenType::NOT, "Expected NOT");
      consume(TokenType::NULL_KW, "Expected NULL");
      action.type = AlterTableActionType::SetNotNull;
    } else if (match(TokenType::DROP)) {
      consume(TokenType::NOT, "Expected NOT");
      consume(TokenType::NULL_KW, "Expected NULL");
      action.type = AlterTableActionType::DropNotNull;
    } else {
      throw ParseException(
          get_error_message("Expected SET NOT NULL or DROP NOT NULL"));
    }
  } else {
    throw ParseException(get_error_message("Invalid ALTER TABLE action"));
  }
  stmt->set_action(action);
  match(TokenType::SEMICOLON);
  return stmt;
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
    if (match(TokenType::EXISTS)) {
      consume(TokenType::LPAREN, "Expected ( after EXISTS");
      auto select = parse_select_statement();
      consume(TokenType::RPAREN, "Expected ) after EXISTS subquery");
      return std::make_shared<ExistsExpression>(select, true);
    }
    auto expr = parse_not_expression();
    return std::make_shared<UnaryOpExpression>(UnaryOpExpression::Operator::NOT,
                                               expr);
  }
  return parse_comparison_expression();
}

ExpressionPtr Parser::parse_comparison_expression() {
  auto expr = parse_additive_expression();
  if (match(TokenType::BETWEEN)) {
    auto lower = parse_additive_expression();
    consume(TokenType::AND, "Expected AND in BETWEEN");
    auto upper = parse_additive_expression();
    auto ge = std::make_shared<BinaryOpExpression>(
        expr, BinaryOpExpression::Operator::GE, lower);
    auto le = std::make_shared<BinaryOpExpression>(
        expr, BinaryOpExpression::Operator::LE, upper);
    return std::make_shared<BinaryOpExpression>(
        ge, BinaryOpExpression::Operator::AND, le);
  }
  if (match(TokenType::IN)) {
    return parse_in_predicate(expr, false);
  }
  if (match(TokenType::NOT) && check(TokenType::IN)) {
    advance();
    return parse_in_predicate(expr, true);
  }
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

ExpressionPtr Parser::parse_in_predicate(ExpressionPtr left, bool is_not) {
  consume(TokenType::LPAREN, "Expected ( after IN");
  if (check(TokenType::SELECT)) {
    auto select = parse_select_statement();
    consume(TokenType::RPAREN, "Expected ) after subquery");
    return std::make_shared<InExpression>(
        std::move(left), std::make_shared<SubqueryExpression>(select), is_not);
  }
  std::vector<ExpressionPtr> values;
  do {
    values.push_back(parse_expression());
  } while (match(TokenType::COMMA));
  consume(TokenType::RPAREN, "Expected ) after IN list");
  if (values.empty()) {
    throw ParseException(get_error_message("IN list must not be empty"));
  }
  return std::make_shared<InExpression>(std::move(left), std::move(values),
                                        is_not);
}

ExpressionPtr Parser::parse_parameter_expression() {
  Token token = advance();
  const std::string &lexeme = token.get_lexeme();
  if (lexeme == "?") {
    if (parameter_style_ == ParameterStyle::Dollar) {
      throw ParseException(
          get_error_message("Cannot mix ? and $n bind parameters"));
    }
    parameter_style_ = ParameterStyle::Question;
    return std::make_shared<ParameterExpression>(next_parameter_index_++);
  }
  if (lexeme.size() < 2 || lexeme[0] != '$') {
    throw ParseException(get_error_message("Invalid bind parameter"));
  }
  if (parameter_style_ == ParameterStyle::Question) {
    throw ParseException(
        get_error_message("Cannot mix ? and $n bind parameters"));
  }
  parameter_style_ = ParameterStyle::Dollar;
  size_t one_based = static_cast<size_t>(std::stoull(lexeme.substr(1)));
  if (one_based == 0) {
    throw ParseException(get_error_message("Bind parameters start at $1"));
  }
  dollar_indices_.insert(one_based);
  if (one_based > max_dollar_index_) {
    max_dollar_index_ = one_based;
  }
  return std::make_shared<ParameterExpression>(one_based - 1);
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
    if (match(TokenType::MULTIPLY) || match(TokenType::ASTERISK)) {
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
  if (check(TokenType::PARAMETER)) {
    return parse_parameter_expression();
  }
  if (match(TokenType::EXISTS)) {
    consume(TokenType::LPAREN, "Expected ( after EXISTS");
    auto select = parse_select_statement();
    consume(TokenType::RPAREN, "Expected ) after EXISTS subquery");
    return std::make_shared<ExistsExpression>(select, false);
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
    if (check(TokenType::SELECT)) {
      auto select = parse_select_statement();
      consume(TokenType::RPAREN, "Expected )");
      return std::make_shared<SubqueryExpression>(select);
    }
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
  std::string upper_name = name;
  for (char &c : upper_name) {
    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  }
  if (upper_name == "CAST" && match(TokenType::LPAREN)) {
    ExpressionPtr inner = parse_expression();
    consume(TokenType::AS, "Expected AS in CAST");
    std::string type_name =
        consume(TokenType::IDENTIFIER, "Expected type in CAST").get_lexeme();
    consume(TokenType::RPAREN, "Expected ) after CAST");
    try {
      return std::make_shared<CastExpression>(
          inner, string_to_data_type(type_name));
    } catch (const std::exception &ex) {
      throw ParseException(get_error_message(ex.what()));
    }
  }
  if (upper_name == "CURRENT_DATE" && !check(TokenType::LPAREN)) {
    return std::make_shared<FunctionCallExpression>("current_date",
                                                    std::vector<ExpressionPtr>{});
  }
  if (check(TokenType::LPAREN)) {
    advance();
    std::vector<ExpressionPtr> args;
    if (!check(TokenType::RPAREN)) {
      do {
        args.push_back(parse_expression());
      } while (match(TokenType::COMMA));
    }
    consume(TokenType::RPAREN, "Expected )");
    auto fn = std::make_shared<FunctionCallExpression>(name, args);
    if (match(TokenType::OVER)) {
      fn->set_window_spec(parse_window_spec());
    }
    return fn;
  }
  if (match(TokenType::DOT)) {
    std::string column = advance().get_lexeme();
    return std::make_shared<ColumnRefExpression>(name, column);
  }
  if (name == "*") {
    return std::make_shared<IdentifierExpression>("*");
  }
  return std::make_shared<ColumnRefExpression>(name);
}

std::shared_ptr<WindowSpec> Parser::parse_window_spec() {
  consume(TokenType::LPAREN, "Expected ( after OVER");
  auto spec = std::make_shared<WindowSpec>();
  if (match(TokenType::PARTITION)) {
    consume(TokenType::BY, "Expected BY after PARTITION");
    do {
      spec->add_partition_by(parse_expression());
    } while (match(TokenType::COMMA));
  }
  if (!match(TokenType::ORDER)) {
    throw ParseException(
        get_error_message("Window OVER requires ORDER BY in v1"));
  }
  consume(TokenType::BY, "Expected BY after ORDER");
  do {
    ExpressionPtr expr = parse_expression();
    bool ascending = !match(TokenType::DESC);
    if (ascending) {
      match(TokenType::ASC);
    }
    spec->add_order_by(expr, ascending);
  } while (match(TokenType::COMMA));
  consume(TokenType::RPAREN, "Expected ) after window specification");
  return spec;
}

std::string Parser::get_error_message(const std::string &message) const {
  std::string msg = message + " at line " + std::to_string(peek().get_line()) +
                    ", column " + std::to_string(peek().get_column()) +
                    " (token: '" + peek().get_lexeme() + "')";
  return msg;
}

}  // namespace db
