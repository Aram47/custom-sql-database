#include "include/parser/Parser.h"
#include <algorithm>
#include <stdexcept>

namespace db
{

	Parser::Parser(const std::string &sql) : current(0)
	{
		Lexer lexer(sql);
		tokens = lexer.getAllTokens();
	}

	Parser::Parser(std::vector<Token> tokens) : tokens(tokens), current(0) {}

	Token Parser::peek() const
	{
		if (isAtEnd())
			return Token(TokenType::END_OF_INPUT, "", 0, 0);
		return tokens[current];
	}

	Token Parser::peekAhead(size_t offset) const
	{
		if (current + offset >= tokens.size())
			return Token(TokenType::END_OF_INPUT, "", 0, 0);
		return tokens[current + offset];
	}

	Token Parser::advance()
	{
		if (!isAtEnd())
			current++;
		return tokens[current - 1];
	}

	bool Parser::check(TokenType type) const
	{
		if (isAtEnd())
			return false;
		return peek().getType() == type;
	}

	bool Parser::match(TokenType type)
	{
		if (check(type))
		{
			advance();
			return true;
		}
		return false;
	}

	bool Parser::match(const std::vector<TokenType> &types)
	{
		for (TokenType type : types)
		{
			if (check(type))
			{
				advance();
				return true;
			}
		}
		return false;
	}

	Token Parser::consume(TokenType type, const std::string &message)
	{
		if (check(type))
			return advance();
		throw ParseException(getErrorMessage(message));
	}

	bool Parser::isAtEnd() const
	{
		if (current >= tokens.size())
			return true;
		return tokens[current].getType() == TokenType::END_OF_INPUT;
	}

	std::variant<std::shared_ptr<SelectStatement>,
							 std::shared_ptr<InsertStatement>,
							 std::shared_ptr<UpdateStatement>,
							 std::shared_ptr<DeleteStatement>,
							 std::shared_ptr<CreateTableStatement>>
	Parser::parseStatement()
	{
		if (check(TokenType::SELECT))
		{
			return parseSelectStatement();
		}
		else if (check(TokenType::INSERT))
		{
			return parseInsertStatement();
		}
		else if (check(TokenType::UPDATE))
		{
			return parseUpdateStatement();
		}
		else if (check(TokenType::DELETE))
		{
			return parseDeleteStatement();
		}
		else if (check(TokenType::CREATE))
		{
			return parseCreateTableStatement();
		}
		throw ParseException("Unknown statement");
	}

	std::shared_ptr<SelectStatement> Parser::parseSelectStatement()
	{
		consume(TokenType::SELECT, "Expected SELECT");
		auto stmt = std::make_shared<SelectStatement>();

		// Check for DISTINCT
		if (match(TokenType::DISTINCT))
		{
			stmt->setDistinct(true);
		}

		// Parse select columns
		do
		{
			auto expr = parseExpression();
			std::string alias;
			if (match(TokenType::AS))
			{
				alias = consume(TokenType::IDENTIFIER, "Expected identifier").getLexeme();
			}
			stmt->addSelectColumn(expr, alias);
		} while (match(TokenType::COMMA));

		// Parse FROM clause
		if (match(TokenType::FROM))
		{
			std::string table = consume(TokenType::IDENTIFIER, "Expected table name").getLexeme();
			std::string alias = table;
			if (match(TokenType::AS))
			{
				alias = consume(TokenType::IDENTIFIER, "Expected alias").getLexeme();
			}
			stmt->setFromTable(table, alias);

			// Parse JOINs
			while (check(TokenType::INNER) || check(TokenType::LEFT) || check(TokenType::RIGHT) ||
						 check(TokenType::JOIN))
			{
				std::string joinType;
				if (match(TokenType::INNER))
				{
					joinType = "INNER";
				}
				else if (match(TokenType::LEFT))
				{
					joinType = "LEFT";
				}
				else if (match(TokenType::RIGHT))
				{
					joinType = "RIGHT";
				}
				else
				{
					joinType = "INNER";
				}
				consume(TokenType::JOIN, "Expected JOIN");

				std::string joinTable = consume(TokenType::IDENTIFIER, "Expected table name").getLexeme();
				std::string joinAlias = joinTable;
				if (match(TokenType::AS))
				{
					joinAlias = consume(TokenType::IDENTIFIER, "Expected alias").getLexeme();
				}

				ExpressionPtr condition;
				if (match(TokenType::ON))
				{
					condition = parseExpression();
				}

				stmt->addJoin(joinType, joinTable, joinAlias, condition);
			}
		}

		// Parse WHERE clause
		if (match(TokenType::WHERE))
		{
			stmt->setWhereCondition(parseExpression());
		}

		// Parse GROUP BY clause
		if (match(TokenType::GROUP))
		{
			consume(TokenType::BY, "Expected BY");
			do
			{
				stmt->addGroupByColumn(parseExpression());
			} while (match(TokenType::COMMA));
		}

		// Parse HAVING clause
		if (match(TokenType::HAVING))
		{
			stmt->setHavingCondition(parseExpression());
		}

		// Parse ORDER BY clause
		if (match(TokenType::ORDER))
		{
			consume(TokenType::BY, "Expected BY");
			do
			{
				auto expr = parseExpression();
				bool ascending = !match(TokenType::DESC);
				if (!ascending)
				{
					// Already consumed DESC
				}
				else
				{
					match(TokenType::ASC); // Optional ASC
				}
				stmt->addOrderByColumn(expr, ascending);
			} while (match(TokenType::COMMA));
		}

		// Parse LIMIT clause
		if (check(TokenType::IDENTIFIER) && peek().getLexeme() == "LIMIT")
		{
			advance();
			if (check(TokenType::NUMBER))
			{
				stmt->setLimit(std::stoi(advance().getLexeme()));
			}
		}

		// Parse OFFSET clause
		if (check(TokenType::IDENTIFIER) && peek().getLexeme() == "OFFSET")
		{
			advance();
			if (check(TokenType::NUMBER))
			{
				stmt->setOffset(std::stoi(advance().getLexeme()));
			}
		}

		return stmt;
	}

