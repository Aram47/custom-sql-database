#include "core/database.h"
#include "storage/backup_service.h"
#include "utils/engine_version.h"
#include "utils/exceptions.h"

#include <filesystem>
#include <fstream>
#include <string>

#include "gtest/gtest.h"
#include "tests/test_util.hh"

namespace db {
namespace {

namespace fs = std::filesystem;

void createSampleDatabase(const std::string &dataDirectory) {
  Database database(dataDirectory, 0);
  ASSERT_TRUE(
      database.execute_query("CREATE TABLE items (id INT PRIMARY KEY, n STRING)")
          .success);
  ASSERT_TRUE(
      database.execute_query("INSERT INTO items VALUES (1, 'alpha')").success);
  ASSERT_TRUE(
      database.execute_query("INSERT INTO items VALUES (2, 'beta')").success);
}

bool manifestListsTable(const fs::path &backupDirectory,
                        const std::string &tableFile) {
  const fs::path manifestPath =
      backupDirectory / BackupService::kManifestFileName;
  std::ifstream input(manifestPath);
  if (!input) {
    return false;
  }
  std::string line;
  while (std::getline(input, line)) {
    if (line.rfind(tableFile, 0) == 0) {
      return true;
    }
  }
  return false;
}

TEST(BackupTest, BackupAfterInsertsWritesManifest) {
  test_util::TempDbDir dataDir;
  test_util::TempDbDir backupDir;
  fs::remove_all(backupDir.path());
  createSampleDatabase(dataDir.path_string());
  BackupRequest request;
  request.dataDirectory = dataDir.path_string();
  request.outputDirectory = backupDir.path_string();
  BackupService service;
  service.executeBackup(request);
  EXPECT_TRUE(fs::exists(backupDir.path() / BackupService::kManifestFileName));
  EXPECT_TRUE(manifestListsTable(backupDir.path(), "items.db"));
  std::ifstream manifest(backupDir.path() / BackupService::kManifestFileName);
  std::string header;
  ASSERT_TRUE(std::getline(manifest, header));
  EXPECT_EQ(header, BackupService::kManifestHeader);
  std::string versionLine;
  ASSERT_TRUE(std::getline(manifest, versionLine));
  EXPECT_EQ(versionLine, std::string("engine_version=") + kEngineVersion);
}

TEST(BackupTest, RestoreRoundTripPreservesData) {
  test_util::TempDbDir dataDir;
  test_util::TempDbDir backupDir;
  fs::remove_all(backupDir.path());
  createSampleDatabase(dataDir.path_string());
  BackupRequest backupRequest;
  backupRequest.dataDirectory = dataDir.path_string();
  backupRequest.outputDirectory = backupDir.path_string();
  BackupService service;
  service.executeBackup(backupRequest);
  fs::remove_all(dataDir.path());
  fs::create_directories(dataDir.path());
  RestoreRequest restoreRequest;
  restoreRequest.backupDirectory = backupDir.path_string();
  restoreRequest.dataDirectory = dataDir.path_string();
  restoreRequest.force = true;
  service.executeRestore(restoreRequest);
  Database restored(dataDir.path_string(), 0);
  restored.load_from_disk();
  const QueryResult result =
      restored.execute_query("SELECT id, n FROM items ORDER BY id");
  ASSERT_TRUE(result.success) << result.message;
  ASSERT_EQ(result.rows.size(), 2u);
  EXPECT_EQ(result.rows[0][0].as_int(), 1);
  EXPECT_EQ(result.rows[0][1].as_string(), "alpha");
  EXPECT_EQ(result.rows[1][0].as_int(), 2);
  EXPECT_EQ(result.rows[1][1].as_string(), "beta");
}

TEST(BackupTest, ChecksumMismatchFailsRestore) {
  test_util::TempDbDir dataDir;
  test_util::TempDbDir backupDir;
  fs::remove_all(backupDir.path());
  createSampleDatabase(dataDir.path_string());
  BackupRequest backupRequest;
  backupRequest.dataDirectory = dataDir.path_string();
  backupRequest.outputDirectory = backupDir.path_string();
  BackupService service;
  service.executeBackup(backupRequest);
  {
    std::ofstream corrupt(backupDir.path() / "items.db",
                          std::ios::binary | std::ios::app);
    corrupt << 'X';
  }
  RestoreRequest restoreRequest;
  restoreRequest.backupDirectory = backupDir.path_string();
  restoreRequest.dataDirectory = dataDir.path_string();
  restoreRequest.force = true;
  EXPECT_THROW(service.executeRestore(restoreRequest), StorageException);
}

TEST(BackupTest, StagingFilesRefuseBackup) {
  test_util::TempDbDir dataDir;
  test_util::TempDbDir backupDir;
  fs::remove_all(backupDir.path());
  createSampleDatabase(dataDir.path_string());
  {
    std::ofstream staging(dataDir.path() / ".wal_stage_items.db",
                          std::ios::binary);
    staging << "incomplete";
  }
  BackupRequest request;
  request.dataDirectory = dataDir.path_string();
  request.outputDirectory = backupDir.path_string();
  BackupService service;
  EXPECT_THROW(service.executeBackup(request), StorageException);
}

TEST(BackupTest, RestoreWithoutForceFailsOnNonEmpty) {
  test_util::TempDbDir dataDir;
  test_util::TempDbDir backupDir;
  fs::remove_all(backupDir.path());
  createSampleDatabase(dataDir.path_string());
  BackupRequest backupRequest;
  backupRequest.dataDirectory = dataDir.path_string();
  backupRequest.outputDirectory = backupDir.path_string();
  BackupService service;
  service.executeBackup(backupRequest);
  RestoreRequest restoreRequest;
  restoreRequest.backupDirectory = backupDir.path_string();
  restoreRequest.dataDirectory = dataDir.path_string();
  restoreRequest.force = false;
  EXPECT_THROW(service.executeRestore(restoreRequest), StorageException);
}

}  // namespace
}  // namespace db
