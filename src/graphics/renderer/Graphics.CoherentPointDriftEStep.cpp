module;
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
module Extrinsic.Graphics.CoherentPointDriftEStep;

import Extrinsic.Graphics.ComputeParallelPrimitives;
import Extrinsic.RHI.BufferManager;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Types;

namespace Extrinsic::Graphics
{
    namespace
    {
        constexpr std::uint32_t kTile = 128u; // CPD_TILE of cpd_estep_common.glslinc
        struct Push
        {
            std::uint64_t Target{}, Source{}, Results{}, SkipRows{};
            double LogOutlier{};
            float InverseTwoSigma2{};
            std::uint32_t TargetCount{}, SourceCount{};
            std::uint32_t First{}, Count{};
            std::uint32_t HasOutlier{}, Reserved{};
        };
        static_assert(sizeof(Push) == 72 && offsetof(Push, InverseTwoSigma2) == 40 && offsetof(Push, HasOutlier) == 60);
    }

    struct CoherentPointDriftEStepWorkspace::Impl
    {
        RHI::IDevice& Device;
        RHI::BufferManager& Scratch;
        RHI::PipelineHandle TargetPass{}, SourcePass{};
        RHI::BufferManager::BufferLease Target{}, Source{}, Results{}, Skip{};
        // ARCH-023, a prepared workspace (Prepare): Target, Source and Skip are device-local, fed from these retained
        // host-visible staging buffers by IDevice::WriteMappedBuffer and compute-lane copies, and every address is
        // resolved, so Record makes no other device call (a method worker records it). Its shape bound is Rows x Cols.
        bool Prepared{false};
        RHI::BufferManager::BufferLease TargetStaging{}, SourceStaging{}, SkipStaging{};
        std::uint64_t TargetAddress{}, SourceAddress{}, ResultsAddress{}, SkipAddress{};
        std::size_t PreparedTargets{0u}, PreparedSources{0u};
        // The uploaded target: its byte size and generation (0: none is current).
        std::size_t TargetBytes{0u};
        std::uint64_t TargetGeneration{0u};
        Impl(RHI::IDevice& device, RHI::BufferManager& scratch) : Device(device), Scratch(scratch) {}
        ~Impl()
        {
            for (auto pipeline : {TargetPass, SourcePass})
                if (pipeline.IsValid()) Device.DestroyPipeline(pipeline);
        }
        // Keeps a host-visible buffer of at least `bytes`, replacing it only when it is too small,
        // so iterations within a lease keep the largest allocation. The shaders address it through
        // the counts in the push constants, never its size. The caller has retired every
        // submission that used it (one computation at a time; pooled leases mature).
        bool Ensure(
            RHI::BufferManager::BufferLease& buffer, std::size_t bytes, const char* name, const bool host = true)
        {
            return Scratch.EnsureScratch(buffer,
                {.SizeBytes = std::max<std::size_t>(bytes, 16),
                    .Usage = RHI::BufferUsage::Storage | RHI::BufferUsage::TransferSrc | RHI::BufferUsage::TransferDst,
                    .HostVisible = host,
                    .DebugName = name});
        }
        RHI::PipelineHandle Pipeline(RHI::PipelineHandle& pipeline, const char* shader, const char* name)
        {
            if (!pipeline.IsValid()) pipeline = CreateComputePipeline(Device, shader, sizeof(Push), name);
            return pipeline;
        }
        // Both passes over the bound inputs (barriers from their writes first), the results ready for transfer reads.
        RHI::BufferHandle RecordPasses(RHI::ICommandContext& commands,
            Push push,
            const std::size_t n,
            const std::size_t m,
            const std::array<RHI::BufferHandle, 4> bound)
        {
            auto& s = *this;
            const auto [target, source, results, skip] = bound;
            for (auto buffer : {target, source, results, skip})
                if (buffer.IsValid())
                    commands.BufferBarrier(buffer,
                        RHI::MemoryAccess::TransferWrite | RHI::MemoryAccess::ShaderRead,
                        RHI::MemoryAccess::ShaderRead | RHI::MemoryAccess::ShaderWrite);
            // Each thread loops over the whole other set, so a dispatch of `count` threads evaluates
            // count * other pairs; chunks are whole workgroups.
            const auto pass = [&](RHI::PipelineHandle pipeline, std::size_t threads, std::size_t other)
            {
                commands.BindPipeline(pipeline);
                // Also within the guaranteed Vulkan limit of 65535 workgroups per dispatch dimension.
                const std::uint64_t groups =
                    std::clamp<std::uint64_t>(MaxPairsPerDispatch / (std::uint64_t(other) * kTile), 1u, 65535u);
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
            commands.BufferBarrier(results,
                RHI::MemoryAccess::ShaderRead | RHI::MemoryAccess::ShaderWrite,
                RHI::MemoryAccess::ShaderRead | RHI::MemoryAccess::ShaderWrite);
            pass(s.SourcePass, m, n);
            commands.BufferBarrier(results,
                RHI::MemoryAccess::ShaderRead | RHI::MemoryAccess::ShaderWrite,
                RHI::MemoryAccess::TransferRead);
            return results;
        }
    };

