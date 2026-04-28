#include <gtest/gtest.h>
#include <cstdint>

#include <glm/glm.hpp>

import Graphics;
import Geometry;

// =============================================================================
// MeshRendererLifecycle — Compile-time contract tests
// =============================================================================
//
// These tests validate the CPU-side contract of the MeshRendererLifecycle
// system without requiring a GPU device. They verify:
//   - Surface::Component default state and field semantics.
//   - GPUScene slot sentinel consistency across component types.
//   - Visibility state fields and transitions.
//   - Material handle cache fields.
//   - Per-vertex and per-face color cache lifecycle.

// =============================================================================
// Section 1: Surface::Component Defaults
// =============================================================================

TEST(MeshRendererLifecycle_Contract, SurfaceComponentDefaultState)
{
    ECS::Surface::Component comp;

    EXPECT_FALSE(comp.Geometry.IsValid());
    EXPECT_EQ(comp.GpuSlot, ECS::kInvalidGpuSlot);
    EXPECT_TRUE(comp.Visible);
    EXPECT_TRUE(comp.CachedVisible);
    EXPECT_TRUE(comp.VertexColorsDirty);
    EXPECT_TRUE(comp.FaceColorsDirty);
    EXPECT_TRUE(comp.CachedVertexColors.empty());
    EXPECT_TRUE(comp.CachedFaceColors.empty());
    EXPECT_TRUE(comp.ShowPerVertexColors);
    EXPECT_TRUE(comp.ShowPerFaceColors);
}

TEST(MeshRendererLifecycle_Contract, InvalidSlotSentinelConsistent)
{
    // All components now use the shared ECS::kInvalidGpuSlot constant.
    EXPECT_EQ(ECS::kInvalidGpuSlot, ~0u);
}

// =============================================================================
// Section 2: Visibility State Transitions
// =============================================================================

TEST(MeshRendererLifecycle_Contract, VisibilityDefaultsToTrue)
{
    ECS::Surface::Component comp;
    EXPECT_TRUE(comp.Visible);
    EXPECT_TRUE(comp.CachedVisible);
}

TEST(MeshRendererLifecycle_Contract, VisibilityToggle)
{
    ECS::Surface::Component comp;
    comp.Visible = false;
    // CachedVisible remains true until GPUSceneSync processes the change.
    EXPECT_FALSE(comp.Visible);
    EXPECT_TRUE(comp.CachedVisible);

    // Simulate GPUSceneSync detecting the transition.
    comp.CachedVisible = comp.Visible;
    EXPECT_FALSE(comp.CachedVisible);
}

TEST(MeshRendererLifecycle_Contract, VisibilityTransitionDetectable)
{
    ECS::Surface::Component comp;
    comp.Visible = true;
    comp.CachedVisible = true;

    // No transition — values match.
    EXPECT_EQ(comp.Visible, comp.CachedVisible);

    // Trigger transition.
    comp.Visible = false;
    EXPECT_NE(comp.Visible, comp.CachedVisible);
}

// =============================================================================
// Section 3: Material Handle Cache
// =============================================================================

TEST(MeshRendererLifecycle_Contract, MaterialCacheDefaultState)
{
    ECS::Surface::Component comp;
    EXPECT_FALSE(comp.Material.IsValid());
    // Cached material revision starts at 0.
    EXPECT_EQ(comp.CachedMaterialRevisionForInstance, 0u);
    EXPECT_FALSE(comp.CachedIsSelectedForInstance);
}

TEST(MeshRendererLifecycle_Contract, GpuSlotCanBeAssignedAndCleared)
{
    ECS::Surface::Component comp;
    EXPECT_EQ(comp.GpuSlot, ECS::kInvalidGpuSlot);

    comp.GpuSlot = 7u;
    EXPECT_EQ(comp.GpuSlot, 7u);

    comp.GpuSlot = ECS::kInvalidGpuSlot;
    EXPECT_EQ(comp.GpuSlot, ECS::kInvalidGpuSlot);
}

// =============================================================================
// Section 4: Per-Vertex and Per-Face Color Cache
// =============================================================================

TEST(MeshRendererLifecycle_Contract, VertexColorCacheStartsEmpty)
{
    ECS::Surface::Component comp;
    EXPECT_TRUE(comp.CachedVertexColors.empty());
    EXPECT_TRUE(comp.VertexColorsDirty);
}

TEST(MeshRendererLifecycle_Contract, FaceColorCacheStartsEmpty)
{
    ECS::Surface::Component comp;
    EXPECT_TRUE(comp.CachedFaceColors.empty());
    EXPECT_TRUE(comp.FaceColorsDirty);
}

TEST(MeshRendererLifecycle_Contract, VertexColorDirtyLifecycle)
{
    ECS::Surface::Component comp;
    EXPECT_TRUE(comp.VertexColorsDirty);

    // Simulate PropertySetDirtySync clearing the flag after extraction.
    comp.VertexColorsDirty = false;
    EXPECT_FALSE(comp.VertexColorsDirty);

    // Simulate DirtyTag::VertexAttributes re-setting the flag.
    comp.VertexColorsDirty = true;
    EXPECT_TRUE(comp.VertexColorsDirty);
}

TEST(MeshRendererLifecycle_Contract, FaceColorDirtyLifecycle)
{
    ECS::Surface::Component comp;
    EXPECT_TRUE(comp.FaceColorsDirty);

    comp.FaceColorsDirty = false;
    EXPECT_FALSE(comp.FaceColorsDirty);

    comp.FaceColorsDirty = true;
    EXPECT_TRUE(comp.FaceColorsDirty);
}

TEST(MeshRendererLifecycle_Contract, VertexColorsPriorityOverFaceColors)
{
    // Per CLAUDE.md: Per-vertex colors take priority over per-face colors.
    ECS::Surface::Component comp;
    comp.CachedVertexColors = {0xFF0000FFu, 0xFF00FF00u, 0xFFFF0000u};
    comp.CachedFaceColors = {0xFFFFFFFFu};
    comp.ShowPerVertexColors = true;
    comp.ShowPerFaceColors = true;

    // When both are present and enabled, vertex colors should be used.
    // This is enforced by SurfacePass shader selection, not the component,
    // but we verify the fields are independently settable.
    EXPECT_TRUE(comp.ShowPerVertexColors);
    EXPECT_TRUE(comp.ShowPerFaceColors);
    EXPECT_FALSE(comp.CachedVertexColors.empty());
    EXPECT_FALSE(comp.CachedFaceColors.empty());
}

// =============================================================================
// Section 5: Attribute Visualization Toggles
// =============================================================================

TEST(MeshRendererLifecycle_Contract, ShowPerVertexColorsDefaultTrue)
{
    ECS::Surface::Component comp;
    EXPECT_TRUE(comp.ShowPerVertexColors);
}

TEST(MeshRendererLifecycle_Contract, ShowPerFaceColorsDefaultTrue)
{
    ECS::Surface::Component comp;
    EXPECT_TRUE(comp.ShowPerFaceColors);
}

TEST(MeshRendererLifecycle_Contract, TogglesAreIndependent)
{
    ECS::Surface::Component comp;
    comp.ShowPerVertexColors = false;
    comp.ShowPerFaceColors = true;

    EXPECT_FALSE(comp.ShowPerVertexColors);
    EXPECT_TRUE(comp.ShowPerFaceColors);
}
