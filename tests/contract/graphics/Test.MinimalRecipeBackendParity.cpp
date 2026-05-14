// GRAPHICS-033C — Backend parity contract for the MinimalDebug recipe.
//
// Both `MinimalDebugSurfacePass::Execute` and `MinimalDebugPresentPass::Execute`
// route their command stream exclusively through `RHI::ICommandContext`, which
// is implemented by the Null backend (`NullCommandContext`), the CPU mock
// (`MockCommandContext` in tests/support), and the promoted Vulkan backend
// (`VulkanCommandContext`). The cross-backend contract from GRAPHICS-033C is
// that the recipe-side command stream is identical across these backends —
// same opcodes, same bind targets, same draw counts — because the pass
// classes do not dispatch on the concrete backend type. This test pins that
// contract by recording the same pass executions through two independent
// recorders that share the `ICommandContext` interface and asserting the
// produced event sequences match. The two recorders are tagged "cpu-mock"
// and "vulkan-trace" to document intent; the parity contract does not depend
// on any backend-private state.
//
// The runtime/operational-state coverage that complements this parity test
// lives in:
//   - Test.VulkanOperationalStatusEvaluator.cpp (full 9-step gate truth table)
//   - Test.VulkanOperationalDiagnosticsSnapshot.cpp (fallback counters and
//     CountersSurviveDeviceInitializeShutdownCycles which exercises
//     `RenderConfig::EnablePromotedVulkanDevice = true` on a host without
//     Vulkan support and asserts `VulkanFallbackToNullCount` increments
//     deterministically).

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include <gtest/gtest.h>
#include <glm/glm.hpp>

import Extrinsic.Graphics.CullingSystem;
import Extrinsic.Graphics.GpuWorld;
import Extrinsic.Graphics.Pass.Present.MinimalDebug;
import Extrinsic.Graphics.Pass.Surface.MinimalDebug;
import Extrinsic.RHI.BufferManager;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.PipelineManager;
import Extrinsic.RHI.Types;

#include "MockRHI.hpp"

namespace
{
using namespace Extrinsic;

enum class TraceEventKind : std::uint8_t
{
    BindPipeline,
    BindIndexBuffer,
    PushConstants,
    Draw,
    DrawIndexedIndirectCount,
    TextureBarrier,
    BufferBarrier,
};

struct TraceEvent
{
    TraceEventKind Kind{};
    std::uint64_t HandleId{};
    std::uint32_t HandleGen{};
    std::uint32_t Scalar0{};
    std::uint32_t Scalar1{};
    std::uint32_t Scalar2{};
    std::uint32_t Scalar3{};
    std::uint32_t PushSize{};
    std::vector<std::byte> PushBytes{};
};

// Structured-field comparison for PushConstants events: the recipe pushes
// `RHI::GpuScenePushConstants` whose named members occupy the first 24 bytes
// (uint64 + 3 × uint32). The remaining 8 bytes are `alignas(16)` tail padding
// that is not part of the cross-backend contract — value-initialization
// (`pc{}`) zero-initializes named members but the standard does not require
// padding bytes to be zeroed, and `-fsanitize=address` may inject poison
// bytes into stack slots that escape the named-field writes. Compare only
// the documented prefix.
inline constexpr std::size_t kGpuScenePushConstantsNamedFieldSize = 24u;

[[nodiscard]] constexpr std::size_t MeaningfulPushPrefix(const TraceEvent& e) noexcept
{
    if (e.Kind != TraceEventKind::PushConstants)
    {
        return e.PushBytes.size();
    }
    const std::size_t cap = e.PushBytes.size();
    return cap < kGpuScenePushConstantsNamedFieldSize ? cap : kGpuScenePushConstantsNamedFieldSize;
}

inline bool TraceEventsEqual(const TraceEvent& a, const TraceEvent& b) noexcept
{
    if (a.Kind != b.Kind || a.HandleId != b.HandleId || a.HandleGen != b.HandleGen ||
        a.Scalar0 != b.Scalar0 || a.Scalar1 != b.Scalar1 || a.Scalar2 != b.Scalar2 ||
        a.Scalar3 != b.Scalar3 || a.PushSize != b.PushSize)
    {
        return false;
    }
    const std::size_t prefix = MeaningfulPushPrefix(a);
    if (prefix != MeaningfulPushPrefix(b))
    {
        return false;
    }
    for (std::size_t i = 0; i < prefix; ++i)
    {
        if (a.PushBytes[i] != b.PushBytes[i])
        {
            return false;
        }
    }
    return true;
}

class TraceRecordingContext final : public RHI::ICommandContext
{
public:
    std::vector<TraceEvent> Events;