    CoherentPointDriftEStepWorkspace::CoherentPointDriftEStepWorkspace(
        RHI::IDevice& device, RHI::BufferManager& scratch)
        : m_Impl(std::make_unique<Impl>(device, scratch))
    {
    }
    CoherentPointDriftEStepWorkspace::~CoherentPointDriftEStepWorkspace() = default;
    void CoherentPointDriftEStepWorkspace::ReleaseScratch()
    {
        auto& s = *m_Impl;
        s.Target = {};
        s.Source = {};
        s.Results = {};
        s.Skip = {};
        s.TargetStaging = {};
        s.SourceStaging = {};
        s.SkipStaging = {};
        s.Prepared = false;
        s.TargetAddress = s.SourceAddress = s.ResultsAddress = s.SkipAddress = 0u;
        s.PreparedTargets = s.PreparedSources = 0u;
        s.TargetBytes = 0u;
        s.TargetGeneration = 0u;
    }

    std::size_t CoherentPointDriftEStepWorkspace::ReadbackBytes(const std::size_t targets, const std::size_t sources) noexcept
    {
        return (2u * targets + 4u * sources) * sizeof(double);
    }

    bool CoherentPointDriftEStepWorkspace::Prepare(const std::size_t targets, const std::size_t sources)
    {
        auto& s = *m_Impl;
        s.Prepared = false;
        if (targets == 0u || sources == 0u || targets > MaxPoints || sources > MaxPoints || !s.Device.IsOperational() ||
            !s.Device.SupportsShaderFloat64())
            return false;
        if (!s.Pipeline(s.TargetPass, "shaders/cpd_estep_target_pass.comp.spv", "CoherentPointDrift.EStep.TargetPass")
                .IsValid() ||
            !s.Pipeline(s.SourcePass, "shaders/cpd_estep_source_pass.comp.spv", "CoherentPointDrift.EStep.SourcePass")
                .IsValid())
            return false;
        const std::size_t targetBytes = targets * 8u * sizeof(float), sourceBytes = sources * 8u * sizeof(float),
                          skipBytes = targets * sizeof(std::uint32_t);
        if (!s.Ensure(s.Target, targetBytes, "CoherentPointDrift.EStep.Target", false) ||
            !s.Ensure(s.Source, sourceBytes, "CoherentPointDrift.EStep.Source", false) ||
            !s.Ensure(s.Skip, skipBytes, "CoherentPointDrift.EStep.SkipRows", false) ||
            !s.Ensure(s.Results, ReadbackBytes(targets, sources), "CoherentPointDrift.EStep.Results", false) ||
            !s.Ensure(s.TargetStaging, targetBytes, "CoherentPointDrift.EStep.TargetStaging") ||
            !s.Ensure(s.SourceStaging, sourceBytes, "CoherentPointDrift.EStep.SourceStaging") ||
            !s.Ensure(s.SkipStaging, skipBytes, "CoherentPointDrift.EStep.SkipStaging"))
            return false;
        const auto address = [&](const RHI::BufferManager::BufferLease& lease)
        {
            return s.Device.GetBufferDeviceAddress(lease.GetHandle());
        };
        s.TargetAddress = address(s.Target);
        s.SourceAddress = address(s.Source);
        s.ResultsAddress = address(s.Results);
        s.SkipAddress = address(s.Skip);
        // A pooled workspace's earlier target (an unprepared run's) is not this layout's.
        s.TargetGeneration = 0u;
        s.TargetBytes = 0u;
        s.PreparedTargets = targets;
        s.PreparedSources = sources;
        s.Prepared = s.TargetAddress && s.SourceAddress && s.ResultsAddress && s.SkipAddress;
        return s.Prepared;
    }

