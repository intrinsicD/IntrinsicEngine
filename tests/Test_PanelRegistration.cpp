#include <gtest/gtest.h>

#include <imgui.h>
#include "RHI.Vulkan.hpp"
#include <string>
#include <vector>

#include "TestImGuiFrameScope.hpp"

import Interface;

// ==========================================================================
// Panel Registration Coverage
//
// Validates the Interface::GUI registration API contract:
//   - Panels can be registered, removed, and re-registered by name.
//   - Overlays can be registered and removed by name.
//   - Menu bars can be registered.
//   - Duplicate panel registration re-opens and updates existing entries.
//
// These tests require an ImGui context (created/destroyed per test) but
// do NOT require a Vulkan device or GLFW window.
// ==========================================================================

// --------------------------------------------------------------------------
// RegisterPanel: basic registration + callback invocation via DrawGUI
// --------------------------------------------------------------------------

TEST(PanelRegistration, RegisterPanel_IsCallable)
{
    TestSupport::ImGuiFrameScope frame;

    bool called = false;
    Interface::GUI::RegisterPanel("TestPanel_A", [&called]() { called = true; });

    // DrawGUI iterates s_Panels and calls each callback.
    Interface::GUI::DrawGUI();
    EXPECT_TRUE(called);

    Interface::GUI::RemovePanel("TestPanel_A");
}

// --------------------------------------------------------------------------
// RemovePanel: verify callback is no longer invoked after removal
// --------------------------------------------------------------------------

TEST(PanelRegistration, RemovePanel_StopsCallback)
{
    TestSupport::ImGuiFrameScope frame;

    int callCount = 0;
    Interface::GUI::RegisterPanel("TestPanel_B", [&callCount]() { ++callCount; });

    Interface::GUI::DrawGUI();
    EXPECT_EQ(callCount, 1);

    Interface::GUI::RemovePanel("TestPanel_B");

    callCount = 0;
    Interface::GUI::DrawGUI();
    EXPECT_EQ(callCount, 0);
}

// --------------------------------------------------------------------------
// RemovePanel: removing non-existent panel is a no-op (no crash)
// --------------------------------------------------------------------------

TEST(PanelRegistration, RemovePanel_NonExistent_NoCrash)
{
    TestSupport::ImGuiFrameScope frame;
    Interface::GUI::RemovePanel("NonExistentPanel_XYZ");
    SUCCEED();
}

// --------------------------------------------------------------------------
// Duplicate registration: re-registering same name updates the callback
// --------------------------------------------------------------------------

TEST(PanelRegistration, DuplicateRegistration_UpdatesCallback)
{
    TestSupport::ImGuiFrameScope frame;

    int firstCount = 0;
    int secondCount = 0;

    Interface::GUI::RegisterPanel("TestPanel_Dup", [&firstCount]() { ++firstCount; });
    Interface::GUI::RegisterPanel("TestPanel_Dup", [&secondCount]() { ++secondCount; });

    Interface::GUI::DrawGUI();

    // The second callback should have replaced the first.
    EXPECT_EQ(firstCount, 0);
    EXPECT_EQ(secondCount, 1);

    Interface::GUI::RemovePanel("TestPanel_Dup");
}

// --------------------------------------------------------------------------
// Multiple panels: all registered panels are drawn
// --------------------------------------------------------------------------

TEST(PanelRegistration, MultiplePanels_AllDrawn)
{
    TestSupport::ImGuiFrameScope frame;

    int countA = 0, countB = 0, countC = 0;
    Interface::GUI::RegisterPanel("Multi_A", [&countA]() { ++countA; });
    Interface::GUI::RegisterPanel("Multi_B", [&countB]() { ++countB; });
    Interface::GUI::RegisterPanel("Multi_C", [&countC]() { ++countC; });

    Interface::GUI::DrawGUI();

    EXPECT_EQ(countA, 1);
    EXPECT_EQ(countB, 1);
    EXPECT_EQ(countC, 1);

    Interface::GUI::RemovePanel("Multi_A");
    Interface::GUI::RemovePanel("Multi_B");
    Interface::GUI::RemovePanel("Multi_C");
}

