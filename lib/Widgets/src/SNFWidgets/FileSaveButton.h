#pragma once

/**
 * @file FileSaveButton.h
 * @brief Button widget that triggers a browser download in Emscripten builds.
 * @ingroup SNFWidgets
 */

#include "Widget.h"
#include <SNFCore/Connection.h>

#include <cstdint>
#include <string>
#include <vector>

namespace snf {
namespace widgets {

/**
 * @class FileSaveButton
 * @ingroup SNFWidgets
 * @brief A button that triggers a browser file download when clicked.
 *
 * On Emscripten/WebAssembly builds, clicking the button creates a Blob from
 * the stored data and triggers a browser download using the configured
 * filename. The button renders as disabled (greyed out) when no data has
 * been set via @ref setData.
 *
 * @note This widget is primarily designed for Emscripten/WebAssembly builds.
 *       On native desktop builds, clicking the button only emits @ref clicked
 *       without performing any file I/O.
 *
 * @code
 * snf::widgets::Window        win("Download", &webApp);
 * snf::widgets::FileSaveButton btn("Save File", &win);
 * btn.setFilename("output.bin");
 * btn.setMimeType("application/octet-stream");
 *
 * ws.binaryMessageReceived.connect([&btn](const std::vector<uint8_t>& data) {
 *     btn.setData(data);
 * });
 * @endcode
 */
class FileSaveButton : public Widget
{
public:
    /**
     * @param label  Text shown inside the button.
     * @param parent Parent node (a Window or another container Widget).
     */
    explicit FileSaveButton(const std::string& label, snf::Node* parent = nullptr);

    /** @brief Changes the button label. */
    void setLabel(const std::string& label);

    /** @brief Returns the current button label. */
    std::string label() const;

    /**
     * @brief Sets the binary data that will be downloaded when the button is clicked.
     *
     * The data is copied into the widget. The button becomes enabled once
     * non-empty data has been set.
     */
    void setData(std::vector<std::uint8_t> data);

    /**
     * @brief Sets the suggested filename for the browser download dialog.
     *
     * Defaults to @c "download.bin".
     */
    void setFilename(const std::string& filename);

    /**
     * @brief Sets the MIME type used when creating the download Blob.
     *
     * Defaults to @c "application/octet-stream".
     */
    void setMimeType(const std::string& mimeType);

    /** @brief Returns @c true if data has been set via @ref setData. */
    bool hasData() const;

    Size sizeHint() const override;

    /** @brief Emitted each time the button is clicked (after the download is triggered). */
    Signal<> clicked;

protected:
    void renderImGui() override;
    void renderImGuiConstrained(float width, float height) override;

private:
    void triggerSave();

    std::string               m_label;
    std::string               m_filename{"download.bin"};
    std::string               m_mimeType{"application/octet-stream"};
    std::vector<std::uint8_t> m_data;
};

}  // namespace widgets
}  // namespace snf
