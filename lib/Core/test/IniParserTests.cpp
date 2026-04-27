#include <gtest/gtest.h>

#include <SNFCore/IniParser.h>

#include <chrono>
#include <filesystem>
#include <fstream>

using namespace snf;

namespace {

std::filesystem::path makeTemporaryIniPath()
{
    const auto uniqueId = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           ("snf_ini_parser_test_" + std::to_string(uniqueId) + ".ini");
}

class IniParserTest : public ::testing::Test
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
        auto path = makeTemporaryIniPath();
        m_pathsToDelete.push_back(path);
        return path;
    }

private:
    std::vector<std::filesystem::path> m_pathsToDelete;
};

}  // namespace

TEST_F(IniParserTest, LoadFromStringParsesGlobalKeyValue)
{
    IniParser parser;

    ASSERT_TRUE(parser.loadFromString("host = localhost\nport = 8080\n"));
    EXPECT_EQ(parser.value("host"), "localhost");
    EXPECT_EQ(parser.value("port"), "8080");
    EXPECT_TRUE(parser.contains("host"));
    EXPECT_EQ(parser.errorText(), "");
}

TEST_F(IniParserTest, LoadFromStringParsesSectionsAndGlobalValues)
{
    IniParser parser;

    ASSERT_TRUE(parser.loadFromString(
        "app = demo\n"
        "[database]\n"
        "host = db.local\n"
        "port = 5432\n"
        "[logging]\n"
        "level = debug\n"));

    EXPECT_EQ(parser.value("app"), "demo");
    EXPECT_EQ(parser.value("database", "host"), "db.local");
    EXPECT_EQ(parser.value("database", "port"), "5432");
    EXPECT_EQ(parser.value("logging", "level"), "debug");

    const auto sections = parser.sections();
    ASSERT_EQ(sections.size(), 2);
    EXPECT_EQ(sections[0], "database");
    EXPECT_EQ(sections[1], "logging");
}

TEST_F(IniParserTest, LoadFromStringIgnoresCommentsAndWhitespace)
{
    IniParser parser;

    ASSERT_TRUE(parser.loadFromString(
        "  # comment\n"
        " ; another comment\n"
        "[network]\n"
        " host = 127.0.0.1 \n"
        "port= 9000\n"));

    EXPECT_EQ(parser.value("network", "host"), "127.0.0.1");
    EXPECT_EQ(parser.value("network", "port"), "9000");
}

TEST_F(IniParserTest, LoadFromStringStripsInlineComments)
{
    IniParser parser;

    ASSERT_TRUE(parser.loadFromString(
        "host = localhost ; primary host\n"
        "port = 8080 # admin port\n"
        "path = /tmp/cache#1\n"));

    EXPECT_EQ(parser.value("host"), "localhost");
    EXPECT_EQ(parser.value("port"), "8080");
    EXPECT_EQ(parser.value("path"), "/tmp/cache#1");
}

TEST_F(IniParserTest, ValueReturnsDefaultForMissingEntries)
{
    IniParser parser;

    ASSERT_TRUE(parser.loadFromString("[core]\nthreads = 4\n"));
    EXPECT_EQ(parser.valueOrDefault("missing", "fallback"), "fallback");
    EXPECT_EQ(parser.value("core", "missing", "fallback"), "fallback");
    EXPECT_FALSE(parser.contains("core", "missing"));
    EXPECT_FALSE(parser.contains("missing"));
}

TEST_F(IniParserTest, KeysReturnsSectionKeys)
{
    IniParser parser;

    ASSERT_TRUE(parser.loadFromString(
        "global = yes\n"
        "[server]\n"
        "host = localhost\n"
        "port = 8080\n"));

    const auto globalKeys = parser.keys();
    ASSERT_EQ(globalKeys.size(), 1);
    EXPECT_EQ(globalKeys[0], "global");

    const auto sectionKeys = parser.keys("server");
    ASSERT_EQ(sectionKeys.size(), 2);
    EXPECT_EQ(sectionKeys[0], "host");
    EXPECT_EQ(sectionKeys[1], "port");
}

TEST_F(IniParserTest, KeysPreserveInsertionOrder)
{
    IniParser parser;

    ASSERT_TRUE(parser.loadFromString(
        "third = c\n"
        "first = a\n"
        "second = b\n"
        "[server]\n"
        "port = 8080\n"
        "host = localhost\n"));

    const auto globalKeys = parser.keys();
    ASSERT_EQ(globalKeys.size(), 3);
    EXPECT_EQ(globalKeys[0], "third");
    EXPECT_EQ(globalKeys[1], "first");
    EXPECT_EQ(globalKeys[2], "second");

    const auto sectionKeys = parser.keys("server");
    ASSERT_EQ(sectionKeys.size(), 2);
    EXPECT_EQ(sectionKeys[0], "port");
    EXPECT_EQ(sectionKeys[1], "host");
}

