module;

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <vector>

export module Extrinsic.Runtime.GpuReadbackJob;

import Extrinsic.Core.Dag.Scheduler;
import Extrinsic.Core.Error;
import Extrinsic.Graphics.GpuTransfer;
import Extrinsic.RHI.BufferTransfer;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.Types;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.WorldHandle;
import Geometry.Properties;

export namespace Extrinsic::Runtime
{
    enum class GpuReadbackPropertyType : std::uint8_t
    {
        Float32,
        Float64,
        Int32,
        UInt32,
        Vec2Float32,
        Vec3Float32,
        Vec4Float32,
    };

    struct GpuReadbackPropertyBinding
    {
        Geometry::PropertyRegistry* TargetProperties{nullptr};
        std::string TargetProperty{};
        GpuReadbackPropertyType TargetType{GpuReadbackPropertyType::Float32};
        RHI::BufferRange SourceRange{};
        std::uint64_t SourceStrideBytes{0u}; // 0 means tightly packed.
    };

    struct GpuReadbackJobDesc
    {
        std::string Name{};
        Core::Dag::TaskPriority Priority{Core::Dag::TaskPriority::Normal};
        std::uint32_t EstimatedCost{1u};
        std::uint64_t CancellationGeneration{0u};
        WorldHandle Scope{DefaultWorldHandle};
        std::vector<JobDependency> DependsOn{};

        Graphics::GpuTransfer* Transfer{nullptr};
        RHI::ICommandContext* CommandContext{nullptr};
        RHI::BufferHandle Source{};
        RHI::BufferDesc SourceDesc{};
        RHI::MemoryAccess SourceAccess{RHI::MemoryAccess::ShaderWrite};
        GpuReadbackPropertyBinding Binding{};

        // Fail-closed revalidation on the main thread, immediately before the
        // readback bytes are allowed to touch the target property.
        std::move_only_function<JobApplyValidation()> ValidateBeforeApply{};
        // Runs after the property write succeeds, on the main thread. A failure
        // here fails the job's completion the same way a failed write does.
        std::move_only_function<Core::Result()> ApplyAfterWrite{};
    };

    [[nodiscard]] Core::Result ValidateGpuReadbackPropertyBinding(
        const GpuReadbackPropertyBinding& binding) noexcept;

    [[nodiscard]] Core::Result WriteGpuReadbackProperty(
        const GpuReadbackPropertyBinding& binding,
        std::span<const std::byte> bytes) noexcept;

    // Issues the readback on a worker, parks the job until the transfer is
    // delivered, then writes the bytes into the bound property from the
    // main-thread completion drain. RUNTIME-194 Slice B5a moved this from
    // `DerivedJobRegistry` onto `JobService`: readback parking is
    // `JobDesc::IsReadyToApply` and staleness is `JobDesc::ValidateBeforeApply`,
    // so the helper composes the one execution service instead of a second one.
    [[nodiscard]] JobToken SubmitGpuReadbackJob(
        JobService& jobs,
        GpuReadbackJobDesc desc);
}
