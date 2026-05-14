#include "include/parser/AST.h"
#include <sstream>

namespace db
{

	// ==================== Expression Implementations ====================

	LiteralExpression::LiteralExpression(const Value &val) : value(val) {}

	const Value &LiteralExpression::getValue() const
	{
		return value;
	}

	std::string LiteralExpression::toString() const
	{
		return value.toString();
	}

	IdentifierExpression::IdentifierExpression(const std::string &name) : name(name) {}

	const std::string &IdentifierExpression::getName() const
	{
		return name;
	}

	std::string IdentifierExpression::toString() const
	{
		return name;
	}

	ColumnRefExpression::ColumnRefExpression(const std::string &table, const std::string &column)
			: table(table), column(column) {}

	ColumnRefExpression::ColumnRefExpression(const std::string &column)
			: table(""), column(column) {}

	const std::string &ColumnRefExpression::getTable() const
	{
		return table;
	}

	const std::string &ColumnRefExpression::getColumn() const
	{
		return column;
	}

	std::string ColumnRefExpression::toString() const
	{
		if (table.empty())
			return column;
		return table + "." + column;
	}

	BinaryOpExpression::BinaryOpExpression(ExpressionPtr left, Operator op, ExpressionPtr right)
			: left(left), right(right), op(op) {}

	const ExpressionPtr &BinaryOpExpression::getLeft() const
	{
		return left;
	}

	const ExpressionPtr &BinaryOpExpression::getRight() const
	{
		return right;
	}

	BinaryOpExpression::Operator BinaryOpExpression::getOperator() const
	{
		return op;
	}

