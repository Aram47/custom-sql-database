#pragma once

#include <memory>
#include <string>
#include <vector>
#include "include/parser/AST.h"
#include "include/core/Table.h"
#include "include/core/Row.h"
#include "include/utils/Exceptions.h"

namespace db
{

	// Result set for queries
	struct QueryResult
	{
		bool success;
		std::string message;
		std::vector<std::string> columnNames;
		std::vector<std::vector<Value>> rows;
		int affectedRows;

		QueryResult() : success(false), affectedRows(0) {}

		static QueryResult successResult(const std::string &msg = "OK")
		{
			QueryResult r;
			r.success = true;
			r.message = msg;
			return r;
		}

		static QueryResult errorResult(const std::string &msg)
		{
			QueryResult r;
			r.success = false;
			r.message = msg;
			return r;
		}
	};

	// Abstract executor interface
	class QueryExecutor
	{
	public:
		virtual ~QueryExecutor() = default;
		virtual QueryResult execute() = 0;
	};

	// SELECT executor
	class SelectExecutor : public QueryExecutor
	{
	public:
		SelectExecutor(std::shared_ptr<SelectStatement> stmt, Table *table);
		QueryResult execute() override;

	private:
		std::shared_ptr<SelectStatement> stmt;
		Table *table;

		bool evaluateCondition(const Row &row, const std::vector<std::string> &colNames,
													 const ExpressionPtr &condition) const;
		Value evaluateExpression(const Row &row, const std::vector<std::string> &colNames,
														 const ExpressionPtr &expr) const;
	};

	// INSERT executor
	class InsertExecutor : public QueryExecutor
	{
	public:
		InsertExecutor(std::shared_ptr<InsertStatement> stmt, Table *table);
		QueryResult execute() override;

	private:
		std::shared_ptr<InsertStatement> stmt;
		Table *table;
	};

	// UPDATE executor
	class UpdateExecutor : public QueryExecutor
	{
	public:
		UpdateExecutor(std::shared_ptr<UpdateStatement> stmt, Table *table);
		QueryResult execute() override;

	private:
		std::shared_ptr<UpdateStatement> stmt;
		Table *table;

		bool evaluateCondition(const Row &row, const std::vector<std::string> &colNames,
													 const ExpressionPtr &condition) const;
		Value evaluateExpression(const Row &row, const std::vector<std::string> &colNames,
														 const ExpressionPtr &expr) const;
	};

	// DELETE executor
	class DeleteExecutor : public QueryExecutor
	{
	public:
		DeleteExecutor(std::shared_ptr<DeleteStatement> stmt, Table *table);
		QueryResult execute() override;

	private:
		std::shared_ptr<DeleteStatement> stmt;
		Table *table;

		bool evaluateCondition(const Row &row, const std::vector<std::string> &colNames,
													 const ExpressionPtr &condition) const;
		Value evaluateExpression(const Row &row, const std::vector<std::string> &colNames,
														 const ExpressionPtr &expr) const;
	};

	// CREATE TABLE executor
	class CreateTableExecutor : public QueryExecutor
	{
	public:
		CreateTableExecutor(std::shared_ptr<CreateTableStatement> stmt, std::map<std::string, std::unique_ptr<Table>> *tables);
		QueryResult execute() override;

	private:
		std::shared_ptr<CreateTableStatement> stmt;
		std::map<std::string, std::unique_ptr<Table>> *tables;
	};

} // namespace db
