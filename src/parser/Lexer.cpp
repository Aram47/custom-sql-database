#include "include/parser/Lexer.h"
#include <cctype>
#include <algorithm>

namespace db
{

	Lexer::Lexer(const std::string &input)
			: input(input), pos(0), line(1), column(1), tokenIndex(0), tokenized(false) {}

	Token Lexer::nextToken()
	{
		if (!tokenized)
		{
			tokenize();
			tokenized = true;
		}

		if (tokenIndex < tokens.size())
		{
			return tokens[tokenIndex++];
		}
		return Token(TokenType::END_OF_INPUT, "", line, column);
	}

	Token Lexer::peekToken(int offset)
	{
		if (!tokenized)
		{
			tokenize();
			tokenized = true;
		}

		if (tokenIndex + offset < tokens.size())
		{
			return tokens[tokenIndex + offset];
		}
		return Token(TokenType::END_OF_INPUT, "", line, column);
	}

	bool Lexer::hasMoreTokens() const
	{
		if (!tokenized)
			return !input.empty();
		return tokenIndex < tokens.size();
	}

	std::vector<Token> Lexer::getAllTokens()
	{
		if (!tokenized)
		{
			tokenize();
			tokenized = true;
		}
		return tokens;
	}

	void Lexer::tokenize()
	{
		tokens.clear();

		while (!isAtEnd())
		{
			skipWhitespace();

			if (isAtEnd())
				break;

			// Check for comments
			if (currentChar() == '-' && peekChar(1) == '-')
			{
				skipComment();
				continue;
			}

			Token token = scanToken();
			if (token.getType() != TokenType::UNKNOWN || !token.getLexeme().empty())
			{
				tokens.push_back(token);
			}
		}

		tokens.push_back(Token(TokenType::END_OF_INPUT, "", line, column));
	}

	Token Lexer::scanToken()
	{
		char c = currentChar();
		advance();

		// String literals
		if (c == '\'' || c == '"')
		{
			return scanString(c);
		}

		// Numbers
		if (isDigit(c))
		{
			pos--; // Back up to reprocess the digit
			return scanNumber();
		}

		// Identifiers and keywords
		if (isAlpha(c) || c == '_')
		{
			pos--; // Back up
			return scanIdentifier();
		}

		// Operators and delimiters
		switch (c)
		{
		case '=':
			return makeToken(TokenType::EQUAL, "=");
		case '<':
			if (currentChar() == '=')
			{
				advance();
				return makeToken(TokenType::LESS_EQUAL, "<=");
			}
			else if (currentChar() == '>')
			{
				advance();
				return makeToken(TokenType::NOT_EQUAL, "<>");
			}
			return makeToken(TokenType::LESS, "<");
		case '>':
			if (currentChar() == '=')
			{
				advance();
				return makeToken(TokenType::GREATER_EQUAL, ">=");
			}
			return makeToken(TokenType::GREATER, ">");
		case '!':
			if (currentChar() == '=')
			{
				advance();
				return makeToken(TokenType::NOT_EQUAL, "!=");
			}
			break;
		case '+':
			return makeToken(TokenType::PLUS, "+");
		case '-':
			return makeToken(TokenType::MINUS, "-");
		case '*':
			return makeToken(TokenType::ASTERISK, "*");
		case '/':
			return makeToken(TokenType::DIVIDE, "/");
		case '%':
			return makeToken(TokenType::MODULO, "%");
		case '(':
			return makeToken(TokenType::LPAREN, "(");
		case ')':
			return makeToken(TokenType::RPAREN, ")");
		case ',':
			return makeToken(TokenType::COMMA, ",");
		case '.':
			return makeToken(TokenType::DOT, ".");
		case ';':
			return makeToken(TokenType::SEMICOLON, ";");
		}

		return Token(TokenType::UNKNOWN, std::string(1, c), line, column);
	}

	Token Lexer::scanString(char quote)
	{
		std::string value;
		int startLine = line;
		int startCol = column;

		while (!isAtEnd() && currentChar() != quote)
		{
			if (currentChar() == '\\')
			{
				advance();
				if (!isAtEnd())
				{
					switch (currentChar())
					{
					case 'n':
						value += '\n';
						break;
					case 't':
						value += '\t';
						break;
					case 'r':
						value += '\r';
						break;
					case '\\':
						value += '\\';
						break;
					case '"':
						value += '"';
						break;
					case '\'':
						value += '\'';
						break;
					default:
						value += currentChar();
					}
					advance();
				}
			}
			else
			{
				if (currentChar() == '\n')
				{
					line++;
					column = 0;
				}
				value += currentChar();
				advance();
			}
		}

		if (!isAtEnd() && currentChar() == quote)
		{
			advance(); // Consume closing quote
		}

		return Token(TokenType::STRING, value, startLine, startCol);
	}

