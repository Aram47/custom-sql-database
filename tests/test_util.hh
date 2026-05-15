#pragma once

#include <atomic>
#include <filesystem>
#include <string>

namespace db {
namespace test_util {

namespace fs = std::filesystem;

/** Unique temporary directory under the system temp path; removed on destroy. */
class TempDbDir {
 public:
  TempDbDir() {
    static std::atomic<uint64_t> seq{0};
    path_ = fs::temp_directory_path() /
            ("custom_sql_db_test_" + std::to_string(++seq));
    fs::create_directories(path_);
  }

  ~TempDbDir() {
    std::error_code ec;
    fs::remove_all(path_, ec);
  }

  TempDbDir(const TempDbDir &) = delete;
  TempDbDir &operator=(const TempDbDir &) = delete;

  const fs::path &path() const { return path_; }
  std::string path_string() const { return path_.string(); }

 private:
  fs::path path_;
};

}  // namespace test_util
}  // namespace db
