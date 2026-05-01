#include <gtest/gtest.h>

#include "SNFCore/Application.h"
#include "SNFCore/TempFile.h"

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

using namespace snf;

namespace {

class TempFileFixture : public ::testing::Test
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

TEST_F(TempFileFixture, OpenCreatesValidTemporaryFile)
{
    TempFile file;

    ASSERT_TRUE(file.open()) << file.errorString();
    EXPECT_TRUE(file.isOpen());
    EXPECT_FALSE(file.filePath().empty());
    EXPECT_TRUE(File::exists(file.filePath()));
}

TEST_F(TempFileFixture, WriteAndReadWork)
{
    TempFile file;

    ASSERT_TRUE(file.open()) << file.errorString();
    ASSERT_TRUE(file.write(ByteArray(std::string("temporary")))) << file.errorString();
    ASSERT_TRUE(file.flush()) << file.errorString();

    const auto content = file.readAll();

    ASSERT_TRUE(content.has_value()) << file.errorString();
    EXPECT_EQ(std::string(reinterpret_cast<const char*>(content->data()), content->size()), "temporary");
}

TEST_F(TempFileFixture, FileExistsWhileObjectIsAlive)
{
    TempFile file;
    ASSERT_TRUE(file.open()) << file.errorString();

    EXPECT_TRUE(File::exists(file.filePath()));
}

TEST_F(TempFileFixture, AutoRemoveDeletesFileOnDestruction)
{
    std::string path;
    {
        TempFile file;
        ASSERT_TRUE(file.open()) << file.errorString();
        path = file.filePath();
        ASSERT_TRUE(File::exists(path));
    }

    EXPECT_FALSE(File::exists(path));
}

TEST_F(TempFileFixture, AutoRemoveFalseKeepsFileOnDestruction)
{
    std::string path;
    {
        TempFile file;
        file.setAutoRemove(false);
        ASSERT_TRUE(file.open()) << file.errorString();
        path = file.filePath();
        pathsToDelete.push_back(path);
        ASSERT_TRUE(File::exists(path));
    }

    EXPECT_TRUE(File::exists(path));
}

TEST_F(TempFileFixture, TwoOpenTempFilesHaveDistinctPaths)
{
    TempFile first;
    TempFile second;

    ASSERT_TRUE(first.open()) << first.errorString();
    ASSERT_TRUE(second.open()) << second.errorString();

    EXPECT_NE(first.filePath(), second.filePath());
}

TEST_F(TempFileFixture, CloseKeepsFilePath)
{
    TempFile file;
    ASSERT_TRUE(file.open()) << file.errorString();
    const std::string path = file.filePath();

    file.close();

    EXPECT_FALSE(file.isOpen());
    EXPECT_EQ(file.filePath(), path);
}

TEST_F(TempFileFixture, BinaryByteArrayRoundTrips)
{
    const std::vector<std::uint8_t> expected{0x00, 0x7f, 0x80, 0x00, 0xff};

    TempFile file;
    ASSERT_TRUE(file.open()) << file.errorString();
    ASSERT_TRUE(file.write(ByteArray(expected))) << file.errorString();
    ASSERT_TRUE(file.flush()) << file.errorString();

    const auto content = file.readAll();

    ASSERT_TRUE(content.has_value()) << file.errorString();
    EXPECT_EQ(toUInt8Vector(*content), expected);
}
