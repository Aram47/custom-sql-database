#include "include/executor/QueryExecutor.h"
#include "include/types/TypeConverter.h"
#include <algorithm>
#include <cmath>
#include <numeric>

namespace db
{

	// ==================== SelectExecutor ====================

	SelectExecutor::SelectExecutor(std::shared_ptr<SelectStatement> stmt, Table *table)
			: stmt(stmt), table(table) {}

	QueryResult SelectExecutor::execute()
	{
		QueryResult result;
		result.success = true;

		if (!table)
		{
			return QueryResult::errorResult("Table not found");
		}

		std::vector<Row> allRows = table->getAllRows();
		std::vector<std::string> colNames;
		for (const auto &col : table->getColumns())
		{
			colNames.push_back(col.getName());
		}

		// Filter rows by WHERE clause
		std::vector<Row> filteredRows;
		for (const auto &row : allRows)
		{
			if (!stmt->getWhereCondition() || evaluateCondition(row, colNames, stmt->getWhereCondition()))
			{
				filteredRows.push_back(row);
			}
		}

		// Extract selected columns
		const auto &selectCols = stmt->getSelectColumns();
		if (selectCols.empty())
		{
			return QueryResult::errorResult("No columns selected");
		}

		const auto isWildcardColumn = [](const ExpressionPtr &e) -> bool
		{
			auto colRef = std::dynamic_pointer_cast<ColumnRefExpression>(e);
			if (colRef && colRef->getColumn() == "*")
				return true;
			auto ident = std::dynamic_pointer_cast<IdentifierExpression>(e);
			return ident && ident->getName() == "*";
		};

		// Build result column names
		for (const auto &[expr, alias] : selectCols)
		{
			if (isWildcardColumn(expr))
			{
				for (const auto &col : table->getColumns())
				{
					result.columnNames.push_back(col.getName());
				}
			}
			else if (!alias.empty())
			{
				result.columnNames.push_back(alias);
			}
			else if (expr)
			{
				result.columnNames.push_back(expr->toString());
			}
		}

		// Build result rows
		for (const auto &row : filteredRows)
		{
			std::vector<Value> resultRow;

			for (const auto &[expr, alias] : selectCols)
			{
				if (isWildcardColumn(expr))
				{
					for (size_t i = 0; i < row.getColumnCount(); ++i)
					{
						resultRow.push_back(row.getValue(i));
					}
				}
				else
				{
					resultRow.push_back(evaluateExpression(row, colNames, expr));
				}
			}

			result.rows.push_back(resultRow);
		}

		// Apply DISTINCT (sort so std::unique removes all duplicates)
		if (stmt->isDistinct())
		{
			const auto rowLess = [](const std::vector<Value> &a, const std::vector<Value> &b)
			{
				const size_t n = std::min(a.size(), b.size());
				for (size_t i = 0; i < n; ++i)
				{
					if (!(a[i] == b[i]))
						return a[i] < b[i];
				}
				return a.size() < b.size();
			};
			const auto rowEqual = [](const std::vector<Value> &a, const std::vector<Value> &b)
			{
				if (a.size() != b.size())
					return false;
				for (size_t i = 0; i < a.size(); ++i)
				{
					if (a[i] != b[i])
						return false;
				}
				return true;
			};
			std::sort(result.rows.begin(), result.rows.end(), rowLess);
			const auto last = std::unique(result.rows.begin(), result.rows.end(), rowEqual);
			result.rows.erase(last, result.rows.end());
		}

		result.affectedRows = result.rows.size();
		result.message = "SELECT OK";

		return result;
	}

	bool SelectExecutor::evaluateCondition(const Row &row, const std::vector<std::string> &colNames,
																				 const ExpressionPtr &condition) const
	{
		if (!condition)
			return true;

		auto binOp = std::dynamic_pointer_cast<BinaryOpExpression>(condition);
		if (binOp)
		{
			Value left = evaluateExpression(row, colNames, binOp->getLeft());
			Value right = evaluateExpression(row, colNames, binOp->getRight());

			switch (binOp->getOperator())
			{
			case BinaryOpExpression::Operator::EQ:
				return left == right;
			case BinaryOpExpression::Operator::NE:
				return left != right;
			case BinaryOpExpression::Operator::LT:
				return left < right;
			case BinaryOpExpression::Operator::LE:
				return left <= right;
			case BinaryOpExpression::Operator::GT:
				return left > right;
			case BinaryOpExpression::Operator::GE:
				return left >= right;
			case BinaryOpExpression::Operator::AND:
				return evaluateCondition(row, colNames, binOp->getLeft()) &&
							 evaluateCondition(row, colNames, binOp->getRight());
			case BinaryOpExpression::Operator::OR:
				return evaluateCondition(row, colNames, binOp->getLeft()) ||
							 evaluateCondition(row, colNames, binOp->getRight());
			default:
				return true;
			}
		}

		auto unaryOp = std::dynamic_pointer_cast<UnaryOpExpression>(condition);
		if (unaryOp)
		{
			if (unaryOp->getOperator() == UnaryOpExpression::Operator::NOT)
			{
				return !evaluateCondition(row, colNames, unaryOp->getExpression());
			}
		}

		return true;
	}

