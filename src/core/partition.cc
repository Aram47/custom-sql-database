#include "core/partition.h"

#include <algorithm>

namespace db {
namespace {

constexpr uint64_t kFnvOffset = 14695981039346656037ULL;
constexpr uint64_t kFnvPrime = 1099511628211ULL;

uint64_t mixBytes(uint64_t hash, const uint8_t *data, size_t length) {
  for (size_t i = 0; i < length; ++i) {
    hash ^= static_cast<uint64_t>(data[i]);
    hash *= kFnvPrime;
  }
  return hash;
}

bool rangesOverlap(const RangePartitionBound &a, const RangePartitionBound &b) {
  return a.minInclusive < b.maxExclusive && b.minInclusive < a.maxExclusive;
}

bool rangeContains(const RangePartitionBound &bound, const Value &key) {
  return bound.minInclusive <= key && key < bound.maxExclusive;
}

bool rangeIntersectsConstraint(const RangePartitionBound &bound,
                               const PartitionPruneConstraint &constraint) {
  switch (constraint.op) {
    case PartitionPruneConstraint::Op::Equal:
      return rangeContains(bound, constraint.lower);
    case PartitionPruneConstraint::Op::Less: {
      if (!constraint.upperInclusive) {
        return bound.minInclusive < constraint.upper;
      }
      return bound.minInclusive <= constraint.upper;
    }
    case PartitionPruneConstraint::Op::LessEqual:
      return bound.minInclusive <= constraint.upper;
    case PartitionPruneConstraint::Op::Greater: {
      const Value &lower = constraint.lower;
      if (constraint.lowerInclusive) {
        return bound.maxExclusive > lower;
      }
      return bound.maxExclusive > lower && !(bound.maxExclusive <= lower);
    }
    case PartitionPruneConstraint::Op::GreaterEqual:
      return bound.maxExclusive > constraint.lower;
    case PartitionPruneConstraint::Op::Between: {
      RangePartitionBound query;
      query.minInclusive = constraint.lower;
      query.maxExclusive = constraint.upper;
      if (!constraint.lowerInclusive) {
        // Approximate: require key > lower by shrinking inclusive min is hard
        // without successor; treat as [lower, upper) intersection test.
      }
      if (!constraint.upperInclusive) {
        // upper already exclusive-style for < comparisons; Between uses
        // inclusive upper when upperInclusive is true.
      }
      if (constraint.lowerInclusive && constraint.upperInclusive) {
        return bound.minInclusive <= constraint.upper &&
               constraint.lower < bound.maxExclusive;
      }
      if (constraint.lowerInclusive && !constraint.upperInclusive) {
        return bound.minInclusive < constraint.upper &&
               constraint.lower < bound.maxExclusive;
      }
      if (!constraint.lowerInclusive && constraint.upperInclusive) {
        return bound.minInclusive <= constraint.upper &&
               constraint.lower < bound.maxExclusive &&
               !(bound.maxExclusive <= constraint.lower);
      }
      return bound.minInclusive < constraint.upper &&
             constraint.lower < bound.maxExclusive;
    }
  }
  return true;
}

class RangePartitionRouter : public IPartitionRouter {
 public:
  explicit RangePartitionRouter(std::vector<PartitionDescriptor> partitions)
      : partitions_(std::move(partitions)) {}

  std::optional<std::string> resolveChild(const Value &key) const override {
    if (key.is_null()) {
      return std::nullopt;
    }
    for (const PartitionDescriptor &part : partitions_) {
      if (!part.bound.range) {
        continue;
      }
      if (rangeContains(*part.bound.range, key)) {
        return part.childTableName;
      }
    }
    return std::nullopt;
  }

  std::vector<std::string> prune(
      const PartitionPruneRequest &request) const override {
    std::vector<std::string> result;
    result.reserve(partitions_.size());
    for (const PartitionDescriptor &part : partitions_) {
      if (!part.bound.range) {
        continue;
      }
      bool include = true;
      for (const PartitionPruneConstraint &constraint : request.constraints) {
        if (!rangeIntersectsConstraint(*part.bound.range, constraint)) {
          include = false;
          break;
        }
      }
      if (include) {
        result.push_back(part.childTableName);
      }
    }
    return result;
  }

 private:
  std::vector<PartitionDescriptor> partitions_;
};

class HashPartitionRouter : public IPartitionRouter {
 public:
  explicit HashPartitionRouter(std::vector<PartitionDescriptor> partitions)
      : partitions_(std::move(partitions)) {}

  std::optional<std::string> resolveChild(const Value &key) const override {
    if (key.is_null() || partitions_.empty()) {
      return std::nullopt;
    }
    const uint64_t hashed = computePartitionHash(key);
    for (const PartitionDescriptor &part : partitions_) {
      if (!part.bound.hash) {
        continue;
      }
      const HashPartitionBound &bound = *part.bound.hash;
      if (bound.modulus <= 0) {
        continue;
      }
      if (static_cast<int64_t>(hashed % static_cast<uint64_t>(bound.modulus)) ==
          bound.remainder) {
        return part.childTableName;
      }
    }
    return std::nullopt;
  }