	Token Lexer::scanNumber()
	{
		std::string number;
		int startCol = column;

		while (!isAtEnd() && isDigit(currentChar()))
		{
			number += currentChar();
			advance();
		}

		// Check for decimal point
		if (!isAtEnd() && currentChar() == '.' && isDigit(peekChar(1)))
		{
			number += currentChar();
			advance();
			while (!isAtEnd() && isDigit(currentChar()))
			{
				number += currentChar();
				advance();
			}
		}

		// Check for scientific notation
		if (!isAtEnd() && (currentChar() == 'e' || currentChar() == 'E'))
		{
			number += currentChar();
			advance();
			if (!isAtEnd() && (currentChar() == '+' || currentChar() == '-'))
			{
				number += currentChar();
				advance();
			}
			while (!isAtEnd() && isDigit(currentChar()))
			{
				number += currentChar();
				advance();
			}
		}

		return Token(TokenType::NUMBER, number, line, startCol);
	}

	Token Lexer::scanIdentifier()
	{
		std::string identifier;
		int startCol = column;

		while (!isAtEnd() && isAlphaNumeric(currentChar()))
		{
			identifier += currentChar();
			advance();
		}

		TokenType type = getKeywordTokenType(identifier);
		return Token(type, identifier, line, startCol);
	}

	Token Lexer::makeToken(TokenType type, const std::string &lexeme)
	{
		return Token(type, lexeme, line, column - (int)lexeme.length());
	}

	char Lexer::currentChar() const
	{
		if (isAtEnd())
			return '\0';
		return input[pos];
	}

	char Lexer::peekChar(size_t offset) const
	{
		if (pos + offset >= input.length())
			return '\0';
		return input[pos + offset];
	}

	void Lexer::advance()
	{
		if (!isAtEnd())
		{
			if (input[pos] == '\n')
			{
				line++;
				column = 1;
			}
			else
			{
				column++;
			}
			pos++;
		}
	}

	void Lexer::skipWhitespace()
	{
		while (!isAtEnd() && std::isspace(currentChar()))
		{
			advance();
		}
	}

	void Lexer::skipComment()
	{
		while (!isAtEnd() && currentChar() != '\n')
		{
			advance();
		}
	}

	bool Lexer::isAtEnd() const
	{
		return pos >= input.length();
	}

	bool Lexer::isDigit(char c) const
	{
		return c >= '0' && c <= '9';
	}

	bool Lexer::isAlpha(char c) const
	{
		return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
	}

	bool Lexer::isAlphaNumeric(char c) const
	{
		return isAlpha(c) || isDigit(c);
	}

	TokenType Lexer::getKeywordTokenType(const std::string &keyword) const
	{
		std::string upper = keyword;
		std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);

		if (upper == "SELECT")
			return TokenType::SELECT;
		if (upper == "INSERT")
			return TokenType::INSERT;
		if (upper == "UPDATE")
			return TokenType::UPDATE;
		if (upper == "DELETE")
			return TokenType::DELETE;
		if (upper == "CREATE")
			return TokenType::CREATE;
		if (upper == "TABLE")
			return TokenType::TABLE;
		if (upper == "FROM")
			return TokenType::FROM;
		if (upper == "WHERE")
			return TokenType::WHERE;
		if (upper == "AND")
			return TokenType::AND;
		if (upper == "OR")
			return TokenType::OR;
		if (upper == "NOT")
			return TokenType::NOT;
		if (upper == "JOIN")
			return TokenType::JOIN;
		if (upper == "INNER")
			return TokenType::INNER;
		if (upper == "LEFT")
			return TokenType::LEFT;
		if (upper == "RIGHT")
			return TokenType::RIGHT;
		if (upper == "ON")
			return TokenType::ON;
		if (upper == "GROUP")
			return TokenType::GROUP;
		if (upper == "BY")
			return TokenType::BY;
		if (upper == "HAVING")
			return TokenType::HAVING;
		if (upper == "ORDER")
			return TokenType::ORDER;
		if (upper == "ASC")
			return TokenType::ASC;
		if (upper == "DESC")
			return TokenType::DESC;
		if (upper == "VALUES")
			return TokenType::VALUES;
		if (upper == "SET")
			return TokenType::SET;
		if (upper == "NULL")
			return TokenType::NULL_KW;
		if (upper == "AS")
			return TokenType::AS;
		if (upper == "DISTINCT")
			return TokenType::DISTINCT;
		if (upper == "COUNT")
			return TokenType::COUNT;
		if (upper == "SUM")
			return TokenType::SUM;
		if (upper == "AVG")
			return TokenType::AVG;
		if (upper == "MIN")
			return TokenType::MIN;
		if (upper == "MAX")
			return TokenType::MAX;

		return TokenType::IDENTIFIER;
	}

} // namespace db