	std::shared_ptr<InsertStatement> Parser::parseInsertStatement()
	{
		consume(TokenType::INSERT, "Expected INSERT");
		consume(TokenType::IDENTIFIER, "Expected INTO"); // INTO

		std::string table = consume(TokenType::IDENTIFIER, "Expected table name").getLexeme();
		auto stmt = std::make_shared<InsertStatement>(table);

		// Parse column list
		if (match(TokenType::LPAREN))
		{
			do
			{
				stmt->addColumn(consume(TokenType::IDENTIFIER, "Expected column name").getLexeme());
			} while (match(TokenType::COMMA));
			consume(TokenType::RPAREN, "Expected )");
		}

		// Parse VALUES
		consume(TokenType::VALUES, "Expected VALUES");

		do
		{
			consume(TokenType::LPAREN, "Expected (");
			std::vector<Value> values;
			do
			{
				if (match(TokenType::NULL_KW))
				{
					values.push_back(Value());
				}
				else if (check(TokenType::NUMBER))
				{
					std::string numStr = advance().getLexeme();
					if (numStr.find('.') != std::string::npos)
					{
						values.push_back(Value(std::stod(numStr)));
					}
					else
					{
						values.push_back(Value(static_cast<int64_t>(std::stoll(numStr))));
					}
				}
				else if (check(TokenType::STRING))
				{
					values.push_back(Value(advance().getLexeme()));
				}
				else
				{
					throw ParseException("Expected value in INSERT");
				}
			} while (match(TokenType::COMMA));
			consume(TokenType::RPAREN, "Expected )");
			stmt->addValues(values);
		} while (match(TokenType::COMMA));

		return stmt;
	}

	std::shared_ptr<UpdateStatement> Parser::parseUpdateStatement()
	{
		consume(TokenType::UPDATE, "Expected UPDATE");

		std::string table = consume(TokenType::IDENTIFIER, "Expected table name").getLexeme();
		auto stmt = std::make_shared<UpdateStatement>(table);

		consume(TokenType::SET, "Expected SET");

		do
		{
			std::string column = consume(TokenType::IDENTIFIER, "Expected column name").getLexeme();
			consume(TokenType::EQUAL, "Expected =");
			auto value = parseExpression();
			stmt->addSetClause(column, value);
		} while (match(TokenType::COMMA));

		if (match(TokenType::WHERE))
		{
			stmt->setWhereCondition(parseExpression());
		}

		return stmt;
	}

