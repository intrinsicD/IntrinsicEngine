module;

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

export module Extrinsic.Graphics.RenderPrepPipeline;

import Extrinsic.Core.Error;
import Extrinsic.RHI.PipelineManager;
import Extrinsic.Graphics.MaterialSystem;
import Extrinsic.Graphics.ColormapSystem;
import Extrinsic.Graphics.VisualizationSyncSystem;
import Extrinsic.Graphics.VisualizationPackets;
import Extrinsic.Graphics.TransformSyncSystem;
import Extrinsic.Graphics.LightSystem;
import Extrinsic.Graphics.GpuWorld;
import Extrinsic.Graphics.CullingSystem;

namespace Extrinsic::Graphics
{
    export enum class RenderPrepStep : std::uint8_t
    {
        PipelineCommit,
        MaterialBaseSync,
        VisualizationSync,
        MaterialOverrideSync,
        TransformSync,
        LightSync,
        ClusterLightTableSync,
        GpuWorldSync,
        CullingSync,
        Count,
    };

    export enum class RenderPrepRequiredInput : std::uint8_t
    {
        PipelineManager,
        MaterialSystem,
        ColormapSystem,
        VisualizationSyncSystem,
        TransformSyncSystem,
        LightSystem,
        GpuWorld,
        CullingSystem,
        ClusterLightResourcesHook,
        Count,
    };

    export enum class RenderPrepFailureReason : std::uint8_t
    {
        None,
        MissingRequiredInput,
        TaskGraphCompileFailed,
        TaskGraphExecuteFailed,
    };

    export struct RenderPrepPipelineInputs
    {
        RHI::PipelineManager* PipelineManager = nullptr;
        MaterialSystem* Materials = nullptr;
        ColormapSystem* Colormaps = nullptr;
        VisualizationSyncSystem* VisualizationSync = nullptr;
        TransformSyncSystem* TransformSync = nullptr;
        LightSystem* Lights = nullptr;
        GpuWorld* World = nullptr;
        CullingSystem* Culling = nullptr;

        std::span<VisualizationSyncRecord> VisualizationSyncRecords{};
        std::span<const VisualizationPropertyBufferAddress> VisualizationPropertyBufferAddresses{};
        std::span<const ScalarAttributePacket> VisualizationScalarPackets{};
        std::span<const TransformSyncRecord> TransformSyncRecords{};
        std::span<const LightSnapshot> LightSnapshots{};

        std::function<bool()> EnsureClusterLightResources{};
        std::function<void(RenderPrepStep)> OnStepExecuted{};
    };

    export struct RenderPrepPipelineOptions
    {
        bool UseTaskGraph = true;
        std::optional<Core::ErrorCode> ForceTaskGraphCompileFailure{};
        std::optional<Core::ErrorCode> ForceTaskGraphExecuteFailure{};
    };

    export struct RenderPrepPipelineResult
    {
        bool Succeeded = false;
        RenderPrepFailureReason FailureReason = RenderPrepFailureReason::None;
        std::vector<RenderPrepRequiredInput> MissingInputs{};
        std::vector<RenderPrepStep> ExecutedSteps{};
        Core::ErrorCode TaskGraphError = Core::ErrorCode::Success;
        std::uint64_t TaskGraphCompileCalls = 0u;
        std::uint64_t TaskGraphPlanBuilds = 0u;
        std::uint64_t TaskGraphPlanReuses = 0u;
        bool TaskGraphLastCompileReusedPlan = false;
        std::string Diagnostic{};
    };

    export [[nodiscard]] std::string_view ToString(RenderPrepStep step) noexcept;
    export [[nodiscard]] std::string_view ToString(RenderPrepRequiredInput input) noexcept;
    export [[nodiscard]] std::string_view ToString(RenderPrepFailureReason reason) noexcept;

    export class RenderPrepPipeline final
    {
    public:
        RenderPrepPipeline();
        ~RenderPrepPipeline();
        RenderPrepPipeline(const RenderPrepPipeline&) = delete;
        RenderPrepPipeline& operator=(const RenderPrepPipeline&) = delete;

        [[nodiscard]] RenderPrepPipelineResult Run(
            const RenderPrepPipelineInputs& inputs,
            const RenderPrepPipelineOptions& options = {});

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
