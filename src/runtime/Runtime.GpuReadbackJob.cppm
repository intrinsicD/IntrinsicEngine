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
import Extrinsic.Runtime.DerivedJobGraph;
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
        DerivedJobKey Key{};
        std::string Name{};
        Core::Dag::TaskPriority Priority{Core::Dag::TaskPriority::Normal};
        std::uint32_t EstimatedCost{1u};
        std::uint64_t CancellationGeneration{0u};
        bool HasPreviousOutput{false};
        std::vector<DerivedJobDependency> DependsOn{};

        Graphics::GpuTransfer* Transfer{nullptr};
        RHI::ICommandContext* CommandContext{nullptr};
        RHI::BufferHandle Source{};
        RHI::BufferDesc SourceDesc{};
        RHI::MemoryAccess SourceAccess{RHI::MemoryAccess::ShaderWrite};
        GpuReadbackPropertyBinding Binding{};

        std::move_only_function<DerivedJobApplyValidation()> ValidateOnMainThread{};
        std::move_only_function<Core::Result(DerivedJobApplyContext&)> ApplyAfterWrite{};
    };

    [[nodiscard]] Core::Result ValidateGpuReadbackPropertyBinding(
        const GpuReadbackPropertyBinding& binding) noexcept;

    [[nodiscard]] Core::Result WriteGpuReadbackProperty(
        const GpuReadbackPropertyBinding& binding,
        std::span<const std::byte> bytes) noexcept;

    [[nodiscard]] DerivedJobHandle SubmitGpuReadbackJob(
        DerivedJobRegistry& registry,
        GpuReadbackJobDesc desc);
}
