#pragma once

#include <vector>
#include <string>
#include <memory>
#include <variant>
#include "include/types/Value.h"
#include "include/utils/Exceptions.h"

namespace db
{

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

	class Expression
	{
	public:
		virtual ~Expression() = default;
		virtual std::string toString() const = 0;
	};

	class LiteralExpression : public Expression
	{
	public:
		explicit LiteralExpression(const Value &val);
		const Value &getValue() const;
		std::string toString() const override;

	private:
		Value value;
	};

	class IdentifierExpression : public Expression
	{
	public:
		explicit IdentifierExpression(const std::string &name);
		const std::string &getName() const;
		std::string toString() const override;

	private:
		std::string name;
	};

	class ColumnRefExpression : public Expression
	{
	public:
		ColumnRefExpression(const std::string &table, const std::string &column);
		ColumnRefExpression(const std::string &column);
		const std::string &getTable() const;
		const std::string &getColumn() const;
		std::string toString() const override;

	private:
		std::string table;
		std::string column;
	};

	class BinaryOpExpression : public Expression
	{
	public:
		enum class Operator
		{
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
		const ExpressionPtr &getLeft() const;
		const ExpressionPtr &getRight() const;
		Operator getOperator() const;
		std::string toString() const override;

	private:
		ExpressionPtr left;
		ExpressionPtr right;
		Operator op;
	};

	class UnaryOpExpression : public Expression
	{
	public:
		enum class Operator
		{
			NOT,
			MINUS
		};

		UnaryOpExpression(Operator op, ExpressionPtr expr);
		Operator getOperator() const;
		const ExpressionPtr &getExpression() const;
		std::string toString() const override;

	private:
		Operator op;
		ExpressionPtr expr;
	};

	class FunctionCallExpression : public Expression
	{
	public:
		FunctionCallExpression(const std::string &name, std::vector<ExpressionPtr> args);
		const std::string &getFunctionName() const;
		const std::vector<ExpressionPtr> &getArguments() const;
		std::string toString() const override;

	private:
		std::string name;
		std::vector<ExpressionPtr> args;
	};

	class CaseExpression : public Expression
	{
	public:
		void addWhenThen(ExpressionPtr when, ExpressionPtr then);
		void setElse(ExpressionPtr elseExpr);
		const std::vector<std::pair<ExpressionPtr, ExpressionPtr>> &getWhenThenPairs() const;
		const ExpressionPtr &getElseExpression() const;
		std::string toString() const override;

	private:
		std::vector<std::pair<ExpressionPtr, ExpressionPtr>> whenThenPairs;
		ExpressionPtr elseExpr;
	};

	// ==================== Query Statements ====================

	class SelectStatement
	{
	public:
		SelectStatement();

		void addSelectColumn(ExpressionPtr expr, const std::string &alias = "");
		void setFromTable(const std::string &table, const std::string &alias = "");
		void setWhereCondition(ExpressionPtr expr);
		void addGroupByColumn(ExpressionPtr expr);
		void setHavingCondition(ExpressionPtr expr);
		void addOrderByColumn(ExpressionPtr expr, bool ascending = true);
		void addJoin(const std::string &type, const std::string &table,
								 const std::string &alias, ExpressionPtr condition);
		void setDistinct(bool distinct);
		void setLimit(int limit);
		void setOffset(int offset);

		const std::vector<std::pair<ExpressionPtr, std::string>> &getSelectColumns() const;
		const std::string &getFromTable() const;
		const std::string &getFromAlias() const;
		const ExpressionPtr &getWhereCondition() const;
		const std::vector<ExpressionPtr> &getGroupByColumns() const;
		const ExpressionPtr &getHavingCondition() const;
		const std::vector<std::pair<ExpressionPtr, bool>> &getOrderByColumns() const;
		const std::vector<std::tuple<std::string, std::string, std::string, ExpressionPtr>> &getJoins() const;
		bool isDistinct() const;
		int getLimit() const;
		int getOffset() const;

		std::string toString() const;

	private:
		std::vector<std::pair<ExpressionPtr, std::string>> selectColumns; // (expr, alias)
		std::string fromTable;
		std::string fromAlias;
		ExpressionPtr whereCondition;
		std::vector<ExpressionPtr> groupByColumns;
		ExpressionPtr havingCondition;
		std::vector<std::pair<ExpressionPtr, bool>> orderByColumns;													 // (expr, ascending)
		std::vector<std::tuple<std::string, std::string, std::string, ExpressionPtr>> joins; // (type, table, alias, condition)
		bool distinct;
		int limit;
		int offset;
	};

	class InsertStatement
	{
	public:
		InsertStatement(const std::string &table);

		const std::string &getTable() const;
		void addColumn(const std::string &col);
		void addValues(const std::vector<Value> &vals);

		const std::vector<std::string> &getColumns() const;
		const std::vector<std::vector<Value>> &getValues() const;

		std::string toString() const;

	private:
		std::string table;
		std::vector<std::string> columns;
		std::vector<std::vector<Value>> values;
	};

	class UpdateStatement
	{
	public:
		UpdateStatement(const std::string &table);

		const std::string &getTable() const;
		void addSetClause(const std::string &column, ExpressionPtr value);
		void setWhereCondition(ExpressionPtr expr);

		const std::vector<std::pair<std::string, ExpressionPtr>> &getSetClauses() const;
		const ExpressionPtr &getWhereCondition() const;

		std::string toString() const;

	private:
		std::string table;
		std::vector<std::pair<std::string, ExpressionPtr>> setClauses;
		ExpressionPtr whereCondition;
	};

	class DeleteStatement
	{
	public:
		explicit DeleteStatement(const std::string &table);

		const std::string &getTable() const;
		void setWhereCondition(ExpressionPtr expr);
		const ExpressionPtr &getWhereCondition() const;

		std::string toString() const;

	private:
		std::string table;
		ExpressionPtr whereCondition;
	};

	class ColumnDefinition
	{
	public:
		ColumnDefinition(const std::string &name, const std::string &type,
										 bool notNull = false, bool primaryKey = false, bool unique = false);

		const std::string &getName() const;
		const std::string &getType() const;
		bool isNotNull() const;
		bool isPrimaryKey() const;
		bool isUnique() const;

		std::string toString() const;

	private:
		std::string name;
		std::string type;
		bool notNull;
		bool primaryKey;
		bool unique;
	};

	class CreateTableStatement
	{
	public:
		explicit CreateTableStatement(const std::string &table);

		const std::string &getTableName() const;
		void addColumn(const ColumnDefinition &col);
		const std::vector<ColumnDefinition> &getColumns() const;

		std::string toString() const;

	private:
		std::string tableName;
		std::vector<ColumnDefinition> columns;
	};

} // namespace db
