module;
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <span>
#include <vector>
module Extrinsic.Graphics.FarthestPointSampling;

import Extrinsic.Graphics.ComputeParallelPrimitives;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Types;

namespace Extrinsic::Graphics
{
    namespace
    {
        constexpr std::uint32_t kGroup = 256u;
        enum Mode : std::uint32_t { Update = 0u, Partial = 1u, Final = 2u };
        struct Push
        {
            std::uint64_t Points{}, Weights{}, Clearance{}, Selected{}, Partials{}, Results{};
            std::uint32_t Count{}, Groups{}, Capacity{}, Round{};
            std::uint32_t Mode{}, Reserved{};
        };
        static_assert(sizeof(Push) == 72);
    }

    struct FarthestPointSamplingWorkspace::Impl
    {
        RHI::IDevice& Device;
        RHI::PipelineHandle Pipeline{};
        // Points, weights, clearance, selected (+ last sample), group winners, results.
        std::array<RHI::BufferHandle, 6> Buffers{};
        std::vector<double> Points{}, Weights{};
        // Rounds one chunk records for `remaining` samples: the pairs stay under the budget.
        [[nodiscard]] std::uint32_t Rounds(std::uint32_t remaining) const noexcept
        {
            return remaining == 0u ? 0u
                : std::uint32_t(std::clamp<std::uint64_t>(MaxPairsPerSubmission / N, 1u, remaining));
        }
        std::uint32_t N{0u}, Count{0u}, First{0u}, Produced{0u};
        bool Uploaded{false};
        explicit Impl(RHI::IDevice& device) : Device(device) {}
        ~Impl() { Release(); }
        void Release()
        {
            for (auto& buffer : Buffers)
                if (buffer.IsValid()) Device.DestroyBuffer(buffer);
            Buffers = {};
            if (Pipeline.IsValid()) Device.DestroyPipeline(Pipeline);
            Pipeline = {};
        }
        bool Create(std::size_t index, std::size_t bytes, const void* data, const char* name)
        {
            Buffers[index] = Device.CreateBuffer({.SizeBytes = std::max<std::size_t>(bytes, 16),
                .Usage = RHI::BufferUsage::Storage | RHI::BufferUsage::TransferSrc | RHI::BufferUsage::TransferDst,
                .HostVisible = true, .DebugName = name});
            if (!Buffers[index].IsValid()) return false;
            if (data != nullptr && bytes > 0u) Device.WriteBuffer(Buffers[index], data, bytes);
            return true;
        }
        bool Upload()
        {
            const std::uint32_t groups = (N + kGroup - 1u) / kGroup;
            std::vector<double> clearance(N, std::numeric_limits<double>::infinity());
            std::vector<std::uint32_t> selected(std::size_t(N) + 1u, 0u);
            selected[First] = 1u;
            selected[N] = First;
            std::vector<std::byte> results(ReadbackBytes(Count));
            const double first = std::numeric_limits<double>::infinity();
            std::memcpy(results.data(), &first, sizeof(first));
            std::memcpy(results.data() + std::size_t(Count) * sizeof(double), &First, sizeof(First));
            return Create(0, Points.size() * sizeof(double), Points.data(), "FarthestPoint.Points") &&
                   (Weights.empty() || Create(1, Weights.size() * sizeof(double), Weights.data(), "FarthestPoint.Weights")) &&
                   Create(2, clearance.size() * sizeof(double), clearance.data(), "FarthestPoint.Clearance") &&
                   Create(3, selected.size() * sizeof(std::uint32_t), selected.data(), "FarthestPoint.Selected") &&
                   Create(4, std::size_t(groups) * 16u, nullptr, "FarthestPoint.GroupWinners") &&
                   Create(5, results.size(), results.data(), "FarthestPoint.Results");
        }
    };

    FarthestPointSamplingWorkspace::FarthestPointSamplingWorkspace(RHI::IDevice& device)
        : m_Impl(std::make_unique<Impl>(device)) {}
    FarthestPointSamplingWorkspace::~FarthestPointSamplingWorkspace() = default;

    std::size_t FarthestPointSamplingWorkspace::ReadbackBytes(const std::uint32_t count) noexcept
    {
        return std::size_t(count) * (sizeof(double) + sizeof(std::uint32_t));
    }