	Value SelectExecutor::evaluateExpression(const Row &row, const std::vector<std::string> &colNames,
																					 const ExpressionPtr &expr) const
	{
		auto literal = std::dynamic_pointer_cast<LiteralExpression>(expr);
		if (literal)
			return literal->getValue();

		auto colRef = std::dynamic_pointer_cast<ColumnRefExpression>(expr);
		if (colRef)
		{
			int idx = -1;
			for (size_t i = 0; i < colNames.size(); ++i)
			{
				if (colNames[i] == colRef->getColumn())
				{
					idx = i;
					break;
				}
			}
			if (idx >= 0 && idx < (int)row.getColumnCount())
			{
				return row.getValue(idx);
			}
			return Value();
		}

		auto binOp = std::dynamic_pointer_cast<BinaryOpExpression>(expr);
		if (binOp)
		{
			Value left = evaluateExpression(row, colNames, binOp->getLeft());
			Value right = evaluateExpression(row, colNames, binOp->getRight());

			switch (binOp->getOperator())
			{
			case BinaryOpExpression::Operator::PLUS:
				return left + right;
			case BinaryOpExpression::Operator::MINUS:
				return left - right;
			case BinaryOpExpression::Operator::MUL:
				return left * right;
			case BinaryOpExpression::Operator::DIV:
				return left / right;
			case BinaryOpExpression::Operator::MOD:
			{
				if (left.isInt() && right.isInt())
				{
					return Value(left.asInt() % right.asInt());
				}
				return Value();
			}
			default:
				return Value();
			}
		}

		auto func = std::dynamic_pointer_cast<FunctionCallExpression>(expr);
		if (func)
		{
			auto funcName = func->getFunctionName();
			std::transform(funcName.begin(), funcName.end(), funcName.begin(), ::tolower);

			const auto &args = func->getArguments();
			if (funcName == "count" && !args.empty())
			{
				return Value((int64_t)1); // Simplified count (used in SELECT context)
			}
			else if (funcName == "upper" && !args.empty())
			{
				Value val = evaluateExpression(row, colNames, args[0]);
				std::string str = val.asString();
				std::transform(str.begin(), str.end(), str.begin(), ::toupper);
				return Value(str);
			}
			else if (funcName == "lower" && !args.empty())
			{
				Value val = evaluateExpression(row, colNames, args[0]);
				std::string str = val.asString();
				std::transform(str.begin(), str.end(), str.begin(), ::tolower);
				return Value(str);
			}
			else if (funcName == "length" && !args.empty())
			{
				Value val = evaluateExpression(row, colNames, args[0]);
				return Value((int64_t)val.asString().length());
			}
		}

		return Value();
	}

	// ==================== InsertExecutor ====================

	InsertExecutor::InsertExecutor(std::shared_ptr<InsertStatement> stmt, Table *table)
			: stmt(stmt), table(table) {}

	QueryResult InsertExecutor::execute()
	{
		if (!table)
		{
			return QueryResult::errorResult("Table not found");
		}

		const auto &columns = stmt->getColumns();
		const auto &values = stmt->getValues();

		if (values.empty())
		{
			return QueryResult::errorResult("No values to insert");
		}

		int insertedCount = 0;

		for (const auto &valueRow : values)
		{
			try
			{
				Row row;

				if (!columns.empty())
				{
					// Map column names to values
					std::vector<Value> rowValues(table->getColumnCount(), Value());
					for (size_t i = 0; i < columns.size() && i < valueRow.size(); ++i)
					{
						int colIdx = table->getColumnIndex(columns[i]);
						if (colIdx >= 0)
						{
							rowValues[colIdx] = valueRow[i];
						}
					}
					row = Row(rowValues);
				}
				else
				{
					// All columns in order
					row = Row(valueRow);
				}

				table->insertRow(row);
				insertedCount++;
			}
			catch (const std::exception &e)
			{
				return QueryResult::errorResult(std::string("Insert failed: ") + e.what());
			}
		}

		QueryResult result = QueryResult::successResult("INSERT OK");
		result.affectedRows = insertedCount;
		return result;
	}

