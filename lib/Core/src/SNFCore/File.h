#pragma once

/**
 * @file File.h
 * @brief Node-based file access helper.
 * @ingroup SNFCore_IO
 */

#include "SNFCore/ByteArray.h"
#include "SNFCore/Node.h"

#include <cstdio>
#include <cstdint>
#include <optional>
#include <string>

namespace snf {

/**
 * @class File
 * @ingroup SNFCore_IO
 * @brief Represents a file associated with a path.
 *
 * On Emscripten builds this class operates on the application's virtual
 * filesystem. It can read files embedded with `--preload-file` or
 * `--embed-file`, and writes only affect virtual filesystem paths.
 */
class File : public Node
{
public:
    enum class OpenMode : unsigned
    {
        NotOpen = 0,
        ReadOnly = 1u << 0u,
        WriteOnly = 1u << 1u,
        ReadWrite = (1u << 0u) | (1u << 1u),
        Append = 1u << 2u,
        Truncate = 1u << 3u
    };

    enum class FileError
    {
        NoError,
        InvalidFilePath,
        OpenFailed,
        NotOpen,
        ReadFailed,
        WriteFailed,
        FlushFailed,
        RemoveFailed,
        SizeFailed,
        PositionFailed
    };

    explicit File(Node* parent = nullptr);
    explicit File(std::string filePath, Node* parent = nullptr);
    ~File() override;

    void setFilePath(std::string filePath);
    const std::string& filePath() const noexcept;

    bool open(OpenMode mode);
    void close();
    bool isOpen() const noexcept;

    bool exists() const;
    std::optional<std::uintmax_t> size() const;

    std::optional<ByteArray> readAll();
    bool readAll(ByteArray& output);
    bool write(const ByteArray& data);
    bool flush();
    bool remove();

    FileError lastError() const noexcept;
    const std::string& errorString() const noexcept;

    static bool exists(const std::string& filePath);
    static std::optional<ByteArray> readAll(const std::string& filePath);
    static bool readAll(const std::string& filePath, ByteArray& output);
    static bool writeAll(const std::string& filePath, const ByteArray& data);
    static bool remove(const std::string& filePath);

protected:
    void update() override;
    void clearError() const;
    void setError(FileError error, std::string errorString) const;

private:
    bool canRead() const noexcept;
    bool canWrite() const noexcept;

private:
    std::string m_filePath;
    std::FILE* m_handle = nullptr;
    OpenMode m_openMode = OpenMode::NotOpen;
    bool m_readable = false;
    bool m_writable = false;
    mutable FileError m_lastError = FileError::NoError;
    mutable std::string m_errorString;
};

constexpr File::OpenMode operator|(File::OpenMode lhs, File::OpenMode rhs) noexcept
{
    return static_cast<File::OpenMode>(static_cast<unsigned>(lhs) | static_cast<unsigned>(rhs));
}

constexpr File::OpenMode operator&(File::OpenMode lhs, File::OpenMode rhs) noexcept
{
    return static_cast<File::OpenMode>(static_cast<unsigned>(lhs) & static_cast<unsigned>(rhs));
}

inline File::OpenMode& operator|=(File::OpenMode& lhs, File::OpenMode rhs) noexcept
{
    lhs = lhs | rhs;
    return lhs;
}

}  // namespace snf