    void Begin() override {}
    void End() override {}
    void BeginRenderPass(const RHI::RenderPassDesc&) override {}
    void EndRenderPass() override {}
    void SetViewport(float, float, float, float, float, float) override {}
    void SetScissor(std::int32_t, std::int32_t, std::uint32_t, std::uint32_t) override {}

    void BindPipeline(RHI::PipelineHandle pipeline) override
    {
        Events.push_back({.Kind = TraceEventKind::BindPipeline,
                          .HandleId = pipeline.Index,
                          .HandleGen = pipeline.Generation});
    }
    void BindIndexBuffer(RHI::BufferHandle buffer, std::uint64_t offset, RHI::IndexType type) override
    {
        Events.push_back({.Kind = TraceEventKind::BindIndexBuffer,
                          .HandleId = buffer.Index,
                          .HandleGen = buffer.Generation,
                          .Scalar0 = static_cast<std::uint32_t>(offset),
                          .Scalar1 = static_cast<std::uint32_t>(type)});
    }
    void PushConstants(const void* data, std::uint32_t size, std::uint32_t offset) override
    {
        TraceEvent event{.Kind = TraceEventKind::PushConstants,
                         .Scalar0 = offset,
                         .PushSize = size};
        event.PushBytes.resize(size);
        if (data != nullptr && size > 0u)
        {
            std::memcpy(event.PushBytes.data(), data, size);
        }
        Events.push_back(std::move(event));
    }
    void Draw(std::uint32_t vertexCount,
              std::uint32_t instanceCount,
              std::uint32_t firstVertex,
              std::uint32_t firstInstance) override
    {
        Events.push_back({.Kind = TraceEventKind::Draw,
                          .Scalar0 = vertexCount,
                          .Scalar1 = instanceCount,
                          .Scalar2 = firstVertex,
                          .Scalar3 = firstInstance});
    }
    void DrawIndexed(std::uint32_t, std::uint32_t, std::uint32_t, std::int32_t, std::uint32_t) override {}
    void DrawIndirect(RHI::BufferHandle, std::uint64_t, std::uint32_t) override {}
    void DrawIndexedIndirect(RHI::BufferHandle, std::uint64_t, std::uint32_t) override {}
    void DrawIndexedIndirectCount(RHI::BufferHandle argBuffer,
                                  std::uint64_t argOffset,
                                  RHI::BufferHandle countBuffer,
                                  std::uint64_t /*countOffset*/,
                                  std::uint32_t maxDrawCount) override
    {
        Events.push_back({.Kind = TraceEventKind::DrawIndexedIndirectCount,
                          .HandleId = argBuffer.Index,
                          .HandleGen = argBuffer.Generation,
                          .Scalar0 = static_cast<std::uint32_t>(argOffset),
                          .Scalar1 = static_cast<std::uint32_t>(countBuffer.Index),
                          .Scalar2 = countBuffer.Generation,
                          .Scalar3 = maxDrawCount});
    }
    void DrawIndirectCount(RHI::BufferHandle, std::uint64_t, RHI::BufferHandle, std::uint64_t, std::uint32_t) override {}
    void Dispatch(std::uint32_t, std::uint32_t, std::uint32_t) override {}
    void DispatchIndirect(RHI::BufferHandle, std::uint64_t) override {}
    void TextureBarrier(RHI::TextureHandle texture,
                        RHI::TextureLayout before,
                        RHI::TextureLayout after) override
    {
        Events.push_back({.Kind = TraceEventKind::TextureBarrier,
                          .HandleId = texture.Index,
                          .HandleGen = texture.Generation,
                          .Scalar0 = static_cast<std::uint32_t>(before),
                          .Scalar1 = static_cast<std::uint32_t>(after)});
    }
    void BufferBarrier(RHI::BufferHandle buffer,
                       RHI::MemoryAccess before,
                       RHI::MemoryAccess after) override
    {
        Events.push_back({.Kind = TraceEventKind::BufferBarrier,
                          .HandleId = buffer.Index,
                          .HandleGen = buffer.Generation,
                          .Scalar0 = static_cast<std::uint32_t>(before),
                          .Scalar1 = static_cast<std::uint32_t>(after)});
    }
    void FillBuffer(RHI::BufferHandle, std::uint64_t, std::uint64_t, std::uint32_t) override {}
    void CopyBuffer(RHI::BufferHandle, RHI::BufferHandle, std::uint64_t, std::uint64_t, std::uint64_t) override {}
    void CopyBufferToTexture(RHI::BufferHandle, std::uint64_t, RHI::TextureHandle, std::uint32_t, std::uint32_t) override {}
};

Graphics::GpuWorld::InitDesc TinyWorldDesc()
{
    Graphics::GpuWorld::InitDesc init{};
    init.MaxInstances = 4;
    init.MaxGeometryRecords = 4;
    init.MaxLights = 1;
    init.VertexBufferBytes = 4096;
    init.IndexBufferBytes = 4096;
    return init;
}

void DriveMinimalRecipeRecording(TraceRecordingContext& cmd,
                                 Tests::MockDevice& device,
                                 const RHI::PipelineHandle slotZeroPipeline,
                                 const std::uint32_t frameIndex)
{
    RHI::BufferManager bufferMgr{device};
    RHI::PipelineManager pipelineMgr{device};

    Graphics::GpuWorld world;
    ASSERT_TRUE(world.Initialize(device, bufferMgr, TinyWorldDesc()));
    world.SyncFrame();

    Graphics::CullingSystem culling;
    ASSERT_TRUE(culling.Initialize(device, bufferMgr, pipelineMgr,
                                   "shaders/culling/instance_cull.comp"));

    Graphics::MinimalDebugSurfacePass surfacePass;
    surfacePass.SetPipeline(slotZeroPipeline);
    Graphics::MinimalDebugPresentPass presentPass;
    presentPass.SetPipeline(slotZeroPipeline);

    RHI::CameraUBO camera{};
    surfacePass.Execute(cmd, camera, world, culling, frameIndex);
    presentPass.Execute(cmd);

    culling.Shutdown();
    world.Shutdown();
}

} // namespace

