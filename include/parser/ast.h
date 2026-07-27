#pragma once

#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "core/check_constraint.h"
#include "core/foreign_key.h"
#include "types/value.h"
#include "utils/exceptions.h"

namespace db {

// Forward declarations
class Expression;
class SelectStatement;
class InsertStatement;
class UpdateStatement;
class DeleteStatement;
class CreateTableStatement;

using ExpressionPtr = std::shared_ptr<Expression>;
using StatementPtr = std::shared_ptr<void>;

// ==================== Expressions ====================

class Expression {
 public:
  virtual ~Expression() = default;
  virtual std::string to_string() const = 0;
};

class LiteralExpression : public Expression {
 public:
  explicit LiteralExpression(const Value &val);
  const Value &get_value() const;
  std::string to_string() const override;

 private:
  Value value_;
};

class IdentifierExpression : public Expression {
 public:
  explicit IdentifierExpression(const std::string &name);
  const std::string &get_name() const;
  std::string to_string() const override;

 private:
  std::string name_;
};

class ColumnRefExpression : public Expression {
 public:
  ColumnRefExpression(const std::string &table, const std::string &column);
  explicit ColumnRefExpression(const std::string &column);
  const std::string &get_table() const;
  const std::string &get_column() const;
  std::string to_string() const override;

 private:
  std::string table_name_;
  std::string column_name_;
};

/** Positional bind parameter (`?`) with 0-based ordinal. */
class ParameterExpression : public Expression {
 public:
  explicit ParameterExpression(size_t index);
  size_t get_index() const;
  std::string to_string() const override;

 private:
  size_t index_;
};

class BinaryOpExpression : public Expression {
 public:
  enum class Operator {
    EQ,
    NE,
    LT,
    LE,
    GT,
    GE,
    AND,
    OR,
    PLUS,
    MINUS,
    MUL,
    DIV,
    MOD
  };

  BinaryOpExpression(ExpressionPtr left, Operator op, ExpressionPtr right);
  const ExpressionPtr &get_left() const;
  const ExpressionPtr &get_right() const;
  Operator get_operator() const;
  std::string to_string() const override;

 private:
  ExpressionPtr left_;
  ExpressionPtr right_;
  Operator op_;
};

class UnaryOpExpression : public Expression {
 public:
  enum class Operator { NOT, MINUS };

  UnaryOpExpression(Operator op, ExpressionPtr expr);
  Operator get_operator() const;
  const ExpressionPtr &get_expression() const;
  std::string to_string() const override;

 private:
  Operator op_;
  ExpressionPtr expr_;
};

class FunctionCallExpression : public Expression {
 public:
  FunctionCallExpression(const std::string &name,
                         std::vector<ExpressionPtr> args);
  const std::string &get_function_name() const;
  const std::vector<ExpressionPtr> &get_arguments() const;
  std::string to_string() const override;

 private:
  std::string name_;
  std::vector<ExpressionPtr> args_;
};

class CaseExpression : public Expression {
 public:
  void add_when_then(ExpressionPtr when, ExpressionPtr then);
  void set_else(ExpressionPtr else_expr);
  const std::vector<std::pair<ExpressionPtr, ExpressionPtr>> &
  get_when_then_pairs() const;
  const ExpressionPtr &get_else_expression() const;
  std::string to_string() const override;

 private:
  std::vector<std::pair<ExpressionPtr, ExpressionPtr>> when_then_pairs_;
  ExpressionPtr else_expr_;
};

// ==================== Query Statements ====================

class SelectStatement {
 public:
  SelectStatement();

  void add_select_column(ExpressionPtr expr, const std::string &alias = "");
  void set_from_table(const std::string &table, const std::string &alias = "");
  void set_where_condition(ExpressionPtr expr);
  void add_group_by_column(ExpressionPtr expr);
  void set_having_condition(ExpressionPtr expr);
  void add_order_by_column(ExpressionPtr expr, bool ascending = true);
  void add_join(const std::string &type, const std::string &table,
                const std::string &alias, ExpressionPtr condition);
  /** Replaces only the table name of an existing JOIN entry. */
  void set_join_table(size_t index, const std::string &table);
  void set_distinct(bool distinct);
  void set_limit(int limit);
  void set_offset(int offset);

  const std::vector<std::pair<ExpressionPtr, std::string>> &get_select_columns()
      const;
  const std::string &get_from_table() const;
  const std::string &get_from_alias() const;
  const ExpressionPtr &get_where_condition() const;
  const std::vector<ExpressionPtr> &get_group_by_columns() const;
  const ExpressionPtr &get_having_condition() const;
  const std::vector<std::pair<ExpressionPtr, bool>> &get_order_by_columns()
      const;
  const std::vector<
      std::tuple<std::string, std::string, std::string, ExpressionPtr>> &
  get_joins() const;
  bool is_distinct() const;
  int get_limit() const;
  int get_offset() const;

