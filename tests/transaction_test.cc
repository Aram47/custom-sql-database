#include "core/database.h"
#include "core/lock_manager.h"
#include "core/session_context.h"

#include <atomic>
#include <chrono>
#include <thread>

#include "gtest/gtest.h"
#include "tests/test_util.hh"

namespace db {
namespace {

TEST(TransactionTest, CommitPersistsRollbackReverts) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string());
  SessionContext session;
  ASSERT_TRUE(
      db.execute_query("CREATE TABLE t (id INT PRIMARY KEY)", &session).success);
  ASSERT_TRUE(db.execute_query("BEGIN", &session).success);
  ASSERT_TRUE(db.execute_query("INSERT INTO t VALUES (1)", &session).success);
  ASSERT_TRUE(db.execute_query("ROLLBACK", &session).success);
  auto after_rb = db.execute_query("SELECT id FROM t", &session);
  ASSERT_TRUE(after_rb.success);
  EXPECT_EQ(after_rb.rows.size(), 0u);
  ASSERT_TRUE(db.execute_query("BEGIN", &session).success);
  ASSERT_TRUE(db.execute_query("INSERT INTO t VALUES (2)", &session).success);
  ASSERT_TRUE(db.execute_query("COMMIT", &session).success);
  auto after_c = db.execute_query("SELECT id FROM t", &session);
  ASSERT_TRUE(after_c.success);
  ASSERT_EQ(after_c.rows.size(), 1u);
  EXPECT_EQ(after_c.rows[0][0].as_int(), 2);
}

TEST(TransactionTest, DeadlockDetected) {
  LockManager locks;
  std::atomic<bool> a_got_deadlock{false};
  std::atomic<bool> b_got_deadlock{false};
  std::thread t1([&]() {
    try {
      locks.acquire(1, "t1", LockMode::Exclusive);
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
      locks.acquire(1, "t2", LockMode::Exclusive);
    } catch (const DeadlockException &) {
      a_got_deadlock = true;
      locks.release_all(1);
    }
  });
  std::thread t2([&]() {
    try {
      locks.acquire(2, "t2", LockMode::Exclusive);
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
      locks.acquire(2, "t1", LockMode::Exclusive);
    } catch (const DeadlockException &) {
      b_got_deadlock = true;
      locks.release_all(2);
    }
  });
  t1.join();
  t2.join();
  EXPECT_TRUE(a_got_deadlock || b_got_deadlock);
}

TEST(TransactionTest, SessionDeadlockOnCrossUpdates) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string());
  ASSERT_TRUE(db.execute_query("CREATE TABLE t1 (id INT PRIMARY KEY)").success);
  ASSERT_TRUE(db.execute_query("CREATE TABLE t2 (id INT PRIMARY KEY)").success);
  ASSERT_TRUE(db.execute_query("INSERT INTO t1 VALUES (1)").success);
  ASSERT_TRUE(db.execute_query("INSERT INTO t2 VALUES (1)").success);
  SessionContext a;
  SessionContext b;
  ASSERT_TRUE(db.execute_query("BEGIN", &a).success);
  ASSERT_TRUE(db.execute_query("BEGIN", &b).success);
  ASSERT_TRUE(db.execute_query("UPDATE t1 SET id = 1 WHERE id = 1", &a).success);
  ASSERT_TRUE(db.execute_query("UPDATE t2 SET id = 1 WHERE id = 1", &b).success);
  std::atomic<bool> a_deadlock{false};
  std::atomic<bool> b_deadlock{false};
  std::thread ta([&]() {
    auto r = db.execute_query("UPDATE t2 SET id = 1 WHERE id = 1", &a);
    if (!r.success && r.message.find("deadlock") != std::string::npos) {
      a_deadlock = true;
    }
  });
  std::thread tb([&]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    auto r = db.execute_query("UPDATE t1 SET id = 1 WHERE id = 1", &b);
    if (!r.success && r.message.find("deadlock") != std::string::npos) {
      b_deadlock = true;
    }
  });
  ta.join();
  tb.join();
  EXPECT_TRUE(a_deadlock || b_deadlock);
}

