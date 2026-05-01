#include <gtest/gtest.h>

#include <SNFJson/JsonDocument.h>

#include <chrono>
#include <filesystem>
#include <fstream>

using namespace snf;

namespace {

std::filesystem::path makeTemporaryJsonPath()
{
    const auto uniqueId = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           ("snf_json_document_test_" + std::to_string(uniqueId) + ".json");
}

class JsonDocumentTest : public ::testing::Test
{
protected:
    void TearDown() override
    {
        for (const auto& path : m_pathsToDelete) {
            std::error_code errorCode;
            std::filesystem::remove(path, errorCode);
        }
    }

    std::filesystem::path newTempPath()
    {
        auto path = makeTemporaryJsonPath();
        m_pathsToDelete.push_back(path);
        return path;
    }

private:
    std::vector<std::filesystem::path> m_pathsToDelete;
};

}  // namespace

TEST_F(JsonDocumentTest, ParseReadsObjectDocument)
{
    const auto document = JsonDocument::parse(R"({"name":"demo","items":[1,2,3]})");

    ASSERT_TRUE(document.isObject());
    const JsonObject root = document.object();
    EXPECT_EQ(root.value("name").asString(), "demo");
    EXPECT_EQ(root.value("items").asArray().at(2).asInt(), 3);
}

TEST_F(JsonDocumentTest, ParseReportsJsonParseErrorWithPosition)
{
    try {
        (void)JsonDocument::parse("{\n  \"name\": \n}");
        FAIL() << "Expected JsonParseError";
    } catch (const JsonParseError& error) {
        EXPECT_GT(error.byte(), 0U);
        EXPECT_GE(error.line(), 2U);
        EXPECT_GT(error.column(), 0U);
        EXPECT_NE(std::string(error.what()).find("JSON parse error"), std::string::npos);
    }
}

TEST_F(JsonDocumentTest, LoadFromStringReturnsFalseAndStoresErrorText)
{
    JsonDocument document(JsonObject{{"keep", true}});

    ASSERT_FALSE(document.loadFromString(R"({"broken": )"));
    EXPECT_FALSE(document.errorText().empty());
    EXPECT_TRUE(document.object().value("keep").asBool());
}

TEST_F(JsonDocumentTest, FormatsDocumentForReadableOutput)
{
    JsonObject root;
    root.set("name", "demo");
    root.set("items", JsonArray{1, 2});

    const JsonDocument document(root);
    const std::string formatted = document.formatted(2);

    EXPECT_NE(formatted.find('\n'), std::string::npos);
    EXPECT_NE(formatted.find("  \"items\""), std::string::npos);
    EXPECT_EQ(document.toString(), R"({"items":[1,2],"name":"demo"})");
}

TEST_F(JsonDocumentTest, SavesAndLoadsFile)
{
    JsonObject root;
    root.set("name", "from-file");
    root.set("enabled", true);

    const auto path = newTempPath();
    const JsonDocument saved(root);

    ASSERT_TRUE(saved.save(path.string(), 2));

    const JsonDocument loaded = JsonDocument::fromFile(path.string());
    ASSERT_TRUE(loaded.isObject());
    EXPECT_EQ(loaded.object().value("name").asString(), "from-file");
    EXPECT_TRUE(loaded.object().value("enabled").asBool());
}

TEST_F(JsonDocumentTest, LoadReturnsFalseForMissingFile)
{
    JsonDocument document;
    const auto path = newTempPath();

    ASSERT_FALSE(document.load(path.string()));
    EXPECT_NE(document.errorText().find("Failed to open JSON file"), std::string::npos);
}
