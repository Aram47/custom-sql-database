#include "storage/backup_service.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>

#include "core/database.h"
#include "utils/engine_version.h"
#include "utils/exceptions.h"
#include "utils/sha256.h"

namespace fs = std::filesystem;

namespace db {
namespace {

constexpr const char *kWalStagePrefix = ".wal_stage_";

bool startsWith(const std::string &value, const std::string &prefix) {
  return value.size() >= prefix.size() &&
         value.compare(0, prefix.size(), prefix) == 0;
}

bool isRelativePathSafe(const std::string &relativePath) {
  if (relativePath.empty() || relativePath[0] == '/' ||
      relativePath.find("..") != std::string::npos) {
    return false;
  }
  return true;
}

}  // namespace

void BackupService::executeBackup(const BackupRequest &request) {
  if (request.dataDirectory.empty() || request.outputDirectory.empty()) {
    throw StorageException("backup requires --data-dir and --output-dir");
  }
  refuseIfStagingPresent(request.dataDirectory);
  refuseIfOutputInsideData(request.dataDirectory, request.outputDirectory);
  runCheckpoint(request.dataDirectory);
  copyDataDirectory(request.dataDirectory, request.outputDirectory);
  BackupManifest manifest = buildManifest(request.outputDirectory);
  manifest.engineVersion = kEngineVersion;
  manifest.createdAt = formatTimestampUtc();
  writeManifest(request.outputDirectory, manifest);
}

void BackupService::executeRestore(const RestoreRequest &request) {
  if (request.backupDirectory.empty() || request.dataDirectory.empty()) {
    throw StorageException("restore requires --backup-dir and --data-dir");
  }
  const BackupManifest manifest = readManifest(request.backupDirectory);
  verifyChecksums(request.backupDirectory, manifest);
  if (isDirectoryNonEmpty(request.dataDirectory) && !request.force) {
    throw StorageException(
        "data directory is not empty; pass --force to replace");
  }
  replaceDataDirectory(request.backupDirectory, request.dataDirectory,
                       manifest);
}

void BackupService::runCheckpoint(const std::string &dataDirectory) {
  Database database(dataDirectory, 0);
  database.load_from_disk();
  database.checkpoint();
}

void BackupService::refuseIfStagingPresent(
    const std::string &dataDirectory) const {
  if (hasWalStagingFiles(dataDirectory)) {
    throw StorageException(
        "WAL staging files present; stop the server and retry backup");
  }
}

void BackupService::refuseIfOutputInsideData(
    const std::string &dataDirectory,
    const std::string &outputDirectory) const {
  std::error_code errorCode;
  const fs::path dataPath = fs::weakly_canonical(dataDirectory, errorCode);
  if (errorCode) {
    return;
  }
  const fs::path outputPath = fs::weakly_canonical(outputDirectory, errorCode);
  if (errorCode) {
    return;
  }
  const auto mismatch =
      std::mismatch(dataPath.begin(), dataPath.end(), outputPath.begin());
  if (mismatch.first == dataPath.end()) {
    throw StorageException("output directory must not be inside data directory");
  }
}

void BackupService::copyDataDirectory(const std::string &sourceDirectory,
                                      const std::string &destDirectory) const {
  const fs::path source(sourceDirectory);
  const fs::path dest(destDirectory);
  if (!fs::exists(source) || !fs::is_directory(source)) {
    throw StorageException("data directory does not exist: " + sourceDirectory);
  }
  fs::create_directories(dest);
  for (const fs::directory_entry &entry :
       fs::recursive_directory_iterator(source)) {
    const fs::path relative = fs::relative(entry.path(), source);
    const fs::path target = dest / relative;
    if (entry.is_directory()) {
      fs::create_directories(target);
      continue;
    }
    fs::create_directories(target.parent_path());
    fs::copy_file(entry.path(), target, fs::copy_options::overwrite_existing);
  }
}

BackupManifest BackupService::buildManifest(
    const std::string &directoryPath) const {
  BackupManifest manifest;
  const fs::path root(directoryPath);
  for (const fs::directory_entry &entry :
       fs::recursive_directory_iterator(root)) {
    if (!entry.is_regular_file()) {
      continue;
    }
    const std::string relative = fs::relative(entry.path(), root).generic_string();
    if (relative == kManifestFileName) {
      continue;
    }
    ManifestEntry fileEntry;
    fileEntry.relativePath = relative;
    fileEntry.size = entry.file_size();
    fileEntry.sha256Hex = computeFileSha256(entry.path().string());
    manifest.files.push_back(std::move(fileEntry));
  }
  return manifest;
}

void BackupService::writeManifest(const std::string &backupDirectory,
                                  const BackupManifest &manifest) const {
  const fs::path path = fs::path(backupDirectory) / kManifestFileName;
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) {
    throw StorageException("cannot write manifest: " + path.string());
  }
  output << kManifestHeader << '\n';
  output << "engine_version=" << manifest.engineVersion << '\n';
  output << "created_at=" << manifest.createdAt << '\n';
  for (const ManifestEntry &entry : manifest.files) {
    output << entry.relativePath << '\t' << entry.size << '\t'
           << entry.sha256Hex << '\n';
  }
}

