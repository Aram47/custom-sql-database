#pragma once

#include <string>
#include <vector>
#include <memory>
#include "include/parser/Token.h"
#include "include/utils/Exceptions.h"

namespace db
{

	class Lexer
	{
	public:
		explicit Lexer(const std::string &input);

		Token nextToken();
		Token peekToken(int offset = 0);
		bool hasMoreTokens() const;

		std::vector<Token> getAllTokens();

	private:
		std::string input;
		size_t pos;
		int line;
		int column;
		std::vector<Token> tokens;
		size_t tokenIndex;
		bool tokenized;

		void tokenize();
		Token scanToken();
		Token scanString(char quote);
		Token scanNumber();
		Token scanIdentifier();
		Token makeToken(TokenType type, const std::string &lexeme);

		char currentChar() const;
		char peekChar(size_t offset = 1) const;
		void advance();
		void skipWhitespace();
		void skipComment();

		bool isAtEnd() const;
		bool isDigit(char c) const;
		bool isAlpha(char c) const;
		bool isAlphaNumeric(char c) const;

		TokenType getKeywordTokenType(const std::string &keyword) const;
	};

} // namespace db
