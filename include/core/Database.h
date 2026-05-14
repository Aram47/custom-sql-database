#pragma once

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include "include/executor/QueryExecutor.h"
#include "include/core/Table.h"
#include "include/parser/Parser.h"
#include "include/utils/Exceptions.h"

namespace db
{

	class Database
	{
	public:
		explicit Database(std::string storageDirectory = "data");

		Database(const Database &) = delete;
		Database &operator=(const Database &) = delete;

		void loadFromDisk();

		QueryResult executeQuery(const std::string &sql);

		void createTable(const std::string &tableName);
		void dropTable(const std::string &tableName);
		Table *getTable(const std::string &tableName);

		std::vector<std::string> listTables() const;
		bool hasTable(const std::string &tableName) const;

	private:
		std::string storageDirectory;
		mutable std::recursive_mutex dbMutex;
		std::map<std::string, std::unique_ptr<Table>> tables;

		QueryResult persistAfterMutation(QueryResult result);
		QueryResult executeSelectStatement(std::shared_ptr<SelectStatement> stmt);
		QueryResult executeInsertStatement(std::shared_ptr<InsertStatement> stmt);
		QueryResult executeUpdateStatement(std::shared_ptr<UpdateStatement> stmt);
		QueryResult executeDeleteStatement(std::shared_ptr<DeleteStatement> stmt);
		QueryResult executeCreateTableStatement(std::shared_ptr<CreateTableStatement> stmt);
	};

} // namespace db
