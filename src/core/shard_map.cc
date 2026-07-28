#include "core/shard_map.h"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace db {
namespace {

std::string trim(const std::string &text) {
  size_t begin = 0;
  while (begin < text.size() &&
         std::isspace(static_cast<unsigned char>(text[begin]))) {
    ++begin;
  }
  size_t end = text.size();
  while (end > begin &&
         std::isspace(static_cast<unsigned char>(text[end - 1]))) {
    --end;
  }
  return text.substr(begin, end - begin);
}

bool parseInt(const std::string &text, int &outValue) {
  if (text.empty()) {
    return false;
  }
  char *end = nullptr;
  const long parsed = std::strtol(text.c_str(), &end, 10);
  if (end == text.c_str() || *end != '\0') {
    return false;
  }
  outValue = static_cast<int>(parsed);
  return true;
}

bool parsePort(const std::string &text, uint16_t &outPort) {
  int value = 0;
  if (!parseInt(text, value) || value <= 0 || value > 65535) {
    return false;
  }
  outPort = static_cast<uint16_t>(value);
  return true;
}

}  // namespace

bool ShardMap::validate(const ShardMap &map, std::string *error) {
  if (map.workers_.empty()) {
    if (error) {
      *error = "shard map has no workers";
    }
    return false;
  }
  if (map.placements_.empty()) {
    if (error) {
      *error = "shard map has no placements";
    }
    return false;
  }
  for (const auto &[child, shardId] : map.placements_) {
    if (map.workers_.find(shardId) == map.workers_.end()) {
      if (error) {
        *error = "placement for " + child + " references unknown shard " +
                 std::to_string(shardId);
      }
      return false;
    }
  }
  return true;
}

std::optional<ShardMap> ShardMap::build(
    std::vector<ShardEndpoint> workers, std::map<std::string, int> placements,
    std::string *error) {
  ShardMap map;
  for (ShardEndpoint &endpoint : workers) {
    if (map.workers_.count(endpoint.shardId) > 0) {
      if (error) {
        *error = "duplicate shard id " + std::to_string(endpoint.shardId);
      }
      return std::nullopt;
    }
    if (endpoint.host.empty() || endpoint.port == 0) {
      if (error) {
        *error = "invalid worker endpoint";
      }
      return std::nullopt;
    }
    map.workers_[endpoint.shardId] = std::move(endpoint);
  }
  map.placements_ = std::move(placements);
  if (!validate(map, error)) {
    return std::nullopt;
  }
  return map;
}

std::optional<ShardMap> ShardMap::loadFromFile(const std::string &path,
                                               std::string *error) {
  std::ifstream input(path);
  if (!input) {
    if (error) {
      *error = "cannot open shard map: " + path;
    }
    return std::nullopt;
  }
  ShardMap map;
  std::string line;
  size_t lineNumber = 0;
  while (std::getline(input, line)) {
    ++lineNumber;
    const std::string trimmed = trim(line);
    if (trimmed.empty() || trimmed[0] == '#') {
      continue;
    }
    std::istringstream stream(trimmed);
    std::vector<std::string> fields;
    std::string field;
    while (stream >> field) {
      fields.push_back(field);
    }
    if (fields.size() == 3) {
      int shardId = 0;
      uint16_t port = 0;
      if (!parseInt(fields[0], shardId) || !parsePort(fields[2], port)) {
        if (error) {
          *error = "invalid worker line " + std::to_string(lineNumber);
        }
        return std::nullopt;
      }
      if (map.workers_.count(shardId) > 0) {
        if (error) {
          *error = "duplicate shard id " + std::to_string(shardId);
        }
        return std::nullopt;
      }
      ShardEndpoint endpoint;
      endpoint.shardId = shardId;
      endpoint.host = fields[1];
      endpoint.port = port;
      map.workers_[shardId] = std::move(endpoint);
      continue;
    }
    if (fields.size() == 2) {
      int shardId = 0;
      if (!parseInt(fields[1], shardId)) {
        if (error) {
          *error = "invalid placement line " + std::to_string(lineNumber);
        }
        return std::nullopt;
      }
      if (map.placements_.count(fields[0]) > 0) {
        if (error) {
          *error = "duplicate placement for " + fields[0];
        }
        return std::nullopt;
      }
      map.placements_[fields[0]] = shardId;
      continue;
    }
    if (error) {
      *error = "invalid shard map line " + std::to_string(lineNumber);
    }
    return std::nullopt;
  }
  if (!validate(map, error)) {
    return std::nullopt;
  }
  return map;
}

std::vector<ShardEndpoint> ShardMap::listWorkers() const {
  std::vector<ShardEndpoint> workers;
  workers.reserve(workers_.size());
  for (const auto &[shardId, endpoint] : workers_) {
    (void)shardId;
    workers.push_back(endpoint);
  }
  return workers;
}

std::optional<ShardEndpoint> ShardMap::findEndpoint(int shardId) const {
  const auto it = workers_.find(shardId);
  if (it == workers_.end()) {
    return std::nullopt;
  }
  return it->second;
}

std::optional<int> ShardMap::findShardId(const std::string &childTableName) const {
  const auto it = placements_.find(childTableName);
  if (it == placements_.end()) {
    return std::nullopt;
  }
  return it->second;
}

bool ShardMap::hasPlacement(const std::string &childTableName) const {
  return placements_.count(childTableName) > 0;
}

std::optional<std::vector<ShardEndpoint>> ShardMap::resolveChildren(
    const std::vector<std::string> &childNames, std::string *error) const {
  std::map<int, ShardEndpoint> unique;
  for (const std::string &child : childNames) {
    const std::optional<int> shardId = findShardId(child);
    if (!shardId) {
      if (error) {
        *error = "no shard placement for partition " + child;
      }
      return std::nullopt;
    }
    const std::optional<ShardEndpoint> endpoint = findEndpoint(*shardId);
    if (!endpoint) {
      if (error) {
        *error = "unknown shard id " + std::to_string(*shardId);
      }
      return std::nullopt;
    }
    unique[*shardId] = *endpoint;
  }
  std::vector<ShardEndpoint> result;
  result.reserve(unique.size());
  for (const auto &[shardId, endpoint] : unique) {
    (void)shardId;
    result.push_back(endpoint);
  }
  return result;
}

std::optional<ShardEndpoint> ShardMap::firstWorker() const {
  if (workers_.empty()) {
    return std::nullopt;
  }
  return workers_.begin()->second;
}

}  // namespace db