TEST(TransactionTest, SnapshotIsolationHidesUncommitted) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string(), 0);
  ASSERT_TRUE(
      db.execute_query("CREATE TABLE t (id INT PRIMARY KEY)").success);
  SessionContext writer;
  SessionContext reader;
  ASSERT_TRUE(db.execute_query("BEGIN", &writer).success);
  ASSERT_TRUE(db.execute_query("BEGIN", &reader).success);
  ASSERT_TRUE(db.execute_query("INSERT INTO t VALUES (1)", &writer).success);
  auto unseen = db.execute_query("SELECT id FROM t", &reader);
  ASSERT_TRUE(unseen.success);
  EXPECT_EQ(unseen.rows.size(), 0u);
  ASSERT_TRUE(db.execute_query("COMMIT", &writer).success);
  auto still_old = db.execute_query("SELECT id FROM t", &reader);
  ASSERT_TRUE(still_old.success);
  EXPECT_EQ(still_old.rows.size(), 0u);
  ASSERT_TRUE(db.execute_query("COMMIT", &reader).success);
  auto after = db.execute_query("SELECT id FROM t");
  ASSERT_TRUE(after.success);
  ASSERT_EQ(after.rows.size(), 1u);
}

TEST(TransactionTest, VacuumHorizonKeepsVisibleVersions) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string(), 0);
  ASSERT_TRUE(
      db.execute_query("CREATE TABLE t (id INT PRIMARY KEY, n INT)").success);
  ASSERT_TRUE(db.execute_query("INSERT INTO t VALUES (1, 10)").success);
  SessionContext reader;
  SessionContext writer;
  ASSERT_TRUE(db.execute_query("BEGIN", &reader).success);
  ASSERT_TRUE(db.execute_query("BEGIN", &writer).success);
  ASSERT_TRUE(
      db.execute_query("UPDATE t SET n = 20 WHERE id = 1", &writer).success);
  ASSERT_TRUE(db.execute_query("COMMIT", &writer).success);
  ASSERT_TRUE(db.execute_query("VACUUM", &writer).success);
  auto seen = db.execute_query("SELECT n FROM t WHERE id = 1", &reader);
  ASSERT_TRUE(seen.success) << seen.message;
  ASSERT_EQ(seen.rows.size(), 1u);
  EXPECT_EQ(seen.rows[0][0].as_int(), 10);
  ASSERT_TRUE(db.execute_query("COMMIT", &reader).success);
  ASSERT_TRUE(db.execute_query("VACUUM").success);
  auto after = db.execute_query("SELECT n FROM t WHERE id = 1");
  ASSERT_TRUE(after.success);
  ASSERT_EQ(after.rows.size(), 1u);
  EXPECT_EQ(after.rows[0][0].as_int(), 20);
}

TEST(TransactionTest, ConcurrentUpdatesOnDifferentRows) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string(), 0);
  ASSERT_TRUE(
      db.execute_query("CREATE TABLE t (id INT PRIMARY KEY, n INT)").success);
  ASSERT_TRUE(db.execute_query("INSERT INTO t VALUES (1, 0)").success);
  ASSERT_TRUE(db.execute_query("INSERT INTO t VALUES (2, 0)").success);
  SessionContext a;
  SessionContext b;
  ASSERT_TRUE(db.execute_query("BEGIN", &a).success);
  ASSERT_TRUE(db.execute_query("BEGIN", &b).success);
  ASSERT_TRUE(db.execute_query("UPDATE t SET n = 1 WHERE id = 1", &a).success);
  ASSERT_TRUE(db.execute_query("UPDATE t SET n = 2 WHERE id = 2", &b).success);
  ASSERT_TRUE(db.execute_query("COMMIT", &a).success);
  ASSERT_TRUE(db.execute_query("COMMIT", &b).success);
  auto r = db.execute_query("SELECT id, n FROM t ORDER BY id");
  ASSERT_TRUE(r.success);
  ASSERT_EQ(r.rows.size(), 2u);
  EXPECT_EQ(r.rows[0][1].as_int(), 1);
  EXPECT_EQ(r.rows[1][1].as_int(), 2);
}

}  // namespace
}  // namespace db
