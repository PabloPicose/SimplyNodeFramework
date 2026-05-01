#include <gtest/gtest.h>

#include <SNFJson/JsonObject.h>

#include <algorithm>

using namespace snf;

TEST(JsonObjectTest, SupportsSetGetContainsAndRemove)
{
    JsonObject object;

    EXPECT_TRUE(object.empty());

    object.set("host", "localhost");
    object.set("port", 8080);
    object.set("secure", true);

    EXPECT_EQ(object.size(), 3U);
    EXPECT_TRUE(object.contains("host"));
    EXPECT_EQ(object.value("host").asString(), "localhost");
    EXPECT_EQ(object["port"].asInt(), 8080);
    EXPECT_TRUE(object.value("secure").asBool());

    EXPECT_TRUE(object.remove("secure"));
    EXPECT_FALSE(object.contains("secure"));
    EXPECT_FALSE(object.remove("secure"));
}

TEST(JsonObjectTest, MissingValueReturnsNullOrFallback)
{
    JsonObject object;
    object.set("name", "demo");

    EXPECT_TRUE(object.value("missing").isNull());
    EXPECT_EQ(object.valueOr("missing", "fallback").asString(), "fallback");
    EXPECT_EQ(object.valueOr("name", "fallback").asString(), "demo");
}

TEST(JsonObjectTest, KeysExposeObjectMembers)
{
    JsonObject object;
    object.set("alpha", 1);
    object.set("beta", 2);

    const auto keys = object.keys();

    EXPECT_EQ(keys.size(), 2U);
    EXPECT_NE(std::find(keys.begin(), keys.end(), "alpha"), keys.end());
    EXPECT_NE(std::find(keys.begin(), keys.end(), "beta"), keys.end());
}

TEST(JsonObjectTest, InitializerListCreatesObject)
{
    const JsonObject object{
        {"name", "service"},
        {"retries", 3},
    };

    EXPECT_EQ(object.value("name").asString(), "service");
    EXPECT_EQ(object.value("retries").asInt(), 3);
}

TEST(JsonObjectTest, CopiesDoNotShareStorage)
{
    JsonObject original;
    original.set("name", "original");

    JsonObject copy = original;
    copy.set("name", "copy");

    EXPECT_EQ(original.value("name").asString(), "original");
    EXPECT_EQ(copy.value("name").asString(), "copy");
}
