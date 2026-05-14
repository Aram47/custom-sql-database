#pragma once

#include <vector>
#include <memory>
#include <stdexcept>
#include "include/types/Value.h"

namespace db
{

	class Row
	{
	public:
		// Constructors
		Row();
		explicit Row(const std::vector<Value> &values);
		Row(const Row &) = default;
		Row &operator=(const Row &) = default;
		Row(Row &&) = default;
		Row &operator=(Row &&) = default;

		// Data access
		void addValue(const Value &value);
		void setValue(size_t index, const Value &value);
		const Value &getValue(size_t index) const;
		Value &getMutableValue(size_t index);

		// Row properties
		size_t getColumnCount() const;
		bool isEmpty() const;

		// Comparison
		bool operator==(const Row &other) const;
		bool operator!=(const Row &other) const;

		// Serialization
		std::vector<Value>::const_iterator begin() const;
		std::vector<Value>::const_iterator end() const;
		std::vector<Value>::iterator begin();
		std::vector<Value>::iterator end();

		// String representation
		std::string toString() const;

	private:
		std::vector<Value> values;
	};

} // namespace db
