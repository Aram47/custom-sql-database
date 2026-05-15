#include "types/type_converter.h"

#include "gtest/gtest.h"
#include "types/data_type.h"
#include "utils/exceptions.h"

namespace db {
namespace {

TEST(TypeConverterTest, StringToValueBasics) {
  EXPECT_TRUE(TypeConverter::string_to_value("", DataType::INT).is_null());
  EXPECT_TRUE(TypeConverter::string_to_value("NULL", DataType::INT).is_null());

  EXPECT_EQ(TypeConverter::string_to_value("42", DataType::INT).as_int(), 42);
  EXPECT_NEAR(TypeConverter::string_to_value("-1.5", DataType::FLOAT).as_float(),
              -1.5, 1e-9);
  EXPECT_EQ(TypeConverter::string_to_value("  hello ", DataType::STRING).as_string(),
            "hello");
  EXPECT_TRUE(TypeConverter::string_to_value("TRUE", DataType::BOOLEAN).as_bool());
  EXPECT_FALSE(TypeConverter::string_to_value("false", DataType::BOOLEAN).as_bool());
}

TEST(TypeConverterTest, DateValidAndInvalid) {
  Value v = TypeConverter::string_to_value("2024-03-15", DataType::DATE);
  EXPECT_TRUE(v.is_string());
  EXPECT_EQ(v.as_string(), "2024-03-15");

  EXPECT_THROW(static_cast<void>(TypeConverter::string_to_value(
                   "15-03-2024", DataType::DATE)),
               TypeException);
  EXPECT_THROW(
      static_cast<void>(TypeConverter::string_to_value("2024-13-01", DataType::DATE)),
      TypeException);
}

TEST(TypeConverterTest, UuidValidAndInvalid) {
  const char *valid = "550e8400-e29b-41d4-a716-446655440000";
  Value v = TypeConverter::string_to_value(valid, DataType::UUID);
  EXPECT_TRUE(v.is_string());
  EXPECT_EQ(v.as_string(), valid);

  EXPECT_THROW(static_cast<void>(TypeConverter::string_to_value(
                   "not-a-uuid", DataType::UUID)),
               TypeException);
}

TEST(TypeConverterTest, SerializeDeserializeRoundTrip) {
  auto check = [](const Value &v, DataType t) {
    auto bytes = TypeConverter::serialize_value(v);
    Value back = TypeConverter::deserialize_value(bytes, t);
    EXPECT_TRUE(back == v) << v.to_string() << " vs " << back.to_string();
  };

  check(Value(), DataType::INT);
  check(Value(int64_t{-99}), DataType::INT);
  check(Value(2.25), DataType::FLOAT);
  check(Value(std::string("abc")), DataType::STRING);
  check(Value(true), DataType::BOOLEAN);
}

TEST(TypeConverterTest, DeserializeInvalidThrows) {
  EXPECT_THROW(static_cast<void>(TypeConverter::deserialize_value({0xAB}, DataType::INT)),
               TypeException);
}

TEST(TypeConverterTest, IsValidValue) {
  EXPECT_TRUE(TypeConverter::is_valid_value("10", DataType::INT));
  EXPECT_FALSE(TypeConverter::is_valid_value("x", DataType::INT));
  EXPECT_TRUE(TypeConverter::is_valid_value("2020-01-01", DataType::DATE));
}

}  // namespace
}  // namespace db
