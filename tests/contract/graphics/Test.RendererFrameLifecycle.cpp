#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <ranges>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

import Extrinsic.Core.Filesystem.PathResolver;
import Extrinsic.Core.Tasks;
import Extrinsic.Core.Telemetry;
import Extrinsic.Graphics.CameraSnapshots;
import Extrinsic.Graphics.CurrentRendererContractAdapter;
import Extrinsic.Graphics.Renderer;
import Extrinsic.Graphics.FrameRecipe;
import Extrinsic.Graphics.HZB;
import Extrinsic.Graphics.ImGuiOverlaySystem;
import Extrinsic.Graphics.LightClusters;
import Extrinsic.Graphics.Pass.PostProcess.Bloom;
import Extrinsic.Graphics.Pass.PostProcess.FXAA;
import Extrinsic.Graphics.Pass.PostProcess.Histogram;
import Extrinsic.Graphics.Pass.PostProcess.SMAA;
import Extrinsic.Graphics.Pass.PostProcess.ToneMap;
import Extrinsic.Graphics.Pass.Selection.Outline;
import Extrinsic.Graphics.PostProcessSystem;
import Extrinsic.Graphics.RenderFrameInput;
import Extrinsic.Graphics.RenderWorld;
import Extrinsic.Graphics.SelectionSystem;
import Extrinsic.Graphics.ShadowSystem;
import Extrinsic.Graphics.VisualizationPackets;
import Extrinsic.RHI.Bindless;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.FrameHandle;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.Profiler;
import Extrinsic.RHI.Types;

#include "MockRHI.hpp"

namespace
{
    constexpr Extrinsic::RHI::FrontFace kVulkanCameraTriangleFrontFace =
        Extrinsic::RHI::FrontFace::Clockwise;

    namespace Tasks = Extrinsic::Core::Tasks;

    class SchedulerScope
    {
    public:
        explicit SchedulerScope(const unsigned threadCount)
            : m_Owns(!Tasks::Scheduler::IsInitialized())
        {
            if (m_Owns)
            {
                Tasks::Scheduler::Initialize(threadCount);
            }
        }

        ~SchedulerScope()
        {
            if (m_Owns)
            {
                Tasks::Scheduler::Shutdown();
            }
        }

        SchedulerScope(const SchedulerScope&) = delete;
        SchedulerScope& operator=(const SchedulerScope&) = delete;

    private:
        bool m_Owns = false;
    };

    [[nodiscard]] const Extrinsic::RHI::PipelineDesc* FindCreatedPipelineDesc(
        const Extrinsic::Tests::MockDevice& device,
        const std::string_view debugName) noexcept
    {
        for (const Extrinsic::RHI::PipelineDesc& desc : device.CreatedPipelineDescs)
        {
            if (desc.DebugName == debugName)
                return &desc;
        }
        return nullptr;
    }

    [[nodiscard]] bool ContainsTextureBarrier(const Extrinsic::Tests::MockCommandContext& context,
                                              const Extrinsic::RHI::TextureHandle handle)
    {
        for (const auto& barrier : context.TextureBarrierCalls)
        {
            if (barrier.Texture == handle)
            {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] int FindEventIndex(const Extrinsic::Tests::MockCommandContext& context,
                                     const Extrinsic::Tests::MockCommandContext::EventKind kind,
                                     const int start = 0)
    {
        for (int i = start; i < static_cast<int>(context.Events.size()); ++i)
        {
            if (context.Events[static_cast<std::size_t>(i)] == kind)
            {
                return i;
            }
        }
        return -1;
    }

    [[nodiscard]] std::size_t CountPushConstantSize(
        const Extrinsic::Tests::MockCommandContext& context,
        const std::uint32_t size) noexcept
    {
        return static_cast<std::size_t>(
            std::ranges::count(context.PushConstantSizes, size));
    }

    [[nodiscard]] Extrinsic::Graphics::ImGuiOverlayFrame MakeOverlayFrameWithWork()
    {
        Extrinsic::Graphics::ImGuiOverlayFrame frame{};
        frame.Enabled = true;
        frame.DisplayWidth = 256u;
        frame.DisplayHeight = 144u;

        Extrinsic::Graphics::ImGuiOverlayDrawList drawList{};
        drawList.CommandCount = 1u;
        drawList.VertexCount = 3u;
        drawList.IndexCount = 3u;
        drawList.UsesUserTexture = false;
        drawList.Vertices = {
            Extrinsic::Graphics::ImGuiOverlayVertex{
                .Position = {0.0f, 0.0f},
                .UV = {0.0f, 0.0f},
                .Color = 0xffffffffu,
            },
            Extrinsic::Graphics::ImGuiOverlayVertex{
                .Position = {1.0f, 0.0f},
                .UV = {1.0f, 0.0f},
                .Color = 0xffffffffu,
            },
            Extrinsic::Graphics::ImGuiOverlayVertex{
                .Position = {0.0f, 1.0f},
                .UV = {0.0f, 1.0f},
                .Color = 0xffffffffu,
            },
        };
        drawList.Indices = {0u, 1u, 2u};
        frame.DrawLists.push_back(std::move(drawList));
        return frame;
    }

    [[nodiscard]] Extrinsic::RHI::TextureHandle FindCreatedTextureByDebugName(
        const Extrinsic::Tests::MockDevice& device,
        const std::string_view debugName) noexcept
    {
        const std::size_t count =
            std::min(device.CreatedTextureDescs.size(), device.CreatedTextureHandles.size());
        for (std::size_t i = 0; i < count; ++i)
        {
            const char* name = device.CreatedTextureDescs[i].DebugName;
            if (name != nullptr && std::string_view{name} == debugName)
            {
                return device.CreatedTextureHandles[i];
            }
        }
        return {};
    }

    [[nodiscard]] bool DispatchMatches(
        const Extrinsic::Tests::MockCommandContext::DispatchRecord& record,
        const std::uint32_t x,
        const std::uint32_t y,
        const std::uint32_t z) noexcept
    {
        return record.X == x && record.Y == y && record.Z == z;
    }

    void ExpectDispatchRecordsContainUnordered(
        const Extrinsic::Tests::MockCommandContext& context,
        const std::vector<Extrinsic::Tests::MockCommandContext::DispatchRecord>& expected,
        const std::size_t startIndex)
    {
        ASSERT_LE(startIndex, context.DispatchRecords.size());
        ASSERT_EQ(context.DispatchRecords.size() - startIndex, expected.size());

        std::vector<bool> matched(context.DispatchRecords.size(), false);
        for (const auto& expectedRecord : expected)
        {
            bool found = false;
            for (std::size_t i = startIndex; i < context.DispatchRecords.size(); ++i)
            {
                if (matched[i])
                {
                    continue;
                }
                if (DispatchMatches(context.DispatchRecords[i],
                                    expectedRecord.X,
                                    expectedRecord.Y,
                                    expectedRecord.Z))
                {
                    matched[i] = true;
                    found = true;
                    break;
                }
            }
            EXPECT_TRUE(found)
                << "Missing dispatch record x=" << expectedRecord.X
                << " y=" << expectedRecord.Y
                << " z=" << expectedRecord.Z;
        }
    }

    [[nodiscard]] const Extrinsic::Graphics::RenderGraphCommandPassStats* FindCommandPass(
        const Extrinsic::Graphics::RenderGraphFrameStats& stats,
        const std::string& name)
    {
        for (const auto& pass : stats.CommandRecords.Passes)
        {
            if (pass.Name == name)
            {
                return &pass;
            }
        }
        return nullptr;
    }

    [[nodiscard]] std::uint32_t CountCommandPass(
        const Extrinsic::Graphics::RenderGraphFrameStats& stats,
        const std::string& name)
    {
        return static_cast<std::uint32_t>(
            std::ranges::count_if(stats.CommandRecords.Passes,
                                  [&](const auto& pass)
                                  {
                                      return pass.Name == name;
                                  }));
    }

    [[nodiscard]] Extrinsic::Graphics::FrameRecipeOverride MakeRecipeOverride(
        std::vector<std::string> disabledSlots)
    {
        return Extrinsic::Graphics::FrameRecipeOverride{
            .Recipe = Extrinsic::Graphics::MakeCurrentRendererRecipeDescriptor(),
            .DisabledExtensionSlots = std::move(disabledSlots),
            .SourceId = "unit-test",
        };
    }

    [[nodiscard]] bool HasProjectionDiagnostic(
        const Extrinsic::Graphics::FrameRecipeOverrideProjection& projection,
        const Extrinsic::Graphics::FrameRecipeOverrideDiagnosticCode code,
        const std::string_view subject)
    {
        return std::any_of(projection.Diagnostics.begin(),
                           projection.Diagnostics.end(),
                           [code, subject](const Extrinsic::Graphics::FrameRecipeOverrideDiagnostic& diagnostic) {
                               return diagnostic.Code == code &&
                                      diagnostic.Subject == subject;
                           });
    }

    [[nodiscard]] std::uint32_t ExpectedCullDispatchGroups() noexcept
    {
        return (Extrinsic::RHI::kMaxIndirectDrawCount + Extrinsic::RHI::kGpuCullDispatchGroupSize - 1u) /
               Extrinsic::RHI::kGpuCullDispatchGroupSize;
    }

    [[nodiscard]] Extrinsic::Graphics::HZBBuildDispatchPlan ExpectedFallbackHZBPlan(
        const std::uint32_t width,
        const std::uint32_t height)
    {
        return Extrinsic::Graphics::ComputeHZBBuildDispatchPlan(
            Extrinsic::Graphics::ComputeHZBDesc(width, height),
            Extrinsic::Graphics::HZBBuildCapabilities{
                .SupportsSinglePassMipChain = false,
            });
    }

    [[nodiscard]] Extrinsic::Graphics::ClusterGridBuildDispatchPlan ExpectedClusterGridPlan(
        const std::uint32_t width,
        const std::uint32_t height)
    {
        return Extrinsic::Graphics::ComputeClusterGridBuildDispatchPlan(
            Extrinsic::Graphics::ComputeClusterGridDesc(width, height));
    }

    [[nodiscard]] const Extrinsic::RHI::GpuSceneTable* FindLastSceneTableWrite(
        const Extrinsic::Tests::MockDevice& device,
        const Extrinsic::RHI::BufferHandle sceneTableBuffer)
    {
        for (auto it = device.BufferWrites.rbegin(); it != device.BufferWrites.rend(); ++it)
        {
            if (it->Handle == sceneTableBuffer &&
                it->Offset == 0u &&
                it->Data.size() == sizeof(Extrinsic::RHI::GpuSceneTable))
            {
                return reinterpret_cast<const Extrinsic::RHI::GpuSceneTable*>(it->Data.data());
            }
        }
        return nullptr;
    }

    [[nodiscard]] std::string ReadShaderSource(const std::string_view relativePath)
    {
        const std::filesystem::path path =
            Extrinsic::Core::Filesystem::GetRoot() / "assets" / "shaders" /
            std::filesystem::path{relativePath};
        std::ifstream input{path};
        EXPECT_TRUE(input.is_open()) << path.string();
        std::ostringstream text;
        text << input.rdbuf();
        return text.str();
    }

    void ExpectMat4Near(const glm::mat4& actual,
                        const glm::mat4& expected,
                        const float epsilon)
    {
        for (int column = 0; column < 4; ++column)
        {
            for (int row = 0; row < 4; ++row)
            {
                EXPECT_NEAR(actual[column][row], expected[column][row], epsilon)
                    << "column=" << column << " row=" << row;
            }
        }
    }

    void ExpectVec4Near(const glm::vec4& actual,
                        const glm::vec4& expected,
                        const float epsilon)
    {
        EXPECT_NEAR(actual.x, expected.x, epsilon);
        EXPECT_NEAR(actual.y, expected.y, epsilon);
        EXPECT_NEAR(actual.z, expected.z, epsilon);
        EXPECT_NEAR(actual.w, expected.w, epsilon);
    }

    class NativeTimestampProfiler final : public Extrinsic::RHI::IProfiler
    {
    public:
        enum class EventKind : std::uint8_t
        {
            BeginQueue,
            BeginScope,
            EndScope,
            EndQueue,
        };

        struct Event
        {
            EventKind Kind{EventKind::BeginQueue};
            Extrinsic::RHI::QueueAffinity Queue{
                Extrinsic::RHI::QueueAffinity::Graphics};
            std::uint32_t ScopeIndex{
                Extrinsic::RHI::ProfilerScopeToken::InvalidIndex};
        };

        struct EndFrameRecord
        {
            Extrinsic::RHI::ProfilerFrameKey Frame{};
            Extrinsic::RHI::ProfilerFrameDisposition Disposition{
                Extrinsic::RHI::ProfilerFrameDisposition::Discarded};
        };

        Extrinsic::RHI::ProfilerStatusSnapshot Status{
            .Status = Extrinsic::RHI::ProfilerBackendStatus::Ready,
            .Source = Extrinsic::RHI::GpuTimestampSource::NativeGpu,
            .Diagnostic = "test native timestamps ready",
        };
        std::uint32_t FramesInFlight{1u};
        std::optional<Extrinsic::RHI::ProfilerError> BeginFrameFailure{};
        std::optional<std::uint32_t> FailBeginScopeIndex{};
        std::optional<Extrinsic::RHI::ProfilerError> ResolveFailure{};
        std::vector<Extrinsic::RHI::ProfilerScopeDesc> PlannedScopes{};
        std::vector<Extrinsic::RHI::ProfilerScopeToken> BeginScopeAttempts{};
        std::vector<Extrinsic::RHI::ProfilerScopeToken> EndScopeCalls{};
        std::vector<Event> Events{};
        std::vector<EndFrameRecord> EndFrameCalls{};
        std::uint32_t BeginFrameCalls{0u};
        mutable std::uint32_t ResolveCalls{0u};

        [[nodiscard]] std::expected<Extrinsic::RHI::ProfilerFramePlan,
                                    Extrinsic::RHI::ProfilerError>
        BeginFrame(
            const Extrinsic::RHI::ProfilerFrameKey frame,
            const std::span<const Extrinsic::RHI::ProfilerScopeDesc> scopes)
            override
        {
            std::scoped_lock lock{Mutex};
            ++BeginFrameCalls;
            if (BeginFrameFailure.has_value())
            {
                return std::unexpected{*BeginFrameFailure};
            }
            if (ActiveFrame.has_value())
            {
                return std::unexpected{
                    Extrinsic::RHI::ProfilerError::InvalidState};
            }
            ActiveFrame = frame;
            ActiveScopes.assign(scopes.begin(), scopes.end());
            PlannedScopes = ActiveScopes;
            ++Generation;

            Extrinsic::RHI::ProfilerFramePlan plan{
                .Frame = frame,
            };
            plan.ScopeTokens.reserve(scopes.size());
            for (std::uint32_t index = 0u;
                 index < scopes.size();
                 ++index)
            {
                plan.ScopeTokens.push_back(
                    Extrinsic::RHI::ProfilerScopeToken{
                        .PlanGeneration = Generation,
                        .ScopeIndex = index,
                    });
            }
            return plan;
        }

        [[nodiscard]] std::expected<void, Extrinsic::RHI::ProfilerError>
        BeginQueue(
            Extrinsic::RHI::ICommandContext&,
            const Extrinsic::RHI::QueueAffinity queue) override
        {
            std::scoped_lock lock{Mutex};
            if (!ActiveFrame.has_value())
            {
                return std::unexpected{
                    Extrinsic::RHI::ProfilerError::InvalidState};
            }
            Events.push_back(Event{
                .Kind = EventKind::BeginQueue,
                .Queue = queue,
            });
            return {};
        }

        [[nodiscard]] std::expected<void, Extrinsic::RHI::ProfilerError>
        EndQueue(
            Extrinsic::RHI::ICommandContext&,
            const Extrinsic::RHI::QueueAffinity queue) override
        {
            std::scoped_lock lock{Mutex};
            if (!ActiveFrame.has_value())
            {
                return std::unexpected{
                    Extrinsic::RHI::ProfilerError::InvalidState};
            }
            Events.push_back(Event{
                .Kind = EventKind::EndQueue,
                .Queue = queue,
            });
            return {};
        }

        [[nodiscard]] std::expected<void, Extrinsic::RHI::ProfilerError>
        BeginScope(
            Extrinsic::RHI::ICommandContext&,
            const Extrinsic::RHI::ProfilerScopeToken scope) override
        {
            std::scoped_lock lock{Mutex};
            BeginScopeAttempts.push_back(scope);
            if (!ActiveFrame.has_value() ||
                scope.PlanGeneration != Generation ||
                scope.ScopeIndex >= ActiveScopes.size())
            {
                return std::unexpected{
                    Extrinsic::RHI::ProfilerError::InvalidArgument};
            }
            if (FailBeginScopeIndex == scope.ScopeIndex)
            {
                return std::unexpected{
                    Extrinsic::RHI::ProfilerError::InvalidState};
            }
            Events.push_back(Event{
                .Kind = EventKind::BeginScope,
                .Queue = ActiveScopes[scope.ScopeIndex].Queue,
                .ScopeIndex = scope.ScopeIndex,
            });
            return {};
        }

        [[nodiscard]] std::expected<void, Extrinsic::RHI::ProfilerError>
        EndScope(
            Extrinsic::RHI::ICommandContext&,
            const Extrinsic::RHI::ProfilerScopeToken scope) override
        {
            std::scoped_lock lock{Mutex};
            if (!ActiveFrame.has_value() ||
                scope.PlanGeneration != Generation ||
                scope.ScopeIndex >= ActiveScopes.size())
            {
                return std::unexpected{
                    Extrinsic::RHI::ProfilerError::InvalidArgument};
            }
            EndScopeCalls.push_back(scope);
            Events.push_back(Event{
                .Kind = EventKind::EndScope,
                .Queue = ActiveScopes[scope.ScopeIndex].Queue,
                .ScopeIndex = scope.ScopeIndex,
            });
            return {};
        }

        [[nodiscard]] std::expected<void, Extrinsic::RHI::ProfilerError>
        EndFrame(
            const Extrinsic::RHI::ProfilerFrameKey frame,
            const Extrinsic::RHI::ProfilerFrameDisposition disposition)
            override
        {
            std::scoped_lock lock{Mutex};
            EndFrameCalls.push_back(EndFrameRecord{
                .Frame = frame,
                .Disposition = disposition,
            });
            if (!ActiveFrame.has_value() ||
                *ActiveFrame != frame)
            {
                return std::unexpected{
                    Extrinsic::RHI::ProfilerError::InvalidState};
            }
            if (disposition ==
                Extrinsic::RHI::ProfilerFrameDisposition::Submitted)
            {
                Extrinsic::RHI::GpuTimestampFrame result{
                    .Frame = frame,
                    .Source =
                        Extrinsic::RHI::GpuTimestampSource::NativeGpu,
                };
                for (const auto& scope : ActiveScopes)
                {
                    if (std::ranges::none_of(
                            result.QueueEnvelopes,
                            [&scope](const auto& envelope)
                            {
                                return envelope.Queue == scope.Queue;
                            }))
                    {
                        result.QueueEnvelopes.push_back(
                            Extrinsic::RHI::GpuTimestampQueueEnvelope{
                                .Queue = scope.Queue,
                                .Source = Extrinsic::RHI::
                                    GpuTimestampSource::NativeGpu,
                                .DurationNs =
                                    100'000u +
                                    static_cast<std::uint64_t>(
                                        scope.Queue),
                            });
                    }
                    result.Scopes.push_back(
                        Extrinsic::RHI::GpuTimestampScope{
                            .Ordinal = scope.Ordinal,
                            .Name = scope.Name,
                            .Queue = scope.Queue,
                            .Source = Extrinsic::RHI::
                                GpuTimestampSource::NativeGpu,
                            .DurationNs =
                                1'000u + scope.Ordinal,
                        });
                }
                SubmittedResult = std::move(result);
            }
            ActiveFrame.reset();
            ActiveScopes.clear();
            return {};
        }

        [[nodiscard]] std::expected<
            Extrinsic::RHI::GpuTimestampFrame,
            Extrinsic::RHI::ProfilerError>
        Resolve(const Extrinsic::RHI::ProfilerFrameKey frame) const override
        {
            std::scoped_lock lock{Mutex};
            ++ResolveCalls;
            if (ResolveFailure.has_value())
            {
                return std::unexpected{*ResolveFailure};
            }
            if (!SubmittedResult.has_value() ||
                SubmittedResult->Frame != frame)
            {
                return std::unexpected{
                    Extrinsic::RHI::ProfilerError::NotReady};
            }
            return *SubmittedResult;
        }

        [[nodiscard]] Extrinsic::RHI::ProfilerStatusSnapshot
        GetStatus() const override
        {
            std::scoped_lock lock{Mutex};
            return Status;
        }

        [[nodiscard]] std::uint32_t
        GetFramesInFlight() const override
        {
            return FramesInFlight;
        }

    private:
        mutable std::mutex Mutex{};
        std::uint64_t Generation{0u};
        std::optional<Extrinsic::RHI::ProfilerFrameKey> ActiveFrame{};
        std::vector<Extrinsic::RHI::ProfilerScopeDesc> ActiveScopes{};
        std::optional<Extrinsic::RHI::GpuTimestampFrame> SubmittedResult{};
    };
}

TEST(RendererFrameLifecycle, ActiveGpuSceneVertexShadersUseSceneTableCameraViewProjection)
{
    constexpr std::string_view kCameraAwareVertexShaders[] = {
        "forward/default_debug_surface.vert",
        "depth_prepass.vert",
        "forward/line.vert",
        "forward/point.vert",
        "selection/entity_id.vert",
        "selection/face_id.vert",
        "selection/edge_id.vert",
        "selection/point_id.vert",
    };

    for (const std::string_view shader : kCameraAwareVertexShaders)
    {
        SCOPED_TRACE(std::string{shader});
        const std::string source = ReadShaderSource(shader);
        EXPECT_NE(source.find("scene.CameraViewProj * dyn.Model"), std::string::npos);
        EXPECT_EQ(source.find("gl_Position = dyn.Model *"), std::string::npos);
    }
}

TEST(RendererFrameLifecycle, FrameRecipeOverrideProjectionDisablesPostProcess)
{
    Extrinsic::Graphics::FrameRecipeFeatures defaults{};
    defaults.EnablePostProcess = true;
    defaults.EnableAntiAliasing = true;
    defaults.EnableDebugView = true;
    defaults.EnablePicking = true;
    defaults.LightingPath = Extrinsic::Graphics::FrameRecipeLightingPath::Deferred;
    defaults.EnableClusterGridBuild = true;
    defaults.EnableClusterLightAssignment = true;

    const Extrinsic::Graphics::FrameRecipeOverrideProjection projection =
        Extrinsic::Graphics::ProjectFrameRecipeOverride(
            defaults,
            MakeRecipeOverride({"postprocess"}));

    EXPECT_TRUE(projection.Diagnostics.empty());
    EXPECT_TRUE(projection.Applied);
    EXPECT_EQ(projection.DisabledSlotCount, 1u);
    EXPECT_FALSE(projection.Features.EnablePostProcess);
    EXPECT_FALSE(projection.Features.EnableAntiAliasing);
    EXPECT_TRUE(projection.Features.EnableDebugView);
    EXPECT_TRUE(projection.Features.EnablePicking);
    EXPECT_EQ(projection.Features.LightingPath,
              Extrinsic::Graphics::FrameRecipeLightingPath::Deferred);
}

TEST(RendererFrameLifecycle, FrameRecipeOverrideProjectionDisablesMappedFeatureSlots)
{
    Extrinsic::Graphics::FrameRecipeFeatures defaults{};
    defaults.EnablePostProcess = true;
    defaults.EnableAntiAliasing = true;
    defaults.EnableDebugView = true;
    defaults.EnablePicking = true;
    defaults.LightingPath = Extrinsic::Graphics::FrameRecipeLightingPath::Deferred;

    const Extrinsic::Graphics::FrameRecipeOverrideProjection projection =
        Extrinsic::Graphics::ProjectFrameRecipeOverride(
            defaults,
            MakeRecipeOverride({"debug-view", "picking", "lighting", "postprocess"}));

    EXPECT_TRUE(projection.Diagnostics.empty());
    EXPECT_TRUE(projection.Applied);
    EXPECT_EQ(projection.DisabledSlotCount, 4u);
    EXPECT_FALSE(projection.Features.EnablePostProcess);
    EXPECT_FALSE(projection.Features.EnableAntiAliasing);
    EXPECT_FALSE(projection.Features.EnableDebugView);
    EXPECT_FALSE(projection.Features.EnablePicking);
    EXPECT_EQ(projection.Features.LightingPath,
              Extrinsic::Graphics::FrameRecipeLightingPath::Forward);
    EXPECT_FALSE(projection.Features.EnableClusterGridBuild);
    EXPECT_FALSE(projection.Features.EnableClusterLightAssignment);
}

TEST(RendererFrameLifecycle, FrameRecipeOverrideProjectionFailsClosedForUnknownSlot)
{
    Extrinsic::Graphics::FrameRecipeFeatures defaults{};
    defaults.EnablePostProcess = true;
    defaults.EnableAntiAliasing = true;

    const Extrinsic::Graphics::FrameRecipeOverrideProjection projection =
        Extrinsic::Graphics::ProjectFrameRecipeOverride(
            defaults,
            MakeRecipeOverride({"ray-traced-gi"}));

    ASSERT_EQ(projection.Diagnostics.size(), 1u);
    EXPECT_EQ(projection.Diagnostics.front().Code,
              Extrinsic::Graphics::FrameRecipeOverrideDiagnosticCode::UnknownSlot);
    EXPECT_FALSE(projection.Applied);
    EXPECT_EQ(projection.DisabledSlotCount, 0u);
    EXPECT_TRUE(projection.Features.EnablePostProcess);
    EXPECT_TRUE(projection.Features.EnableAntiAliasing);
}

TEST(RendererFrameLifecycle, FrameRecipeOverrideProjectionRejectsUnmappedExtensionDisable)
{
    Extrinsic::Graphics::FrameRecipeFeatures defaults{};
    defaults.EnableDebugView = true;

    const Extrinsic::Graphics::FrameRecipeOverrideProjection projection =
        Extrinsic::Graphics::ProjectFrameRecipeOverride(
            defaults,
            MakeRecipeOverride({"visibility"}));

    ASSERT_EQ(projection.Diagnostics.size(), 1u);
    EXPECT_TRUE(HasProjectionDiagnostic(
        projection,
        Extrinsic::Graphics::FrameRecipeOverrideDiagnosticCode::UnsupportedSlotDisable,
        "visibility"));
    EXPECT_FALSE(projection.Applied);
    EXPECT_EQ(projection.DisabledSlotCount, 0u);
    EXPECT_TRUE(projection.Features.EnableDebugView);
}

TEST(RendererFrameLifecycle, FrameRecipeOverrideProjectionFailsClosedForFixedCoreChanges)
{
    Extrinsic::Graphics::FrameRecipeFeatures defaults{};
    defaults.EnableDebugView = true;
    defaults.EnablePicking = true;
    defaults.LightingPath = Extrinsic::Graphics::FrameRecipeLightingPath::Deferred;

    Extrinsic::Graphics::FrameRecipeOverride override =
        MakeRecipeOverride({"debug-view", "default-frame-core"});
    override.Recipe.FixedCoreName = "Extrinsic.Graphics.FrameRecipe.Experimental";
    for (Extrinsic::Graphics::RecipeExtensionSlotDescriptor& slot : override.Recipe.Slots)
    {
        if (slot.StableName == "default-frame-core")
        {
            slot.SchemaId = "intrinsic.graphics.experimental-frame-core/v1";
            break;
        }
    }

    const Extrinsic::Graphics::FrameRecipeOverrideProjection projection =
        Extrinsic::Graphics::ProjectFrameRecipeOverride(defaults, override);

    EXPECT_TRUE(HasProjectionDiagnostic(
        projection,
        Extrinsic::Graphics::FrameRecipeOverrideDiagnosticCode::FixedCoreMutation,
        "Extrinsic.Graphics.FrameRecipe.Experimental"));
    EXPECT_TRUE(HasProjectionDiagnostic(
        projection,
        Extrinsic::Graphics::FrameRecipeOverrideDiagnosticCode::FixedCoreMutation,
        "default-frame-core"));
    EXPECT_TRUE(HasProjectionDiagnostic(
        projection,
        Extrinsic::Graphics::FrameRecipeOverrideDiagnosticCode::FixedCoreSlotDisabled,
        "default-frame-core"));
    EXPECT_FALSE(projection.Applied);
    EXPECT_EQ(projection.DisabledSlotCount, 0u);
    EXPECT_TRUE(projection.Features.EnableDebugView);
    EXPECT_TRUE(projection.Features.EnablePicking);
    EXPECT_EQ(projection.Features.LightingPath,
              Extrinsic::Graphics::FrameRecipeLightingPath::Deferred);
}

TEST(RendererFrameLifecycle, PrepareFramePublishesCameraIntoGpuSceneTable)
{
    Extrinsic::Tests::MockDevice device;
    device.Operational = true;
    device.NextFrame = Extrinsic::RHI::FrameHandle{.FrameIndex = 37u, .SwapchainImageIndex = 1u};

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer =
        Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);

    Extrinsic::RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));
    EXPECT_EQ(frame.FrameIndex, 37u);

    const glm::vec3 position{1.0f, 2.0f, 6.0f};
    const glm::vec3 forward = glm::normalize(glm::vec3{-0.25f, -0.15f, -1.0f});
    const glm::vec3 up{0.0f, 1.0f, 0.0f};
    Extrinsic::Graphics::CameraViewInput camera{};
    camera.Position = position;
    camera.Forward = forward;
    camera.Up = up;
    camera.View = glm::lookAt(position, position + forward, up);
    camera.Projection = glm::perspective(glm::radians(55.0f), 16.0f / 9.0f, 0.25f, 250.0f);
    camera.Projection[1][1] *= -1.0f;
    camera.NearPlane = 0.25f;
    camera.FarPlane = 250.0f;
    camera.Valid = true;
    camera.ExplicitCameraTransition = true;

    const Extrinsic::Graphics::RenderFrameInput input{
        .Viewport = {.Width = 640, .Height = 360},
        .Camera = camera,
    };
    Extrinsic::Graphics::RenderWorld world = renderer->ExtractRenderWorld(input);
    ASSERT_TRUE(world.Camera.Valid);
    renderer->PrepareFrame(world);

    const Extrinsic::RHI::GpuSceneTable* sceneTable =
        FindLastSceneTableWrite(device, renderer->GetGpuWorld().GetSceneTableBuffer());
    ASSERT_NE(sceneTable, nullptr);

    ExpectMat4Near(sceneTable->CameraView, camera.View, 0.0001f);
    ExpectMat4Near(sceneTable->CameraProj, camera.Projection, 0.0001f);
    ExpectMat4Near(sceneTable->CameraViewProj, camera.Projection * camera.View, 0.0001f);
    ExpectMat4Near(sceneTable->CameraInvView, glm::inverse(camera.View), 0.0001f);
    ExpectMat4Near(sceneTable->CameraInvProj, glm::inverse(camera.Projection), 0.0001f);
    ExpectVec4Near(sceneTable->CameraPosition, glm::vec4{position, 0.0f}, 0.0001f);
    ExpectVec4Near(sceneTable->CameraDirection, glm::vec4{forward, 0.0f}, 0.0001f);
    EXPECT_FLOAT_EQ(sceneTable->CameraViewportWidth, 640.0f);
    EXPECT_FLOAT_EQ(sceneTable->CameraViewportHeight, 360.0f);
    EXPECT_FLOAT_EQ(sceneTable->CameraNearPlane, 0.25f);
    EXPECT_FLOAT_EQ(sceneTable->CameraFarPlane, 250.0f);
    EXPECT_EQ(sceneTable->CameraFrameIndex, 37u);
    EXPECT_EQ(sceneTable->CameraCullingFlags,
              Extrinsic::RHI::CameraCulling_ExplicitTransition);

    renderer->Shutdown();
}

TEST(RendererFrameLifecycle, ActiveFrameRecipeOverrideOmitsPostProcessPasses)
{
    Extrinsic::Tests::MockDevice device;
    device.Operational = true;
    device.NextFrame = Extrinsic::RHI::FrameHandle{.FrameIndex = 17u, .SwapchainImageIndex = 0u};
    device.BackbufferHandle = Extrinsic::RHI::TextureHandle{177u, 1u};

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer =
        Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);
    renderer->SetActiveFrameRecipeOverride(
        std::make_optional(MakeRecipeOverride({"postprocess"})));

    Extrinsic::RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));
    const Extrinsic::Graphics::RenderFrameInput input{
        .Viewport = {.Width = 320, .Height = 240},
    };
    Extrinsic::Graphics::RenderWorld world = renderer->ExtractRenderWorld(input);
    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(frame, world);

    const Extrinsic::Graphics::RenderGraphFrameStats& stats =
        renderer->GetLastRenderGraphStats();
    EXPECT_TRUE(stats.Compile.Succeeded) << stats.Diagnostic;
    EXPECT_TRUE(stats.FrameRecipeOverrideActive);
    EXPECT_TRUE(stats.FrameRecipeOverrideApplied);
    EXPECT_EQ(stats.FrameRecipeOverrideDisabledSlotCount, 1u);
    EXPECT_EQ(stats.FrameRecipeOverrideDiagnosticCount, 0u);
    EXPECT_EQ(FindCommandPass(stats, "PostProcessHistogramPass"), nullptr);
    EXPECT_EQ(FindCommandPass(stats, "PostProcessPass"), nullptr);
    EXPECT_EQ(FindCommandPass(stats, "PostProcessAAEdgePass"), nullptr);
    EXPECT_EQ(FindCommandPass(stats, "PostProcessAABlendPass"), nullptr);
    EXPECT_EQ(FindCommandPass(stats, "PostProcessAAResolvePass"), nullptr);
    ASSERT_NE(FindCommandPass(stats, "Present"), nullptr);

    renderer->Shutdown();
}

TEST(RendererFrameLifecycle, ActiveFrameRecipeOverrideDiagnosticsLeaveDefaultsUntouched)
{
    Extrinsic::Tests::MockDevice device;
    device.Operational = true;
    device.NextFrame = Extrinsic::RHI::FrameHandle{.FrameIndex = 18u, .SwapchainImageIndex = 0u};
    device.BackbufferHandle = Extrinsic::RHI::TextureHandle{178u, 1u};

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer =
        Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);
    renderer->SetActiveFrameRecipeOverride(
        std::make_optional(MakeRecipeOverride({"ray-traced-gi"})));

    Extrinsic::RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));
    const Extrinsic::Graphics::RenderFrameInput input{
        .Viewport = {.Width = 320, .Height = 240},
    };
    Extrinsic::Graphics::RenderWorld world = renderer->ExtractRenderWorld(input);
    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(frame, world);

    const Extrinsic::Graphics::RenderGraphFrameStats& stats =
        renderer->GetLastRenderGraphStats();
    EXPECT_TRUE(stats.Compile.Succeeded) << stats.Diagnostic;
    EXPECT_TRUE(stats.FrameRecipeOverrideActive);
    EXPECT_FALSE(stats.FrameRecipeOverrideApplied);
    EXPECT_EQ(stats.FrameRecipeOverrideDisabledSlotCount, 0u);
    ASSERT_EQ(stats.FrameRecipeOverrideDiagnosticCount, 1u);
    ASSERT_EQ(stats.FrameRecipeOverrideDiagnostics.size(), 1u);
    EXPECT_EQ(stats.FrameRecipeOverrideDiagnostics.front().Code,
              Extrinsic::Graphics::FrameRecipeOverrideDiagnosticCode::UnknownSlot);
    ASSERT_NE(FindCommandPass(stats, "PostProcessPass"), nullptr);
    ASSERT_NE(FindCommandPass(stats, "PostProcessHistogramPass"), nullptr);

    renderer->Shutdown();
}

TEST(RendererFrameLifecycle, UsesDeviceFrameLifecycleBackbufferAndCommandContext)
{
    Extrinsic::Tests::MockDevice device;
    device.NextFrame = Extrinsic::RHI::FrameHandle{.FrameIndex = 5u, .SwapchainImageIndex = 2u};
    device.BackbufferHandle = Extrinsic::RHI::TextureHandle{77u, 3u};

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer = Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);

    Extrinsic::RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));
    EXPECT_EQ(device.BeginFrameCount, 1);
    EXPECT_EQ(frame.FrameIndex, 5u);
    EXPECT_EQ(frame.SwapchainImageIndex, 2u);

    const Extrinsic::Graphics::RenderFrameInput input{
        .Viewport = {.Width = 320, .Height = 240},
    };
    const Extrinsic::Graphics::HZBBuildDispatchPlan hzbPlan =
        ExpectedFallbackHZBPlan(320u, 240u);
    ASSERT_TRUE(hzbPlan.IsValid());
    const Extrinsic::Graphics::ClusterGridBuildDispatchPlan clusterPlan =
        ExpectedClusterGridPlan(320u, 240u);
    ASSERT_TRUE(clusterPlan.IsValid());
    Extrinsic::Graphics::RenderWorld world = renderer->ExtractRenderWorld(input);
    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(frame, world);

    const Extrinsic::Graphics::RenderGraphFrameStats& stats = renderer->GetLastRenderGraphStats();
    EXPECT_TRUE(stats.Compile.Succeeded) << stats.Diagnostic;
    EXPECT_TRUE(stats.Execute.Succeeded) << stats.Diagnostic;
    EXPECT_TRUE(stats.Execute.DeviceOperational);
    // GRAPHICS-071 — the default-recipe retained forward surface/line/point
    // passes record their bind/draw shape under the forward lighting path.
    // GRAPHICS-075 Slice A — the default-recipe `"PostProcessPass"` umbrella
    // branch now routes the tonemap leg as well, adding one more Recorded
    // pass + one bind + one push (canonical `PostProcessPushConstants`).
    // GRAPHICS-075 Slice B.1 — the umbrella branch now fans out to the
    // bloom helper *before* the tonemap helper. `EnableBloom` defaults to
    // false so the bloom `Execute` body emits no bind/push/draw, but the
    // helper still returns `Recorded` per the same "structurally-recorded
    // no-op" taxonomy the tonemap helper follows when the chain is
    // disabled. GRAPHICS-075 Slice C — FXAA runs in its *own* ordered
    // graph pass so its `SceneColorLDR` read crosses a real framegraph
    // read-after-write barrier rather than aliasing the umbrella's color
    // attachment. GRAPHICS-075 Slice D.2a — the AA umbrella splits into
    // three ordered graph passes (`"PostProcessAA{Edge,Blend,Resolve}Pass"`)
    // so edge / blend / resolve pipelines can target format-incompatible
    // color attachments. SMAA records under all three; FXAA records
    // under the resolve pass only. `AntiAliasing` defaults to `None` so
    // every AA pass body emits no bind/push/draw (the per-stage helpers
    // still return `Recorded` per the structurally-recorded-no-op
    // taxonomy). GRAPHICS-075 Slice E.1 — the histogram compute
    // dispatch lives in its own ordered graph pass before
    // `"PostProcessPass"` (Vulkan rejects dispatches inside an active
    // render-pass scope, and `"PostProcessPass"` is a render-pass-scope
    // pass — bloom + tonemap write color attachments). With
    // `EnableHistogram == false` the body short-circuits but the helper
    // still reports `Recorded` per the structurally-recorded-no-op
    // taxonomy. GRAPHICS-076 Slice A — the canonical `Pass.Present`
    // now records under the default recipe (BindPipeline + Draw(3,1,0,0)
    // fullscreen finalizer), bumping Recorded by one. Total Recorded
    // GRAPHICS-039C — the clustered-light grid build and assignment passes
    // now record between HZB and lighting when their retained buffers are
    // published through `GpuSceneTable`.
    // entries: 8 routed (Culling/Depth/HZB/ClusterGrid/LightAssign/Surface/Line/Point) + 1 under
    // `"PostProcessHistogramPass"` + 2 under `"PostProcessPass"`
    // (bloom + tonemap) + 3 under the per-stage AA passes
    // (edge / blend / resolve helpers) + 1 under `"Present"` = 15.
    // Remaining unwired passes still soft-skip with SkippedUnavailable.
    EXPECT_EQ(stats.CommandRecords.Recorded, 12u);
    EXPECT_EQ(stats.CommandRecords.SkippedNonOperational, 0u);
    EXPECT_EQ(stats.CommandRecords.Skipped, stats.CommandRecords.SkippedUnavailable);
    EXPECT_GE(stats.CommandRecords.SkippedUnavailable, 1u);
    const auto* cullingPass = FindCommandPass(stats, "CullingPass");
    ASSERT_NE(cullingPass, nullptr);
    EXPECT_EQ(cullingPass->Id,
              Extrinsic::Graphics::ToFramePassId(Extrinsic::Graphics::FrameRecipePassKind::Culling));
    EXPECT_EQ(cullingPass->Status,
              Extrinsic::Graphics::RenderCommandPassStatus::Recorded);
    const auto* depthPrepass = FindCommandPass(stats, "DepthPrepass");
    ASSERT_NE(depthPrepass, nullptr);
    EXPECT_EQ(depthPrepass->Id,
              Extrinsic::Graphics::ToFramePassId(Extrinsic::Graphics::FrameRecipePassKind::DepthPrepass));
    EXPECT_EQ(depthPrepass->Status,
              Extrinsic::Graphics::RenderCommandPassStatus::Recorded);
    ASSERT_NE(FindCommandPass(stats, "HZBBuildPass"), nullptr);
    EXPECT_EQ(FindCommandPass(stats, "HZBBuildPass")->Status,
              Extrinsic::Graphics::RenderCommandPassStatus::Recorded);
    ASSERT_NE(FindCommandPass(stats, "ClusterGridBuildPass"), nullptr);
    EXPECT_EQ(FindCommandPass(stats, "ClusterGridBuildPass")->Status,
              Extrinsic::Graphics::RenderCommandPassStatus::Recorded);
    ASSERT_NE(FindCommandPass(stats, "LightClusterAssignmentPass"), nullptr);
    EXPECT_EQ(FindCommandPass(stats, "LightClusterAssignmentPass")->Status,
              Extrinsic::Graphics::RenderCommandPassStatus::Recorded);
    const auto* surfacePass = FindCommandPass(stats, "SurfacePass");
    ASSERT_NE(surfacePass, nullptr);
    EXPECT_EQ(surfacePass->Id,
              Extrinsic::Graphics::ToFramePassId(Extrinsic::Graphics::FrameRecipePassKind::Surface));
    EXPECT_EQ(surfacePass->Status,
              Extrinsic::Graphics::RenderCommandPassStatus::Recorded);
    ASSERT_NE(FindCommandPass(stats, "LinePass"), nullptr);
    EXPECT_EQ(FindCommandPass(stats, "LinePass")->Status,
              Extrinsic::Graphics::RenderCommandPassStatus::Recorded);
    ASSERT_NE(FindCommandPass(stats, "PointPass"), nullptr);
    EXPECT_EQ(FindCommandPass(stats, "PointPass")->Status,
              Extrinsic::Graphics::RenderCommandPassStatus::Recorded);
    ASSERT_NE(FindCommandPass(stats, "PostProcessHistogramPass"), nullptr);
    EXPECT_EQ(FindCommandPass(stats, "PostProcessHistogramPass")->Status,
              Extrinsic::Graphics::RenderCommandPassStatus::Recorded);
    ASSERT_NE(FindCommandPass(stats, "PostProcessPass"), nullptr);
    EXPECT_EQ(FindCommandPass(stats, "PostProcessPass")->Status,
              Extrinsic::Graphics::RenderCommandPassStatus::Recorded);
    EXPECT_EQ(FindCommandPass(stats, "PostProcessAAEdgePass"), nullptr);
    EXPECT_EQ(FindCommandPass(stats, "PostProcessAABlendPass"), nullptr);
    EXPECT_EQ(FindCommandPass(stats, "PostProcessAAResolvePass"), nullptr);
    // GRAPHICS-076 Slice A — `Pass.Present` records the canonical
    // fullscreen `BindPipeline + Draw(3, 1, 0, 0)` shape on the
    // operational CPU/null path, replacing the previous
    // `SkippedUnavailable` taxonomy.
    const auto* presentPass = FindCommandPass(stats, "Present");
    ASSERT_NE(presentPass, nullptr);
    EXPECT_EQ(presentPass->Id,
              Extrinsic::Graphics::ToFramePassId(Extrinsic::Graphics::FrameRecipePassKind::Present));
    EXPECT_EQ(presentPass->Status,
              Extrinsic::Graphics::RenderCommandPassStatus::Recorded);
    EXPECT_EQ(device.GetBackbufferHandleCount, 1);
    EXPECT_EQ(device.LastBackbufferFrame.FrameIndex, frame.FrameIndex);
    EXPECT_EQ(device.LastBackbufferFrame.SwapchainImageIndex, frame.SwapchainImageIndex);
    EXPECT_EQ(device.CommandContext.BeginCalls, 1);
    EXPECT_EQ(device.CommandContext.EndCalls, 1);
    EXPECT_TRUE(ContainsTextureBarrier(device.CommandContext, device.BackbufferHandle));
    EXPECT_GE(device.CommandContext.FillBufferCalls, 9);
    // GRAPHICS-071 — culling pipeline plus depth/surface/line/point draw
    // pipelines each bind once. The draw passes all carry scene push
    // constants. GRAPHICS-075 Slice A adds the tonemap fullscreen bind +
    // 80-byte `PostProcessToneMapPushConstants` push. GRAPHICS-075
    // Slice B.1 / Slice C — `EnableBloom` and `AntiAliasing` default to
    // false / `None`, so the bloom + FXAA `Execute` bodies emit no
    // bind/push/draw under the default settings (their helpers still
    // report `Recorded` under the umbrella's accumulator).
    // GRAPHICS-038B — the HZB build pass adds one compute pipeline bind and
    // one 32-byte push/dispatch per fallback mip. The first dispatch remains
    // culling; HZB dispatches follow after `DepthPrepass`.
    // GRAPHICS-039C — cluster grid build and light assignment add two compute
    // pipeline binds, two pushes, and two dispatches after the HZB band.
    // GRAPHICS-076 Slice A — the canonical `Pass.Present` finalizer adds
    // one fullscreen `BindPipeline` (the present pipeline) but no
    // additional push constant since `BuildPresentPipelineDesc()` pins
    // `PushConstantSize = 0u`.
    EXPECT_EQ(device.CommandContext.BindPipelineCalls, 10);
    EXPECT_EQ(device.CommandContext.PushConstantsCalls,
              static_cast<int>(8u + hzbPlan.Dispatches.size()));
    ASSERT_EQ(device.CommandContext.PushConstantSizes.size(),
              8u + hzbPlan.Dispatches.size());
    EXPECT_EQ(CountPushConstantSize(device.CommandContext,
                                    sizeof(Extrinsic::Graphics::ClusterGridBuildPushConstants)),
              2u);
    EXPECT_EQ(CountPushConstantSize(device.CommandContext,
                                    sizeof(Extrinsic::Graphics::PostProcessToneMapPushConstants)),
              1u);
    EXPECT_EQ(device.CommandContext.DispatchCalls,
              static_cast<int>(3u + hzbPlan.Dispatches.size()));
    ASSERT_EQ(device.CommandContext.DispatchRecords.size(),
              3u + hzbPlan.Dispatches.size());
    EXPECT_EQ(device.CommandContext.DispatchRecords[0].X, ExpectedCullDispatchGroups());
    EXPECT_EQ(device.CommandContext.DispatchRecords[0].Y, 1u);
    EXPECT_EQ(device.CommandContext.DispatchRecords[0].Z, 1u);
    std::vector<Extrinsic::Tests::MockCommandContext::DispatchRecord> expectedComputeDispatches{};
    expectedComputeDispatches.reserve(hzbPlan.Dispatches.size() + 2u);
    for (std::size_t i = 0u; i < hzbPlan.Dispatches.size(); ++i)
    {
        const auto& expected = hzbPlan.Dispatches[i];
        expectedComputeDispatches.push_back(Extrinsic::Tests::MockCommandContext::DispatchRecord{
            .X = expected.GroupCountX,
            .Y = expected.GroupCountY,
            .Z = expected.GroupCountZ,
        });
    }
    expectedComputeDispatches.push_back(Extrinsic::Tests::MockCommandContext::DispatchRecord{
        .X = clusterPlan.GroupCountX,
        .Y = clusterPlan.GroupCountY,
        .Z = clusterPlan.GroupCountZ,
    });
    expectedComputeDispatches.push_back(Extrinsic::Tests::MockCommandContext::DispatchRecord{
        .X = clusterPlan.GroupCountX,
        .Y = clusterPlan.GroupCountY,
        .Z = clusterPlan.GroupCountZ,
    });
    ExpectDispatchRecordsContainUnordered(device.CommandContext, expectedComputeDispatches, 1u);
    EXPECT_EQ(stats.HZBBuildRecordedFrames, 1u);
    EXPECT_EQ(stats.HZBBuildDispatchCount,
              static_cast<std::uint32_t>(hzbPlan.Dispatches.size()));
    EXPECT_EQ(stats.HZBBuildMipCount, hzbPlan.Desc.MipLevels);
    EXPECT_EQ(stats.HZBBuildFallbackFrames, 1u);
    EXPECT_EQ(stats.HZBBuildSinglePassFrames, 0u);
    EXPECT_EQ(stats.ClusterGridBuildRecordedFrames, 1u);
    EXPECT_EQ(stats.ClusterGridBuildDispatchCount, 1u);
    EXPECT_EQ(stats.ClusterLightAssignmentRecordedFrames, 1u);
    EXPECT_EQ(stats.ClusterLightAssignmentDispatchCount, 1u);
    EXPECT_EQ(device.CommandContext.BindIndexBufferCalls, 2);
    EXPECT_EQ(device.CommandContext.DrawIndexedIndirectCountCalls, 2);
    EXPECT_EQ(device.CommandContext.DrawIndirectCountCalls, 2);
    EXPECT_EQ(device.CommandContext.LastMaxDrawCount, Extrinsic::RHI::kMaxIndirectDrawCount);

    const int dispatchEvent = FindEventIndex(device.CommandContext,
        Extrinsic::Tests::MockCommandContext::EventKind::Dispatch);
    const int drawEvent = FindEventIndex(device.CommandContext,
        Extrinsic::Tests::MockCommandContext::EventKind::DrawIndexedIndirectCount);
    ASSERT_GE(dispatchEvent, 0);
    ASSERT_GE(drawEvent, 0);
    EXPECT_LT(dispatchEvent, drawEvent);

    const std::uint64_t completedFrame = renderer->EndFrame(frame);
    EXPECT_EQ(completedFrame, 1u);
    EXPECT_EQ(device.EndFrameCount, 1);
    EXPECT_EQ(device.PresentCount, 0) << "Runtime remains responsible for presentation.";

    renderer->Shutdown();
}

TEST(RendererFrameLifecycle, PlacedTransientAliasingUsesMemoryBlocksAndAliasBarriers)
{
    Extrinsic::Tests::MockDevice device;
    device.PlacedMemorySupported = true;
    device.NextFrame = Extrinsic::RHI::FrameHandle{.FrameIndex = 3u, .SwapchainImageIndex = 0u};
    device.BackbufferHandle = Extrinsic::RHI::TextureHandle{77u, 3u};

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer =
        Extrinsic::Graphics::CreateRenderer();
    renderer->SetTransientAliasingEnabled(true);
    renderer->Initialize(device);

    Extrinsic::RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));
    const Extrinsic::Graphics::RenderFrameInput input{
        .Viewport = {.Width = 320, .Height = 240},
    };
    Extrinsic::Graphics::RenderWorld world = renderer->ExtractRenderWorld(input);
    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(frame, world);

    const Extrinsic::Graphics::RenderGraphFrameStats& stats =
        renderer->GetLastRenderGraphStats();
    ASSERT_TRUE(stats.Compile.Succeeded) << stats.Diagnostic;
    ASSERT_TRUE(stats.Execute.Succeeded) << stats.Diagnostic;
    EXPECT_GT(stats.Compile.TransientNaiveMemoryEstimateBytes, 0u);
    EXPECT_LT(stats.Compile.TransientPlacedPeakMemoryEstimateBytes,
              stats.Compile.TransientNaiveMemoryEstimateBytes);
    EXPECT_EQ(stats.Compile.TransientMemoryEstimateBytes,
              stats.Compile.TransientPlacedPeakMemoryEstimateBytes);

    EXPECT_GT(device.GetTextureMemoryRequirementsCount +
                  device.GetBufferMemoryRequirementsCount,
              0);
    EXPECT_GT(device.CreateMemoryBlockCount, 0);
    EXPECT_GT(device.CreatePlacedTextureCount + device.CreatePlacedBufferCount, 0);
    EXPECT_FALSE(device.CommandContext.MemoryBarrierCalls.empty());

    renderer->Shutdown();
    EXPECT_EQ(device.DestroyMemoryBlockCount, device.CreateMemoryBlockCount);
}

TEST(RendererFrameLifecycle, PlacedTransientAliasingDefaultsToPerResourceFallback)
{
    Extrinsic::Tests::MockDevice device;
    device.PlacedMemorySupported = true;
    device.NextFrame = Extrinsic::RHI::FrameHandle{.FrameIndex = 4u, .SwapchainImageIndex = 0u};
    device.BackbufferHandle = Extrinsic::RHI::TextureHandle{77u, 3u};

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer =
        Extrinsic::Graphics::CreateRenderer();
    ASSERT_FALSE(renderer->IsTransientAliasingEnabled());
    renderer->Initialize(device);

    Extrinsic::RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));
    const Extrinsic::Graphics::RenderFrameInput input{
        .Viewport = {.Width = 320, .Height = 240},
    };
    Extrinsic::Graphics::RenderWorld world = renderer->ExtractRenderWorld(input);
    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(frame, world);

    const Extrinsic::Graphics::RenderGraphFrameStats& stats =
        renderer->GetLastRenderGraphStats();
    ASSERT_TRUE(stats.Compile.Succeeded) << stats.Diagnostic;
    ASSERT_TRUE(stats.Execute.Succeeded) << stats.Diagnostic;
    EXPECT_GT(stats.Compile.TransientNaiveMemoryEstimateBytes, 0u);
    EXPECT_EQ(stats.Compile.TransientPlacedPeakMemoryEstimateBytes,
              stats.Compile.TransientNaiveMemoryEstimateBytes);
    EXPECT_EQ(stats.Compile.TransientMemoryEstimateBytes,
              stats.Compile.TransientNaiveMemoryEstimateBytes);

    EXPECT_EQ(device.GetTextureMemoryRequirementsCount +
                  device.GetBufferMemoryRequirementsCount,
              0);
    EXPECT_EQ(device.CreateMemoryBlockCount, 0);
    EXPECT_EQ(device.CreatePlacedTextureCount + device.CreatePlacedBufferCount, 0);
    EXPECT_TRUE(device.CommandContext.MemoryBarrierCalls.empty());
    EXPECT_GT(device.CreateTextureCount, 0);

    renderer->Shutdown();
}

TEST(RendererFrameLifecycle, PlacedTransientAliasingFallsBackWhenMemoryBlocksUnavailable)
{
    Extrinsic::Tests::MockDevice device;
    device.PlacedMemorySupported = true;
    device.FailNextMemoryBlockCreate = true;
    device.NextFrame = Extrinsic::RHI::FrameHandle{.FrameIndex = 5u, .SwapchainImageIndex = 0u};
    device.BackbufferHandle = Extrinsic::RHI::TextureHandle{77u, 3u};

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer =
        Extrinsic::Graphics::CreateRenderer();
    renderer->SetTransientAliasingEnabled(true);
    renderer->Initialize(device);

    Extrinsic::RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));
    const Extrinsic::Graphics::RenderFrameInput input{
        .Viewport = {.Width = 320, .Height = 240},
    };
    Extrinsic::Graphics::RenderWorld world = renderer->ExtractRenderWorld(input);
    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(frame, world);

    const Extrinsic::Graphics::RenderGraphFrameStats& stats =
        renderer->GetLastRenderGraphStats();
    ASSERT_TRUE(stats.Compile.Succeeded) << stats.Diagnostic;
    ASSERT_TRUE(stats.Execute.Succeeded) << stats.Diagnostic;
    EXPECT_GT(stats.Compile.TransientNaiveMemoryEstimateBytes, 0u);
    EXPECT_EQ(stats.Compile.TransientPlacedPeakMemoryEstimateBytes,
              stats.Compile.TransientNaiveMemoryEstimateBytes);
    EXPECT_EQ(stats.Compile.TransientMemoryEstimateBytes,
              stats.Compile.TransientNaiveMemoryEstimateBytes);

    EXPECT_GT(device.GetTextureMemoryRequirementsCount +
                  device.GetBufferMemoryRequirementsCount,
              0);
    EXPECT_GT(device.CreateMemoryBlockCount, 0);
    EXPECT_EQ(device.CreatePlacedTextureCount + device.CreatePlacedBufferCount, 0);
    EXPECT_TRUE(device.CommandContext.MemoryBarrierCalls.empty());
    EXPECT_GT(device.CreateTextureCount, 0);

    const int memoryBlockCreateCountAfterFallback = device.CreateMemoryBlockCount;
    const int placedResourceCreateCountAfterFallback =
        device.CreatePlacedTextureCount + device.CreatePlacedBufferCount;
    const std::uint64_t completedFrame = renderer->EndFrame(frame);
    EXPECT_EQ(completedFrame, 1u);

    device.CommandContext.MemoryBarrierCalls.clear();
    device.NextFrame = Extrinsic::RHI::FrameHandle{.FrameIndex = 6u, .SwapchainImageIndex = 0u};

    ASSERT_TRUE(renderer->BeginFrame(frame));
    world = renderer->ExtractRenderWorld(input);
    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(frame, world);

    const Extrinsic::Graphics::RenderGraphFrameStats& retryStats =
        renderer->GetLastRenderGraphStats();
    ASSERT_TRUE(retryStats.Compile.Succeeded) << retryStats.Diagnostic;
    ASSERT_TRUE(retryStats.Execute.Succeeded) << retryStats.Diagnostic;
    EXPECT_LT(retryStats.Compile.TransientPlacedPeakMemoryEstimateBytes,
              retryStats.Compile.TransientNaiveMemoryEstimateBytes);
    EXPECT_EQ(retryStats.Compile.TransientMemoryEstimateBytes,
              retryStats.Compile.TransientPlacedPeakMemoryEstimateBytes);
    EXPECT_GT(device.CreateMemoryBlockCount, memoryBlockCreateCountAfterFallback);
    EXPECT_GT(device.CreatePlacedTextureCount + device.CreatePlacedBufferCount,
              placedResourceCreateCountAfterFallback);
    EXPECT_FALSE(device.CommandContext.MemoryBarrierCalls.empty());

    renderer->Shutdown();
}

TEST(RendererFrameLifecycle, RuntimeFrameCommandHooksRunInsideExecuteFrameCommandContext)
{
    Extrinsic::Tests::MockDevice device;
    device.NextFrame = Extrinsic::RHI::FrameHandle{.FrameIndex = 2u, .SwapchainImageIndex = 0u};
    device.BackbufferHandle = Extrinsic::RHI::TextureHandle{88u, 1u};

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer =
        Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);

    constexpr std::uint32_t kHookDispatchX = 4091u;
    constexpr std::uint32_t kHookDispatchY = 4093u;
    constexpr std::uint32_t kHookDispatchZ = 4099u;
    constexpr std::uint32_t kSecondHookDispatchX = 2027u;
    constexpr std::uint32_t kSecondHookDispatchY = 2029u;
    constexpr std::uint32_t kSecondHookDispatchZ = 2039u;
    std::vector<int> hookOrder{};
    int hookEventStart = -1;
    const Extrinsic::Graphics::RuntimeFrameCommandHookHandle firstHook =
        renderer->RegisterRuntimeFrameCommandHook(
        [&](Extrinsic::RHI::ICommandContext& commandContext)
        {
            hookOrder.push_back(1);
            hookEventStart = static_cast<int>(device.CommandContext.Events.size());
            commandContext.Dispatch(kHookDispatchX, kHookDispatchY, kHookDispatchZ);
        });
    ASSERT_TRUE(firstHook.IsValid());
    const Extrinsic::Graphics::RuntimeFrameCommandHookHandle secondHook =
        renderer->RegisterRuntimeFrameCommandHook(
        [&](Extrinsic::RHI::ICommandContext& commandContext)
        {
            hookOrder.push_back(2);
            commandContext.Dispatch(
                kSecondHookDispatchX,
                kSecondHookDispatchY,
                kSecondHookDispatchZ);
        });
    ASSERT_TRUE(secondHook.IsValid());

    Extrinsic::RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));
    const Extrinsic::Graphics::RenderFrameInput input{
        .Viewport = {.Width = 320, .Height = 240},
    };
    Extrinsic::Graphics::RenderWorld world = renderer->ExtractRenderWorld(input);
    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(frame, world);

    ASSERT_EQ(hookOrder.size(), 2u);
    EXPECT_EQ(hookOrder[0], 1);
    EXPECT_EQ(hookOrder[1], 2);
    ASSERT_FALSE(device.CommandContext.DispatchRecords.empty());
    EXPECT_TRUE(std::ranges::any_of(
        device.CommandContext.DispatchRecords,
        [](const Extrinsic::Tests::MockCommandContext::DispatchRecord& record)
        {
            return DispatchMatches(record, kHookDispatchX, kHookDispatchY, kHookDispatchZ);
        }));
    EXPECT_TRUE(std::ranges::any_of(
        device.CommandContext.DispatchRecords,
        [](const Extrinsic::Tests::MockCommandContext::DispatchRecord& record)
        {
            return DispatchMatches(
                record,
                kSecondHookDispatchX,
                kSecondHookDispatchY,
                kSecondHookDispatchZ);
        }));

    const int beginIndex = FindEventIndex(
        device.CommandContext,
        Extrinsic::Tests::MockCommandContext::EventKind::Begin);
    ASSERT_GE(hookEventStart, 0);
    const int dispatchIndex = FindEventIndex(
        device.CommandContext,
        Extrinsic::Tests::MockCommandContext::EventKind::Dispatch,
        hookEventStart);
    const int endIndex = FindEventIndex(
        device.CommandContext,
        Extrinsic::Tests::MockCommandContext::EventKind::End);
    ASSERT_GE(beginIndex, 0);
    ASSERT_GE(dispatchIndex, 0);
    ASSERT_GE(endIndex, 0);
    EXPECT_LT(beginIndex, dispatchIndex);
    EXPECT_LT(dispatchIndex, endIndex);

    renderer->UnregisterRuntimeFrameCommandHook(firstHook);
    device.CommandContext.DispatchRecords.clear();
    device.CommandContext.Events.clear();
    hookOrder.clear();

    Extrinsic::RHI::FrameHandle secondFrame{};
    ASSERT_TRUE(renderer->BeginFrame(secondFrame));
    Extrinsic::Graphics::RenderWorld secondWorld = renderer->ExtractRenderWorld(input);
    renderer->PrepareFrame(secondWorld);
    renderer->ExecuteFrame(secondFrame, secondWorld);

    ASSERT_EQ(hookOrder.size(), 1u);
    EXPECT_EQ(hookOrder[0], 2);
    EXPECT_TRUE(std::ranges::none_of(
        device.CommandContext.DispatchRecords,
        [](const Extrinsic::Tests::MockCommandContext::DispatchRecord& record)
        {
            return DispatchMatches(record, kHookDispatchX, kHookDispatchY, kHookDispatchZ);
        }));
    EXPECT_TRUE(std::ranges::any_of(
        device.CommandContext.DispatchRecords,
        [](const Extrinsic::Tests::MockCommandContext::DispatchRecord& record)
        {
            return DispatchMatches(
                record,
                kSecondHookDispatchX,
                kSecondHookDispatchY,
                kSecondHookDispatchZ);
        }));

    renderer->UnregisterRuntimeFrameCommandHook(secondHook);
    device.CommandContext.DispatchRecords.clear();
    device.CommandContext.Events.clear();
    hookOrder.clear();

    Extrinsic::RHI::FrameHandle thirdFrame{};
    ASSERT_TRUE(renderer->BeginFrame(thirdFrame));
    Extrinsic::Graphics::RenderWorld thirdWorld =
        renderer->ExtractRenderWorld(input);
    renderer->PrepareFrame(thirdWorld);
    renderer->ExecuteFrame(thirdFrame, thirdWorld);

    EXPECT_TRUE(hookOrder.empty());
    EXPECT_TRUE(std::ranges::none_of(
        device.CommandContext.DispatchRecords,
        [](const Extrinsic::Tests::MockCommandContext::DispatchRecord& record)
        {
            return DispatchMatches(
                record,
                kSecondHookDispatchX,
                kSecondHookDispatchY,
                kSecondHookDispatchZ);
        }));

    renderer->Shutdown();
}

TEST(RendererFrameLifecycle, RenderGraphReportsCurrentRendererContractAndDeclaredArtifacts)
{
    Extrinsic::Tests::MockDevice device;
    device.BackbufferHandle = Extrinsic::RHI::TextureHandle{77u, 3u};

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer =
        Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);

    Extrinsic::RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));

    const Extrinsic::Graphics::RenderFrameInput input{
        .Viewport = {.Width = 320, .Height = 240},
    };
    Extrinsic::Graphics::RenderWorld world = renderer->ExtractRenderWorld(input);
    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(frame, world);

    const Extrinsic::Graphics::RenderGraphFrameStats& stats =
        renderer->GetLastRenderGraphStats();
    ASSERT_TRUE(stats.Compile.Succeeded) << stats.Diagnostic;
    ASSERT_TRUE(stats.Execute.Succeeded) << stats.Diagnostic;

    const Extrinsic::Graphics::RenderGraphContractIntegrationStats& contract =
        stats.Contract;
    EXPECT_TRUE(contract.Evaluated);
    EXPECT_TRUE(contract.ContractCompatible);
    EXPECT_TRUE(contract.SharedProductsCompatible);
    EXPECT_TRUE(contract.ArtifactMetadataValid);
    EXPECT_EQ(contract.RendererId,
              std::string{Extrinsic::Graphics::kCurrentRendererContractId});
    EXPECT_EQ(contract.SnapshotId,
              std::string{Extrinsic::Graphics::kCurrentRendererDefaultSnapshotId});
    EXPECT_EQ(contract.RecipeId,
              std::string{Extrinsic::Graphics::kCurrentRendererDefaultRecipeId});
    EXPECT_EQ(contract.ViewOutputRecipeId,
              std::string{Extrinsic::Graphics::kCurrentRendererDefaultViewRecipeId});
    EXPECT_GE(contract.SnapshotSourceRevisionCount, 1u);
    EXPECT_GE(contract.BindingIntentCount, 1u);
    EXPECT_GE(contract.RecipeSlotCount, 1u);
    EXPECT_GE(contract.ViewOutputCount, 2u);
    EXPECT_GE(contract.VisibilityProductCount, 1u);
    EXPECT_GE(contract.LightingProductCount, 1u);
    EXPECT_GE(contract.LightingResolvedLightCount, 1u);
    EXPECT_EQ(contract.UnsupportedProductDiagnosticCount, 0u);
    EXPECT_EQ(contract.MissingOutputDiagnosticCount, 0u);
    EXPECT_GE(contract.DegradedFallbackDiagnosticCount, 1u);
    EXPECT_EQ(contract.ArtifactPublicationFailureDiagnosticCount, 0u);

    const auto color = std::ranges::find_if(
        contract.DeclaredArtifacts,
        [](const Extrinsic::Graphics::RenderArtifactMetadata& artifact)
        {
            return artifact.Purpose == "color";
        });
    ASSERT_NE(color, contract.DeclaredArtifacts.end());
    EXPECT_EQ(color->RendererId, contract.RendererId);
    EXPECT_EQ(color->SnapshotId, contract.SnapshotId);
    EXPECT_EQ(color->ViewOutputRecipeId, contract.ViewOutputRecipeId);
    EXPECT_FALSE(color->SourceRevisions.empty());
    EXPECT_EQ(color->Status, Extrinsic::Graphics::RenderArtifactStatus::Available);
    EXPECT_EQ(Extrinsic::Graphics::ClassifyRenderArtifactLifecycle(*color),
              Extrinsic::Graphics::RenderArtifactLifecycleClass::TransientAvailable);

    renderer->Shutdown();
}

TEST(RendererFrameLifecycle, AsyncComputeQueuePlanIncrementsUtilizationStat)
{
    Extrinsic::Tests::MockDevice device;
    device.BackbufferHandle = Extrinsic::RHI::TextureHandle{77u, 3u};
    device.AsyncComputeQueueAvailable = true;
    device.AcceptQueueSubmitPlans = true;

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer = Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);

    Extrinsic::RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));

    const Extrinsic::Graphics::RenderFrameInput input{
        .Viewport = {.Width = 320, .Height = 240},
    };
    Extrinsic::Graphics::RenderWorld world = renderer->ExtractRenderWorld(input);
    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(frame, world);

    const Extrinsic::Graphics::RenderGraphFrameStats& stats = renderer->GetLastRenderGraphStats();
    EXPECT_TRUE(stats.Compile.Succeeded) << stats.Diagnostic;
    EXPECT_TRUE(stats.Execute.Succeeded) << stats.Diagnostic;
    EXPECT_EQ(stats.AsyncComputeUtilizedFrames, 1u);

    const auto hasAsyncBatch = std::ranges::any_of(
        device.RecordedQueueSubmitPlan,
        [](const Extrinsic::Tests::MockDevice::RecordedQueueSubmitBatch& batch) {
            return batch.Queue == Extrinsic::RHI::QueueAffinity::AsyncCompute;
        });
    EXPECT_TRUE(hasAsyncBatch);

    const auto requestedAsyncContext = std::ranges::any_of(
        device.QueueSubmitContextRequests,
        [](const Extrinsic::Tests::MockDevice::QueueSubmitContextRequest& request) {
            return request.Affinity == Extrinsic::RHI::QueueAffinity::AsyncCompute;
        });
    EXPECT_TRUE(requestedAsyncContext);
    EXPECT_GE(device.AsyncComputeContext.BeginCalls, 1);
    EXPECT_GE(device.AsyncComputeContext.EndCalls, 1);

    renderer->Shutdown();
}

TEST(RendererFrameLifecycle, ParallelRecordingFlagFallsBackWhenDeviceDeclinesContextPlan)
{
    Extrinsic::Tests::MockDevice device;
    device.BackbufferHandle = Extrinsic::RHI::TextureHandle{77u, 3u};

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer = Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);
    EXPECT_FALSE(renderer->IsParallelRenderGraphRecordingEnabled());
    renderer->SetParallelRenderGraphRecordingEnabled(true);
    EXPECT_TRUE(renderer->IsParallelRenderGraphRecordingEnabled());

    Extrinsic::RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));

    const Extrinsic::Graphics::RenderFrameInput input{
        .Viewport = {.Width = 320, .Height = 240},
    };
    Extrinsic::Graphics::RenderWorld world = renderer->ExtractRenderWorld(input);
    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(frame, world);

    const Extrinsic::Graphics::RenderGraphFrameStats& stats = renderer->GetLastRenderGraphStats();
    EXPECT_TRUE(stats.Compile.Succeeded) << stats.Diagnostic;
    EXPECT_TRUE(stats.Execute.Succeeded) << stats.Diagnostic;
    EXPECT_TRUE(stats.Execute.ParallelRecordingRequested);
    EXPECT_FALSE(stats.Execute.ParallelRecordingAccepted);
    EXPECT_TRUE(stats.Execute.SerialFallbackUsed);
    EXPECT_EQ(stats.Execute.ParallelCommandContextCount, 0u);
    EXPECT_FALSE(stats.Execute.ParallelRecordUsedScheduler);
    EXPECT_EQ(stats.Execute.ParallelRecordWorkerTaskCount, 0u);
    EXPECT_EQ(stats.Execute.ParallelRecordCallerRecordCount, 0u);
    EXPECT_TRUE(device.RecordedParallelCommandContextPlan.empty());
    EXPECT_GE(device.CommandContext.BeginCalls, 1);
    EXPECT_GE(device.CommandContext.EndCalls, 1);

    renderer->Shutdown();
}

TEST(RendererFrameLifecycle, ParallelRecordingUsesAcceptedContextPlanInSerialSubmitOrder)
{
    Extrinsic::Tests::MockDevice device;
    device.BackbufferHandle = Extrinsic::RHI::TextureHandle{77u, 3u};
    device.ParallelCommandContextsAvailable = true;
    device.AcceptParallelCommandContextPlans = true;

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer = Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);
    renderer->SetParallelRenderGraphRecordingEnabled(true);

    Extrinsic::RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));

    const Extrinsic::Graphics::RenderFrameInput input{
        .Viewport = {.Width = 320, .Height = 240},
    };
    Extrinsic::Graphics::RenderWorld world = renderer->ExtractRenderWorld(input);
    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(frame, world);

    const Extrinsic::Graphics::RenderGraphFrameStats& stats = renderer->GetLastRenderGraphStats();
    EXPECT_TRUE(stats.Compile.Succeeded) << stats.Diagnostic;
    EXPECT_TRUE(stats.Execute.Succeeded) << stats.Diagnostic;
    EXPECT_TRUE(stats.Execute.ParallelRecordingRequested);
    EXPECT_TRUE(stats.Execute.ParallelRecordingAccepted);
    EXPECT_FALSE(stats.Execute.SerialFallbackUsed);
    EXPECT_GT(stats.Execute.ParallelCommandContextCount, 0u);
    EXPECT_EQ(stats.Execute.ParallelCommandContextCount,
              device.RecordedParallelCommandContextPlan.size());
    EXPECT_EQ(stats.Execute.ParallelRecordedPassCount,
              device.RecordedParallelCommandContextPlan.size());
    EXPECT_FALSE(stats.Execute.ParallelRecordUsedScheduler);
    EXPECT_EQ(stats.Execute.ParallelRecordWorkerTaskCount, 0u);
    EXPECT_EQ(stats.Execute.ParallelRecordCallerRecordCount,
              stats.Execute.ParallelRecordedPassCount);
    EXPECT_GT(stats.CommandRecords.Passes.size(), 0u);
    EXPECT_EQ(stats.CommandRecords.Passes.size(),
              static_cast<std::size_t>(stats.CommandRecords.Recorded + stats.CommandRecords.Skipped));
    EXPECT_EQ(stats.CommandRecords.Skipped,
              stats.CommandRecords.SkippedNonOperational + stats.CommandRecords.SkippedUnavailable);
    const Extrinsic::Graphics::RenderGraphCommandPassStats* presentPass =
        FindCommandPass(stats, "Present");
    ASSERT_NE(presentPass, nullptr);
    EXPECT_EQ(presentPass->Status,
              Extrinsic::Graphics::RenderCommandPassStatus::Recorded);
    const Extrinsic::Graphics::RenderGraphCommandPassStats* postProcessPass =
        FindCommandPass(stats, "PostProcessPass");
    ASSERT_NE(postProcessPass, nullptr);
    EXPECT_EQ(postProcessPass->Status,
              Extrinsic::Graphics::RenderCommandPassStatus::Recorded);
    const Extrinsic::Graphics::RenderGraphCommandPassStats* histogramPass =
        FindCommandPass(stats, "PostProcessHistogramPass");
    ASSERT_NE(histogramPass, nullptr);
    EXPECT_EQ(histogramPass->Status,
              Extrinsic::Graphics::RenderCommandPassStatus::Recorded);
    EXPECT_EQ(device.ParallelCommandContextRequests.size(),
              device.RecordedParallelCommandContextPlan.size());
    EXPECT_EQ(device.SubmittedParallelCommandContexts.size(),
              device.RecordedParallelCommandContextPlan.size());

    for (std::size_t i = 0; i < device.RecordedParallelCommandContextPlan.size(); ++i)
    {
        EXPECT_EQ(device.SubmittedParallelCommandContexts[i].PassIndex,
                  device.RecordedParallelCommandContextPlan[i].PassIndex);
        EXPECT_EQ(device.SubmittedParallelCommandContexts[i].ContextIndex,
                  device.RecordedParallelCommandContextPlan[i].ContextIndex);
    }

    renderer->Shutdown();
}

TEST(RendererFrameLifecycle, ParallelRecordingUsesSchedulerWorkersWhenAvailable)
{
    SchedulerScope scheduler{4u};

    Extrinsic::Tests::MockDevice device;
    device.BackbufferHandle = Extrinsic::RHI::TextureHandle{79u, 3u};
    device.ParallelCommandContextsAvailable = true;
    device.AcceptParallelCommandContextPlans = true;

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer = Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);
    renderer->SetParallelRenderGraphRecordingEnabled(true);

    Extrinsic::RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));

    const Extrinsic::Graphics::RenderFrameInput input{
        .Viewport = {.Width = 320, .Height = 240},
    };
    Extrinsic::Graphics::RenderWorld world = renderer->ExtractRenderWorld(input);
    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(frame, world);

    const Extrinsic::Graphics::RenderGraphFrameStats& stats = renderer->GetLastRenderGraphStats();
    EXPECT_TRUE(stats.Compile.Succeeded) << stats.Diagnostic;
    EXPECT_TRUE(stats.Execute.Succeeded) << stats.Diagnostic;
    EXPECT_TRUE(stats.Execute.ParallelRecordingRequested);
    EXPECT_TRUE(stats.Execute.ParallelRecordingAccepted);
    EXPECT_FALSE(stats.Execute.SerialFallbackUsed);
    EXPECT_TRUE(stats.Execute.ParallelRecordUsedScheduler);
    EXPECT_GT(stats.Execute.ParallelRecordWorkerTaskCount, 0u);
    EXPECT_EQ(stats.Execute.ParallelRecordWorkerTaskCount,
              stats.Execute.ParallelRecordedPassCount);
    EXPECT_EQ(stats.Execute.ParallelRecordCallerRecordCount, 0u);
    EXPECT_EQ(stats.Execute.ParallelCommandContextCount,
              device.RecordedParallelCommandContextPlan.size());
    EXPECT_EQ(device.ParallelCommandContextRequests.size(),
              device.RecordedParallelCommandContextPlan.size());
    EXPECT_EQ(device.SubmittedParallelCommandContexts.size(),
              device.RecordedParallelCommandContextPlan.size());

    for (std::size_t i = 0; i < device.RecordedParallelCommandContextPlan.size(); ++i)
    {
        EXPECT_EQ(device.SubmittedParallelCommandContexts[i].PassIndex,
                  device.RecordedParallelCommandContextPlan[i].PassIndex);
        EXPECT_EQ(device.SubmittedParallelCommandContexts[i].ContextIndex,
                  device.RecordedParallelCommandContextPlan[i].ContextIndex);
    }

    renderer->Shutdown();
}

TEST(RendererFrameLifecycle, ParallelRecordingUsesAcceptedContextsForAsyncComputeQueuePlan)
{
    SchedulerScope scheduler{4u};

    Extrinsic::Tests::MockDevice device;
    device.BackbufferHandle = Extrinsic::RHI::TextureHandle{81u, 3u};
    device.AsyncComputeQueueAvailable = true;
    device.AcceptQueueSubmitPlans = true;
    device.ParallelCommandContextsAvailable = true;
    device.AcceptParallelCommandContextPlans = true;

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer = Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);
    renderer->SetParallelRenderGraphRecordingEnabled(true);

    Extrinsic::RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));

    const Extrinsic::Graphics::RenderFrameInput input{
        .Viewport = {.Width = 320, .Height = 240},
    };
    Extrinsic::Graphics::RenderWorld world = renderer->ExtractRenderWorld(input);
    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(frame, world);

    const Extrinsic::Graphics::RenderGraphFrameStats& stats = renderer->GetLastRenderGraphStats();
    EXPECT_TRUE(stats.Compile.Succeeded) << stats.Diagnostic;
    EXPECT_TRUE(stats.Execute.Succeeded) << stats.Diagnostic;
    EXPECT_EQ(stats.AsyncComputeUtilizedFrames, 1u);
    EXPECT_TRUE(stats.Execute.ParallelRecordingRequested);
    EXPECT_TRUE(stats.Execute.ParallelRecordingAccepted);
    EXPECT_FALSE(stats.Execute.SerialFallbackUsed);
    EXPECT_TRUE(stats.Execute.ParallelRecordUsedScheduler);
    EXPECT_GT(stats.Execute.ParallelRecordWorkerTaskCount, 0u);
    EXPECT_EQ(stats.Execute.ParallelRecordWorkerTaskCount,
              stats.Execute.ParallelRecordedPassCount);
    EXPECT_EQ(stats.Execute.ParallelRecordCallerRecordCount, 0u);
    EXPECT_EQ(stats.Execute.ParallelCommandContextCount,
              device.RecordedParallelCommandContextPlan.size());
    EXPECT_EQ(device.ParallelCommandContextRequests.size(),
              device.RecordedParallelCommandContextPlan.size());
    EXPECT_EQ(device.SubmittedParallelCommandContexts.size(),
              device.RecordedParallelCommandContextPlan.size());

    const auto hasAsyncBatch = std::ranges::any_of(
        device.RecordedQueueSubmitPlan,
        [](const Extrinsic::Tests::MockDevice::RecordedQueueSubmitBatch& batch) {
            return batch.Queue == Extrinsic::RHI::QueueAffinity::AsyncCompute;
        });
    EXPECT_TRUE(hasAsyncBatch);

    const auto requestedAsyncContext = std::ranges::any_of(
        device.QueueSubmitContextRequests,
        [](const Extrinsic::Tests::MockDevice::QueueSubmitContextRequest& request) {
            return request.Affinity == Extrinsic::RHI::QueueAffinity::AsyncCompute;
        });
    EXPECT_TRUE(requestedAsyncContext);
    EXPECT_GE(device.AsyncComputeContext.BeginCalls, 1);
    EXPECT_GE(device.AsyncComputeContext.EndCalls, 1);

    const auto recordedAsyncParallelContext = std::ranges::any_of(
        device.RecordedParallelCommandContextPlan,
        [](const Extrinsic::RHI::ParallelCommandContextRequest& request) {
            return request.Queue == Extrinsic::RHI::QueueAffinity::AsyncCompute;
        });
    EXPECT_TRUE(recordedAsyncParallelContext);

    const auto submittedAsyncParallelContext = std::ranges::any_of(
        device.SubmittedParallelCommandContexts,
        [](const Extrinsic::RHI::ParallelCommandContextRequest& request) {
            return request.Queue == Extrinsic::RHI::QueueAffinity::AsyncCompute;
        });
    EXPECT_TRUE(submittedAsyncParallelContext);

    for (std::size_t i = 0; i < device.RecordedParallelCommandContextPlan.size(); ++i)
    {
        EXPECT_EQ(device.SubmittedParallelCommandContexts[i].Queue,
                  device.RecordedParallelCommandContextPlan[i].Queue);
        EXPECT_EQ(device.SubmittedParallelCommandContexts[i].PassIndex,
                  device.RecordedParallelCommandContextPlan[i].PassIndex);
        EXPECT_EQ(device.SubmittedParallelCommandContexts[i].ContextIndex,
                  device.RecordedParallelCommandContextPlan[i].ContextIndex);
    }

    renderer->Shutdown();
}

TEST(RendererFrameLifecycle, ParallelRecordingRecordsDynamicUploadPassesThroughAcceptedContexts)
{
    static const std::array<Extrinsic::Graphics::DebugTrianglePacket, 1> kTriangles{{
        Extrinsic::Graphics::DebugTrianglePacket{
            .A = glm::vec3{0.0f, 0.5f, 0.0f},
            .B = glm::vec3{-0.5f, -0.5f, 0.0f},
            .C = glm::vec3{0.5f, -0.5f, 0.0f},
            .Color = glm::vec4{1.0f, 1.0f, 1.0f, 1.0f},
            .DepthTested = true,
        },
    }};
    static const std::array<Extrinsic::Graphics::VectorFieldOverlayPacket, 1> kVectorFields{{
        Extrinsic::Graphics::VectorFieldOverlayPacket{
            .Name = "ParallelRecording.VectorField",
            .Domain = Extrinsic::Graphics::VisualizationAttributeDomain::Vertex,
            .ElementCount = 1u,
            .Scale = 1.0f,
            .Color = glm::vec4{1.0f, 0.0f, 0.0f, 1.0f},
            .DepthTested = true,
        },
    }};
    static const std::array<Extrinsic::Graphics::IsolineOverlayPacket, 1> kIsolines{{
        Extrinsic::Graphics::IsolineOverlayPacket{
            .SourceScalarName = "ParallelRecording.Isoline",
            .Domain = Extrinsic::Graphics::VisualizationAttributeDomain::Face,
            .IsoValueCount = 1u,
            .RangeMin = 0.0f,
            .RangeMax = 1.0f,
            .LineWidth = 1.0f,
            .Color = glm::vec4{0.0f, 1.0f, 0.0f, 1.0f},
            .DepthTested = true,
        },
    }};

    Extrinsic::Tests::MockDevice device;
    device.BackbufferHandle = Extrinsic::RHI::TextureHandle{78u, 3u};
    device.ParallelCommandContextsAvailable = true;
    device.AcceptParallelCommandContextPlans = true;

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer = Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);
    renderer->SetParallelRenderGraphRecordingEnabled(true);

    Extrinsic::Graphics::ImGuiOverlaySystem overlay;
    overlay.Initialize();
    overlay.SubmitFrame(MakeOverlayFrameWithWork());
    renderer->SetImGuiOverlaySystem(&overlay);

    Extrinsic::RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));
    renderer->SubmitRuntimeSnapshots(Extrinsic::Graphics::RuntimeRenderSnapshotBatch{
        .VisualizationVectorFields =
            std::span<const Extrinsic::Graphics::VectorFieldOverlayPacket>{
                kVectorFields.data(), kVectorFields.size()},
        .VisualizationIsolines =
            std::span<const Extrinsic::Graphics::IsolineOverlayPacket>{
                kIsolines.data(), kIsolines.size()},
        .DebugTriangles =
            std::span<const Extrinsic::Graphics::DebugTrianglePacket>{
                kTriangles.data(), kTriangles.size()},
    });

    const Extrinsic::Graphics::RenderFrameInput input{
        .Viewport = {.Width = 320, .Height = 240},
        .DebugOverlayEnabled = true,
    };
    Extrinsic::Graphics::RenderWorld world = renderer->ExtractRenderWorld(input);
    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(frame, world);

    const Extrinsic::Graphics::RenderGraphFrameStats& stats = renderer->GetLastRenderGraphStats();
    EXPECT_TRUE(stats.Compile.Succeeded) << stats.Diagnostic;
    EXPECT_TRUE(stats.Execute.Succeeded) << stats.Diagnostic;
    EXPECT_TRUE(stats.Execute.ParallelRecordingRequested);
    EXPECT_TRUE(stats.Execute.ParallelRecordingAccepted);
    EXPECT_FALSE(stats.Execute.SerialFallbackUsed);
    EXPECT_FALSE(stats.Execute.ParallelRecordUsedScheduler);
    EXPECT_EQ(stats.Execute.ParallelRecordCallerRecordCount,
              stats.Execute.ParallelRecordedPassCount);

    const Extrinsic::Graphics::RenderGraphCommandPassStats* transientPass =
        FindCommandPass(stats, "TransientDebugSurfacePass");
    ASSERT_NE(transientPass, nullptr);
    EXPECT_EQ(transientPass->Status,
              Extrinsic::Graphics::RenderCommandPassStatus::Recorded);
    EXPECT_EQ(stats.TransientDebugUpload.TriangleRecordsSubmitted, 1u);
    EXPECT_EQ(stats.TransientDebugUpload.TriangleRecordsRecorded, 1u);
    EXPECT_EQ(stats.TransientDebugUpload.MissingPipelineSkipCount, 0u);

    const Extrinsic::Graphics::RenderGraphCommandPassStats* visualizationPass =
        FindCommandPass(stats, "VisualizationOverlayPass");
    ASSERT_NE(visualizationPass, nullptr);
    EXPECT_EQ(visualizationPass->Status,
              Extrinsic::Graphics::RenderCommandPassStatus::Recorded);
    EXPECT_EQ(stats.VisualizationOverlayUpload.VectorFieldRecordsSubmitted, 1u);
    EXPECT_EQ(stats.VisualizationOverlayUpload.VectorFieldRecordsRecorded, 1u);
    EXPECT_EQ(stats.VisualizationOverlayUpload.IsolineRecordsSubmitted, 1u);
    EXPECT_EQ(stats.VisualizationOverlayUpload.IsolineRecordsRecorded, 1u);
    EXPECT_EQ(stats.VisualizationOverlayUpload.MissingPipelineSkipCount, 0u);

    const Extrinsic::Graphics::RenderGraphCommandPassStats* imguiPass =
        FindCommandPass(stats, "ImGuiPass");
    ASSERT_NE(imguiPass, nullptr);
    EXPECT_EQ(imguiPass->Status,
              Extrinsic::Graphics::RenderCommandPassStatus::Recorded);
    EXPECT_GT(overlay.GetDiagnostics().DrawCalls, 0u);

    EXPECT_EQ(device.ParallelCommandContextRequests.size(),
              device.RecordedParallelCommandContextPlan.size());
    EXPECT_EQ(device.SubmittedParallelCommandContexts.size(),
              device.RecordedParallelCommandContextPlan.size());

    renderer->Shutdown();
}

TEST(RendererFrameLifecycle, InvalidDeviceBackbufferReportsRecipeDiagnostic)
{
    Extrinsic::Tests::MockDevice device;
    device.BackbufferHandle = {};

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer = Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);

    Extrinsic::RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));

    const Extrinsic::Graphics::RenderFrameInput input{
        .Viewport = {.Width = 64, .Height = 64},
    };
    Extrinsic::Graphics::RenderWorld world = renderer->ExtractRenderWorld(input);
    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(frame, world);

    const Extrinsic::Graphics::RenderGraphFrameStats& stats = renderer->GetLastRenderGraphStats();
    EXPECT_FALSE(stats.Compile.Succeeded);
    EXPECT_FALSE(stats.Execute.Succeeded);
    EXPECT_NE(stats.Diagnostic.find("Backbuffer"), std::string::npos);
    EXPECT_EQ(device.GetBackbufferHandleCount, 1);
    EXPECT_EQ(device.CommandContext.BeginCalls, 0);
    EXPECT_EQ(device.CommandContext.EndCalls, 0);

    renderer->Shutdown();
}

// -----------------------------------------------------------------------------
// GRAPHICS-033E: after a successful recipe build + compile, the renderer must
// publish the recipe-aware validation outcome to the device exactly once via
// `IDevice::NoteRecipeGraphValidation(bool)`. A clean compile of the default
// recipe yields `true`.
// -----------------------------------------------------------------------------
TEST(RendererFrameLifecycle, PublishesRecipeGraphValidationOnSuccessfulCompile)
{
    Extrinsic::Tests::MockDevice device;
    device.BackbufferHandle = Extrinsic::RHI::TextureHandle{77u, 3u};

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer = Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);

    Extrinsic::RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));

    const Extrinsic::Graphics::RenderFrameInput input{
        .Viewport = {.Width = 320, .Height = 240},
    };
    Extrinsic::Graphics::RenderWorld world = renderer->ExtractRenderWorld(input);
    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(frame, world);

    const Extrinsic::Graphics::RenderGraphFrameStats& stats = renderer->GetLastRenderGraphStats();
    ASSERT_TRUE(stats.Compile.Succeeded) << stats.Diagnostic;
    ASSERT_EQ(device.RecipeGraphValidationCalls.size(), 1u);
    EXPECT_TRUE(device.RecipeGraphValidationCalls.front());

    renderer->Shutdown();
}

// -----------------------------------------------------------------------------
// GRAPHICS-033E: when the recipe build fails (e.g. invalid backbuffer handle),
// the renderer publishes `false` so the operational gate cannot inherit a
// stale-clean state from a prior compile.
// -----------------------------------------------------------------------------
TEST(RendererFrameLifecycle, PublishesFailClosedRecipeValidationOnRecipeBuildFailure)
{
    Extrinsic::Tests::MockDevice device;
    device.BackbufferHandle = {};

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer = Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);

    Extrinsic::RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));

    const Extrinsic::Graphics::RenderFrameInput input{
        .Viewport = {.Width = 64, .Height = 64},
    };
    Extrinsic::Graphics::RenderWorld world = renderer->ExtractRenderWorld(input);
    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(frame, world);

    const Extrinsic::Graphics::RenderGraphFrameStats& stats = renderer->GetLastRenderGraphStats();
    EXPECT_FALSE(stats.Compile.Succeeded);
    ASSERT_EQ(device.RecipeGraphValidationCalls.size(), 1u);
    EXPECT_FALSE(device.RecipeGraphValidationCalls.front());

    renderer->Shutdown();
}

TEST(RendererFrameLifecycle, NonOperationalDeviceSkipsCullingCommandsButExecutesGraph)
{
    Extrinsic::Tests::MockDevice device;
    device.Operational = false;
    device.BackbufferHandle = Extrinsic::RHI::TextureHandle{91u, 1u};

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer = Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);

    Extrinsic::RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));

    const Extrinsic::Graphics::RenderFrameInput input{
        .Viewport = {.Width = 128, .Height = 72},
    };
    Extrinsic::Graphics::RenderWorld world = renderer->ExtractRenderWorld(input);
    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(frame, world);

    const Extrinsic::Graphics::RenderGraphFrameStats& stats = renderer->GetLastRenderGraphStats();
    EXPECT_TRUE(stats.Compile.Succeeded) << stats.Diagnostic;
    EXPECT_TRUE(stats.Execute.Succeeded) << stats.Diagnostic;
    EXPECT_FALSE(stats.Execute.DeviceOperational);
    EXPECT_EQ(stats.CommandRecords.Recorded, 0u);
    // GRAPHICS-018 §4: a non-operational device skips every routed pass with
    // SkippedNonOperational so CPU CI surfaces accidental operational claims.
    EXPECT_EQ(stats.CommandRecords.SkippedUnavailable, 0u);
    EXPECT_EQ(stats.CommandRecords.Skipped, stats.CommandRecords.SkippedNonOperational);
    EXPECT_GE(stats.CommandRecords.SkippedNonOperational, 2u);
    ASSERT_NE(FindCommandPass(stats, "CullingPass"), nullptr);
    EXPECT_EQ(FindCommandPass(stats, "CullingPass")->Status,
              Extrinsic::Graphics::RenderCommandPassStatus::SkippedNonOperational);
    ASSERT_NE(FindCommandPass(stats, "DepthPrepass"), nullptr);
    EXPECT_EQ(FindCommandPass(stats, "DepthPrepass")->Status,
              Extrinsic::Graphics::RenderCommandPassStatus::SkippedNonOperational);
    ASSERT_NE(FindCommandPass(stats, "SurfacePass"), nullptr);
    EXPECT_EQ(FindCommandPass(stats, "SurfacePass")->Status,
              Extrinsic::Graphics::RenderCommandPassStatus::SkippedNonOperational);
    ASSERT_NE(FindCommandPass(stats, "Present"), nullptr);
    EXPECT_EQ(FindCommandPass(stats, "Present")->Status,
              Extrinsic::Graphics::RenderCommandPassStatus::SkippedNonOperational);
    EXPECT_EQ(device.CommandContext.BeginCalls, 1);
    EXPECT_EQ(device.CommandContext.EndCalls, 1);
    EXPECT_EQ(device.CommandContext.FillBufferCalls, 0);
    EXPECT_EQ(device.CommandContext.BindPipelineCalls, 0);
    EXPECT_EQ(device.CommandContext.PushConstantsCalls, 0);
    EXPECT_EQ(device.CommandContext.DispatchCalls, 0);
    EXPECT_EQ(device.CommandContext.BindIndexBufferCalls, 0);
    EXPECT_EQ(device.CommandContext.DrawIndexedIndirectCountCalls, 0);

    renderer->Shutdown();
}

TEST(RendererFrameLifecycle, OperationalRebuildAfterNonOperationalStartupRecordsRoutedCommands)
{
    Extrinsic::Tests::MockDevice device;
    device.Operational = false;
    device.BackbufferHandle = Extrinsic::RHI::TextureHandle{93u, 1u};

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer = Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);
    EXPECT_FALSE(renderer->GetMaterialSystem().GetBuffer().IsValid());
    EXPECT_FALSE(renderer->GetGpuWorld().GetSceneTableBuffer().IsValid());

    Extrinsic::RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));

    const Extrinsic::Graphics::RenderFrameInput input{
        .Viewport = {.Width = 160, .Height = 90},
    };
    const Extrinsic::Graphics::HZBBuildDispatchPlan hzbPlan =
        ExpectedFallbackHZBPlan(160u, 90u);
    ASSERT_TRUE(hzbPlan.IsValid());
    Extrinsic::Graphics::RenderWorld world = renderer->ExtractRenderWorld(input);
    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(frame, world);
    const Extrinsic::Graphics::RenderGraphFrameStats& nonOperationalStats =
        renderer->GetLastRenderGraphStats();
    EXPECT_GE(nonOperationalStats.CommandRecords.SkippedNonOperational, 2u);

    device.Operational = true;
    EXPECT_TRUE(renderer->RebuildOperationalResources(device));
    EXPECT_TRUE(renderer->GetMaterialSystem().GetBuffer().IsValid());
    EXPECT_TRUE(renderer->GetGpuWorld().GetSceneTableBuffer().IsValid());
    EXPECT_EQ(renderer->GetMaterialSystem().GetDiagnostics().Capacity, 256u);

    device.CommandContext = Extrinsic::Tests::MockCommandContext{};
    frame = {};
    ASSERT_TRUE(renderer->BeginFrame(frame));
    world = renderer->ExtractRenderWorld(input);
    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(frame, world);

    const Extrinsic::Graphics::RenderGraphFrameStats& stats = renderer->GetLastRenderGraphStats();
    EXPECT_TRUE(stats.Compile.Succeeded) << stats.Diagnostic;
    EXPECT_TRUE(stats.Execute.Succeeded) << stats.Diagnostic;
    EXPECT_TRUE(stats.Execute.DeviceOperational);
    // GRAPHICS-071 — five routed passes (Culling/DepthPrepass/Surface/Line/Point)
    // after the operational rebuild publishes the forward pass pipeline leases.
    // GRAPHICS-075 Slice A — the post-rebuild publish also publishes the
    // tonemap pipeline lease, so the `"PostProcessPass"` umbrella branch
    // routes its tonemap leg. GRAPHICS-075 Slice B.1 — the rebuild also
    // publishes the bloom downsample + upsample leases, so the umbrella
    // fans out to the bloom helper too (returning Recorded under the
    // "structurally-recorded no-op" taxonomy even though `EnableBloom`
    // defaults to false). GRAPHICS-075 Slice C/D.2a — the rebuild also
    // publishes the FXAA + three SMAA pipeline leases, and the recipe
    // declares the AA umbrella split across three ordered graph passes
    // (`"PostProcessAA{Edge,Blend,Resolve}Pass"`). Each per-stage helper
    // returns `Recorded` under the structurally-recorded-no-op taxonomy
    // when `AntiAliasing == None`. GRAPHICS-075 Slice E.1 — the rebuild
    // also publishes the histogram compute pipeline lease, so the new
    // ordered `"PostProcessHistogramPass"` graph pass routes its helper
    // (returning `Recorded` per the structurally-recorded-no-op taxonomy
    // since `EnableHistogram` defaults to false). GRAPHICS-076 Slice A —
    // the rebuild also publishes the canonical present pipeline lease,
    // so the `"Present"` graph pass routes through `RecordPresentPass`
    // and reports `Recorded` after the operational rebuild. GRAPHICS-038B
    // adds the routed `"HZBBuildPass"` after `DepthPrepass`. GRAPHICS-039C
    // adds the cluster grid build and light-assignment routes before
    // lighting. Total `Recorded`: 8 routed + 1 under `PostProcessHistogramPass` + 2
    // under `PostProcessPass` (bloom + tonemap) + 3 under the AA
    // passes + 1 under `Present` = 15. Remaining unwired passes still
    // soft-skip with SkippedUnavailable.
    EXPECT_EQ(stats.CommandRecords.Recorded, 12u);
    EXPECT_EQ(stats.CommandRecords.SkippedNonOperational, 0u);
    EXPECT_EQ(stats.CommandRecords.Skipped, stats.CommandRecords.SkippedUnavailable);
    ASSERT_NE(FindCommandPass(stats, "CullingPass"), nullptr);
    EXPECT_EQ(FindCommandPass(stats, "CullingPass")->Status,
              Extrinsic::Graphics::RenderCommandPassStatus::Recorded);
    ASSERT_NE(FindCommandPass(stats, "DepthPrepass"), nullptr);
    EXPECT_EQ(FindCommandPass(stats, "DepthPrepass")->Status,
              Extrinsic::Graphics::RenderCommandPassStatus::Recorded);
    ASSERT_NE(FindCommandPass(stats, "HZBBuildPass"), nullptr);
    EXPECT_EQ(FindCommandPass(stats, "HZBBuildPass")->Status,
              Extrinsic::Graphics::RenderCommandPassStatus::Recorded);
    ASSERT_NE(FindCommandPass(stats, "ClusterGridBuildPass"), nullptr);
    EXPECT_EQ(FindCommandPass(stats, "ClusterGridBuildPass")->Status,
              Extrinsic::Graphics::RenderCommandPassStatus::Recorded);
    ASSERT_NE(FindCommandPass(stats, "LightClusterAssignmentPass"), nullptr);
    EXPECT_EQ(FindCommandPass(stats, "LightClusterAssignmentPass")->Status,
              Extrinsic::Graphics::RenderCommandPassStatus::Recorded);
    ASSERT_NE(FindCommandPass(stats, "SurfacePass"), nullptr);
    EXPECT_EQ(FindCommandPass(stats, "SurfacePass")->Status,
              Extrinsic::Graphics::RenderCommandPassStatus::Recorded);
    ASSERT_NE(FindCommandPass(stats, "LinePass"), nullptr);
    EXPECT_EQ(FindCommandPass(stats, "LinePass")->Status,
              Extrinsic::Graphics::RenderCommandPassStatus::Recorded);
    ASSERT_NE(FindCommandPass(stats, "PointPass"), nullptr);
    EXPECT_EQ(FindCommandPass(stats, "PointPass")->Status,
              Extrinsic::Graphics::RenderCommandPassStatus::Recorded);
    ASSERT_NE(FindCommandPass(stats, "PostProcessHistogramPass"), nullptr);
    EXPECT_EQ(FindCommandPass(stats, "PostProcessHistogramPass")->Status,
              Extrinsic::Graphics::RenderCommandPassStatus::Recorded);
    ASSERT_NE(FindCommandPass(stats, "PostProcessPass"), nullptr);
    EXPECT_EQ(FindCommandPass(stats, "PostProcessPass")->Status,
              Extrinsic::Graphics::RenderCommandPassStatus::Recorded);
    EXPECT_EQ(FindCommandPass(stats, "PostProcessAAEdgePass"), nullptr);
    EXPECT_EQ(FindCommandPass(stats, "PostProcessAABlendPass"), nullptr);
    EXPECT_EQ(FindCommandPass(stats, "PostProcessAAResolvePass"), nullptr);
    EXPECT_EQ(device.CommandContext.DispatchCalls,
              static_cast<int>(3u + hzbPlan.Dispatches.size()));
    EXPECT_EQ(stats.HZBBuildDispatchCount,
              static_cast<std::uint32_t>(hzbPlan.Dispatches.size()));
    EXPECT_EQ(stats.HZBBuildFallbackFrames, 1u);
    EXPECT_EQ(stats.ClusterGridBuildRecordedFrames, 1u);
    EXPECT_EQ(stats.ClusterLightAssignmentRecordedFrames, 1u);
    EXPECT_EQ(device.CommandContext.DrawIndexedIndirectCountCalls, 2);
    EXPECT_EQ(device.CommandContext.DrawIndirectCountCalls, 2);

    renderer->Shutdown();
}

TEST(RendererFrameLifecycle, DepthPrepassPipelineFailureSkipsUnavailableCommandPass)
{
    Extrinsic::Tests::MockDevice device;
    device.FailPipelineCreateCall = 2;

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer = Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);

    Extrinsic::RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));

    const Extrinsic::Graphics::RenderFrameInput input{
        .Viewport = {.Width = 128, .Height = 72},
    };
    Extrinsic::Graphics::RenderWorld world = renderer->ExtractRenderWorld(input);
    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(frame, world);

    const Extrinsic::Graphics::RenderGraphFrameStats& stats = renderer->GetLastRenderGraphStats();
    EXPECT_TRUE(stats.Compile.Succeeded) << stats.Diagnostic;
    EXPECT_TRUE(stats.Execute.Succeeded) << stats.Diagnostic;
    // GRAPHICS-070 — even when the depth-prepass pipeline lease is missing,
    // the forward surface pipeline lease is independently created in
    // `InitializeOperationalPassResources()` and the surface pass still
    // records its bind/draw shape. The depth prepass entry continues to
    // report `SkippedUnavailable` so its missing lease cannot regress
    // silently. GRAPHICS-075 Slice A — the tonemap pipeline is created
    // after the depth-prepass failure point and is unaffected by it, so
    // `"PostProcessPass"` still routes Recorded. GRAPHICS-075 Slice B.1
    // — the bloom downsample + upsample pipelines are likewise created
    // after the depth-prepass failure point, so the umbrella also fans
    // out to the bloom helper. GRAPHICS-075 Slice D.2a — the FXAA and
    // three SMAA pipelines are created independently of the depth-
    // prepass failure point, and the AA umbrella splits into three
    // ordered graph passes (`"PostProcessAA{Edge,Blend,Resolve}Pass"`)
    // so all three per-stage helpers fire → total climbs to 9 (4 routed
    // + 2 under PostProcessPass + 3 under the AA passes).
    // GRAPHICS-075 Slice E.1 — the histogram compute pipeline is created
    // independently of the depth-prepass failure point, and the new
    // ordered `"PostProcessHistogramPass"` graph pass fans out
    // independently of `PostProcessPass`, so its helper records
    // `Recorded` per the structurally-recorded-no-op taxonomy
    // (`EnableHistogram` defaults to false) → total climbs to 10
    // (4 routed + 1 under PostProcessHistogramPass + 2 under
    // PostProcessPass + 3 under the AA passes). GRAPHICS-076 Slice A —
    // the canonical present pipeline is created after postprocess (call #23), well
    // after the depth-prepass failure point at call #2, so the
    // `"Present"` graph pass records via `RecordPresentPass` → total
    // climbs to 11. GRAPHICS-038B also declares `"HZBBuildPass"` when its
    // retained target is allocated, but the executor fails closed as
    // `SkippedUnavailable` because the depth producer's pipeline is missing.
    // GRAPHICS-039C cluster grid/assignment do not consume the depth output
    // directly, so they still record their compute command shape: total 13.
    EXPECT_EQ(stats.CommandRecords.Recorded, 10u);
    EXPECT_EQ(stats.CommandRecords.SkippedNonOperational, 0u);
    EXPECT_EQ(stats.CommandRecords.Skipped, stats.CommandRecords.SkippedUnavailable);
    EXPECT_GE(stats.CommandRecords.SkippedUnavailable, 1u);
    ASSERT_NE(FindCommandPass(stats, "CullingPass"), nullptr);
    EXPECT_EQ(FindCommandPass(stats, "CullingPass")->Status,
              Extrinsic::Graphics::RenderCommandPassStatus::Recorded);
    ASSERT_NE(FindCommandPass(stats, "DepthPrepass"), nullptr);
    EXPECT_EQ(FindCommandPass(stats, "DepthPrepass")->Status,
              Extrinsic::Graphics::RenderCommandPassStatus::SkippedUnavailable);
    ASSERT_NE(FindCommandPass(stats, "HZBBuildPass"), nullptr);
    EXPECT_EQ(FindCommandPass(stats, "HZBBuildPass")->Status,
              Extrinsic::Graphics::RenderCommandPassStatus::SkippedUnavailable);
    ASSERT_NE(FindCommandPass(stats, "ClusterGridBuildPass"), nullptr);
    EXPECT_EQ(FindCommandPass(stats, "ClusterGridBuildPass")->Status,
              Extrinsic::Graphics::RenderCommandPassStatus::Recorded);
    ASSERT_NE(FindCommandPass(stats, "LightClusterAssignmentPass"), nullptr);
    EXPECT_EQ(FindCommandPass(stats, "LightClusterAssignmentPass")->Status,
              Extrinsic::Graphics::RenderCommandPassStatus::Recorded);
    ASSERT_NE(FindCommandPass(stats, "SurfacePass"), nullptr);
    EXPECT_EQ(FindCommandPass(stats, "SurfacePass")->Status,
              Extrinsic::Graphics::RenderCommandPassStatus::Recorded);
    ASSERT_NE(FindCommandPass(stats, "LinePass"), nullptr);
    EXPECT_EQ(FindCommandPass(stats, "LinePass")->Status,
              Extrinsic::Graphics::RenderCommandPassStatus::Recorded);
    ASSERT_NE(FindCommandPass(stats, "PointPass"), nullptr);
    EXPECT_EQ(FindCommandPass(stats, "PointPass")->Status,
              Extrinsic::Graphics::RenderCommandPassStatus::Recorded);
    ASSERT_NE(FindCommandPass(stats, "PostProcessHistogramPass"), nullptr);
    EXPECT_EQ(FindCommandPass(stats, "PostProcessHistogramPass")->Status,
              Extrinsic::Graphics::RenderCommandPassStatus::Recorded);
    ASSERT_NE(FindCommandPass(stats, "PostProcessPass"), nullptr);
    EXPECT_EQ(FindCommandPass(stats, "PostProcessPass")->Status,
              Extrinsic::Graphics::RenderCommandPassStatus::Recorded);
    EXPECT_EQ(FindCommandPass(stats, "PostProcessAAEdgePass"), nullptr);
    EXPECT_EQ(FindCommandPass(stats, "PostProcessAABlendPass"), nullptr);
    EXPECT_EQ(FindCommandPass(stats, "PostProcessAAResolvePass"), nullptr);
    EXPECT_EQ(device.CommandContext.DispatchCalls, 3);
    EXPECT_EQ(device.CommandContext.DrawIndexedIndirectCountCalls, 1);
    EXPECT_EQ(device.CommandContext.DrawIndirectCountCalls, 2);

    renderer->Shutdown();
}

TEST(RendererFrameLifecycle, CullingPipelineFailureSkipsRoutedCommandPassesUnavailable)
{
    Extrinsic::Tests::MockDevice device;
    device.FailPipelineCreateCall = 1;

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer = Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);

    Extrinsic::RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));

    const Extrinsic::Graphics::RenderFrameInput input{
        .Viewport = {.Width = 128, .Height = 72},
    };
    Extrinsic::Graphics::RenderWorld world = renderer->ExtractRenderWorld(input);
    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(frame, world);

    const Extrinsic::Graphics::RenderGraphFrameStats& stats = renderer->GetLastRenderGraphStats();
    EXPECT_TRUE(stats.Compile.Succeeded) << stats.Diagnostic;
    EXPECT_TRUE(stats.Execute.Succeeded) << stats.Diagnostic;
    // GRAPHICS-075 Slice A — culling-pipeline failure still leaves the
    // tonemap pipeline lease intact (`PostProcessSystem` + tonemap
    // pipeline do not depend on `m_CullingOutputAvailable`), so
    // `"PostProcessPass"` still routes Recorded. Every other routed
    // pass requires the culling output and reports `SkippedUnavailable`.
    // GRAPHICS-075 Slice B.1 — the bloom pipelines have the same
    // culling-independence as the tonemap pipeline, so the umbrella
    // adds a second Recorded entry for the bloom helper. GRAPHICS-075
    // Slice D.2a — the FXAA + three SMAA pipelines are similarly
    // culling-independent, and the AA umbrella splits into three
    // ordered graph passes (`"PostProcessAA{Edge,Blend,Resolve}Pass"`);
    // each per-stage helper records `Recorded` → three more entries
    // (total 5: 0 routed + 2 under PostProcessPass + 3 under the AA
    // is similarly culling-independent and lives in its own ordered
    // graph pass `"PostProcessHistogramPass"`; the helper records
    // `Recorded` per the structurally-recorded-no-op taxonomy
    // (`EnableHistogram` defaults to false) → total climbs to 6
    // (0 routed + 1 under PostProcessHistogramPass + 2 under
    // PostProcessPass + 3 under the AA passes). GRAPHICS-076 Slice A —
    // the canonical present pipeline is created after postprocess (call #23) and is
    // similarly culling-independent (the present helper only checks
    // device-operational + present pipeline lease, never
    // `m_CullingOutputAvailable`); the `"Present"` graph pass records
    // → total climbs to 7. GRAPHICS-038B declares `"HZBBuildPass"` when the
    // retained target is allocated, but the executor keeps it
    // `SkippedUnavailable` because culling/depth prerequisites are missing.
    // GRAPHICS-039C cluster grid/assignment remain culling-independent and
    // record their compute command shape: total climbs to 9.
    EXPECT_EQ(stats.CommandRecords.Recorded, 6u);
    EXPECT_EQ(stats.CommandRecords.SkippedNonOperational, 0u);
    EXPECT_EQ(stats.CommandRecords.Skipped, stats.CommandRecords.SkippedUnavailable);
    EXPECT_GE(stats.CommandRecords.SkippedUnavailable, 2u);
    ASSERT_NE(FindCommandPass(stats, "CullingPass"), nullptr);
    EXPECT_EQ(FindCommandPass(stats, "CullingPass")->Status,
              Extrinsic::Graphics::RenderCommandPassStatus::SkippedUnavailable);
    ASSERT_NE(FindCommandPass(stats, "DepthPrepass"), nullptr);
    EXPECT_EQ(FindCommandPass(stats, "DepthPrepass")->Status,
              Extrinsic::Graphics::RenderCommandPassStatus::SkippedUnavailable);
    ASSERT_NE(FindCommandPass(stats, "HZBBuildPass"), nullptr);
    EXPECT_EQ(FindCommandPass(stats, "HZBBuildPass")->Status,
              Extrinsic::Graphics::RenderCommandPassStatus::SkippedUnavailable);
    ASSERT_NE(FindCommandPass(stats, "ClusterGridBuildPass"), nullptr);
    EXPECT_EQ(FindCommandPass(stats, "ClusterGridBuildPass")->Status,
              Extrinsic::Graphics::RenderCommandPassStatus::Recorded);
    ASSERT_NE(FindCommandPass(stats, "LightClusterAssignmentPass"), nullptr);
    EXPECT_EQ(FindCommandPass(stats, "LightClusterAssignmentPass")->Status,
              Extrinsic::Graphics::RenderCommandPassStatus::Recorded);
    ASSERT_NE(FindCommandPass(stats, "PostProcessHistogramPass"), nullptr);
    EXPECT_EQ(FindCommandPass(stats, "PostProcessHistogramPass")->Status,
              Extrinsic::Graphics::RenderCommandPassStatus::Recorded);
    ASSERT_NE(FindCommandPass(stats, "PostProcessPass"), nullptr);
    EXPECT_EQ(FindCommandPass(stats, "PostProcessPass")->Status,
              Extrinsic::Graphics::RenderCommandPassStatus::Recorded);
    EXPECT_EQ(FindCommandPass(stats, "PostProcessAAEdgePass"), nullptr);
    EXPECT_EQ(FindCommandPass(stats, "PostProcessAABlendPass"), nullptr);
    EXPECT_EQ(FindCommandPass(stats, "PostProcessAAResolvePass"), nullptr);
    EXPECT_EQ(device.CommandContext.DispatchCalls, 2);
    EXPECT_EQ(device.CommandContext.DrawIndexedIndirectCountCalls, 0);

    renderer->Shutdown();
}

TEST(RendererFrameLifecycle, BeginFrameWithoutDeviceReportsLifecycleDiagnostic)
{
    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer = Extrinsic::Graphics::CreateRenderer();

    Extrinsic::RHI::FrameHandle frame{};
    EXPECT_FALSE(renderer->BeginFrame(frame));

    const Extrinsic::Graphics::RenderGraphFrameStats& stats = renderer->GetLastRenderGraphStats();
    EXPECT_FALSE(stats.LifecycleDiagnostic.empty());
    EXPECT_TRUE(stats.Diagnostic.empty());
}

TEST(RendererFrameLifecycle, BeginFrameSkipDoesNotRecordCommands)
{
    Extrinsic::Tests::MockDevice device;
    device.BeginFrameResult = false;

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer = Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);

    Extrinsic::RHI::FrameHandle frame{};
    EXPECT_FALSE(renderer->BeginFrame(frame));
    EXPECT_EQ(device.BeginFrameCount, 1);
    EXPECT_EQ(device.GetBackbufferHandleCount, 0);
    EXPECT_EQ(device.CommandContext.BeginCalls, 0);
    EXPECT_EQ(device.CommandContext.EndCalls, 0);

    renderer->Shutdown();
}

TEST(RendererFrameLifecycle, ExecuteFrameRejectsAfterRenderPrepFailure)
{
    Extrinsic::Tests::MockDevice device;
    device.BackbufferHandle = Extrinsic::RHI::TextureHandle{77u, 3u};

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer = Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);

    Extrinsic::RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));

    const Extrinsic::Graphics::RenderFrameInput input{
        .Viewport = {.Width = 64, .Height = 64},
    };
    Extrinsic::Graphics::RenderWorld world = renderer->ExtractRenderWorld(input);

    renderer->Shutdown();
    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(frame, world);

    const Extrinsic::Graphics::RenderGraphFrameStats& stats = renderer->GetLastRenderGraphStats();
    EXPECT_FALSE(stats.Compile.Succeeded);
    EXPECT_FALSE(stats.Execute.Succeeded);
    EXPECT_NE(stats.Diagnostic.find("ExecuteFrame requires successful PrepareFrame"), std::string::npos);
    EXPECT_NE(stats.Diagnostic.find("Render prep missing required input"), std::string::npos);
    EXPECT_EQ(device.CommandContext.BeginCalls, 0);
    EXPECT_EQ(device.CommandContext.EndCalls, 0);
}

TEST(RendererFrameLifecycle, FrameRecipePassesAllProduceStructuredCommandRecordStatuses)
{
    // GRAPHICS-018 §4 contract: every pass emitted by the default FrameRecipe
    // produces a structured RenderGraphCommandPassStats entry. Routed passes
    // (CullingPass, DepthPrepass) record real Vulkan-flavored command traffic
    // through the RHI seam; the remaining pass command bodies are not yet
    // wired and must soft-skip with SkippedUnavailable so the renderer reports
    // complete per-pass status. Render-graph bracketing remains in effect:
    // command-context Begin/End wrap the per-pass routing exactly once.
    Extrinsic::Tests::MockDevice device;
    device.BackbufferHandle = Extrinsic::RHI::TextureHandle{200u, 1u};

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer = Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);

    Extrinsic::RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));

    const Extrinsic::Graphics::RenderFrameInput input{
        .Viewport = {.Width = 256, .Height = 256},
    };
    Extrinsic::Graphics::RenderWorld world = renderer->ExtractRenderWorld(input);
    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(frame, world);

    const Extrinsic::Graphics::RenderGraphFrameStats& stats = renderer->GetLastRenderGraphStats();
    EXPECT_TRUE(stats.Compile.Succeeded) << stats.Diagnostic;
    EXPECT_TRUE(stats.Execute.Succeeded) << stats.Diagnostic;
    EXPECT_EQ(device.CommandContext.BeginCalls, 1);
    EXPECT_EQ(device.CommandContext.EndCalls, 1);

    // GRAPHICS-071 — default features select the forward lighting path
    // (`CompositionPass` is not declared in forward mode), and the retained
    // surface/line/point passes are routed by the renderer.
    // Every entry below must carry a structured status so future routing
    // changes can't silently regress to a no-op.
    static constexpr const char* kRoutedPasses[] = {
        "CullingPass", "DepthPrepass",
        // GRAPHICS-038B — the retained HZB build pass runs as a compute pass
        // immediately after `DepthPrepass` when the renderer owns a valid
        // `HZB.Current` target.
        "HZBBuildPass",
        "ClusterGridBuildPass",
        "LightClusterAssignmentPass",
        "SurfacePass", "LinePass", "PointPass",
        // GRAPHICS-075 Slice E.1 — the histogram compute dispatch lives
        // in its own ordered graph pass before `"PostProcessPass"`
        // because Vulkan rejects `vkCmdDispatch` inside an active
        // render-pass scope (and `"PostProcessPass"` is a render-pass-
        // scope pass — bloom + tonemap write color attachments).
        "PostProcessHistogramPass",
        // GRAPHICS-075 Slice A — `"PostProcessPass"` is now wired through the
        // umbrella executor branch (Bloom + ToneMap legs) and reports
        // `Recorded` on the operational CPU/null gate.
        "PostProcessPass",
        // GRAPHICS-076 Slice A — the canonical default-recipe present pass
        // records the fullscreen `BindPipeline + Draw(3, 1, 0, 0)` shape
        // when its pipeline lease is valid; the executor's `"Present"`
        // branch routes through `RecordPresentPass(...)` and reports
        // `Recorded` on the operational CPU/null gate.
        "Present",
    };
    static constexpr const char* kSoftSkippedPasses[] = {
        "ImGuiPass",
    };
    EXPECT_EQ(FindCommandPass(stats, "CompositionPass"), nullptr)
        << "CompositionPass is forward-mode-disabled per GRAPHICS-070; "
           "GRAPHICS-072 reintroduces it when deferred wiring lands.";

    for (const char* name : kRoutedPasses)
    {
        ASSERT_NE(FindCommandPass(stats, name), nullptr) << name;
        EXPECT_EQ(FindCommandPass(stats, name)->Status,
                  Extrinsic::Graphics::RenderCommandPassStatus::Recorded) << name;
    }
    for (const char* name : kSoftSkippedPasses)
    {
        ASSERT_NE(FindCommandPass(stats, name), nullptr) << name;
        EXPECT_EQ(FindCommandPass(stats, name)->Status,
                  Extrinsic::Graphics::RenderCommandPassStatus::SkippedUnavailable) << name;
    }

    EXPECT_EQ(stats.CommandRecords.SkippedNonOperational, 0u);
    EXPECT_GE(stats.CommandRecords.SkippedUnavailable,
              sizeof(kSoftSkippedPasses) / sizeof(kSoftSkippedPasses[0]));

    renderer->Shutdown();
}

// ---------------------------------------------------------------------------
// GRAPHICS-031A — canonical missing-material fallback pipeline lease
// ---------------------------------------------------------------------------

namespace
{
    [[nodiscard]] bool PipelineDescBytesEqual(const Extrinsic::RHI::PipelineDesc& lhs,
                                              const Extrinsic::RHI::PipelineDesc& rhs) noexcept
    {
        if (lhs.VertexShaderPath != rhs.VertexShaderPath) return false;
        if (lhs.FragmentShaderPath != rhs.FragmentShaderPath) return false;
        if (lhs.ComputeShaderPath != rhs.ComputeShaderPath) return false;
        if (lhs.PrimitiveTopology != rhs.PrimitiveTopology) return false;
        if (lhs.Rasterizer.Culling != rhs.Rasterizer.Culling) return false;
        if (lhs.Rasterizer.Winding != rhs.Rasterizer.Winding) return false;
        if (lhs.Rasterizer.Fill != rhs.Rasterizer.Fill) return false;
        if (lhs.DepthStencil.DepthTestEnable != rhs.DepthStencil.DepthTestEnable) return false;
        if (lhs.DepthStencil.DepthWriteEnable != rhs.DepthStencil.DepthWriteEnable) return false;
        if (lhs.DepthStencil.DepthFunc != rhs.DepthStencil.DepthFunc) return false;
        if (lhs.DepthStencil.StencilEnable != rhs.DepthStencil.StencilEnable) return false;
        if (lhs.ColorTargetCount != rhs.ColorTargetCount) return false;
        for (std::uint32_t i = 0u; i < lhs.ColorTargetCount; ++i)
        {
            if (lhs.ColorBlend[i].Enable != rhs.ColorBlend[i].Enable) return false;
            if (lhs.ColorTargetFormats[i] != rhs.ColorTargetFormats[i]) return false;
        }
        if (lhs.DepthTargetFormat != rhs.DepthTargetFormat) return false;
        if (lhs.PushConstantSize != rhs.PushConstantSize) return false;
        return true;
    }
}

TEST(RendererFrameLifecycle, DefaultDebugSurfacePipelineSurvivesOperationalRebuild)
{
    Extrinsic::Tests::MockDevice device;
    device.Operational = true;
    device.BackbufferHandle = Extrinsic::RHI::TextureHandle{173u, 1u};

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer = Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);

    const Extrinsic::RHI::PipelineHandle initialPipeline = renderer->GetDefaultDebugSurfacePipeline();
    EXPECT_TRUE(initialPipeline.IsValid());
    const Extrinsic::RHI::PipelineDesc initialDesc = renderer->GetDefaultDebugSurfacePipelineDesc();
    // The descriptor must reference the compiled SPIR-V artifact emitted by
    // intrinsic_add_glsl_shaders(), not the raw GLSL source — VulkanDevice::
    // CreatePipeline() reads the path verbatim as a SPIR-V binary.
    EXPECT_TRUE(initialDesc.VertexShaderPath.ends_with(
        "shaders/forward/default_debug_surface.vert.spv"))
        << initialDesc.VertexShaderPath;
    EXPECT_TRUE(initialDesc.FragmentShaderPath.ends_with(
        "shaders/forward/default_debug_surface.frag.spv"))
        << initialDesc.FragmentShaderPath;
    EXPECT_EQ(initialDesc.Rasterizer.Culling, Extrinsic::RHI::CullMode::Back);
    EXPECT_EQ(initialDesc.Rasterizer.Winding, kVulkanCameraTriangleFrontFace);
    EXPECT_EQ(initialDesc.DepthStencil.DepthFunc, Extrinsic::RHI::DepthOp::Less);
    EXPECT_FALSE(initialDesc.ColorBlend[0].Enable);
    EXPECT_EQ(initialDesc.PrimitiveTopology, Extrinsic::RHI::Topology::TriangleList);

    const Extrinsic::RHI::PipelineDesc* depthPrepass =
        FindCreatedPipelineDesc(device, "Renderer.DepthPrepass");
    ASSERT_NE(depthPrepass, nullptr);
    EXPECT_EQ(depthPrepass->Rasterizer.Culling, Extrinsic::RHI::CullMode::Back);
    EXPECT_EQ(depthPrepass->Rasterizer.Winding, kVulkanCameraTriangleFrontFace);

    EXPECT_TRUE(renderer->RebuildOperationalResources(device));
    const Extrinsic::RHI::PipelineHandle rebuiltPipeline = renderer->GetDefaultDebugSurfacePipeline();
    EXPECT_TRUE(rebuiltPipeline.IsValid());
    const Extrinsic::RHI::PipelineDesc rebuiltDesc = renderer->GetDefaultDebugSurfacePipelineDesc();
    EXPECT_TRUE(PipelineDescBytesEqual(initialDesc, rebuiltDesc));

    renderer->Shutdown();
}

// ---------------------------------------------------------------------------
// GRAPHICS-070 — default-recipe forward surface pipeline lease + republish
// ---------------------------------------------------------------------------

TEST(RendererFrameLifecycle, ForwardSurfacePipelineSurvivesOperationalRebuild)
{
    const std::string surfaceVertex = ReadShaderSource("forward/default_debug_surface.vert");
    const std::string surfaceFragment = ReadShaderSource("forward/default_debug_surface.frag");
    const std::string defaultGBufferFragment =
        ReadShaderSource("deferred/default_debug_gbuffer.frag");
    const std::string promotedGBufferFragment =
        ReadShaderSource("deferred/gbuffer.frag");

    EXPECT_NE(surfaceVertex.find("localVertexIndex = vertexIndex - geo.VertexOffset"), std::string::npos);
    EXPECT_NE(surfaceVertex.find("GpuReadPackedVec2(geo.TexcoordBufferBDA, localVertexIndex)"), std::string::npos);
    EXPECT_NE(surfaceVertex.find("GpuReadPackedVec3(geo.NormalBufferBDA, localVertexIndex)"), std::string::npos);
    EXPECT_NE(surfaceVertex.find("fragUv = localUv"), std::string::npos);
    EXPECT_NE(surfaceVertex.find("fragWorldNormal"), std::string::npos);
    EXPECT_NE(surfaceFragment.find("mat.AlbedoID"), std::string::npos);
    EXPECT_NE(surfaceFragment.find("fragWorldNormal"), std::string::npos);
    EXPECT_NE(surfaceFragment.find("mat.NormalID"), std::string::npos);
    EXPECT_NE(surfaceFragment.find("GpuMaterialFlag_ObjectSpaceNormalMap"), std::string::npos);
    EXPECT_NE(surfaceFragment.find("ResolveSurfaceNormal"), std::string::npos);
    EXPECT_NE(defaultGBufferFragment.find("mat.NormalID"), std::string::npos);
    EXPECT_NE(defaultGBufferFragment.find("GpuMaterialFlag_ObjectSpaceNormalMap"),
              std::string::npos);
    EXPECT_NE(defaultGBufferFragment.find("ResolveSurfaceNormal"),
              std::string::npos);
    EXPECT_NE(promotedGBufferFragment.find("mat.NormalID"), std::string::npos);
    EXPECT_NE(promotedGBufferFragment.find("GpuMaterialFlag_ObjectSpaceNormalMap"), std::string::npos);
    EXPECT_NE(promotedGBufferFragment.find("ResolveSurfaceNormal"), std::string::npos);
    EXPECT_NE(surfaceFragment.find("GpuMaterialType_DefaultDebugUVs"), std::string::npos);
    EXPECT_NE(surfaceFragment.find("DebugUvChecker(fragUv)"), std::string::npos);
    EXPECT_NE(surfaceFragment.find("normalShade"), std::string::npos);
    // GRAPHICS-105: lit/unlit is decided by the material ShadingModel, not by the
    // DefaultDebugSurface material type. The type-branch must be gone from the
    // unlit gate (the only remaining type-branch is the DebugUVs checker).
    EXPECT_NE(surfaceFragment.find("GpuShadingModel_Unlit"), std::string::npos);
    EXPECT_NE(surfaceFragment.find("mat.ShadingModel"), std::string::npos);
    EXPECT_EQ(surfaceFragment.find("GpuMaterialType_DefaultDebugSurface"), std::string::npos);
    // GRAPHICS-105 Slice B: the Normal channel's attribute-vs-texture choice is
    // data-driven via the material's per-channel source, in both promoted paths.
    EXPECT_NE(surfaceFragment.find("GpuMaterialChannelSource"), std::string::npos);
    EXPECT_NE(promotedGBufferFragment.find("GpuMaterialChannelSource"), std::string::npos);
    EXPECT_NE(defaultGBufferFragment.find("fragConfigSlot"), std::string::npos);
    EXPECT_NE(defaultGBufferFragment.find("fragVisualizationScalar"), std::string::npos);
    EXPECT_NE(defaultGBufferFragment.find("fragVisualizationColor"), std::string::npos);
    EXPECT_NE(defaultGBufferFragment.find("fragInstanceSlot"), std::string::npos);
    EXPECT_NE(defaultGBufferFragment.find("GpuResolveVisualizationColorWithColormap"), std::string::npos);

    Extrinsic::Tests::MockDevice device;
    device.Operational = true;
    device.BackbufferHandle = Extrinsic::RHI::TextureHandle{181u, 1u};

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer = Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);

    const Extrinsic::RHI::PipelineHandle initialPipeline = renderer->GetForwardSurfacePipeline();
    EXPECT_TRUE(initialPipeline.IsValid());
    const Extrinsic::RHI::PipelineDesc initialDesc = renderer->GetForwardSurfacePipelineDesc();
    // The descriptor must reference the SPIR-V emitted by intrinsic_add_glsl_shaders()
    // and the depth-prepass-on path documented in
    // docs/architecture/rendering-three-pass.md. The shader pair must also
    // observe the GpuScene push-constant contract that
    // `ForwardSurfacePass::Execute()` pushes — the canonical GpuScene-aware
    // forward shader pair (`forward/default_debug_surface.{vert,frag}`)
    // matches `sizeof(GpuScenePushConstants)` and the BDA-only descriptor
    // layout. The legacy `surface.vert/frag` pair is incompatible
    // (mat4 Model + PtrPositions push block + set=2/3 SSBOs).
    EXPECT_TRUE(initialDesc.VertexShaderPath.ends_with(
        "shaders/forward/default_debug_surface.vert.spv"))
        << initialDesc.VertexShaderPath;
    EXPECT_TRUE(initialDesc.FragmentShaderPath.ends_with(
        "shaders/forward/default_debug_surface.frag.spv"))
        << initialDesc.FragmentShaderPath;
    EXPECT_EQ(initialDesc.PrimitiveTopology, Extrinsic::RHI::Topology::TriangleList);
    EXPECT_EQ(initialDesc.Rasterizer.Culling, Extrinsic::RHI::CullMode::Back);
    EXPECT_EQ(initialDesc.Rasterizer.Winding, kVulkanCameraTriangleFrontFace);
    EXPECT_TRUE(initialDesc.DepthStencil.DepthTestEnable);
    EXPECT_FALSE(initialDesc.DepthStencil.DepthWriteEnable);
    EXPECT_EQ(initialDesc.DepthStencil.DepthFunc, Extrinsic::RHI::DepthOp::Equal);
    EXPECT_FALSE(initialDesc.ColorBlend[0].Enable);
    EXPECT_EQ(initialDesc.ColorTargetCount, 1u);
    EXPECT_EQ(initialDesc.ColorTargetFormats[0], Extrinsic::RHI::Format::RGBA16_FLOAT);
    EXPECT_EQ(initialDesc.DepthTargetFormat, Extrinsic::RHI::Format::D32_FLOAT);
    EXPECT_EQ(initialDesc.PushConstantSize, sizeof(Extrinsic::RHI::GpuScenePushConstants));

    EXPECT_TRUE(renderer->RebuildOperationalResources(device));
    const Extrinsic::RHI::PipelineHandle rebuiltPipeline = renderer->GetForwardSurfacePipeline();
    EXPECT_TRUE(rebuiltPipeline.IsValid());
    const Extrinsic::RHI::PipelineDesc rebuiltDesc = renderer->GetForwardSurfacePipelineDesc();
    EXPECT_TRUE(PipelineDescBytesEqual(initialDesc, rebuiltDesc));

    renderer->Shutdown();
}

// ---------------------------------------------------------------------------
// GRAPHICS-071 — default-recipe retained line/point pipeline leases + republish
// ---------------------------------------------------------------------------

TEST(RendererFrameLifecycle, ForwardLinePointPipelinesSurviveOperationalRebuild)
{
    Extrinsic::Tests::MockDevice device;
    device.Operational = true;
    device.BackbufferHandle = Extrinsic::RHI::TextureHandle{183u, 1u};

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer = Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);

    const Extrinsic::RHI::PipelineHandle initialLinePipeline = renderer->GetForwardLinePipeline();
    const Extrinsic::RHI::PipelineHandle initialPointPipeline = renderer->GetForwardPointPipeline();
    EXPECT_TRUE(initialLinePipeline.IsValid());
    EXPECT_TRUE(initialPointPipeline.IsValid());

    const Extrinsic::RHI::PipelineDesc initialLineDesc = renderer->GetForwardLinePipelineDesc();
    EXPECT_TRUE(initialLineDesc.VertexShaderPath.ends_with("shaders/forward/line.vert.spv"))
        << initialLineDesc.VertexShaderPath;
    EXPECT_TRUE(initialLineDesc.FragmentShaderPath.ends_with("shaders/forward/line.frag.spv"))
        << initialLineDesc.FragmentShaderPath;
    EXPECT_EQ(initialLineDesc.PrimitiveTopology, Extrinsic::RHI::Topology::TriangleList);
    EXPECT_EQ(initialLineDesc.Rasterizer.Culling, Extrinsic::RHI::CullMode::None);
    EXPECT_TRUE(initialLineDesc.DepthStencil.DepthTestEnable);
    EXPECT_FALSE(initialLineDesc.DepthStencil.DepthWriteEnable);
    EXPECT_EQ(initialLineDesc.DepthStencil.DepthFunc, Extrinsic::RHI::DepthOp::LessEqual);
    EXPECT_TRUE(initialLineDesc.ColorBlend[0].Enable);
    EXPECT_EQ(initialLineDesc.ColorTargetFormats[0], Extrinsic::RHI::Format::RGBA16_FLOAT);
    EXPECT_EQ(initialLineDesc.DepthTargetFormat, Extrinsic::RHI::Format::D32_FLOAT);
    EXPECT_EQ(initialLineDesc.PushConstantSize, sizeof(Extrinsic::RHI::GpuScenePushConstants));

    const Extrinsic::RHI::PipelineDesc initialPointDesc = renderer->GetForwardPointPipelineDesc();
    EXPECT_TRUE(initialPointDesc.VertexShaderPath.ends_with("shaders/forward/point.vert.spv"))
        << initialPointDesc.VertexShaderPath;
    EXPECT_TRUE(initialPointDesc.FragmentShaderPath.ends_with("shaders/forward/point.frag.spv"))
        << initialPointDesc.FragmentShaderPath;
    EXPECT_EQ(initialPointDesc.PrimitiveTopology, Extrinsic::RHI::Topology::TriangleList);
    EXPECT_EQ(initialPointDesc.Rasterizer.Culling, Extrinsic::RHI::CullMode::None);
    EXPECT_TRUE(initialPointDesc.DepthStencil.DepthTestEnable);
    EXPECT_TRUE(initialPointDesc.DepthStencil.DepthWriteEnable);
    EXPECT_EQ(initialPointDesc.DepthStencil.DepthFunc, Extrinsic::RHI::DepthOp::LessEqual);
    EXPECT_TRUE(initialPointDesc.ColorBlend[0].Enable);
    EXPECT_EQ(initialPointDesc.ColorTargetFormats[0], Extrinsic::RHI::Format::RGBA16_FLOAT);
    EXPECT_EQ(initialPointDesc.DepthTargetFormat, Extrinsic::RHI::Format::D32_FLOAT);
    EXPECT_EQ(initialPointDesc.PushConstantSize, sizeof(Extrinsic::RHI::GpuScenePushConstants));

    EXPECT_TRUE(renderer->RebuildOperationalResources(device));
    EXPECT_TRUE(renderer->GetForwardLinePipeline().IsValid());
    EXPECT_TRUE(renderer->GetForwardPointPipeline().IsValid());
    EXPECT_TRUE(PipelineDescBytesEqual(initialLineDesc, renderer->GetForwardLinePipelineDesc()));
    EXPECT_TRUE(PipelineDescBytesEqual(initialPointDesc, renderer->GetForwardPointPipelineDesc()));

    renderer->Shutdown();
}

TEST(RendererFrameLifecycle, ForwardPointSphereImpostorsWriteCorrectedDepth)
{
    const std::string pointVertex = ReadShaderSource("forward/point.vert");
    const std::string pointFragment = ReadShaderSource("forward/point.frag");
    const std::string cullShader = ReadShaderSource("culling/instance_cull.comp");
    const std::string activeCullShader = ReadShaderSource("instance_cull.comp");

    EXPECT_NE(pointVertex.find("/ 6u"), std::string::npos);
    EXPECT_NE(pointVertex.find("ResolvePointSizePx"), std::string::npos);
    EXPECT_NE(pointVertex.find("float pointSizePx = cfg.Point.PointSize"),
              std::string::npos);
    EXPECT_NE(pointVertex.find("cfg.Point.PointSizeBDA != uint64_t(0)"),
              std::string::npos);
    EXPECT_NE(pointVertex.find(
                  "GpuFloatBufferRef(cfg.Point.PointSizeBDA).Data[pointElementId]"),
              std::string::npos);
    EXPECT_NE(pointVertex.find("clamp(pointSizePx, 0.5, 32.0)"),
              std::string::npos);
    EXPECT_NE(pointVertex.find("vDiscUV"), std::string::npos);
    EXPECT_NE(pointFragment.find("surfaceViewPos"), std::string::npos);
    EXPECT_NE(pointFragment.find("gl_FragDepth = depth"), std::string::npos);
    EXPECT_NE(cullShader.find("geo.PointVertexCount * 6u"), std::string::npos);
    EXPECT_NE(cullShader.find("geo.PointFirstVertex * 6u"), std::string::npos);
    EXPECT_NE(activeCullShader.find("buckets.LineQuads"), std::string::npos);
    EXPECT_NE(activeCullShader.find("(geo.LineIndexCount / 2u) * 6u"), std::string::npos);
}

TEST(RendererFrameLifecycle, ForwardLinePointShadersUseSharedVisualizationColorHelpers)
{
    const std::string lineVertex = ReadShaderSource("forward/line.vert");
    const std::string lineFragment = ReadShaderSource("forward/line.frag");
    const std::string pointVertex = ReadShaderSource("forward/point.vert");

    EXPECT_NE(pointVertex.find("ResolvePointVisualizationColor"), std::string::npos);
    EXPECT_NE(pointVertex.find("GpuVisualizationReadScalar"), std::string::npos);
    EXPECT_NE(pointVertex.find("GpuVisualizationReadColor"), std::string::npos);
    EXPECT_NE(pointVertex.find("GpuResolveVisualizationColorWithColormap"),
              std::string::npos);
    EXPECT_NE(pointVertex.find("GpuResolveVisualizationColorFallback"),
              std::string::npos);

    EXPECT_NE(lineVertex.find("vVisualizationScalar"), std::string::npos);
    EXPECT_NE(lineVertex.find("vVisualizationColor"), std::string::npos);
    EXPECT_NE(lineVertex.find("GpuVisualizationDomain_Vertex"), std::string::npos);
    EXPECT_NE(lineVertex.find("GpuVisualizationReadScalar"), std::string::npos);
    EXPECT_NE(lineVertex.find("GpuVisualizationReadColor"), std::string::npos);
    EXPECT_NE(lineFragment.find("GpuResolveVisualizationColorWithColormap"),
              std::string::npos);
    EXPECT_NE(lineFragment.find("GpuResolveVisualizationColorFallback"),
              std::string::npos);
}

// ---------------------------------------------------------------------------
// GRAPHICS-073 Slice A — default-recipe depth-only shadow pipeline lease +
// republish. The CPU/null contract here is the byte-identical descriptor
// across the initial init and `RebuildOperationalResources()`; Slice B adds
// the `ShadowSystem`-owned atlas/sampler + `FrameRecipeShadowSizing` import
// seam.
// ---------------------------------------------------------------------------

TEST(RendererFrameLifecycle, ShadowPipelineSurvivesOperationalRebuild)
{
    Extrinsic::Tests::MockDevice device;
    device.Operational = true;
    device.BackbufferHandle = Extrinsic::RHI::TextureHandle{185u, 1u};

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer = Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);

    const Extrinsic::RHI::PipelineHandle initialShadowPipeline = renderer->GetShadowPipeline();
    EXPECT_TRUE(initialShadowPipeline.IsValid());

    const Extrinsic::RHI::PipelineDesc initialShadowDesc = renderer->GetShadowPipelineDesc();
    EXPECT_TRUE(initialShadowDesc.VertexShaderPath.ends_with("shaders/depth_prepass.vert.spv"))
        << initialShadowDesc.VertexShaderPath;
    EXPECT_TRUE(initialShadowDesc.FragmentShaderPath.empty()) << initialShadowDesc.FragmentShaderPath;
    EXPECT_EQ(initialShadowDesc.PrimitiveTopology, Extrinsic::RHI::Topology::TriangleList);
    EXPECT_EQ(initialShadowDesc.Rasterizer.Culling, Extrinsic::RHI::CullMode::Back);
    EXPECT_EQ(initialShadowDesc.Rasterizer.Winding, kVulkanCameraTriangleFrontFace);
    EXPECT_TRUE(initialShadowDesc.DepthStencil.DepthTestEnable);
    EXPECT_TRUE(initialShadowDesc.DepthStencil.DepthWriteEnable);
    EXPECT_EQ(initialShadowDesc.DepthStencil.DepthFunc, Extrinsic::RHI::DepthOp::LessEqual);
    EXPECT_FALSE(initialShadowDesc.ColorBlend[0].Enable);
    EXPECT_EQ(initialShadowDesc.ColorTargetCount, 0u);
    EXPECT_EQ(initialShadowDesc.DepthTargetFormat, Extrinsic::RHI::Format::D32_FLOAT);
    EXPECT_EQ(initialShadowDesc.PushConstantSize, sizeof(Extrinsic::RHI::GpuScenePushConstants));

    EXPECT_TRUE(renderer->RebuildOperationalResources(device));
    EXPECT_TRUE(renderer->GetShadowPipeline().IsValid());
    EXPECT_TRUE(PipelineDescBytesEqual(initialShadowDesc, renderer->GetShadowPipelineDesc()));

    renderer->Shutdown();
}

// ---------------------------------------------------------------------------
// GRAPHICS-073 Slice B — `ShadowSystem`-owned atlas + sampler. Once shadows
// are enabled the atlas handle must stay byte-identical across an operational
// rebuild, because `RebuildOperationalResources()` only recreates pipeline +
// culling resources; the texture manager + ShadowSystem hold the atlas across
// the boundary. The runtime extraction publisher will eventually call
// `SetParams(...)` once shadow-casters arrive; in this test we mutate the
// ShadowSystem directly via `GetShadowSystem()` to avoid spinning up a full
// extraction harness.
// ---------------------------------------------------------------------------

TEST(RendererFrameLifecycle, ShadowAtlasSurvivesOperationalRebuild)
{
    Extrinsic::Tests::MockDevice device;
    device.Operational = true;
    device.BackbufferHandle = Extrinsic::RHI::TextureHandle{186u, 1u};

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer = Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);

    Extrinsic::Graphics::ShadowSystem& shadows = renderer->GetShadowSystem();
    shadows.SetParams(Extrinsic::Graphics::ShadowParams{
        .Enabled = true,
        .CascadeCount = 2u,
        .AtlasResolution = 512u,
    });

    const Extrinsic::RHI::TextureHandle atlasHandle = shadows.GetAtlasTexture();
    EXPECT_TRUE(atlasHandle.IsValid());
    const Extrinsic::RHI::SamplerHandle samplerHandle = shadows.GetAtlasSampler();
    EXPECT_TRUE(samplerHandle.IsValid());
    const Extrinsic::Graphics::ShadowAtlasDesc initialAtlas = shadows.GetAllocatedAtlasDesc();
    EXPECT_TRUE(initialAtlas.Enabled);
    EXPECT_EQ(initialAtlas.Width, 512u * 2u);
    EXPECT_EQ(initialAtlas.Height, 512u);

    EXPECT_TRUE(renderer->RebuildOperationalResources(device));

    EXPECT_EQ(shadows.GetAtlasTexture(), atlasHandle);
    EXPECT_EQ(shadows.GetAtlasSampler(), samplerHandle);
    const Extrinsic::Graphics::ShadowAtlasDesc afterRebuild = shadows.GetAllocatedAtlasDesc();
    EXPECT_EQ(afterRebuild.Width, initialAtlas.Width);
    EXPECT_EQ(afterRebuild.Height, initialAtlas.Height);
    EXPECT_EQ(afterRebuild.CascadeCount, initialAtlas.CascadeCount);

    renderer->Shutdown();
}

// GRAPHICS-070 — when culling output is unavailable (cull pipeline creation
// failed), the executor reports the forward surface pass as
// SkippedUnavailable rather than recording a draw against an empty bucket.
TEST(RendererFrameLifecycle, ForwardSurfacePassSkipsUnavailableWhenCullOutputMissing)
{
    Extrinsic::Tests::MockDevice device;
    device.Operational = true;
    device.FailPipelineCreateCall = 1; // Cull compute pipeline creation fails.
    device.BackbufferHandle = Extrinsic::RHI::TextureHandle{182u, 1u};

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer = Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);

    Extrinsic::RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));

    const Extrinsic::Graphics::RenderFrameInput input{
        .Viewport = {.Width = 128, .Height = 72},
    };
    Extrinsic::Graphics::RenderWorld world = renderer->ExtractRenderWorld(input);
    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(frame, world);

    const Extrinsic::Graphics::RenderGraphFrameStats& stats = renderer->GetLastRenderGraphStats();
    EXPECT_TRUE(stats.Compile.Succeeded) << stats.Diagnostic;
    EXPECT_TRUE(stats.Execute.Succeeded) << stats.Diagnostic;
    ASSERT_NE(FindCommandPass(stats, "SurfacePass"), nullptr);
    EXPECT_EQ(FindCommandPass(stats, "SurfacePass")->Status,
              Extrinsic::Graphics::RenderCommandPassStatus::SkippedUnavailable);
    EXPECT_EQ(device.CommandContext.DrawIndexedIndirectCountCalls, 0);

    renderer->Shutdown();
}

// GRAPHICS-071 — retained line/point pass routing is fail-closed on culling

// output availability. This preserves the transient-debug split: invalid or
// absent retained buckets soft-skip here rather than routing debug packets
// through the retained line/point lanes.
TEST(RendererFrameLifecycle, ForwardLinePointPassesSkipUnavailableWhenCullOutputMissing)
{
    Extrinsic::Tests::MockDevice device;
    device.Operational = true;
    device.FailPipelineCreateCall = 1; // Cull compute pipeline creation fails.
    device.BackbufferHandle = Extrinsic::RHI::TextureHandle{184u, 1u};

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer = Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);

    Extrinsic::RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));

    const Extrinsic::Graphics::RenderFrameInput input{
        .Viewport = {.Width = 128, .Height = 72},
    };
    Extrinsic::Graphics::RenderWorld world = renderer->ExtractRenderWorld(input);
    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(frame, world);

    const Extrinsic::Graphics::RenderGraphFrameStats& stats = renderer->GetLastRenderGraphStats();
    EXPECT_TRUE(stats.Compile.Succeeded) << stats.Diagnostic;
    EXPECT_TRUE(stats.Execute.Succeeded) << stats.Diagnostic;
    ASSERT_NE(FindCommandPass(stats, "LinePass"), nullptr);
    EXPECT_EQ(FindCommandPass(stats, "LinePass")->Status,
              Extrinsic::Graphics::RenderCommandPassStatus::SkippedUnavailable);
    ASSERT_NE(FindCommandPass(stats, "PointPass"), nullptr);
    EXPECT_EQ(FindCommandPass(stats, "PointPass")->Status,
              Extrinsic::Graphics::RenderCommandPassStatus::SkippedUnavailable);
    EXPECT_EQ(device.CommandContext.DrawIndexedIndirectCountCalls, 0);
    EXPECT_EQ(device.CommandContext.DrawIndirectCountCalls, 0);

    renderer->Shutdown();
}

// ---------------------------------------------------------------------------
// GRAPHICS-072 Slice A — default-recipe deferred GBuffer pipeline lease +
// republish, deferred-mode `"SurfacePass"` executor branch routing, and
// fail-closed `SkippedUnavailable` taxonomy when the pipeline lease is
// missing. Slice B owns the `"CompositionPass"` executor branch (deferred
// lighting); Slice C owns the shadow-atlas descriptor binding and the
// end-to-end shadow-casting recorded-for-both-passes test.
// ---------------------------------------------------------------------------

TEST(RendererFrameLifecycle, DeferredGBufferPipelineSurvivesOperationalRebuild)
{
    Extrinsic::Tests::MockDevice device;
    device.Operational = true;
    device.BackbufferHandle = Extrinsic::RHI::TextureHandle{281u, 1u};

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer = Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);

    const Extrinsic::RHI::PipelineHandle initialPipeline = renderer->GetDeferredGBufferPipeline();
    EXPECT_TRUE(initialPipeline.IsValid());
    const Extrinsic::RHI::PipelineDesc initialDesc = renderer->GetDeferredGBufferPipelineDesc();

    // GRAPHICS-072 Slice A — the deferred GBuffer pipeline MUST select
    // shaders that declare a `layout(push_constant) ScenePC` block matching
    // `RHI::GpuScenePushConstants` byte-for-byte, because
    // `DeferredGBufferPass::Execute` pushes that struct verbatim. The
    // legacy `assets/shaders/surface.vert` + `surface_gbuffer.frag` pair
    // declares the pre-GpuScene `mat4 Model + Ptr*` push block and must
    // never be referenced here (the `SceneTableBDA` pointer would land in
    // `mat4 Model[0]` and every BDA dereference would read garbage on a
    // real Vulkan run). See the renderer README "Shader push-constant
    // compatibility policy" subsection.
    EXPECT_TRUE(initialDesc.VertexShaderPath.ends_with(
        "shaders/forward/default_debug_surface.vert.spv"))
        << initialDesc.VertexShaderPath;
    EXPECT_TRUE(initialDesc.FragmentShaderPath.ends_with(
        "shaders/deferred/default_debug_gbuffer.frag.spv"))
        << initialDesc.FragmentShaderPath;
    EXPECT_EQ(initialDesc.PrimitiveTopology, Extrinsic::RHI::Topology::TriangleList);
    EXPECT_EQ(initialDesc.Rasterizer.Culling, Extrinsic::RHI::CullMode::Back);
    EXPECT_EQ(initialDesc.Rasterizer.Winding, kVulkanCameraTriangleFrontFace);
    EXPECT_TRUE(initialDesc.DepthStencil.DepthTestEnable);
    EXPECT_FALSE(initialDesc.DepthStencil.DepthWriteEnable);
    EXPECT_EQ(initialDesc.DepthStencil.DepthFunc, Extrinsic::RHI::DepthOp::Equal);
    EXPECT_EQ(initialDesc.ColorTargetCount, 3u);
    EXPECT_EQ(initialDesc.ColorTargetFormats[0], Extrinsic::RHI::Format::RGBA16_FLOAT);
    EXPECT_EQ(initialDesc.ColorTargetFormats[1], Extrinsic::RHI::Format::RGBA8_UNORM);
    EXPECT_EQ(initialDesc.ColorTargetFormats[2], Extrinsic::RHI::Format::RGBA16_FLOAT);
    EXPECT_EQ(initialDesc.DepthTargetFormat, Extrinsic::RHI::Format::D32_FLOAT);
    EXPECT_EQ(initialDesc.PushConstantSize, sizeof(Extrinsic::RHI::GpuScenePushConstants));

    EXPECT_TRUE(renderer->RebuildOperationalResources(device));
    const Extrinsic::RHI::PipelineHandle rebuiltPipeline = renderer->GetDeferredGBufferPipeline();
    EXPECT_TRUE(rebuiltPipeline.IsValid());
    const Extrinsic::RHI::PipelineDesc rebuiltDesc = renderer->GetDeferredGBufferPipelineDesc();
    EXPECT_TRUE(PipelineDescBytesEqual(initialDesc, rebuiltDesc));

    renderer->Shutdown();
}

// GRAPHICS-072 Slice A — when `SetLightingPath(Deferred)` is in effect and
// the operational publisher has produced both the cull-output and the GBuffer
// pipeline, the deferred-mode `"SurfacePass"` executor branch records the
// GBuffer pass's bind/draw shape and reports `Recorded`. The companion
// `"CompositionPass"` (deferred lighting) is owned by Slice B and currently
// soft-skips to `SkippedUnavailable`.
TEST(RendererFrameLifecycle, DeferredSurfacePassRecordsWhenLightingPathIsDeferred)
{
    Extrinsic::Tests::MockDevice device;
    device.Operational = true;
    device.BackbufferHandle = Extrinsic::RHI::TextureHandle{282u, 1u};

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer = Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);
    renderer->SetLightingPath(Extrinsic::Graphics::FrameRecipeLightingPath::Deferred);
    EXPECT_EQ(renderer->GetLightingPath(),
              Extrinsic::Graphics::FrameRecipeLightingPath::Deferred);

    Extrinsic::RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));

    const Extrinsic::Graphics::RenderFrameInput input{
        .Viewport = {.Width = 128, .Height = 72},
    };
    Extrinsic::Graphics::RenderWorld world = renderer->ExtractRenderWorld(input);
    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(frame, world);

    const Extrinsic::Graphics::RenderGraphFrameStats& stats = renderer->GetLastRenderGraphStats();
    EXPECT_TRUE(stats.Compile.Succeeded) << stats.Diagnostic;
    EXPECT_TRUE(stats.Execute.Succeeded) << stats.Diagnostic;
    ASSERT_NE(FindCommandPass(stats, "SurfacePass"), nullptr);
    EXPECT_EQ(FindCommandPass(stats, "SurfacePass")->Status,
              Extrinsic::Graphics::RenderCommandPassStatus::Recorded);
    // GRAPHICS-072 Slice B — the `"CompositionPass"` executor branch now
    // records the deferred lighting pass's `Bind/Push/Draw(3,1,0,0)`
    // fullscreen shape.
    ASSERT_NE(FindCommandPass(stats, "CompositionPass"), nullptr);
    EXPECT_EQ(FindCommandPass(stats, "CompositionPass")->Status,
              Extrinsic::Graphics::RenderCommandPassStatus::Recorded);

    renderer->Shutdown();
}

// GRAPHICS-072 Slice A — when the deferred surface pipeline could not be
// created (here, the device fails the corresponding `CreatePipeline` call),
// the deferred-mode `"SurfacePass"` executor branch reports
// `SkippedUnavailable` rather than recording against an unset pipeline
// handle. Mirrors the forward-path
// `ForwardSurfacePassSkipsUnavailableWhenCullOutputMissing` policy.
TEST(RendererFrameLifecycle, DeferredSurfacePassSkipsUnavailableWhenPipelineMissing)
{
    Extrinsic::Tests::MockDevice device;
    device.Operational = true;
    // The deferred GBuffer pipeline is the eighth (1-indexed) pipeline
    // created by the operational publisher: 1 culling compute, 2 depth
    // prepass, 3 default debug surface, 4 forward surface, 5 forward line,
    // 6 forward point, 7 shadow, 8 deferred GBuffer. Fail call #8 so all
    // upstream pipelines succeed
    // (including culling, which keeps `m_CullingOutputAvailable=true`) but
    // the GBuffer lease is left empty. The `SkippedUnavailable` taxonomy
    // distinguishes this from the `SkippedNonOperational` path.
    device.FailPipelineCreateCall = 8;
    device.BackbufferHandle = Extrinsic::RHI::TextureHandle{283u, 1u};

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer = Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);
    renderer->SetLightingPath(Extrinsic::Graphics::FrameRecipeLightingPath::Deferred);
    EXPECT_FALSE(renderer->GetDeferredGBufferPipeline().IsValid());

    Extrinsic::RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));

    const Extrinsic::Graphics::RenderFrameInput input{
        .Viewport = {.Width = 128, .Height = 72},
    };
    Extrinsic::Graphics::RenderWorld world = renderer->ExtractRenderWorld(input);
    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(frame, world);

    const Extrinsic::Graphics::RenderGraphFrameStats& stats = renderer->GetLastRenderGraphStats();
    EXPECT_TRUE(stats.Compile.Succeeded) << stats.Diagnostic;
    EXPECT_TRUE(stats.Execute.Succeeded) << stats.Diagnostic;
    ASSERT_NE(FindCommandPass(stats, "SurfacePass"), nullptr);
    EXPECT_EQ(FindCommandPass(stats, "SurfacePass")->Status,
              Extrinsic::Graphics::RenderCommandPassStatus::SkippedUnavailable);
    // GRAPHICS-072 Slice B — when the GBuffer pass cannot record (here,
    // because its pipeline lease is missing), the deferred composition pass
    // must mirror the `SkippedUnavailable` taxonomy rather than lighting
    // against uninitialized SceneNormal/Albedo/Material0 attachments. The
    // lighting pipeline itself was created successfully (call #10), so this
    // test pins the GBuffer-prerequisite gate in `RecordDeferredLightingPass`,
    // not the lighting-pipeline gate covered by
    // `DeferredLightingPassSkipsUnavailableWhenPipelineMissing`.
    ASSERT_NE(FindCommandPass(stats, "CompositionPass"), nullptr);
    EXPECT_EQ(FindCommandPass(stats, "CompositionPass")->Status,
              Extrinsic::Graphics::RenderCommandPassStatus::SkippedUnavailable);
    EXPECT_TRUE(renderer->GetDeferredLightingPipeline().IsValid());
    // Other passes (DepthPrepass, LinePass, PointPass) still record because
    // their pipelines were created successfully.

    renderer->Shutdown();
}

// GRAPHICS-072 Slice B — the deferred lighting pipeline must survive
// `RebuildOperationalResources()` byte-identically. The descriptor is also
// asserted shader-path-explicit so the shader push-constant compatibility
// policy (see `src/graphics/renderer/README.md`) is enforced at the contract
// level: the lighting pipeline MUST pair `post_fullscreen.vert.spv` with
// `deferred/lighting.frag.spv`, whose `layout(push_constant, scalar)
// PushConstants { uint64_t SceneTableBDA; uint _pad0; uint _pad1; }` block
// matches `DeferredLightingPushConstants` byte-for-byte. The legacy
// `assets/shaders/deferred_lighting.frag` declares a much larger Push block
// plus multiple descriptor sets and would silently misinterpret the pushed
// bytes — referencing it here is a known footgun that this test catches.
TEST(RendererFrameLifecycle, DeferredLightingPipelineSurvivesOperationalRebuild)
{
    Extrinsic::Tests::MockDevice device;
    device.Operational = true;
    device.BackbufferHandle = Extrinsic::RHI::TextureHandle{284u, 1u};

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer = Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);

    const Extrinsic::RHI::PipelineHandle initialPipeline = renderer->GetDeferredLightingPipeline();
    EXPECT_TRUE(initialPipeline.IsValid());
    const Extrinsic::RHI::PipelineDesc initialDesc = renderer->GetDeferredLightingPipelineDesc();

    EXPECT_TRUE(initialDesc.VertexShaderPath.ends_with(
        "shaders/post_fullscreen.vert.spv"))
        << initialDesc.VertexShaderPath;
    EXPECT_TRUE(initialDesc.FragmentShaderPath.ends_with(
        "shaders/deferred/lighting.frag.spv"))
        << initialDesc.FragmentShaderPath;
    EXPECT_EQ(initialDesc.PrimitiveTopology, Extrinsic::RHI::Topology::TriangleList);
    EXPECT_EQ(initialDesc.Rasterizer.Culling, Extrinsic::RHI::CullMode::None);
    EXPECT_FALSE(initialDesc.DepthStencil.DepthTestEnable);
    EXPECT_FALSE(initialDesc.DepthStencil.DepthWriteEnable);
    EXPECT_EQ(initialDesc.ColorTargetCount, 1u);
    EXPECT_EQ(initialDesc.ColorTargetFormats[0], Extrinsic::RHI::Format::RGBA16_FLOAT);
    EXPECT_EQ(initialDesc.DepthTargetFormat, Extrinsic::RHI::Format::Undefined);
    EXPECT_EQ(initialDesc.PushConstantSize, 16u);

    EXPECT_TRUE(renderer->RebuildOperationalResources(device));
    const Extrinsic::RHI::PipelineHandle rebuiltPipeline = renderer->GetDeferredLightingPipeline();
    EXPECT_TRUE(rebuiltPipeline.IsValid());
    const Extrinsic::RHI::PipelineDesc rebuiltDesc = renderer->GetDeferredLightingPipelineDesc();
    EXPECT_TRUE(PipelineDescBytesEqual(initialDesc, rebuiltDesc));

    renderer->Shutdown();
}

// GRAPHICS-072 Slice B — when the deferred lighting pipeline could not be
// created, the `"CompositionPass"` executor branch reports
// `SkippedUnavailable` rather than recording against an unset pipeline
// handle. The GBuffer pipeline must remain available so `"SurfacePass"`
// still records `Recorded`; only the composition pass should soft-skip.
TEST(RendererFrameLifecycle, DeferredLightingPassSkipsUnavailableWhenPipelineMissing)
{
    Extrinsic::Tests::MockDevice device;
    device.Operational = true;
    // Publisher pipeline order (1-indexed): 1 culling compute, 2 depth
    // prepass, 3 default debug surface, 4 forward surface, 5 forward line,
    // 6 forward point, 7 shadow, 8 deferred GBuffer, 9 deferred lighting.
    // Failing call #9 leaves the lighting lease empty while every upstream
    // pipeline (including the GBuffer at #8) succeeds.
    device.FailPipelineCreateCall = 9;
    device.BackbufferHandle = Extrinsic::RHI::TextureHandle{285u, 1u};

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer = Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);
    renderer->SetLightingPath(Extrinsic::Graphics::FrameRecipeLightingPath::Deferred);
    EXPECT_TRUE(renderer->GetDeferredGBufferPipeline().IsValid());
    EXPECT_FALSE(renderer->GetDeferredLightingPipeline().IsValid());

    Extrinsic::RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));

    const Extrinsic::Graphics::RenderFrameInput input{
        .Viewport = {.Width = 128, .Height = 72},
    };
    Extrinsic::Graphics::RenderWorld world = renderer->ExtractRenderWorld(input);
    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(frame, world);

    const Extrinsic::Graphics::RenderGraphFrameStats& stats = renderer->GetLastRenderGraphStats();
    EXPECT_TRUE(stats.Compile.Succeeded) << stats.Diagnostic;
    EXPECT_TRUE(stats.Execute.Succeeded) << stats.Diagnostic;
    ASSERT_NE(FindCommandPass(stats, "SurfacePass"), nullptr);
    EXPECT_EQ(FindCommandPass(stats, "SurfacePass")->Status,
              Extrinsic::Graphics::RenderCommandPassStatus::Recorded);
    ASSERT_NE(FindCommandPass(stats, "CompositionPass"), nullptr);
    EXPECT_EQ(FindCommandPass(stats, "CompositionPass")->Status,
              Extrinsic::Graphics::RenderCommandPassStatus::SkippedUnavailable);

    renderer->Shutdown();
}

// GRAPHICS-072 Slice B — between the deferred-mode `"SurfacePass"`
// (GBuffer write) and `"CompositionPass"` (GBuffer read), the frame-graph
// compiler MUST emit `ColorAttachment → ShaderReadOnly` layout transitions
// for the three GBuffer textures (`SceneNormal`, `Albedo`, `Material0`).
// The MockCommandContext interleaves `TextureBarrier` events into its
// `Events` log alongside `BindPipeline`/`DrawIndexedIndirectCount`/etc.,
// so the test can sequence: the GBuffer pass's `DrawIndexedIndirectCount`
// must precede a run of three `ColorAttachment → ShaderReadOnly`
// `TextureBarrier` events on three *distinct* texture handles, which must
// precede the lighting pass's next `BindPipeline` event.
TEST(RendererFrameLifecycle, DeferredGBufferToCompositionEmitsColorToShaderReadBarriers)
{
    using EventKind = Extrinsic::Tests::MockCommandContext::EventKind;

    Extrinsic::Tests::MockDevice device;
    device.Operational = true;
    device.BackbufferHandle = Extrinsic::RHI::TextureHandle{286u, 1u};

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer = Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);
    renderer->SetLightingPath(Extrinsic::Graphics::FrameRecipeLightingPath::Deferred);

    Extrinsic::RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));

    const Extrinsic::Graphics::RenderFrameInput input{
        .Viewport = {.Width = 128, .Height = 72},
    };
    Extrinsic::Graphics::RenderWorld world = renderer->ExtractRenderWorld(input);
    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(frame, world);

    const Extrinsic::Graphics::RenderGraphFrameStats& stats = renderer->GetLastRenderGraphStats();
    ASSERT_TRUE(stats.Compile.Succeeded) << stats.Diagnostic;
    ASSERT_TRUE(stats.Execute.Succeeded) << stats.Diagnostic;
    ASSERT_NE(FindCommandPass(stats, "SurfacePass"), nullptr);
    ASSERT_EQ(FindCommandPass(stats, "SurfacePass")->Status,
              Extrinsic::Graphics::RenderCommandPassStatus::Recorded);
    ASSERT_NE(FindCommandPass(stats, "CompositionPass"), nullptr);
    ASSERT_EQ(FindCommandPass(stats, "CompositionPass")->Status,
              Extrinsic::Graphics::RenderCommandPassStatus::Recorded);

    // GPU order for the deferred frame (mock-visible events only — the
    // mock does not tally non-indirect `Draw` calls):
    //   1) Culling: BindPipeline → PushConstants → Dispatch
    //   2) DepthPrepass: BindPipeline → BindIndexBuffer → PushConstants
    //      → DrawIndexedIndirectCount (first DIIC)
    //   3) GBuffer (deferred surface pass): BindPipeline → BindIndexBuffer
    //      → PushConstants → DrawIndexedIndirectCount (second DIIC)
    //   4) Cross-pass barriers (ColorAttachment → ShaderReadOnly ×3 for
    //      SceneNormal/Albedo/Material0).
    //   5) Lighting (CompositionPass): BindPipeline → PushConstants → Draw
    int gbufferDrawEvent = -1;
    {
        int dIIC = 0;
        for (int i = 0; i < static_cast<int>(device.CommandContext.Events.size()); ++i)
        {
            if (device.CommandContext.Events[static_cast<std::size_t>(i)] ==
                EventKind::DrawIndexedIndirectCount)
            {
                ++dIIC;
                if (dIIC == 2) { gbufferDrawEvent = i; break; }
            }
        }
    }
    ASSERT_GE(gbufferDrawEvent, 0)
        << "Expected a second DrawIndexedIndirectCount event for the deferred GBuffer pass";

    int compositionBindEvent = -1;
    for (int i = gbufferDrawEvent + 1; i < static_cast<int>(device.CommandContext.Events.size()); ++i)
    {
        if (device.CommandContext.Events[static_cast<std::size_t>(i)] ==
            EventKind::BindPipeline)
        {
            compositionBindEvent = i;
            break;
        }
    }
    ASSERT_GE(compositionBindEvent, 0)
        << "Expected a CompositionPass BindPipeline event after the GBuffer pass";

    // Walk the events between the GBuffer draw and the composition bind,
    // pulling barrier records out of `TextureBarrierCalls` in lockstep
    // with `EventKind::TextureBarrier` markers. Count distinct texture
    // handles transitioning ColorAttachment → ShaderReadOnly.
    int textureBarrierIndex = 0;
    for (int i = 0; i <= gbufferDrawEvent; ++i)
    {
        if (device.CommandContext.Events[static_cast<std::size_t>(i)] ==
            EventKind::TextureBarrier)
        {
            ++textureBarrierIndex;
        }
    }
    std::vector<Extrinsic::RHI::TextureHandle> crossPassColorToShaderRead;
    for (int i = gbufferDrawEvent + 1; i < compositionBindEvent; ++i)
    {
        if (device.CommandContext.Events[static_cast<std::size_t>(i)] !=
            EventKind::TextureBarrier)
        {
            continue;
        }
        ASSERT_LT(textureBarrierIndex,
                  static_cast<int>(device.CommandContext.TextureBarrierCalls.size()))
            << "TextureBarrier event has no matching entry in TextureBarrierCalls";
        const auto& barrier =
            device.CommandContext.TextureBarrierCalls[static_cast<std::size_t>(textureBarrierIndex)];
        ++textureBarrierIndex;
        if (barrier.Before == Extrinsic::RHI::TextureLayout::ColorAttachment &&
            barrier.After == Extrinsic::RHI::TextureLayout::ShaderReadOnly)
        {
            crossPassColorToShaderRead.push_back(barrier.Texture);
        }
    }

    EXPECT_EQ(crossPassColorToShaderRead.size(), 3u)
        << "Expected ColorAttachment→ShaderReadOnly barriers for SceneNormal, Albedo, Material0";
    // Distinct handles — the recipe declares SceneNormal/Albedo/Material0 as
    // three separate transient color attachments, not a single shared one.
    std::vector<Extrinsic::RHI::TextureHandle> unique = crossPassColorToShaderRead;
    std::sort(unique.begin(), unique.end(),
              [](const Extrinsic::RHI::TextureHandle& a, const Extrinsic::RHI::TextureHandle& b) {
                  if (a.Index != b.Index) { return a.Index < b.Index; }
                  return a.Generation < b.Generation;
              });
    unique.erase(std::unique(unique.begin(), unique.end(),
                             [](const Extrinsic::RHI::TextureHandle& a, const Extrinsic::RHI::TextureHandle& b) {
                                 return a.Index == b.Index && a.Generation == b.Generation;
                             }),
                 unique.end());
    EXPECT_EQ(unique.size(), 3u)
        << "Expected three distinct GBuffer textures transitioning ColorAttachment → ShaderReadOnly";

    renderer->Shutdown();
}

// ---------------------------------------------------------------------------
// GRAPHICS-072 Slice C — deferred lighting shadow-atlas binding.
//
// The deferred lighting pass samples the `ShadowSystem`-owned atlas through
// the engine's bindless heap; the legacy `set 1, binding 1` `sampler2DShadow`
// model from `assets/shaders/deferred_lighting.frag` cannot be honored on
// the promoted Vulkan pipeline layout (which declares only the bindless set
// at `set = 0`), so the wiring publishes the atlas slot through
// `DeferredLightingPushConstants::ShadowAtlasBindlessIndex` instead. These
// tests pin the two contracts that fall out:
//   1) the recipe emits a `DepthAttachment → ShaderReadOnly` layout
//      transition for the shadow atlas before `CompositionPass` records;
//   2) end-to-end, both `ShadowPass` and `CompositionPass` record
//      `Recorded`, and the bindless index pushed by the lighting pass
//      matches `ShadowSystem::GetAtlasBindlessIndex()`.
// ---------------------------------------------------------------------------

TEST(RendererFrameLifecycle, DeferredLightingShadowAtlasTransitionsDepthToShaderReadBeforeComposition)
{
    using EventKind = Extrinsic::Tests::MockCommandContext::EventKind;

    Extrinsic::Tests::MockDevice device;
    device.Operational = true;
    device.BackbufferHandle = Extrinsic::RHI::TextureHandle{287u, 1u};

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer = Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);
    renderer->SetLightingPath(Extrinsic::Graphics::FrameRecipeLightingPath::Deferred);

    // Enable shadows so the recipe declares ShadowPass + the
    // CompositionPass shadow-atlas read. Allocating the atlas through
    // `SetParams` lets `FrameRecipeImports::ShadowAtlas` carry the
    // ShadowSystem-owned handle into the recipe, so the barrier the test
    // looks for transitions that specific texture.
    Extrinsic::Graphics::ShadowSystem& shadows = renderer->GetShadowSystem();
    shadows.SetParams(Extrinsic::Graphics::ShadowParams{
        .Enabled         = true,
        .CascadeCount    = 2u,
        .AtlasResolution = 256u,
    });
    const Extrinsic::RHI::TextureHandle shadowAtlasHandle = shadows.GetAtlasTexture();
    ASSERT_TRUE(shadowAtlasHandle.IsValid())
        << "ShadowSystem must lazily allocate the atlas after SetParams enables shadows";

    Extrinsic::RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));

    const Extrinsic::Graphics::RenderFrameInput input{
        .Viewport = {.Width = 128, .Height = 72},
    };
    Extrinsic::Graphics::RenderWorld world = renderer->ExtractRenderWorld(input);
    // The default ExtractRenderWorld does not populate Shadows from the
    // ShadowSystem (runtime publishes that separately). Mirror the runtime
    // contract here so `DeriveDefaultFrameRecipeFeatures` flips
    // `EnableShadows` on for this frame.
    world.Shadows.Enabled         = true;
    world.Shadows.CascadeCount    = 2u;
    world.Shadows.AtlasResolution = 256u;
    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(frame, world);

    const Extrinsic::Graphics::RenderGraphFrameStats& stats = renderer->GetLastRenderGraphStats();
    ASSERT_TRUE(stats.Compile.Succeeded) << stats.Diagnostic;
    ASSERT_TRUE(stats.Execute.Succeeded) << stats.Diagnostic;
    ASSERT_NE(FindCommandPass(stats, "ShadowPass"), nullptr);
    EXPECT_EQ(FindCommandPass(stats, "ShadowPass")->Status,
              Extrinsic::Graphics::RenderCommandPassStatus::Recorded);
    ASSERT_NE(FindCommandPass(stats, "CompositionPass"), nullptr);
    EXPECT_EQ(FindCommandPass(stats, "CompositionPass")->Status,
              Extrinsic::Graphics::RenderCommandPassStatus::Recorded);

    // Locate the lighting pass's bind in the event stream — the
    // `CompositionPass` body opens with `BindPipeline` for the deferred
    // lighting pipeline. The shadow-atlas barrier MUST precede that bind.
    // The DeferredGBufferToCompositionEmitsColorToShaderReadBarriers test
    // covers the SceneNormal/Albedo/Material0 transitions emitted at the
    // same boundary; this test adds the parallel shadow-atlas check.
    int gbufferDrawEvent = -1;
    {
        int dIIC = 0;
        for (int i = 0; i < static_cast<int>(device.CommandContext.Events.size()); ++i)
        {
            if (device.CommandContext.Events[static_cast<std::size_t>(i)] ==
                EventKind::DrawIndexedIndirectCount)
            {
                ++dIIC;
                // (1) DepthPrepass DIIC, (2) ShadowPass DIIC, (3) GBuffer DIIC.
                if (dIIC == 3) { gbufferDrawEvent = i; break; }
            }
        }
    }
    ASSERT_GE(gbufferDrawEvent, 0)
        << "Expected a third DrawIndexedIndirectCount event for the deferred "
           "GBuffer pass (after DepthPrepass and ShadowPass)";

    int compositionBindEvent = -1;
    for (int i = gbufferDrawEvent + 1; i < static_cast<int>(device.CommandContext.Events.size()); ++i)
    {
        if (device.CommandContext.Events[static_cast<std::size_t>(i)] ==
            EventKind::BindPipeline)
        {
            compositionBindEvent = i;
            break;
        }
    }
    ASSERT_GE(compositionBindEvent, 0)
        << "Expected a CompositionPass BindPipeline event after the GBuffer pass";

    // Walk barrier records in lockstep with TextureBarrier events between
    // gbufferDrawEvent and compositionBindEvent. Track records that
    // transition the ShadowSystem-owned atlas from DepthAttachment →
    // ShaderReadOnly.
    int textureBarrierIndex = 0;
    for (int i = 0; i <= gbufferDrawEvent; ++i)
    {
        if (device.CommandContext.Events[static_cast<std::size_t>(i)] ==
            EventKind::TextureBarrier)
        {
            ++textureBarrierIndex;
        }
    }
    bool sawShadowAtlasDepthToShaderRead = false;
    for (int i = gbufferDrawEvent + 1; i < compositionBindEvent; ++i)
    {
        if (device.CommandContext.Events[static_cast<std::size_t>(i)] !=
            EventKind::TextureBarrier)
        {
            continue;
        }
        ASSERT_LT(textureBarrierIndex,
                  static_cast<int>(device.CommandContext.TextureBarrierCalls.size()))
            << "TextureBarrier event has no matching entry in TextureBarrierCalls";
        const auto& barrier =
            device.CommandContext.TextureBarrierCalls[static_cast<std::size_t>(textureBarrierIndex)];
        ++textureBarrierIndex;
        if (barrier.Texture == shadowAtlasHandle &&
            barrier.Before == Extrinsic::RHI::TextureLayout::DepthAttachment &&
            barrier.After == Extrinsic::RHI::TextureLayout::ShaderReadOnly)
        {
            sawShadowAtlasDepthToShaderRead = true;
        }
    }
    EXPECT_TRUE(sawShadowAtlasDepthToShaderRead)
        << "Expected a DepthAttachment → ShaderReadOnly barrier on the "
           "ShadowSystem-owned shadow atlas between the GBuffer pass and "
           "the deferred lighting pass";

    renderer->Shutdown();
}

TEST(RendererFrameLifecycle, DeferredLightingPushConstantsCarryShadowAtlasBindlessIndex)
{
    using EventKind = Extrinsic::Tests::MockCommandContext::EventKind;

    Extrinsic::Tests::MockDevice device;
    device.Operational = true;
    device.BackbufferHandle = Extrinsic::RHI::TextureHandle{288u, 1u};

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer = Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);
    renderer->SetLightingPath(Extrinsic::Graphics::FrameRecipeLightingPath::Deferred);

    Extrinsic::Graphics::ShadowSystem& shadows = renderer->GetShadowSystem();
    shadows.SetParams(Extrinsic::Graphics::ShadowParams{
        .Enabled         = true,
        .CascadeCount    = 2u,
        .AtlasResolution = 256u,
    });
    const Extrinsic::RHI::BindlessIndex shadowAtlasBindlessIndex =
        shadows.GetAtlasBindlessIndex();
    ASSERT_NE(shadowAtlasBindlessIndex, Extrinsic::RHI::kInvalidBindlessIndex)
        << "ShadowSystem must register the atlas in the bindless heap after "
           "SetParams enables shadows";

    Extrinsic::RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));

    const Extrinsic::Graphics::RenderFrameInput input{
        .Viewport = {.Width = 128, .Height = 72},
    };
    Extrinsic::Graphics::RenderWorld world = renderer->ExtractRenderWorld(input);
    world.Shadows.Enabled         = true;
    world.Shadows.CascadeCount    = 2u;
    world.Shadows.AtlasResolution = 256u;
    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(frame, world);

    const Extrinsic::Graphics::RenderGraphFrameStats& stats = renderer->GetLastRenderGraphStats();
    ASSERT_TRUE(stats.Compile.Succeeded) << stats.Diagnostic;
    ASSERT_TRUE(stats.Execute.Succeeded) << stats.Diagnostic;

    // End-to-end shadow-casting contract: both passes record Recorded.
    ASSERT_NE(FindCommandPass(stats, "ShadowPass"), nullptr);
    EXPECT_EQ(FindCommandPass(stats, "ShadowPass")->Status,
              Extrinsic::Graphics::RenderCommandPassStatus::Recorded);
    ASSERT_NE(FindCommandPass(stats, "CompositionPass"), nullptr);
    EXPECT_EQ(FindCommandPass(stats, "CompositionPass")->Status,
              Extrinsic::Graphics::RenderCommandPassStatus::Recorded);

    // Find the lighting pass's PushConstants payload. The CompositionPass
    // body's first BindPipeline marks the start; the next PushConstants
    // event after it carries the deferred lighting push-constant block.
    // Map the event index to PushConstantPayloads[] by counting prior
    // PushConstants events in submission order.
    int gbufferDrawEvent = -1;
    {
        int dIIC = 0;
        for (int i = 0; i < static_cast<int>(device.CommandContext.Events.size()); ++i)
        {
            if (device.CommandContext.Events[static_cast<std::size_t>(i)] ==
                EventKind::DrawIndexedIndirectCount)
            {
                ++dIIC;
                if (dIIC == 3) { gbufferDrawEvent = i; break; }
            }
        }
    }
    ASSERT_GE(gbufferDrawEvent, 0);

    int compositionBindEvent = -1;
    for (int i = gbufferDrawEvent + 1; i < static_cast<int>(device.CommandContext.Events.size()); ++i)
    {
        if (device.CommandContext.Events[static_cast<std::size_t>(i)] ==
            EventKind::BindPipeline)
        {
            compositionBindEvent = i;
            break;
        }
    }
    ASSERT_GE(compositionBindEvent, 0);

    int compositionPushConstantsEvent = -1;
    for (int i = compositionBindEvent + 1; i < static_cast<int>(device.CommandContext.Events.size()); ++i)
    {
        if (device.CommandContext.Events[static_cast<std::size_t>(i)] ==
            EventKind::PushConstants)
        {
            compositionPushConstantsEvent = i;
            break;
        }
    }
    ASSERT_GE(compositionPushConstantsEvent, 0)
        << "Expected a PushConstants event after the deferred lighting BindPipeline";

    int payloadIndex = 0;
    for (int i = 0; i < compositionPushConstantsEvent; ++i)
    {
        if (device.CommandContext.Events[static_cast<std::size_t>(i)] ==
            EventKind::PushConstants)
        {
            ++payloadIndex;
        }
    }
    ASSERT_LT(static_cast<std::size_t>(payloadIndex),
              device.CommandContext.PushConstantPayloads.size())
        << "PushConstants event has no matching entry in PushConstantPayloads";

    const auto& payload = device.CommandContext.PushConstantPayloads[
        static_cast<std::size_t>(payloadIndex)];
    // The deferred lighting block: 8 bytes SceneTableBDA + 4 bytes
    // ShadowAtlasBindlessIndex + 4 bytes padding.
    ASSERT_EQ(payload.size(), 16u);
    std::uint32_t pushedShadowIndex = 0u;
    std::memcpy(&pushedShadowIndex, payload.data() + sizeof(std::uint64_t),
                sizeof(std::uint32_t));
    EXPECT_EQ(pushedShadowIndex, shadowAtlasBindlessIndex)
        << "DeferredLightingPass::Execute must push ShadowSystem::GetAtlasBindlessIndex() "
           "as the ShadowAtlasBindlessIndex push-constant field (the bindless-heap "
           "equivalent of the legacy `set 1, binding 1` shadow-atlas binding)";

    renderer->Shutdown();
}

// ---------------------------------------------------------------------------
// GRAPHICS-074 Slice A — default-recipe EntityId selection pipeline lease +
// republish. The CPU/null contract here is the byte-identical descriptor
// across the initial init and `RebuildOperationalResources()`, plus the
// shader-path and color-target assertions that catch the GpuScene-aware
// shader-pair contract (the legacy `assets/shaders/pick_id.{vert,frag}` is a
// known footgun because it declares the pre-GpuScene `mat4 Model +
// PtrPositions + ... + uint EntityID` push block and would silently
// misinterpret the `RHI::GpuScenePushConstants` bytes that
// `EntityIdPass::Execute` pushes).
//
// Render-pass compatibility: GRAPHICS-074's recipe-side follow-up reordered
// `BuildDefaultFrameRecipe` so `PickingPass` runs *after* `DepthPrepass` and
// declares `Read(SceneDepth, DepthRead)`. The framegraph compiler therefore
// emits a render pass with a `D32_FLOAT` depth attachment in read-only state,
// so this pipeline mirrors the depth-equal / depth-write-off shape the
// forward and deferred GBuffer pipelines use. The depth-equal test guarantees
// only the nearest-surface fragment wins each pixel — without it the readback
// drain would return wrong IDs for any pixel covered by more than one draw.
// The recipe gates picking on `EnablePicking && EnableDepthPrepass`, so the
// pipeline is only requested when a populated `SceneDepth` is available.
// ---------------------------------------------------------------------------

TEST(RendererFrameLifecycle, EntityIdPickingPipelineSurvivesOperationalRebuild)
{
    Extrinsic::Tests::MockDevice device;
    device.Operational = true;
    device.BackbufferHandle = Extrinsic::RHI::TextureHandle{287u, 1u};

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer = Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);

    const Extrinsic::RHI::PipelineHandle initialPipeline = renderer->GetSelectionEntityIdPipeline();
    EXPECT_TRUE(initialPipeline.IsValid());

    const Extrinsic::RHI::PipelineDesc initialDesc = renderer->GetSelectionEntityIdPipelineDesc();
    EXPECT_TRUE(initialDesc.VertexShaderPath.ends_with(
        "shaders/selection/entity_id.vert.spv"))
        << initialDesc.VertexShaderPath;
    EXPECT_TRUE(initialDesc.FragmentShaderPath.ends_with(
        "shaders/selection/entity_id.frag.spv"))
        << initialDesc.FragmentShaderPath;
    EXPECT_EQ(initialDesc.PrimitiveTopology, Extrinsic::RHI::Topology::TriangleList);
    EXPECT_EQ(initialDesc.Rasterizer.Culling, Extrinsic::RHI::CullMode::Back);
    EXPECT_EQ(initialDesc.Rasterizer.Winding, kVulkanCameraTriangleFrontFace);
    // Render-pass compatibility with the recipe-declared depth-read
    // PickingPass: depth-test on, depth-equal, depth-write off, D32_FLOAT.
    EXPECT_TRUE(initialDesc.DepthStencil.DepthTestEnable);
    EXPECT_FALSE(initialDesc.DepthStencil.DepthWriteEnable);
    EXPECT_EQ(initialDesc.DepthStencil.DepthFunc, Extrinsic::RHI::DepthOp::Equal);
    EXPECT_FALSE(initialDesc.ColorBlend[0].Enable);
    EXPECT_FALSE(initialDesc.ColorBlend[1].Enable);
    EXPECT_EQ(initialDesc.ColorTargetCount, 2u);
    EXPECT_EQ(initialDesc.ColorTargetFormats[0], Extrinsic::RHI::Format::R32_UINT);
    EXPECT_EQ(initialDesc.ColorTargetFormats[1], Extrinsic::RHI::Format::R32_UINT);
    EXPECT_EQ(initialDesc.DepthTargetFormat, Extrinsic::RHI::Format::D32_FLOAT);
    EXPECT_EQ(initialDesc.PushConstantSize, sizeof(Extrinsic::RHI::GpuScenePushConstants));

    EXPECT_TRUE(renderer->RebuildOperationalResources(device));
    const Extrinsic::RHI::PipelineHandle rebuiltPipeline = renderer->GetSelectionEntityIdPipeline();
    EXPECT_TRUE(rebuiltPipeline.IsValid());
    const Extrinsic::RHI::PipelineDesc rebuiltDesc = renderer->GetSelectionEntityIdPipelineDesc();
    EXPECT_TRUE(PipelineDescBytesEqual(initialDesc, rebuiltDesc));

    renderer->Shutdown();
}

TEST(RendererFrameLifecycle, EntityIdOutlinePipelineUsesSingleTargetShape)
{
    Extrinsic::Tests::MockDevice device;
    device.Operational = true;
    device.BackbufferHandle = Extrinsic::RHI::TextureHandle{327u, 1u};

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer = Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);

    const Extrinsic::RHI::PipelineHandle initialPipeline =
        renderer->GetSelectionEntityIdOutlinePipeline();
    EXPECT_TRUE(initialPipeline.IsValid());

    const Extrinsic::RHI::PipelineDesc initialDesc =
        renderer->GetSelectionEntityIdOutlinePipelineDesc();
    EXPECT_TRUE(initialDesc.VertexShaderPath.ends_with(
        "shaders/selection/entity_id.vert.spv"))
        << initialDesc.VertexShaderPath;
    EXPECT_TRUE(initialDesc.FragmentShaderPath.ends_with(
        "shaders/selection/entity_id_outline.frag.spv"))
        << initialDesc.FragmentShaderPath;
    EXPECT_EQ(initialDesc.PrimitiveTopology, Extrinsic::RHI::Topology::TriangleList);
    EXPECT_EQ(initialDesc.Rasterizer.Culling, Extrinsic::RHI::CullMode::Back);
    EXPECT_EQ(initialDesc.Rasterizer.Winding, kVulkanCameraTriangleFrontFace);
    EXPECT_TRUE(initialDesc.DepthStencil.DepthTestEnable);
    EXPECT_FALSE(initialDesc.DepthStencil.DepthWriteEnable);
    EXPECT_EQ(initialDesc.DepthStencil.DepthFunc, Extrinsic::RHI::DepthOp::Equal);
    EXPECT_FALSE(initialDesc.ColorBlend[0].Enable);
    EXPECT_EQ(initialDesc.ColorTargetCount, 1u);
    EXPECT_EQ(initialDesc.ColorTargetFormats[0], Extrinsic::RHI::Format::R32_UINT);
    EXPECT_EQ(initialDesc.ColorTargetFormats[1], Extrinsic::RHI::Format::Undefined);
    EXPECT_EQ(initialDesc.DepthTargetFormat, Extrinsic::RHI::Format::D32_FLOAT);
    EXPECT_EQ(initialDesc.PushConstantSize, sizeof(Extrinsic::RHI::GpuScenePushConstants));

    EXPECT_TRUE(renderer->RebuildOperationalResources(device));
    const Extrinsic::RHI::PipelineHandle rebuiltPipeline =
        renderer->GetSelectionEntityIdOutlinePipeline();
    EXPECT_TRUE(rebuiltPipeline.IsValid());
    const Extrinsic::RHI::PipelineDesc rebuiltDesc =
        renderer->GetSelectionEntityIdOutlinePipelineDesc();
    EXPECT_TRUE(PipelineDescBytesEqual(initialDesc, rebuiltDesc));

    renderer->Shutdown();
}

// ---------------------------------------------------------------------------
// GRAPHICS-074 Slice B — default-recipe Face / Edge / Point selection ID
// pipeline lease + republish. Each test mirrors the EntityId pipeline check:
// the operational publisher creates the pipeline on initial init, the
// descriptor matches the shader-pair contract (the legacy
// `assets/shaders/pick_mesh.{vert,frag}` / `pick_line.{vert,frag}` /
// `pick_point.{vert,frag}` shaders are pre-GpuScene footguns and are
// deliberately *not* used here — see `src/graphics/renderer/README.md`
// "Shader push-constant compatibility policy"), and
// `RebuildOperationalResources()` republishes a byte-identical descriptor.
// All three pipelines share the EntityId pipeline's depth-equal / depth-write-
// off / two-R32_UINT-color-target render-pass shape so they can be bound
// inside the same recipe-declared `PickingPass` render pass; they differ only
// in primitive topology and cull mode.
// ---------------------------------------------------------------------------

TEST(RendererFrameLifecycle, FaceIdPickingPipelineSurvivesOperationalRebuild)
{
    Extrinsic::Tests::MockDevice device;
    device.Operational = true;
    device.BackbufferHandle = Extrinsic::RHI::TextureHandle{288u, 1u};

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer = Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);

    const Extrinsic::RHI::PipelineHandle initialPipeline = renderer->GetSelectionFaceIdPipeline();
    EXPECT_TRUE(initialPipeline.IsValid());

    const Extrinsic::RHI::PipelineDesc initialDesc = renderer->GetSelectionFaceIdPipelineDesc();
    EXPECT_TRUE(initialDesc.VertexShaderPath.ends_with(
        "shaders/selection/face_id.vert.spv"))
        << initialDesc.VertexShaderPath;
    EXPECT_TRUE(initialDesc.FragmentShaderPath.ends_with(
        "shaders/selection/face_id.frag.spv"))
        << initialDesc.FragmentShaderPath;
    EXPECT_EQ(initialDesc.PrimitiveTopology, Extrinsic::RHI::Topology::TriangleList);
    EXPECT_EQ(initialDesc.Rasterizer.Culling, Extrinsic::RHI::CullMode::Back);
    EXPECT_EQ(initialDesc.Rasterizer.Winding, kVulkanCameraTriangleFrontFace);
    EXPECT_TRUE(initialDesc.DepthStencil.DepthTestEnable);
    EXPECT_FALSE(initialDesc.DepthStencil.DepthWriteEnable);
    EXPECT_EQ(initialDesc.DepthStencil.DepthFunc, Extrinsic::RHI::DepthOp::Equal);
    EXPECT_FALSE(initialDesc.ColorBlend[0].Enable);
    EXPECT_FALSE(initialDesc.ColorBlend[1].Enable);
    EXPECT_EQ(initialDesc.ColorTargetCount, 2u);
    EXPECT_EQ(initialDesc.ColorTargetFormats[0], Extrinsic::RHI::Format::R32_UINT);
    EXPECT_EQ(initialDesc.ColorTargetFormats[1], Extrinsic::RHI::Format::R32_UINT);
    EXPECT_EQ(initialDesc.DepthTargetFormat, Extrinsic::RHI::Format::D32_FLOAT);
    EXPECT_EQ(initialDesc.PushConstantSize, sizeof(Extrinsic::RHI::GpuScenePushConstants));

    EXPECT_TRUE(renderer->RebuildOperationalResources(device));
    const Extrinsic::RHI::PipelineHandle rebuiltPipeline = renderer->GetSelectionFaceIdPipeline();
    EXPECT_TRUE(rebuiltPipeline.IsValid());
    const Extrinsic::RHI::PipelineDesc rebuiltDesc = renderer->GetSelectionFaceIdPipelineDesc();
    EXPECT_TRUE(PipelineDescBytesEqual(initialDesc, rebuiltDesc));

    renderer->Shutdown();
}

TEST(RendererFrameLifecycle, EdgeIdPickingPipelineSurvivesOperationalRebuild)
{
    Extrinsic::Tests::MockDevice device;
    device.Operational = true;
    device.BackbufferHandle = Extrinsic::RHI::TextureHandle{289u, 1u};

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer = Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);

    const Extrinsic::RHI::PipelineHandle initialPipeline = renderer->GetSelectionEdgeIdPipeline();
    EXPECT_TRUE(initialPipeline.IsValid());

    const Extrinsic::RHI::PipelineDesc initialDesc = renderer->GetSelectionEdgeIdPipelineDesc();
    EXPECT_TRUE(initialDesc.VertexShaderPath.ends_with(
        "shaders/selection/edge_id.vert.spv"))
        << initialDesc.VertexShaderPath;
    EXPECT_TRUE(initialDesc.FragmentShaderPath.ends_with(
        "shaders/selection/edge_id.frag.spv"))
        << initialDesc.FragmentShaderPath;
    EXPECT_EQ(initialDesc.PrimitiveTopology, Extrinsic::RHI::Topology::LineList);
    EXPECT_EQ(initialDesc.Rasterizer.Culling, Extrinsic::RHI::CullMode::None);
    EXPECT_EQ(initialDesc.Rasterizer.Winding, Extrinsic::RHI::FrontFace::CounterClockwise);
    EXPECT_TRUE(initialDesc.DepthStencil.DepthTestEnable);
    EXPECT_FALSE(initialDesc.DepthStencil.DepthWriteEnable);
    EXPECT_EQ(initialDesc.DepthStencil.DepthFunc, Extrinsic::RHI::DepthOp::Equal);
    EXPECT_FALSE(initialDesc.ColorBlend[0].Enable);
    EXPECT_FALSE(initialDesc.ColorBlend[1].Enable);
    EXPECT_EQ(initialDesc.ColorTargetCount, 2u);
    EXPECT_EQ(initialDesc.ColorTargetFormats[0], Extrinsic::RHI::Format::R32_UINT);
    EXPECT_EQ(initialDesc.ColorTargetFormats[1], Extrinsic::RHI::Format::R32_UINT);
    EXPECT_EQ(initialDesc.DepthTargetFormat, Extrinsic::RHI::Format::D32_FLOAT);
    EXPECT_EQ(initialDesc.PushConstantSize, sizeof(Extrinsic::RHI::GpuScenePushConstants));

    EXPECT_TRUE(renderer->RebuildOperationalResources(device));
    const Extrinsic::RHI::PipelineHandle rebuiltPipeline = renderer->GetSelectionEdgeIdPipeline();
    EXPECT_TRUE(rebuiltPipeline.IsValid());
    const Extrinsic::RHI::PipelineDesc rebuiltDesc = renderer->GetSelectionEdgeIdPipelineDesc();
    EXPECT_TRUE(PipelineDescBytesEqual(initialDesc, rebuiltDesc));

    renderer->Shutdown();
}

TEST(RendererFrameLifecycle, PointIdPickingPipelineSurvivesOperationalRebuild)
{
    Extrinsic::Tests::MockDevice device;
    device.Operational = true;
    device.BackbufferHandle = Extrinsic::RHI::TextureHandle{290u, 1u};

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer = Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);

    const Extrinsic::RHI::PipelineHandle initialPipeline = renderer->GetSelectionPointIdPipeline();
    EXPECT_TRUE(initialPipeline.IsValid());

    const Extrinsic::RHI::PipelineDesc initialDesc = renderer->GetSelectionPointIdPipelineDesc();
    EXPECT_TRUE(initialDesc.VertexShaderPath.ends_with(
        "shaders/selection/point_id.vert.spv"))
        << initialDesc.VertexShaderPath;
    EXPECT_TRUE(initialDesc.FragmentShaderPath.ends_with(
        "shaders/selection/point_id.frag.spv"))
        << initialDesc.FragmentShaderPath;
    EXPECT_EQ(initialDesc.PrimitiveTopology, Extrinsic::RHI::Topology::PointList);
    EXPECT_EQ(initialDesc.Rasterizer.Culling, Extrinsic::RHI::CullMode::None);
    EXPECT_EQ(initialDesc.Rasterizer.Winding, Extrinsic::RHI::FrontFace::CounterClockwise);
    EXPECT_TRUE(initialDesc.DepthStencil.DepthTestEnable);
    EXPECT_FALSE(initialDesc.DepthStencil.DepthWriteEnable);
    EXPECT_EQ(initialDesc.DepthStencil.DepthFunc, Extrinsic::RHI::DepthOp::Equal);
    EXPECT_FALSE(initialDesc.ColorBlend[0].Enable);
    EXPECT_FALSE(initialDesc.ColorBlend[1].Enable);
    EXPECT_EQ(initialDesc.ColorTargetCount, 2u);
    EXPECT_EQ(initialDesc.ColorTargetFormats[0], Extrinsic::RHI::Format::R32_UINT);
    EXPECT_EQ(initialDesc.ColorTargetFormats[1], Extrinsic::RHI::Format::R32_UINT);
    EXPECT_EQ(initialDesc.DepthTargetFormat, Extrinsic::RHI::Format::D32_FLOAT);
    EXPECT_EQ(initialDesc.PushConstantSize, sizeof(Extrinsic::RHI::GpuScenePushConstants));

    EXPECT_TRUE(renderer->RebuildOperationalResources(device));
    const Extrinsic::RHI::PipelineHandle rebuiltPipeline = renderer->GetSelectionPointIdPipeline();
    EXPECT_TRUE(rebuiltPipeline.IsValid());
    const Extrinsic::RHI::PipelineDesc rebuiltDesc = renderer->GetSelectionPointIdPipelineDesc();
    EXPECT_TRUE(PipelineDescBytesEqual(initialDesc, rebuiltDesc));

    renderer->Shutdown();
}

// ---------------------------------------------------------------------------
// GRAPHICS-074 Slice C — default-recipe selection outline pipeline lease +
// republish, and executor-branch routing. The outline pipeline is a fullscreen
// quad (`post_fullscreen.vert` + `selection_outline.frag`) bound by the
// `"SelectionOutlinePass"` executor branch when the recipe's
// `features.EnableSelectionOutline` is true (driven by
// `world.Selection.HasHovered || !world.Selection.SelectedStableIds.empty()`
// in `DeriveDefaultFrameRecipeFeatures`).
// ---------------------------------------------------------------------------

TEST(RendererFrameLifecycle, SelectionOutlinePipelineSurvivesOperationalRebuild)
{
    Extrinsic::Tests::MockDevice device;
    device.Operational = true;
    device.BackbufferHandle = Extrinsic::RHI::TextureHandle{291u, 1u};

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer = Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);

    const Extrinsic::RHI::PipelineHandle initialPipeline = renderer->GetSelectionOutlinePipeline();
    EXPECT_TRUE(initialPipeline.IsValid());

    const Extrinsic::RHI::PipelineDesc initialDesc = renderer->GetSelectionOutlinePipelineDesc();
    EXPECT_TRUE(initialDesc.VertexShaderPath.ends_with(
        "shaders/post_fullscreen.vert.spv"))
        << initialDesc.VertexShaderPath;
    EXPECT_TRUE(initialDesc.FragmentShaderPath.ends_with(
        "shaders/selection_outline.frag.spv"))
        << initialDesc.FragmentShaderPath;
    EXPECT_EQ(initialDesc.PrimitiveTopology, Extrinsic::RHI::Topology::TriangleList);
    EXPECT_EQ(initialDesc.Rasterizer.Culling, Extrinsic::RHI::CullMode::None);
    EXPECT_FALSE(initialDesc.DepthStencil.DepthTestEnable);
    EXPECT_FALSE(initialDesc.DepthStencil.DepthWriteEnable);
    EXPECT_TRUE(initialDesc.ColorBlend[0].Enable);
    EXPECT_EQ(initialDesc.ColorBlend[0].SrcColorFactor, Extrinsic::RHI::BlendFactor::SrcAlpha);
    EXPECT_EQ(initialDesc.ColorBlend[0].DstColorFactor, Extrinsic::RHI::BlendFactor::OneMinusSrcAlpha);
    EXPECT_EQ(initialDesc.ColorBlend[0].ColorOp, Extrinsic::RHI::BlendOp::Add);
    EXPECT_EQ(initialDesc.ColorBlend[0].SrcAlphaFactor, Extrinsic::RHI::BlendFactor::One);
    EXPECT_EQ(initialDesc.ColorBlend[0].DstAlphaFactor, Extrinsic::RHI::BlendFactor::OneMinusSrcAlpha);
    EXPECT_EQ(initialDesc.ColorBlend[0].AlphaOp, Extrinsic::RHI::BlendOp::Add);
    EXPECT_EQ(initialDesc.ColorTargetCount, 1u);
    // Color target matches the recipe's current present source, whose format
    // is `FrameRecipeSizing::BackbufferFormat`; the MockDevice does not
    // override `GetBackbufferFormat()` so the renderer's stored format is
    // `RHI::Format::RGBA8_UNORM`.
    EXPECT_EQ(initialDesc.ColorTargetFormats[0], Extrinsic::RHI::Format::RGBA8_UNORM);
    // Render-pass-compatible with the recipe-declared depth attachment
    // (`builder.Read(SceneDepth, DepthRead)`) even though the pipeline does
    // not test or write depth itself.
    EXPECT_EQ(initialDesc.DepthTargetFormat, Extrinsic::RHI::Format::D32_FLOAT);
    // Matches `SelectionOutlinePushConstants` in `Pass.Selection.Outline.cpp`,
    // which mirrors the `selection_outline.frag` `Push` block byte-for-byte
    // (vec4 OutlineColor + vec4 HoverColor + 12 floats/uints + uint[16]
    // SelectedIds = 144 bytes under Vulkan std430). The pass body pushes a
    // zero-initialised instance every frame so the shader sees defined
    // values rather than stale push memory left by a prior draw.
    EXPECT_EQ(initialDesc.PushConstantSize, 144u);

    EXPECT_TRUE(renderer->RebuildOperationalResources(device));
    const Extrinsic::RHI::PipelineHandle rebuiltPipeline = renderer->GetSelectionOutlinePipeline();
    EXPECT_TRUE(rebuiltPipeline.IsValid());
    const Extrinsic::RHI::PipelineDesc rebuiltDesc = renderer->GetSelectionOutlinePipelineDesc();
    EXPECT_TRUE(PipelineDescBytesEqual(initialDesc, rebuiltDesc));

    renderer->Shutdown();
}

// ---------------------------------------------------------------------------
// GRAPHICS-075 Slice A — default-recipe postprocess tonemap pipeline lease +
// republish. Mirrors the SelectionOutline rebuild test above for the
// fullscreen `post_fullscreen.vert` + `post_tonemap.frag` shader pair.
// ---------------------------------------------------------------------------

TEST(RendererFrameLifecycle, PostProcessToneMapPipelineSurvivesOperationalRebuild)
{
    Extrinsic::Tests::MockDevice device;
    device.Operational = true;
    device.BackbufferHandle = Extrinsic::RHI::TextureHandle{293u, 1u};

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer = Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);

    const Extrinsic::RHI::PipelineHandle initialPipeline = renderer->GetPostProcessToneMapPipeline();
    EXPECT_TRUE(initialPipeline.IsValid());

    const Extrinsic::RHI::PipelineDesc initialDesc = renderer->GetPostProcessToneMapPipelineDesc();
    EXPECT_TRUE(initialDesc.VertexShaderPath.ends_with(
        "shaders/post_fullscreen.vert.spv"))
        << initialDesc.VertexShaderPath;
    EXPECT_TRUE(initialDesc.FragmentShaderPath.ends_with(
        "shaders/post_tonemap.frag.spv"))
        << initialDesc.FragmentShaderPath;
    EXPECT_EQ(initialDesc.PrimitiveTopology, Extrinsic::RHI::Topology::TriangleList);
    EXPECT_EQ(initialDesc.Rasterizer.Culling, Extrinsic::RHI::CullMode::None);
    EXPECT_FALSE(initialDesc.DepthStencil.DepthTestEnable);
    EXPECT_FALSE(initialDesc.DepthStencil.DepthWriteEnable);
    EXPECT_FALSE(initialDesc.ColorBlend[0].Enable);
    EXPECT_EQ(initialDesc.ColorTargetCount, 1u);
    // The recipe's `SceneColorLDR` is allocated with
    // `FrameRecipeSizing::BackbufferFormat`; MockDevice keeps the default
    // `RHI::Format::RGBA8_UNORM`.
    EXPECT_EQ(initialDesc.ColorTargetFormats[0], Extrinsic::RHI::Format::RGBA8_UNORM);
    // No depth attachment is required by `"PostProcessPass"`, so the
    // tonemap pipeline declares `DepthTargetFormat::Undefined` (unlike the
    // SelectionOutline pipeline, which stays render-pass-compatible with
    // the recipe-read SceneDepth attachment).
    EXPECT_EQ(initialDesc.DepthTargetFormat, Extrinsic::RHI::Format::Undefined);
    // Matches the pass-local `PostProcessToneMapPushConstants` block
    // exported by `Pass.PostProcess.ToneMap` (80 bytes — `Exposure +
    // Operator + BloomIntensity + ColorGradingOn` + 4 grading scalars +
    // three `vec3 + float pad` rows under std430). The block mirrors the
    // shader's `layout(push_constant) Push { ... }` declaration byte-for-
    // byte; the canonical 20-byte `PostProcessPushConstants` block
    // shared by the other postprocess stages is intentionally not used
    // for tonemap since it aliases `HistogramBinCount` /
    // `StageKind` onto `ColorGradingOn` / `Saturation` and leaves the
    // grading tail unwritten.
    EXPECT_EQ(initialDesc.PushConstantSize, sizeof(Extrinsic::Graphics::PostProcessToneMapPushConstants));

    EXPECT_TRUE(renderer->RebuildOperationalResources(device));
    const Extrinsic::RHI::PipelineHandle rebuiltPipeline = renderer->GetPostProcessToneMapPipeline();
    EXPECT_TRUE(rebuiltPipeline.IsValid());
    const Extrinsic::RHI::PipelineDesc rebuiltDesc = renderer->GetPostProcessToneMapPipelineDesc();
    EXPECT_TRUE(PipelineDescBytesEqual(initialDesc, rebuiltDesc));

    renderer->Shutdown();
}

// ---------------------------------------------------------------------------
// GRAPHICS-075 Slice B.1 — default-recipe postprocess bloom downsample +
// upsample pipeline leases + republish. Mirrors the tonemap rebuild test
// above for the two fullscreen bloom shader pairs
// (`post_fullscreen.vert` + `post_bloom_downsample.frag` /
//  `post_fullscreen.vert` + `post_bloom_upsample.frag`).
// ---------------------------------------------------------------------------

TEST(RendererFrameLifecycle, PostProcessBloomPipelinesSurviveOperationalRebuild)
{
    Extrinsic::Tests::MockDevice device;
    device.Operational = true;
    device.BackbufferHandle = Extrinsic::RHI::TextureHandle{295u, 1u};

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer = Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);

    const Extrinsic::RHI::PipelineHandle initialDownsamplePipeline =
        renderer->GetPostProcessBloomDownsamplePipeline();
    EXPECT_TRUE(initialDownsamplePipeline.IsValid());
    const Extrinsic::RHI::PipelineHandle initialUpsamplePipeline =
        renderer->GetPostProcessBloomUpsamplePipeline();
    EXPECT_TRUE(initialUpsamplePipeline.IsValid());

    const Extrinsic::RHI::PipelineDesc initialDownsampleDesc =
        renderer->GetPostProcessBloomDownsamplePipelineDesc();
    EXPECT_TRUE(initialDownsampleDesc.VertexShaderPath.ends_with(
        "shaders/post_fullscreen.vert.spv"))
        << initialDownsampleDesc.VertexShaderPath;
    EXPECT_TRUE(initialDownsampleDesc.FragmentShaderPath.ends_with(
        "shaders/post_bloom_downsample.frag.spv"))
        << initialDownsampleDesc.FragmentShaderPath;
    EXPECT_EQ(initialDownsampleDesc.PrimitiveTopology, Extrinsic::RHI::Topology::TriangleList);
    EXPECT_EQ(initialDownsampleDesc.Rasterizer.Culling, Extrinsic::RHI::CullMode::None);
    EXPECT_FALSE(initialDownsampleDesc.DepthStencil.DepthTestEnable);
    EXPECT_FALSE(initialDownsampleDesc.DepthStencil.DepthWriteEnable);
    EXPECT_FALSE(initialDownsampleDesc.ColorBlend[0].Enable);
    EXPECT_EQ(initialDownsampleDesc.ColorTargetCount, 1u);
    // BloomScratch is declared as `RGBA16_FLOAT` in `BuildDefaultFrameRecipe`,
    // so both bloom pipelines target that format (independent of the
    // backbuffer format the tonemap pipeline picks up).
    EXPECT_EQ(initialDownsampleDesc.ColorTargetFormats[0], Extrinsic::RHI::Format::RGBA16_FLOAT);
    EXPECT_EQ(initialDownsampleDesc.DepthTargetFormat, Extrinsic::RHI::Format::Undefined);
    // 16-byte std430 block: `vec2 InvSrcResolution + float Threshold +
    // int IsFirstMip`. The canonical 20-byte block is intentionally not
    // used here per the standing shader-push-constant compatibility
    // policy.
    EXPECT_EQ(initialDownsampleDesc.PushConstantSize,
              sizeof(Extrinsic::Graphics::PostProcessBloomDownsamplePushConstants));

    const Extrinsic::RHI::PipelineDesc initialUpsampleDesc =
        renderer->GetPostProcessBloomUpsamplePipelineDesc();
    EXPECT_TRUE(initialUpsampleDesc.VertexShaderPath.ends_with(
        "shaders/post_fullscreen.vert.spv"))
        << initialUpsampleDesc.VertexShaderPath;
    EXPECT_TRUE(initialUpsampleDesc.FragmentShaderPath.ends_with(
        "shaders/post_bloom_upsample.frag.spv"))
        << initialUpsampleDesc.FragmentShaderPath;
    EXPECT_EQ(initialUpsampleDesc.PrimitiveTopology, Extrinsic::RHI::Topology::TriangleList);
    EXPECT_EQ(initialUpsampleDesc.Rasterizer.Culling, Extrinsic::RHI::CullMode::None);
    EXPECT_FALSE(initialUpsampleDesc.DepthStencil.DepthTestEnable);
    EXPECT_FALSE(initialUpsampleDesc.DepthStencil.DepthWriteEnable);
    EXPECT_FALSE(initialUpsampleDesc.ColorBlend[0].Enable);
    EXPECT_EQ(initialUpsampleDesc.ColorTargetCount, 1u);
    EXPECT_EQ(initialUpsampleDesc.ColorTargetFormats[0], Extrinsic::RHI::Format::RGBA16_FLOAT);
    EXPECT_EQ(initialUpsampleDesc.DepthTargetFormat, Extrinsic::RHI::Format::Undefined);
    // 16-byte std430 block: `vec2 InvCoarserResolution + float FilterRadius +
    // float _pad0`. Slice B.2 keeps this layout and feeds it per upsample
    // step.
    EXPECT_EQ(initialUpsampleDesc.PushConstantSize,
              sizeof(Extrinsic::Graphics::PostProcessBloomUpsamplePushConstants));

    EXPECT_TRUE(renderer->RebuildOperationalResources(device));
    const Extrinsic::RHI::PipelineHandle rebuiltDownsamplePipeline =
        renderer->GetPostProcessBloomDownsamplePipeline();
    EXPECT_TRUE(rebuiltDownsamplePipeline.IsValid());
    const Extrinsic::RHI::PipelineHandle rebuiltUpsamplePipeline =
        renderer->GetPostProcessBloomUpsamplePipeline();
    EXPECT_TRUE(rebuiltUpsamplePipeline.IsValid());
    const Extrinsic::RHI::PipelineDesc rebuiltDownsampleDesc =
        renderer->GetPostProcessBloomDownsamplePipelineDesc();
    const Extrinsic::RHI::PipelineDesc rebuiltUpsampleDesc =
        renderer->GetPostProcessBloomUpsamplePipelineDesc();
    EXPECT_TRUE(PipelineDescBytesEqual(initialDownsampleDesc, rebuiltDownsampleDesc));
    EXPECT_TRUE(PipelineDescBytesEqual(initialUpsampleDesc, rebuiltUpsampleDesc));

    renderer->Shutdown();
}

// ---------------------------------------------------------------------------
// GRAPHICS-075 Slice C — default-recipe postprocess FXAA pipeline lease +
// republish. Mirrors the tonemap rebuild test above for the fullscreen
// `post_fullscreen.vert` + `post_fxaa.frag` shader pair.
// ---------------------------------------------------------------------------

TEST(RendererFrameLifecycle, PostProcessFXAAPipelineSurvivesOperationalRebuild)
{
    Extrinsic::Tests::MockDevice device;
    device.Operational = true;
    device.BackbufferHandle = Extrinsic::RHI::TextureHandle{297u, 1u};

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer = Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);

    const Extrinsic::RHI::PipelineHandle initialPipeline = renderer->GetPostProcessFXAAPipeline();
    EXPECT_TRUE(initialPipeline.IsValid());

    const Extrinsic::RHI::PipelineDesc initialDesc = renderer->GetPostProcessFXAAPipelineDesc();
    EXPECT_TRUE(initialDesc.VertexShaderPath.ends_with(
        "shaders/post_fullscreen.vert.spv"))
        << initialDesc.VertexShaderPath;
    EXPECT_TRUE(initialDesc.FragmentShaderPath.ends_with(
        "shaders/post_fxaa.frag.spv"))
        << initialDesc.FragmentShaderPath;
    EXPECT_EQ(initialDesc.PrimitiveTopology, Extrinsic::RHI::Topology::TriangleList);
    EXPECT_EQ(initialDesc.Rasterizer.Culling, Extrinsic::RHI::CullMode::None);
    EXPECT_FALSE(initialDesc.DepthStencil.DepthTestEnable);
    EXPECT_FALSE(initialDesc.DepthStencil.DepthWriteEnable);
    EXPECT_FALSE(initialDesc.ColorBlend[0].Enable);
    EXPECT_EQ(initialDesc.ColorTargetCount, 1u);
    // FXAA writes into the recipe's `SceneColorLDR` target, which is
    // allocated with `FrameRecipeSizing::BackbufferFormat`; MockDevice
    // keeps the default `RHI::Format::RGBA8_UNORM`.
    EXPECT_EQ(initialDesc.ColorTargetFormats[0], Extrinsic::RHI::Format::RGBA8_UNORM);
    EXPECT_EQ(initialDesc.DepthTargetFormat, Extrinsic::RHI::Format::Undefined);
    // 20-byte std430 block: `vec2 InvResolution + float ContrastThreshold +
    // float RelativeThreshold + float SubpixelBlending`. The canonical
    // 20-byte `PostProcessPushConstants` block happens to share the same
    // byte size but a completely different field layout (Exposure/Gamma/
    // BloomIntensity/HistogramBinCount/StageKind), so this assertion uses
    // the typed `sizeof(PostProcessFXAAPushConstants)` rather than a raw
    // 20u literal to keep the contract anchored to the FXAA-shaped struct.
    EXPECT_EQ(initialDesc.PushConstantSize,
              sizeof(Extrinsic::Graphics::PostProcessFXAAPushConstants));

    EXPECT_TRUE(renderer->RebuildOperationalResources(device));
    const Extrinsic::RHI::PipelineHandle rebuiltPipeline = renderer->GetPostProcessFXAAPipeline();
    EXPECT_TRUE(rebuiltPipeline.IsValid());
    const Extrinsic::RHI::PipelineDesc rebuiltDesc = renderer->GetPostProcessFXAAPipelineDesc();
    EXPECT_TRUE(PipelineDescBytesEqual(initialDesc, rebuiltDesc));

    renderer->Shutdown();
}

// ---------------------------------------------------------------------------
// GRAPHICS-075 Slice D.2a — default-recipe postprocess SMAA pipelines lease
// + republish. Three pipelines (edge / blend / resolve) are created across
// the per-stage AA graph passes (`"PostProcessAA{Edge,Blend,Resolve}Pass"`).
// The recipe's `PostProcess.AATemp.{Edges,Weights,Resolved}` split pins
// edge to `RG8_UNORM`, blend to `RGBA8_UNORM`, and resolve to the
// backbuffer format. MockDevice keeps the default
// `RHI::Format::RGBA8_UNORM` for the backbuffer, so the resolve pipeline
// reports that format while edge / blend report their own fixed formats.
// Each push block is 16 bytes and mirrors its shader's std430 layout
// byte-for-byte. Each descriptor must survive
// `RebuildOperationalResources()` byte-identical.
// ---------------------------------------------------------------------------

TEST(RendererFrameLifecycle, PostProcessSMAAPipelinesSurviveOperationalRebuild)
{
    Extrinsic::Tests::MockDevice device;
    device.Operational = true;
    device.BackbufferHandle = Extrinsic::RHI::TextureHandle{298u, 1u};

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer = Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);

    const Extrinsic::RHI::PipelineHandle initialEdgePipeline =
        renderer->GetPostProcessSMAAEdgePipeline();
    EXPECT_TRUE(initialEdgePipeline.IsValid());
    const Extrinsic::RHI::PipelineHandle initialBlendPipeline =
        renderer->GetPostProcessSMAABlendPipeline();
    EXPECT_TRUE(initialBlendPipeline.IsValid());
    const Extrinsic::RHI::PipelineHandle initialResolvePipeline =
        renderer->GetPostProcessSMAAResolvePipeline();
    EXPECT_TRUE(initialResolvePipeline.IsValid());

    const Extrinsic::RHI::PipelineDesc initialEdgeDesc =
        renderer->GetPostProcessSMAAEdgePipelineDesc();
    EXPECT_TRUE(initialEdgeDesc.VertexShaderPath.ends_with(
        "shaders/post_fullscreen.vert.spv"))
        << initialEdgeDesc.VertexShaderPath;
    EXPECT_TRUE(initialEdgeDesc.FragmentShaderPath.ends_with(
        "shaders/post_smaa_edge.frag.spv"))
        << initialEdgeDesc.FragmentShaderPath;
    EXPECT_EQ(initialEdgeDesc.PrimitiveTopology, Extrinsic::RHI::Topology::TriangleList);
    EXPECT_EQ(initialEdgeDesc.Rasterizer.Culling, Extrinsic::RHI::CullMode::None);
    EXPECT_FALSE(initialEdgeDesc.DepthStencil.DepthTestEnable);
    EXPECT_FALSE(initialEdgeDesc.DepthStencil.DepthWriteEnable);
    EXPECT_FALSE(initialEdgeDesc.ColorBlend[0].Enable);
    EXPECT_EQ(initialEdgeDesc.ColorTargetCount, 1u);
    // Slice D.2a: edge pipeline is fixed at `RG8_UNORM` to match the
    // recipe's `PostProcess.AATemp.Edges` transient (the SMAA edge
    // shader writes `vec2 edges`).
    EXPECT_EQ(initialEdgeDesc.ColorTargetFormats[0], Extrinsic::RHI::Format::RG8_UNORM);
    EXPECT_EQ(initialEdgeDesc.DepthTargetFormat, Extrinsic::RHI::Format::Undefined);
    // 16-byte std430 block: `vec2 InvResolution + float EdgeThreshold + float _pad0`.
    EXPECT_EQ(initialEdgeDesc.PushConstantSize,
              sizeof(Extrinsic::Graphics::PostProcessSMAAEdgePushConstants));

    const Extrinsic::RHI::PipelineDesc initialBlendDesc =
        renderer->GetPostProcessSMAABlendPipelineDesc();
    EXPECT_TRUE(initialBlendDesc.VertexShaderPath.ends_with(
        "shaders/post_fullscreen.vert.spv"))
        << initialBlendDesc.VertexShaderPath;
    EXPECT_TRUE(initialBlendDesc.FragmentShaderPath.ends_with(
        "shaders/post_smaa_blend.frag.spv"))
        << initialBlendDesc.FragmentShaderPath;
    EXPECT_EQ(initialBlendDesc.PrimitiveTopology, Extrinsic::RHI::Topology::TriangleList);
    EXPECT_FALSE(initialBlendDesc.DepthStencil.DepthTestEnable);
    EXPECT_FALSE(initialBlendDesc.ColorBlend[0].Enable);
    EXPECT_EQ(initialBlendDesc.ColorTargetCount, 1u);
    // Slice D.2a: blend pipeline is fixed at `RGBA8_UNORM` to match the
    // recipe's `PostProcess.AATemp.Weights` transient.
    EXPECT_EQ(initialBlendDesc.ColorTargetFormats[0], Extrinsic::RHI::Format::RGBA8_UNORM);
    EXPECT_EQ(initialBlendDesc.DepthTargetFormat, Extrinsic::RHI::Format::Undefined);
    // 16-byte std430 block: `vec2 InvResolution + int MaxSearchSteps + int MaxSearchStepsDiag`.
    EXPECT_EQ(initialBlendDesc.PushConstantSize,
              sizeof(Extrinsic::Graphics::PostProcessSMAABlendPushConstants));

    const Extrinsic::RHI::PipelineDesc initialResolveDesc =
        renderer->GetPostProcessSMAAResolvePipelineDesc();
    EXPECT_TRUE(initialResolveDesc.VertexShaderPath.ends_with(
        "shaders/post_fullscreen.vert.spv"))
        << initialResolveDesc.VertexShaderPath;
    EXPECT_TRUE(initialResolveDesc.FragmentShaderPath.ends_with(
        "shaders/post_smaa_resolve.frag.spv"))
        << initialResolveDesc.FragmentShaderPath;
    EXPECT_EQ(initialResolveDesc.PrimitiveTopology, Extrinsic::RHI::Topology::TriangleList);
    EXPECT_FALSE(initialResolveDesc.DepthStencil.DepthTestEnable);
    EXPECT_FALSE(initialResolveDesc.ColorBlend[0].Enable);
    EXPECT_EQ(initialResolveDesc.ColorTargetCount, 1u);
    // Slice D.2a: resolve writes `PostProcess.AATemp.Resolved`, which
    // the recipe allocates with `FrameRecipeSizing::BackbufferFormat`;
    // MockDevice keeps the default `RHI::Format::RGBA8_UNORM`.
    EXPECT_EQ(initialResolveDesc.ColorTargetFormats[0], Extrinsic::RHI::Format::RGBA8_UNORM);
    EXPECT_EQ(initialResolveDesc.DepthTargetFormat, Extrinsic::RHI::Format::Undefined);
    // 16-byte std430 block: `vec2 InvResolution + float _pad0 + float _pad1`.
    EXPECT_EQ(initialResolveDesc.PushConstantSize,
              sizeof(Extrinsic::Graphics::PostProcessSMAAResolvePushConstants));

    EXPECT_TRUE(renderer->RebuildOperationalResources(device));
    EXPECT_TRUE(renderer->GetPostProcessSMAAEdgePipeline().IsValid());
    EXPECT_TRUE(renderer->GetPostProcessSMAABlendPipeline().IsValid());
    EXPECT_TRUE(renderer->GetPostProcessSMAAResolvePipeline().IsValid());
    const Extrinsic::RHI::PipelineDesc rebuiltEdgeDesc =
        renderer->GetPostProcessSMAAEdgePipelineDesc();
    const Extrinsic::RHI::PipelineDesc rebuiltBlendDesc =
        renderer->GetPostProcessSMAABlendPipelineDesc();
    const Extrinsic::RHI::PipelineDesc rebuiltResolveDesc =
        renderer->GetPostProcessSMAAResolvePipelineDesc();
    EXPECT_TRUE(PipelineDescBytesEqual(initialEdgeDesc, rebuiltEdgeDesc));
    EXPECT_TRUE(PipelineDescBytesEqual(initialBlendDesc, rebuiltBlendDesc));
    EXPECT_TRUE(PipelineDescBytesEqual(initialResolveDesc, rebuiltResolveDesc));

    renderer->Shutdown();
}

// ---------------------------------------------------------------------------
// GRAPHICS-075 Slice E.1 — default-recipe postprocess histogram compute
// pipeline lease + republish. Mirrors the SMAA rebuild test above for the
// `post_histogram.comp` shader: the pipeline is a *compute* pipeline (no
// vertex / fragment stages), targets no color attachment, and carries the
// 16-byte `PostProcessHistogramPushConstants` block byte-for-byte
// matching the shader's std430 push declaration. The pipeline lives in
// its own ordered graph pass (`"PostProcessHistogramPass"`) because
// Vulkan rejects `vkCmdDispatch` inside an active render-pass scope —
// the survive-rebuild contract pins both the descriptor shape and the
// byte-identical rebuild behavior.
// ---------------------------------------------------------------------------

TEST(RendererFrameLifecycle, PostProcessHistogramPipelineSurvivesOperationalRebuild)
{
    Extrinsic::Tests::MockDevice device;
    device.Operational = true;
    device.BackbufferHandle = Extrinsic::RHI::TextureHandle{421u, 1u};

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer = Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);

    const Extrinsic::RHI::PipelineHandle initialPipeline = renderer->GetPostProcessHistogramPipeline();
    EXPECT_TRUE(initialPipeline.IsValid());

    const Extrinsic::RHI::PipelineDesc initialDesc = renderer->GetPostProcessHistogramPipelineDesc();
    EXPECT_TRUE(initialDesc.ComputeShaderPath.ends_with(
        "shaders/post_histogram.comp.spv"))
        << initialDesc.ComputeShaderPath;
    EXPECT_TRUE(initialDesc.VertexShaderPath.empty())
        << "Compute pipeline must leave VertexShaderPath empty so the "
           "backend interprets the descriptor as compute.";
    EXPECT_TRUE(initialDesc.FragmentShaderPath.empty())
        << "Compute pipeline must leave FragmentShaderPath empty so the "
           "backend interprets the descriptor as compute.";
    EXPECT_EQ(initialDesc.ColorTargetCount, 0u);
    EXPECT_EQ(initialDesc.DepthTargetFormat, Extrinsic::RHI::Format::Undefined);
    // 16-byte std430 block: `uint Width + uint Height + float MinLogLum +
    // float RangeLogLum`. The canonical 20-byte block is intentionally
    // not used here per the standing shader-push-constant compatibility
    // policy (it would alias `Exposure` onto `Width` as
    // `bit_cast<uint>(1.0f)` ≈ 1.07e9, producing a degenerate dispatch
    // shape).
    EXPECT_EQ(initialDesc.PushConstantSize,
              sizeof(Extrinsic::Graphics::PostProcessHistogramPushConstants));

    EXPECT_TRUE(renderer->RebuildOperationalResources(device));
    const Extrinsic::RHI::PipelineHandle rebuiltPipeline = renderer->GetPostProcessHistogramPipeline();
    EXPECT_TRUE(rebuiltPipeline.IsValid());
    const Extrinsic::RHI::PipelineDesc rebuiltDesc = renderer->GetPostProcessHistogramPipelineDesc();
    EXPECT_TRUE(PipelineDescBytesEqual(initialDesc, rebuiltDesc));

    renderer->Shutdown();
}

// ---------------------------------------------------------------------------
// GRAPHICS-038B — default-recipe HZB build compute pipeline + command shape.
// ---------------------------------------------------------------------------

TEST(RendererFrameLifecycle, HZBBuildPipelineSurvivesOperationalRebuild)
{
    Extrinsic::Tests::MockDevice device;
    device.Operational = true;
    device.BackbufferHandle = Extrinsic::RHI::TextureHandle{431u, 1u};

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer = Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);

    const Extrinsic::RHI::PipelineHandle initialPipeline = renderer->GetHZBBuildPipeline();
    EXPECT_TRUE(initialPipeline.IsValid());

    const Extrinsic::RHI::PipelineDesc initialDesc = renderer->GetHZBBuildPipelineDesc();
    EXPECT_TRUE(initialDesc.ComputeShaderPath.ends_with(
        "shaders/hzb_build.comp.spv"))
        << initialDesc.ComputeShaderPath;
    EXPECT_TRUE(initialDesc.VertexShaderPath.empty())
        << "HZB build is a compute pipeline; VertexShaderPath must stay empty.";
    EXPECT_TRUE(initialDesc.FragmentShaderPath.empty())
        << "HZB build is a compute pipeline; FragmentShaderPath must stay empty.";
    EXPECT_EQ(initialDesc.ColorTargetCount, 0u);
    EXPECT_EQ(initialDesc.DepthTargetFormat, Extrinsic::RHI::Format::Undefined);
    static_assert(sizeof(Extrinsic::Graphics::HZBBuildPushConstants) == 32u);
    EXPECT_EQ(initialDesc.PushConstantSize,
              sizeof(Extrinsic::Graphics::HZBBuildPushConstants));

    EXPECT_TRUE(renderer->RebuildOperationalResources(device));
    const Extrinsic::RHI::PipelineHandle rebuiltPipeline = renderer->GetHZBBuildPipeline();
    EXPECT_TRUE(rebuiltPipeline.IsValid());
    const Extrinsic::RHI::PipelineDesc rebuiltDesc = renderer->GetHZBBuildPipelineDesc();
    EXPECT_TRUE(PipelineDescBytesEqual(initialDesc, rebuiltDesc));

    renderer->Shutdown();
}

TEST(RendererFrameLifecycle, ClusterLightingPipelinesAndSceneTablePublishSurviveRebuild)
{
    Extrinsic::Tests::MockDevice device;
    device.Operational = true;
    device.BackbufferHandle = Extrinsic::RHI::TextureHandle{433u, 1u};

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer = Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);

    const Extrinsic::RHI::PipelineHandle initialGridPipeline =
        renderer->GetClusterGridBuildPipeline();
    const Extrinsic::RHI::PipelineHandle initialAssignmentPipeline =
        renderer->GetClusterLightAssignmentPipeline();
    EXPECT_TRUE(initialGridPipeline.IsValid());
    EXPECT_TRUE(initialAssignmentPipeline.IsValid());

    const Extrinsic::RHI::PipelineDesc initialGridDesc =
        renderer->GetClusterGridBuildPipelineDesc();
    EXPECT_TRUE(initialGridDesc.ComputeShaderPath.ends_with(
        "shaders/cluster_grid_build.comp.spv"))
        << initialGridDesc.ComputeShaderPath;
    EXPECT_TRUE(initialGridDesc.VertexShaderPath.empty());
    EXPECT_TRUE(initialGridDesc.FragmentShaderPath.empty());
    EXPECT_EQ(initialGridDesc.ColorTargetCount, 0u);
    EXPECT_EQ(initialGridDesc.DepthTargetFormat, Extrinsic::RHI::Format::Undefined);
    EXPECT_EQ(initialGridDesc.PushConstantSize,
              sizeof(Extrinsic::Graphics::ClusterGridBuildPushConstants));

    const Extrinsic::RHI::PipelineDesc initialAssignmentDesc =
        renderer->GetClusterLightAssignmentPipelineDesc();
    EXPECT_TRUE(initialAssignmentDesc.ComputeShaderPath.ends_with(
        "shaders/light_cluster_assign.comp.spv"))
        << initialAssignmentDesc.ComputeShaderPath;
    EXPECT_TRUE(initialAssignmentDesc.VertexShaderPath.empty());
    EXPECT_TRUE(initialAssignmentDesc.FragmentShaderPath.empty());
    EXPECT_EQ(initialAssignmentDesc.ColorTargetCount, 0u);
    EXPECT_EQ(initialAssignmentDesc.DepthTargetFormat, Extrinsic::RHI::Format::Undefined);
    EXPECT_EQ(initialAssignmentDesc.PushConstantSize,
              sizeof(Extrinsic::Graphics::ClusterLightAssignmentPushConstants));

    Extrinsic::RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));
    const Extrinsic::Graphics::RenderFrameInput input{
        .Viewport = {.Width = 160, .Height = 96},
    };
    Extrinsic::Graphics::RenderWorld world = renderer->ExtractRenderWorld(input);
    renderer->PrepareFrame(world);

    const Extrinsic::RHI::GpuSceneTable* sceneTable =
        FindLastSceneTableWrite(device, renderer->GetGpuWorld().GetSceneTableBuffer());
    ASSERT_NE(sceneTable, nullptr);
    const Extrinsic::Graphics::ClusterGridDesc expectedDesc =
        Extrinsic::Graphics::ComputeClusterGridDesc(160u, 96u);
    ASSERT_TRUE(expectedDesc.IsValid());
    EXPECT_NE(sceneTable->ClusterLightHeaderBDA, 0u);
    EXPECT_NE(sceneTable->ClusterLightIndexBDA, 0u);
    EXPECT_EQ(sceneTable->ClusterTilePx, expectedDesc.ClusterTilePx);
    EXPECT_EQ(sceneTable->ClusterTilesX, expectedDesc.TilesX);
    EXPECT_EQ(sceneTable->ClusterTilesY, expectedDesc.TilesY);
    EXPECT_EQ(sceneTable->ClusterSlicesZ, expectedDesc.SlicesZ);
    EXPECT_EQ(sceneTable->ClusterCellCount, expectedDesc.CellCount);
    EXPECT_EQ(sceneTable->ClusterMaxLightsPerCell,
              Extrinsic::Graphics::kMaxClusterLightsPerCell);
    EXPECT_GT(sceneTable->ClusterNearZ, 0.0f);
    EXPECT_GT(sceneTable->ClusterFarZ, sceneTable->ClusterNearZ);

    EXPECT_TRUE(renderer->RebuildOperationalResources(device));
    EXPECT_TRUE(renderer->GetClusterGridBuildPipeline().IsValid());
    EXPECT_TRUE(renderer->GetClusterLightAssignmentPipeline().IsValid());
    EXPECT_TRUE(PipelineDescBytesEqual(initialGridDesc,
                                      renderer->GetClusterGridBuildPipelineDesc()));
    EXPECT_TRUE(PipelineDescBytesEqual(initialAssignmentDesc,
                                      renderer->GetClusterLightAssignmentPipelineDesc()));

    renderer->Shutdown();
}

TEST(RendererFrameLifecycle, HZBBuildPassRecordsFallbackDispatches)
{
    Extrinsic::Tests::MockDevice device;
    device.Operational = true;
    device.BackbufferHandle = Extrinsic::RHI::TextureHandle{432u, 1u};

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer = Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);

    Extrinsic::RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));

    const Extrinsic::Graphics::RenderFrameInput input{
        .Viewport = {.Width = 64, .Height = 32},
    };
    const Extrinsic::Graphics::HZBBuildDispatchPlan hzbPlan =
        ExpectedFallbackHZBPlan(64u, 32u);
    ASSERT_TRUE(hzbPlan.IsValid());
    const Extrinsic::Graphics::ClusterGridBuildDispatchPlan clusterPlan =
        ExpectedClusterGridPlan(64u, 32u);
    ASSERT_TRUE(clusterPlan.IsValid());

    Extrinsic::Graphics::RenderWorld world = renderer->ExtractRenderWorld(input);
    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(frame, world);

    const Extrinsic::Graphics::RenderGraphFrameStats& stats = renderer->GetLastRenderGraphStats();
    ASSERT_TRUE(stats.Compile.Succeeded) << stats.Diagnostic;
    ASSERT_TRUE(stats.Execute.Succeeded) << stats.Diagnostic;
    ASSERT_NE(FindCommandPass(stats, "HZBBuildPass"), nullptr);
    EXPECT_EQ(FindCommandPass(stats, "HZBBuildPass")->Status,
              Extrinsic::Graphics::RenderCommandPassStatus::Recorded);

    EXPECT_TRUE(renderer->GetHZBSystem().IsAllocated());
    EXPECT_EQ(renderer->GetHZBSystem().GetAllocatedDesc(), hzbPlan.Desc);
    EXPECT_EQ(stats.HZBBuildRecordedFrames, 1u);
    EXPECT_EQ(stats.HZBBuildDispatchCount,
              static_cast<std::uint32_t>(hzbPlan.Dispatches.size()));
    EXPECT_EQ(stats.HZBBuildMipCount, hzbPlan.Desc.MipLevels);
    EXPECT_EQ(stats.HZBBuildFallbackFrames, 1u);
    EXPECT_EQ(stats.HZBBuildSinglePassFrames, 0u);

    ASSERT_EQ(device.CommandContext.DispatchRecords.size(),
              3u + hzbPlan.Dispatches.size());
    EXPECT_EQ(device.CommandContext.DispatchRecords[0].X, ExpectedCullDispatchGroups());
    EXPECT_EQ(device.CommandContext.DispatchRecords[0].Y, 1u);
    EXPECT_EQ(device.CommandContext.DispatchRecords[0].Z, 1u);
    std::vector<Extrinsic::Tests::MockCommandContext::DispatchRecord> expectedComputeDispatches{};
    expectedComputeDispatches.reserve(hzbPlan.Dispatches.size() + 2u);
    for (std::size_t i = 0u; i < hzbPlan.Dispatches.size(); ++i)
    {
        const auto& expected = hzbPlan.Dispatches[i];
        expectedComputeDispatches.push_back(Extrinsic::Tests::MockCommandContext::DispatchRecord{
            .X = expected.GroupCountX,
            .Y = expected.GroupCountY,
            .Z = expected.GroupCountZ,
        });
    }
    expectedComputeDispatches.push_back(Extrinsic::Tests::MockCommandContext::DispatchRecord{
        .X = clusterPlan.GroupCountX,
        .Y = clusterPlan.GroupCountY,
        .Z = clusterPlan.GroupCountZ,
    });
    expectedComputeDispatches.push_back(Extrinsic::Tests::MockCommandContext::DispatchRecord{
        .X = clusterPlan.GroupCountX,
        .Y = clusterPlan.GroupCountY,
        .Z = clusterPlan.GroupCountZ,
    });
    ExpectDispatchRecordsContainUnordered(device.CommandContext, expectedComputeDispatches, 1u);

    const Extrinsic::RHI::TextureHandle builtHZB = renderer->GetHZBSystem().PreviousHZB();
    ASSERT_TRUE(builtHZB.IsValid());
    std::uint32_t stitchBarrierCount = 0u;
    for (const Extrinsic::Tests::MockCommandContext::TextureBarrierRecord& barrier :
         device.CommandContext.TextureBarrierCalls)
    {
        if (barrier.Texture == builtHZB &&
            barrier.Before == Extrinsic::RHI::TextureLayout::General &&
            barrier.After == Extrinsic::RHI::TextureLayout::General &&
            barrier.BeforeAccess == Extrinsic::RHI::MemoryAccess::ShaderWrite &&
            barrier.AfterAccess == (Extrinsic::RHI::MemoryAccess::ShaderRead |
                                    Extrinsic::RHI::MemoryAccess::ShaderWrite))
        {
            ++stitchBarrierCount;
        }
    }
    EXPECT_EQ(stitchBarrierCount, hzbPlan.Dispatches.size() - 1u);

    renderer->Shutdown();
}

// ---------------------------------------------------------------------------
// GRAPHICS-075 Slice D.2b — retained SMAA `AreaTex` / `SearchTex` LUT
// textures + exposure-adaptation history buffer. `PostProcessSystem`'s
// device-aware Initialize() allocates the area/search LUTs (uploaded via
// the transfer queue) and the exposure-history buffer up-front, and the
// handles + dimensions must survive `RebuildOperationalResources()`
// byte-identical so the SMAA blend pass keeps sampling the same retained
// resources across recipe rebuilds. The test also pins the upload
// payload sizes (160 * 560 * 2 = 179200 bytes for the area texture,
// 66 * 33 = 2178 bytes for the search texture) and asserts no
// re-allocation or re-upload happens across the rebuild.
// ---------------------------------------------------------------------------

TEST(RendererFrameLifecycle, PostProcessSMAALookupTexturesSurviveOperationalRebuild)
{
    Extrinsic::Tests::MockDevice device;
    device.Operational = true;
    device.BackbufferHandle = Extrinsic::RHI::TextureHandle{411u, 1u};

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer = Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);

    const Extrinsic::RHI::TextureHandle initialAreaTex =
        renderer->GetPostProcessSystem().GetSMAAAreaTexture();
    const Extrinsic::RHI::TextureHandle initialSearchTex =
        renderer->GetPostProcessSystem().GetSMAASearchTexture();
    const Extrinsic::RHI::BufferHandle initialExposureBuffer =
        renderer->GetPostProcessSystem().GetExposureHistoryBuffer();

    EXPECT_TRUE(initialAreaTex.IsValid());
    EXPECT_TRUE(initialSearchTex.IsValid());
    EXPECT_TRUE(initialExposureBuffer.IsValid());

    constexpr std::uint64_t kExpectedAreaBytes =
        static_cast<std::uint64_t>(Extrinsic::Graphics::kPostProcessSMAAAreaTextureWidth) *
        Extrinsic::Graphics::kPostProcessSMAAAreaTextureHeight * 2u;
    constexpr std::uint64_t kExpectedSearchBytes =
        static_cast<std::uint64_t>(Extrinsic::Graphics::kPostProcessSMAASearchTextureWidth) *
        Extrinsic::Graphics::kPostProcessSMAASearchTextureHeight;

    const auto findUpload =
        [&device](Extrinsic::RHI::TextureHandle handle) -> const Extrinsic::Tests::MockTransferQueue::TextureUploadRecord* {
            for (const auto& upload : device.TransferQueue.TextureUploads)
            {
                if (upload.Texture == handle)
                {
                    return &upload;
                }
            }
            return nullptr;
        };

    const Extrinsic::Tests::MockTransferQueue::TextureUploadRecord* areaUpload =
        findUpload(initialAreaTex);
    const Extrinsic::Tests::MockTransferQueue::TextureUploadRecord* searchUpload =
        findUpload(initialSearchTex);
    ASSERT_NE(areaUpload, nullptr);
    ASSERT_NE(searchUpload, nullptr);
    EXPECT_EQ(areaUpload->SizeBytes, kExpectedAreaBytes);
    EXPECT_EQ(searchUpload->SizeBytes, kExpectedSearchBytes);
    EXPECT_EQ(areaUpload->MipLevel, 0u);
    EXPECT_EQ(areaUpload->ArrayLayer, 0u);
    EXPECT_EQ(searchUpload->MipLevel, 0u);
    EXPECT_EQ(searchUpload->ArrayLayer, 0u);

    const std::size_t uploadsAfterInit = device.TransferQueue.TextureUploads.size();

    EXPECT_TRUE(renderer->RebuildOperationalResources(device));

    // Byte-identical survival: the retained SMAA LUT + exposure-history
    // leases use the same underlying device handles after the rebuild,
    // and no extra transfer-queue upload was issued (the analytical LUT
    // bytes are uploaded once at first device-aware Initialize() time).
    EXPECT_EQ(renderer->GetPostProcessSystem().GetSMAAAreaTexture(), initialAreaTex);
    EXPECT_EQ(renderer->GetPostProcessSystem().GetSMAASearchTexture(), initialSearchTex);
    EXPECT_EQ(renderer->GetPostProcessSystem().GetExposureHistoryBuffer(), initialExposureBuffer);
    EXPECT_EQ(device.TransferQueue.TextureUploads.size(), uploadsAfterInit);

    // Snapshot the device's destroy counters *after* the rebuild — the
    // rebuild legitimately recreates other systems' GPU resources
    // (GpuWorld / MaterialSystem buffers, etc.), but the SMAA LUT +
    // exposure-history leases must outlive it. Shutdown() must then
    // release those leases via TextureManager / BufferManager, which
    // calls back into IDevice::Destroy{Texture,Buffer}.
    const int destroyTexturesBeforeShutdown = device.DestroyTextureCount;
    const int destroyBuffersBeforeShutdown  = device.DestroyBufferCount;
    renderer->Shutdown();
    EXPECT_GE(device.DestroyTextureCount, destroyTexturesBeforeShutdown + 2)
        << "Shutdown should release the SMAA AreaTex and SearchTex leases.";
    EXPECT_GE(device.DestroyBufferCount, destroyBuffersBeforeShutdown + 1)
        << "Shutdown should release the exposure-adaptation history buffer lease.";
}

// ---------------------------------------------------------------------------
// GRAPHICS-075 Slice D.2b — failed-upload retry. UploadTexture(...) can
// return an invalid token (Value == 0) when the backend rejects the
// upload (e.g. staging allocation failure). The retained-resource
// allocator must drop the freshly-created lease in that case so the
// idempotence check sees an invalid handle and a follow-up Initialize
// (which the renderer invokes from RebuildOperationalResources) retries
// the allocate+upload instead of leaving SMAA sampling uninitialized
// texture content indefinitely.
// ---------------------------------------------------------------------------

TEST(RendererFrameLifecycle, PostProcessSMAALookupTexturesRetryAfterFailedUpload)
{
    Extrinsic::Tests::MockDevice device;
    device.Operational = true;
    device.BackbufferHandle = Extrinsic::RHI::TextureHandle{412u, 1u};
    device.TransferQueue.FailTextureUploads = true;

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer = Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);

    // Initial Initialize() ran while every UploadTexture returned an
    // invalid token; both LUT leases must have been rolled back so the
    // public handles report invalid. The exposure-history buffer does
    // not flow through the transfer queue, so its lease can be valid.
    EXPECT_FALSE(renderer->GetPostProcessSystem().GetSMAAAreaTexture().IsValid());
    EXPECT_FALSE(renderer->GetPostProcessSystem().GetSMAASearchTexture().IsValid());

    // Clear the failure knob and trigger a re-init via
    // RebuildOperationalResources(...). The retained-resource allocator
    // is idempotent on already-valid leases but must retry the failed
    // ones; the LUT handles should land valid this time.
    device.TransferQueue.FailTextureUploads = false;
    EXPECT_TRUE(renderer->RebuildOperationalResources(device));

    const Extrinsic::RHI::TextureHandle retriedAreaTex =
        renderer->GetPostProcessSystem().GetSMAAAreaTexture();
    const Extrinsic::RHI::TextureHandle retriedSearchTex =
        renderer->GetPostProcessSystem().GetSMAASearchTexture();
    EXPECT_TRUE(retriedAreaTex.IsValid());
    EXPECT_TRUE(retriedSearchTex.IsValid());

    // The transfer queue records every UploadTexture call (including the
    // rejected first attempt), so it must hold at least the two retried
    // upload records keyed to the now-valid handles.
    const auto findUpload =
        [&device](Extrinsic::RHI::TextureHandle handle) -> const Extrinsic::Tests::MockTransferQueue::TextureUploadRecord* {
            for (const auto& upload : device.TransferQueue.TextureUploads)
            {
                if (upload.Texture == handle)
                {
                    return &upload;
                }
            }
            return nullptr;
        };
    EXPECT_NE(findUpload(retriedAreaTex), nullptr);
    EXPECT_NE(findUpload(retriedSearchTex), nullptr);

    renderer->Shutdown();
}

// ---------------------------------------------------------------------------
// GRAPHICS-075 Slice D.2a — AA-mode-aware resolve gate. The recipe-build
// site flips `FrameRecipeFeatures::EnableAntiAliasing` (and thus
// `presentSource = PostProcess.AATemp.Resolved`) only when the selected
// AA mode's pipeline(s) are actually available, and
// `RecordPostProcessAAResolvePass` mirrors the same gate. Otherwise a
// user-selected AA mode whose matching pipeline failed to build would
// route present to the unwritten `AATemp.Resolved` attachment while
// the pass body short-circuited to a no-op — the user would see a
// cleared / undefined frame instead of the tonemapped `SceneColorLDR`.
// These regression tests pin the gate so future scope creep can't
// loosen it back to "either AA pipeline is good enough".
//
// `FailPipelineCreateCall` is a 1-indexed counter of `IDevice` pipeline-
// create calls. Pipeline creation order inside
// `InitializeOperationalPassResources` is:
//   1 culling, 2 depth, 3 defaultDebugSurface, 4 forwardSurface,
//   5 forwardLine, 6 forwardPoint, 7 shadow, 8 deferredGBuffer,
//   9 deferredLighting, 10-13 selectionId, 14 selectionOutline,
//   15 tonemap, 16 bloomDownsample, 17 bloomUpsample, 18 postProcessFXAA,
//   19 smaaEdge, 20 smaaBlend, 21 smaaResolve, 22 postProcessHistogram,
//   23 present.
// If a future change reorders pipeline creation, update the constants.
// ---------------------------------------------------------------------------

namespace
{
    constexpr int kPostProcessFXAACreateCallIndex = 18;
    constexpr int kPostProcessSMAAResolveCreateCallIndex = 21;
}

TEST(RendererFrameLifecycle, TAARecordsReconstructionPassAndDiagnostics)
{
    Extrinsic::Tests::MockDevice device;
    device.Operational = true;
    device.BackbufferHandle = Extrinsic::RHI::TextureHandle{323u, 1u};
    device.NextFrame = Extrinsic::RHI::FrameHandle{.FrameIndex = 1u, .SwapchainImageIndex = 0u};

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer = Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);
    renderer->GetPostProcessSystem().SetSettings(Extrinsic::Graphics::PostProcessSettings{
        .Enabled = true,
        .AntiAliasing = Extrinsic::Graphics::PostProcessAntiAliasing::TAA,
    });

    Extrinsic::RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));
    const Extrinsic::Graphics::RenderFrameInput input{
        .Viewport = {.Width = 128, .Height = 72},
    };
    Extrinsic::Graphics::RenderWorld world = renderer->ExtractRenderWorld(input);
    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(frame, world);

    const Extrinsic::Graphics::RenderGraphFrameStats& stats = renderer->GetLastRenderGraphStats();
    EXPECT_TRUE(stats.Compile.Succeeded) << stats.Diagnostic;
    EXPECT_TRUE(stats.Execute.Succeeded) << stats.Diagnostic;

    const Extrinsic::Graphics::RenderGraphCommandPassStats* reconstruction =
        FindCommandPass(stats, "ReconstructionPass");
    ASSERT_NE(reconstruction, nullptr);
    EXPECT_EQ(reconstruction->Status,
              Extrinsic::Graphics::RenderCommandPassStatus::Recorded);
    EXPECT_EQ(FindCommandPass(stats, "PostProcessAAEdgePass"), nullptr);
    EXPECT_EQ(FindCommandPass(stats, "PostProcessAABlendPass"), nullptr);
    EXPECT_EQ(FindCommandPass(stats, "PostProcessAAResolvePass"), nullptr);
    EXPECT_EQ(stats.ReconstructorAppliedFrames, 1u);
    EXPECT_FLOAT_EQ(stats.HistoryDisocclusionPercent, 0.0f);
    EXPECT_TRUE(stats.JitterOffsetX != 0.0f || stats.JitterOffsetY != 0.0f);

    renderer->Shutdown();
}

TEST(RendererFrameLifecycle, FXAASelectedWithoutPipelineKeepsResolveSkippedAndPresentOnSceneColorLDR)
{
    Extrinsic::Tests::MockDevice device;
    device.Operational = true;
    device.BackbufferHandle = Extrinsic::RHI::TextureHandle{321u, 1u};
    device.FailPipelineCreateCall = kPostProcessFXAACreateCallIndex;

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer = Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);

    // The targeted Create call should have failed; FXAA lease is invalid
    // while SMAA leases remain valid.
    EXPECT_FALSE(renderer->GetPostProcessFXAAPipeline().IsValid())
        << "Test fixture targeted the wrong pipeline-create call; "
           "FailPipelineCreateCall index needs to match the FXAA slot.";
    EXPECT_TRUE(renderer->GetPostProcessSMAAResolvePipeline().IsValid());

    // Select FXAA. Without the gate-tightening the resolve helper would
    // accept the pass (SMAA resolve pipeline exists), the recipe would
    // flip presentSource to AATemp.Resolved, and present would consume
    // a cleared attachment.
    renderer->GetPostProcessSystem().SetSettings(Extrinsic::Graphics::PostProcessSettings{
        .Enabled = true,
        .AntiAliasing = Extrinsic::Graphics::PostProcessAntiAliasing::FXAA,
    });

    Extrinsic::RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));
    const Extrinsic::Graphics::RenderFrameInput input{
        .Viewport = {.Width = 128, .Height = 72},
    };
    Extrinsic::Graphics::RenderWorld world = renderer->ExtractRenderWorld(input);
    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(frame, world);

    const Extrinsic::Graphics::RenderGraphFrameStats& stats = renderer->GetLastRenderGraphStats();
    EXPECT_TRUE(stats.Compile.Succeeded) << stats.Diagnostic;
    EXPECT_TRUE(stats.Execute.Succeeded) << stats.Diagnostic;

    const Extrinsic::Graphics::RenderGraphCommandPassStats* resolvePass =
        FindCommandPass(stats, "PostProcessAAResolvePass");
    ASSERT_NE(resolvePass, nullptr);
    EXPECT_EQ(resolvePass->Status,
              Extrinsic::Graphics::RenderCommandPassStatus::SkippedUnavailable)
        << "FXAA was selected but the FXAA pipeline failed to build; the "
           "resolve helper must report SkippedUnavailable rather than "
           "falsely recording a no-op against the unwritten resolved "
           "attachment.";

    renderer->Shutdown();
}

TEST(RendererFrameLifecycle, SMAASelectedWithoutResolvePipelineKeepsResolveSkippedAndPresentOnSceneColorLDR)
{
    Extrinsic::Tests::MockDevice device;
    device.Operational = true;
    device.BackbufferHandle = Extrinsic::RHI::TextureHandle{322u, 1u};
    device.FailPipelineCreateCall = kPostProcessSMAAResolveCreateCallIndex;

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer = Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);

    EXPECT_FALSE(renderer->GetPostProcessSMAAResolvePipeline().IsValid())
        << "Test fixture targeted the wrong pipeline-create call; "
           "FailPipelineCreateCall index needs to match the SMAA resolve "
           "slot.";
    EXPECT_TRUE(renderer->GetPostProcessFXAAPipeline().IsValid());
    EXPECT_TRUE(renderer->GetPostProcessSMAAEdgePipeline().IsValid());
    EXPECT_TRUE(renderer->GetPostProcessSMAABlendPipeline().IsValid());

    renderer->GetPostProcessSystem().SetSettings(Extrinsic::Graphics::PostProcessSettings{
        .Enabled = true,
        .AntiAliasing = Extrinsic::Graphics::PostProcessAntiAliasing::SMAA,
    });

    Extrinsic::RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));
    const Extrinsic::Graphics::RenderFrameInput input{
        .Viewport = {.Width = 128, .Height = 72},
    };
    Extrinsic::Graphics::RenderWorld world = renderer->ExtractRenderWorld(input);
    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(frame, world);

    const Extrinsic::Graphics::RenderGraphFrameStats& stats = renderer->GetLastRenderGraphStats();
    EXPECT_TRUE(stats.Compile.Succeeded) << stats.Diagnostic;
    EXPECT_TRUE(stats.Execute.Succeeded) << stats.Diagnostic;

    const Extrinsic::Graphics::RenderGraphCommandPassStats* resolvePass =
        FindCommandPass(stats, "PostProcessAAResolvePass");
    ASSERT_NE(resolvePass, nullptr);
    EXPECT_EQ(resolvePass->Status,
              Extrinsic::Graphics::RenderCommandPassStatus::SkippedUnavailable)
        << "SMAA was selected but the resolve pipeline failed to build; "
           "the resolve helper must report SkippedUnavailable so the "
           "recipe-build site keeps presentSource on SceneColorLDR.";

    renderer->Shutdown();
}

TEST(RendererFrameLifecycle, SelectionOutlinePassRecordsWhenSelectableEntityPresent)
{
    Extrinsic::Tests::MockDevice device;
    device.Operational = true;
    device.BackbufferHandle = Extrinsic::RHI::TextureHandle{292u, 1u};

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer = Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);

    Extrinsic::RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));

    const Extrinsic::Graphics::RenderFrameInput input{
        .Viewport = {.Width = 320, .Height = 240},
    };
    Extrinsic::Graphics::RenderWorld world = renderer->ExtractRenderWorld(input);
    // Force `features.EnableSelectionOutline = true` so the recipe declares
    // `SelectionOutlinePass`. Without this the recipe drops the pass and
    // `FindCommandPass(stats, "SelectionOutlinePass")` would correctly return
    // null — covering only the "outline is gated off" path, not Slice C's
    // executor-route contract.
    world.Selection.HasHovered = true;
    world.Selection.HoveredStableId = 42u;

    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(frame, world);

    const Extrinsic::Graphics::RenderGraphFrameStats& stats = renderer->GetLastRenderGraphStats();
    EXPECT_TRUE(stats.Compile.Succeeded) << stats.Diagnostic;
    EXPECT_TRUE(stats.Execute.Succeeded) << stats.Diagnostic;
    EXPECT_TRUE(stats.Execute.DeviceOperational);

    const Extrinsic::Graphics::RenderGraphCommandPassStats* outlinePass =
        FindCommandPass(stats, "SelectionOutlinePass");
    ASSERT_NE(outlinePass, nullptr);
    EXPECT_EQ(outlinePass->Status, Extrinsic::Graphics::RenderCommandPassStatus::Recorded);

    // GRAPHICS-074 Slice C/D.4 — the outline pass must push a deterministic
    // 144-byte `SelectionOutlinePushConstants` block before its fullscreen
    // draw so the shader never reads stale push memory from a prior pass.
    // Slice D.4 now sources the payload from `renderWorld.Selection`, so the
    // recorded bytes match `BuildSelectionOutlinePushConstants(...)` for the
    // seeded snapshot (rather than the Slice C all-zero placeholder). Walk
    // the captured `PushConstants(...)` payloads for a 144-byte block that
    // byte-matches the expected contents — earlier passes in the same frame
    // contribute their own (`GpuScenePushConstants`, etc.) so we cannot
    // just assert payload count == 1.
    const Extrinsic::Graphics::SelectionOutlinePushConstants expected =
        Extrinsic::Graphics::BuildSelectionOutlinePushConstants(world.Selection);
    bool foundOutlinePush = false;
    for (const std::vector<std::byte>& payload : device.CommandContext.PushConstantPayloads)
    {
        if (payload.size() != sizeof(Extrinsic::Graphics::SelectionOutlinePushConstants))
        {
            continue;
        }
        if (std::memcmp(payload.data(), &expected, sizeof(expected)) == 0)
        {
            foundOutlinePush = true;
            break;
        }
    }
    EXPECT_TRUE(foundOutlinePush)
        << "SelectionOutlinePass must push a 144-byte SelectionOutlinePushConstants "
        << "block byte-matching BuildSelectionOutlinePushConstants(renderWorld.Selection).";

    renderer->Shutdown();
}

TEST(RendererFrameLifecycle, SelectionOutlineBindsEntityIdToDedicatedSampledSlot)
{
    Extrinsic::Tests::MockDevice device;
    device.Operational = true;
    device.BackbufferHandle = Extrinsic::RHI::TextureHandle{293u, 1u};

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer = Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);

    Extrinsic::RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));

    const Extrinsic::Graphics::RenderFrameInput input{
        .Viewport = {.Width = 320, .Height = 240},
    };
    Extrinsic::Graphics::RenderWorld world = renderer->ExtractRenderWorld(input);
    const std::uint32_t selectedIds[] = {42u};
    world.Selection.SelectedStableIds = std::span<const std::uint32_t>(selectedIds);

    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(frame, world);

    const Extrinsic::Graphics::RenderGraphFrameStats& stats = renderer->GetLastRenderGraphStats();
    EXPECT_TRUE(stats.Compile.Succeeded) << stats.Diagnostic;
    EXPECT_TRUE(stats.Execute.Succeeded) << stats.Diagnostic;
    EXPECT_EQ(CountCommandPass(stats, "PickingPass"), 1u)
        << "Outline-only selected frames need the EntityId target but must not "
           "record the face/edge/point primitive-picking subpasses.";
    EXPECT_EQ(stats.SelectionOutlineEntityIdPassCount, 1u)
        << "Outline-only selected frames should record exactly the one-target "
           "EntityId producer.";
    EXPECT_EQ(stats.SelectionPrimitiveIdPassCount, 0u)
        << "Primitive ID refinement belongs only to pending click-pick frames.";
    EXPECT_EQ(stats.PickingReadbackCopyCount, 0u)
        << "Selected/hovered outline frames must not enqueue a pick readback "
           "unless a click-pick request is pending.";

    const Extrinsic::Graphics::RenderGraphCommandPassStats* outlinePass =
        FindCommandPass(stats, "SelectionOutlinePass");
    ASSERT_NE(outlinePass, nullptr);
    EXPECT_EQ(outlinePass->Status, Extrinsic::Graphics::RenderCommandPassStatus::Recorded);

    const Extrinsic::RHI::TextureHandle entityId =
        FindCreatedTextureByDebugName(device, "EntityId");
    ASSERT_TRUE(entityId.IsValid())
        << "Selection outline must have a concrete EntityId texture to sample.";
    EXPECT_FALSE(FindCreatedTextureByDebugName(device, "PrimitiveId").IsValid())
        << "Outline-only selected frames must not allocate a PrimitiveId target.";

    Extrinsic::RHI::TextureHandle lastDefaultSlotBinding{};
    Extrinsic::RHI::TextureHandle lastSelectionOutlineSlotBinding{};
    for (const auto& binding : device.CommandContext.SampledTextureBindings)
    {
        if (binding.DescriptorIndex == 0u)
        {
            lastDefaultSlotBinding = binding.Texture;
        }
        if (binding.DescriptorIndex == 3u)
        {
            lastSelectionOutlineSlotBinding = binding.Texture;
        }
    }

    EXPECT_NE(lastDefaultSlotBinding, entityId)
        << "SelectionOutlinePass must not overwrite descriptor slot 0; "
        << "that slot is still sampled by earlier-recorded postprocess draws "
        << "when the single bindless descriptor set is submitted.";
    EXPECT_EQ(lastSelectionOutlineSlotBinding, entityId)
        << "selection_outline.frag samples EntityId from dedicated frame-sampled "
        << "descriptor slot 3 so hierarchy selection cannot clobber tonemap slot 0.";

    renderer->Shutdown();
}

// ---------------------------------------------------------------------------
// GRAPHICS-074 Slice D.4 — outline push-constant plumbing from
// `RenderWorld::Selection`. The default-recipe `"SelectionOutlinePass"`
// executor route now builds the `selection_outline.frag` push block from the
// runtime-extracted snapshot via
// `BuildSelectionOutlinePushConstants(renderWorld.Selection)` and pushes it
// before `Draw(3,1,0,0)`. Seeding a non-trivial snapshot (multiple selected
// ids, a hovered id, a non-default outline color/width/mode) and asserting
// the captured 144-byte push payload byte-matches the helper output
// exercises:
//   - SelectedStableIds is copied into `SelectedIds[]` and truncated to the
//     `kSelectionOutlineMaxSelectedIds` cap (smaller in this test).
//   - HoveredStableId only lands in `HoveredId` when `HasHovered` is true.
//   - The outline visual style fields (color/width/mode/fill/pulse/glow) are
//     plumbed through verbatim under the std430 byte layout the shader
//     reads (`vec4 OutlineColor + vec4 HoverColor + ...`).
// ---------------------------------------------------------------------------

TEST(RendererFrameLifecycle, SelectionOutlinePushConstantsMatchRecipeInputs)
{
    Extrinsic::Tests::MockDevice device;
    device.Operational = true;
    device.BackbufferHandle = Extrinsic::RHI::TextureHandle{295u, 1u};

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer = Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);

    Extrinsic::RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));

    const Extrinsic::Graphics::RenderFrameInput input{
        .Viewport = {.Width = 320, .Height = 240},
    };
    Extrinsic::Graphics::RenderWorld world = renderer->ExtractRenderWorld(input);

    // Seed the snapshot with a recognisable selection set + hover + a
    // non-default visual style. The backing vector outlives `ExecuteFrame`
    // since the snapshot's `SelectedStableIds` is a non-owning span.
    const std::vector<std::uint32_t> selectedIds{11u, 22u, 33u, 44u};
    world.Selection.SelectedStableIds = std::span<const std::uint32_t>(selectedIds);
    world.Selection.HasHovered = true;
    world.Selection.HoveredStableId = 99u;
    world.Selection.OutlineColor = glm::vec4(0.5f, 0.25f, 0.75f, 1.0f);
    world.Selection.HoverColor   = glm::vec4(0.10f, 0.90f, 0.40f, 0.50f);
    world.Selection.OutlineWidth = 3.0f;
    world.Selection.OutlineMode  = 1u; // Pulse
    world.Selection.SelectionFillAlpha = 0.20f;
    world.Selection.HoverFillAlpha     = 0.05f;
    world.Selection.PulsePhase = 1.25f;
    world.Selection.PulseMin   = 0.30f;
    world.Selection.PulseMax   = 0.95f;
    world.Selection.GlowFalloff = 1.75f;

    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(frame, world);

    const Extrinsic::Graphics::RenderGraphFrameStats& stats = renderer->GetLastRenderGraphStats();
    EXPECT_TRUE(stats.Compile.Succeeded) << stats.Diagnostic;
    EXPECT_TRUE(stats.Execute.Succeeded) << stats.Diagnostic;
    EXPECT_TRUE(stats.Execute.DeviceOperational);

    const Extrinsic::Graphics::RenderGraphCommandPassStats* outlinePass =
        FindCommandPass(stats, "SelectionOutlinePass");
    ASSERT_NE(outlinePass, nullptr);
    EXPECT_EQ(outlinePass->Status, Extrinsic::Graphics::RenderCommandPassStatus::Recorded);

    // Reference payload computed via the public helper the renderer also
    // uses. Any divergence between this and the recorded payload indicates
    // the renderer either failed to forward `renderWorld.Selection` or
    // diverged from the shader's std430 layout.
    const Extrinsic::Graphics::SelectionOutlinePushConstants expected =
        Extrinsic::Graphics::BuildSelectionOutlinePushConstants(world.Selection);

    EXPECT_EQ(expected.SelectedCount, 4u);
    EXPECT_EQ(expected.HoveredId, 99u);
    EXPECT_EQ(expected.OutlineMode, 1u);
    EXPECT_EQ(expected.SelectedIds[0], 11u);
    EXPECT_EQ(expected.SelectedIds[1], 22u);
    EXPECT_EQ(expected.SelectedIds[2], 33u);
    EXPECT_EQ(expected.SelectedIds[3], 44u);

    bool matched = false;
    for (const std::vector<std::byte>& payload : device.CommandContext.PushConstantPayloads)
    {
        if (payload.size() != sizeof(Extrinsic::Graphics::SelectionOutlinePushConstants))
        {
            continue;
        }
        if (std::memcmp(payload.data(), &expected, sizeof(expected)) == 0)
        {
            matched = true;
            break;
        }
    }
    EXPECT_TRUE(matched)
        << "Recorded SelectionOutlinePass push payload did not byte-match "
        << "BuildSelectionOutlinePushConstants(renderWorld.Selection).";

    renderer->Shutdown();
}

// ---------------------------------------------------------------------------
// GRAPHICS-074 Slice D.1 — renderer-owned host-visible `Picking.Readback`
// buffer lifecycle. The operational publisher allocates the buffer the first
// time `InitializeOperationalPassResources()` runs and intentionally does
// *not* re-allocate it on subsequent `RebuildOperationalResources()` calls,
// so the handle Slice D.2 will import into the recipe stays byte-identical
// across rebuilds (same pattern `ShadowSystem` uses for its depth atlas).
// The buffer is sized for `16 * frames-in-flight` bytes: one 4-byte
// `EntityId` word + one 4-byte `EncodedSelectionId` word (`GRAPHICS-012Q`)
// + one 4-byte `SceneDepth` R32 float sample (BUG-026) + 4 pad bytes per
// in-flight frame slot, allocated with `HostVisible = true` +
// `BufferUsage::TransferDst` so the executor can record
// `CopyTextureToBuffer(EntityId/PrimitiveId/SceneDepth, ...,
// m_PickingReadbackBuffer, slot * 16 [+4|+8])` after the four selection-ID
// sub-passes and the drain can map the buffer on `BeginFrame()` once the
// issuing frame has completed.
// ---------------------------------------------------------------------------

TEST(RendererFrameLifecycle, PickingReadbackBufferSurvivesOperationalRebuild)
{
    Extrinsic::Tests::MockDevice device;
    device.Operational = true;
    device.BackbufferHandle = Extrinsic::RHI::TextureHandle{293u, 1u};

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer = Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);

    const Extrinsic::RHI::BufferHandle initialBuffer = renderer->GetPickingReadbackBuffer();
    EXPECT_TRUE(initialBuffer.IsValid());

    // Size = 16 bytes per in-flight frame slot (EntityId word +
    // EncodedSelectionId word + SceneDepth float + pad, BUG-026).
    // `MockDevice::GetFramesInFlight()` returns 2, so 32 bytes.
    const std::uint64_t initialSize = renderer->GetPickingReadbackBufferSize();
    EXPECT_EQ(initialSize, static_cast<std::uint64_t>(16u) *
                               static_cast<std::uint64_t>(device.GetFramesInFlight()));

    EXPECT_TRUE(renderer->RebuildOperationalResources(device));

    // The buffer survives the rebuild byte-identical: same handle (so the
    // recipe import Slice D.2 wires up stays stable across rebuilds) and
    // same size.
    const Extrinsic::RHI::BufferHandle rebuiltBuffer = renderer->GetPickingReadbackBuffer();
    EXPECT_TRUE(rebuiltBuffer.IsValid());
    EXPECT_EQ(rebuiltBuffer.Index, initialBuffer.Index);
    EXPECT_EQ(rebuiltBuffer.Generation, initialBuffer.Generation);
    EXPECT_EQ(renderer->GetPickingReadbackBufferSize(), initialSize);

    renderer->Shutdown();

    // After `Shutdown()` the lease is released and a fresh accessor returns
    // an invalid handle / zero size, so a later `Initialize()` would
    // allocate against the new BufferManager rather than handing out a
    // dangling handle.
    EXPECT_FALSE(renderer->GetPickingReadbackBuffer().IsValid());
    EXPECT_EQ(renderer->GetPickingReadbackBufferSize(), 0u);
}

// GRAPHICS-074 Slice D.1 — when `RebuildOperationalResources()` runs against
// a device whose `GetFramesInFlight()` differs from the previous allocation
// (e.g. a swapchain rebuild changed the in-flight count), the lazy allocator
// must drop the old lease and re-create the buffer so the executor's
// `slot * 16` per-frame copy addressing never overruns the allocation.
TEST(RendererFrameLifecycle, PickingReadbackBufferReallocatesWhenFramesInFlightChanges)
{
    Extrinsic::Tests::MockDevice device;
    device.Operational = true;
    device.BackbufferHandle = Extrinsic::RHI::TextureHandle{294u, 1u};
    device.FramesInFlight = 2u;

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer = Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);

    const Extrinsic::RHI::BufferHandle initialBuffer = renderer->GetPickingReadbackBuffer();
    ASSERT_TRUE(initialBuffer.IsValid());
    EXPECT_EQ(renderer->GetPickingReadbackBufferSize(),
              static_cast<std::uint64_t>(16u) * 2u);

    // Simulate a swapchain rebuild that promotes the device from
    // double- to triple-buffered. The lease must be reallocated so the
    // buffer is sized for three slots, not the original two.
    device.FramesInFlight = 3u;
    EXPECT_TRUE(renderer->RebuildOperationalResources(device));

    const Extrinsic::RHI::BufferHandle resizedBuffer = renderer->GetPickingReadbackBuffer();
    ASSERT_TRUE(resizedBuffer.IsValid());
    EXPECT_EQ(renderer->GetPickingReadbackBufferSize(),
              static_cast<std::uint64_t>(16u) * 3u);
    // The reallocation must surface as a *different* handle so downstream
    // recipe imports (Slice D.2) re-import the new buffer rather than
    // continuing to copy into the freed allocation. `BufferManager`
    // recycles slot indices through a free list and bumps `Generation` on
    // each free, so the new handle typically reuses the same index with a
    // newer generation — assert handle inequality (either component
    // differs) rather than just index inequality.
    EXPECT_TRUE(resizedBuffer.Index != initialBuffer.Index ||
                resizedBuffer.Generation != initialBuffer.Generation)
        << "Expected the reallocated buffer to have a different handle than "
        << "the original (got Index=" << resizedBuffer.Index
        << " Generation=" << resizedBuffer.Generation << " both before and after).";

    // Subsequent rebuilds with the same frames-in-flight count must keep
    // the handle stable (the lazy path of the allocator).
    EXPECT_TRUE(renderer->RebuildOperationalResources(device));
    const Extrinsic::RHI::BufferHandle stableBuffer = renderer->GetPickingReadbackBuffer();
    EXPECT_EQ(stableBuffer.Index, resizedBuffer.Index);
    EXPECT_EQ(stableBuffer.Generation, resizedBuffer.Generation);
    EXPECT_EQ(renderer->GetPickingReadbackBufferSize(),
              static_cast<std::uint64_t>(16u) * 3u);

    renderer->Shutdown();
}

// GRAPHICS-074 Slice D.2 — when a pick is pending and the device is
// operational, the PickingPass executor branch must record the EntityId +
// PrimitiveId texture-to-buffer copy pair (wrapped by ColorAttachment →
// TransferSrc → ColorAttachment barriers) into the renderer-owned host-visible
// `Picking.Readback` buffer at the per-frame slot. The CPU-observable contract
// for the copy is the per-frame `PickingReadbackCopyCount` counter on
// `RenderGraphFrameStats`. Slice D.3 drains the buffer on `BeginFrame()` to
// publish `PublishPickResult`/`PublishNoHit`.
TEST(RendererFrameLifecycle, PickingReadbackCopyRecordedWhenPickPending)
{
    Extrinsic::Tests::MockDevice device;
    device.Operational = true;
    device.BackbufferHandle = Extrinsic::RHI::TextureHandle{295u, 1u};

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer = Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);

    const Extrinsic::RHI::BufferHandle pickingBuffer = renderer->GetPickingReadbackBuffer();
    ASSERT_TRUE(pickingBuffer.IsValid());

    Extrinsic::RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));

    // A pending pick request: setting `input.Pick.Pending = true` makes
    // `ExtractRenderWorld` populate `world.PickRequest.Pending = true` and
    // `DeriveDefaultFrameRecipeFeatures` set `EnablePicking = true`. With
    // `EnableDepthPrepass = true` (the default), `pickingActive` is true and
    // the recipe declares `PickingPass` + imports the renderer's buffer.
    const Extrinsic::Graphics::RenderFrameInput input{
        .Viewport = {.Width = 320, .Height = 240},
        .HasPendingPick = true,
        .Pick = Extrinsic::Graphics::PickPixelRequest{.X = 17u, .Y = 23u, .Pending = true},
    };
    Extrinsic::Graphics::RenderWorld world = renderer->ExtractRenderWorld(input);
    ASSERT_TRUE(world.PickRequest.Pending);

    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(frame, world);

    const Extrinsic::Graphics::RenderGraphFrameStats& stats = renderer->GetLastRenderGraphStats();
    EXPECT_TRUE(stats.Compile.Succeeded) << stats.Diagnostic;
    EXPECT_TRUE(stats.Execute.Succeeded) << stats.Diagnostic;
    EXPECT_TRUE(stats.Execute.DeviceOperational);

    // The four selection-ID sub-passes record together with the readback
    // copy pair under the single `PickingPass` aggregate. The pass must
    // report `Recorded` (one of the sub-passes records the
    // `Bind/Push/DrawIndirectCount` shape against the operational device).
    const Extrinsic::Graphics::RenderGraphCommandPassStats* pickingPass =
        FindCommandPass(stats, "PickingPass");
    ASSERT_NE(pickingPass, nullptr);
    EXPECT_EQ(pickingPass->Status, Extrinsic::Graphics::RenderCommandPassStatus::Recorded);
    EXPECT_EQ(CountCommandPass(stats, "PickingPass"), 4u)
        << "Pending click-pick frames must still record entity, face, edge, "
           "and point ID subpasses before the readback copy.";
    EXPECT_EQ(stats.SelectionOutlineEntityIdPassCount, 0u);
    EXPECT_EQ(stats.SelectionPrimitiveIdPassCount, 1u)
        << "Pending click-pick frames should expose one primitive-ID work "
           "diagnostic after the face/edge/point subpasses record.";

    EXPECT_EQ(stats.PickingReadbackCopyCount, 1u)
        << "Picking-readback copy pair must record exactly once per operational "
           "frame when a pick is pending.";

    // The ColorAttachment → TransferSrc → ColorAttachment triplet must be
    // visible on the mock context for both EntityId and PrimitiveId
    // transient targets — but their handles are generated by the framegraph
    // compiler so we cannot look them up by name from outside. Instead
    // assert the aggregate barrier shape: at least two pairs of (CA→TS) +
    // (TS→CA) transitions land in the recorded barriers (one per
    // EntityId / PrimitiveId) after the picking sub-passes.
    std::uint32_t colorToTransfer = 0u;
    std::uint32_t transferToColor = 0u;
    for (const auto& barrier : device.CommandContext.TextureBarrierCalls)
    {
        if (barrier.Before == Extrinsic::RHI::TextureLayout::ColorAttachment &&
            barrier.After == Extrinsic::RHI::TextureLayout::TransferSrc)
        {
            ++colorToTransfer;
        }
        else if (barrier.Before == Extrinsic::RHI::TextureLayout::TransferSrc &&
                 barrier.After == Extrinsic::RHI::TextureLayout::ColorAttachment)
        {
            ++transferToColor;
        }
    }
    EXPECT_GE(colorToTransfer, 2u)
        << "Picking readback must record ColorAttachment → TransferSrc barriers "
        << "for both EntityId and PrimitiveId before the copies.";
    EXPECT_GE(transferToColor, 2u)
        << "Picking readback must restore EntityId and PrimitiveId to "
        << "ColorAttachment after the copies so downstream barriers stay valid.";

    renderer->Shutdown();
}

// GRAPHICS-074 Slice D.2 — when no pick is pending, the recipe drops the
// PickingPass entirely (`EnablePicking = false`), so no readback copy is
// recorded and `PickingReadbackCopyCount` stays at zero. This test pairs
// with `PickingReadbackCopyRecordedWhenPickPending` to lock in that the
// per-frame counter accurately distinguishes pending and non-pending frames.
TEST(RendererFrameLifecycle, PickingReadbackCopySkippedWhenNotPending)
{
    Extrinsic::Tests::MockDevice device;
    device.Operational = true;
    device.BackbufferHandle = Extrinsic::RHI::TextureHandle{296u, 1u};

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer = Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);

    const Extrinsic::RHI::BufferHandle pickingBuffer = renderer->GetPickingReadbackBuffer();
    ASSERT_TRUE(pickingBuffer.IsValid());

    Extrinsic::RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));

    // No pick pending: `EnablePicking = false` upstream, so the recipe
    // does not declare PickingPass and the executor never reaches the
    // copy-pair recording site.
    const Extrinsic::Graphics::RenderFrameInput input{
        .Viewport = {.Width = 320, .Height = 240},
    };
    Extrinsic::Graphics::RenderWorld world = renderer->ExtractRenderWorld(input);
    ASSERT_FALSE(world.PickRequest.Pending);

    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(frame, world);

    const Extrinsic::Graphics::RenderGraphFrameStats& stats = renderer->GetLastRenderGraphStats();
    EXPECT_TRUE(stats.Compile.Succeeded) << stats.Diagnostic;
    EXPECT_TRUE(stats.Execute.Succeeded) << stats.Diagnostic;
    EXPECT_EQ(stats.PickingReadbackCopyCount, 0u)
        << "Picking-readback copy must not record when no pick is pending.";

    // PickingPass must not appear in the recorded command stats either —
    // the recipe drops the pass entirely when `EnablePicking = false`.
    EXPECT_EQ(FindCommandPass(stats, "PickingPass"), nullptr)
        << "Recipe must drop PickingPass entirely when no pick is pending.";

    renderer->Shutdown();
}

// ---------------------------------------------------------------------------
// GRAPHICS-074 Slice D.3 — `BeginFrame()`-side drain that decodes the
// per-slot `Picking.Readback` bytes and routes the result to
// `SelectionSystem::PublishPickResult(...)` / `PublishNoHit()`. The
// `MockCommandContext::CopyTextureToBuffer(...)` is a no-op (no GPU traffic),
// so each test seeds the renderer's host-visible buffer via
// `MockDevice::BufferContents[handle.Index]` to simulate the bytes the
// GRAPHICS-072 / GRAPHICS-012Q EntityId + EncodedSelectionId pipeline pair
// would have written into slot 0 (`MockDevice::FramesInFlight = 2` keeps the
// arithmetic compatible with the production sizing). The slot bookkeeping
// the drain keys off (`m_PickingSlotPending[slot]`,
// `m_PickingSlotIssuedFrame[slot]`, `m_PickingSlotInvalidated[slot]`) is
// populated by the D.2 copy-pair recording site under
// `world.PickRequest.Pending = true`; the drain then runs at the *next*
// `BeginFrame()` once `IDevice::GetGlobalFrameNumber()` has incremented past
// the issuing frame.
// ---------------------------------------------------------------------------

namespace
{
    // Helper: encode one slot's words (`EntityId` + `EncodedSelectionId` +
    // `SceneDepth` float bits, BUG-026) into `MockDevice::BufferContents` so
    // the next `BeginFrame()` drain reads them back. The slot mirrors the
    // `slot * 16 [+4|+8]` offsets the executor records.
    void SeedPickingReadbackSlot(Extrinsic::Tests::MockDevice& device,
                                 const Extrinsic::RHI::BufferHandle& buffer,
                                 const std::uint64_t bufferSize,
                                 const std::size_t slot,
                                 const std::uint32_t entityId,
                                 const Extrinsic::Graphics::EncodedSelectionId encoded,
                                 const float depth = 1.0f)
    {
        std::vector<std::byte>& contents = device.BufferContents[buffer.Index];
        contents.assign(static_cast<std::size_t>(bufferSize), std::byte{0});
        const std::size_t offset = slot * 16u;
        std::memcpy(contents.data() + offset,                &entityId,         sizeof(entityId));
        std::memcpy(contents.data() + offset + 4u, &encoded.Value, sizeof(encoded.Value));
        std::memcpy(contents.data() + offset + 8u, &depth, sizeof(depth));
    }
}

TEST(RendererFrameLifecycle, PickingReadbackPublishesPickResultForHitPixel)
{
    Extrinsic::Tests::MockDevice device;
    device.Operational = true;
    device.BackbufferHandle = Extrinsic::RHI::TextureHandle{297u, 1u};

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer = Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);

    const Extrinsic::RHI::BufferHandle pickingBuffer = renderer->GetPickingReadbackBuffer();
    ASSERT_TRUE(pickingBuffer.IsValid());
    const std::uint64_t pickingBufferSize = renderer->GetPickingReadbackBufferSize();
    ASSERT_GE(pickingBufferSize, 8u);

    // Frame 0 — record the copy. `world.PickRequest.Pending = true` after
    // extraction causes the executor to populate the slot-0 metadata
    // (`Pending=true`, `IssuedFrame=0`).
    Extrinsic::RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));

    const Extrinsic::Graphics::RenderFrameInput input{
        .Viewport = {.Width = 320, .Height = 240},
        .HasPendingPick = true,
        .Pick = Extrinsic::Graphics::PickPixelRequest{.X = 17u, .Y = 23u, .Pending = true},
    };
    Extrinsic::Graphics::RenderWorld world = renderer->ExtractRenderWorld(input);
    ASSERT_TRUE(world.PickRequest.Pending);

    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(frame, world);
    ASSERT_EQ(renderer->GetLastRenderGraphStats().PickingReadbackCopyCount, 1u);

    [[maybe_unused]] const std::uint64_t completedFrame = renderer->EndFrame(frame);
    ASSERT_GE(device.GlobalFrameNumber, 1u);

    // Seed the renderer-owned host-visible buffer with the bytes the GPU
    // would have copied into slot 0 for a hit pixel: `EntityId = 42`,
    // `EncodedSelectionId = EncodeSelectionId(Entity, 0)`. The drain at the
    // next BeginFrame reads these via `MockDevice::ReadBuffer`.
    constexpr std::uint32_t hitStableEntityId = 42u;
    constexpr float hitDepth = 0.625f;
    const Extrinsic::Graphics::EncodedSelectionId hitEncoded =
        Extrinsic::Graphics::EncodeSelectionId(
            Extrinsic::Graphics::SelectionPrimitiveDomain::Entity, 0u);
    SeedPickingReadbackSlot(device, pickingBuffer, pickingBufferSize,
                            /*slot=*/0u, hitStableEntityId, hitEncoded, hitDepth);

    // Frame 1 — the drain runs at the top of `BeginFrame()` before
    // `m_Device->BeginFrame(...)`. Slot 0 has `IssuedFrame=0 <
    // GlobalFrameNumber=1`, so the drain reads the bytes we seeded and
    // routes to `PublishPickResult` (non-zero EntityId, not invalidated).
    device.NextFrame.FrameIndex = 1u;
    Extrinsic::RHI::FrameHandle nextFrame{};
    ASSERT_TRUE(renderer->BeginFrame(nextFrame));

    const Extrinsic::Graphics::SelectionSystem& selection = renderer->GetSelectionSystem();
    const std::optional<Extrinsic::Graphics::PickReadbackResult> last = selection.GetLastPickResult();
    ASSERT_TRUE(last.has_value()) << "Drain must publish a PickReadbackResult for a hit slot.";
    EXPECT_TRUE(last->Hit);
    EXPECT_EQ(last->StableEntityId, hitStableEntityId);
    EXPECT_EQ(last->EncodedId.Value, hitEncoded.Value);
    // BUG-026 — the drain publishes the SceneDepth sample and the request
    // pixel so the runtime can unproject the world-space cursor position.
    EXPECT_TRUE(last->HasDepth);
    EXPECT_EQ(last->Depth, hitDepth);
    EXPECT_EQ(last->PixelX, 17u);
    EXPECT_EQ(last->PixelY, 23u);

    const Extrinsic::Graphics::SelectionSystemDiagnostics diag = selection.GetDiagnostics();
    EXPECT_EQ(diag.PickHitCount, 1u);
    EXPECT_EQ(diag.PickNoHitCount, 0u);

    renderer->Shutdown();
}

// RUNTIME-089 — the runtime correlation `Sequence` on the issuing pick must
// survive the full round-trip: RenderFrameInput::Pick -> RenderWorld.PickRequest
// -> the picking slot bookkeeping -> the published PickReadbackResult. Without
// it the runtime cannot tell which in-flight request a readback belongs to.
TEST(RendererFrameLifecycle, PickingReadbackPreservesCorrelationSequence)
{
    Extrinsic::Tests::MockDevice device;
    device.Operational = true;
    device.BackbufferHandle = Extrinsic::RHI::TextureHandle{298u, 1u};

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer = Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);

    const Extrinsic::RHI::BufferHandle pickingBuffer = renderer->GetPickingReadbackBuffer();
    ASSERT_TRUE(pickingBuffer.IsValid());
    const std::uint64_t pickingBufferSize = renderer->GetPickingReadbackBufferSize();
    ASSERT_GE(pickingBufferSize, 8u);

    constexpr std::uint64_t correlationSequence = 0xABCDEF01ull;

    Extrinsic::RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));

    const Extrinsic::Graphics::RenderFrameInput input{
        .Viewport = {.Width = 320, .Height = 240},
        .HasPendingPick = true,
        .Pick = Extrinsic::Graphics::PickPixelRequest{
            .X = 17u, .Y = 23u, .Pending = true, .Sequence = correlationSequence},
    };
    Extrinsic::Graphics::RenderWorld world = renderer->ExtractRenderWorld(input);
    ASSERT_TRUE(world.PickRequest.Pending);
    EXPECT_EQ(world.PickRequest.Sequence, correlationSequence)
        << "ExtractRenderWorld must carry the pick Sequence onto RenderWorld.";

    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(frame, world);
    ASSERT_EQ(renderer->GetLastRenderGraphStats().PickingReadbackCopyCount, 1u);
    [[maybe_unused]] const std::uint64_t completedFrame = renderer->EndFrame(frame);

    constexpr std::uint32_t hitStableEntityId = 42u;
    constexpr float hitDepth = 0.625f;
    const Extrinsic::Graphics::EncodedSelectionId hitEncoded =
        Extrinsic::Graphics::EncodeSelectionId(
            Extrinsic::Graphics::SelectionPrimitiveDomain::Entity, 0u);
    SeedPickingReadbackSlot(device, pickingBuffer, pickingBufferSize,
                            /*slot=*/0u, hitStableEntityId, hitEncoded, hitDepth);

    device.NextFrame.FrameIndex = 1u;
    Extrinsic::RHI::FrameHandle nextFrame{};
    ASSERT_TRUE(renderer->BeginFrame(nextFrame));

    const Extrinsic::Graphics::SelectionSystem& selection = renderer->GetSelectionSystem();
    const std::optional<Extrinsic::Graphics::PickReadbackResult> last = selection.GetLastPickResult();
    ASSERT_TRUE(last.has_value());
    EXPECT_TRUE(last->Hit);
    EXPECT_EQ(last->StableEntityId, hitStableEntityId);
    EXPECT_EQ(last->Sequence, correlationSequence)
        << "The drained readback must replay the issuing pick's Sequence.";

    renderer->Shutdown();
}

TEST(RendererFrameLifecycle, PickingReadbackPublishesNoHitForMissPixel)
{
    Extrinsic::Tests::MockDevice device;
    device.Operational = true;
    device.BackbufferHandle = Extrinsic::RHI::TextureHandle{298u, 1u};

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer = Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);

    const Extrinsic::RHI::BufferHandle pickingBuffer = renderer->GetPickingReadbackBuffer();
    ASSERT_TRUE(pickingBuffer.IsValid());
    const std::uint64_t pickingBufferSize = renderer->GetPickingReadbackBufferSize();
    ASSERT_GE(pickingBufferSize, 8u);

    Extrinsic::RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));

    const Extrinsic::Graphics::RenderFrameInput input{
        .Viewport = {.Width = 320, .Height = 240},
        .HasPendingPick = true,
        .Pick = Extrinsic::Graphics::PickPixelRequest{.X = 11u, .Y = 4u, .Pending = true},
    };
    Extrinsic::Graphics::RenderWorld world = renderer->ExtractRenderWorld(input);
    ASSERT_TRUE(world.PickRequest.Pending);

    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(frame, world);
    ASSERT_EQ(renderer->GetLastRenderGraphStats().PickingReadbackCopyCount, 1u);

    [[maybe_unused]] const std::uint64_t completedFrame = renderer->EndFrame(frame);

    // Seed the buffer with the bytes a "background" pixel would emit:
    // `EntityId = 0` (no surface won the depth-equal test). The drain
    // must route this to `PublishNoHit()` rather than reporting a hit
    // with `StableEntityId = 0`.
    SeedPickingReadbackSlot(device, pickingBuffer, pickingBufferSize,
                            /*slot=*/0u, /*entityId=*/0u,
                            Extrinsic::Graphics::EncodedSelectionId{.Value = 0u});

    device.NextFrame.FrameIndex = 1u;
    Extrinsic::RHI::FrameHandle nextFrame{};
    ASSERT_TRUE(renderer->BeginFrame(nextFrame));

    const Extrinsic::Graphics::SelectionSystem& selection = renderer->GetSelectionSystem();
    const std::optional<Extrinsic::Graphics::PickReadbackResult> last = selection.GetLastPickResult();
    ASSERT_TRUE(last.has_value())
        << "Drain must publish a NoHit result (an empty PickReadbackResult), not stay silent.";
    EXPECT_FALSE(last->Hit);
    EXPECT_EQ(last->StableEntityId, 0u);
    EXPECT_EQ(last->EncodedId.Value, 0u);

    const Extrinsic::Graphics::SelectionSystemDiagnostics diag = selection.GetDiagnostics();
    EXPECT_EQ(diag.PickHitCount, 0u);
    EXPECT_EQ(diag.PickNoHitCount, 1u);

    renderer->Shutdown();
}

TEST(RendererFrameLifecycle, PickingReadbackPublishesNoHitForInvalidatedRequest)
{
    Extrinsic::Tests::MockDevice device;
    device.Operational = true;
    device.BackbufferHandle = Extrinsic::RHI::TextureHandle{299u, 1u};

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer = Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);

    const Extrinsic::RHI::BufferHandle pickingBuffer = renderer->GetPickingReadbackBuffer();
    ASSERT_TRUE(pickingBuffer.IsValid());
    const std::uint64_t pickingBufferSize = renderer->GetPickingReadbackBufferSize();
    ASSERT_GE(pickingBufferSize, 8u);

    Extrinsic::RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));

    const Extrinsic::Graphics::RenderFrameInput input{
        .Viewport = {.Width = 320, .Height = 240},
        .HasPendingPick = true,
        .Pick = Extrinsic::Graphics::PickPixelRequest{.X = 17u, .Y = 23u, .Pending = true},
    };
    Extrinsic::Graphics::RenderWorld world = renderer->ExtractRenderWorld(input);
    ASSERT_TRUE(world.PickRequest.Pending);

    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(frame, world);
    ASSERT_EQ(renderer->GetLastRenderGraphStats().PickingReadbackCopyCount, 1u);

    [[maybe_unused]] const std::uint64_t completedFrame = renderer->EndFrame(frame);

    // Seed slot 0 with bytes that *would* normally publish a hit — the
    // point of this test is that the invalidation path overrides the byte
    // content. If the drain ignored `Invalidated[0]` it would publish
    // `PickResult{EntityId=99, Hit=true}` and this test would fail.
    constexpr std::uint32_t poisonStableEntityId = 99u;
    const Extrinsic::Graphics::EncodedSelectionId poisonEncoded =
        Extrinsic::Graphics::EncodeSelectionId(
            Extrinsic::Graphics::SelectionPrimitiveDomain::Entity, 0u);
    SeedPickingReadbackSlot(device, pickingBuffer, pickingBufferSize,
                            /*slot=*/0u, poisonStableEntityId, poisonEncoded);

    // Simulate a device-lost / swapchain-rebuild recovery: any in-flight
    // pending pick is marked invalidated by
    // `RebuildOperationalResources()` so the upcoming drain publishes
    // NoHit rather than the now-untrusted pre-rebuild bytes. The buffer
    // itself survives the rebuild byte-identical (Slice D.1 invariant) so
    // the drain *can* read those bytes — it just refuses to trust them.
    ASSERT_TRUE(renderer->RebuildOperationalResources(device));
    // The buffer survives the rebuild byte-identical when frames-in-flight
    // is unchanged, so the same handle is still valid.
    ASSERT_EQ(renderer->GetPickingReadbackBuffer().Index, pickingBuffer.Index);
    ASSERT_EQ(renderer->GetPickingReadbackBuffer().Generation, pickingBuffer.Generation);

    device.NextFrame.FrameIndex = 1u;
    Extrinsic::RHI::FrameHandle nextFrame{};
    ASSERT_TRUE(renderer->BeginFrame(nextFrame));

    const Extrinsic::Graphics::SelectionSystem& selection = renderer->GetSelectionSystem();
    const std::optional<Extrinsic::Graphics::PickReadbackResult> last = selection.GetLastPickResult();
    ASSERT_TRUE(last.has_value())
        << "Drain must publish a NoHit result for an invalidated slot.";
    EXPECT_FALSE(last->Hit)
        << "Invalidated slot must publish NoHit even when the slot bytes "
           "would otherwise decode to a hit.";
    EXPECT_EQ(last->StableEntityId, 0u);
    EXPECT_EQ(last->EncodedId.Value, 0u);

    const Extrinsic::Graphics::SelectionSystemDiagnostics diag = selection.GetDiagnostics();
    EXPECT_EQ(diag.PickHitCount, 0u);
    EXPECT_EQ(diag.PickNoHitCount, 1u);

    renderer->Shutdown();
}

// GRAPHICS-074 Slice D.3 — when `RebuildOperationalResources()` shrinks
// the frames-in-flight count (e.g. a swapchain rebuild demotes the device
// from triple- to double-buffered), slot indices `>= newSlotCount` are
// truncated from the per-slot picking metadata arrays. Any pending
// readback in that tail must be *resolved* with `PublishNoHit()` before
// the truncation, otherwise the SelectionSystem keeps its `PendingPick`
// visible to the runtime/editor forever (the new slot indexing addresses
// a strictly smaller range, so the dropped slots can never be drained
// naturally).
TEST(RendererFrameLifecycle, PickingReadbackPublishesNoHitForTruncatedSlotOnFifShrink)
{
    Extrinsic::Tests::MockDevice device;
    device.Operational = true;
    device.BackbufferHandle = Extrinsic::RHI::TextureHandle{300u, 1u};
    // Triple-buffered initially so frame 2 routes to slot index 2 (which
    // the shrink-to-FIF=2 rebuild below will drop).
    device.FramesInFlight = 3u;
    device.NextFrame.FrameIndex = 0u;

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer = Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);

    const Extrinsic::RHI::BufferHandle pickingBuffer = renderer->GetPickingReadbackBuffer();
    ASSERT_TRUE(pickingBuffer.IsValid());
    ASSERT_EQ(renderer->GetPickingReadbackBufferSize(),
              static_cast<std::uint64_t>(16u) * 3u);

    // Step the device's `FrameIndex` to 2 so the executor populates the
    // tail slot (slot 2) inside `m_PickingSlot*`. Run BeginFrame +
    // ExtractRenderWorld + ExecuteFrame + EndFrame once to issue a
    // copy that flags slot 2 as `Pending=true, IssuedFrame=2`.
    device.NextFrame.FrameIndex = 2u;
    Extrinsic::RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));
    ASSERT_EQ(frame.FrameIndex, 2u);

    const Extrinsic::Graphics::RenderFrameInput input{
        .Viewport = {.Width = 320, .Height = 240},
        .HasPendingPick = true,
        .Pick = Extrinsic::Graphics::PickPixelRequest{.X = 5u, .Y = 9u, .Pending = true},
    };
    Extrinsic::Graphics::RenderWorld world = renderer->ExtractRenderWorld(input);
    ASSERT_TRUE(world.PickRequest.Pending);

    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(frame, world);
    ASSERT_EQ(renderer->GetLastRenderGraphStats().PickingReadbackCopyCount, 1u);

    [[maybe_unused]] const std::uint64_t completedFrame = renderer->EndFrame(frame);

    // The pre-rebuild SelectionSystem state must have no resolved pick
    // yet — the drain only runs at the *next* BeginFrame, which we never
    // reach because the FIF-shrink rebuild fires first.
    {
        const Extrinsic::Graphics::SelectionSystem& preRebuildSelection = renderer->GetSelectionSystem();
        EXPECT_FALSE(preRebuildSelection.GetLastPickResult().has_value());
    }

    // Simulate the swapchain demoting the device from triple- to
    // double-buffered. `RebuildOperationalResources()` reallocates the
    // buffer (size shrinks to two slots) and truncates the per-slot
    // bookkeeping to 2 entries. Slot 2 was `Pending=true` — without the
    // truncation-time NoHit publish, that pending readback would leak
    // silently and the SelectionSystem would keep showing the pre-rebuild
    // pending pick to consumers.
    device.FramesInFlight = 2u;
    ASSERT_TRUE(renderer->RebuildOperationalResources(device));
    ASSERT_EQ(renderer->GetPickingReadbackBufferSize(),
              static_cast<std::uint64_t>(16u) * 2u);

    const Extrinsic::Graphics::SelectionSystem& selection = renderer->GetSelectionSystem();
    const std::optional<Extrinsic::Graphics::PickReadbackResult> last = selection.GetLastPickResult();
    ASSERT_TRUE(last.has_value())
        << "Truncated pending slot must publish NoHit during the rebuild "
           "so SelectionSystem state matches the new slot indexing.";
    EXPECT_FALSE(last->Hit);
    EXPECT_EQ(last->StableEntityId, 0u);
    EXPECT_EQ(last->EncodedId.Value, 0u);

    const Extrinsic::Graphics::SelectionSystemDiagnostics diag = selection.GetDiagnostics();
    EXPECT_EQ(diag.PickHitCount, 0u);
    EXPECT_EQ(diag.PickNoHitCount, 1u);

    renderer->Shutdown();
}

// ---------------------------------------------------------------------------
// GRAPHICS-075 Slice E.2 — renderer-owned host-visible `Histogram.Readback`
// buffer lifecycle. The operational publisher allocates the buffer the first
// time `InitializeOperationalPassResources()` runs (1024 bytes per in-flight
// frame slot — 256 uint32 bins per slot) and intentionally does not
// re-allocate it across `RebuildOperationalResources()` calls when
// `device.GetFramesInFlight()` is unchanged, so the handle the recipe imports
// stays byte-identical (same pattern picking follows).
// ---------------------------------------------------------------------------

TEST(RendererFrameLifecycle, HistogramReadbackBufferSurvivesOperationalRebuild)
{
    Extrinsic::Tests::MockDevice device;
    device.Operational = true;
    device.BackbufferHandle = Extrinsic::RHI::TextureHandle{401u, 1u};

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer = Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);

    const Extrinsic::RHI::BufferHandle initialBuffer = renderer->GetHistogramReadbackBuffer();
    EXPECT_TRUE(initialBuffer.IsValid());

    // Size = 1024 bytes per in-flight frame slot (256 uint32 bins).
    // `MockDevice::GetFramesInFlight()` defaults to 2, so the allocation
    // must be 2048 bytes.
    const std::uint64_t initialSize = renderer->GetHistogramReadbackBufferSize();
    EXPECT_EQ(initialSize, 1024ull *
                               static_cast<std::uint64_t>(device.GetFramesInFlight()));

    EXPECT_TRUE(renderer->RebuildOperationalResources(device));

    // Same handle (so the recipe import stays stable across rebuilds) and
    // same size.
    const Extrinsic::RHI::BufferHandle rebuiltBuffer = renderer->GetHistogramReadbackBuffer();
    EXPECT_TRUE(rebuiltBuffer.IsValid());
    EXPECT_EQ(rebuiltBuffer.Index, initialBuffer.Index);
    EXPECT_EQ(rebuiltBuffer.Generation, initialBuffer.Generation);
    EXPECT_EQ(renderer->GetHistogramReadbackBufferSize(), initialSize);

    renderer->Shutdown();

    // After `Shutdown()` the lease is released so a later `Initialize()`
    // would allocate against the new BufferManager rather than handing out
    // a dangling handle.
    EXPECT_FALSE(renderer->GetHistogramReadbackBuffer().IsValid());
    EXPECT_EQ(renderer->GetHistogramReadbackBufferSize(), 0u);
}

// GRAPHICS-075 Slice E.2 — when the histogram stage is *live* on an
// operational frame, the executor must also record the per-frame
// `CopyBuffer(PostProcess.Histogram → Histogram.Readback @ slot)` after
// the compute dispatch. The CPU-observable contract is the
// `HistogramReadbackCopyCount` stat counter on `RenderGraphFrameStats`
// (matching `PickingReadbackCopyCount`). The copy is gated on
// `IsStageEnabled(Histogram)` so the structurally-recorded-no-op path
// (helper returns `Recorded` with the body short-circuited because
// `EnableHistogram == false`) cannot publish stale transient bytes;
// `HistogramReadbackCopySkippedWhenStageDisabled` pins that gate from
// the other side.
TEST(RendererFrameLifecycle, HistogramReadbackCopyRecordedOnOperationalFrame)
{
    Extrinsic::Tests::MockDevice device;
    device.Operational = true;
    device.BackbufferHandle = Extrinsic::RHI::TextureHandle{402u, 1u};

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer = Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);

    const Extrinsic::RHI::BufferHandle readbackBuffer = renderer->GetHistogramReadbackBuffer();
    ASSERT_TRUE(readbackBuffer.IsValid());

    // Enable the histogram stage so the dispatch + readback copy actually
    // run. With the default `EnableHistogram == false` the pass body
    // short-circuits and the executor must (and does — see
    // `HistogramReadbackCopySkippedWhenStageDisabled`) skip the copy.
    Extrinsic::Graphics::PostProcessSettings settings = renderer->GetPostProcessSystem().GetSettings();
    settings.EnableHistogram = true;
    renderer->GetPostProcessSystem().SetSettings(settings);

    Extrinsic::RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));

    const Extrinsic::Graphics::RenderFrameInput input{
        .Viewport = {.Width = 320, .Height = 240},
    };
    Extrinsic::Graphics::RenderWorld world = renderer->ExtractRenderWorld(input);

    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(frame, world);

    const Extrinsic::Graphics::RenderGraphFrameStats& stats = renderer->GetLastRenderGraphStats();
    EXPECT_TRUE(stats.Compile.Succeeded) << stats.Diagnostic;
    EXPECT_TRUE(stats.Execute.Succeeded) << stats.Diagnostic;
    EXPECT_TRUE(stats.Execute.DeviceOperational);

    EXPECT_EQ(stats.HistogramReadbackCopyCount, 1u)
        << "Histogram-readback copy must record exactly once per operational frame.";

    // The dispatch is bracketed by `ShaderWrite → TransferRead → ShaderWrite`
    // buffer barriers on the per-frame `PostProcess.Histogram` handle so
    // the atomic accumulations are visible to the copy.
    std::uint32_t shaderWriteToTransferRead = 0u;
    std::uint32_t transferReadToShaderWrite = 0u;
    for (const auto& barrier : device.CommandContext.BufferBarrierCalls)
    {
        if (barrier.Before == Extrinsic::RHI::MemoryAccess::ShaderWrite &&
            barrier.After == Extrinsic::RHI::MemoryAccess::TransferRead)
        {
            ++shaderWriteToTransferRead;
        }
        else if (barrier.Before == Extrinsic::RHI::MemoryAccess::TransferRead &&
                 barrier.After == Extrinsic::RHI::MemoryAccess::ShaderWrite)
        {
            ++transferReadToShaderWrite;
        }
    }
    EXPECT_GE(shaderWriteToTransferRead, 1u)
        << "Histogram readback must record ShaderWrite → TransferRead barrier "
        << "before the copy.";
    EXPECT_GE(transferReadToShaderWrite, 1u)
        << "Histogram readback must restore the histogram buffer to ShaderWrite "
        << "after the copy.";

    renderer->Shutdown();
}

// GRAPHICS-075 Slice E.2 — the `BeginFrame()`-side drain decodes the
// per-slot `Histogram.Readback` bytes and forwards them to
// `PostProcessSystem::PublishHistogramReadback(...)` once the issuing
// frame's `GlobalFrameNumber` has advanced. `MockCommandContext::CopyBuffer(...)`
// is a no-op, so this test seeds the renderer-owned host-visible buffer via
// `MockDevice::BufferContents[handle.Index]` to simulate the bytes the
// histogram dispatch would have copied into slot 0.
TEST(RendererFrameLifecycle, HistogramReadbackDrainPublishesEachSlotExactlyOnce)
{
    Extrinsic::Tests::MockDevice device;
    device.Operational = true;
    device.BackbufferHandle = Extrinsic::RHI::TextureHandle{403u, 1u};

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer = Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);

    const Extrinsic::RHI::BufferHandle readbackBuffer = renderer->GetHistogramReadbackBuffer();
    ASSERT_TRUE(readbackBuffer.IsValid());
    const std::uint64_t readbackBufferSize = renderer->GetHistogramReadbackBufferSize();
    ASSERT_GE(readbackBufferSize, 1024u);

    // Enable the histogram stage so the executor records the copy + slot
    // metadata the drain keys off (see
    // `HistogramReadbackCopySkippedWhenStageDisabled` for the negative
    // gate).
    {
        Extrinsic::Graphics::PostProcessSettings settings = renderer->GetPostProcessSystem().GetSettings();
        settings.EnableHistogram = true;
        renderer->GetPostProcessSystem().SetSettings(settings);
    }

    // Frame 0 — record the copy. The executor populates slot-0 metadata
    // (`Pending=true`, `IssuedFrame=0`).
    Extrinsic::RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));

    const Extrinsic::Graphics::RenderFrameInput input{
        .Viewport = {.Width = 320, .Height = 240},
    };
    Extrinsic::Graphics::RenderWorld world = renderer->ExtractRenderWorld(input);

    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(frame, world);
    ASSERT_EQ(renderer->GetLastRenderGraphStats().HistogramReadbackCopyCount, 1u);

    [[maybe_unused]] const std::uint64_t completedFrame = renderer->EndFrame(frame);
    ASSERT_GE(device.GlobalFrameNumber, 1u);

    // Seed slot 0 with a uniform 256-bin payload so the decoded average
    // log luminance lands at the midpoint of the `[-10, +10]` range.
    {
        std::vector<std::byte>& contents = device.BufferContents[readbackBuffer.Index];
        contents.assign(static_cast<std::size_t>(readbackBufferSize), std::byte{0});
        std::uint32_t uniformBin = 1u;
        for (std::size_t i = 0; i < 256u; ++i)
        {
            std::memcpy(contents.data() + i * sizeof(std::uint32_t),
                        &uniformBin, sizeof(uniformBin));
        }
    }

    ASSERT_EQ(renderer->GetPostProcessSystem().GetHistogramPublishCount(), 0u);

    // Frame 1 — the drain runs at the top of `BeginFrame()`. Slot 0 has
    // `IssuedFrame=0 < GlobalFrameNumber=1`, so the drain reads the bytes
    // we seeded and forwards them to `PublishHistogramReadback(...)`.
    device.NextFrame.FrameIndex = 1u;
    Extrinsic::RHI::FrameHandle nextFrame{};
    ASSERT_TRUE(renderer->BeginFrame(nextFrame));

    EXPECT_EQ(renderer->GetPostProcessSystem().GetHistogramPublishCount(), 1u)
        << "Drain must invoke PublishHistogramReadback exactly once for the completed slot.";

    // Frame 2 — drain runs again, but slot 0 is no longer pending (was
    // consumed) and no new copy has been recorded yet (we never called
    // ExecuteFrame on this frame). Publish count must stay at 1.
    device.NextFrame.FrameIndex = 2u;
    Extrinsic::RHI::FrameHandle thirdFrame{};
    ASSERT_TRUE(renderer->BeginFrame(thirdFrame));
    EXPECT_EQ(renderer->GetPostProcessSystem().GetHistogramPublishCount(), 1u)
        << "Each completed slot must publish at most once; the second drain must be a no-op.";

    renderer->Shutdown();
}

// GRAPHICS-075 Slice E.2 — when the histogram stage is *not* live (default
// `PostProcessSettings::EnableHistogram == false`), the helper still
// returns `Recorded` under the structurally-recorded-no-op taxonomy bloom /
// FXAA / SMAA also follow — the pass body early-returns without
// dispatching, so the transient `PostProcess.Histogram` buffer is never
// zero-filled or atomically populated this frame. The executor must
// therefore skip the post-dispatch readback copy (and the associated slot
// metadata) so the next drain does not publish undefined transient bytes
// into the exposure-history mirror.
TEST(RendererFrameLifecycle, HistogramReadbackCopySkippedWhenStageDisabled)
{
    Extrinsic::Tests::MockDevice device;
    device.Operational = true;
    device.BackbufferHandle = Extrinsic::RHI::TextureHandle{405u, 1u};

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer = Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);

    // Sanity: default settings keep the histogram stage off.
    ASSERT_FALSE(renderer->GetPostProcessSystem().GetSettings().EnableHistogram);

    Extrinsic::RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));

    const Extrinsic::Graphics::RenderFrameInput input{
        .Viewport = {.Width = 320, .Height = 240},
    };
    Extrinsic::Graphics::RenderWorld world = renderer->ExtractRenderWorld(input);
    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(frame, world);

    const Extrinsic::Graphics::RenderGraphFrameStats& stats = renderer->GetLastRenderGraphStats();
    EXPECT_TRUE(stats.Execute.Succeeded) << stats.Diagnostic;

    // Histogram pass still routes as `Recorded` per the
    // structurally-recorded-no-op taxonomy.
    const Extrinsic::Graphics::RenderGraphCommandPassStats* histogramPass =
        FindCommandPass(stats, "PostProcessHistogramPass");
    ASSERT_NE(histogramPass, nullptr);
    EXPECT_EQ(histogramPass->Status, Extrinsic::Graphics::RenderCommandPassStatus::Recorded);

    // But no readback copy is recorded — the per-frame counter stays zero.
    EXPECT_EQ(stats.HistogramReadbackCopyCount, 0u)
        << "Disabled-stage frames must not record the readback copy, since the "
        << "transient PostProcess.Histogram buffer was never zero-filled or "
        << "populated this frame.";

    [[maybe_unused]] const std::uint64_t completedFrame = renderer->EndFrame(frame);

    // No slot was marked pending, so the next BeginFrame() drain must be a
    // no-op (publish counter stays at zero).
    device.NextFrame.FrameIndex = 1u;
    Extrinsic::RHI::FrameHandle nextFrame{};
    ASSERT_TRUE(renderer->BeginFrame(nextFrame));
    EXPECT_EQ(renderer->GetPostProcessSystem().GetHistogramPublishCount(), 0u)
        << "Disabled-stage frames must leave the drain idle so PublishHistogramReadback "
        << "is never invoked with undefined transient bytes.";

    renderer->Shutdown();
}

// GRAPHICS-075 Slice E.2 — `PublishHistogramReadback(...)` must update the
// retained `PostProcessExposureHistory` CPU mirror. The first publish snaps
// directly to the observed average log luminance so the history is not
// anchored to the default-constructed `0.0` on startup. Subsequent publishes
// blend the freshly observed value into the retained mirror through the
// one-pole IIR with the canonical adaptation velocity.
TEST(RendererFrameLifecycle, PublishHistogramReadbackUpdatesExposureHistory)
{
    Extrinsic::Tests::MockDevice device;
    device.Operational = true;
    device.BackbufferHandle = Extrinsic::RHI::TextureHandle{404u, 1u};

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer = Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);

    Extrinsic::Graphics::PostProcessSystem& post = renderer->GetPostProcessSystem();

    // Sanity: default snapshot is zero-initialised.
    const Extrinsic::Graphics::PostProcessExposureHistory before = post.GetExposureHistorySnapshot();
    EXPECT_FLOAT_EQ(before.PreviousAverageLogLum, 0.0f);
    EXPECT_FLOAT_EQ(before.AdaptationVelocity, 0.0f);
    EXPECT_EQ(before.FrameIndex, 0u);

    // Uniform payload — count-weighted bin centre lands at the midpoint
    // (`(-10 + 10) / 2 = 0`). The first publish snaps directly to the
    // observed value.
    std::array<std::uint32_t, 256> uniformBins{};
    for (std::uint32_t& bin : uniformBins) { bin = 1u; }
    post.PublishHistogramReadback(
        std::span<const std::uint32_t>{uniformBins.data(), uniformBins.size()},
        /*frameIndex=*/7u,
        &device);

    const Extrinsic::Graphics::PostProcessExposureHistory afterFirst = post.GetExposureHistorySnapshot();
    EXPECT_NEAR(afterFirst.PreviousAverageLogLum, 0.0f, 1e-5f);
    EXPECT_GT(afterFirst.AdaptationVelocity, 0.0f);
    EXPECT_EQ(afterFirst.FrameIndex, 7u);
    EXPECT_EQ(post.GetHistogramPublishCount(), 1u);

    // Heavily-skewed payload: all samples in bin 0. The decoded average
    // log luminance lands near `kHistogramMinLogLum + half-step ≈ -9.96`.
    // The second publish blends this observation into the previous 0.0
    // mirror through the one-pole IIR — the new mirror must be strictly
    // negative but well above the raw observation (the previous-frame
    // contribution is 5%).
    std::array<std::uint32_t, 256> bin0Bins{};
    bin0Bins[0] = 1024u;
    post.PublishHistogramReadback(
        std::span<const std::uint32_t>{bin0Bins.data(), bin0Bins.size()},
        /*frameIndex=*/8u,
        &device);

    const Extrinsic::Graphics::PostProcessExposureHistory afterSecond = post.GetExposureHistorySnapshot();
    EXPECT_LT(afterSecond.PreviousAverageLogLum, 0.0f);
    EXPECT_GT(afterSecond.PreviousAverageLogLum, -10.0f);
    EXPECT_EQ(afterSecond.FrameIndex, 8u);
    EXPECT_EQ(post.GetHistogramPublishCount(), 2u);

    // Rejected payload (wrong bin count) must leave the mirror untouched
    // and must not increment the publish counter.
    std::array<std::uint32_t, 16> tooFewBins{};
    post.PublishHistogramReadback(
        std::span<const std::uint32_t>{tooFewBins.data(), tooFewBins.size()},
        /*frameIndex=*/9u,
        &device);
    const Extrinsic::Graphics::PostProcessExposureHistory afterReject = post.GetExposureHistorySnapshot();
    EXPECT_EQ(post.GetHistogramPublishCount(), 2u);
    EXPECT_FLOAT_EQ(afterReject.PreviousAverageLogLum, afterSecond.PreviousAverageLogLum);
    EXPECT_EQ(afterReject.FrameIndex, afterSecond.FrameIndex);

    renderer->Shutdown();
}

TEST(RendererFrameLifecycle,
     NativeGpuProfilerSubmitsExactCompiledPlanAndPublishesOnlyRecordedPasses)
{
    NativeTimestampProfiler profiler;
    Extrinsic::Tests::MockDevice device;
    device.FramesInFlight = 1u;
    device.NextFrame =
        Extrinsic::RHI::FrameHandle{
            .FrameIndex = 0u,
            .SwapchainImageIndex = 0u,
        };
    device.BackbufferHandle =
        Extrinsic::RHI::TextureHandle{901u, 1u};
    device.Profiler = &profiler;

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer =
        Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);

    Extrinsic::RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));
    const Extrinsic::Graphics::RenderFrameInput input{
        .Viewport = {.Width = 320u, .Height = 240u},
        .EnableGpuProfiling = true,
    };
    Extrinsic::Graphics::RenderWorld world =
        renderer->ExtractRenderWorld(input);
    EXPECT_TRUE(world.EnableGpuProfiling);
    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(frame, world);

    const Extrinsic::Graphics::RenderGraphFrameStats recordedStats =
        renderer->GetLastRenderGraphStats();
    ASSERT_TRUE(recordedStats.Compile.Succeeded)
        << recordedStats.Diagnostic;
    ASSERT_TRUE(recordedStats.Execute.Succeeded)
        << recordedStats.Diagnostic;
    EXPECT_EQ(recordedStats.GpuProfile.Status,
              Extrinsic::Graphics::RenderGraphGpuProfileStatus::Recording);

    ASSERT_FALSE(profiler.PlannedScopes.empty());
    EXPECT_EQ(profiler.BeginScopeAttempts.size(),
              profiler.PlannedScopes.size());
    EXPECT_EQ(profiler.EndScopeCalls.size(),
              profiler.PlannedScopes.size());
    ASSERT_FALSE(profiler.Events.empty());
    EXPECT_EQ(profiler.Events.front().Kind,
              NativeTimestampProfiler::EventKind::BeginQueue);
    EXPECT_EQ(profiler.Events.front().Queue,
              Extrinsic::RHI::QueueAffinity::Graphics);
    EXPECT_EQ(profiler.Events.back().Kind,
              NativeTimestampProfiler::EventKind::EndQueue);
    EXPECT_EQ(profiler.Events.back().Queue,
              Extrinsic::RHI::QueueAffinity::Graphics);
    for (std::uint32_t scopeIndex = 0u;
         scopeIndex < profiler.PlannedScopes.size();
         ++scopeIndex)
    {
        const auto matchesScope =
            [scopeIndex](const Extrinsic::RHI::ProfilerScopeToken token)
            {
                return token.ScopeIndex == scopeIndex;
            };
        EXPECT_EQ(std::ranges::count_if(
                      profiler.BeginScopeAttempts,
                      matchesScope),
                  1);
        EXPECT_EQ(std::ranges::count_if(
                      profiler.EndScopeCalls,
                      matchesScope),
                  1);
        EXPECT_EQ(profiler.PlannedScopes[scopeIndex].Queue,
                  Extrinsic::RHI::QueueAffinity::Graphics);
    }

    EXPECT_EQ(renderer->EndFrame(frame), 1u);
    ASSERT_EQ(profiler.EndFrameCalls.size(), 1u);
    EXPECT_EQ(
        profiler.EndFrameCalls.front().Disposition,
        Extrinsic::RHI::ProfilerFrameDisposition::Submitted);
    EXPECT_EQ(renderer->GetLastRenderGraphStats().GpuProfile.Status,
              Extrinsic::Graphics::RenderGraphGpuProfileStatus::Submitted);

    device.NextFrame.FrameIndex = 0u;
    Extrinsic::RHI::FrameHandle reusedFrame{};
    ASSERT_TRUE(renderer->BeginFrame(reusedFrame));
    const auto& resolved =
        renderer->GetLastRenderGraphStats().GpuProfile;
    EXPECT_EQ(resolved.Status,
              Extrinsic::Graphics::RenderGraphGpuProfileStatus::Resolved);
    EXPECT_EQ(resolved.Source,
              Extrinsic::RHI::GpuTimestampSource::NativeGpu);
    EXPECT_TRUE(resolved.Fresh);
    EXPECT_FALSE(resolved.Stale);
    EXPECT_TRUE(resolved.HasResolvedFrame);
    EXPECT_EQ(resolved.ResolvedSubmittedFrameNumber, 0u);
    EXPECT_EQ(resolved.ResolvedFrameSlot, 0u);
    EXPECT_EQ(resolved.SampleAgeFrames, 1u);
    ASSERT_EQ(resolved.Passes.size(), profiler.PlannedScopes.size());

    std::size_t skippedProfileRows = 0u;
    std::size_t recordedProfileRows = 0u;
    for (const auto& profilePass : resolved.Passes)
    {
        bool exactRecorded = false;
        bool exactSkipped = false;
        for (const auto& commandPass :
             recordedStats.CommandRecords.Passes)
        {
            if (commandPass.Id != profilePass.Id ||
                commandPass.Name != profilePass.Name)
            {
                continue;
            }
            exactRecorded =
                exactRecorded ||
                commandPass.Status ==
                    Extrinsic::Graphics::
                        RenderCommandPassStatus::Recorded;
            exactSkipped =
                exactSkipped ||
                commandPass.Status !=
                    Extrinsic::Graphics::
                        RenderCommandPassStatus::Recorded;
        }

        if (exactRecorded)
        {
            ++recordedProfileRows;
            EXPECT_EQ(profilePass.CommandStatus,
                      Extrinsic::Graphics::
                          RenderCommandPassStatus::Recorded);
            EXPECT_EQ(profilePass.Source,
                      Extrinsic::RHI::
                          GpuTimestampSource::NativeGpu);
            EXPECT_TRUE(profilePass.DurationNs.has_value());
        }
        else
        {
            ++skippedProfileRows;
            EXPECT_TRUE(exactSkipped);
            EXPECT_NE(profilePass.CommandStatus,
                      Extrinsic::Graphics::
                          RenderCommandPassStatus::Recorded);
            EXPECT_EQ(profilePass.Source,
                      Extrinsic::RHI::
                          GpuTimestampSource::Unavailable);
            EXPECT_FALSE(profilePass.DurationNs.has_value());
        }
    }
    EXPECT_GT(recordedProfileRows, 0u);
    EXPECT_GT(skippedProfileRows, 0u);

    const auto& telemetry =
        Extrinsic::Core::Telemetry::TelemetrySystem::Get()
            .GetPassTimings();
    EXPECT_EQ(telemetry.size(), recordedProfileRows);
    for (const auto& timing : telemetry)
    {
        const auto profileIt = std::ranges::find_if(
            resolved.Passes,
            [&timing](const auto& profilePass)
            {
                return profilePass.Name == timing.Name;
            });
        ASSERT_NE(profileIt, resolved.Passes.end());
        EXPECT_EQ(profileIt->CommandStatus,
                  Extrinsic::Graphics::
                      RenderCommandPassStatus::Recorded);
        EXPECT_EQ(profileIt->Source,
                  Extrinsic::RHI::GpuTimestampSource::NativeGpu);
        ASSERT_TRUE(profileIt->DurationNs.has_value());
        EXPECT_EQ(timing.GpuTimeNs, *profileIt->DurationNs);
    }

    world = renderer->ExtractRenderWorld(input);
    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(reusedFrame, world);
    EXPECT_EQ(renderer->EndFrame(reusedFrame), 2u);
    profiler.ResolveFailure =
        Extrinsic::RHI::ProfilerError::NotReady;

    Extrinsic::RHI::FrameHandle notReadyFrame{};
    ASSERT_TRUE(renderer->BeginFrame(notReadyFrame));
    const auto& stale =
        renderer->GetLastRenderGraphStats().GpuProfile;
    EXPECT_EQ(stale.Status,
              Extrinsic::Graphics::
                  RenderGraphGpuProfileStatus::NotReady);
    EXPECT_FALSE(stale.Fresh);
    EXPECT_TRUE(stale.Stale);
    EXPECT_TRUE(stale.HasResolvedFrame);
    EXPECT_EQ(stale.ResolvedSubmittedFrameNumber, 0u);
    EXPECT_EQ(stale.ResolvedFrameSlot, 0u);
    EXPECT_EQ(stale.SampleAgeFrames, 2u);
    EXPECT_TRUE(
        Extrinsic::Core::Telemetry::TelemetrySystem::Get()
            .GetPassTimings()
            .empty());

    renderer->Shutdown();
}

TEST(RendererFrameLifecycle,
     NativeGpuProfilerUsesAcceptedParallelMultiQueueAttribution)
{
    SchedulerScope scheduler{4u};
    NativeTimestampProfiler profiler;
    Extrinsic::Tests::MockDevice device;
    device.FramesInFlight = 1u;
    device.BackbufferHandle =
        Extrinsic::RHI::TextureHandle{902u, 1u};
    device.AsyncComputeQueueAvailable = true;
    device.AcceptQueueSubmitPlans = true;
    device.ParallelCommandContextsAvailable = true;
    device.AcceptParallelCommandContextPlans = true;
    device.Profiler = &profiler;

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer =
        Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);
    renderer->SetParallelRenderGraphRecordingEnabled(true);

    Extrinsic::RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));
    const Extrinsic::Graphics::RenderFrameInput input{
        .Viewport = {.Width = 320u, .Height = 240u},
        .EnableGpuProfiling = true,
    };
    Extrinsic::Graphics::RenderWorld world =
        renderer->ExtractRenderWorld(input);
    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(frame, world);

    const auto& stats = renderer->GetLastRenderGraphStats();
    ASSERT_TRUE(stats.Execute.Succeeded) << stats.Diagnostic;
    ASSERT_TRUE(stats.Execute.ParallelRecordingAccepted);
    ASSERT_EQ(stats.AsyncComputeUtilizedFrames, 1u);
    ASSERT_FALSE(profiler.PlannedScopes.empty());
    EXPECT_EQ(profiler.BeginScopeAttempts.size(),
              profiler.PlannedScopes.size());
    EXPECT_EQ(profiler.EndScopeCalls.size(),
              profiler.PlannedScopes.size());

    for (const auto& request :
         device.RecordedParallelCommandContextPlan)
    {
        const auto scopeIt = std::ranges::find_if(
            profiler.PlannedScopes,
            [&request](const auto& scope)
            {
                return scope.Ordinal == request.PassIndex;
            });
        ASSERT_NE(scopeIt, profiler.PlannedScopes.end());
        EXPECT_EQ(scopeIt->Queue, request.Queue);
    }

    for (const Extrinsic::RHI::QueueAffinity queue :
         {Extrinsic::RHI::QueueAffinity::Graphics,
          Extrinsic::RHI::QueueAffinity::AsyncCompute})
    {
        auto queueEvents = std::views::filter(
            profiler.Events,
            [queue](const auto& event)
            {
                return event.Queue == queue;
            });
        std::vector<NativeTimestampProfiler::Event> events{
            queueEvents.begin(),
            queueEvents.end(),
        };
        ASSERT_FALSE(events.empty());
        EXPECT_EQ(events.front().Kind,
                  NativeTimestampProfiler::EventKind::BeginQueue);
        EXPECT_EQ(events.back().Kind,
                  NativeTimestampProfiler::EventKind::EndQueue);
        EXPECT_EQ(std::ranges::count_if(
                      events,
                      [](const auto& event)
                      {
                          return event.Kind ==
                              NativeTimestampProfiler::EventKind::
                                  BeginQueue;
                      }),
                  1);
        EXPECT_EQ(std::ranges::count_if(
                      events,
                      [](const auto& event)
                      {
                          return event.Kind ==
                              NativeTimestampProfiler::EventKind::
                                  EndQueue;
                      }),
                  1);
    }

    EXPECT_EQ(renderer->EndFrame(frame), 1u);
    ASSERT_EQ(profiler.EndFrameCalls.size(), 1u);
    EXPECT_EQ(
        profiler.EndFrameCalls.front().Disposition,
        Extrinsic::RHI::ProfilerFrameDisposition::Submitted);

    device.NextFrame.FrameIndex = 0u;
    Extrinsic::RHI::FrameHandle reusedFrame{};
    ASSERT_TRUE(renderer->BeginFrame(reusedFrame));
    const auto& resolved =
        renderer->GetLastRenderGraphStats().GpuProfile;
    ASSERT_EQ(resolved.Status,
              Extrinsic::Graphics::
                  RenderGraphGpuProfileStatus::Resolved);
    ASSERT_EQ(resolved.QueueEnvelopes.size(), 2u);
    for (const Extrinsic::RHI::QueueAffinity queue :
         {Extrinsic::RHI::QueueAffinity::Graphics,
          Extrinsic::RHI::QueueAffinity::AsyncCompute})
    {
        const auto envelope = std::ranges::find_if(
            resolved.QueueEnvelopes,
            [queue](const auto& candidate)
            {
                return candidate.Queue == queue;
            });
        ASSERT_NE(envelope, resolved.QueueEnvelopes.end());
        EXPECT_EQ(envelope->Source,
                  Extrinsic::RHI::GpuTimestampSource::NativeGpu);
        EXPECT_TRUE(envelope->DurationNs.has_value());
    }
    EXPECT_TRUE(std::ranges::any_of(
        resolved.Passes,
        [](const auto& pass)
        {
            return pass.Queue ==
                Extrinsic::RHI::QueueAffinity::AsyncCompute;
        }));
    renderer->Shutdown();
}

TEST(RendererFrameLifecycle,
     NativeGpuProfilerLabelsRejectedAsyncPlanAsGraphicsFallback)
{
    NativeTimestampProfiler profiler;
    Extrinsic::Tests::MockDevice device;
    device.FramesInFlight = 1u;
    device.BackbufferHandle =
        Extrinsic::RHI::TextureHandle{905u, 1u};
    device.AsyncComputeQueueAvailable = true;
    device.AcceptQueueSubmitPlans = false;
    device.Profiler = &profiler;

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer =
        Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);

    Extrinsic::RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));
    const Extrinsic::Graphics::RenderFrameInput input{
        .Viewport = {.Width = 320u, .Height = 240u},
        .EnableGpuProfiling = true,
    };
    Extrinsic::Graphics::RenderWorld world =
        renderer->ExtractRenderWorld(input);
    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(frame, world);

    ASSERT_TRUE(
        renderer->GetLastRenderGraphStats().Execute.Succeeded);
    ASSERT_FALSE(profiler.PlannedScopes.empty());
    EXPECT_TRUE(std::ranges::all_of(
        profiler.PlannedScopes,
        [](const auto& scope)
        {
            return scope.Queue ==
                Extrinsic::RHI::QueueAffinity::Graphics;
        }));
    EXPECT_FALSE(std::ranges::any_of(
        profiler.Events,
        [](const auto& event)
        {
            return event.Queue ==
                Extrinsic::RHI::QueueAffinity::AsyncCompute;
        }));
    EXPECT_EQ(renderer->EndFrame(frame), 1u);
    ASSERT_EQ(profiler.EndFrameCalls.size(), 1u);
    EXPECT_EQ(
        profiler.EndFrameCalls.front().Disposition,
        Extrinsic::RHI::ProfilerFrameDisposition::Submitted);
    renderer->Shutdown();
}

TEST(RendererFrameLifecycle,
     NativeGpuProfilerDiscardsWhenDeviceSubmissionDoesNotAdvance)
{
    NativeTimestampProfiler profiler;
    Extrinsic::Tests::MockDevice device;
    device.FramesInFlight = 1u;
    device.BackbufferHandle =
        Extrinsic::RHI::TextureHandle{906u, 1u};
    device.AdvanceGlobalFrameOnEndFrame = false;
    device.Profiler = &profiler;

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer =
        Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);

    Extrinsic::RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));
    const Extrinsic::Graphics::RenderFrameInput input{
        .Viewport = {.Width = 320u, .Height = 240u},
        .EnableGpuProfiling = true,
    };
    Extrinsic::Graphics::RenderWorld world =
        renderer->ExtractRenderWorld(input);
    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(frame, world);
    ASSERT_TRUE(
        renderer->GetLastRenderGraphStats().Execute.Succeeded);

    EXPECT_EQ(renderer->EndFrame(frame), 0u);
    ASSERT_EQ(profiler.EndFrameCalls.size(), 1u);
    EXPECT_EQ(
        profiler.EndFrameCalls.front().Disposition,
        Extrinsic::RHI::ProfilerFrameDisposition::Discarded);
    const auto& profile =
        renderer->GetLastRenderGraphStats().GpuProfile;
    EXPECT_EQ(profile.Status,
              Extrinsic::Graphics::
                  RenderGraphGpuProfileStatus::InvalidLifecycle);
    EXPECT_FALSE(profile.Fresh);
    EXPECT_FALSE(profile.HasResolvedFrame);
    EXPECT_TRUE(
        Extrinsic::Core::Telemetry::TelemetrySystem::Get()
            .GetPassTimings()
            .empty());
    renderer->Shutdown();
}

TEST(RendererFrameLifecycle,
     DeviceLostAfterActiveProfileDeviceEndOverridesInvalidLifecycle)
{
    NativeTimestampProfiler profiler;
    Extrinsic::Tests::MockDevice device;
    device.FramesInFlight = 1u;
    device.BackbufferHandle =
        Extrinsic::RHI::TextureHandle{910u, 1u};
    device.Profiler = &profiler;

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer =
        Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);

    const Extrinsic::Graphics::RenderFrameInput input{
        .Viewport = {.Width = 320u, .Height = 240u},
        .EnableGpuProfiling = true,
    };
    Extrinsic::RHI::FrameHandle submittedFrame{};
    ASSERT_TRUE(renderer->BeginFrame(submittedFrame));
    Extrinsic::Graphics::RenderWorld world =
        renderer->ExtractRenderWorld(input);
    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(submittedFrame, world);
    ASSERT_TRUE(
        renderer->GetLastRenderGraphStats().Execute.Succeeded);
    ASSERT_EQ(renderer->EndFrame(submittedFrame), 1u);

    Extrinsic::RHI::FrameHandle reusedFrame{};
    ASSERT_TRUE(renderer->BeginFrame(reusedFrame));
    const auto resolved =
        renderer->GetLastRenderGraphStats().GpuProfile;
    ASSERT_EQ(
        resolved.Status,
        Extrinsic::Graphics::
            RenderGraphGpuProfileStatus::Resolved);
    ASSERT_TRUE(resolved.Fresh);
    ASSERT_TRUE(resolved.HasResolvedFrame);

    world = renderer->ExtractRenderWorld(input);
    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(reusedFrame, world);
    ASSERT_TRUE(
        renderer->GetLastRenderGraphStats().Execute.Succeeded);
    ASSERT_EQ(profiler.EndFrameCalls.size(), 1u);
    Extrinsic::Core::Telemetry::TelemetrySystem::Get()
        .SetPassGpuTimings({
            Extrinsic::Core::Telemetry::PassTimingEntry{
                .Name = "must-clear-on-active-end-frame-loss",
                .GpuTimeNs = 91u,
                .CpuTimeNs = 0u,
            },
        });

    device.AdvanceGlobalFrameOnEndFrame = false;
    profiler.Status = Extrinsic::RHI::ProfilerStatusSnapshot{
        .Status =
            Extrinsic::RHI::ProfilerBackendStatus::DeviceLost,
        .Source =
            Extrinsic::RHI::GpuTimestampSource::Unavailable,
        .Diagnostic = "test device lost after active device EndFrame",
    };
    EXPECT_EQ(renderer->EndFrame(reusedFrame), 1u);

    ASSERT_EQ(profiler.EndFrameCalls.size(), 2u);
    EXPECT_EQ(
        profiler.EndFrameCalls.back().Disposition,
        Extrinsic::RHI::ProfilerFrameDisposition::Discarded);
    const auto& stale =
        renderer->GetLastRenderGraphStats().GpuProfile;
    EXPECT_EQ(
        stale.Status,
        Extrinsic::Graphics::
            RenderGraphGpuProfileStatus::DeviceLost);
    EXPECT_EQ(stale.Diagnostic, profiler.Status.Diagnostic);
    EXPECT_FALSE(stale.Fresh);
    EXPECT_TRUE(stale.Stale);
    EXPECT_TRUE(stale.HasResolvedFrame);
    EXPECT_EQ(
        stale.ResolvedSubmittedFrameNumber,
        resolved.ResolvedSubmittedFrameNumber);
    EXPECT_EQ(stale.ResolvedFrameSlot, resolved.ResolvedFrameSlot);
    EXPECT_EQ(stale.Source, resolved.Source);
    EXPECT_TRUE(
        Extrinsic::Core::Telemetry::TelemetrySystem::Get()
            .GetPassTimings()
            .empty());
    renderer->Shutdown();
}

TEST(RendererFrameLifecycle,
     DeviceLostAfterHotDisabledDeviceEndOverridesDisabledProfile)
{
    NativeTimestampProfiler profiler;
    Extrinsic::Tests::MockDevice device;
    device.FramesInFlight = 1u;
    device.BackbufferHandle =
        Extrinsic::RHI::TextureHandle{911u, 1u};
    device.Profiler = &profiler;

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer =
        Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);

    const Extrinsic::Graphics::RenderFrameInput enabledInput{
        .Viewport = {.Width = 320u, .Height = 240u},
        .EnableGpuProfiling = true,
    };
    Extrinsic::RHI::FrameHandle submittedFrame{};
    ASSERT_TRUE(renderer->BeginFrame(submittedFrame));
    Extrinsic::Graphics::RenderWorld world =
        renderer->ExtractRenderWorld(enabledInput);
    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(submittedFrame, world);
    ASSERT_TRUE(
        renderer->GetLastRenderGraphStats().Execute.Succeeded);
    ASSERT_EQ(renderer->EndFrame(submittedFrame), 1u);

    Extrinsic::RHI::FrameHandle reusedFrame{};
    ASSERT_TRUE(renderer->BeginFrame(reusedFrame));
    const auto resolved =
        renderer->GetLastRenderGraphStats().GpuProfile;
    ASSERT_EQ(
        resolved.Status,
        Extrinsic::Graphics::
            RenderGraphGpuProfileStatus::Resolved);
    ASSERT_TRUE(resolved.Fresh);
    ASSERT_TRUE(resolved.HasResolvedFrame);

    const Extrinsic::Graphics::RenderFrameInput disabledInput{
        .Viewport = {.Width = 320u, .Height = 240u},
        .EnableGpuProfiling = false,
    };
    world = renderer->ExtractRenderWorld(disabledInput);
    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(reusedFrame, world);
    ASSERT_TRUE(
        renderer->GetLastRenderGraphStats().Execute.Succeeded);
    ASSERT_EQ(
        renderer->GetLastRenderGraphStats().GpuProfile.Status,
        Extrinsic::Graphics::
            RenderGraphGpuProfileStatus::Disabled);
    ASSERT_EQ(profiler.EndFrameCalls.size(), 1u);
    Extrinsic::Core::Telemetry::TelemetrySystem::Get()
        .SetPassGpuTimings({
            Extrinsic::Core::Telemetry::PassTimingEntry{
                .Name = "must-clear-on-disabled-end-frame-loss",
                .GpuTimeNs = 92u,
                .CpuTimeNs = 0u,
            },
        });

    device.AdvanceGlobalFrameOnEndFrame = false;
    profiler.Status = Extrinsic::RHI::ProfilerStatusSnapshot{
        .Status =
            Extrinsic::RHI::ProfilerBackendStatus::DeviceLost,
        .Source =
            Extrinsic::RHI::GpuTimestampSource::Unavailable,
        .Diagnostic = "test device lost after disabled device EndFrame",
    };
    EXPECT_EQ(renderer->EndFrame(reusedFrame), 1u);

    EXPECT_EQ(profiler.EndFrameCalls.size(), 1u);
    const auto& stale =
        renderer->GetLastRenderGraphStats().GpuProfile;
    EXPECT_EQ(
        stale.Status,
        Extrinsic::Graphics::
            RenderGraphGpuProfileStatus::DeviceLost);
    EXPECT_EQ(stale.Diagnostic, profiler.Status.Diagnostic);
    EXPECT_FALSE(stale.Fresh);
    EXPECT_TRUE(stale.Stale);
    EXPECT_TRUE(stale.HasResolvedFrame);
    EXPECT_EQ(
        stale.ResolvedSubmittedFrameNumber,
        resolved.ResolvedSubmittedFrameNumber);
    EXPECT_EQ(stale.ResolvedFrameSlot, resolved.ResolvedFrameSlot);
    EXPECT_EQ(stale.Source, resolved.Source);
    EXPECT_TRUE(
        Extrinsic::Core::Telemetry::TelemetrySystem::Get()
            .GetPassTimings()
            .empty());
    renderer->Shutdown();
}

TEST(RendererFrameLifecycle,
     GpuProfilerScopeFailureDoesNotAbortRenderingAndDiscardsWholeCandidate)
{
    NativeTimestampProfiler profiler;
    profiler.FailBeginScopeIndex = 0u;
    Extrinsic::Tests::MockDevice device;
    device.FramesInFlight = 1u;
    device.BackbufferHandle =
        Extrinsic::RHI::TextureHandle{903u, 1u};
    device.Profiler = &profiler;

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer =
        Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);

    Extrinsic::RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));
    const Extrinsic::Graphics::RenderFrameInput input{
        .Viewport = {.Width = 320u, .Height = 240u},
        .EnableGpuProfiling = true,
    };
    Extrinsic::Graphics::RenderWorld world =
        renderer->ExtractRenderWorld(input);
    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(frame, world);
    EXPECT_TRUE(renderer->GetLastRenderGraphStats().Execute.Succeeded);

    ASSERT_FALSE(profiler.BeginScopeAttempts.empty());
    EXPECT_EQ(profiler.BeginScopeAttempts.front().ScopeIndex, 0u);
    EXPECT_EQ(std::ranges::count_if(
                  profiler.EndScopeCalls,
                  [](const auto token)
                  {
                      return token.ScopeIndex == 0u;
                  }),
              0);

    EXPECT_EQ(renderer->EndFrame(frame), 1u);
    ASSERT_EQ(profiler.EndFrameCalls.size(), 1u);
    EXPECT_EQ(
        profiler.EndFrameCalls.front().Disposition,
        Extrinsic::RHI::ProfilerFrameDisposition::Discarded);
    const auto& profile =
        renderer->GetLastRenderGraphStats().GpuProfile;
    EXPECT_EQ(profile.Status,
              Extrinsic::Graphics::
                  RenderGraphGpuProfileStatus::InvalidLifecycle);
    EXPECT_FALSE(profile.Fresh);
    EXPECT_FALSE(profile.HasResolvedFrame);
    EXPECT_TRUE(
        Extrinsic::Core::Telemetry::TelemetrySystem::Get()
            .GetPassTimings()
            .empty());

    renderer->Shutdown();
}

TEST(RendererFrameLifecycle,
     GpuProfilerNoFreshBeginPathsClearPriorTelemetry)
{
    auto& telemetry =
        Extrinsic::Core::Telemetry::TelemetrySystem::Get();
    telemetry.SetPassGpuTimings({
        Extrinsic::Core::Telemetry::PassTimingEntry{
            .Name = "stale",
            .GpuTimeNs = 99u,
            .CpuTimeNs = 0u,
        },
    });

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer =
        Extrinsic::Graphics::CreateRenderer();
    Extrinsic::RHI::FrameHandle frame{};
    EXPECT_FALSE(renderer->BeginFrame(frame));
    EXPECT_TRUE(telemetry.GetPassTimings().empty());
    EXPECT_EQ(renderer->GetLastRenderGraphStats().GpuProfile.Status,
              Extrinsic::Graphics::
                  RenderGraphGpuProfileStatus::Unavailable);

    Extrinsic::Tests::MockDevice device;
    device.BeginFrameResult = false;
    renderer->Initialize(device);
    telemetry.SetPassGpuTimings({
        Extrinsic::Core::Telemetry::PassTimingEntry{
            .Name = "stale-again",
            .GpuTimeNs = 101u,
            .CpuTimeNs = 0u,
        },
    });
    EXPECT_FALSE(renderer->BeginFrame(frame));
    EXPECT_TRUE(telemetry.GetPassTimings().empty());
    EXPECT_EQ(renderer->GetLastRenderGraphStats().GpuProfile.Status,
              Extrinsic::Graphics::
                  RenderGraphGpuProfileStatus::Unavailable);

    device.BeginFrameResult = true;
    ASSERT_TRUE(renderer->BeginFrame(frame));
    const Extrinsic::Graphics::RenderFrameInput input{
        .Viewport = {.Width = 320u, .Height = 240u},
        .EnableGpuProfiling = true,
    };
    Extrinsic::Graphics::RenderWorld world =
        renderer->ExtractRenderWorld(input);
    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(frame, world);
    EXPECT_TRUE(renderer->GetLastRenderGraphStats().Execute.Succeeded);
    EXPECT_EQ(renderer->GetLastRenderGraphStats().GpuProfile.Status,
              Extrinsic::Graphics::
                  RenderGraphGpuProfileStatus::Unavailable);
    EXPECT_TRUE(telemetry.GetPassTimings().empty());
    renderer->Shutdown();
}

TEST(RendererFrameLifecycle,
     GpuProfilerPerFrameUnsupportedPlanDoesNotAbortRendering)
{
    NativeTimestampProfiler profiler;
    profiler.BeginFrameFailure =
        Extrinsic::RHI::ProfilerError::Unsupported;
    Extrinsic::Tests::MockDevice device;
    device.FramesInFlight = 1u;
    device.BackbufferHandle =
        Extrinsic::RHI::TextureHandle{907u, 1u};
    device.Profiler = &profiler;

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer =
        Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);

    Extrinsic::RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));
    const Extrinsic::Graphics::RenderFrameInput input{
        .Viewport = {.Width = 320u, .Height = 240u},
        .EnableGpuProfiling = true,
    };
    Extrinsic::Graphics::RenderWorld world =
        renderer->ExtractRenderWorld(input);
    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(frame, world);

    const auto& profile =
        renderer->GetLastRenderGraphStats().GpuProfile;
    EXPECT_TRUE(
        renderer->GetLastRenderGraphStats().Execute.Succeeded);
    EXPECT_EQ(
        profile.Status,
        Extrinsic::Graphics::
            RenderGraphGpuProfileStatus::Unsupported);
    EXPECT_FALSE(profile.Fresh);
    EXPECT_FALSE(profile.HasResolvedFrame);
    EXPECT_EQ(profiler.BeginFrameCalls, 1u);
    EXPECT_TRUE(profiler.EndFrameCalls.empty());
    EXPECT_TRUE(
        Extrinsic::Core::Telemetry::TelemetrySystem::Get()
            .GetPassTimings()
            .empty());
    EXPECT_EQ(renderer->EndFrame(frame), 1u);
    renderer->Shutdown();
}

TEST(RendererFrameLifecycle,
     HotDisabledGpuProfilerPreservesDeviceLostStaleLastGoodProfile)
{
    NativeTimestampProfiler profiler;
    Extrinsic::Tests::MockDevice device;
    device.FramesInFlight = 1u;
    device.BackbufferHandle =
        Extrinsic::RHI::TextureHandle{908u, 1u};
    device.Profiler = &profiler;

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer =
        Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);

    Extrinsic::RHI::FrameHandle submittedFrame{};
    ASSERT_TRUE(renderer->BeginFrame(submittedFrame));
    const Extrinsic::Graphics::RenderFrameInput input{
        .Viewport = {.Width = 320u, .Height = 240u},
        .EnableGpuProfiling = true,
    };
    Extrinsic::Graphics::RenderWorld world =
        renderer->ExtractRenderWorld(input);
    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(submittedFrame, world);
    ASSERT_TRUE(
        renderer->GetLastRenderGraphStats().Execute.Succeeded);
    ASSERT_EQ(renderer->EndFrame(submittedFrame), 1u);

    Extrinsic::RHI::FrameHandle reusedFrame{};
    ASSERT_TRUE(renderer->BeginFrame(reusedFrame));
    const auto resolved =
        renderer->GetLastRenderGraphStats().GpuProfile;
    ASSERT_EQ(
        resolved.Status,
        Extrinsic::Graphics::
            RenderGraphGpuProfileStatus::Resolved);
    ASSERT_TRUE(resolved.Fresh);
    ASSERT_TRUE(resolved.HasResolvedFrame);
    ASSERT_FALSE(
        Extrinsic::Core::Telemetry::TelemetrySystem::Get()
            .GetPassTimings()
            .empty());

    profiler.Status = Extrinsic::RHI::ProfilerStatusSnapshot{
        .Status =
            Extrinsic::RHI::ProfilerBackendStatus::DeviceLost,
        .Source =
            Extrinsic::RHI::GpuTimestampSource::Unavailable,
        .Diagnostic = "test device lost before disabled ExecuteFrame",
    };
    const Extrinsic::Graphics::RenderFrameInput disabledInput{
        .Viewport = {.Width = 320u, .Height = 240u},
        .EnableGpuProfiling = false,
    };
    world = renderer->ExtractRenderWorld(disabledInput);
    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(reusedFrame, world);
    ASSERT_TRUE(
        renderer->GetLastRenderGraphStats().Execute.Succeeded);
    EXPECT_EQ(
        renderer->GetLastRenderGraphStats().GpuProfile.Status,
        Extrinsic::Graphics::
            RenderGraphGpuProfileStatus::DeviceLost);
    const auto& stale =
        renderer->GetLastRenderGraphStats().GpuProfile;
    EXPECT_EQ(stale.Diagnostic, profiler.Status.Diagnostic);
    EXPECT_FALSE(stale.Fresh);
    EXPECT_TRUE(stale.Stale);
    EXPECT_TRUE(stale.HasResolvedFrame);
    EXPECT_EQ(
        stale.ResolvedSubmittedFrameNumber,
        resolved.ResolvedSubmittedFrameNumber);
    EXPECT_EQ(stale.ResolvedFrameSlot, resolved.ResolvedFrameSlot);
    EXPECT_EQ(stale.Source, resolved.Source);
    EXPECT_TRUE(
        Extrinsic::Core::Telemetry::TelemetrySystem::Get()
            .GetPassTimings()
            .empty());
    ASSERT_EQ(renderer->EndFrame(reusedFrame), 2u);
    renderer->Shutdown();
}

TEST(RendererFrameLifecycle,
     DeviceLostProfilerStatusMapsFailedDeviceBeginFrame)
{
    NativeTimestampProfiler profiler;
    Extrinsic::Tests::MockDevice device;
    device.FramesInFlight = 1u;
    device.BackbufferHandle =
        Extrinsic::RHI::TextureHandle{909u, 1u};
    device.Profiler = &profiler;

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer =
        Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);

    Extrinsic::RHI::FrameHandle submittedFrame{};
    ASSERT_TRUE(renderer->BeginFrame(submittedFrame));
    const Extrinsic::Graphics::RenderFrameInput input{
        .Viewport = {.Width = 320u, .Height = 240u},
        .EnableGpuProfiling = true,
    };
    Extrinsic::Graphics::RenderWorld world =
        renderer->ExtractRenderWorld(input);
    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(submittedFrame, world);
    ASSERT_TRUE(
        renderer->GetLastRenderGraphStats().Execute.Succeeded);
    ASSERT_EQ(renderer->EndFrame(submittedFrame), 1u);

    Extrinsic::RHI::FrameHandle reusedFrame{};
    ASSERT_TRUE(renderer->BeginFrame(reusedFrame));
    const auto resolved =
        renderer->GetLastRenderGraphStats().GpuProfile;
    ASSERT_EQ(
        resolved.Status,
        Extrinsic::Graphics::
            RenderGraphGpuProfileStatus::Resolved);
    ASSERT_TRUE(resolved.Fresh);
    ASSERT_TRUE(resolved.HasResolvedFrame);

    const Extrinsic::Graphics::RenderFrameInput disabledInput{
        .Viewport = {.Width = 320u, .Height = 240u},
        .EnableGpuProfiling = false,
    };
    world = renderer->ExtractRenderWorld(disabledInput);
    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(reusedFrame, world);
    ASSERT_TRUE(
        renderer->GetLastRenderGraphStats().Execute.Succeeded);
    ASSERT_EQ(renderer->EndFrame(reusedFrame), 2u);

    Extrinsic::Core::Telemetry::TelemetrySystem::Get()
        .SetPassGpuTimings({
            Extrinsic::Core::Telemetry::PassTimingEntry{
                .Name = "must-clear-on-failed-begin",
                .GpuTimeNs = 77u,
                .CpuTimeNs = 0u,
            },
        });
    device.BeginFrameResult = false;

    Extrinsic::RHI::FrameHandle unavailableFrame{};
    EXPECT_FALSE(renderer->BeginFrame(unavailableFrame));
    const auto& unavailable =
        renderer->GetLastRenderGraphStats().GpuProfile;
    EXPECT_EQ(
        unavailable.Status,
        Extrinsic::Graphics::
            RenderGraphGpuProfileStatus::Unavailable);
    EXPECT_EQ(
        unavailable.Diagnostic,
        "GPU profile resolution is unavailable because frame acquisition "
        "failed.");
    EXPECT_FALSE(unavailable.Fresh);
    EXPECT_TRUE(unavailable.Stale);
    EXPECT_TRUE(unavailable.HasResolvedFrame);
    EXPECT_EQ(
        unavailable.ResolvedSubmittedFrameNumber,
        resolved.ResolvedSubmittedFrameNumber);
    EXPECT_EQ(
        unavailable.ResolvedFrameSlot,
        resolved.ResolvedFrameSlot);
    EXPECT_TRUE(
        Extrinsic::Core::Telemetry::TelemetrySystem::Get()
            .GetPassTimings()
            .empty());

    Extrinsic::Core::Telemetry::TelemetrySystem::Get()
        .SetPassGpuTimings({
            Extrinsic::Core::Telemetry::PassTimingEntry{
                .Name = "must-clear-on-device-loss",
                .GpuTimeNs = 88u,
                .CpuTimeNs = 0u,
            },
        });
    profiler.Status = Extrinsic::RHI::ProfilerStatusSnapshot{
        .Status =
            Extrinsic::RHI::ProfilerBackendStatus::DeviceLost,
        .Source =
            Extrinsic::RHI::GpuTimestampSource::Unavailable,
        .Diagnostic = "test device lost during BeginFrame",
    };

    Extrinsic::RHI::FrameHandle lostFrame{};
    EXPECT_FALSE(renderer->BeginFrame(lostFrame));
    const auto& stale =
        renderer->GetLastRenderGraphStats().GpuProfile;
    EXPECT_EQ(
        stale.Status,
        Extrinsic::Graphics::
            RenderGraphGpuProfileStatus::DeviceLost);
    EXPECT_EQ(stale.Diagnostic, profiler.Status.Diagnostic);
    EXPECT_FALSE(stale.Fresh);
    EXPECT_TRUE(stale.Stale);
    EXPECT_TRUE(stale.HasResolvedFrame);
    EXPECT_EQ(
        stale.ResolvedSubmittedFrameNumber,
        resolved.ResolvedSubmittedFrameNumber);
    EXPECT_EQ(stale.ResolvedFrameSlot, resolved.ResolvedFrameSlot);
    EXPECT_EQ(stale.Source, resolved.Source);
    EXPECT_TRUE(
        Extrinsic::Core::Telemetry::TelemetrySystem::Get()
            .GetPassTimings()
            .empty());
    renderer->Shutdown();
}

TEST(RendererFrameLifecycle,
     GpuProfilerHotDisableOmitsScopesAndReenableReusesAdapter)
{
    NativeTimestampProfiler profiler;
    Extrinsic::Tests::MockDevice device;
    device.FramesInFlight = 1u;
    device.BackbufferHandle =
        Extrinsic::RHI::TextureHandle{905u, 1u};
    device.Profiler = &profiler;

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer =
        Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);

    const auto executeFrame =
        [&](const bool enableGpuProfiling)
        {
            Extrinsic::RHI::FrameHandle frame{};
            EXPECT_TRUE(renderer->BeginFrame(frame));
            const Extrinsic::Graphics::RenderFrameInput input{
                .Viewport = {.Width = 320u, .Height = 240u},
                .EnableGpuProfiling = enableGpuProfiling,
            };
            Extrinsic::Graphics::RenderWorld world =
                renderer->ExtractRenderWorld(input);
            EXPECT_EQ(world.EnableGpuProfiling, enableGpuProfiling);
            renderer->PrepareFrame(world);
            renderer->ExecuteFrame(frame, world);
            EXPECT_TRUE(
                renderer->GetLastRenderGraphStats().Execute.Succeeded);
            (void)renderer->EndFrame(frame);
        };

    executeFrame(true);
    ASSERT_EQ(profiler.BeginFrameCalls, 1u);
    ASSERT_EQ(profiler.EndFrameCalls.size(), 1u);
    ASSERT_FALSE(profiler.BeginScopeAttempts.empty());
    const std::size_t scopeAttemptsAfterEnabled =
        profiler.BeginScopeAttempts.size();
    const int buffersAfterEnabled = device.CreateBufferCount;
    const int texturesAfterEnabled = device.CreateTextureCount;
    const int samplersAfterEnabled = device.CreateSamplerCount;
    const int pipelinesAfterEnabled = device.CreatePipelineCount;
    EXPECT_EQ(device.GetProfiler(), &profiler);

    executeFrame(false);
    EXPECT_EQ(
        renderer->GetLastRenderGraphStats().GpuProfile.Status,
        Extrinsic::Graphics::RenderGraphGpuProfileStatus::Disabled);
    EXPECT_EQ(profiler.BeginFrameCalls, 1u);
    EXPECT_EQ(profiler.EndFrameCalls.size(), 1u);
    EXPECT_EQ(profiler.BeginScopeAttempts.size(),
              scopeAttemptsAfterEnabled);
    EXPECT_EQ(device.CreateBufferCount, buffersAfterEnabled);
    EXPECT_EQ(device.CreateTextureCount, texturesAfterEnabled);
    EXPECT_EQ(device.CreateSamplerCount, samplersAfterEnabled);
    EXPECT_EQ(device.CreatePipelineCount, pipelinesAfterEnabled);
    EXPECT_EQ(device.GetProfiler(), &profiler);

    executeFrame(true);
    EXPECT_EQ(profiler.BeginFrameCalls, 2u);
    EXPECT_EQ(profiler.EndFrameCalls.size(), 2u);
    EXPECT_GT(profiler.BeginScopeAttempts.size(),
              scopeAttemptsAfterEnabled);
    EXPECT_EQ(device.CreateBufferCount, buffersAfterEnabled);
    EXPECT_EQ(device.CreateTextureCount, texturesAfterEnabled);
    EXPECT_EQ(device.CreateSamplerCount, samplersAfterEnabled);
    EXPECT_EQ(device.CreatePipelineCount, pipelinesAfterEnabled);
    EXPECT_EQ(device.GetProfiler(), &profiler);

    renderer->Shutdown();
}

TEST(RendererFrameLifecycle,
     GpuProfilerShutdownDiscardsActiveCandidateBeforeReinitialize)
{
    NativeTimestampProfiler profiler;
    Extrinsic::Tests::MockDevice device;
    device.FramesInFlight = 1u;
    device.BackbufferHandle =
        Extrinsic::RHI::TextureHandle{904u, 1u};
    device.Profiler = &profiler;

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer =
        Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);

    Extrinsic::RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));
    const Extrinsic::Graphics::RenderFrameInput input{
        .Viewport = {.Width = 320u, .Height = 240u},
        .EnableGpuProfiling = true,
    };
    Extrinsic::Graphics::RenderWorld world =
        renderer->ExtractRenderWorld(input);
    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(frame, world);
    ASSERT_EQ(profiler.EndFrameCalls.size(), 0u);

    renderer->Shutdown();
    ASSERT_EQ(profiler.EndFrameCalls.size(), 1u);
    EXPECT_EQ(
        profiler.EndFrameCalls.front().Disposition,
        Extrinsic::RHI::ProfilerFrameDisposition::Discarded);

    renderer->Initialize(device);
    ASSERT_TRUE(renderer->BeginFrame(frame));
    world = renderer->ExtractRenderWorld(input);
    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(frame, world);
    EXPECT_EQ(renderer->EndFrame(frame), 1u);
    ASSERT_EQ(profiler.EndFrameCalls.size(), 2u);
    EXPECT_EQ(
        profiler.EndFrameCalls.back().Disposition,
        Extrinsic::RHI::ProfilerFrameDisposition::Submitted);

    renderer->Shutdown();
}