	// ==================== UpdateExecutor ====================

	UpdateExecutor::UpdateExecutor(std::shared_ptr<UpdateStatement> stmt, Table *table)
			: stmt(stmt), table(table) {}

	QueryResult UpdateExecutor::execute()
	{
		if (!table)
		{
			return QueryResult::errorResult("Table not found");
		}

		std::vector<std::string> colNames;
		for (const auto &col : table->getColumns())
		{
			colNames.push_back(col.getName());
		}

		int updatedCount = 0;
		std::vector<Row> &rows = table->getMutableRows();

		for (size_t i = 0; i < rows.size(); ++i)
		{
			if (!stmt->getWhereCondition() ||
					evaluateCondition(rows[i], colNames, stmt->getWhereCondition()))
			{

				Row newRow = rows[i];

				for (const auto &[colName, expr] : stmt->getSetClauses())
				{
					int colIdx = table->getColumnIndex(colName);
					if (colIdx >= 0)
					{
						Value newValue = evaluateExpression(rows[i], colNames, expr);
						newRow.setValue(colIdx, newValue);
					}
				}

				try
				{
					table->updateRow(i, newRow);
					updatedCount++;
				}
				catch (const std::exception &e)
				{
					return QueryResult::errorResult(std::string("Update failed: ") + e.what());
				}
			}
		}

		QueryResult result = QueryResult::successResult("UPDATE OK");
		result.affectedRows = updatedCount;
		return result;
	}

	bool UpdateExecutor::evaluateCondition(const Row &row, const std::vector<std::string> &colNames,
																				 const ExpressionPtr &condition) const
	{
		if (!condition)
			return true;

		auto binOp = std::dynamic_pointer_cast<BinaryOpExpression>(condition);
		if (binOp)
		{
			Value left = evaluateExpression(row, colNames, binOp->getLeft());
			Value right = evaluateExpression(row, colNames, binOp->getRight());

			switch (binOp->getOperator())
			{
			case BinaryOpExpression::Operator::EQ:
				return left == right;
			case BinaryOpExpression::Operator::NE:
				return left != right;
			case BinaryOpExpression::Operator::LT:
				return left < right;
			case BinaryOpExpression::Operator::LE:
				return left <= right;
			case BinaryOpExpression::Operator::GT:
				return left > right;
			case BinaryOpExpression::Operator::GE:
				return left >= right;
			case BinaryOpExpression::Operator::AND:
				return evaluateCondition(row, colNames, binOp->getLeft()) &&
							 evaluateCondition(row, colNames, binOp->getRight());
			case BinaryOpExpression::Operator::OR:
				return evaluateCondition(row, colNames, binOp->getLeft()) ||
							 evaluateCondition(row, colNames, binOp->getRight());
			default:
				return true;
			}
		}

		return true;
	}

	Value UpdateExecutor::evaluateExpression(const Row &row, const std::vector<std::string> &colNames,
																					 const ExpressionPtr &expr) const
	{
		auto literal = std::dynamic_pointer_cast<LiteralExpression>(expr);
		if (literal)
			return literal->getValue();

		auto colRef = std::dynamic_pointer_cast<ColumnRefExpression>(expr);
		if (colRef)
		{
			int idx = -1;
			for (size_t i = 0; i < colNames.size(); ++i)
			{
				if (colNames[i] == colRef->getColumn())
				{
					idx = i;
					break;
				}
			}
			if (idx >= 0 && idx < (int)row.getColumnCount())
			{
				return row.getValue(idx);
			}
			return Value();
		}

		return Value();
	}

	// ==================== DeleteExecutor ====================

	DeleteExecutor::DeleteExecutor(std::shared_ptr<DeleteStatement> stmt, Table *table)
			: stmt(stmt), table(table) {}

