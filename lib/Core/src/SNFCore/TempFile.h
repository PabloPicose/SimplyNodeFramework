#pragma once

/**
 * @file TempFile.h
 * @brief Node-based temporary file helper.
 * @ingroup SNFCore_IO
 */

#include "SNFCore/File.h"

namespace snf {

/**
 * @class TempFile
 * @ingroup SNFCore_IO
 * @brief Represents a uniquely named temporary file.
 *
 * On Linux/desktop the file is created with an atomic temporary-file creation
 * primitive. On Emscripten the file is created under `/tmp` in the virtual
 * filesystem; it is not a real host operating-system temporary file.
 */
class TempFile : public File
{
public:
    explicit TempFile(Node* parent = nullptr);
    ~TempFile() override;

    bool open();

    void setAutoRemove(bool enabled) noexcept;
    bool autoRemove() const noexcept;

private:
    bool m_autoRemove = true;
};

}  // namespace snf
