#include <gtest/gtest.h>

#include <SNFJson/JsonArray.h>
#include <SNFJson/JsonObject.h>
#include <SNFJson/JsonValue.h>

using namespace snf;

TEST(JsonValueTest, StoresPrimitiveTypes)
{
    EXPECT_TRUE(JsonValue().isNull());
    EXPECT_TRUE(JsonValue(nullptr).isNull());
    EXPECT_TRUE(JsonValue(true).isBool());
    EXPECT_TRUE(JsonValue(42).isInteger());
    EXPECT_TRUE(JsonValue(42U).isUnsigned());
    EXPECT_TRUE(JsonValue(3.5).isFloat());
    EXPECT_TRUE(JsonValue("hello").isString());

    EXPECT_TRUE(JsonValue(true).asBool());
    EXPECT_EQ(JsonValue(42).asInt(), 42);
    EXPECT_EQ(JsonValue(42ULL).asUInt64(), 42ULL);
    EXPECT_DOUBLE_EQ(JsonValue(3.5).asDouble(), 3.5);
    EXPECT_EQ(JsonValue("hello").asString(), "hello");
}

TEST(JsonValueTest, WrapsObjectAndArray)
{
    JsonObject object;
    object.set("name", "SNF");
    object.set("enabled", true);

    JsonArray array;
    array.append(1);
    array.append(object);

    const JsonValue objectValue(object);
    const JsonValue arrayValue(array);

    ASSERT_TRUE(objectValue.isObject());
    ASSERT_TRUE(arrayValue.isArray());
    EXPECT_EQ(objectValue.asObject().value("name").asString(), "SNF");
    EXPECT_EQ(arrayValue.asArray().at(1).asObject().value("enabled").asBool(), true);
}

TEST(JsonValueTest, SerializesCompactAndFormattedText)
{
    JsonObject object;
    object.set("name", "demo");
    object.set("count", 2);

    const JsonValue value(object);

    EXPECT_EQ(value.toString(), R"({"count":2,"name":"demo"})");
    EXPECT_NE(value.formatted(2).find("\n  \"count\": 2"), std::string::npos);
}

TEST(JsonValueTest, EqualityComparesJsonContent)
{
    JsonObject first;
    first.set("a", 1);

    JsonObject second;
    second.set("a", 1);

    JsonObject third;
    third.set("a", 2);

    EXPECT_EQ(JsonValue(first), JsonValue(second));
    EXPECT_NE(JsonValue(first), JsonValue(third));
}