    RHI::BufferHandle CoherentPointDriftEStepWorkspace::Record(RHI::ICommandContext& commands,
                                                               const CoherentPointDriftEStepGpuInput& input)
    {
        auto& s = *m_Impl;
        const std::size_t n = input.Target.size() / 8u, m = input.Source.size() / 8u;
        if (n == 0u || m == 0u || n > MaxPoints || m > MaxPoints || input.Target.size() % 8u ||
            input.Source.size() % 8u || (!input.SkipRows.empty() && input.SkipRows.size() != n) ||
            !(input.Sigma2 > 0.0) || !std::isfinite(input.Sigma2) || std::isnan(input.LogOutlier) ||
            input.LogOutlier == std::numeric_limits<double>::infinity())
            return {};
        if (s.Prepared)
            return RecordPrepared(commands, input, n, m);
        if (!s.Device.IsOperational() || !s.Device.SupportsShaderFloat64())
            return {};
        if (!s.Pipeline(s.TargetPass, "shaders/cpd_estep_target_pass.comp.spv", "CoherentPointDrift.EStep.TargetPass").IsValid() ||
            !s.Pipeline(s.SourcePass, "shaders/cpd_estep_source_pass.comp.spv", "CoherentPointDrift.EStep.SourcePass").IsValid())
            return {};
        // Generations are process-unique, so a pooled workspace never mistakes another run's
        // target for this one; generation 0 is never treated as current.
        const bool targetCurrent = s.Target.IsValid() && input.TargetGeneration != 0u &&
                                   s.TargetGeneration == input.TargetGeneration &&
                                   s.TargetBytes == input.Target.size_bytes();
        if (!targetCurrent)
        {
            s.TargetGeneration = 0u;
            s.TargetBytes = 0u;
            if (!s.Ensure(s.Target, input.Target.size_bytes(), "CoherentPointDrift.EStep.Target"))
                return {};
            s.Device.WriteBuffer(s.Target.GetHandle(), input.Target.data(), input.Target.size_bytes());
            s.TargetGeneration = input.TargetGeneration;
            s.TargetBytes = input.Target.size_bytes();
        }
        if (!s.Ensure(s.Source, input.Source.size_bytes(), "CoherentPointDrift.EStep.Source") ||
            !s.Ensure(s.Results, ReadbackBytes(n, m), "CoherentPointDrift.EStep.Results"))
            return {};
        s.Device.WriteBuffer(s.Source.GetHandle(), input.Source.data(), input.Source.size_bytes());
        if (!input.SkipRows.empty())
        {
            if (!s.Ensure(s.Skip, input.SkipRows.size_bytes(), "CoherentPointDrift.EStep.SkipRows"))
                return {};
            s.Device.WriteBuffer(s.Skip.GetHandle(), input.SkipRows.data(), input.SkipRows.size_bytes());
        }
        const auto target = s.Target.GetHandle(), source = s.Source.GetHandle(), results = s.Results.GetHandle(),
                   skip = s.Skip.GetHandle();

        const auto address = [&](RHI::BufferHandle b) { return s.Device.GetBufferDeviceAddress(b); };
        const bool outlier = input.LogOutlier > -std::numeric_limits<double>::infinity();
        Push push{.Target = address(target),
            .Source = address(source),
            .Results = address(results),
            .SkipRows = input.SkipRows.empty() ? 0u : address(skip),
            .LogOutlier = outlier ? input.LogOutlier : 0.0,
            .InverseTwoSigma2 = float(1.0 / (2.0 * input.Sigma2)),
            .TargetCount = std::uint32_t(n),
            .SourceCount = std::uint32_t(m),
            .HasOutlier = outlier ? 1u : 0u};
        return s.RecordPasses(commands, push, n, m, {target, source, results, skip});
    }

    RHI::BufferHandle CoherentPointDriftEStepWorkspace::RecordPrepared(RHI::ICommandContext& commands,
        const CoherentPointDriftEStepGpuInput& input,
        const std::size_t n,
        const std::size_t m)
    {
        auto& s = *m_Impl;
        if (n > s.PreparedTargets || m > s.PreparedSources)
            return {};
        // Host rows into the retained staging (no submitted work still reads it: E-steps are serial), then
        // compute-lane copies into the device-local inputs, ordered after their earlier passes' reads.
        const auto stage = [&](const RHI::BufferManager::BufferLease& staging,
                               const RHI::BufferManager::BufferLease& to,
                               const void* data,
                               const std::size_t bytes)
        {
            if (!s.Device.WriteMappedBuffer(staging.GetHandle(), data, bytes))
                return false;
            commands.BufferBarrier(to.GetHandle(), RHI::MemoryAccess::ShaderRead, RHI::MemoryAccess::TransferWrite);
            commands.CopyBuffer(staging.GetHandle(), to.GetHandle(), 0u, 0u, bytes);
            return true;
        };
        const bool targetCurrent = input.TargetGeneration != 0u && s.TargetGeneration == input.TargetGeneration &&
            s.TargetBytes == input.Target.size_bytes();
        if (!targetCurrent)
        {
            s.TargetGeneration = 0u;
            s.TargetBytes = 0u;
            if (!stage(s.TargetStaging, s.Target, input.Target.data(), input.Target.size_bytes()))
                return {};
            s.TargetGeneration = input.TargetGeneration;
            s.TargetBytes = input.Target.size_bytes();
        }
        if (!stage(s.SourceStaging, s.Source, input.Source.data(), input.Source.size_bytes()) ||
            (!input.SkipRows.empty() &&
                !stage(s.SkipStaging, s.Skip, input.SkipRows.data(), input.SkipRows.size_bytes())))
            return {};
        const bool outlier = input.LogOutlier > -std::numeric_limits<double>::infinity();
        const Push push{.Target = s.TargetAddress,
            .Source = s.SourceAddress,
            .Results = s.ResultsAddress,
            .SkipRows = input.SkipRows.empty() ? 0u : s.SkipAddress,
            .LogOutlier = outlier ? input.LogOutlier : 0.0,
            .InverseTwoSigma2 = float(1.0 / (2.0 * input.Sigma2)),
            .TargetCount = std::uint32_t(n),
            .SourceCount = std::uint32_t(m),
            .HasOutlier = outlier ? 1u : 0u};
        return s.RecordPasses(commands,
            push,
            n,
            m,
            {s.Target.GetHandle(), s.Source.GetHandle(), s.Results.GetHandle(), s.Skip.GetHandle()});
    }
}