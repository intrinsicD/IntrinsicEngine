module;
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
module Extrinsic.Graphics.CoherentPointDriftEStep;

import Extrinsic.RHI.Device;
import Extrinsic.RHI.CommandContext;
import Extrinsic.Core.Filesystem.PathResolver;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Types;

namespace Extrinsic::Graphics
{
    namespace
    {
        constexpr std::uint32_t kTile = 128u; // CPD_TILE of cpd_estep_common.glslinc
        struct Push
        {
            std::uint64_t Target{}, Source{}, Results{};
            double LogOutlier{};
            float InverseTwoSigma2{};
            std::uint32_t TargetCount{}, SourceCount{};
            std::uint32_t First{}, Count{};
            std::uint32_t HasOutlier{};
        };
        static_assert(sizeof(Push) == 56 && offsetof(Push, InverseTwoSigma2) == 32 && offsetof(Push, HasOutlier) == 52);
    }

    struct CoherentPointDriftEStepWorkspace::Impl
    {
        RHI::IDevice& Device;
        RHI::PipelineHandle TargetPass{}, SourcePass{};
        RHI::BufferHandle Target{}, Source{}, Results{};
        std::size_t TargetBytes{0u}, SourceBytes{0u}, ResultBytes{0u};
        std::uint64_t TargetGeneration{0u};
        explicit Impl(RHI::IDevice& device) : Device(device) {}
        ~Impl()
        {
            for (auto buffer : {Target, Source, Results})
                if (buffer.IsValid()) Device.DestroyBuffer(buffer);
            for (auto pipeline : {TargetPass, SourcePass})
                if (pipeline.IsValid()) Device.DestroyPipeline(pipeline);
        }
        // Keeps a host-visible buffer of exactly `bytes`, recreating it when the size changes.
        bool Ensure(RHI::BufferHandle& buffer, std::size_t& current, std::size_t bytes, const char* name)
        {
            if (buffer.IsValid() && current == bytes) return true;
            if (buffer.IsValid()) Device.DestroyBuffer(buffer);
            buffer = Device.CreateBuffer({.SizeBytes = std::max<std::size_t>(bytes, 16),
                .Usage = RHI::BufferUsage::Storage | RHI::BufferUsage::TransferSrc | RHI::BufferUsage::TransferDst,
                .HostVisible = true, .DebugName = name});
            current = buffer.IsValid() ? bytes : 0u;
            return buffer.IsValid();
        }
        RHI::PipelineHandle Pipeline(RHI::PipelineHandle& pipeline, const char* shader, const char* name)
        {
            if (!pipeline.IsValid())
            {
                const auto path = Core::Filesystem::GetShaderPath(shader);
                pipeline = Device.CreatePipeline({.VertexShaderPath = {}, .FragmentShaderPath = {},
                    .ComputeShaderPath = path.c_str(), .PushConstantSize = sizeof(Push), .DebugName = name});
            }
            return pipeline;
        }
    };

    CoherentPointDriftEStepWorkspace::CoherentPointDriftEStepWorkspace(RHI::IDevice& device)
        : m_Impl(std::make_unique<Impl>(device)) {}
    CoherentPointDriftEStepWorkspace::~CoherentPointDriftEStepWorkspace() = default;

    std::size_t CoherentPointDriftEStepWorkspace::ReadbackBytes(const std::size_t targets, const std::size_t sources) noexcept
    {
        return (2u * targets + 4u * sources) * sizeof(double);
    }

