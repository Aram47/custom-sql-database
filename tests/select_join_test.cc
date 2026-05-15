#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "core/database.h"

#include "gtest/gtest.h"
#include "tests/test_util.hh"

namespace db {
namespace {

void sort_int_string_pairs(std::vector<std::pair<int, std::string>> *rows) {
  std::sort(rows->begin(), rows->end(),
            [](const std::pair<int, std::string> &p1,
               const std::pair<int, std::string> &p2) {
              if (p1.first != p2.first) return p1.first < p2.first;
              return p1.second < p2.second;
            });
}

TEST(SelectJoinTest, InnerJoinEquality) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string());
  ASSERT_TRUE(db
                  .execute_query(
                      "CREATE TABLE orders (id INT PRIMARY KEY, cust_id INT)")
                  .success);
  ASSERT_TRUE(
      db.execute_query(
           "CREATE TABLE customers (id INT PRIMARY KEY, name STRING NOT NULL)")
          .success);
  ASSERT_TRUE(
      db.execute_query("INSERT INTO orders VALUES (1, 10), (2, 10), (3, 99)")
          .success);
  ASSERT_TRUE(db.execute_query(
                   "INSERT INTO customers VALUES (10, 'Ann'), (20, 'Bob')")
                  .success);

  auto r = db.execute_query(
      "SELECT orders.id, customers.name FROM orders INNER JOIN customers ON "
      "orders.cust_id = customers.id");

  ASSERT_TRUE(r.success) << r.message;
  ASSERT_EQ(r.rows.size(), 2u);
  std::vector<std::pair<int, std::string>> got;
  for (const auto &row : r.rows) {
    got.emplace_back(row[0].as_int(), row[1].as_string());
  }
  sort_int_string_pairs(&got);
  EXPECT_EQ(got[0].first, 1);
  EXPECT_EQ(got[0].second, "Ann");
  EXPECT_EQ(got[1].first, 2);
  EXPECT_EQ(got[1].second, "Ann");
}

TEST(SelectJoinTest, LeftJoinPreservesUnmatchedLeftNullRight) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string());
  ASSERT_TRUE(db.execute_query("CREATE TABLE a (aid INT PRIMARY KEY)").success);
  ASSERT_TRUE(db.execute_query("CREATE TABLE b (bid INT PRIMARY KEY)").success);
  ASSERT_TRUE(db.execute_query("INSERT INTO a VALUES (1), (2)").success);
  ASSERT_TRUE(db.execute_query("INSERT INTO b VALUES (1)").success);

  auto r = db.execute_query(
      "SELECT a.aid, b.bid FROM a LEFT JOIN b ON a.aid = b.bid");

  ASSERT_TRUE(r.success) << r.message;
  ASSERT_EQ(r.rows.size(), 2u);
  int null_bid_rows = 0;
  int match_rows = 0;
  for (const auto &row : r.rows) {
    const int aid = row[0].as_int();
    if (aid == 1) {
      ASSERT_FALSE(row[1].is_null());
      EXPECT_EQ(row[1].as_int(), 1);
      match_rows++;
    } else if (aid == 2) {
      EXPECT_TRUE(row[1].is_null());
      null_bid_rows++;
    }
  }
  EXPECT_EQ(match_rows, 1);
  EXPECT_EQ(null_bid_rows, 1);
}

TEST(SelectJoinTest, RightJoinKeepsRightWhenLeftMissing) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string());
  ASSERT_TRUE(
      db.execute_query("CREATE TABLE r_a (id INT PRIMARY KEY)").success);
  ASSERT_TRUE(
      db.execute_query(
           "CREATE TABLE r_b (id INT PRIMARY KEY, k INT NOT NULL)")
          .success);
  ASSERT_TRUE(db.execute_query("INSERT INTO r_b VALUES (1, 42)").success);

  auto r = db.execute_query(
      "SELECT r_a.id, r_b.k FROM r_a RIGHT JOIN r_b ON r_a.id = r_b.id");

  ASSERT_TRUE(r.success) << r.message;
  ASSERT_EQ(r.rows.size(), 1u);
  EXPECT_TRUE(r.rows[0][0].is_null());
  EXPECT_EQ(r.rows[0][1].as_int(), 42);
}

TEST(SelectJoinTest, TwoSequentialInnerJoins) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string());
  ASSERT_TRUE(db.execute_query("CREATE TABLE s (sid INT PRIMARY KEY)").success);
  ASSERT_TRUE(db.execute_query("CREATE TABLE j1 (jid INT PRIMARY KEY)").success);
  ASSERT_TRUE(db.execute_query("CREATE TABLE j2 (j2id INT PRIMARY KEY)").success);
  ASSERT_TRUE(db.execute_query("INSERT INTO s VALUES (100)").success);
  ASSERT_TRUE(db.execute_query("INSERT INTO j1 VALUES (100)").success);
  ASSERT_TRUE(db.execute_query("INSERT INTO j2 VALUES (100)").success);

  auto r = db.execute_query(
      "SELECT j2.j2id FROM s INNER JOIN j1 ON s.sid = j1.jid "
      "INNER JOIN j2 ON j1.jid = j2.j2id");

  ASSERT_TRUE(r.success) << r.message;
  ASSERT_EQ(r.rows.size(), 1u);
  EXPECT_EQ(r.rows[0][0].as_int(), 100);
}