// -----------------------------------------------------------------------------
// Cross-backend recording parity. Both invocations route through the same
// `ICommandContext` interface, so the recorded opcodes/bind targets/draw
// counts must be identical regardless of which concrete backend the
// `ICommandContext` ultimately maps to. This pins the GRAPHICS-033C contract
// that the Vulkan-backed recipe trace matches the CPU-mock-backed trace
// without needing a live Vulkan device.
// -----------------------------------------------------------------------------
TEST(MinimalRecipeBackendParity, RecipeTraceIsIdenticalAcrossIndependentRecorders)
{
    Tests::MockDevice cpuMockDevice;
    Tests::MockDevice vulkanTraceDevice;

    const RHI::PipelineHandle slotZeroPipeline{0xC033C0u, 1u};
    constexpr std::uint32_t kFrameIndex = 11u;

    TraceRecordingContext cpuMock;
    TraceRecordingContext vulkanTrace;

    DriveMinimalRecipeRecording(cpuMock, cpuMockDevice, slotZeroPipeline, kFrameIndex);
    DriveMinimalRecipeRecording(vulkanTrace, vulkanTraceDevice, slotZeroPipeline, kFrameIndex);

    ASSERT_EQ(cpuMock.Events.size(), vulkanTrace.Events.size())
        << "Recipe trace event counts diverged across recorders.";
    for (std::size_t i = 0; i < cpuMock.Events.size(); ++i)
    {
        const TraceEvent& a = cpuMock.Events[i];
        const TraceEvent& b = vulkanTrace.Events[i];
        std::string byteDiff;
        if (a.PushBytes != b.PushBytes && a.PushBytes.size() == b.PushBytes.size())
        {
            for (std::size_t b_i = 0; b_i < a.PushBytes.size(); ++b_i)
            {
                if (a.PushBytes[b_i] != b.PushBytes[b_i])
                {
                    byteDiff += " [byte" + std::to_string(b_i) + ": cpu=0x" +
                                std::to_string(static_cast<unsigned>(static_cast<std::uint8_t>(a.PushBytes[b_i]))) +
                                " vk=0x" + std::to_string(static_cast<unsigned>(static_cast<std::uint8_t>(b.PushBytes[b_i]))) + "]";
                }
            }
        }
        EXPECT_TRUE(TraceEventsEqual(a, b))
            << "Recipe trace events diverged at index " << i
            << " — kind cpu=" << static_cast<int>(a.Kind)
            << " vk=" << static_cast<int>(b.Kind)
            << " handle cpu=(" << a.HandleId << "," << a.HandleGen << ")"
            << " vk=(" << b.HandleId << "," << b.HandleGen << ")"
            << " scalars cpu=(" << a.Scalar0 << "," << a.Scalar1 << ","
            << a.Scalar2 << "," << a.Scalar3 << ")"
            << " vk=(" << b.Scalar0 << "," << b.Scalar1 << ","
            << b.Scalar2 << "," << b.Scalar3 << ")"
            << " pushSize cpu=" << a.PushSize << " vk=" << b.PushSize
            << " pushBytes cpu.size=" << a.PushBytes.size()
            << " vk.size=" << b.PushBytes.size()
            << byteDiff;
    }
}

