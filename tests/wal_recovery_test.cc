#include "core/database.h"
#include "storage/persistence_manager.h"
#include "storage/wal_manager.h"

#include <filesystem>
#include <fstream>

#include "gtest/gtest.h"
#include "tests/test_util.hh"

namespace db {
namespace {

TEST(WalRecoveryTest, RecoversCommittedBlobWithoutDone) {
  test_util::TempDbDir tmp;
  {
    Database db(tmp.path_string());
    ASSERT_TRUE(
        db.execute_query("CREATE TABLE t (id INT PRIMARY KEY)").success);
    ASSERT_TRUE(db.execute_query("INSERT INTO t VALUES (42)").success);
  }
  const std::string db_path = (tmp.path() / "t.db").string();
  ASSERT_TRUE(std::filesystem::exists(db_path));
  std::ifstream in(db_path, std::ios::binary);
  std::vector<uint8_t> blob((std::istreambuf_iterator<char>(in)),
                            std::istreambuf_iterator<char>());
  in.close();
  std::filesystem::remove(db_path);
  WalManager wal(tmp.path_string());
  wal.append_table_blob("t", blob);
  wal.append_commit(1);
  wal.sync();
  Database recovered(tmp.path_string());
  recovered.load_from_disk();
  ASSERT_TRUE(recovered.has_table("t"));
  auto r = recovered.execute_query("SELECT id FROM t");
  ASSERT_TRUE(r.success) << r.message;
  ASSERT_EQ(r.rows.size(), 1u);
  EXPECT_EQ(r.rows[0][0].as_int(), 42);
}

}  // namespace
}  // namespace db