TEST(SelectJoinTest, InnerJoinWithoutOnCartesianProduct) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string());
  ASSERT_TRUE(db.execute_query("CREATE TABLE x (i INT PRIMARY KEY)").success);
  ASSERT_TRUE(db.execute_query("CREATE TABLE y (j INT PRIMARY KEY)").success);
  ASSERT_TRUE(db.execute_query("INSERT INTO x VALUES (1), (2)").success);
  ASSERT_TRUE(
      db.execute_query("INSERT INTO y VALUES (10), (20), (30)").success);

  auto r = db.execute_query("SELECT x.i, y.j FROM x INNER JOIN y");
  ASSERT_TRUE(r.success) << r.message;
  EXPECT_EQ(r.rows.size(), 6u);
}

TEST(SelectJoinTest, CrossJoinExplicitCartesian) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string());
  ASSERT_TRUE(db.execute_query("CREATE TABLE cx (i INT PRIMARY KEY)").success);
  ASSERT_TRUE(db.execute_query("CREATE TABLE cy (j INT PRIMARY KEY)").success);
  ASSERT_TRUE(db.execute_query("INSERT INTO cx VALUES (7)").success);
  ASSERT_TRUE(
      db.execute_query("INSERT INTO cy VALUES (8), (9)").success);

  auto r = db.execute_query("SELECT cx.i, cy.j FROM cx CROSS JOIN cy");
  ASSERT_TRUE(r.success) << r.message;
  ASSERT_EQ(r.rows.size(), 2u);
}

TEST(SelectJoinTest, CrossJoinWithOnRejectedAtParse) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string());
  ASSERT_TRUE(db.execute_query("CREATE TABLE x (i INT PRIMARY KEY)").success);
  ASSERT_TRUE(db.execute_query("CREATE TABLE y (j INT PRIMARY KEY)").success);
  auto r = db.execute_query(
      "SELECT * FROM x CROSS JOIN y ON x.i = y.j");
  ASSERT_FALSE(r.success);
  EXPECT_NE(r.message.find("CROSS JOIN cannot have ON"), std::string::npos);
}

TEST(SelectJoinTest, LeftJoinRequiresOn) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string());
  ASSERT_TRUE(db.execute_query("CREATE TABLE x (i INT PRIMARY KEY)").success);
  ASSERT_TRUE(db.execute_query("CREATE TABLE y (j INT PRIMARY KEY)").success);
  auto r = db.execute_query("SELECT * FROM x LEFT JOIN y");
  ASSERT_FALSE(r.success);
  EXPECT_NE(r.message.find("LEFT JOIN requires ON"), std::string::npos);
}

TEST(SelectJoinTest, FullOuterJoinUnmatchedBothSides) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string());
  ASSERT_TRUE(db.execute_query("CREATE TABLE fa (id INT PRIMARY KEY)").success);
  ASSERT_TRUE(db.execute_query("CREATE TABLE fb (id INT PRIMARY KEY)").success);
  ASSERT_TRUE(db.execute_query("INSERT INTO fa VALUES (1), (2), (3)").success);
  ASSERT_TRUE(db.execute_query("INSERT INTO fb VALUES (2), (4)").success);

  auto r = db.execute_query(
      "SELECT fa.id, fb.id FROM fa FULL OUTER JOIN fb ON fa.id = fb.id");

  ASSERT_TRUE(r.success) << r.message;
  ASSERT_EQ(r.rows.size(), 4u);

  std::vector<std::string> sigs;
  sigs.reserve(r.rows.size());
  for (const auto &row : r.rows) {
    const std::string l =
        row[0].is_null() ? "n" : std::to_string(row[0].as_int());
    const std::string rgt =
        row[1].is_null() ? "n" : std::to_string(row[1].as_int());
    sigs.push_back(l + ":" + rgt);
  }
  std::sort(sigs.begin(), sigs.end());
  std::vector<std::string> want{"1:n", "2:2", "3:n", "n:4"};
  std::sort(want.begin(), want.end());
  EXPECT_EQ(sigs, want);
}

TEST(SelectJoinTest, AmbiguousBareColumnRejected) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string());
  ASSERT_TRUE(db.execute_query("CREATE TABLE t1 (id INT PRIMARY KEY)").success);
  ASSERT_TRUE(db.execute_query("CREATE TABLE t2 (id INT PRIMARY KEY)").success);
  ASSERT_TRUE(db.execute_query("INSERT INTO t1 VALUES (1)").success);
  ASSERT_TRUE(db.execute_query("INSERT INTO t2 VALUES (1)").success);

  auto r = db.execute_query(
      "SELECT id FROM t1 INNER JOIN t2 ON t1.id = t2.id");
  ASSERT_FALSE(r.success);
  EXPECT_NE(r.message.find("Ambiguous"), std::string::npos);
}

TEST(SelectJoinTest, WildcardQualifiedColumnNames) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string());
  ASSERT_TRUE(db.execute_query("CREATE TABLE p (pid INT PRIMARY KEY)").success);
  ASSERT_TRUE(db.execute_query("CREATE TABLE q (qid INT PRIMARY KEY)").success);
  ASSERT_TRUE(db.execute_query("INSERT INTO p VALUES (7)").success);
  ASSERT_TRUE(db.execute_query("INSERT INTO q VALUES (7)").success);

  auto r = db.execute_query("SELECT * FROM p INNER JOIN q ON p.pid = q.qid");
  ASSERT_TRUE(r.success) << r.message;
  ASSERT_EQ(r.column_names.size(), 2u);
  EXPECT_NE(r.column_names[0].find('.'), std::string::npos);
  EXPECT_NE(r.column_names[1].find('.'), std::string::npos);
}

}  // namespace
}  // namespace db
