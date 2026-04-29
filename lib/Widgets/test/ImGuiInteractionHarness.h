#pragma once

#include "SNFWidgets/Widget.h"

#include "imgui.h"

namespace snf::widgets::test {

struct InteractionRect {
    ImVec2 min;
    ImVec2 max;
    bool hovered = false;
    bool active = false;
};

template <typename WidgetT>
class TestWidget final : public WidgetT
{
public:
    using WidgetT::WidgetT;

    void renderForTest()
    {
        this->renderWidget();
    }
};

class ImGuiInteractionHarness
{
public:
    ImGuiInteractionHarness()
    {
        m_context = ImGui::CreateContext();
        ImGui::SetCurrentContext(m_context);

        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(800.0f, 600.0f);
        io.DeltaTime = 1.0f / 60.0f;
        io.ConfigInputTrickleEventQueue = false;
        io.AddFocusEvent(true);

        unsigned char* pixels = nullptr;
        int width = 0;
        int height = 0;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    }

    ~ImGuiInteractionHarness()
    {
        ImGui::SetCurrentContext(m_context);
        ImGui::DestroyContext(m_context);
    }

    void beginFrame(const ImVec2& mousePos = offscreenMousePos(), bool mouseDown = false)
    {
        ImGui::SetCurrentContext(m_context);

        ImGuiIO& io = ImGui::GetIO();
        io.MousePos = mousePos;
        io.MouseDown[0] = mouseDown;
        io.AddMousePosEvent(mousePos.x, mousePos.y);
        io.AddMouseButtonEvent(0, mouseDown);

        ImGui::NewFrame();

        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(io.DisplaySize, ImGuiCond_Always);
        ImGui::SetNextWindowFocus();
        ImGui::Begin("InteractionTest",
                     nullptr,
                     ImGuiWindowFlags_NoTitleBar |
                         ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoCollapse |
                         ImGuiWindowFlags_NoSavedSettings);
        ImGui::SetCursorScreenPos(ImVec2(32.0f, 32.0f));
    }

    void endFrame()
    {
        ImGui::End();
        ImGui::Render();
    }

    template <typename WidgetT>
    InteractionRect render(WidgetT& widget)
    {
        widget.renderForTest();
        return lastItemRect();
    }

    void sameLine()
    {
        ImGui::SameLine();
    }

    static ImVec2 centerOf(const InteractionRect& rect)
    {
        return ImVec2((rect.min.x + rect.max.x) * 0.5f, (rect.min.y + rect.max.y) * 0.5f);
    }

    static ImVec2 offscreenMousePos()
    {
        return ImVec2(-100.0f, -100.0f);
    }

private:
    static InteractionRect lastItemRect()
    {
        return InteractionRect{
            ImGui::GetItemRectMin(),
            ImGui::GetItemRectMax(),
            ImGui::IsItemHovered(),
            ImGui::IsItemActive()};
    }

    ImGuiContext* m_context = nullptr;
};

}  // namespace snf::widgets::test
