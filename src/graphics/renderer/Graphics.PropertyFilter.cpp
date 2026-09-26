module;
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>
module Extrinsic.Graphics.PropertyFilter;

import Extrinsic.RHI.Device;
import Extrinsic.RHI.CommandContext;
import Extrinsic.Core.Filesystem.PathResolver;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Types;

namespace Extrinsic::Graphics
{
    namespace
    {
        enum Mode : std::uint32_t { Apply, BilateralWeights, BilateralDegree, Scale, Axpy, Divide, Copy };
        struct Push
        {
            std::uint64_t Src{}, Dst{}, Edges{}, Weights{}, BaseWeights{}, Offsets{}, Incidences{}, Degree{}, Fixed{};
            std::uint32_t Rows{}, Channels{}, EdgeCount{}, Mode{};
            std::uint32_t RandomWalk{}, Reserved{};
            double Scalar{};
        };
        static_assert(sizeof(Push) == 104 && offsetof(Push, Scalar) == 96);
    }

    struct PropertyFilterWorkspace::Impl
    {
        RHI::IDevice& Device;
        RHI::PipelineHandle Pipeline{};
        std::vector<RHI::BufferHandle> Buffers{};
        explicit Impl(RHI::IDevice& device) : Device(device) {}
        ~Impl()
        {
            for (auto buffer : Buffers)
                if (buffer.IsValid()) Device.DestroyBuffer(buffer);
            if (Pipeline.IsValid()) Device.DestroyPipeline(Pipeline);
        }
        RHI::BufferHandle Upload(const void* data, std::size_t bytes, const char* name)
        {
            const auto buffer = Device.CreateBuffer({.SizeBytes = std::max<std::size_t>(bytes, 8),
                .Usage = RHI::BufferUsage::Storage | RHI::BufferUsage::TransferSrc | RHI::BufferUsage::TransferDst,
                .HostVisible = true, .DebugName = name});
            if (!buffer.IsValid()) return {};
            Buffers.push_back(buffer);
            if (data && bytes) Device.WriteBuffer(buffer, data, bytes);
            return buffer;
        }
    };

    PropertyFilterWorkspace::PropertyFilterWorkspace(RHI::IDevice& device) : m_Impl(std::make_unique<Impl>(device)) {}
    PropertyFilterWorkspace::~PropertyFilterWorkspace() = default;

    std::uint64_t PropertyFilterWorkspace::DispatchCount(const PropertyFilterGpuParams& p)
    {
        const std::uint64_t perIteration =
            p.Method == PropertyFilterGpuMethod::SpectralHeat ? std::uint64_t(p.HeatSplits) * (3 + 2 * 18)
            : p.Method == PropertyFilterGpuMethod::Taubin ? 2
            : p.Method == PropertyFilterGpuMethod::Bilateral ? 3 : 1;
        return perIteration * p.Iterations;
    }

