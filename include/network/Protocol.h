#pragma once

#include <string>
#include <memory>
#include <vector>
#include "include/executor/QueryExecutor.h"

namespace db
{

	class Protocol
	{
	public:
		// Request format: "QUERY|<sql>\n"
		// Response format: "OK|<result_data>\n" or "ERROR|<error_msg>\n"

		struct Request
		{
			std::string type; // "QUERY", "PING", etc.
			std::string data;
		};

		struct Response
		{
			bool success;
			std::string message;
			std::vector<std::string> columnNames;
			std::vector<std::vector<std::string>> rows;
		};

		static Request parseRequest(const std::string &message);
		static std::string formatResponse(const Response &response);
		static std::string formatQueryResult(const QueryResult &result);
	};

} // namespace db