    RHI::BufferHandle CoherentPointDriftEStepWorkspace::Record(RHI::ICommandContext& commands,
                                                               const CoherentPointDriftEStepGpuInput& input)
    {
        auto& s = *m_Impl;
        const std::size_t n = input.Target.size() / 4u, m = input.Source.size() / 4u;
        if (n == 0u || m == 0u || n > MaxPoints || m > MaxPoints || input.Target.size() % 4u || input.Source.size() % 4u ||
            !(input.Sigma2 > 0.0) || !std::isfinite(input.Sigma2) || std::isnan(input.LogOutlier) ||
            input.LogOutlier == std::numeric_limits<double>::infinity() ||
            !s.Device.IsOperational() || !s.Device.SupportsShaderFloat64())
            return {};
        if (!s.Pipeline(s.TargetPass, "shaders/cpd_estep_target_pass.comp.spv", "CoherentPointDrift.EStep.TargetPass").IsValid() ||
            !s.Pipeline(s.SourcePass, "shaders/cpd_estep_source_pass.comp.spv", "CoherentPointDrift.EStep.SourcePass").IsValid())
            return {};
        const bool targetCurrent = s.Target.IsValid() && s.TargetBytes == input.Target.size_bytes() &&
                                   s.TargetGeneration == input.TargetGeneration;
        if (!targetCurrent)
        {
            if (!s.Ensure(s.Target, s.TargetBytes, input.Target.size_bytes(), "CoherentPointDrift.EStep.Target")) return {};
            s.Device.WriteBuffer(s.Target, input.Target.data(), input.Target.size_bytes());
            s.TargetGeneration = input.TargetGeneration;
        }
        if (!s.Ensure(s.Source, s.SourceBytes, input.Source.size_bytes(), "CoherentPointDrift.EStep.Source") ||
            !s.Ensure(s.Results, s.ResultBytes, ReadbackBytes(n, m), "CoherentPointDrift.EStep.Results"))
            return {};
        s.Device.WriteBuffer(s.Source, input.Source.data(), input.Source.size_bytes());

        const auto address = [&](RHI::BufferHandle b) { return s.Device.GetBufferDeviceAddress(b); };
        const bool outlier = input.LogOutlier > -std::numeric_limits<double>::infinity();
        Push push{.Target = address(s.Target), .Source = address(s.Source), .Results = address(s.Results),
                  .LogOutlier = outlier ? input.LogOutlier : 0.0,
                  .InverseTwoSigma2 = float(1.0 / (2.0 * input.Sigma2)),
                  .TargetCount = std::uint32_t(n), .SourceCount = std::uint32_t(m), .HasOutlier = outlier ? 1u : 0u};
        for (auto buffer : {s.Target, s.Source, s.Results})
            commands.BufferBarrier(buffer, RHI::MemoryAccess::TransferWrite | RHI::MemoryAccess::ShaderRead,
                                   RHI::MemoryAccess::ShaderRead | RHI::MemoryAccess::ShaderWrite);
        // Each thread loops over the whole other set, so a dispatch of `count` threads evaluates
        // count * other pairs; chunks are whole workgroups.
        const auto pass = [&](RHI::PipelineHandle pipeline, std::size_t threads, std::size_t other)
        {
            commands.BindPipeline(pipeline);
            const std::uint64_t groups = std::max<std::uint64_t>(1u, MaxPairsPerDispatch / (std::uint64_t(other) * kTile));
            const std::size_t chunk = std::size_t(groups) * kTile;
            for (std::size_t first = 0; first < threads; first += chunk)
            {
                push.First = std::uint32_t(first);
                push.Count = std::uint32_t(std::min(chunk, threads - first));
                commands.PushConstants(&push, sizeof(push), 0);
                commands.Dispatch((push.Count + kTile - 1u) / kTile, 1, 1);
            }
        };
        pass(s.TargetPass, n, m);
        commands.BufferBarrier(s.Results, RHI::MemoryAccess::ShaderRead | RHI::MemoryAccess::ShaderWrite,
                               RHI::MemoryAccess::ShaderRead | RHI::MemoryAccess::ShaderWrite);
        pass(s.SourcePass, m, n);
        commands.BufferBarrier(s.Results, RHI::MemoryAccess::ShaderRead | RHI::MemoryAccess::ShaderWrite,
                               RHI::MemoryAccess::TransferRead);
        return s.Results;
    }
}
