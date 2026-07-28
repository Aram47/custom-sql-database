#include <iostream>
#include <string>
#include <vector>

#include "core/database.h"
#include "storage/backup_service.h"
#include "utils/exceptions.h"

namespace {

void printUsage(const char *programName) {
  std::cerr
      << "Usage:\n"
      << "  " << programName
      << " backup --data-dir <path> --output-dir <path>\n"
      << "  " << programName
      << " restore --backup-dir <path> --data-dir <path> [--force]\n"
      << "  " << programName << " checkpoint --data-dir <path>\n";
}

bool takeValue(const std::vector<std::string> &args, size_t *index,
               std::string *value) {
  if (*index + 1 >= args.size()) {
    return false;
  }
  ++(*index);
  *value = args[*index];
  return true;
}

int runBackup(const std::vector<std::string> &args) {
  db::BackupRequest request;
  for (size_t i = 0; i < args.size(); ++i) {
    if (args[i] == "--data-dir" && takeValue(args, &i, &request.dataDirectory)) {
      continue;
    }
    if (args[i] == "--output-dir" &&
        takeValue(args, &i, &request.outputDirectory)) {
      continue;
    }
    std::cerr << "Unknown or incomplete backup argument: " << args[i] << '\n';
    return 1;
  }
  db::BackupService service;
  service.executeBackup(request);
  std::cout << "Backup written to " << request.outputDirectory << '\n';
  return 0;
}

int runRestore(const std::vector<std::string> &args) {
  db::RestoreRequest request;
  for (size_t i = 0; i < args.size(); ++i) {
    if (args[i] == "--data-dir" && takeValue(args, &i, &request.dataDirectory)) {
      continue;
    }
    if (args[i] == "--backup-dir" &&
        takeValue(args, &i, &request.backupDirectory)) {
      continue;
    }
    if (args[i] == "--force") {
      request.force = true;
      continue;
    }
    std::cerr << "Unknown or incomplete restore argument: " << args[i] << '\n';
    return 1;
  }
  db::BackupService service;
  service.executeRestore(request);
  std::cout << "Restored into " << request.dataDirectory << '\n';
  return 0;
}

int runCheckpoint(const std::vector<std::string> &args) {
  std::string dataDirectory;
  for (size_t i = 0; i < args.size(); ++i) {
    if (args[i] == "--data-dir" && takeValue(args, &i, &dataDirectory)) {
      continue;
    }
    std::cerr << "Unknown or incomplete checkpoint argument: " << args[i]
              << '\n';
    return 1;
  }
  if (dataDirectory.empty()) {
    std::cerr << "checkpoint requires --data-dir\n";
    return 1;
  }
  db::Database database(dataDirectory, 0);
  database.load_from_disk();
  database.checkpoint();
  std::cout << "Checkpoint complete for " << dataDirectory << '\n';
  return 0;
}

}  // namespace

int main(int argc, char *argv[]) {
  if (argc < 2) {
    printUsage(argv[0]);
    return 1;
  }
  const std::string command = argv[1];
  std::vector<std::string> args;
  for (int i = 2; i < argc; ++i) {
    args.emplace_back(argv[i]);
  }
  try {
    if (command == "backup") {
      return runBackup(args);
    }
    if (command == "restore") {
      return runRestore(args);
    }
    if (command == "checkpoint") {
      return runCheckpoint(args);
    }
    if (command == "-h" || command == "--help") {
      printUsage(argv[0]);
      return 0;
    }
    std::cerr << "Unknown command: " << command << '\n';
    printUsage(argv[0]);
    return 1;
  } catch (const db::DatabaseException &error) {
    std::cerr << error.what() << '\n';
    return 1;
  } catch (const std::exception &error) {
    std::cerr << "Error: " << error.what() << '\n';
    return 1;
  }
}
