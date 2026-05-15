#include "types/value.h"

#include "gtest/gtest.h"
#include "types/data_type.h"
#include "utils/exceptions.h"

namespace db {
namespace {

TEST(ValueTest, NullAndTypes) {
  Value v;
  EXPECT_TRUE(v.is_null());
  EXPECT_FALSE(v.is_int());

  Value i(int64_t{42});
  EXPECT_TRUE(i.is_int());
  EXPECT_EQ(i.as_int(), 42);

  Value d(3.14);
  EXPECT_TRUE(d.is_float());
  EXPECT_NEAR(d.as_float(), 3.14, 1e-9);

  Value s(std::string("hello"));
  EXPECT_TRUE(s.is_string());
  EXPECT_EQ(s.as_string(), "hello");

  Value b(true);
  EXPECT_TRUE(b.is_bool());
  EXPECT_TRUE(b.as_bool());
}

TEST(ValueTest, ComparisonAndArithmetic) {
  EXPECT_TRUE(Value(2) == Value(2));
  EXPECT_FALSE(Value(2) == Value(3));
  EXPECT_TRUE(Value(2) < Value(3));

  Value sum = Value(10) + Value(3);
  EXPECT_TRUE(sum.is_int());
  EXPECT_EQ(sum.as_int(), 13);

  EXPECT_NEAR((Value(2.5) * Value(4)).as_float(), 10.0, 1e-9);

  EXPECT_THROW(static_cast<void>(Value(1) / Value(0)), TypeException);
}

TEST(ValueTest, FromStringBasicTypes) {
  EXPECT_EQ(Value::from_string("100", DataType::INT).as_int(), 100);
  EXPECT_NEAR(Value::from_string("1.25", DataType::FLOAT).as_float(), 1.25,
              1e-9);
  EXPECT_EQ(Value::from_string("abc", DataType::STRING).as_string(), "abc");
  EXPECT_TRUE(Value::from_string("true", DataType::BOOLEAN).as_bool());
}

TEST(ValueTest, OrderingMixedNumericAndCrossTypes) {
  EXPECT_TRUE(Value(2) <= Value(2));
  EXPECT_TRUE(Value(2) <= Value(3));
  EXPECT_TRUE(Value(4) >= Value(3));
  EXPECT_TRUE(Value(2.5) > Value(2));     // cross-type numeric compare path
  EXPECT_TRUE(Value(1.9) < Value(int64_t{2}));

  EXPECT_TRUE(Value(std::string("a")) < Value(std::string("b")));
}

TEST(ValueTest, SubtractionAndMultiplyInts) {
  EXPECT_EQ((Value(10) - Value(4)).as_int(), 6);
  EXPECT_EQ((Value(6) * Value(7)).as_int(), 42);
}

TEST(ValueTest, BoolCoercionFromStrings) {
  EXPECT_FALSE(Value(std::string("0")).as_bool());
  EXPECT_FALSE(Value(std::string("false")).as_bool());
  EXPECT_TRUE(Value(std::string("yes")).as_bool());
}

}  // namespace
}  // namespace db
