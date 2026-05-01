#include "SNFCore/TempFile.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace snf {

namespace {

std::string temporaryDirectory()
{
#ifdef __EMSCRIPTEN__
    const char* directory = "/tmp";
    if (::mkdir(directory, 0777) != 0 && errno != EEXIST) {
        return {};
    }
    return directory;
#else
    const char* envDirectory = std::getenv("TMPDIR");
    if (envDirectory != nullptr && envDirectory[0] != '\0') {
        return envDirectory;
    }
    return "/tmp";
#endif
}

std::string createTemporaryPath()
{
    const std::string directory = temporaryDirectory();
    if (directory.empty()) {
        return {};
    }

    std::string pattern = directory;
    if (! pattern.empty() && pattern.back() != '/') {
        pattern += '/';
    }
    pattern += "snf_temp_XXXXXX";

    std::vector<char> buffer(pattern.begin(), pattern.end());
    buffer.push_back('\0');

    errno = 0;
    const int descriptor = ::mkstemp(buffer.data());
    if (descriptor < 0) {
        return {};
    }

    ::close(descriptor);
    return std::string(buffer.data());
}

}  // namespace

TempFile::TempFile(Node* parent)
    : File(parent)
{
}

TempFile::~TempFile()
{
    const std::string path = filePath();
    close();

    if (m_autoRemove && ! path.empty()) {
        File::remove(path);
    }
}

bool TempFile::open()
{
    if (isOpen()) {
        return true;
    }

    if (filePath().empty()) {
        const std::string path = createTemporaryPath();
        if (path.empty()) {
            setError(FileError::OpenFailed, "Failed to create temporary file");
            return false;
        }
        setFilePath(path);
    }

    return File::open(OpenMode::ReadWrite);
}

void TempFile::setAutoRemove(bool enabled) noexcept
{
    m_autoRemove = enabled;
}

bool TempFile::autoRemove() const noexcept
{
    return m_autoRemove;
}

}  // namespace snf
