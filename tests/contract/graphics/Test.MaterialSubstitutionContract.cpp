#include <array>
#include <cstdint>
#include <memory>

#include <gtest/gtest.h>
#include <glm/glm.hpp>

import Extrinsic.Graphics.GpuWorld;
import Extrinsic.Graphics.Material;
import Extrinsic.Graphics.MaterialSystem;
import Extrinsic.Graphics.RenderFrameInput;
import Extrinsic.Graphics.Renderer;
import Extrinsic.Graphics.RenderWorld;
import Extrinsic.Graphics.TransformSyncSystem;
import Extrinsic.RHI.FrameHandle;
import Extrinsic.RHI.Types;

#include "MockRHI.hpp"

using namespace Extrinsic;

// GRAPHICS-031B Decision 7 path-(b) — renderer-side substitution of
// missing/invalid material slots to `kDefaultMaterialSlotIndex` (slot 0 =
// `Material.DefaultDebugSurface`) with Decision 8 per-frame diagnostics.

namespace
{
    std::unique_ptr<Graphics::IRenderer> MakeRenderer(Tests::MockDevice& device)
    {
        auto renderer = Graphics::CreateRenderer();
        renderer->Initialize(device);
        return renderer;
    }

    Graphics::TransformSyncRecord MakeRecord(std::uint32_t stableId,
                                             Graphics::GpuInstanceHandle instance,
                                             std::uint32_t materialSlot,
                                             bool hasMaterialSlot)
    {
        return Graphics::TransformSyncRecord{
            .StableId = stableId,
            .Instance = instance,
            .Model = glm::mat4{1.f},
            .RenderFlags = RHI::GpuRender_Visible | RHI::GpuRender_Surface | RHI::GpuRender_Opaque,
            .Bounds = {},
            .MaterialSlot = materialSlot,
            .HasMaterialSlot = hasMaterialSlot,
        };
    }
} // namespace

TEST(MaterialSubstitutionContract, SentinelUnsetSubstitutesDefaultSlotAndIncrementsMissingCounter)
{
    Tests::MockDevice device;
    auto renderer = MakeRenderer(device);

    RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));

    const auto instance = renderer->GetGpuWorld().AllocateInstance(11u);
    ASSERT_TRUE(instance.IsValid());

    const std::array<Graphics::TransformSyncRecord, 1> transforms{{
        MakeRecord(11u, instance, /*materialSlot=*/0u, /*hasMaterialSlot=*/false),
    }};
    renderer->SubmitRuntimeSnapshots(Graphics::RuntimeRenderSnapshotBatch{.Transforms = transforms});

    const Graphics::RenderWorld world = renderer->ExtractRenderWorld({});
    ASSERT_EQ(world.Renderables.size(), 1u);
    EXPECT_EQ(world.Renderables[0].MaterialSlot, Graphics::kDefaultMaterialSlotIndex);
    EXPECT_TRUE(world.Renderables[0].HasMaterialSlot);

    const auto diagnostics = renderer->GetMaterialSystem().GetDiagnostics();
    EXPECT_EQ(diagnostics.MissingMaterialFallbackCount, 1u);
    EXPECT_EQ(diagnostics.InvalidMaterialSlotCount, 0u);
    EXPECT_EQ(diagnostics.DefaultDebugSurfaceUses, 1u);

    renderer->Shutdown();
}

