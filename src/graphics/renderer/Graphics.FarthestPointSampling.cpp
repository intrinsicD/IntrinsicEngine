module;
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
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
        enum Mode : std::uint32_t { Update = 0u, Partial = 1u, Final = 2u, Initialize = 3u };
        struct Push
        {
            std::uint64_t Points{}, Weights{}, Clearance{}, Selected{}, Partials{}, Results{};
            std::uint32_t Count{}, Groups{}, Capacity{}, Round{};
            std::uint32_t Mode{}, First{};
            std::uint64_t SourcePoints{}, SourceWeights{}, Rows{}, Model{};
            std::uint32_t WeightBytes{}, Reserved{};
        };
        static_assert(sizeof(Push) == 112);
    }

    struct FarthestPointSamplingWorkspace::Impl
    {
        RHI::IDevice& Device;
        RHI::PipelineHandle Pipeline{};
        // Working points/weights, clearance, selection, winners, results, row map and transform.
        std::array<RHI::BufferHandle, 8> Buffers{};
        std::array<std::size_t, 8> Capacity{};
        GpuPropertyView Positions{}, Weights{};
        std::vector<std::uint32_t> Rows{};
        std::array<double, 16> Model{};
        // Rounds one chunk records for `remaining` samples: the pairs stay under the budget.
        [[nodiscard]] std::uint32_t Rounds(std::uint32_t remaining) const noexcept
        {
            return remaining == 0u ? 0u
                : std::uint32_t(std::clamp<std::uint64_t>(MaxPairsPerSubmission / N, 1u, remaining));
        }
        std::uint32_t N{0u}, Count{0u}, First{0u}, Produced{0u};
        bool Initialized{false};
        explicit Impl(RHI::IDevice& device) : Device(device) {}
        ~Impl() { Release(); }
        void Release()
        {
            for (auto& buffer : Buffers)
                if (buffer.IsValid()) Device.DestroyBuffer(buffer);
            Buffers = {};
            Capacity = {};
            if (Pipeline.IsValid()) Device.DestroyPipeline(Pipeline);
            Pipeline = {};
        }
        bool Create(std::size_t index, std::size_t bytes, const void* data, const char* name)
        {
            const auto required = std::max<std::size_t>(bytes, 16);
            if (!Buffers[index].IsValid() || Capacity[index] < required)
            {
                const auto replacement = Device.CreateBuffer({.SizeBytes = required,
                    .Usage = RHI::BufferUsage::Storage | RHI::BufferUsage::TransferSrc | RHI::BufferUsage::TransferDst,
                    .HostVisible = true, .DebugName = name});
                if (!replacement.IsValid()) return false;
                if (Buffers[index].IsValid()) Device.DestroyBuffer(Buffers[index]);
                Buffers[index] = replacement;
                Capacity[index] = required;
            }
            if (data != nullptr && bytes > 0u) Device.WriteBuffer(Buffers[index], data, bytes);
            return true;
        }
        bool Allocate()
        {
            const std::uint32_t groups = (N + kGroup - 1u) / kGroup;
            return Create(0, std::size_t(N) * 3u * sizeof(double), nullptr, "FarthestPoint.Points") &&
                   (!Weights.Valid() || Create(1, std::size_t(N) * sizeof(double), nullptr, "FarthestPoint.Weights")) &&
                   Create(2, std::size_t(N) * sizeof(double), nullptr, "FarthestPoint.Clearance") &&
                   Create(3, (std::size_t(N) + 1u) * sizeof(std::uint32_t), nullptr, "FarthestPoint.Selected") &&
                   Create(4, std::size_t(groups) * 16u, nullptr, "FarthestPoint.GroupWinners") &&
                   Create(5, ReadbackBytes(Count), nullptr, "FarthestPoint.Results") &&
                   (Rows.empty() || Create(6, Rows.size() * sizeof(std::uint32_t), Rows.data(), "FarthestPoint.Rows")) &&
                   Create(7, sizeof(Model), Model.data(), "FarthestPoint.Model");
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
        const auto& positions = input.Positions;
        const auto& weights = input.Weights;
        const std::size_t n = input.Rows.empty() ? positions.Layout.Count : input.Rows.size();
        if (!positions.Valid() || !positions.Address || positions.Layout.Scalar != GpuScalarType::Float32 ||
            positions.Bytes < positions.Layout.Bytes() || positions.Layout.Channels != 3u || positions.Layout.ElementBytes() != 12u || positions.Layout.RowMap != 0u ||
            n == 0u || n > MaxPoints || input.FirstIndex >= n || input.Count == 0u || input.Count > n ||
            std::ranges::any_of(input.Rows, [&](auto row) { return row >= positions.Layout.Count; }))
            return false;
        if (weights.Valid() && (weights.Bytes < weights.Layout.Bytes() || !weights.Address || weights.Layout.Count != positions.Layout.Count ||
            weights.Layout.Channels != 1u || weights.Layout.RowMap != 0u ||
            (weights.Layout.Scalar != GpuScalarType::Float32 && weights.Layout.Scalar != GpuScalarType::Float64) ||
            weights.Layout.ElementBytes() != GpuScalarBytes(weights.Layout.Scalar))) return false;
        s.N = std::uint32_t(n);
        s.Count = input.Count;
        s.First = input.FirstIndex;
        s.Produced = 0u;
        s.Initialized = false;
        s.Positions = positions;
        s.Weights = weights;
        s.Rows.assign(input.Rows.begin(), input.Rows.end());
        s.Model = input.Model;
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
        const bool initialize = !s.Initialized;
        if (initialize)
        {
            if (!s.Allocate()) return {};
            s.Initialized = true;
            s.Produced = 1u;
        }
        const auto address = [&](std::size_t index) {
            return s.Buffers[index].IsValid() ? s.Device.GetBufferDeviceAddress(s.Buffers[index]) : 0u;
        };
        const std::uint32_t groups = (s.N + kGroup - 1u) / kGroup;
        Push push{.Points = address(0), .Weights = s.Weights.Valid() ? address(1) : 0u, .Clearance = address(2), .Selected = address(3),
                  .Partials = address(4), .Results = address(5), .Count = s.N, .Groups = groups, .Capacity = s.Count,
                  .First = s.First, .SourcePoints = s.Positions.Address, .SourceWeights = s.Weights.Address,
                  .Rows = s.Rows.empty() ? 0u : address(6), .Model = address(7),
                  .WeightBytes = s.Weights.Valid() ? GpuScalarBytes(s.Weights.Layout.Scalar) : 0u};
        for (const auto buffer : s.Buffers)
            if (buffer.IsValid())
                commands.BufferBarrier(buffer, RHI::MemoryAccess::TransferWrite | RHI::MemoryAccess::TransferRead | RHI::MemoryAccess::ShaderRead | RHI::MemoryAccess::ShaderWrite,
                                       RHI::MemoryAccess::ShaderRead | RHI::MemoryAccess::ShaderWrite);
        for (const auto& view : {s.Positions, s.Weights})
            if (view.Valid()) commands.BufferBarrier(view.Buffer, RHI::MemoryAccess::TransferWrite | RHI::MemoryAccess::ShaderWrite,
                                                     RHI::MemoryAccess::ShaderRead);
        commands.BindPipeline(s.Pipeline);
        const auto dispatch = [&](std::uint32_t mode, std::uint32_t groupCount) {
            push.Mode = mode;
            commands.PushConstants(&push, sizeof(push), 0);
            commands.Dispatch(groupCount, 1, 1);
            for (const std::size_t index : {std::size_t{2}, std::size_t{3}, std::size_t{4}, std::size_t{5}})
                commands.BufferBarrier(s.Buffers[index], RHI::MemoryAccess::ShaderRead | RHI::MemoryAccess::ShaderWrite,
                                       RHI::MemoryAccess::ShaderRead | RHI::MemoryAccess::ShaderWrite);
        };
        if (initialize)
        {
            dispatch(Initialize, groups);
            for (const auto index : {0u, 1u})
                if (s.Buffers[index].IsValid()) commands.BufferBarrier(s.Buffers[index],
                    RHI::MemoryAccess::ShaderWrite, RHI::MemoryAccess::ShaderRead);
        }
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
        const std::uint32_t produced = s.Initialized ? s.Produced : 1u; // initialization writes the first sample
        return produced + s.Rounds(s.Count - produced) >= s.Count;
    }

    bool FarthestPointSamplingWorkspace::Finished() const noexcept
    {
        return m_Impl->N != 0u && m_Impl->Initialized && m_Impl->Produced >= m_Impl->Count;
    }
}