  std::vector<std::string> prune(
      const PartitionPruneRequest &request) const override {
    std::optional<Value> equalKey;
    for (const PartitionPruneConstraint &constraint : request.constraints) {
      if (constraint.op == PartitionPruneConstraint::Op::Equal) {
        equalKey = constraint.lower;
        break;
      }
    }
    if (!equalKey) {
      std::vector<std::string> all;
      all.reserve(partitions_.size());
      for (const PartitionDescriptor &part : partitions_) {
        all.push_back(part.childTableName);
      }
      return all;
    }
    std::optional<std::string> child = resolveChild(*equalKey);
    if (!child) {
      return {};
    }
    return {*child};
  }

 private:
  std::vector<PartitionDescriptor> partitions_;
};

}  // namespace

uint64_t computePartitionHash(const Value &value) {
  uint64_t hash = kFnvOffset;
  const uint8_t typeTag = static_cast<uint8_t>(value.get_type());
  hash = mixBytes(hash, &typeTag, 1);
  if (value.is_null()) {
    return hash;
  }
  if (value.is_int()) {
    const int64_t v = value.as_int();
    hash = mixBytes(hash, reinterpret_cast<const uint8_t *>(&v), sizeof(v));
    return hash;
  }
  if (value.is_float()) {
    const double v = value.as_float();
    hash = mixBytes(hash, reinterpret_cast<const uint8_t *>(&v), sizeof(v));
    return hash;
  }
  if (value.is_bool()) {
    const uint8_t v = value.as_bool() ? 1 : 0;
    hash = mixBytes(hash, &v, 1);
    return hash;
  }
  const std::string s = value.as_string();
  hash = mixBytes(hash, reinterpret_cast<const uint8_t *>(s.data()), s.size());
  return hash;
}

uint64_t PartitionedTableMetadata::hashValue(const Value &value) {
  return computePartitionHash(value);
}

PartitionedTableMetadata::PartitionedTableMetadata(PartitionKind kind,
                                                   std::string keyColumn)
    : kind_(kind), key_column_(std::move(keyColumn)) {}

PartitionKind PartitionedTableMetadata::getKind() const { return kind_; }

const std::string &PartitionedTableMetadata::getKeyColumn() const {
  return key_column_;
}

const std::vector<PartitionDescriptor> &
PartitionedTableMetadata::getPartitions() const {
  return partitions_;
}

bool PartitionedTableMetadata::validateNewBound(const PartitionBound &bound,
                                                 std::string *error) const {
  if (kind_ == PartitionKind::Range) {
    if (!bound.range || bound.hash) {
      if (error) {
        *error = "RANGE partition requires FROM/TO bounds";
      }
      return false;
    }
    if (!(bound.range->minInclusive < bound.range->maxExclusive)) {
      if (error) {
        *error = "RANGE bound requires min < max";
      }
      return false;
    }
    for (const PartitionDescriptor &existing : partitions_) {
      if (existing.bound.range &&
          rangesOverlap(*bound.range, *existing.bound.range)) {
        if (error) {
          *error = "RANGE partition overlaps existing partition '" +
                   existing.childTableName + "'";
        }
        return false;
      }
    }
    return true;
  }
  if (!bound.hash || bound.range) {
    if (error) {
      *error = "HASH partition requires MODULUS/REMAINDER";
    }
    return false;
  }
  if (bound.hash->modulus <= 0) {
    if (error) {
      *error = "HASH MODULUS must be positive";
    }
    return false;
  }
  if (bound.hash->remainder < 0 ||
      bound.hash->remainder >= bound.hash->modulus) {
    if (error) {
      *error = "HASH REMAINDER must be in [0, MODULUS)";
    }
    return false;
  }
  for (const PartitionDescriptor &existing : partitions_) {
    if (!existing.bound.hash) {
      continue;
    }
    if (existing.bound.hash->modulus != bound.hash->modulus) {
      if (error) {
        *error = "HASH MODULUS must match existing partitions";
      }
      return false;
    }
    if (existing.bound.hash->remainder == bound.hash->remainder) {
      if (error) {
        *error = "HASH REMAINDER already used by '" + existing.childTableName +
                 "'";
      }
      return false;
    }
  }
  return true;
}

bool PartitionedTableMetadata::addPartition(PartitionDescriptor descriptor,
                                            std::string *error) {
  if (descriptor.childTableName.empty()) {
    if (error) {
      *error = "Partition child name is empty";
    }
    return false;
  }
  if (hasChild(descriptor.childTableName)) {
    if (error) {
      *error = "Partition '" + descriptor.childTableName + "' already exists";
    }
    return false;
  }
  if (!validateNewBound(descriptor.bound, error)) {
    return false;
  }
  partitions_.push_back(std::move(descriptor));
  return true;
}

bool PartitionedTableMetadata::removePartition(
    const std::string &childTableName) {
  auto it = std::find_if(
      partitions_.begin(), partitions_.end(),
      [&](const PartitionDescriptor &part) {
        return part.childTableName == childTableName;
      });
  if (it == partitions_.end()) {
    return false;
  }
  partitions_.erase(it);
  return true;
}

bool PartitionedTableMetadata::hasChild(
    const std::string &childTableName) const {
  return std::any_of(
      partitions_.begin(), partitions_.end(),
      [&](const PartitionDescriptor &part) {
        return part.childTableName == childTableName;
      });
}

std::unique_ptr<IPartitionRouter> PartitionedTableMetadata::createRouter()
    const {
  if (kind_ == PartitionKind::Range) {
    return std::make_unique<RangePartitionRouter>(partitions_);
  }
  return std::make_unique<HashPartitionRouter>(partitions_);
}

}  // namespace db
