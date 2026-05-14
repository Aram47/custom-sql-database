#include "include/core/Row.h"

namespace db
{

	Row::Row() = default;

	Row::Row(const std::vector<Value> &values) : values(values) {}

	void Row::addValue(const Value &value)
	{
		values.push_back(value);
	}

	void Row::setValue(size_t index, const Value &value)
	{
		if (index >= values.size())
		{
			throw std::out_of_range("Row index out of range");
		}
		values[index] = value;
	}

	const Value &Row::getValue(size_t index) const
	{
		if (index >= values.size())
		{
			throw std::out_of_range("Row index out of range");
		}
		return values[index];
	}

	Value &Row::getMutableValue(size_t index)
	{
		if (index >= values.size())
		{
			throw std::out_of_range("Row index out of range");
		}
		return values[index];
	}

	size_t Row::getColumnCount() const
	{
		return values.size();
	}

	bool Row::isEmpty() const
	{
		return values.empty();
	}

	bool Row::operator==(const Row &other) const
	{
		if (values.size() != other.values.size())
			return false;
		for (size_t i = 0; i < values.size(); ++i)
		{
			if (values[i] != other.values[i])
				return false;
		}
		return true;
	}

	bool Row::operator!=(const Row &other) const
	{
		return !(*this == other);
	}

	std::vector<Value>::const_iterator Row::begin() const
	{
		return values.begin();
	}

	std::vector<Value>::const_iterator Row::end() const
	{
		return values.end();
	}

	std::vector<Value>::iterator Row::begin()
	{
		return values.begin();
	}

	std::vector<Value>::iterator Row::end()
	{
		return values.end();
	}

	std::string Row::toString() const
	{
		std::string str = "Row(";
		for (size_t i = 0; i < values.size(); ++i)
		{
			if (i > 0)
				str += ", ";
			str += values[i].toString();
		}
		str += ")";
		return str;
	}

} // namespace db
