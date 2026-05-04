#pragma once

/**
 * @file FileOpenButton.h
 * @brief Button widget that opens the browser file picker in Emscripten builds.
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
 * @class FileOpenButton
 * @ingroup SNFWidgets
 * @brief A button that opens the browser's file picker and emits the loaded data.
 *
 * On Emscripten/WebAssembly builds, clicking the button opens the browser's
 * native file picker. When the user selects a file, it is read asynchronously
 * and the @ref fileLoaded signal is emitted with the file's raw bytes and
 * original filename.
 *
 * @note This widget is primarily designed for Emscripten/WebAssembly builds.
 *       On native desktop builds, clicking the button only emits @ref clicked.
 *
 * @par Required Emscripten link flags
 * The final executable that links this widget must export the internal C
 * callback and the memory allocation helpers via link options:
 * @code
 * target_link_options(myapp PRIVATE
 *     "SHELL:-sEXPORTED_FUNCTIONS=_main,_malloc,_free,_snf_file_open_result"
 * )
 * @endcode
 *
 * @code
 * snf::widgets::Window         win("Upload", &webApp);
 * snf::widgets::FileOpenButton btn("Open File…", &win);
 * btn.setAcceptFilter(".bin,.csv");
 *
 * btn.fileLoaded.connect([](const std::vector<uint8_t>& data, const std::string& name) {
 *     std::printf("Loaded '%s': %zu bytes\n", name.c_str(), data.size());
 * });
 * @endcode
 */
class FileOpenButton : public Widget
{
public:
    /**
     * @param label  Text shown inside the button.
     * @param parent Parent node (a Window or another container Widget).
     */
    explicit FileOpenButton(const std::string& label, snf::Node* parent = nullptr);

    /** @brief Changes the button label. */
    void setLabel(const std::string& label);

    /** @brief Returns the current button label. */
    std::string label() const;

    /**
     * @brief Sets the file type filter shown in the browser file picker.
     *
     * Use standard HTML accept attribute syntax, e.g. @c ".bin,.csv" or
     * @c "image/png" or @c "image/jpeg". Defaults to @c "*" (all files).
     */
    void setAcceptFilter(const std::string& filter);

    Size sizeHint() const override;

    /** @brief Emitted when the button is pressed (before the file picker opens). */
    Signal<> clicked;

    /**
     * @brief Emitted once a file has been selected and fully read by the browser.
     *
     * @param data     Raw bytes of the file.
     * @param filename Original filename as reported by the browser.
     */
    Signal<std::vector<std::uint8_t>, std::string> fileLoaded;

protected:
    void renderImGui() override;
    void renderImGuiConstrained(float width, float height) override;

private:
    void triggerOpen();

    std::string m_label;
    std::string m_acceptFilter{"*"};
};

}  // namespace widgets
}  // namespace snf