  std::string to_string() const;

 private:
  std::vector<std::pair<ExpressionPtr, std::string>> select_columns_;
  std::string from_table_;
  std::string from_alias_;
  ExpressionPtr where_condition_;
  std::vector<ExpressionPtr> group_by_columns_;
  ExpressionPtr having_condition_;
  std::vector<std::pair<ExpressionPtr, bool>> order_by_columns_;
  std::vector<std::tuple<std::string, std::string, std::string, ExpressionPtr>>
      joins_;
  bool distinct_;
  int limit_;
  int offset_;
};

class InsertStatement {
 public:
  explicit InsertStatement(const std::string &table);

  const std::string &get_table() const;
  void add_column(const std::string &col);
  void add_values(const std::vector<Value> &vals);

  const std::vector<std::string> &get_columns() const;
  const std::vector<std::vector<Value>> &get_values() const;

  std::string to_string() const;

 private:
  std::string table_;
  std::vector<std::string> columns_;
  std::vector<std::vector<Value>> values_;
};

class UpdateStatement {
 public:
  explicit UpdateStatement(const std::string &table);

  const std::string &get_table() const;
  void add_set_clause(const std::string &column, ExpressionPtr value);
  void set_where_condition(ExpressionPtr expr);

  const std::vector<std::pair<std::string, ExpressionPtr>> &get_set_clauses()
      const;
  const ExpressionPtr &get_where_condition() const;

  std::string to_string() const;

 private:
  std::string table_;
  std::vector<std::pair<std::string, ExpressionPtr>> set_clauses_;
  ExpressionPtr where_condition_;
};

class DeleteStatement {
 public:
  explicit DeleteStatement(const std::string &table);

  const std::string &get_table() const;
  void set_where_condition(ExpressionPtr expr);
  const ExpressionPtr &get_where_condition() const;

  std::string to_string() const;

 private:
  std::string table_;
  ExpressionPtr where_condition_;
};

class ColumnDefinition {
 public:
  ColumnDefinition(const std::string &name, const std::string &type,
                   bool not_null = false, bool primary_key = false,
                   bool unique = false);

  const std::string &get_name() const;
  const std::string &get_type() const;
  bool is_not_null() const;
  bool is_primary_key() const;
  bool is_unique() const;
  void set_default_value(const Value &value);
  bool has_default() const;
  const Value &get_default_value() const;
  void set_references(const std::string &parent_table,
                      const std::string &parent_column,
                      ReferentialAction on_delete = ReferentialAction::Restrict,
                      ReferentialAction on_update = ReferentialAction::Restrict);
  bool has_references() const;
  const std::string &get_references_table() const;
  const std::string &get_references_column() const;
  ReferentialAction get_on_delete() const;
  ReferentialAction get_on_update() const;
  void set_check_expression(ExpressionPtr expr);
  bool has_check_expression() const;
  const ExpressionPtr &get_check_expression() const;

  std::string to_string() const;

 private:
  std::string name_;
  std::string type_;
  bool not_null_;
  bool primary_key_;
  bool unique_;
  std::optional<Value> default_value_;
  std::string references_table_;
  std::string references_column_;
  ReferentialAction on_delete_{ReferentialAction::Restrict};
  ReferentialAction on_update_{ReferentialAction::Restrict};
  ExpressionPtr check_expression_;
};

class CreateTableStatement {
 public:
  explicit CreateTableStatement(const std::string &table);

  const std::string &get_table_name() const;
  void add_column(const ColumnDefinition &col);
  void add_foreign_key(const ForeignKeyDefinition &fk);
  void add_check(const CheckConstraintDefinition &check);
  const std::vector<ColumnDefinition> &get_columns() const;
  const std::vector<ForeignKeyDefinition> &get_foreign_keys() const;
  const std::vector<CheckConstraintDefinition> &get_checks() const;

  std::string to_string() const;

 private:
  std::string table_name_;
  std::vector<ColumnDefinition> columns_;
  std::vector<ForeignKeyDefinition> foreign_keys_;
  std::vector<CheckConstraintDefinition> checks_;
};

class DropTableStatement {
 public:
  explicit DropTableStatement(const std::string &table);
  const std::string &get_table_name() const;
  std::string to_string() const;