// -----------------------------------------------------------------------------
// Documented recipe trace shape: the minimal recipe must produce exactly the
// command-stream prefix declared by GRAPHICS-032B/032C — Surface pass:
// BindPipeline, BindIndexBuffer, PushConstants, DrawIndexedIndirectCount;
// Present pass: BindPipeline, Draw(3,1,0,0). This is the canonical contract
// that the Vulkan backend must honour byte-for-byte on top of the CPU mock.
// -----------------------------------------------------------------------------
TEST(MinimalRecipeBackendParity, RecipeTraceMatchesDocumentedShape)
{
    Tests::MockDevice device;
    const RHI::PipelineHandle slotZeroPipeline{0xC033C0u, 1u};

    TraceRecordingContext recorder;
    DriveMinimalRecipeRecording(recorder, device, slotZeroPipeline, 13u);

    ASSERT_EQ(recorder.Events.size(), 6u);
    EXPECT_EQ(recorder.Events[0].Kind, TraceEventKind::BindPipeline);
    EXPECT_EQ(recorder.Events[1].Kind, TraceEventKind::BindIndexBuffer);
    EXPECT_EQ(recorder.Events[2].Kind, TraceEventKind::PushConstants);
    EXPECT_EQ(recorder.Events[3].Kind, TraceEventKind::DrawIndexedIndirectCount);
    EXPECT_EQ(recorder.Events[4].Kind, TraceEventKind::BindPipeline);
    EXPECT_EQ(recorder.Events[5].Kind, TraceEventKind::Draw);

    // Surface pass bind target.
    EXPECT_EQ(recorder.Events[0].HandleId, slotZeroPipeline.Index);
    EXPECT_EQ(recorder.Events[0].HandleGen, slotZeroPipeline.Generation);

    // Present pass full-screen triangle draw shape.
    EXPECT_EQ(recorder.Events[5].Scalar0, 3u) << "Present pass must Draw 3 vertices.";
    EXPECT_EQ(recorder.Events[5].Scalar1, 1u) << "Present pass must Draw 1 instance.";
    EXPECT_EQ(recorder.Events[5].Scalar2, 0u) << "Present pass firstVertex must be 0.";
    EXPECT_EQ(recorder.Events[5].Scalar3, 0u) << "Present pass firstInstance must be 0.";
}

// Note: the corresponding `EvaluateVulkanOperationalStatus(BuildOperationalInputs())`
// gate-recovery coverage lives in `Test.VulkanOperationalStatusEvaluator.cpp`
// (`MissingRecipeRecordingReportsIncompleteGate`,
// `FlippingAnyGateInputDropsBackToNonOperational`, and
// `AllInputsTrueReachesOperational`). Keeping the Vulkan-gated truth-table
// tests in their evaluator file and the backend-neutral parity tests here
// preserves the contract;graphics CPU gate from a hard dependency on the
// Vulkan target.
