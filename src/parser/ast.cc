#include "parser/ast.h"

#include <sstream>

namespace db {

// ==================== Expression Implementations ====================

LiteralExpression::LiteralExpression(const Value &val) : value_(val) {}

const Value &LiteralExpression::get_value() const { return value_; }

std::string LiteralExpression::to_string() const { return value_.to_string(); }

IdentifierExpression::IdentifierExpression(const std::string &name)
    : name_(name) {}

const std::string &IdentifierExpression::get_name() const { return name_; }

std::string IdentifierExpression::to_string() const { return name_; }

ColumnRefExpression::ColumnRefExpression(const std::string &table,
                                         const std::string &column)
    : table_name_(table), column_name_(column) {}

ColumnRefExpression::ColumnRefExpression(const std::string &column)
    : table_name_(""), column_name_(column) {}

const std::string &ColumnRefExpression::get_table() const {
  return table_name_;
}

const std::string &ColumnRefExpression::get_column() const {
  return column_name_;
}

std::string ColumnRefExpression::to_string() const {
  if (table_name_.empty()) return column_name_;
  return table_name_ + "." + column_name_;
}

BinaryOpExpression::BinaryOpExpression(ExpressionPtr left, Operator op,
                                       ExpressionPtr right)
    : left_(left), right_(right), op_(op) {}

const ExpressionPtr &BinaryOpExpression::get_left() const { return left_; }

const ExpressionPtr &BinaryOpExpression::get_right() const { return right_; }

BinaryOpExpression::Operator BinaryOpExpression::get_operator() const {
  return op_;
}

std::string BinaryOpExpression::to_string() const {
  std::string opStr;
  switch (op_) {
    case Operator::EQ:
      opStr = "=";
      break;
    case Operator::NE:
      opStr = "!=";
      break;
    case Operator::LT:
      opStr = "<";
      break;
    case Operator::LE:
      opStr = "<=";
      break;
    case Operator::GT:
      opStr = ">";
      break;
    case Operator::GE:
      opStr = ">=";
      break;
    case Operator::AND:
      opStr = "AND";
      break;
    case Operator::OR:
      opStr = "OR";
      break;
    case Operator::PLUS:
      opStr = "+";
      break;
    case Operator::MINUS:
      opStr = "-";
      break;
    case Operator::MUL:
      opStr = "*";
      break;
    case Operator::DIV:
      opStr = "/";
      break;
    case Operator::MOD:
      opStr = "%";
      break;
  }
  return "(" + left_->to_string() + " " + opStr + " " + right_->to_string() +
         ")";
}

UnaryOpExpression::UnaryOpExpression(Operator op, ExpressionPtr expr)
    : op_(op), expr_(expr) {}

UnaryOpExpression::Operator UnaryOpExpression::get_operator() const {
  return op_;
}

const ExpressionPtr &UnaryOpExpression::get_expression() const { return expr_; }

std::string UnaryOpExpression::to_string() const {
  std::string opStr = (op_ == Operator::NOT) ? "NOT" : "-";
  return "(" + opStr + " " + expr_->to_string() + ")";
}

FunctionCallExpression::FunctionCallExpression(const std::string &name,
                                               std::vector<ExpressionPtr> args)
    : name_(name), args_(std::move(args)) {}

const std::string &FunctionCallExpression::get_function_name() const {
  return name_;
}

const std::vector<ExpressionPtr> &FunctionCallExpression::get_arguments()
    const {
  return args_;
}

std::string FunctionCallExpression::to_string() const {
  std::string result = name_ + "(";
  for (size_t i = 0; i < args_.size(); ++i) {
    if (i > 0) result += ", ";
    result += args_[i]->to_string();
  }
  result += ")";
  return result;
}

void CaseExpression::add_when_then(ExpressionPtr when, ExpressionPtr then) {
  when_then_pairs_.push_back({when, then});
}

void CaseExpression::set_else(ExpressionPtr else_expr) {
  else_expr_ = else_expr;
}

const std::vector<std::pair<ExpressionPtr, ExpressionPtr>> &
CaseExpression::get_when_then_pairs() const {
  return when_then_pairs_;
}

const ExpressionPtr &CaseExpression::get_else_expression() const {
  return else_expr_;
}

std::string CaseExpression::to_string() const {
  std::string result = "CASE";
  for (const auto &[when, then] : when_then_pairs_) {
    result += " WHEN " + when->to_string() + " THEN " + then->to_string();
  }
  if (else_expr_) {
    result += " ELSE " + else_expr_->to_string();
  }
  result += " END";
  return result;
}

// ==================== SelectStatement ====================

SelectStatement::SelectStatement() : distinct_(false), limit_(-1), offset_(0) {}

void SelectStatement::add_select_column(ExpressionPtr expr,
                                        const std::string &alias) {
  select_columns_.push_back({expr, alias});
}

void SelectStatement::set_from_table(const std::string &table,
                                     const std::string &alias) {
  from_table_ = table;
  from_alias_ = alias.empty() ? table : alias;
}

void SelectStatement::set_where_condition(ExpressionPtr expr) {
  where_condition_ = expr;
}

void SelectStatement::add_group_by_column(ExpressionPtr expr) {
  group_by_columns_.push_back(expr);
}

void SelectStatement::set_having_condition(ExpressionPtr expr) {
  having_condition_ = expr;
}

void SelectStatement::add_order_by_column(ExpressionPtr expr, bool ascending) {
  order_by_columns_.push_back({expr, ascending});
}

void SelectStatement::add_join(const std::string &type,
                               const std::string &table,
                               const std::string &alias,
                               ExpressionPtr condition) {
  joins_.push_back({type, table, alias, condition});
}

void SelectStatement::set_distinct(bool distinct) { distinct_ = distinct; }

void SelectStatement::set_limit(int limit) { limit_ = limit; }

void SelectStatement::set_offset(int offset) { offset_ = offset; }

const std::vector<std::pair<ExpressionPtr, std::string>> &
SelectStatement::get_select_columns() const {
  return select_columns_;
}

const std::string &SelectStatement::get_from_table() const {
  return from_table_;
}

const std::string &SelectStatement::get_from_alias() const {
  return from_alias_;
}

const ExpressionPtr &SelectStatement::get_where_condition() const {
  return where_condition_;
}

const std::vector<ExpressionPtr> &SelectStatement::get_group_by_columns()
    const {
  return group_by_columns_;
}

const ExpressionPtr &SelectStatement::get_having_condition() const {
  return having_condition_;
}

const std::vector<std::pair<ExpressionPtr, bool>> &
SelectStatement::get_order_by_columns() const {
  return order_by_columns_;
}

const std::vector<
    std::tuple<std::string, std::string, std::string, ExpressionPtr>> &
SelectStatement::get_joins() const {
  return joins_;
}

bool SelectStatement::is_distinct() const { return distinct_; }

int SelectStatement::get_limit() const { return limit_; }

int SelectStatement::get_offset() const { return offset_; }

std::string SelectStatement::to_string() const {
  std::ostringstream oss;
  oss << "SELECT ";
  if (distinct_) oss << "DISTINCT ";

  for (size_t i = 0; i < select_columns_.size(); ++i) {
    if (i > 0) oss << ", ";
    oss << select_columns_[i].first->to_string();
    if (!select_columns_[i].second.empty()) {
      oss << " AS " << select_columns_[i].second;
    }
  }

  if (!from_table_.empty()) {
    oss << " FROM " << from_table_;
    if (from_alias_ != from_table_) oss << " " << from_alias_;
  }

  for (const auto &[type, tname, alias, condition] : joins_) {
    if (type == "CROSS") {
      oss << " CROSS JOIN " << tname;
    } else if (type == "FULL") {
      oss << " FULL JOIN " << tname;
    } else {
      oss << " " << type << " JOIN " << tname;
    }
    if (alias != tname) oss << " " << alias;
    if (condition) oss << " ON " << condition->to_string();
  }

  if (where_condition_) {
    oss << " WHERE " << where_condition_->to_string();
  }

  if (!group_by_columns_.empty()) {
    oss << " GROUP BY ";
    for (size_t i = 0; i < group_by_columns_.size(); ++i) {
      if (i > 0) oss << ", ";
      oss << group_by_columns_[i]->to_string();
    }
  }

  if (having_condition_) {
    oss << " HAVING " << having_condition_->to_string();
  }

  if (!order_by_columns_.empty()) {
    oss << " ORDER BY ";
    for (size_t i = 0; i < order_by_columns_.size(); ++i) {
      if (i > 0) oss << ", ";
      oss << order_by_columns_[i].first->to_string();
      oss << (order_by_columns_[i].second ? " ASC" : " DESC");
    }
  }

  if (limit_ > 0) {
    oss << " LIMIT " << limit_;
  }

  if (offset_ > 0) {
    oss << " OFFSET " << offset_;
  }

  return oss.str();
}

// ==================== InsertStatement ====================

InsertStatement::InsertStatement(const std::string &table) : table_(table) {}

const std::string &InsertStatement::get_table() const { return table_; }

void InsertStatement::add_column(const std::string &col) {
  columns_.push_back(col);
}

void InsertStatement::add_values(const std::vector<Value> &vals) {
  values_.push_back(vals);
}

const std::vector<std::string> &InsertStatement::get_columns() const {
  return columns_;
}

const std::vector<std::vector<Value>> &InsertStatement::get_values() const {
  return values_;
}

std::string InsertStatement::to_string() const {
  std::ostringstream oss;
  oss << "INSERT INTO " << table_ << " (";
  for (size_t i = 0; i < columns_.size(); ++i) {
    if (i > 0) oss << ", ";
    oss << columns_[i];
  }
  oss << ") VALUES ";
  for (size_t i = 0; i < values_.size(); ++i) {
    if (i > 0) oss << ", ";
    oss << "(";
    for (size_t j = 0; j < values_[i].size(); ++j) {
      if (j > 0) oss << ", ";
      oss << values_[i][j].to_string();
    }
    oss << ")";
  }
  return oss.str();
}

// ==================== UpdateStatement ====================

UpdateStatement::UpdateStatement(const std::string &table) : table_(table) {}

const std::string &UpdateStatement::get_table() const { return table_; }

void UpdateStatement::add_set_clause(const std::string &column,
                                     ExpressionPtr value) {
  set_clauses_.push_back({column, value});
}

void UpdateStatement::set_where_condition(ExpressionPtr expr) {
  where_condition_ = expr;
}

const std::vector<std::pair<std::string, ExpressionPtr>> &
UpdateStatement::get_set_clauses() const {
  return set_clauses_;
}

const ExpressionPtr &UpdateStatement::get_where_condition() const {
  return where_condition_;
}

std::string UpdateStatement::to_string() const {
  std::ostringstream oss;
  oss << "UPDATE " << table_ << " SET ";
  for (size_t i = 0; i < set_clauses_.size(); ++i) {
    if (i > 0) oss << ", ";
    oss << set_clauses_[i].first << " = "
        << set_clauses_[i].second->to_string();
  }
  if (where_condition_) {
    oss << " WHERE " << where_condition_->to_string();
  }
  return oss.str();
}

// ==================== DeleteStatement ====================

DeleteStatement::DeleteStatement(const std::string &table) : table_(table) {}

const std::string &DeleteStatement::get_table() const { return table_; }

void DeleteStatement::set_where_condition(ExpressionPtr expr) {
  where_condition_ = expr;
}

const ExpressionPtr &DeleteStatement::get_where_condition() const {
  return where_condition_;
}

std::string DeleteStatement::to_string() const {
  std::ostringstream oss;
  oss << "DELETE FROM " << table_;
  if (where_condition_) {
    oss << " WHERE " << where_condition_->to_string();
  }
  return oss.str();
}

// ==================== ColumnDefinition ====================

ColumnDefinition::ColumnDefinition(const std::string &name,
                                   const std::string &type, bool not_null,
                                   bool primary_key, bool unique)
    : name_(name),
      type_(type),
      not_null_(not_null),
      primary_key_(primary_key),
      unique_(unique) {}

const std::string &ColumnDefinition::get_name() const { return name_; }

const std::string &ColumnDefinition::get_type() const { return type_; }

bool ColumnDefinition::is_not_null() const { return not_null_; }

bool ColumnDefinition::is_primary_key() const { return primary_key_; }

bool ColumnDefinition::is_unique() const { return unique_; }

std::string ColumnDefinition::to_string() const {
  std::ostringstream oss;
  oss << name_ << " " << type_;
  if (not_null_) oss << " NOT NULL";
  if (primary_key_) oss << " PRIMARY KEY";
  if (unique_) oss << " UNIQUE";
  return oss.str();
}

// ==================== CreateTableStatement ====================

CreateTableStatement::CreateTableStatement(const std::string &table)
    : table_name_(table) {}

const std::string &CreateTableStatement::get_table_name() const {
  return table_name_;
}

void CreateTableStatement::add_column(const ColumnDefinition &col) {
  columns_.push_back(col);
}

const std::vector<ColumnDefinition> &CreateTableStatement::get_columns() const {
  return columns_;
}

std::string CreateTableStatement::to_string() const {
  std::ostringstream oss;
  oss << "CREATE TABLE " << table_name_ << " (";
  for (size_t i = 0; i < columns_.size(); ++i) {
    if (i > 0) oss << ", ";
    oss << columns_[i].to_string();
  }
  oss << ")";
  return oss.str();
}

}  // namespace db