TEST(MaterialSubstitutionContract, OutOfRangeSlotSubstitutesDefaultAndIncrementsInvalidCounter)
{
    Tests::MockDevice device;
    auto renderer = MakeRenderer(device);

    RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));

    const std::uint32_t capacity = renderer->GetMaterialSystem().GetCapacity();
    ASSERT_GT(capacity, 0u);

    const auto instance = renderer->GetGpuWorld().AllocateInstance(22u);
    ASSERT_TRUE(instance.IsValid());

    const std::array<Graphics::TransformSyncRecord, 1> transforms{{
        MakeRecord(22u, instance, /*materialSlot=*/capacity + 10u, /*hasMaterialSlot=*/true),
    }};
    renderer->SubmitRuntimeSnapshots(Graphics::RuntimeRenderSnapshotBatch{.Transforms = transforms});

    const Graphics::RenderWorld world = renderer->ExtractRenderWorld({});
    ASSERT_EQ(world.Renderables.size(), 1u);
    EXPECT_EQ(world.Renderables[0].MaterialSlot, Graphics::kDefaultMaterialSlotIndex);
    EXPECT_TRUE(world.Renderables[0].HasMaterialSlot);

    const auto diagnostics = renderer->GetMaterialSystem().GetDiagnostics();
    EXPECT_EQ(diagnostics.MissingMaterialFallbackCount, 0u);
    EXPECT_EQ(diagnostics.InvalidMaterialSlotCount, 1u);
    EXPECT_EQ(diagnostics.DefaultDebugSurfaceUses, 1u);

    renderer->Shutdown();
}

TEST(MaterialSubstitutionContract, DefaultDebugSurfaceUsesEqualsAuthoredDefaultPlusFallbacks)
{
    Tests::MockDevice device;
    auto renderer = MakeRenderer(device);

    RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));

    const std::uint32_t capacity = renderer->GetMaterialSystem().GetCapacity();
    ASSERT_GE(capacity, 4u);

    const auto i0 = renderer->GetGpuWorld().AllocateInstance(100u);
    const auto i1 = renderer->GetGpuWorld().AllocateInstance(101u);
    const auto i2 = renderer->GetGpuWorld().AllocateInstance(102u);
    const auto i3 = renderer->GetGpuWorld().AllocateInstance(103u);
    ASSERT_TRUE(i0.IsValid());
    ASSERT_TRUE(i1.IsValid());
    ASSERT_TRUE(i2.IsValid());
    ASSERT_TRUE(i3.IsValid());

    // Mix:
    //   - authored default (slot 0, HasMaterialSlot=true)
    //   - authored non-default (slot 3, HasMaterialSlot=true) — within capacity
    //   - sentinel unset (HasMaterialSlot=false) — substitutes to slot 0
    //   - out-of-range (slot=capacity+5, HasMaterialSlot=true) — substitutes to slot 0
    const std::array<Graphics::TransformSyncRecord, 4> transforms{{
        MakeRecord(100u, i0, /*materialSlot=*/Graphics::kDefaultMaterialSlotIndex, /*hasMaterialSlot=*/true),
        MakeRecord(101u, i1, /*materialSlot=*/3u, /*hasMaterialSlot=*/true),
        MakeRecord(102u, i2, /*materialSlot=*/0u, /*hasMaterialSlot=*/false),
        MakeRecord(103u, i3, /*materialSlot=*/capacity + 5u, /*hasMaterialSlot=*/true),
    }};
    renderer->SubmitRuntimeSnapshots(Graphics::RuntimeRenderSnapshotBatch{.Transforms = transforms});

    const Graphics::RenderWorld world = renderer->ExtractRenderWorld({});
    ASSERT_EQ(world.Renderables.size(), 4u);
    EXPECT_EQ(world.Renderables[0].MaterialSlot, Graphics::kDefaultMaterialSlotIndex);
    EXPECT_EQ(world.Renderables[1].MaterialSlot, 3u);
    EXPECT_EQ(world.Renderables[2].MaterialSlot, Graphics::kDefaultMaterialSlotIndex);
    EXPECT_EQ(world.Renderables[3].MaterialSlot, Graphics::kDefaultMaterialSlotIndex);

    const auto diagnostics = renderer->GetMaterialSystem().GetDiagnostics();
    EXPECT_EQ(diagnostics.MissingMaterialFallbackCount, 1u);
    EXPECT_EQ(diagnostics.InvalidMaterialSlotCount, 1u);
    // DefaultDebugSurfaceUses = authored-default (1) + Missing (1) + Invalid (1) = 3.
    EXPECT_EQ(diagnostics.DefaultDebugSurfaceUses, 3u);
    const std::uint32_t authoredDefault =
        diagnostics.DefaultDebugSurfaceUses
        - diagnostics.MissingMaterialFallbackCount
        - diagnostics.InvalidMaterialSlotCount;
    EXPECT_EQ(authoredDefault, 1u);

    renderer->Shutdown();
}

