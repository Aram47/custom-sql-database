#include "core/partition_catalog.h"

#include <filesystem>
#include <fstream>
#include <sstream>

#include "types/data_type.h"
#include "utils/exceptions.h"

namespace fs = std::filesystem;

namespace db {
namespace {

std::string dataTypeToTag(DataType type) {
  switch (type) {
    case DataType::INT:
      return "INT";
    case DataType::FLOAT:
      return "FLOAT";
    case DataType::STRING:
      return "STRING";
    case DataType::BOOLEAN:
      return "BOOLEAN";
    case DataType::DATE:
      return "DATE";
    case DataType::UUID:
      return "UUID";
  }
  return "STRING";
}

DataType tagToDataType(const std::string &tag) {
  if (tag == "INT") {
    return DataType::INT;
  }
  if (tag == "FLOAT") {
    return DataType::FLOAT;
  }
  if (tag == "STRING") {
    return DataType::STRING;
  }
  if (tag == "BOOLEAN") {
    return DataType::BOOLEAN;
  }
  if (tag == "DATE") {
    return DataType::DATE;
  }
  if (tag == "UUID") {
    return DataType::UUID;
  }
  throw StorageException("Unknown value type tag: " + tag);
}

}  // namespace

std::string PartitionCatalog::buildPath(const std::string &storageDirectory,
                                        const std::string &parentName) {
  return (fs::path(storageDirectory) / PARTITIONS_SUBDIR /
          (parentName + PARTITION_EXTENSION))
      .string();
}

std::string PartitionCatalog::serializeValue(const Value &value) {
  if (value.is_null()) {
    return "NULL:";
  }
  return dataTypeToTag(value.get_type()) + ":" + value.to_string();
}

Value PartitionCatalog::deserializeValue(const std::string &text) {
  const size_t colon = text.find(':');
  if (colon == std::string::npos) {
    throw StorageException("Invalid partition value encoding");
  }
  const std::string tag = text.substr(0, colon);
  const std::string payload = text.substr(colon + 1);
  if (tag == "NULL") {
    return Value();
  }
  return Value::from_string(payload, tagToDataType(tag));
}

void PartitionCatalog::saveParent(const std::string &storageDirectory,
                                  const std::string &parentName,
                                  const PartitionedTableMetadata &metadata) {
  const fs::path dir = fs::path(storageDirectory) / PARTITIONS_SUBDIR;
  fs::create_directories(dir);
  const std::string path = buildPath(storageDirectory, parentName);
  const std::string tmpPath = path + ".tmp";
  {
    std::ofstream out(tmpPath, std::ios::binary | std::ios::trunc);
    if (!out) {
      throw StorageException("Cannot write partition file: " + tmpPath);
    }
    out << "v1\n";
    out << (metadata.getKind() == PartitionKind::Range ? "RANGE" : "HASH")
        << "\n";
    out << metadata.getKeyColumn() << "\n";
    out << metadata.getPartitions().size() << "\n";
    for (const PartitionDescriptor &part : metadata.getPartitions()) {
      out << part.childTableName << "\n";
      if (metadata.getKind() == PartitionKind::Range) {
        out << serializeValue(part.bound.range->minInclusive) << "\n";
        out << serializeValue(part.bound.range->maxExclusive) << "\n";
      } else {
        out << part.bound.hash->modulus << "\n";
        out << part.bound.hash->remainder << "\n";
      }
    }
    out.flush();
    if (!out) {
      throw StorageException("Failed writing partition file: " + tmpPath);
    }
  }
  fs::rename(tmpPath, path);
}

void PartitionCatalog::removeParentFile(const std::string &storageDirectory,
                                        const std::string &parentName) {
  const std::string path = buildPath(storageDirectory, parentName);
  std::error_code ec;
  fs::remove(path, ec);
}

void PartitionCatalog::loadAll(
    const std::string &storageDirectory,
    std::map<std::string, std::unique_ptr<Table>> &tables) {
  const fs::path dir = fs::path(storageDirectory) / PARTITIONS_SUBDIR;
  if (!fs::exists(dir) || !fs::is_directory(dir)) {
    return;
  }
  for (const auto &entry : fs::directory_iterator(dir)) {
    if (!entry.is_regular_file()) {
      continue;
    }
    const std::string filename = entry.path().filename().string();
    const std::string ext = PARTITION_EXTENSION;
    if (filename.size() <= ext.size() ||
        filename.compare(filename.size() - ext.size(), ext.size(), ext) != 0) {
      continue;
    }
    const std::string parentName =
        filename.substr(0, filename.size() - ext.size());
    auto tableIt = tables.find(parentName);
    if (tableIt == tables.end()) {
      continue;
    }
    std::ifstream in(entry.path(), std::ios::binary);
    if (!in) {
      throw StorageException("Cannot read partition file: " +
                             entry.path().string());
    }
    std::string version;
    std::getline(in, version);
    if (version != "v1") {
      throw StorageException("Unsupported partition meta version: " + version);
    }
    std::string kindLine;
    std::getline(in, kindLine);
    PartitionKind kind = PartitionKind::Range;
    if (kindLine == "HASH") {
      kind = PartitionKind::Hash;
    } else if (kindLine != "RANGE") {
      throw StorageException("Invalid partition kind: " + kindLine);
    }
    std::string keyColumn;
    std::getline(in, keyColumn);
    std::string countLine;
    std::getline(in, countLine);
    const size_t count = static_cast<size_t>(std::stoul(countLine));
    auto meta = std::make_unique<PartitionedTableMetadata>(kind, keyColumn);
    for (size_t i = 0; i < count; ++i) {
      PartitionDescriptor descriptor;
      std::getline(in, descriptor.childTableName);
      if (kind == PartitionKind::Range) {
        std::string minLine;
        std::string maxLine;
        std::getline(in, minLine);
        std::getline(in, maxLine);
        RangePartitionBound range;
        range.minInclusive = deserializeValue(minLine);
        range.maxExclusive = deserializeValue(maxLine);
        descriptor.bound.range = range;
      } else {
        std::string modulusLine;
        std::string remainderLine;
        std::getline(in, modulusLine);
        std::getline(in, remainderLine);
        HashPartitionBound hash;
        hash.modulus = std::stoll(modulusLine);
        hash.remainder = std::stoll(remainderLine);
        descriptor.bound.hash = hash;
      }
      std::string error;
      if (!meta->addPartition(std::move(descriptor), &error)) {
        throw StorageException("Failed loading partition for '" + parentName +
                               "': " + error);
      }
    }
    tableIt->second->setPartitionMetadata(std::move(meta));
  }
}

}  // namespace db
