#include <gtest/gtest.h>

#include "SNFCore/Application.h"
#include "SNFCore/File.h"

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

using namespace snf;

namespace {

class FileFixture : public ::testing::Test
{
public:
    void SetUp() override { app = new Application(0, nullptr); }

    void TearDown() override
    {
        for (const auto& path : pathsToDelete) {
            std::error_code errorCode;
            std::filesystem::remove(path, errorCode);
        }
        delete app;
        app = nullptr;
    }

    std::filesystem::path newTempPath()
    {
        const auto uniqueId = std::chrono::steady_clock::now().time_since_epoch().count();
        auto path = std::filesystem::temp_directory_path() /
                    ("snf_file_test_" + std::to_string(uniqueId) + "_" +
                     std::to_string(pathsToDelete.size()) + ".bin");
        pathsToDelete.push_back(path);
        return path;
    }

    Application* app = nullptr;
    std::vector<std::filesystem::path> pathsToDelete;
};

std::vector<std::uint8_t> toUInt8Vector(const ByteArray& bytes)
{
    std::vector<std::uint8_t> result;
    result.reserve(bytes.size());
    for (const std::byte byte : bytes.bytes()) {
        result.push_back(static_cast<std::uint8_t>(byte));
    }
    return result;
}

}  // namespace

TEST_F(FileFixture, CreatesWritesAndReadsFileWithByteArray)
{
    const auto path = newTempPath();
    File file(path.string());

    ASSERT_TRUE(file.open(File::OpenMode::WriteOnly | File::OpenMode::Truncate)) << file.errorString();
    EXPECT_TRUE(file.write(ByteArray(std::string("hello")))) << file.errorString();
    EXPECT_TRUE(file.flush()) << file.errorString();
    file.close();

    ASSERT_TRUE(file.open(File::OpenMode::ReadOnly)) << file.errorString();
    const auto content = file.readAll();

    ASSERT_TRUE(content.has_value()) << file.errorString();
    EXPECT_EQ(std::string(reinterpret_cast<const char*>(content->data()), content->size()), "hello");
}

TEST_F(FileFixture, EmptyFileReadIsDistinctFromError)
{
    const auto path = newTempPath();
    ASSERT_TRUE(File::writeAll(path.string(), ByteArray()));

    File file(path.string());
    ASSERT_TRUE(file.open(File::OpenMode::ReadOnly)) << file.errorString();

    const auto content = file.readAll();

    ASSERT_TRUE(content.has_value()) << file.errorString();
    EXPECT_TRUE(content->empty());
    EXPECT_EQ(file.lastError(), File::FileError::NoError);
}

TEST_F(FileFixture, ExistsBeforeAndAfterCreate)
{
    const auto path = newTempPath();
    File file(path.string());

    EXPECT_FALSE(file.exists());
    ASSERT_TRUE(File::writeAll(path.string(), ByteArray(std::string("data"))));
    EXPECT_TRUE(file.exists());
    EXPECT_TRUE(File::exists(path.string()));
}

TEST_F(FileFixture, SizeReportsFileLength)
{
    const auto path = newTempPath();
    ASSERT_TRUE(File::writeAll(path.string(), ByteArray(std::string("abcdef"))));

    File file(path.string());
    const auto size = file.size();

    ASSERT_TRUE(size.has_value()) << file.errorString();
    EXPECT_EQ(*size, 6U);
}

TEST_F(FileFixture, AppendAddsBytesAtEnd)
{
    const auto path = newTempPath();
    ASSERT_TRUE(File::writeAll(path.string(), ByteArray(std::string("one"))));

    File file(path.string());
    ASSERT_TRUE(file.open(File::OpenMode::Append)) << file.errorString();
    ASSERT_TRUE(file.write(ByteArray(std::string("two")))) << file.errorString();
    ASSERT_TRUE(file.flush()) << file.errorString();
    file.close();

    const auto content = File::readAll(path.string());
    ASSERT_TRUE(content.has_value());
    EXPECT_EQ(std::string(reinterpret_cast<const char*>(content->data()), content->size()), "onetwo");
}

TEST_F(FileFixture, RemoveEliminatesFile)
{
    const auto path = newTempPath();
    ASSERT_TRUE(File::writeAll(path.string(), ByteArray(std::string("data"))));

    File file(path.string());
    ASSERT_TRUE(file.exists());
    EXPECT_TRUE(file.remove()) << file.errorString();
    EXPECT_FALSE(File::exists(path.string()));
}

TEST_F(FileFixture, OpenReadOnlyFailsForMissingPath)
{
    const auto path = newTempPath();
    File file(path.string());

    EXPECT_FALSE(file.open(File::OpenMode::ReadOnly));
    EXPECT_EQ(file.lastError(), File::FileError::OpenFailed);
    EXPECT_FALSE(file.errorString().empty());
}

TEST_F(FileFixture, StaticWriteAllAndReadAllWork)
{
    const auto path = newTempPath();

    ASSERT_TRUE(File::writeAll(path.string(), ByteArray(std::string("static"))));
    const auto content = File::readAll(path.string());

    ASSERT_TRUE(content.has_value());
    EXPECT_EQ(std::string(reinterpret_cast<const char*>(content->data()), content->size()), "static");
}

TEST_F(FileFixture, ReadsBinaryDataWithEmbeddedNullBytes)
{
    const auto path = newTempPath();
    const std::vector<std::uint8_t> expected{0x41, 0x00, 0x42, 0xff, 0x00, 0x43};

    ASSERT_TRUE(File::writeAll(path.string(), ByteArray(expected)));
    const auto content = File::readAll(path.string());

    ASSERT_TRUE(content.has_value());
    EXPECT_EQ(toUInt8Vector(*content), expected);
}