    bool FarthestPointSamplingWorkspace::Begin(const FarthestPointGpuInput& input)
    {
        auto& s = *m_Impl;
        const std::size_t n = input.X.size();
        if (n == 0u || n > MaxPoints || input.Y.size() != n || input.Z.size() != n ||
            (!input.Weights.empty() && input.Weights.size() != n) || input.FirstIndex >= n ||
            input.Count == 0u || input.Count > n)
            return false;
        s.Release();
        s.N = std::uint32_t(n);
        s.Count = input.Count;
        s.First = input.FirstIndex;
        s.Produced = 0u;
        s.Uploaded = false;
        s.Points.resize(3u * n);
        for (std::size_t i = 0; i < n; ++i)
        {
            s.Points[3u * i] = input.X[i];
            s.Points[3u * i + 1u] = input.Y[i];
            s.Points[3u * i + 2u] = input.Z[i];
        }
        s.Weights.assign(input.Weights.begin(), input.Weights.end());
        return true;
    }

    RHI::BufferHandle FarthestPointSamplingWorkspace::RecordNext(RHI::ICommandContext& commands)
    {
        auto& s = *m_Impl;
        if (s.N == 0u || Finished() || !s.Device.IsOperational() || !s.Device.SupportsShaderFloat64()) return {};
        if (!s.Pipeline.IsValid())
            s.Pipeline = CreateComputePipeline(s.Device, "shaders/point_sampling_farthest.comp.spv", sizeof(Push),
                                               "FarthestPointSampling");
        if (!s.Pipeline.IsValid()) return {};
        if (!s.Uploaded)
        {
            if (!s.Upload()) return {};
            s.Uploaded = true;
            s.Produced = 1u;
        }
        const auto address = [&](std::size_t index) {
            return s.Buffers[index].IsValid() ? s.Device.GetBufferDeviceAddress(s.Buffers[index]) : 0u;
        };
        const std::uint32_t groups = (s.N + kGroup - 1u) / kGroup;
        Push push{.Points = address(0), .Weights = address(1), .Clearance = address(2), .Selected = address(3),
                  .Partials = address(4), .Results = address(5), .Count = s.N, .Groups = groups, .Capacity = s.Count};
        for (const auto buffer : s.Buffers)
            if (buffer.IsValid())
                commands.BufferBarrier(buffer, RHI::MemoryAccess::TransferWrite | RHI::MemoryAccess::ShaderRead,
                                       RHI::MemoryAccess::ShaderRead | RHI::MemoryAccess::ShaderWrite);
        commands.BindPipeline(s.Pipeline);
        const auto dispatch = [&](std::uint32_t mode, std::uint32_t groupCount) {
            push.Mode = mode;
            commands.PushConstants(&push, sizeof(push), 0);
            commands.Dispatch(groupCount, 1, 1);
            for (const std::size_t index : {std::size_t{2}, std::size_t{3}, std::size_t{4}, std::size_t{5}})
                commands.BufferBarrier(s.Buffers[index], RHI::MemoryAccess::ShaderRead | RHI::MemoryAccess::ShaderWrite,
                                       RHI::MemoryAccess::ShaderRead | RHI::MemoryAccess::ShaderWrite);
        };
        // Each round is three bounded dispatches over the points; a chunk keeps its pairs under
        // the submission budget, so no single submission runs long enough for a watchdog.
        const std::uint32_t rounds = s.Rounds(s.Count - s.Produced);
        for (std::uint32_t r = 0; r < rounds; ++r)
        {
            push.Round = s.Produced + r;
            dispatch(Update, groups);
            dispatch(Partial, groups);
            dispatch(Final, 1u);
        }
        s.Produced += rounds;
        commands.BufferBarrier(s.Buffers[5], RHI::MemoryAccess::ShaderRead | RHI::MemoryAccess::ShaderWrite,
                               RHI::MemoryAccess::TransferRead);
        return s.Buffers[5];
    }

    std::uint32_t FarthestPointSamplingWorkspace::Produced() const noexcept { return m_Impl->Produced; }

    bool FarthestPointSamplingWorkspace::NextChunkFinishes() const noexcept
    {
        const auto& s = *m_Impl;
        if (s.N == 0u) return true;
        const std::uint32_t produced = s.Uploaded ? s.Produced : 1u; // the first sample comes with the upload
        return produced + s.Rounds(s.Count - produced) >= s.Count;
    }

    bool FarthestPointSamplingWorkspace::Finished() const noexcept
    {
        return m_Impl->N != 0u && m_Impl->Uploaded && m_Impl->Produced >= m_Impl->Count;
    }
}
