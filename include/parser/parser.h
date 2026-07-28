#pragma once

#include <memory>
#include <string>
#include <unordered_set>
#include <variant>
#include <vector>

#include "parser/ast.h"
#include "parser/lexer.h"
#include "parser/token.h"
#include "utils/exceptions.h"

namespace db {

class ExplainStatement;

using ParsedStatement = std::variant<
    std::shared_ptr<SelectStatement>,
    std::shared_ptr<SetOperationStatement>, std::shared_ptr<InsertStatement>,
    std::shared_ptr<UpdateStatement>, std::shared_ptr<DeleteStatement>,
    std::shared_ptr<CreateTableStatement>, std::shared_ptr<DropTableStatement>,
    std::shared_ptr<AlterTableStatement>, std::shared_ptr<CreateIndexStatement>,
    std::shared_ptr<DropIndexStatement>, std::shared_ptr<CreateViewStatement>,
    std::shared_ptr<DropViewStatement>,
    std::shared_ptr<CreateFunctionStatement>,
    std::shared_ptr<DropFunctionStatement>,
    std::shared_ptr<CreateProcedureStatement>,
    std::shared_ptr<DropProcedureStatement>, std::shared_ptr<CallStatement>,
    std::shared_ptr<CreateTriggerStatement>,
    std::shared_ptr<DropTriggerStatement>, std::shared_ptr<SetNewStatement>,
    std::shared_ptr<BeginStatement>, std::shared_ptr<CommitStatement>,
    std::shared_ptr<RollbackStatement>, std::shared_ptr<PrepareStatement>,
    std::shared_ptr<ExecutePreparedStatement>,
    std::shared_ptr<DeallocatePreparedStatement>,
    std::shared_ptr<VacuumStatement>, std::shared_ptr<ExplainStatement>>;

/** Wraps an inner statement for EXPLAIN execution and plan text output. */
class ExplainStatement {
 public:
  explicit ExplainStatement(ParsedStatement inner);
  const ParsedStatement &get_inner() const;
  std::string to_string() const;

 private:
  ParsedStatement inner_;
};

class Parser {
 public:
  explicit Parser(const std::string &sql);
  explicit Parser(std::vector<Token> token_list);

  std::shared_ptr<SelectStatement> parse_select_statement();
  /** Parses SELECT or set-op chain (UNION / INTERSECT / EXCEPT). */
  ParsedStatement parse_query_statement();
  std::shared_ptr<InsertStatement> parse_insert_statement();
  std::shared_ptr<UpdateStatement> parse_update_statement();
  std::shared_ptr<DeleteStatement> parse_delete_statement();
  std::shared_ptr<CreateTableStatement> parse_create_table_statement();
  std::shared_ptr<CreateIndexStatement> parse_create_index_statement();
  std::shared_ptr<CreateViewStatement> parse_create_view_statement();
  std::shared_ptr<CreateFunctionStatement> parse_create_function_statement();
  std::shared_ptr<DropFunctionStatement> parse_drop_function_statement();
  std::shared_ptr<CreateProcedureStatement> parse_create_procedure_statement();
  std::shared_ptr<DropProcedureStatement> parse_drop_procedure_statement();
  std::shared_ptr<CallStatement> parse_call_statement();
  std::shared_ptr<CreateTriggerStatement> parse_create_trigger_statement();
  std::shared_ptr<DropTriggerStatement> parse_drop_trigger_statement();
  std::shared_ptr<SetNewStatement> parse_set_new_statement();
  std::shared_ptr<DropTableStatement> parse_drop_table_statement();
  std::shared_ptr<DropIndexStatement> parse_drop_index_statement();
  std::shared_ptr<DropViewStatement> parse_drop_view_statement();
  std::shared_ptr<AlterTableStatement> parse_alter_table_statement();
  std::shared_ptr<BeginStatement> parse_begin_statement();
  std::shared_ptr<CommitStatement> parse_commit_statement();
  std::shared_ptr<RollbackStatement> parse_rollback_statement();
  std::shared_ptr<PrepareStatement> parse_prepare_statement();
  std::shared_ptr<ExecutePreparedStatement> parse_execute_statement();
  std::shared_ptr<DeallocatePreparedStatement> parse_deallocate_statement();
  std::shared_ptr<VacuumStatement> parse_vacuum_statement();
  std::shared_ptr<ExplainStatement> parse_explain_statement();

  ParsedStatement parse_statement();
  /** Parses a bare expression (used for CHECK persistence round-trip). */
  ExpressionPtr parse_standalone_expression();
  size_t get_parameter_count() const;

 private:
  enum class ParameterStyle { None, Question, Dollar };

  std::vector<Token> tokens_;
  size_t current_{};
  std::string original_sql_;
  ParameterStyle parameter_style_{ParameterStyle::None};
  size_t next_parameter_index_{0};
  size_t max_dollar_index_{0};
  std::unordered_set<size_t> dollar_indices_;

  Token peek() const;
  Token peek_ahead(size_t offset) const;
  Token advance();
  bool check(TokenType type) const;
  bool match(TokenType type);
  bool match(const std::vector<TokenType> &types);
  Token consume(TokenType type, const std::string &message);
  bool is_at_end() const;

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
  std::shared_ptr<WindowSpec> parse_window_spec();
  ExpressionPtr parse_in_predicate(ExpressionPtr left, bool is_not);
  ExpressionPtr parse_parameter_expression();
  void validate_dollar_parameters() const;

  /** Parses function body text: optional RETURN, expression, optional ;. */
  ExpressionPtr parse_function_body_expression();
  std::vector<RoutineParamAst> parse_routine_param_list();
  std::vector<std::string> split_sql_statements(const std::string &body) const;
  std::string trim_original_sql() const;

  ColumnDefinition parse_column_definition();
  ForeignKeyDefinition parse_table_foreign_key();
  CheckConstraintDefinition parse_table_check_constraint(
      const std::string &constraint_name);
  void parse_referential_actions(ReferentialAction &on_delete,
                                 ReferentialAction &on_update);
  ReferentialAction parse_referential_action();
  std::string get_error_message(const std::string &message) const;
  ParsedStatement parse_inner_statement();
  std::shared_ptr<SelectStatement> parse_select_core();
  void parse_order_limit_clauses(SelectStatement &stmt);
  void parse_order_limit_clauses(SetOperationStatement &stmt);
  SetOperationStatement::Operand parse_query_primary();
  SetOperationStatement::Operand parse_query_inner();
  bool match_set_operation(SetOperationKind &kind, bool &isAll);
};

}  // namespace db
