#pragma once

#include <memory>
#include <vector>
#include "include/parser/Token.h"
#include "include/parser/AST.h"
#include "include/parser/Lexer.h"
#include "include/utils/Exceptions.h"

namespace db
{

	class Parser
	{
	public:
		explicit Parser(const std::string &sql);
		explicit Parser(std::vector<Token> tokens);

		// Parse different statement types
		std::shared_ptr<SelectStatement> parseSelectStatement();
		std::shared_ptr<InsertStatement> parseInsertStatement();
		std::shared_ptr<UpdateStatement> parseUpdateStatement();
		std::shared_ptr<DeleteStatement> parseDeleteStatement();
		std::shared_ptr<CreateTableStatement> parseCreateTableStatement();

		// Generic statement parser
		std::variant<std::shared_ptr<SelectStatement>,
								 std::shared_ptr<InsertStatement>,
								 std::shared_ptr<UpdateStatement>,
								 std::shared_ptr<DeleteStatement>,
								 std::shared_ptr<CreateTableStatement>>
		parseStatement();

	private:
		std::vector<Token> tokens;
		size_t current;

		// Token management
		Token peek() const;
		Token peekAhead(size_t offset) const;
		Token advance();
		bool check(TokenType type) const;
		bool match(TokenType type);
		bool match(const std::vector<TokenType> &types);
		Token consume(TokenType type, const std::string &message);
		bool isAtEnd() const;

		// Expression parsing
		ExpressionPtr parseExpression();
		ExpressionPtr parseOrExpression();
		ExpressionPtr parseAndExpression();
		ExpressionPtr parseNotExpression();
		ExpressionPtr parseComparisonExpression();
		ExpressionPtr parseAdditiveExpression();
		ExpressionPtr parseMultiplicativeExpression();
		ExpressionPtr parseUnaryExpression();
		ExpressionPtr parsePrimaryExpression();
		ExpressionPtr parseIdentifierOrFunction();

		// Statement parsing helpers
		std::shared_ptr<SelectStatement> parseSelectClause();
		std::shared_ptr<InsertStatement> parseInsertClause();
		std::shared_ptr<UpdateStatement> parseUpdateClause();
		std::shared_ptr<DeleteStatement> parseDeleteClause();
		std::shared_ptr<CreateTableStatement> parseCreateTableClause();

		// Utility parsing methods
		std::string parseIdentifier();
		std::vector<ColumnDefinition> parseColumnDefinitions();
		ColumnDefinition parseColumnDefinition();

		// Error handling
		std::string getErrorMessage(const std::string &message) const;
	};

} // namespace db
