#include "SNFCore/File.h"

#include <cerrno>
#include <cstring>
#include <sys/stat.h>
#include <utility>
#include <vector>

namespace snf {

namespace {

bool hasMode(File::OpenMode modes, File::OpenMode mode) noexcept
{
    return static_cast<unsigned>(modes & mode) != 0u;
}

std::string systemErrorString(const std::string& prefix, const std::string& filePath)
{
    std::string error = prefix;
    if (! filePath.empty()) {
        error += ": " + filePath;
    }
    if (errno != 0) {
        error += " (";
        error += std::strerror(errno);
        error += ")";
    }
    return error;
}

const char* modeString(File::OpenMode mode, bool& readable, bool& writable)
{
    const bool append = hasMode(mode, File::OpenMode::Append);
    const bool truncate = hasMode(mode, File::OpenMode::Truncate);
    bool read = hasMode(mode, File::OpenMode::ReadOnly);
    bool write = hasMode(mode, File::OpenMode::WriteOnly);

    if (append || truncate) {
        write = true;
    }

    readable = read;
    writable = write;

    if (append) {
        return read ? "a+b" : "ab";
    }

    if (truncate) {
        return read ? "w+b" : "wb";
    }

    if (read && write) {
        return "r+b";
    }

    if (write) {
        return "wb";
    }

    if (read) {
        return "rb";
    }

    return nullptr;
}

std::optional<std::uintmax_t> sizeForPath(const std::string& filePath)
{
    struct stat status
    {
    };
    if (::stat(filePath.c_str(), &status) != 0) {
        return std::nullopt;
    }

    return static_cast<std::uintmax_t>(status.st_size);
}

std::optional<ByteArray> readAllFromHandle(std::FILE* handle, bool flushBeforeRead)
{
    if (flushBeforeRead && std::fflush(handle) != 0) {
        return std::nullopt;
    }

    const long originalPosition = std::ftell(handle);
    if (originalPosition < 0) {
        return std::nullopt;
    }

    if (std::fseek(handle, 0, SEEK_END) != 0) {
        return std::nullopt;
    }

    const long endPosition = std::ftell(handle);
    if (endPosition < 0) {
        return std::nullopt;
    }

    if (std::fseek(handle, 0, SEEK_SET) != 0) {
        return std::nullopt;
    }

    ByteArray::Storage storage(static_cast<std::size_t>(endPosition));
    if (! storage.empty()) {
        const std::size_t read = std::fread(storage.data(), 1, storage.size(), handle);
        if (read != storage.size()) {
            return std::nullopt;
        }
    }

    if (std::fseek(handle, originalPosition, SEEK_SET) != 0) {
        return std::nullopt;
    }

    return ByteArray(std::move(storage));
}

}  // namespace

File::File(Node* parent)
    : Node(parent)
{
}

File::File(std::string filePath, Node* parent)
    : Node(parent)
    , m_filePath(std::move(filePath))
{
}

File::~File()
{
    close();
}

void File::setFilePath(std::string filePath)
{
    if (isOpen()) {
        close();
    }

    m_filePath = std::move(filePath);
    clearError();
}

const std::string& File::filePath() const noexcept
{
    return m_filePath;
}

bool File::open(OpenMode mode)
{
    if (isOpen()) {
        close();
    }

    if (m_filePath.empty()) {
        setError(FileError::InvalidFilePath, "File path is empty");
        return false;
    }

    bool readable = false;
    bool writable = false;
    const char* modeText = modeString(mode, readable, writable);
    if (modeText == nullptr) {
        setError(FileError::OpenFailed, "Invalid file open mode");
        return false;
    }

    errno = 0;
    m_handle = std::fopen(m_filePath.c_str(), modeText);
    if (m_handle == nullptr) {
        setError(FileError::OpenFailed, systemErrorString("Failed to open file", m_filePath));
        m_openMode = OpenMode::NotOpen;
        m_readable = false;
        m_writable = false;
        return false;
    }

    m_openMode = mode;
    m_readable = readable;
    m_writable = writable;
    clearError();
    return true;
}

void File::close()
{
    if (m_handle != nullptr) {
        std::fclose(m_handle);
        m_handle = nullptr;
    }

    m_openMode = OpenMode::NotOpen;
    m_readable = false;
    m_writable = false;
}

bool File::isOpen() const noexcept
{
    return m_handle != nullptr;
}

bool File::exists() const
{
    return exists(m_filePath);
}

std::optional<std::uintmax_t> File::size() const
{
    if (m_filePath.empty()) {
        setError(FileError::InvalidFilePath, "File path is empty");
        return std::nullopt;
    }

    errno = 0;
    auto result = sizeForPath(m_filePath);
    if (! result.has_value()) {
        setError(FileError::SizeFailed, systemErrorString("Failed to read file size", m_filePath));
        return std::nullopt;
    }

    clearError();
    return result;
}

std::optional<ByteArray> File::readAll()
{
    if (! isOpen()) {
        setError(FileError::NotOpen, "File is not open");
        return std::nullopt;
    }

    if (! canRead()) {
        setError(FileError::ReadFailed, "File is not open for reading");
        return std::nullopt;
    }

    errno = 0;
    auto result = readAllFromHandle(m_handle, canWrite());
    if (! result.has_value()) {
        setError(FileError::ReadFailed, systemErrorString("Failed to read file", m_filePath));
        return std::nullopt;
    }

    clearError();
    return result;
}

bool File::readAll(ByteArray& output)
{
    auto result = readAll();
    if (! result.has_value()) {
        return false;
    }

    output = std::move(*result);
    return true;
}

bool File::write(const ByteArray& data)
{
    if (! isOpen()) {
        setError(FileError::NotOpen, "File is not open");
        return false;
    }

    if (! canWrite()) {
        setError(FileError::WriteFailed, "File is not open for writing");
        return false;
    }

    const std::size_t size = data.remainingSize();
    if (size == 0) {
        clearError();
        return true;
    }

    errno = 0;
    const auto* bytes = reinterpret_cast<const unsigned char*>(data.remainingData());
    const std::size_t written = std::fwrite(bytes, 1, size, m_handle);
    if (written != size) {
        setError(FileError::WriteFailed, systemErrorString("Failed to write file", m_filePath));
        return false;
    }

    clearError();
    return true;
}

bool File::flush()
{
    if (! isOpen()) {
        setError(FileError::NotOpen, "File is not open");
        return false;
    }

    errno = 0;
    if (std::fflush(m_handle) != 0) {
        setError(FileError::FlushFailed, systemErrorString("Failed to flush file", m_filePath));
        return false;
    }

    clearError();
    return true;
}

bool File::remove()
{
    if (m_filePath.empty()) {
        setError(FileError::InvalidFilePath, "File path is empty");
        return false;
    }

    if (isOpen()) {
        close();
    }

    errno = 0;
    if (std::remove(m_filePath.c_str()) != 0) {
        setError(FileError::RemoveFailed, systemErrorString("Failed to remove file", m_filePath));
        return false;
    }

    clearError();
    return true;
}

File::FileError File::lastError() const noexcept
{
    return m_lastError;
}

const std::string& File::errorString() const noexcept
{
    return m_errorString;
}

bool File::exists(const std::string& filePath)
{
    if (filePath.empty()) {
        return false;
    }

    struct stat status
    {
    };
    return ::stat(filePath.c_str(), &status) == 0;
}

std::optional<ByteArray> File::readAll(const std::string& filePath)
{
    if (filePath.empty()) {
        return std::nullopt;
    }

    errno = 0;
    std::FILE* handle = std::fopen(filePath.c_str(), "rb");
    if (handle == nullptr) {
        return std::nullopt;
    }

    auto result = readAllFromHandle(handle, false);
    std::fclose(handle);
    return result;
}

bool File::readAll(const std::string& filePath, ByteArray& output)
{
    auto result = readAll(filePath);
    if (! result.has_value()) {
        return false;
    }

    output = std::move(*result);
    return true;
}

bool File::writeAll(const std::string& filePath, const ByteArray& data)
{
    if (filePath.empty()) {
        return false;
    }

    errno = 0;
    std::FILE* handle = std::fopen(filePath.c_str(), "wb");
    if (handle == nullptr) {
        return false;
    }

    bool ok = true;
    const std::size_t size = data.remainingSize();
    if (size > 0) {
        const auto* bytes = reinterpret_cast<const unsigned char*>(data.remainingData());
        ok = std::fwrite(bytes, 1, size, handle) == size;
    }

    ok = std::fflush(handle) == 0 && ok;
    ok = std::fclose(handle) == 0 && ok;
    return ok;
}

bool File::remove(const std::string& filePath)
{
    if (filePath.empty()) {
        return false;
    }

    errno = 0;
    return std::remove(filePath.c_str()) == 0;
}

void File::update() {}

void File::clearError() const
{
    m_lastError = FileError::NoError;
    m_errorString.clear();
}

void File::setError(FileError error, std::string errorString) const
{
    m_lastError = error;
    m_errorString = std::move(errorString);
}

bool File::canRead() const noexcept
{
    return m_readable;
}

bool File::canWrite() const noexcept
{
    return m_writable;
}

}  // namespace snf