	std::string BinaryOpExpression::toString() const
	{
		std::string opStr;
		switch (op)
		{
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
		return "(" + left->toString() + " " + opStr + " " + right->toString() + ")";
	}

	UnaryOpExpression::UnaryOpExpression(Operator op, ExpressionPtr expr)
			: op(op), expr(expr) {}

	UnaryOpExpression::Operator UnaryOpExpression::getOperator() const
	{
		return op;
	}

	const ExpressionPtr &UnaryOpExpression::getExpression() const
	{
		return expr;
	}

	std::string UnaryOpExpression::toString() const
	{
		std::string opStr = (op == Operator::NOT) ? "NOT" : "-";
		return "(" + opStr + " " + expr->toString() + ")";
	}

	FunctionCallExpression::FunctionCallExpression(const std::string &name, std::vector<ExpressionPtr> args)
			: name(name), args(args) {}

	const std::string &FunctionCallExpression::getFunctionName() const
	{
		return name;
	}

	const std::vector<ExpressionPtr> &FunctionCallExpression::getArguments() const
	{
		return args;
	}

	std::string FunctionCallExpression::toString() const
	{
		std::string result = name + "(";
		for (size_t i = 0; i < args.size(); ++i)
		{
			if (i > 0)
				result += ", ";
			result += args[i]->toString();
		}
		result += ")";
		return result;
	}

	void CaseExpression::addWhenThen(ExpressionPtr when, ExpressionPtr then)
	{
		whenThenPairs.push_back({when, then});
	}

	void CaseExpression::setElse(ExpressionPtr elseExpr)
	{
		this->elseExpr = elseExpr;
	}

	const std::vector<std::pair<ExpressionPtr, ExpressionPtr>> &CaseExpression::getWhenThenPairs() const
	{
		return whenThenPairs;
	}

	const ExpressionPtr &CaseExpression::getElseExpression() const
	{
		return elseExpr;
	}

	std::string CaseExpression::toString() const
	{
		std::string result = "CASE";
		for (const auto &[when, then] : whenThenPairs)
		{
			result += " WHEN " + when->toString() + " THEN " + then->toString();
		}
		if (elseExpr)
		{
			result += " ELSE " + elseExpr->toString();
		}
		result += " END";
		return result;
	}

	// ==================== SelectStatement ====================

	SelectStatement::SelectStatement()
			: distinct(false), limit(-1), offset(0) {}

	void SelectStatement::addSelectColumn(ExpressionPtr expr, const std::string &alias)
	{
		selectColumns.push_back({expr, alias});
	}

	void SelectStatement::setFromTable(const std::string &table, const std::string &alias)
	{
		fromTable = table;
		fromAlias = alias.empty() ? table : alias;
	}

	void SelectStatement::setWhereCondition(ExpressionPtr expr)
	{
		whereCondition = expr;
	}

	void SelectStatement::addGroupByColumn(ExpressionPtr expr)
	{
		groupByColumns.push_back(expr);
	}

	void SelectStatement::setHavingCondition(ExpressionPtr expr)
	{
		havingCondition = expr;
	}

	void SelectStatement::addOrderByColumn(ExpressionPtr expr, bool ascending)
	{
		orderByColumns.push_back({expr, ascending});
	}

	void SelectStatement::addJoin(const std::string &type, const std::string &table,
																const std::string &alias, ExpressionPtr condition)
	{
		joins.push_back({type, table, alias, condition});
	}

	void SelectStatement::setDistinct(bool distinct)
	{
		this->distinct = distinct;
	}

	void SelectStatement::setLimit(int limit)
	{
		this->limit = limit;
	}

	void SelectStatement::setOffset(int offset)
	{
		this->offset = offset;
	}

	const std::vector<std::pair<ExpressionPtr, std::string>> &SelectStatement::getSelectColumns() const
	{
		return selectColumns;
	}

	const std::string &SelectStatement::getFromTable() const
	{
		return fromTable;
	}

	const std::string &SelectStatement::getFromAlias() const
	{
		return fromAlias;
	}

	const ExpressionPtr &SelectStatement::getWhereCondition() const
	{
		return whereCondition;
	}

	const std::vector<ExpressionPtr> &SelectStatement::getGroupByColumns() const
	{
		return groupByColumns;
	}

	const ExpressionPtr &SelectStatement::getHavingCondition() const
	{
		return havingCondition;
	}

	const std::vector<std::pair<ExpressionPtr, bool>> &SelectStatement::getOrderByColumns() const
	{
		return orderByColumns;
	}

	const std::vector<std::tuple<std::string, std::string, std::string, ExpressionPtr>> &SelectStatement::getJoins() const
	{
		return joins;
	}

	bool SelectStatement::isDistinct() const
	{
		return distinct;
	}

	int SelectStatement::getLimit() const
	{
		return limit;
	}

	int SelectStatement::getOffset() const
	{
		return offset;
	}

	std::string SelectStatement::toString() const
	{
		std::ostringstream oss;
		oss << "SELECT ";
		if (distinct)
			oss << "DISTINCT ";

		for (size_t i = 0; i < selectColumns.size(); ++i)
		{
			if (i > 0)
				oss << ", ";
			oss << selectColumns[i].first->toString();
			if (!selectColumns[i].second.empty())
			{
				oss << " AS " << selectColumns[i].second;
			}
		}

		if (!fromTable.empty())
		{
			oss << " FROM " << fromTable;
			if (fromAlias != fromTable)
				oss << " " << fromAlias;
		}

		for (const auto &[type, table, alias, condition] : joins)
		{
			oss << " " << type << " JOIN " << table;
			if (alias != table)
				oss << " " << alias;
			if (condition)
				oss << " ON " << condition->toString();
		}

		if (whereCondition)
		{
			oss << " WHERE " << whereCondition->toString();
		}

		if (!groupByColumns.empty())
		{
			oss << " GROUP BY ";
			for (size_t i = 0; i < groupByColumns.size(); ++i)
			{
				if (i > 0)
					oss << ", ";
				oss << groupByColumns[i]->toString();
			}
		}

		if (havingCondition)
		{
			oss << " HAVING " << havingCondition->toString();
		}

		if (!orderByColumns.empty())
		{
			oss << " ORDER BY ";
			for (size_t i = 0; i < orderByColumns.size(); ++i)
			{
				if (i > 0)
					oss << ", ";
				oss << orderByColumns[i].first->toString();
				oss << (orderByColumns[i].second ? " ASC" : " DESC");
			}
		}

		if (limit > 0)
		{
			oss << " LIMIT " << limit;
		}

		if (offset > 0)
		{
			oss << " OFFSET " << offset;
		}

		return oss.str();
	}

	// ==================== InsertStatement ====================

	InsertStatement::InsertStatement(const std::string &table) : table(table) {}

	const std::string &InsertStatement::getTable() const
	{
		return table;
	}

	void InsertStatement::addColumn(const std::string &col)
	{
		columns.push_back(col);
	}

	void InsertStatement::addValues(const std::vector<Value> &vals)
	{
		values.push_back(vals);
	}

	const std::vector<std::string> &InsertStatement::getColumns() const
	{
		return columns;
	}

	const std::vector<std::vector<Value>> &InsertStatement::getValues() const
	{
		return values;
	}

	std::string InsertStatement::toString() const
	{
		std::ostringstream oss;
		oss << "INSERT INTO " << table << " (";
		for (size_t i = 0; i < columns.size(); ++i)
		{
			if (i > 0)
				oss << ", ";
			oss << columns[i];
		}
		oss << ") VALUES ";
		for (size_t i = 0; i < values.size(); ++i)
		{
			if (i > 0)
				oss << ", ";
			oss << "(";
			for (size_t j = 0; j < values[i].size(); ++j)
			{
				if (j > 0)
					oss << ", ";
				oss << values[i][j].toString();
			}
			oss << ")";
		}
		return oss.str();
	}

	// ==================== UpdateStatement ====================

	UpdateStatement::UpdateStatement(const std::string &table) : table(table) {}

	const std::string &UpdateStatement::getTable() const
	{
		return table;
	}

	void UpdateStatement::addSetClause(const std::string &column, ExpressionPtr value)
	{
		setClauses.push_back({column, value});
	}

	void UpdateStatement::setWhereCondition(ExpressionPtr expr)
	{
		whereCondition = expr;
	}

	const std::vector<std::pair<std::string, ExpressionPtr>> &UpdateStatement::getSetClauses() const
	{
		return setClauses;
	}

	const ExpressionPtr &UpdateStatement::getWhereCondition() const
	{
		return whereCondition;
	}

	std::string UpdateStatement::toString() const
	{
		std::ostringstream oss;
		oss << "UPDATE " << table << " SET ";
		for (size_t i = 0; i < setClauses.size(); ++i)
		{
			if (i > 0)
				oss << ", ";
			oss << setClauses[i].first << " = " << setClauses[i].second->toString();
		}
		if (whereCondition)
		{
			oss << " WHERE " << whereCondition->toString();
		}
		return oss.str();
	}

	// ==================== DeleteStatement ====================

	DeleteStatement::DeleteStatement(const std::string &table) : table(table) {}

	const std::string &DeleteStatement::getTable() const
	{
		return table;
	}

	void DeleteStatement::setWhereCondition(ExpressionPtr expr)
	{
		whereCondition = expr;
	}

	const ExpressionPtr &DeleteStatement::getWhereCondition() const
	{
		return whereCondition;
	}

	std::string DeleteStatement::toString() const
	{
		std::ostringstream oss;
		oss << "DELETE FROM " << table;
		if (whereCondition)
		{
			oss << " WHERE " << whereCondition->toString();
		}
		return oss.str();
	}

	// ==================== ColumnDefinition ====================

	ColumnDefinition::ColumnDefinition(const std::string &name, const std::string &type,
																		 bool notNull, bool primaryKey, bool unique)
			: name(name), type(type), notNull(notNull), primaryKey(primaryKey), unique(unique) {}

	const std::string &ColumnDefinition::getName() const
	{
		return name;
	}

	const std::string &ColumnDefinition::getType() const
	{
		return type;
	}

	bool ColumnDefinition::isNotNull() const
	{
		return notNull;
	}

	bool ColumnDefinition::isPrimaryKey() const
	{
		return primaryKey;
	}

	bool ColumnDefinition::isUnique() const
	{
		return unique;
	}

	std::string ColumnDefinition::toString() const
	{
		std::ostringstream oss;
		oss << name << " " << type;
		if (notNull)
			oss << " NOT NULL";
		if (primaryKey)
			oss << " PRIMARY KEY";
		if (unique)
			oss << " UNIQUE";
		return oss.str();
	}

	// ==================== CreateTableStatement ====================

	CreateTableStatement::CreateTableStatement(const std::string &table) : tableName(table) {}

	const std::string &CreateTableStatement::getTableName() const
	{
		return tableName;
	}

	void CreateTableStatement::addColumn(const ColumnDefinition &col)
	{
		columns.push_back(col);
	}

	const std::vector<ColumnDefinition> &CreateTableStatement::getColumns() const
	{
		return columns;
	}

	std::string CreateTableStatement::toString() const
	{
		std::ostringstream oss;
		oss << "CREATE TABLE " << tableName << " (";
		for (size_t i = 0; i < columns.size(); ++i)
		{
			if (i > 0)
				oss << ", ";
			oss << columns[i].toString();
		}
		oss << ")";
		return oss.str();
	}

} // namespace db
