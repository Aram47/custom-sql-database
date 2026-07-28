#include "core/database.h"
#include "core/session_context.h"
#include "parser/parser.h"

#include "gtest/gtest.h"
#include "tests/test_util.hh"
#include "utils/exceptions.h"

namespace db {
namespace {

TEST(TriggerTest, AfterInsertWritesAuditRow) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string(), 0);
  ASSERT_TRUE(
      db.execute_query("CREATE TABLE t (id INT PRIMARY KEY)").success);
  ASSERT_TRUE(
      db.execute_query("CREATE TABLE audit (id INT)").success);
  QueryResult create = db.execute_query(
      "CREATE TRIGGER audit_ins AFTER INSERT ON t FOR EACH ROW EXECUTE $$\n"
      "  INSERT INTO audit VALUES (NEW.id);\n"
      "$$");
  ASSERT_TRUE(create.success) << create.message;
  ASSERT_TRUE(db.execute_query("INSERT INTO t VALUES (7)").success);
  QueryResult audit = db.execute_query("SELECT id FROM audit");
  ASSERT_TRUE(audit.success) << audit.message;
  ASSERT_EQ(audit.rows.size(), 1u);
  EXPECT_EQ(audit.rows[0][0].as_int(), 7);
}

TEST(TriggerTest, BeforeInsertAdjustsNew) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string(), 0);
  ASSERT_TRUE(
      db.execute_query("CREATE TABLE t (id INT PRIMARY KEY, x INT)").success);
  QueryResult create = db.execute_query(
      "CREATE TRIGGER bump BEFORE INSERT ON t FOR EACH ROW EXECUTE $$\n"
      "  SET NEW.x = NEW.x + 1;\n"
      "$$");
  ASSERT_TRUE(create.success) << create.message;
  ASSERT_TRUE(db.execute_query("INSERT INTO t VALUES (1, 10)").success);
  QueryResult select = db.execute_query("SELECT x FROM t");
  ASSERT_TRUE(select.success) << select.message;
  ASSERT_EQ(select.rows.size(), 1u);
  EXPECT_EQ(select.rows[0][0].as_int(), 11);
}

TEST(TriggerTest, RecursiveTriggerHitsDepthLimit) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string(), 0);
  ASSERT_TRUE(
      db.execute_query("CREATE TABLE t (id INT PRIMARY KEY)").success);
  QueryResult create = db.execute_query(
      "CREATE TRIGGER recurse AFTER INSERT ON t FOR EACH ROW EXECUTE $$\n"
      "  INSERT INTO t VALUES (NEW.id + 1);\n"
      "$$");
  ASSERT_TRUE(create.success) << create.message;
  QueryResult insert = db.execute_query("INSERT INTO t VALUES (1)");
  EXPECT_FALSE(insert.success);
  EXPECT_NE(insert.message.find("recursion depth exceeded"),
            std::string::npos)
      << insert.message;
}

TEST(TriggerTest, DropTriggerStopsFiring) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string(), 0);
  ASSERT_TRUE(
      db.execute_query("CREATE TABLE t (id INT PRIMARY KEY)").success);
  ASSERT_TRUE(
      db.execute_query("CREATE TABLE audit (id INT)").success);
  ASSERT_TRUE(db.execute_query(
                       "CREATE TRIGGER audit_ins AFTER INSERT ON t FOR EACH "
                       "ROW EXECUTE $$\n"
                       "  INSERT INTO audit VALUES (NEW.id);\n"
                       "$$")
                  .success);
  ASSERT_TRUE(db.execute_query("DROP TRIGGER audit_ins").success);
  ASSERT_TRUE(db.execute_query("INSERT INTO t VALUES (1)").success);
  QueryResult audit = db.execute_query("SELECT id FROM audit");
  ASSERT_TRUE(audit.success);
  EXPECT_EQ(audit.rows.size(), 0u);
}

TEST(TriggerTest, RollbackUndoesTriggerSideEffects) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string(), 0);
  SessionContext session;
  ASSERT_TRUE(
      db.execute_query("CREATE TABLE t (id INT PRIMARY KEY)", &session)
          .success);
  ASSERT_TRUE(
      db.execute_query("CREATE TABLE audit (id INT)", &session).success);
  ASSERT_TRUE(db.execute_query(
                       "CREATE TRIGGER audit_ins AFTER INSERT ON t FOR EACH "
                       "ROW EXECUTE $$\n"
                       "  INSERT INTO audit VALUES (NEW.id);\n"
                       "$$",
                       &session)
                  .success);
  ASSERT_TRUE(db.execute_query("BEGIN", &session).success);
  ASSERT_TRUE(db.execute_query("INSERT INTO t VALUES (1)", &session).success);
  ASSERT_TRUE(db.execute_query("ROLLBACK", &session).success);
  QueryResult t_rows = db.execute_query("SELECT id FROM t", &session);
  QueryResult audit = db.execute_query("SELECT id FROM audit", &session);
  ASSERT_TRUE(t_rows.success);
  ASSERT_TRUE(audit.success);
  EXPECT_EQ(t_rows.rows.size(), 0u);
  EXPECT_EQ(audit.rows.size(), 0u);
}

TEST(TriggerTest, PersistAndReload) {
  test_util::TempDbDir tmp;
  {
    Database db(tmp.path_string(), 0);
    ASSERT_TRUE(
        db.execute_query("CREATE TABLE t (id INT PRIMARY KEY)").success);
    ASSERT_TRUE(
        db.execute_query("CREATE TABLE audit (id INT)").success);
    ASSERT_TRUE(db.execute_query(
                         "CREATE TRIGGER audit_ins AFTER INSERT ON t FOR EACH "
                         "ROW EXECUTE $$\n"
                         "  INSERT INTO audit VALUES (NEW.id);\n"
                         "$$")
                    .success);
  }
  Database db2(tmp.path_string(), 0);
  db2.load_from_disk();
  ASSERT_TRUE(db2.execute_query("INSERT INTO t VALUES (9)").success);
  QueryResult audit = db2.execute_query("SELECT id FROM audit");
  ASSERT_TRUE(audit.success) << audit.message;
  ASSERT_EQ(audit.rows.size(), 1u);
  EXPECT_EQ(audit.rows[0][0].as_int(), 9);
}

TEST(TriggerTest, BeforeUpdateAdjustsNew) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string(), 0);
  ASSERT_TRUE(
      db.execute_query("CREATE TABLE t (id INT PRIMARY KEY, x INT)").success);
  ASSERT_TRUE(db.execute_query("INSERT INTO t VALUES (1, 5)").success);
  ASSERT_TRUE(db.execute_query(
                       "CREATE TRIGGER bump_upd BEFORE UPDATE ON t FOR EACH "
                       "ROW EXECUTE $$\n"
                       "  SET NEW.x = NEW.x * 2;\n"
                       "$$")
                  .success);
  ASSERT_TRUE(db.execute_query("UPDATE t SET x = 3 WHERE id = 1").success);
  QueryResult select = db.execute_query("SELECT x FROM t");
  ASSERT_TRUE(select.success);
  ASSERT_EQ(select.rows.size(), 1u);
  EXPECT_EQ(select.rows[0][0].as_int(), 6);
}

}  // namespace
}  // namespace db
