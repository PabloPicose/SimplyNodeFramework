#include "SNFWidgets/FileSaveButton.h"

#include "imgui.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>

// clang-format off
/**
 * Triggers a browser file download using a Blob URL.
 *
 * @param data     Pointer into the WASM heap holding the bytes to download.
 * @param len      Number of bytes.
 * @param filename Null-terminated suggested filename string.
 * @param mime     Null-terminated MIME type string.
 */
EM_JS(void, snf_trigger_download_js,
      (const uint8_t* data, size_t len, const char* filename, const char* mime),
{
    var bytes    = HEAPU8.subarray(data, data + len);
    var blob     = new Blob([bytes], { type: UTF8ToString(mime) });
    var url      = URL.createObjectURL(blob);
    var a        = document.createElement('a');
    a.href       = url;
    a.download   = UTF8ToString(filename);
    a.style.display = 'none';
    document.body.appendChild(a);
    a.click();
    setTimeout(function() {
        document.body.removeChild(a);
        URL.revokeObjectURL(url);
    }, 100);
});
// clang-format on

#endif  // __EMSCRIPTEN__

namespace snf {
namespace widgets {

FileSaveButton::FileSaveButton(const std::string& label, snf::Node* parent)
    : Widget(parent), m_label(label)
{
}

void FileSaveButton::setLabel(const std::string& label)
{
    m_label = label;
}

std::string FileSaveButton::label() const
{
    return m_label;
}

void FileSaveButton::setData(std::vector<std::uint8_t> data)
{
    m_data = std::move(data);
}

void FileSaveButton::setFilename(const std::string& filename)
{
    m_filename = filename;
}

void FileSaveButton::setMimeType(const std::string& mimeType)
{
    m_mimeType = mimeType;
}

bool FileSaveButton::hasData() const
{
    return !m_data.empty();
}

Size FileSaveButton::sizeHint() const
{
    if (ImGui::GetCurrentContext() == nullptr) {
        return {};
    }
    const ImVec2      ts = ImGui::CalcTextSize(m_label.c_str(), nullptr, true);
    const ImGuiStyle& s  = ImGui::GetStyle();
    return Size{ts.x + s.FramePadding.x * 2.0f, ts.y + s.FramePadding.y * 2.0f};
}

void FileSaveButton::triggerSave()
{
#ifdef __EMSCRIPTEN__
    snf_trigger_download_js(
        m_data.data(),
        m_data.size(),
        m_filename.c_str(),
        m_mimeType.c_str());
#endif
    clicked.emit();
}

void FileSaveButton::renderImGui()
{
    const bool noData = m_data.empty();
    if (noData) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button(m_label.c_str())) {
        triggerSave();
    }
    if (noData) {
        ImGui::EndDisabled();
    }
}

void FileSaveButton::renderImGuiConstrained(float width, float height)
{
    const ImVec2 size(width > 0.0f ? width : 0.0f, height > 0.0f ? height : 0.0f);
    const bool   noData = m_data.empty();
    if (noData) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button(m_label.c_str(), size)) {
        triggerSave();
    }
    if (noData) {
        ImGui::EndDisabled();
    }
}

}  // namespace widgets
}  // namespace snf