BackupManifest BackupService::readManifest(
    const std::string &backupDirectory) const {
  const fs::path path = fs::path(backupDirectory) / kManifestFileName;
  std::ifstream input(path);
  if (!input) {
    throw StorageException("missing backup manifest: " + path.string());
  }
  BackupManifest manifest;
  std::string line;
  if (!std::getline(input, line) || line != kManifestHeader) {
    throw StorageException("invalid backup manifest header");
  }
  if (!std::getline(input, line) || !startsWith(line, "engine_version=")) {
    throw StorageException("invalid backup manifest engine_version");
  }
  manifest.engineVersion = line.substr(std::string("engine_version=").size());
  if (!std::getline(input, line) || !startsWith(line, "created_at=")) {
    throw StorageException("invalid backup manifest created_at");
  }
  manifest.createdAt = line.substr(std::string("created_at=").size());
  while (std::getline(input, line)) {
    if (line.empty()) {
      continue;
    }
    std::istringstream stream(line);
    ManifestEntry entry;
    std::string sizeText;
    if (!std::getline(stream, entry.relativePath, '\t') ||
        !std::getline(stream, sizeText, '\t') ||
        !std::getline(stream, entry.sha256Hex)) {
      throw StorageException("invalid backup manifest entry: " + line);
    }
    entry.size = static_cast<uint64_t>(std::stoull(sizeText));
    if (!isRelativePathSafe(entry.relativePath)) {
      throw StorageException("unsafe path in manifest: " + entry.relativePath);
    }
    manifest.files.push_back(std::move(entry));
  }
  return manifest;
}

void BackupService::verifyChecksums(const std::string &backupDirectory,
                                    const BackupManifest &manifest) const {
  for (const ManifestEntry &entry : manifest.files) {
    const fs::path path = fs::path(backupDirectory) / entry.relativePath;
    if (!fs::is_regular_file(path)) {
      throw StorageException("missing backup file: " + entry.relativePath);
    }
    const uint64_t size = fs::file_size(path);
    if (size != entry.size) {
      throw StorageException("size mismatch for " + entry.relativePath);
    }
    const std::string digest = computeFileSha256(path.string());
    if (digest != entry.sha256Hex) {
      throw StorageException("checksum mismatch for " + entry.relativePath);
    }
  }
}

void BackupService::replaceDataDirectory(
    const std::string &backupDirectory, const std::string &dataDirectory,
    const BackupManifest &manifest) const {
  const fs::path dataPath(dataDirectory);
  if (fs::exists(dataPath)) {
    fs::remove_all(dataPath);
  }
  fs::create_directories(dataPath);
  for (const ManifestEntry &entry : manifest.files) {
    const fs::path source = fs::path(backupDirectory) / entry.relativePath;
    const fs::path target = dataPath / entry.relativePath;
    fs::create_directories(target.parent_path());
    fs::copy_file(source, target, fs::copy_options::overwrite_existing);
  }
}

bool BackupService::isDirectoryNonEmpty(
    const std::string &directoryPath) const {
  const fs::path path(directoryPath);
  if (!fs::exists(path) || !fs::is_directory(path)) {
    return false;
  }
  return fs::directory_iterator(path) != fs::directory_iterator{};
}

bool BackupService::hasWalStagingFiles(
    const std::string &dataDirectory) const {
  const fs::path path(dataDirectory);
  if (!fs::exists(path) || !fs::is_directory(path)) {
    return false;
  }
  for (const fs::directory_entry &entry : fs::directory_iterator(path)) {
    if (!entry.is_regular_file()) {
      continue;
    }
    if (startsWith(entry.path().filename().string(), kWalStagePrefix)) {
      return true;
    }
  }
  return false;
}

std::string BackupService::computeFileSha256(
    const std::string &filePath) const {
  return to_hex(sha256(readFileBytes(filePath)));
}

std::string BackupService::formatTimestampUtc() const {
  const std::time_t now =
      std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  std::tm utcTime{};
#if defined(_WIN32)
  gmtime_s(&utcTime, &now);
#else
  gmtime_r(&now, &utcTime);
#endif
  char buffer[32];
  if (std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &utcTime) ==
      0) {
    throw StorageException("failed to format backup timestamp");
  }
  return std::string(buffer);
}

std::vector<uint8_t> BackupService::readFileBytes(
    const std::string &filePath) const {
  std::ifstream file(filePath, std::ios::binary);
  if (!file) {
    throw StorageException("cannot read file: " + filePath);
  }
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(file)),
                              std::istreambuf_iterator<char>());
}

}  // namespace db
