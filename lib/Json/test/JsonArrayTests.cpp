#include <gtest/gtest.h>

#include <SNFJson/JsonArray.h>
#include <SNFJson/JsonObject.h>

using namespace snf;

TEST(JsonArrayTest, SupportsAppendValueAtAndRemove)
{
    JsonArray array;
    array.append("first");
    array.append(2);
    array.append(true);

    ASSERT_EQ(array.size(), 3U);
    EXPECT_EQ(array.at(0).asString(), "first");
    EXPECT_EQ(array.value(1).asInt(), 2);
    EXPECT_TRUE(array.value(2).asBool());

    EXPECT_TRUE(array.remove(1));
    ASSERT_EQ(array.size(), 2U);
    EXPECT_TRUE(array.at(1).asBool());
    EXPECT_FALSE(array.remove(99));
}

TEST(JsonArrayTest, OutOfRangeValueReturnsNullButAtThrows)
{
    JsonArray array;
    array.append(1);

    EXPECT_TRUE(array.value(10).isNull());
    EXPECT_THROW(array.at(10), std::out_of_range);
}

TEST(JsonArrayTest, ValuesReturnsWrappedElements)
{
    JsonObject object;
    object.set("name", "item");

    JsonArray array;
    array.append(1);
    array.append(object);

    const auto values = array.values();

    ASSERT_EQ(values.size(), 2U);
    EXPECT_EQ(values[0].asInt(), 1);
    EXPECT_EQ(values[1].asObject().value("name").asString(), "item");
}

TEST(JsonArrayTest, InitializerListCreatesArray)
{
    const JsonArray array{1, "two", false};

    ASSERT_EQ(array.size(), 3U);
    EXPECT_EQ(array.at(0).asInt(), 1);
    EXPECT_EQ(array.at(1).asString(), "two");
    EXPECT_FALSE(array.at(2).asBool());
}

TEST(JsonArrayTest, CopiesDoNotShareStorage)
{
    JsonArray original;
    original.append("original");

    JsonArray copy = original;
    copy.append("copy");

    ASSERT_EQ(original.size(), 1U);
    ASSERT_EQ(copy.size(), 2U);
    EXPECT_EQ(original.at(0).asString(), "original");
    EXPECT_EQ(copy.at(1).asString(), "copy");
}
