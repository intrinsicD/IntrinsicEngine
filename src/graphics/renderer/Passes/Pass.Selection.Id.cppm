// Picking ID passes: entity and face IDs draw the opaque surface bucket, edge IDs the
// indexed line bucket and point IDs the non-indexed selection-point bucket.
module;

#include <cstdint>

export module Extrinsic.Graphics.Pass.Selection.Id;

import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.Types;
import Extrinsic.Graphics.CullingSystem;
import Extrinsic.Graphics.GpuWorld;
import Extrinsic.Graphics.SelectionSystem;

namespace Extrinsic::Graphics
{
    // Shared pass state. Each pass keeps its own pipeline and records nothing until
    // the selection system is initialized and that pipeline is valid.
    export class SelectionIdPassState
    {
    public:
        explicit SelectionIdPassState(SelectionSystem& selection) : m_SelectionSystem(selection) {}

        SelectionIdPassState(const SelectionIdPassState&)            = delete;
        SelectionIdPassState& operator=(const SelectionIdPassState&) = delete;

        void SetPipeline(RHI::PipelineHandle pipeline) noexcept { m_Pipeline = pipeline; }

    protected:
        [[nodiscard]] bool Ready() const noexcept
        {
            return m_SelectionSystem.IsInitialized() && m_Pipeline.IsValid();
        }

        SelectionSystem&    m_SelectionSystem;
        RHI::PipelineHandle m_Pipeline{};
    };

    export class EntityIdPass : public SelectionIdPassState
    {
    public:
        using SelectionIdPassState::SelectionIdPassState;
        void Execute(RHI::ICommandContext& cmd, const RHI::CameraUBO& camera, const GpuWorld& gpuWorld,
                     const CullingSystem& culling, std::uint32_t frameIndex);
    };

    export class FaceIdPass : public SelectionIdPassState
    {
    public:
        using SelectionIdPassState::SelectionIdPassState;
        void Execute(RHI::ICommandContext& cmd, const RHI::CameraUBO& camera, const GpuWorld& gpuWorld,
                     const CullingSystem& culling, std::uint32_t frameIndex);
    };

    export class EdgeIdPass : public SelectionIdPassState
    {
    public:
        using SelectionIdPassState::SelectionIdPassState;
        void Execute(RHI::ICommandContext& cmd, const RHI::CameraUBO& camera, const GpuWorld& gpuWorld,
                     const CullingSystem& culling, std::uint32_t frameIndex);
    };

    export class PointIdPass : public SelectionIdPassState
    {
    public:
        using SelectionIdPassState::SelectionIdPassState;
        void Execute(RHI::ICommandContext& cmd, const RHI::CameraUBO& camera, const GpuWorld& gpuWorld,
                     const CullingSystem& culling, std::uint32_t frameIndex);
    };
}