TEST_F(IniParserTest, SetValueCreatesAndOverwritesEntries)
{
    IniParser parser;

    parser.setValue("mode", "dev");
    parser.setValue("database", "host", "localhost");
    parser.setValue("database", "host", "db.internal");

    EXPECT_EQ(parser.value("mode"), "dev");
    EXPECT_EQ(parser.value("database", "host"), "db.internal");
    EXPECT_TRUE(parser.hasSection("database"));
}

TEST_F(IniParserTest, RemoveDeletesKeysAndSections)
{
    IniParser parser;

    ASSERT_TRUE(parser.loadFromString(
        "global = yes\n"
        "[database]\n"
        "host = localhost\n"
        "port = 5432\n"));

    EXPECT_TRUE(parser.remove("global"));
    EXPECT_FALSE(parser.contains("global"));
    EXPECT_TRUE(parser.remove("database", "host"));
    EXPECT_FALSE(parser.contains("database", "host"));
    EXPECT_TRUE(parser.removeSection("database"));
    EXPECT_FALSE(parser.hasSection("database"));
    EXPECT_FALSE(parser.removeSection("database"));
}

TEST_F(IniParserTest, ToStringRoundTripsParsedContent)
{
    IniParser parser;
    IniParser reparsed;

    ASSERT_TRUE(parser.loadFromString(
        "name = service\n"
        "[network]\n"
        "host = 10.0.0.1\n"
        "port = 4040\n"
        "[logging]\n"
        "level = info\n"));

    const std::string serialized = parser.toString();
    ASSERT_TRUE(reparsed.loadFromString(serialized));

    EXPECT_EQ(reparsed.value("name"), "service");
    EXPECT_EQ(reparsed.value("network", "host"), "10.0.0.1");
    EXPECT_EQ(reparsed.value("network", "port"), "4040");
    EXPECT_EQ(reparsed.value("logging", "level"), "info");
}

TEST_F(IniParserTest, ToStringPreservesKeyInsertionOrder)
{
    IniParser parser;

    parser.setValue("second", "2");
    parser.setValue("first", "1");
    parser.setValue("service", "port", "8080");
    parser.setValue("service", "host", "localhost");

    EXPECT_EQ(parser.toString(),
              "second = 2\n"
              "first = 1\n"
              "\n"
              "[service]\n"
              "port = 8080\n"
              "host = localhost\n");
}

TEST_F(IniParserTest, TypedValueHelpersParseSupportedTypes)
{
    IniParser parser;

    ASSERT_TRUE(parser.loadFromString(
        "workers = 8\n"
        "enabled = true\n"
        "ratio = 0.25\n"
        "[flags]\n"
        "tls = on\n"
        "threshold = 2.5\n"));

    EXPECT_EQ(parser.intValue("workers", -1), 8);
    EXPECT_TRUE(parser.boolValue("enabled", false));
    EXPECT_DOUBLE_EQ(parser.doubleValue("ratio", -1.0), 0.25);
    EXPECT_TRUE(parser.boolValue("flags", "tls", false));
    EXPECT_DOUBLE_EQ(parser.doubleValue("flags", "threshold", -1.0), 2.5);
}

TEST_F(IniParserTest, TypedValueHelpersReturnDefaultsForInvalidValues)
{
    IniParser parser;

    ASSERT_TRUE(parser.loadFromString(
        "workers = many\n"
        "enabled = maybe\n"
        "ratio = pi\n"));

    EXPECT_EQ(parser.intValue("workers", 3), 3);
    EXPECT_FALSE(parser.boolValue("enabled", false));
    EXPECT_DOUBLE_EQ(parser.doubleValue("ratio", 1.5), 1.5);
    EXPECT_TRUE(parser.boolValue("missing", true));
}

TEST_F(IniParserTest, LoadFromFileReadsValues)
{
    const auto path = newTempPath();
    std::ofstream output(path);
    output << "[service]\n"
              "name = worker\n"
              "threads = 8\n";
    output.close();

    IniParser parser;
    ASSERT_TRUE(parser.load(path.string()));
    EXPECT_EQ(parser.value("service", "name"), "worker");
    EXPECT_EQ(parser.value("service", "threads"), "8");
}

TEST_F(IniParserTest, LoadFromMissingFileReturnsError)
{
    IniParser parser;
    const auto path = newTempPath();

    ASSERT_FALSE(parser.load(path.string()));
    EXPECT_FALSE(parser.errorText().empty());
}

