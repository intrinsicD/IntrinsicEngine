#include <gtest/gtest.h>

// This test avoids touching ImGui directly (requires Vulkan context).
// It validates linkability of the EditorUI module and exercises the
// SceneDirtyTracker state machine, which is pure CPU state.

import Runtime.Engine;
import Runtime.EditorUI;
import Runtime.SceneSerializer;

using namespace Runtime;

// =========================================================================
// Symbol linkability
// =========================================================================

TEST(EditorUI, RegisterDefaultPanels_IsLinkable)
{
    auto* fn = &Runtime::EditorUI::RegisterDefaultPanels;
    ASSERT_NE(fn, nullptr);
}

TEST(EditorUI, GetSceneDirtyTracker_IsLinkable)
{
    auto* fn = &Runtime::EditorUI::GetSceneDirtyTracker;
    ASSERT_NE(fn, nullptr);
}

// =========================================================================
// SceneDirtyTracker state machine
// =========================================================================

TEST(EditorUISceneDirtyTracker, InitiallyClean)
{
    SceneDirtyTracker tracker;
    EXPECT_FALSE(tracker.IsDirty());
}

TEST(EditorUISceneDirtyTracker, MarkDirty_SetsDirtyState)
{
    SceneDirtyTracker tracker;
    tracker.MarkDirty();
    EXPECT_TRUE(tracker.IsDirty());
}

TEST(EditorUISceneDirtyTracker, ClearDirty_ResetsState)
{
    SceneDirtyTracker tracker;
    tracker.MarkDirty();
    EXPECT_TRUE(tracker.IsDirty());

    tracker.ClearDirty();
    EXPECT_FALSE(tracker.IsDirty());
}

TEST(EditorUISceneDirtyTracker, MarkDirty_IsIdempotent)
{
    SceneDirtyTracker tracker;
    tracker.MarkDirty();
    tracker.MarkDirty();
    tracker.MarkDirty();
    EXPECT_TRUE(tracker.IsDirty());

    tracker.ClearDirty();
    EXPECT_FALSE(tracker.IsDirty());
}

TEST(EditorUISceneDirtyTracker, InitialPathIsEmpty)
{
    SceneDirtyTracker tracker;
    EXPECT_TRUE(tracker.GetCurrentPath().empty());
}

TEST(EditorUISceneDirtyTracker, SetCurrentPath_StoresPath)
{
    SceneDirtyTracker tracker;
    tracker.SetCurrentPath("scenes/test.json");
    EXPECT_EQ(tracker.GetCurrentPath(), "scenes/test.json");
}

TEST(EditorUISceneDirtyTracker, SetCurrentPath_Overwrites)
{
    SceneDirtyTracker tracker;
    tracker.SetCurrentPath("first.json");
    tracker.SetCurrentPath("second.json");
    EXPECT_EQ(tracker.GetCurrentPath(), "second.json");
}

TEST(EditorUISceneDirtyTracker, PathAndDirtyAreIndependent)
{
    SceneDirtyTracker tracker;

    tracker.SetCurrentPath("scene.json");
    EXPECT_FALSE(tracker.IsDirty());

    tracker.MarkDirty();
    EXPECT_EQ(tracker.GetCurrentPath(), "scene.json");

    tracker.ClearDirty();
    EXPECT_EQ(tracker.GetCurrentPath(), "scene.json");
    EXPECT_FALSE(tracker.IsDirty());
}

TEST(EditorUISceneDirtyTracker, ClearDirty_DoesNotClearPath)
{
    SceneDirtyTracker tracker;
    tracker.SetCurrentPath("my_scene.json");
    tracker.MarkDirty();
    tracker.ClearDirty();

    EXPECT_FALSE(tracker.IsDirty());
    EXPECT_EQ(tracker.GetCurrentPath(), "my_scene.json");
}

// =========================================================================
// Global tracker accessed via EditorUI
// =========================================================================

TEST(EditorUI, GlobalDirtyTracker_RoundTrip)
{
    auto& tracker = Runtime::EditorUI::GetSceneDirtyTracker();

    // Save original state to restore after test.
    const bool wasDirty = tracker.IsDirty();
    const auto origPath = tracker.GetCurrentPath();

    tracker.SetCurrentPath("test_roundtrip.json");
    tracker.MarkDirty();
    EXPECT_TRUE(tracker.IsDirty());
    EXPECT_EQ(tracker.GetCurrentPath(), "test_roundtrip.json");

    tracker.ClearDirty();
    EXPECT_FALSE(tracker.IsDirty());

    // Restore original state.
    tracker.SetCurrentPath(origPath);
    if (wasDirty) tracker.MarkDirty();
    else tracker.ClearDirty();
}
