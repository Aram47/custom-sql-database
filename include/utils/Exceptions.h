#pragma once

#include <stdexcept>
#include <string>

namespace db
{

	// Base exception class
	class DatabaseException : public std::runtime_error
	{
	public:
		explicit DatabaseException(const std::string &message)
				: std::runtime_error(message) {}
	};

	// Parse errors
	class ParseException : public DatabaseException
	{
	public:
		explicit ParseException(const std::string &message)
				: DatabaseException("Parse Error: " + message) {}
	};

	// Constraint violations
	class ConstraintException : public DatabaseException
	{
	public:
		explicit ConstraintException(const std::string &message)
				: DatabaseException("Constraint Error: " + message) {}
	};

	// Type conversion errors
	class TypeException : public DatabaseException
	{
	public:
		explicit TypeException(const std::string &message)
				: DatabaseException("Type Error: " + message) {}
	};

	// Table/column not found
	class NotFoundException : public DatabaseException
	{
	public:
		explicit NotFoundException(const std::string &message)
				: DatabaseException("Not Found: " + message) {}
	};

	// Invalid operations
	class InvalidOperationException : public DatabaseException
	{
	public:
		explicit InvalidOperationException(const std::string &message)
				: DatabaseException("Invalid Operation: " + message) {}
	};

	// Storage/IO errors
	class StorageException : public DatabaseException
	{
	public:
		explicit StorageException(const std::string &message)
				: DatabaseException("Storage Error: " + message) {}
	};

	// Network errors
	class NetworkException : public DatabaseException
	{
	public:
		explicit NetworkException(const std::string &message)
				: DatabaseException("Network Error: " + message) {}
	};

} // namespace db