	std::shared_ptr<DeleteStatement> Parser::parseDeleteStatement()
	{
		consume(TokenType::DELETE, "Expected DELETE");
		consume(TokenType::FROM, "Expected FROM");

		std::string table = consume(TokenType::IDENTIFIER, "Expected table name").getLexeme();
		auto stmt = std::make_shared<DeleteStatement>(table);

		if (match(TokenType::WHERE))
		{
			stmt->setWhereCondition(parseExpression());
		}

		return stmt;
	}

	std::shared_ptr<CreateTableStatement> Parser::parseCreateTableStatement()
	{
		consume(TokenType::CREATE, "Expected CREATE");
		consume(TokenType::TABLE, "Expected TABLE");

		std::string table = consume(TokenType::IDENTIFIER, "Expected table name").getLexeme();
		auto stmt = std::make_shared<CreateTableStatement>(table);

		consume(TokenType::LPAREN, "Expected (");

		do
		{
			ColumnDefinition col = parseColumnDefinition();
			stmt->addColumn(col);
		} while (match(TokenType::COMMA));

		consume(TokenType::RPAREN, "Expected )");

		return stmt;
	}

	ColumnDefinition Parser::parseColumnDefinition()
	{
		std::string name = consume(TokenType::IDENTIFIER, "Expected column name").getLexeme();
		std::string type = consume(TokenType::IDENTIFIER, "Expected type").getLexeme();

		bool notNull = false;
		bool primaryKey = false;
		bool unique = false;

		while (check(TokenType::NOT) || check(TokenType::IDENTIFIER))
		{
			if (match(TokenType::NOT))
			{
				consume(TokenType::NULL_KW, "Expected NULL");
				notNull = true;
			}
			else if (check(TokenType::IDENTIFIER))
			{
				if (peek().getLexeme() == "PRIMARY")
				{
					advance();
					consume(TokenType::IDENTIFIER, "Expected KEY"); // "KEY"
					primaryKey = true;
				}
				else if (peek().getLexeme() == "UNIQUE")
				{
					advance();
					unique = true;
				}
				else
				{
					break;
				}
			}
			else
			{
				break;
			}
		}

		return ColumnDefinition(name, type, notNull, primaryKey, unique);
	}

	ExpressionPtr Parser::parseExpression()
	{
		return parseOrExpression();
	}

	ExpressionPtr Parser::parseOrExpression()
	{
		auto expr = parseAndExpression();

		while (match(TokenType::OR))
		{
			auto right = parseAndExpression();
			expr = std::make_shared<BinaryOpExpression>(expr, BinaryOpExpression::Operator::OR, right);
		}

		return expr;
	}

	ExpressionPtr Parser::parseAndExpression()
	{
		auto expr = parseNotExpression();

		while (match(TokenType::AND))
		{
			auto right = parseNotExpression();
			expr = std::make_shared<BinaryOpExpression>(expr, BinaryOpExpression::Operator::AND, right);
		}

		return expr;
	}

	ExpressionPtr Parser::parseNotExpression()
	{
		if (match(TokenType::NOT))
		{
			auto expr = parseNotExpression();
			return std::make_shared<UnaryOpExpression>(UnaryOpExpression::Operator::NOT, expr);
		}

		return parseComparisonExpression();
	}

	ExpressionPtr Parser::parseComparisonExpression()
	{
		auto expr = parseAdditiveExpression();

		if (match(TokenType::EQUAL))
		{
			auto right = parseAdditiveExpression();
			expr = std::make_shared<BinaryOpExpression>(expr, BinaryOpExpression::Operator::EQ, right);
		}
		else if (match(TokenType::NOT_EQUAL))
		{
			auto right = parseAdditiveExpression();
			expr = std::make_shared<BinaryOpExpression>(expr, BinaryOpExpression::Operator::NE, right);
		}
		else if (match(TokenType::LESS))
		{
			auto right = parseAdditiveExpression();
			expr = std::make_shared<BinaryOpExpression>(expr, BinaryOpExpression::Operator::LT, right);
		}
		else if (match(TokenType::LESS_EQUAL))
		{
			auto right = parseAdditiveExpression();
			expr = std::make_shared<BinaryOpExpression>(expr, BinaryOpExpression::Operator::LE, right);
		}
		else if (match(TokenType::GREATER))
		{
			auto right = parseAdditiveExpression();
			expr = std::make_shared<BinaryOpExpression>(expr, BinaryOpExpression::Operator::GT, right);
		}
		else if (match(TokenType::GREATER_EQUAL))
		{
			auto right = parseAdditiveExpression();
			expr = std::make_shared<BinaryOpExpression>(expr, BinaryOpExpression::Operator::GE, right);
		}

		return expr;
	}

