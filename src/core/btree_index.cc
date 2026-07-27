#include "core/btree_index.h"

#include <algorithm>

namespace db {

bool BTreeIndex::is_less(const IndexKey &a, const IndexKey &b) { return a < b; }

bool BTreeIndex::is_equal(const IndexKey &a, const IndexKey &b) {
  return a == b;
}

bool BTreeIndex::passes_lower(const IndexKey &key,
                              const std::optional<IndexKey> &lower,
                              bool lower_inclusive) {
  if (!lower.has_value()) {
    return true;
  }
  if (lower_inclusive) {
    return !is_less(key, *lower);
  }
  return is_less(*lower, key);
}

bool BTreeIndex::passes_upper(const IndexKey &key,
                              const std::optional<IndexKey> &upper,
                              bool upper_inclusive) {
  if (!upper.has_value()) {
    return true;
  }
  if (upper_inclusive) {
    return !is_less(*upper, key);
  }
  return is_less(key, *upper);
}

void BTreeIndex::clear() { root_.reset(); }

bool BTreeIndex::empty() const { return root_ == nullptr; }

BTreeIndex::Node *BTreeIndex::ensure_root() {
  if (!root_) {
    root_ = std::make_unique<Node>();
  }
  return root_.get();
}

void BTreeIndex::split_child(Node *parent, size_t child_index) {
  Node *full_child = parent->children[child_index].get();
  auto new_child = std::make_unique<Node>();
  new_child->is_leaf = full_child->is_leaf;
  const size_t mid = kMinDegree - 1;
  IndexKey mid_key = full_child->keys[mid];
  std::vector<size_t> mid_rows = full_child->row_lists[mid];
  new_child->keys.assign(full_child->keys.begin() + static_cast<long>(mid) + 1,
                         full_child->keys.end());
  new_child->row_lists.assign(
      full_child->row_lists.begin() + static_cast<long>(mid) + 1,
      full_child->row_lists.end());
  full_child->keys.resize(mid);
  full_child->row_lists.resize(mid);
  if (!full_child->is_leaf) {
    new_child->children.assign(
        std::make_move_iterator(full_child->children.begin() +
                                static_cast<long>(mid) + 1),
        std::make_move_iterator(full_child->children.end()));
    full_child->children.resize(mid + 1);
  }
  parent->keys.insert(parent->keys.begin() + static_cast<long>(child_index),
                      mid_key);
  parent->row_lists.insert(
      parent->row_lists.begin() + static_cast<long>(child_index),
      std::move(mid_rows));
  parent->children.insert(
      parent->children.begin() + static_cast<long>(child_index) + 1,
      std::move(new_child));
}

void BTreeIndex::insert_non_full(Node *node, const IndexKey &key,
                                 size_t row_index) {
  size_t i = 0;
  while (i < node->keys.size() && is_less(node->keys[i], key)) {
    ++i;
  }
  if (i < node->keys.size() && is_equal(node->keys[i], key)) {
    node->row_lists[i].push_back(row_index);
    return;
  }
  if (node->is_leaf) {
    node->keys.insert(node->keys.begin() + static_cast<long>(i), key);
    node->row_lists.insert(node->row_lists.begin() + static_cast<long>(i),
                           std::vector<size_t>{row_index});
    return;
  }
  if (node->children[i]->keys.size() == 2 * kMinDegree - 1) {
    split_child(node, i);
    if (is_less(node->keys[i], key)) {
      ++i;
    } else if (is_equal(node->keys[i], key)) {
      node->row_lists[i].push_back(row_index);
      return;
    }
  }
  insert_non_full(node->children[i].get(), key, row_index);
}

void BTreeIndex::insert(const IndexKey &key, size_t row_index) {
  if (key.has_null() || key.empty()) {
    return;
  }
  Node *root = ensure_root();
  if (root->keys.size() == 2 * kMinDegree - 1) {
    auto new_root = std::make_unique<Node>();
    new_root->is_leaf = false;
    new_root->children.push_back(std::move(root_));
    root_ = std::move(new_root);
    split_child(root_.get(), 0);
    insert_non_full(root_.get(), key, row_index);
    return;
  }
  insert_non_full(root, key, row_index);
}

void BTreeIndex::insert(const Value &key, size_t row_index) {
  insert(IndexKey(key), row_index);
}

bool BTreeIndex::remove_from_node(Node *node, const IndexKey &key,
                                  size_t row_index) {
  size_t i = 0;
  while (i < node->keys.size() && is_less(node->keys[i], key)) {
    ++i;
  }
  if (i < node->keys.size() && is_equal(node->keys[i], key)) {
    auto &rows = node->row_lists[i];
    rows.erase(std::remove(rows.begin(), rows.end(), row_index), rows.end());
    if (!rows.empty()) {
      return true;
    }
    node->keys.erase(node->keys.begin() + static_cast<long>(i));
    node->row_lists.erase(node->row_lists.begin() + static_cast<long>(i));
    return true;
  }
  if (node->is_leaf) {
    return false;
  }
  return remove_from_node(node->children[i].get(), key, row_index);
}

void BTreeIndex::remove(const IndexKey &key, size_t row_index) {
  if (!root_ || key.has_null() || key.empty()) {
    return;
  }
  remove_from_node(root_.get(), key, row_index);
  if (root_ && root_->keys.empty() && !root_->is_leaf &&
      root_->children.size() == 1) {
    root_ = std::move(root_->children[0]);
  }
  if (root_ && root_->keys.empty() && root_->is_leaf) {
    root_.reset();
  }
}

void BTreeIndex::remove(const Value &key, size_t row_index) {
  remove(IndexKey(key), row_index);
}

void BTreeIndex::collect_equal(const Node *node, const IndexKey &key,
                               std::vector<size_t> &out) const {
  if (node == nullptr) {
    return;
  }
  size_t i = 0;
  while (i < node->keys.size() && is_less(node->keys[i], key)) {
    ++i;
  }
  if (i < node->keys.size() && is_equal(node->keys[i], key)) {
    out.insert(out.end(), node->row_lists[i].begin(), node->row_lists[i].end());
    return;
  }
  if (!node->is_leaf) {
    collect_equal(node->children[i].get(), key, out);
  }
}

std::vector<size_t> BTreeIndex::find_equal(const IndexKey &key) const {
  std::vector<size_t> result;
  if (!root_ || key.has_null() || key.empty()) {
    return result;
  }
  collect_equal(root_.get(), key, result);
  return result;
}

std::vector<size_t> BTreeIndex::find_equal(const Value &key) const {
  return find_equal(IndexKey(key));
}

void BTreeIndex::collect_prefix(const Node *node, const IndexKey &prefix,
                                std::vector<size_t> &out) const {
  if (node == nullptr) {
    return;
  }
  for (size_t i = 0; i < node->keys.size(); ++i) {
    if (!node->is_leaf) {
      collect_prefix(node->children[i].get(), prefix, out);
    }
    if (node->keys[i].starts_with(prefix)) {
      out.insert(out.end(), node->row_lists[i].begin(),
                 node->row_lists[i].end());
    } else if (is_less(prefix, node->keys[i]) &&
               !node->keys[i].starts_with(prefix)) {
      const size_t limit = prefix.size() < node->keys[i].size()
                               ? prefix.size()
                               : node->keys[i].size();
      bool prefix_exceeded = false;
      for (size_t j = 0; j < limit; ++j) {
        if (prefix.get_components()[j] <
            node->keys[i].get_components()[j]) {
          prefix_exceeded = true;
          break;
        }
        if (node->keys[i].get_components()[j] <
            prefix.get_components()[j]) {
          break;
        }
      }
      if (prefix_exceeded && !node->keys[i].starts_with(prefix)) {
        return;
      }
    }
  }
  if (!node->is_leaf) {
    collect_prefix(node->children.back().get(), prefix, out);
  }
}

std::vector<size_t> BTreeIndex::find_prefix(const IndexKey &prefix) const {
  std::vector<size_t> result;
  if (!root_ || prefix.has_null() || prefix.empty()) {
    return result;
  }
  collect_prefix(root_.get(), prefix, result);
  return result;
}

void BTreeIndex::collect_range(const Node *node,
                               const std::optional<IndexKey> &lower,
                               bool lower_inclusive,
                               const std::optional<IndexKey> &upper,
                               bool upper_inclusive,
                               std::vector<size_t> &out) const {
  if (node == nullptr) {
    return;
  }
  for (size_t i = 0; i < node->keys.size(); ++i) {
    if (!node->is_leaf) {
      collect_range(node->children[i].get(), lower, lower_inclusive, upper,
                    upper_inclusive, out);
    }
    if (passes_lower(node->keys[i], lower, lower_inclusive) &&
        passes_upper(node->keys[i], upper, upper_inclusive)) {
      out.insert(out.end(), node->row_lists[i].begin(),
                 node->row_lists[i].end());
    } else if (upper.has_value() && is_less(*upper, node->keys[i])) {
      return;
    }
  }
  if (!node->is_leaf) {
    collect_range(node->children.back().get(), lower, lower_inclusive, upper,
                  upper_inclusive, out);
  }
}

std::vector<size_t> BTreeIndex::find_range(
    const std::optional<IndexKey> &lower, bool lower_inclusive,
    const std::optional<IndexKey> &upper, bool upper_inclusive) const {
  std::vector<size_t> result;
  if (!root_) {
    return result;
  }
  collect_range(root_.get(), lower, lower_inclusive, upper, upper_inclusive,
                result);
  return result;
}

std::vector<size_t> BTreeIndex::find_range(
    const std::optional<Value> &lower, bool lower_inclusive,
    const std::optional<Value> &upper, bool upper_inclusive) const {
  std::optional<IndexKey> lower_key;
  std::optional<IndexKey> upper_key;
  if (lower.has_value()) {
    lower_key = IndexKey(*lower);
  }
  if (upper.has_value()) {
    upper_key = IndexKey(*upper);
  }
  return find_range(lower_key, lower_inclusive, upper_key, upper_inclusive);
}

}  // namespace db
