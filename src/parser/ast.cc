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

ParameterExpression::ParameterExpression(size_t index) : index_(index) {}

size_t ParameterExpression::get_index() const { return index_; }

std::string ParameterExpression::to_string() const { return "?"; }

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

void WindowSpec::add_partition_by(ExpressionPtr expr) {
  partition_by_.push_back(std::move(expr));
}

void WindowSpec::add_order_by(ExpressionPtr expr, bool ascending) {
  order_by_.emplace_back(std::move(expr), ascending);
}

const std::vector<ExpressionPtr> &WindowSpec::get_partition_by() const {
  return partition_by_;
}

const std::vector<std::pair<ExpressionPtr, bool>> &WindowSpec::get_order_by()
    const {
  return order_by_;
}

std::string WindowSpec::to_string() const {
  std::ostringstream oss;
  oss << "(";
  if (!partition_by_.empty()) {
    oss << "PARTITION BY ";
    for (size_t i = 0; i < partition_by_.size(); ++i) {
      if (i > 0) oss << ", ";
      oss << partition_by_[i]->to_string();
    }
  }
  if (!order_by_.empty()) {
    if (!partition_by_.empty()) oss << " ";
    oss << "ORDER BY ";
    for (size_t i = 0; i < order_by_.size(); ++i) {
      if (i > 0) oss << ", ";
      oss << order_by_[i].first->to_string();
      oss << (order_by_[i].second ? " ASC" : " DESC");
    }
  }
  oss << ")";
  return oss.str();
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

void FunctionCallExpression::set_window_spec(
    std::shared_ptr<WindowSpec> windowSpec) {
  window_spec_ = std::move(windowSpec);
}

const std::shared_ptr<WindowSpec> &FunctionCallExpression::get_window_spec()
    const {
  return window_spec_;
}

bool FunctionCallExpression::is_windowed() const {
  return static_cast<bool>(window_spec_);
}

std::string FunctionCallExpression::to_string() const {
  std::string result = name_ + "(";
  for (size_t i = 0; i < args_.size(); ++i) {
    if (i > 0) result += ", ";
    result += args_[i]->to_string();
  }
  result += ")";
  if (window_spec_) {
    result += " OVER " + window_spec_->to_string();
  }
  return result;
}

CastExpression::CastExpression(ExpressionPtr expr, DataType target_type)
    : expr_(std::move(expr)), target_type_(target_type) {}

const ExpressionPtr &CastExpression::get_expression() const { return expr_; }

DataType CastExpression::get_target_type() const { return target_type_; }

std::string CastExpression::to_string() const {
  return "CAST(" + expr_->to_string() + " AS " +
         data_type_to_string(target_type_) + ")";
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

void SelectStatement::set_join_table(size_t index, const std::string &table) {
  std::get<1>(joins_.at(index)) = table;
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

  if (limit_ >= 0) {
    oss << " LIMIT " << limit_;
  }

  if (offset_ > 0) {
    oss << " OFFSET " << offset_;
  }

  return oss.str();
}

// ==================== SetOperationStatement ====================

namespace {

std::string set_operation_kind_to_string(SetOperationKind kind, bool isAll) {
  switch (kind) {
    case SetOperationKind::Union:
      return isAll ? "UNION ALL" : "UNION";
    case SetOperationKind::Intersect:
      return "INTERSECT";
    case SetOperationKind::Except:
      return "EXCEPT";
  }
  return "UNION";
}

std::string query_operand_to_string(
    const SetOperationStatement::Operand &operand) {
  if (auto select = std::get_if<std::shared_ptr<SelectStatement>>(&operand)) {
    return (*select)->to_string();
  }
  return std::get<std::shared_ptr<SetOperationStatement>>(operand)->to_string();
}

}  // namespace

SetOperationStatement::SetOperationStatement(Operand left,
                                             SetOperationKind kind,
                                             Operand right, bool isAll)
    : left_(std::move(left)),
      right_(std::move(right)),
      kind_(kind),
      is_all_(isAll),
      limit_(-1),
      offset_(0) {}

const SetOperationStatement::Operand &SetOperationStatement::get_left() const {
  return left_;
}

const SetOperationStatement::Operand &SetOperationStatement::get_right() const {
  return right_;
}

SetOperationKind SetOperationStatement::get_kind() const { return kind_; }

bool SetOperationStatement::is_all() const { return is_all_; }

void SetOperationStatement::add_order_by_column(ExpressionPtr expr,
                                                bool ascending) {
  order_by_columns_.emplace_back(expr, ascending);
}

void SetOperationStatement::set_limit(int limit) { limit_ = limit; }

void SetOperationStatement::set_offset(int offset) { offset_ = offset; }

const std::vector<std::pair<ExpressionPtr, bool>> &
SetOperationStatement::get_order_by_columns() const {
  return order_by_columns_;
}

int SetOperationStatement::get_limit() const { return limit_; }

int SetOperationStatement::get_offset() const { return offset_; }

std::string SetOperationStatement::to_string() const {
  std::ostringstream oss;
  oss << query_operand_to_string(left_) << " "
      << set_operation_kind_to_string(kind_, is_all_) << " "
      << query_operand_to_string(right_);
  if (!order_by_columns_.empty()) {
    oss << " ORDER BY ";
    for (size_t i = 0; i < order_by_columns_.size(); ++i) {
      if (i > 0) oss << ", ";
      oss << order_by_columns_[i].first->to_string();
      oss << (order_by_columns_[i].second ? " ASC" : " DESC");
    }
  }
  if (limit_ >= 0) {
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

void InsertStatement::add_values(const std::vector<ExpressionPtr> &vals) {
  values_.push_back(vals);
}

const std::vector<std::string> &InsertStatement::get_columns() const {
  return columns_;
}

const std::vector<std::vector<ExpressionPtr>> &InsertStatement::get_values()
    const {
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
      oss << values_[i][j]->to_string();
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

void ColumnDefinition::set_default_value(const Value &value) {
  default_value_ = value;
}

bool ColumnDefinition::has_default() const { return default_value_.has_value(); }

const Value &ColumnDefinition::get_default_value() const {
  return *default_value_;
}

void ColumnDefinition::set_references(const std::string &parent_table,
                                      const std::string &parent_column,
                                      ReferentialAction on_delete,
                                      ReferentialAction on_update) {
  references_table_ = parent_table;
  references_column_ = parent_column;
  on_delete_ = on_delete;
  on_update_ = on_update;
}

bool ColumnDefinition::has_references() const {
  return !references_table_.empty();
}

const std::string &ColumnDefinition::get_references_table() const {
  return references_table_;
}

const std::string &ColumnDefinition::get_references_column() const {
  return references_column_;
}

ReferentialAction ColumnDefinition::get_on_delete() const { return on_delete_; }

ReferentialAction ColumnDefinition::get_on_update() const { return on_update_; }

void ColumnDefinition::set_check_expression(ExpressionPtr expr) {
  check_expression_ = std::move(expr);
}

bool ColumnDefinition::has_check_expression() const {
  return check_expression_ != nullptr;
}

const ExpressionPtr &ColumnDefinition::get_check_expression() const {
  return check_expression_;
}

std::string ColumnDefinition::to_string() const {
  std::ostringstream oss;
  oss << name_ << " " << type_;
  if (not_null_) oss << " NOT NULL";
  if (primary_key_) oss << " PRIMARY KEY";
  if (unique_) oss << " UNIQUE";
  if (has_default()) oss << " DEFAULT " << default_value_->to_string();
  if (has_references()) {
    oss << " REFERENCES " << references_table_ << "(" << references_column_
        << ")";
  }
  if (has_check_expression()) {
    oss << " CHECK (" << check_expression_->to_string() << ")";
  }
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

void CreateTableStatement::add_foreign_key(const ForeignKeyDefinition &fk) {
  foreign_keys_.push_back(fk);
}

void CreateTableStatement::add_check(const CheckConstraintDefinition &check) {
  checks_.push_back(check);
}

void CreateTableStatement::set_primary_key(std::vector<std::string> columns) {
  primary_key_columns_ = std::move(columns);
}

void CreateTableStatement::add_unique(std::string name,
                                      std::vector<std::string> columns) {
  unique_constraints_.emplace_back(std::move(name), std::move(columns));
}

const std::vector<ColumnDefinition> &CreateTableStatement::get_columns() const {
  return columns_;
}

const std::vector<ForeignKeyDefinition> &
CreateTableStatement::get_foreign_keys() const {
  return foreign_keys_;
}

const std::vector<CheckConstraintDefinition> &
CreateTableStatement::get_checks() const {
  return checks_;
}

const std::vector<std::string> &CreateTableStatement::get_primary_key_columns()
    const {
  return primary_key_columns_;
}

const std::vector<std::pair<std::string, std::vector<std::string>>> &
CreateTableStatement::get_unique_constraints() const {
  return unique_constraints_;
}

void CreateTableStatement::setPartitionBy(PartitionKind kind,
                                          std::string keyColumn) {
  has_partition_by_ = true;
  partition_kind_ = kind;
  partition_key_column_ = std::move(keyColumn);
}

bool CreateTableStatement::hasPartitionBy() const { return has_partition_by_; }

PartitionKind CreateTableStatement::getPartitionKind() const {
  return partition_kind_;
}

const std::string &CreateTableStatement::getPartitionKeyColumn() const {
  return partition_key_column_;
}

void CreateTableStatement::setPartitionOf(std::string parentName,
                                          PartitionBound bound) {
  is_partition_of_ = true;
  partition_of_parent_ = std::move(parentName);
  partition_bound_ = std::move(bound);
}

bool CreateTableStatement::isPartitionOf() const { return is_partition_of_; }

const std::string &CreateTableStatement::getPartitionOfParent() const {
  return partition_of_parent_;
}

const PartitionBound &CreateTableStatement::getPartitionBound() const {
  return partition_bound_;
}

std::string CreateTableStatement::to_string() const {
  std::ostringstream oss;
  oss << "CREATE TABLE " << table_name_;
  if (is_partition_of_) {
    oss << " PARTITION OF " << partition_of_parent_;
    return oss.str();
  }
  oss << " (";
  for (size_t i = 0; i < columns_.size(); ++i) {
    if (i > 0) oss << ", ";
    oss << columns_[i].to_string();
  }
  if (!primary_key_columns_.empty()) {
    oss << ", PRIMARY KEY (";
    for (size_t i = 0; i < primary_key_columns_.size(); ++i) {
      if (i > 0) oss << ", ";
      oss << primary_key_columns_[i];
    }
    oss << ")";
  }
  for (const auto &[uq_name, uq_cols] : unique_constraints_) {
    oss << ", ";
    if (!uq_name.empty()) {
      oss << "CONSTRAINT " << uq_name << " ";
    }
    oss << "UNIQUE (";
    for (size_t i = 0; i < uq_cols.size(); ++i) {
      if (i > 0) oss << ", ";
      oss << uq_cols[i];
    }
    oss << ")";
  }
  for (const auto &fk : foreign_keys_) {
    oss << ", FOREIGN KEY (";
    for (size_t i = 0; i < fk.child_columns.size(); ++i) {
      if (i > 0) oss << ", ";
      oss << fk.child_columns[i];
    }
    oss << ") REFERENCES " << fk.parent_table << " (";
    for (size_t i = 0; i < fk.parent_columns.size(); ++i) {
      if (i > 0) oss << ", ";
      oss << fk.parent_columns[i];
    }
    oss << ")";
  }
  oss << ")";
  if (has_partition_by_) {
    oss << " PARTITION BY "
        << (partition_kind_ == PartitionKind::Range ? "RANGE" : "HASH") << " ("
        << partition_key_column_ << ")";
  }
  return oss.str();
}

DropTableStatement::DropTableStatement(const std::string &table)
    : table_name_(table) {}

const std::string &DropTableStatement::get_table_name() const {
  return table_name_;
}

std::string DropTableStatement::to_string() const {
  return "DROP TABLE " + table_name_;
}

AlterTableStatement::AlterTableStatement(const std::string &table)
    : table_name_(table) {}

const std::string &AlterTableStatement::get_table_name() const {
  return table_name_;
}

void AlterTableStatement::set_action(const AlterTableAction &action) {
  action_ = action;
}

const AlterTableAction &AlterTableStatement::get_action() const {
  return action_;
}

std::string AlterTableStatement::to_string() const {
  return "ALTER TABLE " + table_name_;
}

CreateIndexStatement::CreateIndexStatement(
    const std::string &index_name, const std::string &table_name,
    std::vector<std::string> column_names)
    : index_name_(index_name),
      table_name_(table_name),
      column_names_(std::move(column_names)) {}

const std::string &CreateIndexStatement::get_index_name() const {
  return index_name_;
}

const std::string &CreateIndexStatement::get_table_name() const {
  return table_name_;
}

const std::vector<std::string> &CreateIndexStatement::get_column_names() const {
  return column_names_;
}

std::string CreateIndexStatement::to_string() const {
  std::string columns;
  for (size_t i = 0; i < column_names_.size(); ++i) {
    if (i > 0) {
      columns += ", ";
    }
    columns += column_names_[i];
  }
  return "CREATE INDEX " + index_name_ + " ON " + table_name_ + "(" + columns +
         ")";
}

DropIndexStatement::DropIndexStatement(const std::string &index_name)
    : index_name_(index_name) {}

const std::string &DropIndexStatement::get_index_name() const {
  return index_name_;
}

std::string DropIndexStatement::to_string() const {
  return "DROP INDEX " + index_name_;
}

CreateViewStatement::CreateViewStatement(
    const std::string &view_name, const std::string &select_sql,
    std::shared_ptr<SelectStatement> select)
    : view_name_(view_name),
      select_sql_(select_sql),
      select_(std::move(select)) {}

const std::string &CreateViewStatement::get_view_name() const {
  return view_name_;
}

const std::string &CreateViewStatement::get_select_sql() const {
  return select_sql_;
}

const std::shared_ptr<SelectStatement> &CreateViewStatement::get_select()
    const {
  return select_;
}

std::string CreateViewStatement::to_string() const {
  return "CREATE VIEW " + view_name_ + " AS " + select_sql_;
}

DropViewStatement::DropViewStatement(const std::string &view_name,
                                     bool if_exists)
    : view_name_(view_name), if_exists_(if_exists) {}

const std::string &DropViewStatement::get_view_name() const {
  return view_name_;
}

bool DropViewStatement::is_if_exists() const { return if_exists_; }

std::string DropViewStatement::to_string() const {
  if (if_exists_) {
    return "DROP VIEW IF EXISTS " + view_name_;
  }
  return "DROP VIEW " + view_name_;
}

CreateFunctionStatement::CreateFunctionStatement(
    std::string name, std::vector<RoutineParamAst> params,
    std::string return_type, ExpressionPtr body, std::string source_sql)
    : name_(std::move(name)),
      params_(std::move(params)),
      return_type_(std::move(return_type)),
      body_(std::move(body)),
      source_sql_(std::move(source_sql)) {}

const std::string &CreateFunctionStatement::get_name() const { return name_; }

const std::vector<RoutineParamAst> &CreateFunctionStatement::get_params()
    const {
  return params_;
}

const std::string &CreateFunctionStatement::get_return_type() const {
  return return_type_;
}

const ExpressionPtr &CreateFunctionStatement::get_body() const { return body_; }

const std::string &CreateFunctionStatement::get_source_sql() const {
  return source_sql_;
}

std::string CreateFunctionStatement::to_string() const {
  return source_sql_.empty() ? ("CREATE FUNCTION " + name_) : source_sql_;
}

DropFunctionStatement::DropFunctionStatement(std::string name, bool if_exists)
    : name_(std::move(name)), if_exists_(if_exists) {}

const std::string &DropFunctionStatement::get_name() const { return name_; }

bool DropFunctionStatement::is_if_exists() const { return if_exists_; }

std::string DropFunctionStatement::to_string() const {
  if (if_exists_) {
    return "DROP FUNCTION IF EXISTS " + name_;
  }
  return "DROP FUNCTION " + name_;
}

CreateProcedureStatement::CreateProcedureStatement(
    std::string name, std::vector<RoutineParamAst> params,
    std::vector<std::string> statement_sqls, std::string source_sql)
    : name_(std::move(name)),
      params_(std::move(params)),
      statement_sqls_(std::move(statement_sqls)),
      source_sql_(std::move(source_sql)) {}

const std::string &CreateProcedureStatement::get_name() const { return name_; }

const std::vector<RoutineParamAst> &CreateProcedureStatement::get_params()
    const {
  return params_;
}

const std::vector<std::string> &CreateProcedureStatement::get_statement_sqls()
    const {
  return statement_sqls_;
}

const std::string &CreateProcedureStatement::get_source_sql() const {
  return source_sql_;
}

std::string CreateProcedureStatement::to_string() const {
  return source_sql_.empty() ? ("CREATE PROCEDURE " + name_) : source_sql_;
}

DropProcedureStatement::DropProcedureStatement(std::string name, bool if_exists)
    : name_(std::move(name)), if_exists_(if_exists) {}

const std::string &DropProcedureStatement::get_name() const { return name_; }

bool DropProcedureStatement::is_if_exists() const { return if_exists_; }

std::string DropProcedureStatement::to_string() const {
  if (if_exists_) {
    return "DROP PROCEDURE IF EXISTS " + name_;
  }
  return "DROP PROCEDURE " + name_;
}

CallStatement::CallStatement(std::string name,
                             std::vector<ExpressionPtr> arguments)
    : name_(std::move(name)), arguments_(std::move(arguments)) {}

const std::string &CallStatement::get_name() const { return name_; }

const std::vector<ExpressionPtr> &CallStatement::get_arguments() const {
  return arguments_;
}

std::string CallStatement::to_string() const {
  std::string result = "CALL " + name_ + "(";
  for (size_t i = 0; i < arguments_.size(); ++i) {
    if (i > 0) {
      result += ", ";
    }
    result += arguments_[i]->to_string();
  }
  result += ")";
  return result;
}

CreateTriggerStatement::CreateTriggerStatement(
    std::string name, std::string table_name, TriggerTiming timing,
    TriggerEvent event, std::vector<std::string> statement_sqls,
    std::string source_sql)
    : name_(std::move(name)),
      table_name_(std::move(table_name)),
      timing_(timing),
      event_(event),
      statement_sqls_(std::move(statement_sqls)),
      source_sql_(std::move(source_sql)) {}

const std::string &CreateTriggerStatement::get_name() const { return name_; }

const std::string &CreateTriggerStatement::get_table_name() const {
  return table_name_;
}

TriggerTiming CreateTriggerStatement::get_timing() const { return timing_; }

TriggerEvent CreateTriggerStatement::get_event() const { return event_; }

const std::vector<std::string> &CreateTriggerStatement::get_statement_sqls()
    const {
  return statement_sqls_;
}

const std::string &CreateTriggerStatement::get_source_sql() const {
  return source_sql_;
}

std::string CreateTriggerStatement::to_string() const {
  return source_sql_.empty() ? ("CREATE TRIGGER " + name_) : source_sql_;
}

DropTriggerStatement::DropTriggerStatement(std::string name, bool if_exists)
    : name_(std::move(name)), if_exists_(if_exists) {}

const std::string &DropTriggerStatement::get_name() const { return name_; }

bool DropTriggerStatement::is_if_exists() const { return if_exists_; }

std::string DropTriggerStatement::to_string() const {
  if (if_exists_) {
    return "DROP TRIGGER IF EXISTS " + name_;
  }
  return "DROP TRIGGER " + name_;
}

SetNewStatement::SetNewStatement(std::string column_name, ExpressionPtr value)
    : column_name_(std::move(column_name)), value_(std::move(value)) {}

const std::string &SetNewStatement::get_column_name() const {
  return column_name_;
}

const ExpressionPtr &SetNewStatement::get_value() const { return value_; }

std::string SetNewStatement::to_string() const {
  return "SET NEW." + column_name_ + " = " + value_->to_string();
}

std::string BeginStatement::to_string() const { return "BEGIN"; }

std::string CommitStatement::to_string() const { return "COMMIT"; }

std::string RollbackStatement::to_string() const { return "ROLLBACK"; }

PrepareStatement::PrepareStatement(const std::string &name,
                                   const std::string &sql)
    : name_(name), sql_(sql) {}

const std::string &PrepareStatement::get_name() const { return name_; }

const std::string &PrepareStatement::get_sql() const { return sql_; }

std::string PrepareStatement::to_string() const {
  return "PREPARE " + name_ + " AS " + sql_;
}

ExecutePreparedStatement::ExecutePreparedStatement(
    const std::string &name, std::vector<Value> arguments)
    : name_(name), arguments_(std::move(arguments)) {}

const std::string &ExecutePreparedStatement::get_name() const { return name_; }

const std::vector<Value> &ExecutePreparedStatement::get_arguments() const {
  return arguments_;
}

std::string ExecutePreparedStatement::to_string() const {
  return "EXECUTE " + name_;
}

DeallocatePreparedStatement::DeallocatePreparedStatement(
    const std::string &name)
    : name_(name) {}

const std::string &DeallocatePreparedStatement::get_name() const {
  return name_;
}

std::string DeallocatePreparedStatement::to_string() const {
  return "DEALLOCATE PREPARE " + name_;
}

VacuumStatement::VacuumStatement(const std::string &table_name)
    : table_name_(table_name) {}

const std::string &VacuumStatement::get_table_name() const {
  return table_name_;
}

std::string VacuumStatement::to_string() const {
  if (table_name_.empty()) {
    return "VACUUM";
  }
  return "VACUUM " + table_name_;
}

InExpression::InExpression(ExpressionPtr left, ExpressionPtr subquery,
                           bool is_not)
    : left_(std::move(left)),
      subquery_(std::move(subquery)),
      is_not_(is_not) {}

InExpression::InExpression(ExpressionPtr left,
                           std::vector<ExpressionPtr> values, bool is_not)
    : left_(std::move(left)),
      values_(std::move(values)),
      is_not_(is_not) {}

const ExpressionPtr &InExpression::get_left() const { return left_; }

bool InExpression::is_not() const { return is_not_; }

bool InExpression::has_subquery() const { return subquery_ != nullptr; }

const ExpressionPtr &InExpression::get_subquery() const { return subquery_; }

const std::vector<ExpressionPtr> &InExpression::get_values() const {
  return values_;
}

std::string InExpression::to_string() const {
  std::string result = left_->to_string();
  result += is_not_ ? " NOT IN (" : " IN (";
  if (subquery_) {
    result += subquery_->to_string();
  } else {
    for (size_t i = 0; i < values_.size(); ++i) {
      if (i > 0) {
        result += ", ";
      }
      result += values_[i]->to_string();
    }
  }
  result += ")";
  return result;
}

ExistsExpression::ExistsExpression(std::shared_ptr<SelectStatement> select,
                                   bool is_not)
    : select_(std::move(select)), is_not_(is_not) {}

const std::shared_ptr<SelectStatement> &ExistsExpression::get_select() const {
  return select_;
}

bool ExistsExpression::is_not() const { return is_not_; }

std::string ExistsExpression::to_string() const {
  return std::string(is_not_ ? "NOT EXISTS (" : "EXISTS (") +
         select_->to_string() + ")";
}

SubqueryExpression::SubqueryExpression(
    std::shared_ptr<SelectStatement> select)
    : select_(std::move(select)) {}

const std::shared_ptr<SelectStatement> &SubqueryExpression::get_select()
    const {
  return select_;
}

std::string SubqueryExpression::to_string() const {
  return "(" + select_->to_string() + ")";
}

}  // namespace db
