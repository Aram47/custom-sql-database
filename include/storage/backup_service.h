#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace db {

/** Input for an offline backup of a data directory. */
struct BackupRequest {
  std::string dataDirectory;
  std::string outputDirectory;
};

/** Input for restoring a backup into a data directory. */
struct RestoreRequest {
  std::string backupDirectory;
  std::string dataDirectory;
  bool force{false};
};

/** One file recorded in a backup manifest. */
struct ManifestEntry {
  std::string relativePath;
  uint64_t size{0};
  std::string sha256Hex;
};

/** Parsed backup_manifest contents. */
struct BackupManifest {
  std::string engineVersion;
  std::string createdAt;
  std::vector<ManifestEntry> files;
};

/**
 * Offline checkpoint + copy + SHA-256 manifest backup/restore.
 * Server must be stopped; refuses mid-commit WAL staging files.
 */
class BackupService {
 public:
  static constexpr const char *kManifestFileName = "backup_manifest";
  static constexpr const char *kManifestHeader = "nobugdb-backup-manifest-v1";

  /** Checkpoints, copies the data dir, and writes backup_manifest. */
  void executeBackup(const BackupRequest &request);

  /** Verifies checksums and replaces the data dir from a backup. */
  void executeRestore(const RestoreRequest &request);

 private:
  void runCheckpoint(const std::string &dataDirectory);
  void refuseIfStagingPresent(const std::string &dataDirectory) const;
  void refuseIfOutputInsideData(const std::string &dataDirectory,
                                const std::string &outputDirectory) const;
  void copyDataDirectory(const std::string &sourceDirectory,
                         const std::string &destDirectory) const;
  BackupManifest buildManifest(const std::string &directoryPath) const;
  void writeManifest(const std::string &backupDirectory,
                     const BackupManifest &manifest) const;
  BackupManifest readManifest(const std::string &backupDirectory) const;
  void verifyChecksums(const std::string &backupDirectory,
                       const BackupManifest &manifest) const;
  void replaceDataDirectory(const std::string &backupDirectory,
                            const std::string &dataDirectory,
                            const BackupManifest &manifest) const;
  bool isDirectoryNonEmpty(const std::string &directoryPath) const;
  bool hasWalStagingFiles(const std::string &dataDirectory) const;
  std::string computeFileSha256(const std::string &filePath) const;
  std::string formatTimestampUtc() const;
  std::vector<uint8_t> readFileBytes(const std::string &filePath) const;
};

}  // namespace db
