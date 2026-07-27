#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

#include "core/index_key.h"
#include "types/value.h"

namespace db {

/**
 * In-memory B-tree index mapping IndexKey to row indices.
 * Supports equality, prefix, and closed/open range lookups.
 */
class BTreeIndex {
 public:
  static constexpr size_t kMinDegree = 16;

  BTreeIndex() = default;
  void clear();
  void insert(const IndexKey &key, size_t row_index);
  void insert(const Value &key, size_t row_index);
  void remove(const IndexKey &key, size_t row_index);
  void remove(const Value &key, size_t row_index);
  std::vector<size_t> find_equal(const IndexKey &key) const;
  std::vector<size_t> find_equal(const Value &key) const;
  /** Rows whose key starts with prefix (leftmost components). */
  std::vector<size_t> find_prefix(const IndexKey &prefix) const;
  std::vector<size_t> find_range(const std::optional<IndexKey> &lower,
                                 bool lower_inclusive,
                                 const std::optional<IndexKey> &upper,
                                 bool upper_inclusive) const;
  std::vector<size_t> find_range(const std::optional<Value> &lower,
                                 bool lower_inclusive,
                                 const std::optional<Value> &upper,
                                 bool upper_inclusive) const;
  bool empty() const;

 private:
  struct Node {
    bool is_leaf{true};
    std::vector<IndexKey> keys;
    std::vector<std::vector<size_t>> row_lists;
    std::vector<std::unique_ptr<Node>> children;
  };

  std::unique_ptr<Node> root_;

  Node *ensure_root();
  void split_child(Node *parent, size_t child_index);
  void insert_non_full(Node *node, const IndexKey &key, size_t row_index);
  bool remove_from_node(Node *node, const IndexKey &key, size_t row_index);
  void collect_equal(const Node *node, const IndexKey &key,
                     std::vector<size_t> &out) const;
  void collect_prefix(const Node *node, const IndexKey &prefix,
                      std::vector<size_t> &out) const;
  void collect_range(const Node *node, const std::optional<IndexKey> &lower,
                     bool lower_inclusive, const std::optional<IndexKey> &upper,
                     bool upper_inclusive, std::vector<size_t> &out) const;
  static bool is_less(const IndexKey &a, const IndexKey &b);
  static bool is_equal(const IndexKey &a, const IndexKey &b);
  static bool passes_lower(const IndexKey &key,
                           const std::optional<IndexKey> &lower,
                           bool lower_inclusive);
  static bool passes_upper(const IndexKey &key,
                           const std::optional<IndexKey> &upper,
                           bool upper_inclusive);
};

}  // namespace db