	ExpressionPtr Parser::parseAdditiveExpression()
	{
		auto expr = parseMultiplicativeExpression();

		while (true)
		{
			if (match(TokenType::PLUS))
			{
				auto right = parseMultiplicativeExpression();
				expr = std::make_shared<BinaryOpExpression>(expr, BinaryOpExpression::Operator::PLUS, right);
			}
			else if (match(TokenType::MINUS))
			{
				auto right = parseMultiplicativeExpression();
				expr = std::make_shared<BinaryOpExpression>(expr, BinaryOpExpression::Operator::MINUS, right);
			}
			else
			{
				break;
			}
		}

		return expr;
	}

	ExpressionPtr Parser::parseMultiplicativeExpression()
	{
		auto expr = parseUnaryExpression();

		while (true)
		{
			if (match(TokenType::MULTIPLY))
			{
				auto right = parseUnaryExpression();
				expr = std::make_shared<BinaryOpExpression>(expr, BinaryOpExpression::Operator::MUL, right);
			}
			else if (match(TokenType::DIVIDE))
			{
				auto right = parseUnaryExpression();
				expr = std::make_shared<BinaryOpExpression>(expr, BinaryOpExpression::Operator::DIV, right);
			}
			else if (match(TokenType::MODULO))
			{
				auto right = parseUnaryExpression();
				expr = std::make_shared<BinaryOpExpression>(expr, BinaryOpExpression::Operator::MOD, right);
			}
			else
			{
				break;
			}
		}

		return expr;
	}

	ExpressionPtr Parser::parseUnaryExpression()
	{
		if (match(TokenType::MINUS))
		{
			auto expr = parseUnaryExpression();
			return std::make_shared<UnaryOpExpression>(UnaryOpExpression::Operator::MINUS, expr);
		}

		return parsePrimaryExpression();
	}

	ExpressionPtr Parser::parsePrimaryExpression()
	{
		if (match(TokenType::NULL_KW))
		{
			return std::make_shared<LiteralExpression>(Value());
		}

		if (check(TokenType::NUMBER))
		{
			std::string numStr = advance().getLexeme();
			if (numStr.find('.') != std::string::npos)
			{
				return std::make_shared<LiteralExpression>(Value(std::stod(numStr)));
			}
			else
			{
				return std::make_shared<LiteralExpression>(Value(static_cast<int64_t>(std::stoll(numStr))));
			}
		}

		if (check(TokenType::STRING))
		{
			return std::make_shared<LiteralExpression>(Value(advance().getLexeme()));
		}

		if (match(TokenType::LPAREN))
		{
			auto expr = parseExpression();
			consume(TokenType::RPAREN, "Expected )");
			return expr;
		}

		if (check(TokenType::IDENTIFIER) || check(TokenType::ASTERISK))
		{
			return parseIdentifierOrFunction();
		}

		throw ParseException("Expected expression");
	}

	ExpressionPtr Parser::parseIdentifierOrFunction()
	{
		std::string name = advance().getLexeme();

		// Check for function call
		if (check(TokenType::LPAREN))
		{
			advance();
			std::vector<ExpressionPtr> args;
			if (!check(TokenType::RPAREN))
			{
				do
				{
					args.push_back(parseExpression());
				} while (match(TokenType::COMMA));
			}
			consume(TokenType::RPAREN, "Expected )");
			return std::make_shared<FunctionCallExpression>(name, args);
		}

		// Check for column reference (table.column)
		if (match(TokenType::DOT))
		{
			std::string column = advance().getLexeme();
			return std::make_shared<ColumnRefExpression>(name, column);
		}

		// Just a column reference
		if (name == "*")
		{
			return std::make_shared<IdentifierExpression>("*");
		}

		return std::make_shared<ColumnRefExpression>(name);
	}

	std::string Parser::getErrorMessage(const std::string &message) const
	{
		std::string msg = message + " at line " + std::to_string(peek().getLine()) +
											", column " + std::to_string(peek().getColumn()) +
											" (token: '" + peek().getLexeme() + "')";
		return msg;
	}

} // namespace db
