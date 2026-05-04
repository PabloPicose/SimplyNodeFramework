#include "SNFWidgets/FileOpenButton.h"

#include "imgui.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>

#include <unordered_map>

namespace {

// Maps a pending callback ID to the FileOpenButton that requested the file.
// Entries are added in triggerOpen() and removed inside snf_file_open_result().
std::unordered_map<int, snf::widgets::FileOpenButton*> s_pendingCallbacks;
int                                                     s_nextCallbackId = 1;

}  // namespace

// clang-format off
/**
 * Creates a hidden <input type="file"> element, attaches a FileReader, and
 * calls snf_file_open_result() with the loaded bytes once the user has
 * confirmed their selection.
 *
 * @param callbackId  Opaque integer used to route the result back to the
 *                    correct FileOpenButton instance.
 * @param accept      Null-terminated HTML accept attribute string, e.g. ".bin".
 */
EM_JS(void, snf_trigger_file_open_js, (int callbackId, const char* accept),
{
    var input         = document.createElement('input');
    input.type        = 'file';
    input.accept      = UTF8ToString(accept);
    input.style.display = 'none';
    document.body.appendChild(input);

    input.onchange = function(e) {
        var file = e.target.files[0];
        document.body.removeChild(input);
        if (!file) return;

        var reader  = new FileReader();
        reader.onload = function(ev) {
            var bytes   = new Uint8Array(ev.target.result);
            var dataPtr = _malloc(bytes.length);
            HEAPU8.set(bytes, dataPtr);

            var nameLen = lengthBytesUTF8(file.name) + 1;
            var namePtr = _malloc(nameLen);
            stringToUTF8(file.name, namePtr, nameLen);

            _snf_file_open_result(callbackId, dataPtr, bytes.length, namePtr);

            _free(dataPtr);
            _free(namePtr);
        };
        reader.readAsArrayBuffer(file);
    };

    input.click();
});
// clang-format on

extern "C" {

/**
 * Called from JavaScript once the browser has finished reading the selected
 * file.  Routes the data back to the correct FileOpenButton via the registry
 * and emits its fileLoaded signal.
 *
 * @param callbackId  ID returned by the corresponding triggerOpen() call.
 * @param data        Pointer to the file bytes on the WASM heap (caller-owned).
 * @param len         Number of bytes.
 * @param filename    Null-terminated original filename string.
 */
EMSCRIPTEN_KEEPALIVE
void snf_file_open_result(int callbackId, uint8_t* data, int len, const char* filename)
{
    auto it = s_pendingCallbacks.find(callbackId);
    if (it == s_pendingCallbacks.end()) {
        return;
    }
    snf::widgets::FileOpenButton* btn = it->second;
    s_pendingCallbacks.erase(it);

    btn->fileLoaded.emit(
        std::vector<std::uint8_t>(data, data + len),
        std::string(filename));
}

}  // extern "C"

#endif  // __EMSCRIPTEN__

namespace snf {
namespace widgets {

FileOpenButton::FileOpenButton(const std::string& label, snf::Node* parent)
    : Widget(parent), m_label(label)
{
}

void FileOpenButton::setLabel(const std::string& label)
{
    m_label = label;
}

std::string FileOpenButton::label() const
{
    return m_label;
}

void FileOpenButton::setAcceptFilter(const std::string& filter)
{
    m_acceptFilter = filter;
}

Size FileOpenButton::sizeHint() const
{
    if (ImGui::GetCurrentContext() == nullptr) {
        return {};
    }
    const ImVec2      ts = ImGui::CalcTextSize(m_label.c_str(), nullptr, true);
    const ImGuiStyle& s  = ImGui::GetStyle();
    return Size{ts.x + s.FramePadding.x * 2.0f, ts.y + s.FramePadding.y * 2.0f};
}

void FileOpenButton::triggerOpen()
{
    clicked.emit();
#ifdef __EMSCRIPTEN__
    const int id         = s_nextCallbackId++;
    s_pendingCallbacks[id] = this;
    snf_trigger_file_open_js(id, m_acceptFilter.c_str());
#endif
}

void FileOpenButton::renderImGui()
{
    if (ImGui::Button(m_label.c_str())) {
        triggerOpen();
    }
}

void FileOpenButton::renderImGuiConstrained(float width, float height)
{
    const ImVec2 size(width > 0.0f ? width : 0.0f, height > 0.0f ? height : 0.0f);
    if (ImGui::Button(m_label.c_str(), size)) {
        triggerOpen();
    }
}

}  // namespace widgets
}  // namespace snf