TEST_F(IniParserTest, SaveWritesFileThatCanBeReloaded)
{
    IniParser parser;
    IniParser reloaded;
    const auto path = newTempPath();

    parser.setValue("version", "1");
    parser.setValue("database", "host", "localhost");
    parser.setValue("database", "port", "3306");

    ASSERT_TRUE(parser.save(path.string()));
    ASSERT_TRUE(reloaded.load(path.string()));

    EXPECT_EQ(reloaded.value("version"), "1");
    EXPECT_EQ(reloaded.value("database", "host"), "localhost");
    EXPECT_EQ(reloaded.value("database", "port"), "3306");
}

TEST_F(IniParserTest, SaveWithoutPathFailsAfterLoadFromString)
{
    IniParser parser;

    ASSERT_TRUE(parser.loadFromString("name = demo\n"));
    ASSERT_FALSE(parser.save());
    EXPECT_EQ(parser.errorText(), "No INI file path available for save()");
}

TEST_F(IniParserTest, InvalidLineReturnsFalseAndSetsErrorText)
{
    IniParser parser;

    ASSERT_FALSE(parser.loadFromString(
        "[core]\n"
        "threads = 4\n"
        "this is invalid\n"));
    EXPECT_FALSE(parser.errorText().empty());
}

TEST_F(IniParserTest, LoadFromFileWithInvalidContentFails)
{
    const auto path = newTempPath();
    std::ofstream output(path);
    output << "[broken\n"
              "key = value\n";
    output.close();

    IniParser parser;
    ASSERT_FALSE(parser.load(path.string()));
    EXPECT_FALSE(parser.errorText().empty());
}

TEST_F(IniParserTest, LoadFromStringHandlesCRLF)
{
    IniParser parser;

    ASSERT_TRUE(parser.loadFromString("host = localhost\r\n[net]\r\nport = 9000\r\n"));
    EXPECT_EQ(parser.value("host"), "localhost");
    EXPECT_EQ(parser.value("net", "port"), "9000");
}

TEST_F(IniParserTest, LoadFromStringInvalidSectionHeaderSetsError)
{
    IniParser parser;

    ASSERT_FALSE(parser.loadFromString("[invalid\nkey = value\n"));
    EXPECT_FALSE(parser.errorText().empty());
}

TEST_F(IniParserTest, LoadFromStringEmptySectionNameSetsError)
{
    IniParser parser;

    ASSERT_FALSE(parser.loadFromString("[   ]\nkey = value\n"));
    EXPECT_FALSE(parser.errorText().empty());
}

TEST_F(IniParserTest, LoadFromStringEmptyKeySetsError)
{
    IniParser parser;

    ASSERT_FALSE(parser.loadFromString("= value\n"));
    EXPECT_FALSE(parser.errorText().empty());
}

TEST_F(IniParserTest, SaveWithoutPathSucceedsAfterLoadFromFile)
{
    const auto path = newTempPath();
    std::ofstream output(path);
    output << "name = original\n";
    output.close();

    IniParser parser;
    ASSERT_TRUE(parser.load(path.string()));

    parser.setValue("name", "updated");
    ASSERT_TRUE(parser.save());

    IniParser reloaded;
    ASSERT_TRUE(reloaded.load(path.string()));
    EXPECT_EQ(reloaded.value("name"), "updated");
}

TEST_F(IniParserTest, SaveToDirectoryPathFails)
{
    IniParser parser;
    parser.setValue("name", "demo");

    ASSERT_FALSE(parser.save(std::filesystem::temp_directory_path().string()));
    EXPECT_FALSE(parser.errorText().empty());
}

TEST_F(IniParserTest, SaveToDevFullFailsDuringWrite)
{
    IniParser parser;
    parser.setValue("name", "demo");

    ASSERT_FALSE(parser.save("/dev/full"));
    EXPECT_FALSE(parser.errorText().empty());
}

TEST_F(IniParserTest, BoolValueParsesOffAsFalse)
{
    IniParser parser;

    ASSERT_TRUE(parser.loadFromString("enabled = off\n"));
    EXPECT_FALSE(parser.boolValue("enabled", true));
}

TEST_F(IniParserTest, KeysForMissingSectionReturnsEmpty)
{
    IniParser parser;
    parser.setValue("present", "yes");

    const auto keys = parser.keys("missing");
    EXPECT_TRUE(keys.empty());
}

TEST_F(IniParserTest, RemoveReturnsFalseForMissingSectionAndKey)
{
    IniParser parser;
    parser.setValue("database", "host", "localhost");

    EXPECT_FALSE(parser.remove("missing", "host"));
    EXPECT_FALSE(parser.remove("database", "missing"));
}

TEST_F(IniParserTest, RemoveLastKeyRemovesSection)
{
    IniParser parser;
    parser.setValue("database", "host", "localhost");

    ASSERT_TRUE(parser.remove("database", "host"));
    EXPECT_FALSE(parser.hasSection("database"));
}

TEST_F(IniParserTest, RemoveSectionWithEmptyNameReturnsFalse)
{
    IniParser parser;
    EXPECT_FALSE(parser.removeSection(""));
}