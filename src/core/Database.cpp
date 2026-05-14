#include "include/core/Database.h"
#include "include/utils/Logger.h"
#include "include/parser/Parser.h"
#include "include/executor/QueryExecutor.h"
#include "include/storage/PersistenceManager.h"

namespace db
{

	Database::Database(std::string storageDirectory)
			: storageDirectory(std::move(storageDirectory)) {}

	void Database::loadFromDisk()
	{
		std::lock_guard<std::recursive_mutex> lock(dbMutex);
		try
		{
			tables = PersistenceManager::loadDatabase(storageDirectory);
		}
		catch (const StorageException &e)
		{
			DB_LOG_ERROR("Failed to load database from disk: ", e.what());
			throw;
		}
	}

	QueryResult Database::persistAfterMutation(QueryResult result)
	{
		if (!result.success)
		{
			return result;
		}
		try
		{
			PersistenceManager::saveDatabase(tables, storageDirectory);
			return result;
		}
		catch (const StorageException &e)
		{
			DB_LOG_ERROR("Persistence failed: ", e.what());
			return QueryResult::errorResult(std::string("Persistence failed: ") + e.what());
		}
	}

	QueryResult Database::executeQuery(const std::string &sql)
	{
		std::lock_guard<std::recursive_mutex> lock(dbMutex);

		try
		{
			DB_LOG_DEBUG("Executing query: ", sql);

			Parser parser(sql);
			auto stmt = parser.parseStatement();

			if (auto selectStmt = std::get_if<std::shared_ptr<SelectStatement>>(&stmt))
			{
				return executeSelectStatement(*selectStmt);
			}
			if (auto insertStmt = std::get_if<std::shared_ptr<InsertStatement>>(&stmt))
			{
				return executeInsertStatement(*insertStmt);
			}
			if (auto updateStmt = std::get_if<std::shared_ptr<UpdateStatement>>(&stmt))
			{
				return executeUpdateStatement(*updateStmt);
			}
			if (auto deleteStmt = std::get_if<std::shared_ptr<DeleteStatement>>(&stmt))
			{
				return executeDeleteStatement(*deleteStmt);
			}
			if (auto createStmt = std::get_if<std::shared_ptr<CreateTableStatement>>(&stmt))
			{
				return executeCreateTableStatement(*createStmt);
			}

			return QueryResult::errorResult("Unknown statement type");
		}
		catch (const ParseException &e)
		{
			DB_LOG_ERROR("Parse error: ", e.what());
			return QueryResult::errorResult(e.what());
		}
		catch (const NotFoundException &e)
		{
			DB_LOG_ERROR("Not found: ", e.what());
			return QueryResult::errorResult(e.what());
		}
		catch (const ConstraintException &e)
		{
			DB_LOG_ERROR("Constraint error: ", e.what());
			return QueryResult::errorResult(e.what());
		}
		catch (const DatabaseException &e)
		{
			DB_LOG_ERROR("Database error: ", e.what());
			return QueryResult::errorResult(e.what());
		}
		catch (const std::exception &e)
		{
			DB_LOG_ERROR("Unexpected error: ", e.what());
			return QueryResult::errorResult(std::string("Unexpected error: ") + e.what());
		}
	}

	void Database::createTable(const std::string &tableName)
	{
		std::lock_guard<std::recursive_mutex> lock(dbMutex);
		if (tables.count(tableName))
		{
			throw ConstraintException("Table '" + tableName + "' already exists");
		}
		tables[tableName] = std::make_unique<Table>(tableName);
	}

	void Database::dropTable(const std::string &tableName)
	{
		std::lock_guard<std::recursive_mutex> lock(dbMutex);
		if (!tables.count(tableName))
		{
			throw NotFoundException("Table '" + tableName + "' not found");
		}
		tables.erase(tableName);
	}

	Table *Database::getTable(const std::string &tableName)
	{
		std::lock_guard<std::recursive_mutex> lock(dbMutex);
		if (!tables.count(tableName))
		{
			return nullptr;
		}
		return tables[tableName].get();
	}

	std::vector<std::string> Database::listTables() const
	{
		std::lock_guard<std::recursive_mutex> lock(dbMutex);
		std::vector<std::string> result;
		for (const auto &[name, table] : tables)
		{
			result.push_back(name);
		}
		return result;
	}

	bool Database::hasTable(const std::string &tableName) const
	{
		std::lock_guard<std::recursive_mutex> lock(dbMutex);
		return tables.count(tableName) > 0;
	}

	QueryResult Database::executeSelectStatement(std::shared_ptr<SelectStatement> stmt)
	{
		if (stmt->getFromTable().empty())
		{
			return QueryResult::errorResult("SELECT requires FROM clause");
		}

		Table *table = getTable(stmt->getFromTable());
		if (!table)
		{
			return QueryResult::errorResult("Table '" + stmt->getFromTable() + "' not found");
		}

		SelectExecutor executor(stmt, table);
		return executor.execute();
	}

	QueryResult Database::executeInsertStatement(std::shared_ptr<InsertStatement> stmt)
	{
		Table *table = getTable(stmt->getTable());
		if (!table)
		{
			return QueryResult::errorResult("Table '" + stmt->getTable() + "' not found");
		}

		InsertExecutor executor(stmt, table);
		return persistAfterMutation(executor.execute());
	}

	QueryResult Database::executeUpdateStatement(std::shared_ptr<UpdateStatement> stmt)
	{
		Table *table = getTable(stmt->getTable());
		if (!table)
		{
			return QueryResult::errorResult("Table '" + stmt->getTable() + "' not found");
		}

		UpdateExecutor executor(stmt, table);
		return persistAfterMutation(executor.execute());
	}

	QueryResult Database::executeDeleteStatement(std::shared_ptr<DeleteStatement> stmt)
	{
		Table *table = getTable(stmt->getTable());
		if (!table)
		{
			return QueryResult::errorResult("Table '" + stmt->getTable() + "' not found");
		}

		DeleteExecutor executor(stmt, table);
		return persistAfterMutation(executor.execute());
	}

	QueryResult Database::executeCreateTableStatement(std::shared_ptr<CreateTableStatement> stmt)
	{
		CreateTableExecutor executor(stmt, &tables);
		return persistAfterMutation(executor.execute());
	}

} // namespace db
