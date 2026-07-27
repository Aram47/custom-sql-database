#pragma once

#include <list>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

#include "parser/parser.h"

namespace db {

/** Thread-safe LRU cache of parsed SQL statements. */
class PlanCache {
 public:
  explicit PlanCache(size_t capacity = 128);

  std::optional<ParsedStatement> get(const std::string &sql);
  void put(const std::string &sql, ParsedStatement stmt);
  void clear();

 private:
  size_t capacity_;
  std::mutex mutex_;
  std::list<std::string> lru_order_;
  std::unordered_map<std::string,
                     std::pair<ParsedStatement, std::list<std::string>::iterator>>
      entries_;
};

}  // namespace db
