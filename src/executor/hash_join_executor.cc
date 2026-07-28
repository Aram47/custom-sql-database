#include "executor/hash_join_executor.h"

#include <unordered_map>

#include "executor/row_key.h"

namespace std {

template <>
struct hash<db::Value> {
  std::size_t operator()(const db::Value &value) const {
    return db::hashValue(value);
  }
};

}  // namespace std

namespace db {
namespace {

std::vector<Value> concatVectors(const std::vector<Value> &left,
                                 const std::vector<Value> &right) {
  std::vector<Value> out;
  out.reserve(left.size() + right.size());
  out.insert(out.end(), left.begin(), left.end());
  out.insert(out.end(), right.begin(), right.end());
  return out;
}

using HashTable = std::unordered_multimap<Value, const std::vector<Value> *>;

void buildHashTable(HashTable &table,
                    const std::vector<std::vector<Value>> &rows,
                    size_t key_index) {
  for (const std::vector<Value> &row : rows) {
    if (key_index >= row.size()) {
      continue;
    }
    const Value &key = row[key_index];
    if (key.is_null()) {
      continue;
    }
    table.emplace(key, &row);
  }
}

void probeAndEmit(const HashTable &table,
                  const std::vector<std::vector<Value>> &probe_rows,
                  size_t probe_key_index, bool probe_is_left,
                  std::vector<std::vector<Value>> &output) {
  for (const std::vector<Value> &probe_row : probe_rows) {
    if (probe_key_index >= probe_row.size()) {
      continue;
    }
    const Value &key = probe_row[probe_key_index];
    if (key.is_null()) {
      continue;
    }
    const auto range = table.equal_range(key);
    for (auto it = range.first; it != range.second; ++it) {
      const std::vector<Value> &build_row = *it->second;
      if (probe_is_left) {
        output.push_back(concatVectors(probe_row, build_row));
      } else {
        output.push_back(concatVectors(build_row, probe_row));
      }
    }
  }
}

}  // namespace

std::vector<std::vector<Value>> HashJoinExecutor::executeInnerEqui(
    const std::vector<std::vector<Value>> &left_rows,
    const std::vector<std::vector<Value>> &right_rows, size_t left_key_index,
    size_t right_key_index, bool build_on_left) {
  std::vector<std::vector<Value>> output;
  HashTable table;
  if (build_on_left) {
    buildHashTable(table, left_rows, left_key_index);
    probeAndEmit(table, right_rows, right_key_index, false, output);
  } else {
    buildHashTable(table, right_rows, right_key_index);
    probeAndEmit(table, left_rows, left_key_index, true, output);
  }
  return output;
}

}  // namespace db
