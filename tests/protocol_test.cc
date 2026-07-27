#include "network/protocol.h"

#include "gtest/gtest.h"
#include "types/value.h"

namespace db {
namespace {

TEST(ProtocolTest, ParseRequestValid) {
  auto q = Protocol::parse_request("QUERY|SELECT 1\n");
  EXPECT_EQ(q.type, "QUERY");
  EXPECT_EQ(q.data, "SELECT 1");

  auto p = Protocol::parse_request("PING|\n");
  EXPECT_EQ(p.type, "PING");
  EXPECT_EQ(p.data, "");

  auto a = Protocol::parse_request("AUTH|admin|secret\n");
  EXPECT_EQ(a.type, "AUTH");
  EXPECT_EQ(a.data, "admin|secret");
}

TEST(ProtocolTest, ParseRequestInvalidThrows) {
  EXPECT_THROW(static_cast<void>(Protocol::parse_request("BAD")), std::runtime_error);
}

TEST(ProtocolTest, FormatResponseError) {
  Protocol::Response r;
  r.success = false;
  r.message = "oops";
  std::string s = Protocol::format_response(r);
  EXPECT_EQ(s, "ERROR|oops\n");
}

TEST(ProtocolTest, FormatResponseOkWithColumnsAndRows) {
  Protocol::Response r;
  r.success = true;
  r.column_names = {"a", "b"};
  r.rows = {{"1", "x"}, {"2", "y"}};
  std::string s = Protocol::format_response(r);
  EXPECT_EQ(s, "OK|a\tb\n1\tx\n2\ty\n");
}

TEST(ProtocolTest, FormatResponseOkEmptyRowsStillEndsHeaderLine) {
  Protocol::Response r;
  r.success = true;
  r.column_names = {"only"};
  r.rows = {};
  std::string s = Protocol::format_response(r);
  EXPECT_EQ(s, "OK|only\n");
}

TEST(ProtocolTest, FormatQueryResultSuccess) {
  QueryResult qr;
  qr.success = true;
  qr.message = "SELECT OK";
  qr.column_names = {"n", "s"};
  qr.rows.push_back({Value(int64_t{7}), Value(std::string("hi"))});
  std::string s = Protocol::format_query_result(qr);
  EXPECT_NE(s.find("OK|"), std::string::npos);
  EXPECT_NE(s.find("n\ts"), std::string::npos);
  EXPECT_NE(s.find("7"), std::string::npos);
  EXPECT_NE(s.find("hi"), std::string::npos);
}

TEST(ProtocolTest, FormatQueryResultFailureUsesErrorBranch) {
  QueryResult qr = QueryResult::error_result("boom");
  std::string s = Protocol::format_query_result(qr);
  EXPECT_EQ(s, "ERROR|boom\n");
}

}  // namespace
}  // namespace db
