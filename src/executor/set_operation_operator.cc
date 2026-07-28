#include "executor/set_operation_operator.h"

#include <unordered_set>

#include "executor/row_key.h"
#include "types/data_type.h"

namespace db {

namespace {

QueryResult makeArityError(size_t leftWidth, size_t rightWidth) {
  return QueryResult::error_result(
      "Set operation column count mismatch: left has " +
      std::to_string(leftWidth) + ", right has " + std::to_string(rightWidth));
}

QueryResult makeTypeError(size_t columnIndex, DataType leftType,
                          DataType rightType) {
  return QueryResult::error_result(
      "Set operation type mismatch at column " + std::to_string(columnIndex) +
      ": " + data_type_to_string(leftType) + " vs " +
      data_type_to_string(rightType));
}

bool validateColumnTypes(const QueryResult &left, const QueryResult &right,
                         QueryResult &errorOut) {
  if (left.rows.empty() || right.rows.empty()) {
    return true;
  }
  const std::vector<Value> &leftRow = left.rows.front();
  const std::vector<Value> &rightRow = right.rows.front();
  for (size_t i = 0; i < leftRow.size(); ++i) {
    if (leftRow[i].is_null() || rightRow[i].is_null()) {
      continue;
    }
    const DataType leftType = leftRow[i].get_type();
    const DataType rightType = rightRow[i].get_type();
    if (leftType != rightType) {
      errorOut = makeTypeError(i, leftType, rightType);
      return false;
    }
  }
  return true;
}

QueryResult buildSuccessResult(const QueryResult &left,
                               std::vector<std::vector<Value>> rows) {
  QueryResult result = QueryResult::success_result("OK");
  result.column_names = left.column_names;
  result.rows = std::move(rows);
  result.affected_rows = static_cast<int>(result.rows.size());
  return result;
}

QueryResult executeUnionAll(const QueryResult &left, const QueryResult &right) {
  std::vector<std::vector<Value>> rows = left.rows;
  rows.insert(rows.end(), right.rows.begin(), right.rows.end());
  return buildSuccessResult(left, std::move(rows));
}

QueryResult executeUnionDistinct(const QueryResult &left,
                                 const QueryResult &right) {
  std::unordered_set<RowKey> seen;
  std::vector<std::vector<Value>> rows;
  auto appendUnique = [&](const std::vector<std::vector<Value>> &source) {
    for (const std::vector<Value> &row : source) {
      RowKey key(row);
      if (seen.insert(key).second) {
        rows.push_back(row);
      }
    }
  };
  appendUnique(left.rows);
  appendUnique(right.rows);
  return buildSuccessResult(left, std::move(rows));
}

QueryResult executeIntersect(const QueryResult &left,
                             const QueryResult &right) {
  std::unordered_set<RowKey> rightKeys;
  for (const std::vector<Value> &row : right.rows) {
    rightKeys.emplace(row);
  }
  std::unordered_set<RowKey> emitted;
  std::vector<std::vector<Value>> rows;
  for (const std::vector<Value> &row : left.rows) {
    RowKey key(row);
    if (rightKeys.find(key) == rightKeys.end()) {
      continue;
    }
    if (!emitted.insert(key).second) {
      continue;
    }
    rows.push_back(row);
  }
  return buildSuccessResult(left, std::move(rows));
}

QueryResult executeExcept(const QueryResult &left, const QueryResult &right) {
  std::unordered_set<RowKey> rightKeys;
  for (const std::vector<Value> &row : right.rows) {
    rightKeys.emplace(row);
  }
  std::unordered_set<RowKey> emitted;
  std::vector<std::vector<Value>> rows;
  for (const std::vector<Value> &row : left.rows) {
    RowKey key(row);
    if (rightKeys.find(key) != rightKeys.end()) {
      continue;
    }
    if (!emitted.insert(key).second) {
      continue;
    }
    rows.push_back(row);
  }
  return buildSuccessResult(left, std::move(rows));
}

}  // namespace

QueryResult SetOperationOperator::execute(const QueryResult &left,
                                          const QueryResult &right,
                                          SetOperationKind kind,
                                          bool isAll) const {
  if (!left.success) {
    return left;
  }
  if (!right.success) {
    return right;
  }
  if (left.column_names.size() != right.column_names.size()) {
    return makeArityError(left.column_names.size(), right.column_names.size());
  }
  QueryResult typeError;
  if (!validateColumnTypes(left, right, typeError)) {
    return typeError;
  }
  switch (kind) {
    case SetOperationKind::Union:
      if (isAll) {
        return executeUnionAll(left, right);
      }
      return executeUnionDistinct(left, right);
    case SetOperationKind::Intersect:
      return executeIntersect(left, right);
    case SetOperationKind::Except:
      return executeExcept(left, right);
  }
  return QueryResult::error_result("Unknown set operation");
}

}  // namespace db