	QueryResult DeleteExecutor::execute()
	{
		if (!table)
		{
			return QueryResult::errorResult("Table not found");
		}

		std::vector<std::string> colNames;
		for (const auto &col : table->getColumns())
		{
			colNames.push_back(col.getName());
		}

		int deletedCount = 0;
		const std::vector<Row> &rows = table->getAllRows();

		// Collect indices to delete (in reverse order to avoid index shifting)
		std::vector<int> indicesToDelete;
		for (int i = rows.size() - 1; i >= 0; --i)
		{
			if (!stmt->getWhereCondition() ||
					evaluateCondition(rows[i], colNames, stmt->getWhereCondition()))
			{
				indicesToDelete.push_back(i);
			}
		}

		// Delete in reverse order
		for (int idx : indicesToDelete)
		{
			try
			{
				table->deleteRow(idx);
				deletedCount++;
			}
			catch (const std::exception &e)
			{
				return QueryResult::errorResult(std::string("Delete failed: ") + e.what());
			}
		}

		QueryResult result = QueryResult::successResult("DELETE OK");
		result.affectedRows = deletedCount;
		return result;
	}

	bool DeleteExecutor::evaluateCondition(const Row &row, const std::vector<std::string> &colNames,
																				 const ExpressionPtr &condition) const
	{
		if (!condition)
			return true;

		auto binOp = std::dynamic_pointer_cast<BinaryOpExpression>(condition);
		if (binOp)
		{
			Value left = evaluateExpression(row, colNames, binOp->getLeft());
			Value right = evaluateExpression(row, colNames, binOp->getRight());

			switch (binOp->getOperator())
			{
			case BinaryOpExpression::Operator::EQ:
				return left == right;
			case BinaryOpExpression::Operator::NE:
				return left != right;
			case BinaryOpExpression::Operator::LT:
				return left < right;
			case BinaryOpExpression::Operator::LE:
				return left <= right;
			case BinaryOpExpression::Operator::GT:
				return left > right;
			case BinaryOpExpression::Operator::GE:
				return left >= right;
			case BinaryOpExpression::Operator::AND:
				return evaluateCondition(row, colNames, binOp->getLeft()) &&
							 evaluateCondition(row, colNames, binOp->getRight());
			case BinaryOpExpression::Operator::OR:
				return evaluateCondition(row, colNames, binOp->getLeft()) ||
							 evaluateCondition(row, colNames, binOp->getRight());
			default:
				return true;
			}
		}

		return true;
	}

	Value DeleteExecutor::evaluateExpression(const Row &row, const std::vector<std::string> &colNames,
																					 const ExpressionPtr &expr) const
	{
		auto literal = std::dynamic_pointer_cast<LiteralExpression>(expr);
		if (literal)
			return literal->getValue();

		auto colRef = std::dynamic_pointer_cast<ColumnRefExpression>(expr);
		if (colRef)
		{
			int idx = -1;
			for (size_t i = 0; i < colNames.size(); ++i)
			{
				if (colNames[i] == colRef->getColumn())
				{
					idx = i;
					break;
				}
			}
			if (idx >= 0 && idx < (int)row.getColumnCount())
			{
				return row.getValue(idx);
			}
			return Value();
		}

		return Value();
	}

	// ==================== CreateTableExecutor ====================

	CreateTableExecutor::CreateTableExecutor(std::shared_ptr<CreateTableStatement> stmt,
																					 std::map<std::string, std::unique_ptr<Table>> *tables)
			: stmt(stmt), tables(tables) {}

	QueryResult CreateTableExecutor::execute()
	{
		if (!tables)
		{
			return QueryResult::errorResult("Internal error: no tables map");
		}

		if (tables->count(stmt->getTableName()))
		{
			return QueryResult::errorResult("Table '" + stmt->getTableName() + "' already exists");
		}

		try
		{
			auto table = std::make_unique<Table>(stmt->getTableName());

			for (const auto &colDef : stmt->getColumns())
			{
				DataType dataType = stringToDataType(colDef.getType());
				const bool nullable = !colDef.isNotNull() && !colDef.isPrimaryKey();
				Column col(colDef.getName(), dataType, nullable, colDef.isPrimaryKey(), colDef.isUnique());
				table->addColumn(col);
			}

			(*tables)[stmt->getTableName()] = std::move(table);

			QueryResult result = QueryResult::successResult("CREATE TABLE OK");
			return result;
		}
		catch (const std::exception &e)
		{
			return QueryResult::errorResult(std::string("CREATE TABLE failed: ") + e.what());
		}
	}

} // namespace db