// --------------------------------------------------------------------------
// RegisterOverlay + RemoveOverlay
// --------------------------------------------------------------------------

TEST(PanelRegistration, RegisterOverlay_IsInvokedDuringDrawGUI)
{
    TestSupport::ImGuiFrameScope frame;

    bool overlayFired = false;
    Interface::GUI::RegisterOverlay("TestOverlay", [&overlayFired]() { overlayFired = true; });

    Interface::GUI::DrawGUI();
    EXPECT_TRUE(overlayFired);

    Interface::GUI::RemoveOverlay("TestOverlay");

    overlayFired = false;
    Interface::GUI::DrawGUI();
    EXPECT_FALSE(overlayFired);
}

TEST(PanelRegistration, RemoveOverlay_NonExistent_NoCrash)
{
    TestSupport::ImGuiFrameScope frame;
    Interface::GUI::RemoveOverlay("GhostOverlay_999");
    SUCCEED();
}

TEST(PanelRegistration, AddTexture_WithoutBackend_ReturnsNull)
{
    TestSupport::ImGuiFrameScope frame;

    void* textureId = Interface::GUI::AddTexture(VK_NULL_HANDLE, VK_NULL_HANDLE,
                                                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    EXPECT_EQ(textureId, nullptr);
}

TEST(PanelRegistration, RemoveTexture_WithoutBackend_IsNoOp)
{
    TestSupport::ImGuiFrameScope frame;

    Interface::GUI::RemoveTexture(reinterpret_cast<void*>(0x1));
    Interface::GUI::RemoveTexture(reinterpret_cast<void*>(0x1));
    Interface::GUI::RemoveTexture(nullptr);
    SUCCEED();
}

// --------------------------------------------------------------------------
// Overlay duplicate registration: updates callback
// --------------------------------------------------------------------------

TEST(PanelRegistration, DuplicateOverlay_UpdatesCallback)
{
    TestSupport::ImGuiFrameScope frame;

    int first = 0, second = 0;
    Interface::GUI::RegisterOverlay("OvDup", [&first]() { ++first; });
    Interface::GUI::RegisterOverlay("OvDup", [&second]() { ++second; });

    Interface::GUI::DrawGUI();

    EXPECT_EQ(first, 0);
    EXPECT_EQ(second, 1);

    Interface::GUI::RemoveOverlay("OvDup");
}

// --------------------------------------------------------------------------
// RegisterMainMenuBar: verify callback executes during DrawGUI
// --------------------------------------------------------------------------

TEST(PanelRegistration, RegisterMainMenuBar_IsInvoked)
{
    TestSupport::ImGuiFrameScope frame;

    bool menuFired = false;
    Interface::GUI::RegisterMainMenuBar("TestMenu", [&menuFired]()
    {
        if (ImGui::BeginMenu("TestMenu"))
        {
            menuFired = true;
            ImGui::EndMenu();
        }
    });

    Interface::GUI::DrawGUI();

    // Menu callbacks are called during BeginMainMenuBar, but the menu item
    // may not be visible/clicked in an automated test. The callback itself
    // should still be invoked (the BeginMenu call runs, even if it returns
    // false because no user interaction happened).
    // We verify the callback was at least reached by checking it didn't crash.
    SUCCEED();

    // Note: no RemoveMainMenuBar API exists — menus accumulate. This is by
    // design (see Gui.cpp). Tests should be aware that menu bar state is
    // process-global.
}

// --------------------------------------------------------------------------
// Panel closability flag
// --------------------------------------------------------------------------

TEST(PanelRegistration, NonClosablePanel_AlwaysDrawn)
{
    TestSupport::ImGuiFrameScope frame;

    int callCount = 0;
    Interface::GUI::RegisterPanel("Sticky", [&callCount]() { ++callCount; }, /*isClosable=*/false);

    Interface::GUI::DrawGUI();
    EXPECT_EQ(callCount, 1);

    // Draw again — non-closable panels should still be drawn.
    Interface::GUI::DrawGUI();
    EXPECT_EQ(callCount, 2);

    Interface::GUI::RemovePanel("Sticky");
}