TEST(MaterialSubstitutionContract, PerFrameCountersResetAtBeginFrame)
{
    Tests::MockDevice device;
    auto renderer = MakeRenderer(device);

    RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));

    const auto instance = renderer->GetGpuWorld().AllocateInstance(55u);
    ASSERT_TRUE(instance.IsValid());

    const std::array<Graphics::TransformSyncRecord, 1> first{{
        MakeRecord(55u, instance, /*materialSlot=*/0u, /*hasMaterialSlot=*/false),
    }};
    renderer->SubmitRuntimeSnapshots(Graphics::RuntimeRenderSnapshotBatch{.Transforms = first});
    (void)renderer->ExtractRenderWorld({});

    EXPECT_EQ(renderer->GetMaterialSystem().GetDiagnostics().MissingMaterialFallbackCount, 1u);
    EXPECT_EQ(renderer->GetMaterialSystem().GetDiagnostics().DefaultDebugSurfaceUses, 1u);

    ASSERT_TRUE(renderer->BeginFrame(frame));
    const auto diagnostics = renderer->GetMaterialSystem().GetDiagnostics();
    EXPECT_EQ(diagnostics.MissingMaterialFallbackCount, 0u);
    EXPECT_EQ(diagnostics.InvalidMaterialSlotCount, 0u);
    EXPECT_EQ(diagnostics.DefaultDebugSurfaceUses, 0u);

    renderer->Shutdown();
}

TEST(MaterialSubstitutionContract, DefaultSlotIdentitySurvivesRebuildGpuResources)
{
    Tests::MockDevice device;
    device.Operational = false;
    auto renderer = MakeRenderer(device);

    // Before rebuild: slot 0 should already carry the default-debug-surface
    // params from MaterialSystem::Initialize (registered even on non-operational
    // devices for CPU-side coherence).
    const Graphics::MaterialHandle defaultHandle{Graphics::kDefaultMaterialSlotIndex, 0u};
    const Graphics::MaterialParams beforeParams =
        renderer->GetMaterialSystem().GetParams(defaultHandle);

    // Drive an operational rebuild — this re-creates the SSBO and re-uploads
    // the CPU mirror, but slot 0's params/type identity must survive.
    device.Operational = true;
    ASSERT_TRUE(renderer->RebuildOperationalResources(device));

    const Graphics::MaterialParams afterParams =
        renderer->GetMaterialSystem().GetParams(defaultHandle);

    EXPECT_FLOAT_EQ(afterParams.BaseColorFactor.x, beforeParams.BaseColorFactor.x);
    EXPECT_FLOAT_EQ(afterParams.BaseColorFactor.y, beforeParams.BaseColorFactor.y);
    EXPECT_FLOAT_EQ(afterParams.BaseColorFactor.z, beforeParams.BaseColorFactor.z);
    EXPECT_FLOAT_EQ(afterParams.BaseColorFactor.w, beforeParams.BaseColorFactor.w);
    EXPECT_EQ(afterParams.Flags, beforeParams.Flags);

    // The DefaultDebugSurface type registration must persist by name and ID.
    const auto debugType = renderer->GetMaterialSystem().FindType(
        Graphics::kMaterialTypeName_DefaultDebugSurface);
    ASSERT_TRUE(debugType.IsValid());
    EXPECT_EQ(debugType.Index, Graphics::kMaterialTypeID_DefaultDebugSurface);

    // Slot 0 still resolves to itself.
    EXPECT_EQ(renderer->GetMaterialSystem().GetMaterialSlot(defaultHandle),
              Graphics::kDefaultMaterialSlotIndex);

    renderer->Shutdown();
}