 private:
  std::string table_name_;
};

enum class AlterTableActionType {
  AddColumn,
  DropColumn,
  RenameTable,
  RenameColumn,
  AddPrimaryKey,
  AddUnique,
  SetNotNull,
  DropPrimaryKey,
  DropUnique,
  DropNotNull,
  AddCheck,
  DropCheck
};

struct AlterTableAction {
  AlterTableActionType type;
  ColumnDefinition column_def{"", ""};
  std::string name;
  std::string new_name;
  std::string check_name;
  ExpressionPtr check_expression;
};

class AlterTableStatement {
 public:
  explicit AlterTableStatement(const std::string &table);
  const std::string &get_table_name() const;
  void set_action(const AlterTableAction &action);
  const AlterTableAction &get_action() const;
  std::string to_string() const;

 private:
  std::string table_name_;
  AlterTableAction action_{};
};

class CreateIndexStatement {
 public:
  CreateIndexStatement(const std::string &index_name,
                       const std::string &table_name,
                       std::vector<std::string> column_names);
  const std::string &get_index_name() const;
  const std::string &get_table_name() const;
  const std::vector<std::string> &get_column_names() const;
  std::string to_string() const;

 private:
  std::string index_name_;
  std::string table_name_;
  std::vector<std::string> column_names_;
};

class DropIndexStatement {
 public:
  explicit DropIndexStatement(const std::string &index_name);
  const std::string &get_index_name() const;
  std::string to_string() const;

 private:
  std::string index_name_;
};

/** CREATE VIEW name AS <select>. */
class CreateViewStatement {
 public:
  CreateViewStatement(const std::string &view_name, const std::string &select_sql,
                      std::shared_ptr<SelectStatement> select);
  const std::string &get_view_name() const;
  const std::string &get_select_sql() const;
  const std::shared_ptr<SelectStatement> &get_select() const;
  std::string to_string() const;

 private:
  std::string view_name_;
  std::string select_sql_;
  std::shared_ptr<SelectStatement> select_;
};

/** DROP VIEW [IF EXISTS] name. */
class DropViewStatement {
 public:
  DropViewStatement(const std::string &view_name, bool if_exists = false);
  const std::string &get_view_name() const;
  bool is_if_exists() const;
  std::string to_string() const;

 private:
  std::string view_name_;
  bool if_exists_{false};
};

class BeginStatement {
 public:
  std::string to_string() const;
};

class CommitStatement {
 public:
  std::string to_string() const;
};

class RollbackStatement {
 public:
  std::string to_string() const;
};

class PrepareStatement {
 public:
  PrepareStatement(const std::string &name, const std::string &sql);
  const std::string &get_name() const;
  const std::string &get_sql() const;
  std::string to_string() const;

 private:
  std::string name_;
  std::string sql_;
};

class ExecutePreparedStatement {
 public:
  explicit ExecutePreparedStatement(const std::string &name,
                                    std::vector<Value> arguments = {});
  const std::string &get_name() const;
  const std::vector<Value> &get_arguments() const;
  std::string to_string() const;

 private:
  std::string name_;
  std::vector<Value> arguments_;
};

class DeallocatePreparedStatement {
 public:
  explicit DeallocatePreparedStatement(const std::string &name);
  const std::string &get_name() const;
  std::string to_string() const;

 private:
  std::string name_;
};

class VacuumStatement {
 public:
  VacuumStatement() = default;
  explicit VacuumStatement(const std::string &table_name);
  const std::string &get_table_name() const;
  std::string to_string() const;

 private:
  std::string table_name_;
};

class InExpression : public Expression {
 public:
  InExpression(ExpressionPtr left, ExpressionPtr subquery, bool is_not = false);
  InExpression(ExpressionPtr left, std::vector<ExpressionPtr> values,
               bool is_not = false);
  const ExpressionPtr &get_left() const;
  bool is_not() const;
  bool has_subquery() const;
  const ExpressionPtr &get_subquery() const;
  const std::vector<ExpressionPtr> &get_values() const;
  std::string to_string() const override;

 private:
  ExpressionPtr left_;
  ExpressionPtr subquery_;
  std::vector<ExpressionPtr> values_;
  bool is_not_{false};
};

class ExistsExpression : public Expression {
 public:
  ExistsExpression(std::shared_ptr<SelectStatement> select,
                   bool is_not = false);
  const std::shared_ptr<SelectStatement> &get_select() const;
  bool is_not() const;
  std::string to_string() const override;

 private:
  std::shared_ptr<SelectStatement> select_;
  bool is_not_{false};
};

class SubqueryExpression : public Expression {
 public:
  explicit SubqueryExpression(std::shared_ptr<SelectStatement> select);
  const std::shared_ptr<SelectStatement> &get_select() const;
  std::string to_string() const override;

 private:
  std::shared_ptr<SelectStatement> select_;
};

}  // namespace db
