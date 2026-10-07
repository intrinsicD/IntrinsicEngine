// Test-only cursor probe that finds a Dear ImGui item on its actual rectangle:
// the cursor walks a scan line until ImGui reports the item hovered. Shared by
// the Null-window shell suite and the Vulkan Sandbox smoke; neither needs a
// production test hook or fixed menu coordinates.
#pragma once

#include <cstddef>
#include <functional>
#include <vector>

#include <glm/vec2.hpp>
#include <imgui.h>
#include <imgui_internal.h>

namespace TestSupport
{
    // Whether ImGui reported item `label` of window `window` hovered in the
    // frame before its latest NewFrame. A menu-bar item's id is scoped by
    // "##MenuBar".
    [[nodiscard]] inline bool ImGuiItemHoveredPreviousFrame(const char* window, const char* label,
                                                            const bool menuBar)
    {
        if (ImGui::GetCurrentContext() == nullptr)
            return false;
        const ImGuiWindow* w = ImGui::FindWindowByName(window);
        if (w == nullptr)
            return false;
        const ImGuiID seed = menuBar ? ImHashStr("##MenuBar", 0, w->ID) : w->ID;
        return ImGui::GetCurrentContext()->HoveredIdPreviousFrame == ImHashStr(label, 0, seed);
    }

    // Point `k` of `line` over window `window`, or {0,0} while it does not exist.
    [[nodiscard]] inline glm::vec2 ImGuiWindowScan(const char* window,
                                                   const std::function<glm::vec2(const ImGuiWindow&, int)>& line,
                                                   const int k)
    {
        const ImGuiWindow* w = ImGui::GetCurrentContext() != nullptr ? ImGui::FindWindowByName(window) : nullptr;
        return w != nullptr ? line(*w, k) : glm::vec2{};
    }

    // Scan lines: leftwards along a menu bar from where its last menu ends
    // (ImGui keeps that offset for the next append), and down a popup or panel.
    [[nodiscard]] inline glm::vec2 MenuBarScan(const ImGuiWindow& w, const int k)
    {
        return {w.Pos.x + w.DC.MenuBarOffset.x - 6.0f * static_cast<float>(k + 2), w.Pos.y + w.Size.y * 0.5f};
    }
    [[nodiscard]] inline glm::vec2 WindowColumnScan(const ImGuiWindow& w, const int k)
    {
        return {w.Pos.x + 30.0f, w.Pos.y + 4.0f * static_cast<float>(k)};
    }

    // One step per frame, run after the previous frame's UI: moves the cursor
    // through `at(0)`, `at(1)`, ... until `hit()` reports the hover two steps
    // later. Returns true when done; `Found` then holds that point and the
    // cursor is back on it ({0,0}: not found within `count` points).
    struct ImGuiCursorProbe
    {
        std::vector<glm::vec2> Points{};
        glm::vec2 Found{0.0f};

        [[nodiscard]] bool Step(const std::function<bool()>& hit, const std::function<glm::vec2(int)>& at,
                                const int count, const std::function<void(glm::vec2)>& moveCursor)
        {
            const int k = static_cast<int>(Points.size());
            if (k >= 2 && hit())
            {
                Found = Points[static_cast<std::size_t>(k - 2)];
                moveCursor(Found);
                return true;
            }
            if (k >= count)
                return true;
            Points.push_back(at(k));
            moveCursor(Points.back());
            return false;
        }
    };
}