    RHI::BufferHandle PropertyFilterWorkspace::Record(RHI::ICommandContext& commands,
        const PropertyFilterGpuInput& input, const PropertyFilterGpuParams& p)
    {
        auto& s = *m_Impl;
        const std::size_t C = input.Channels;
        if (C < 1 || C > 4 || input.Values.empty() || input.Values.size() % C) return {};
        const std::size_t rows = input.Values.size() / C, edgeCount = input.Weights.size();
        if (rows > (1u << 24) || edgeCount > (1u << 26) || input.Edges.size() != 2 * edgeCount ||
            input.Degree.size() != rows || input.Fixed.size() != rows ||
            DispatchCount(p) > MaxDispatches || !s.Device.IsOperational() || !s.Device.SupportsShaderFloat64())
            return {};
        if (!s.Pipeline.IsValid())
        {
            const auto path = Core::Filesystem::GetShaderPath("shaders/property_filter.comp.spv");
            s.Pipeline = s.Device.CreatePipeline({.VertexShaderPath = {}, .FragmentShaderPath = {},
                .ComputeShaderPath = path.c_str(), .PushConstantSize = sizeof(Push), .DebugName = "PropertyFilter"});
            if (!s.Pipeline.IsValid()) return {};
        }
        // Incidences per row in ascending edge order, the order the CPU reference scatters them.
        std::vector<std::uint32_t> offsets(rows + 1, 0), incidences(2 * edgeCount);
        for (std::size_t e = 0; e < edgeCount; ++e)
        {
            if (input.Edges[2 * e] >= rows || input.Edges[2 * e + 1] >= rows) return {};
            ++offsets[input.Edges[2 * e] + 1];
            ++offsets[input.Edges[2 * e + 1] + 1];
        }
        for (std::size_t i = 0; i < rows; ++i) offsets[i + 1] += offsets[i];
        std::vector<std::uint32_t> cursor(offsets.begin(), offsets.end() - 1);
        for (std::uint32_t e = 0; e < edgeCount; ++e)
        {
            incidences[cursor[input.Edges[2 * e]]++] = e;
            incidences[cursor[input.Edges[2 * e + 1]]++] = e;
        }
        const std::size_t valueBytes = input.Values.size() * sizeof(double);
        const std::array values{s.Upload(input.Values.data(), valueBytes, "PropertyFilter.Values"),
                                s.Upload(nullptr, valueBytes, "PropertyFilter.Next"),
                                s.Upload(nullptr, valueBytes, "PropertyFilter.Term"),
                                s.Upload(nullptr, valueBytes, "PropertyFilter.Sum")};
        const auto edges = s.Upload(input.Edges.data(), input.Edges.size_bytes(), "PropertyFilter.Edges");
        const auto base = s.Upload(input.Weights.data(), input.Weights.size_bytes(), "PropertyFilter.BaseWeights");
        const auto weights = s.Upload(input.Weights.data(), input.Weights.size_bytes(), "PropertyFilter.Weights");
        const auto offsetBuffer = s.Upload(offsets.data(), offsets.size() * 4, "PropertyFilter.Offsets");
        const auto incidenceBuffer = s.Upload(incidences.data(), incidences.size() * 4, "PropertyFilter.Incidences");
        const auto degree = s.Upload(input.Degree.data(), input.Degree.size_bytes(), "PropertyFilter.Degree");
        const auto fixed = s.Upload(input.Fixed.data(), input.Fixed.size_bytes(), "PropertyFilter.Fixed");
        if (std::ranges::any_of(s.Buffers, [](auto b) { return !b.IsValid(); }) || s.Buffers.size() != 11) return {};
        const auto address = [&](RHI::BufferHandle b) { return s.Device.GetBufferDeviceAddress(b); };
        Push push{.Edges = address(edges), .Weights = address(weights), .BaseWeights = address(base),
                  .Offsets = address(offsetBuffer), .Incidences = address(incidenceBuffer), .Degree = address(degree),
                  .Fixed = address(fixed), .Rows = std::uint32_t(rows), .Channels = std::uint32_t(C),
                  .EdgeCount = std::uint32_t(edgeCount), .RandomWalk = p.RandomWalk ? 1u : 0u};
        for (auto buffer : s.Buffers)
            commands.BufferBarrier(buffer, RHI::MemoryAccess::TransferWrite | RHI::MemoryAccess::ShaderRead,
                                   RHI::MemoryAccess::ShaderRead | RHI::MemoryAccess::ShaderWrite);
        commands.BindPipeline(s.Pipeline);
        const auto dispatch = [&](std::uint32_t mode, RHI::BufferHandle src, RHI::BufferHandle dst, double scalar) {
            push.Mode = mode;
            push.Src = address(src);
            push.Dst = address(dst);
            push.Scalar = scalar;
            commands.PushConstants(&push, sizeof(push), 0);
            const auto threads = mode == BilateralWeights ? edgeCount : rows;
            commands.Dispatch(std::uint32_t((threads + 63) / 64), 1, 1);
            for (auto buffer : {dst, weights, degree})
                commands.BufferBarrier(buffer, RHI::MemoryAccess::ShaderRead | RHI::MemoryAccess::ShaderWrite,
                                       RHI::MemoryAccess::ShaderRead | RHI::MemoryAccess::ShaderWrite);
        };
        // current and next ping-pong between the first two value buffers; spectral heat also uses
        // a term pair (term, next) and a running sum.
        RHI::BufferHandle current = values[0], next = values[1], term = values[2], sum = values[3];
        for (std::uint32_t iteration = 0; iteration < p.Iterations; ++iteration)
        {
            if (p.Method == PropertyFilterGpuMethod::SpectralHeat)
            {
                for (std::uint32_t split = 0; split < p.HeatSplits; ++split)
                {
                    dispatch(Copy, current, term, 0.0);
                    dispatch(Scale, term, sum, p.HeatCoefficients[0]);
                    for (std::size_t k = 1; k <= 18; ++k)
                    {
                        dispatch(Apply, term, next, p.HeatStep);
                        std::swap(term, next);
                        dispatch(Axpy, term, sum, p.HeatCoefficients[k]);
                    }
                    dispatch(Divide, sum, current, p.HeatMass);
                }
                continue;
            }
            if (p.Method == PropertyFilterGpuMethod::Bilateral)
            {
                dispatch(BilateralWeights, current, weights, p.RangeSigma);
                dispatch(BilateralDegree, current, degree, 0.0);
            }
            dispatch(Apply, current, next, p.Step);
            std::swap(current, next);
            if (p.Method == PropertyFilterGpuMethod::Taubin)
            {
                dispatch(Apply, current, next, p.TaubinStep);
                std::swap(current, next);
            }
        }
        commands.BufferBarrier(current, RHI::MemoryAccess::ShaderRead | RHI::MemoryAccess::ShaderWrite,
                               RHI::MemoryAccess::TransferRead);
        return current;
    }
}
