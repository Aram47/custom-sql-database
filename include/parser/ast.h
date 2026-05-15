#pragma once

#include <memory>
#include <string>
#include <variant>
#include <vector>

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

  std::string to_string() const;

 private:
  std::string name_;
  std::string type_;
  bool not_null_;
  bool primary_key_;
  bool unique_;
};

class CreateTableStatement {
 public:
  explicit CreateTableStatement(const std::string &table);

  const std::string &get_table_name() const;
  void add_column(const ColumnDefinition &col);
  const std::vector<ColumnDefinition> &get_columns() const;

  std::string to_string() const;

 private:
  std::string table_name_;
  std::vector<ColumnDefinition> columns_;
};

}  // namespace db
