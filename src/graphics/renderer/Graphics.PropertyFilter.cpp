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

import Extrinsic.Graphics.ComputeParallelPrimitives;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Types;

namespace Extrinsic::Graphics
{
    namespace
    {
        enum Mode : std::uint32_t { Apply, BilateralWeights, BilateralDegree, Scale, Axpy, Divide, Copy, Load, Store };
        enum Scalar : std::uint32_t { Float32, Float64 };
        struct Push
        {
            std::uint64_t Src{}, Dst{}, Edges{}, Weights{}, BaseWeights{}, Offsets{}, Incidences{}, Degree{}, Fixed{};
            std::uint64_t Slots{}, Presentation{};
            std::uint32_t Rows{}, Channels{}, EdgeCount{}, Mode{};
            std::uint32_t RandomWalk{}, ScalarType{}, RowStride{}, ChannelStride{};
            double Scalar{};
        };
        static_assert(sizeof(Push) == 128 && offsetof(Push, Scalar) == 120);
        constexpr auto kShaderAccess = RHI::MemoryAccess::ShaderRead | RHI::MemoryAccess::ShaderWrite;
    }

    struct PropertyFilterWorkspace::Impl
    {
        RHI::IDevice& Device;
        RHI::PipelineHandle Pipeline{};
        std::vector<RHI::BufferHandle> Buffers{};
        RHI::BufferHandle Slots{}, Restore{}; // the resident row map and restore mask, uploaded once
        std::size_t SlotCount{}, RestoreCount{};
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
        bool EnsurePipeline()
        {
            if (!Pipeline.IsValid())
                Pipeline = CreateComputePipeline(Device, "shaders/property_filter.comp.spv", sizeof(Push), "PropertyFilter");
            return Pipeline.IsValid();
        }
        // A per-row uint map, uploaded on first use (0: none).
        std::uint64_t MapAddress(RHI::BufferHandle& buffer, std::size_t& count, const std::span<const std::uint32_t> map,
                                 const char* name)
        {
            if (map.empty()) return 0u;
            if (!buffer.IsValid() || count != map.size())
            {
                buffer = Upload(map.data(), map.size_bytes(), name);
                count = map.size();
            }
            return buffer.IsValid() ? Device.GetBufferDeviceAddress(buffer) : 0u;
        }
        std::uint64_t SlotsAddress(const std::span<const std::uint32_t> slots)
        {
            return MapAddress(Slots, SlotCount, slots, "PropertyFilter.Slots");
        }
        static std::uint32_t Rows(const PropertyFilterResidentIo& io)
        {
            return io.Slots.empty() ? io.Rows : std::uint32_t(io.Slots.size());
        }
        // Fills a resident target from its base (or zeroes it) and hands it to the compute stage.
        void PrepareTarget(RHI::ICommandContext& commands, const PropertyFilterResidentView& view, const std::uint64_t bytes,
                           const PropertyFilterResidentView& base = {})
        {
            if (!view.Valid()) return;
            if (bytes)
            {
                commands.BufferBarrier(view.Buffer, kShaderAccess | RHI::MemoryAccess::TransferRead, RHI::MemoryAccess::TransferWrite);
                if (base.Valid() && base.Buffer != view.Buffer)
                {
                    commands.BufferBarrier(base.Buffer, RHI::MemoryAccess::TransferWrite | kShaderAccess, RHI::MemoryAccess::TransferRead);
                    commands.CopyBuffer(base.Buffer, view.Buffer, 0u, 0u, bytes);
                }
                else
                    commands.FillBuffer(view.Buffer, 0u, bytes, 0u);
                commands.BufferBarrier(view.Buffer, RHI::MemoryAccess::TransferWrite, kShaderAccess);
            }
            else
                commands.BufferBarrier(view.Buffer, kShaderAccess | RHI::MemoryAccess::TransferRead, kShaderAccess);
        }
        // Load or Store between a working block and the resident property (see the shader).
        bool Move(RHI::ICommandContext& commands, const PropertyFilterResidentIo& io, const std::uint32_t channels,
                  const RHI::BufferHandle working, const std::uint64_t workingAddress,
                  const std::uint32_t rowStride, const std::uint32_t channelStride, const bool store)
        {
            const auto rows = Rows(io);
            const auto& property = store ? io.Output : io.Input;
            if (!property.Valid() || rows == 0u || channels < 1u || channels > 4u || !working.IsValid() || !workingAddress ||
                !Device.IsOperational() || !EnsurePipeline())
                return false;
            const auto slots = SlotsAddress(io.Slots);
            if (!io.Slots.empty() && !slots) return false;
            std::uint64_t restore = 0u, restoreMask = 0u;
            if (store)
            {
                if (!io.RestoreMask.empty() && io.Input.Valid())
                {
                    restoreMask = MapAddress(Restore, RestoreCount, io.RestoreMask, "PropertyFilter.Restore");
                    if (!restoreMask) return false;
                    restore = io.Input.Address;
                    commands.BufferBarrier(io.Input.Buffer, RHI::MemoryAccess::TransferWrite | kShaderAccess, RHI::MemoryAccess::ShaderRead);
                }
                PrepareTarget(commands, io.Output, io.OutputBytes, io.Base);
                PrepareTarget(commands, io.Presentation, io.PresentationBytes);
                commands.BufferBarrier(working, kShaderAccess | RHI::MemoryAccess::TransferRead, RHI::MemoryAccess::ShaderRead);
            }
            else
            {
                commands.BufferBarrier(io.Input.Buffer, RHI::MemoryAccess::TransferWrite | kShaderAccess, RHI::MemoryAccess::ShaderRead);
                commands.BufferBarrier(working, RHI::MemoryAccess::TransferWrite | kShaderAccess, RHI::MemoryAccess::ShaderWrite);
            }
            for (const auto map : {Slots, Restore})
                if (map.IsValid())
                    commands.BufferBarrier(map, RHI::MemoryAccess::TransferWrite | RHI::MemoryAccess::ShaderRead, RHI::MemoryAccess::ShaderRead);
            commands.BindPipeline(Pipeline);
            Push push{.Src = store ? workingAddress : io.Input.Address, .Dst = store ? io.Output.Address : workingAddress,
                      .Degree = restore, .Fixed = restoreMask, .Slots = slots,
                      .Presentation = store && io.Presentation.Valid() ? io.Presentation.Address : 0u,
                      .Rows = rows, .Channels = channels, .Mode = store ? Mode::Store : Mode::Load,
                      .RandomWalk = io.Input.Double ? Scalar::Float64 : Scalar::Float32,
                      .ScalarType = property.Double ? Scalar::Float64 : Scalar::Float32,
                      .RowStride = rowStride, .ChannelStride = channelStride};
            commands.PushConstants(&push, sizeof(push), 0);
            commands.Dispatch((rows + 63u) / 64u, 1, 1);
            if (store)
            {
                // Ready for the renderer's reads and the Accept readback.
                for (const auto& view : {io.Output, io.Presentation})
                    if (view.Valid())
                        commands.BufferBarrier(view.Buffer, RHI::MemoryAccess::ShaderWrite,
                                               RHI::MemoryAccess::ShaderRead | RHI::MemoryAccess::TransferRead);
            }
            else
                commands.BufferBarrier(working, RHI::MemoryAccess::ShaderWrite, kShaderAccess);
            return true;
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

    bool PropertyFilterWorkspace::RecordStore(RHI::ICommandContext& commands, const PropertyFilterResidentIo& resident,
        const std::uint32_t channels, const RHI::BufferHandle source, const std::uint64_t sourceAddress,
        const std::uint32_t rowStride, const std::uint32_t channelStride)
    {
        return m_Impl->Move(commands, resident, channels, source, sourceAddress, rowStride, channelStride, true);
    }

    bool PropertyFilterWorkspace::RecordLoad(RHI::ICommandContext& commands, const PropertyFilterResidentIo& resident,
        const std::uint32_t channels, const RHI::BufferHandle target, const std::uint64_t targetAddress,
        const std::uint32_t rowStride, const std::uint32_t channelStride)
    {
        return m_Impl->Move(commands, resident, channels, target, targetAddress, rowStride, channelStride, false);
    }

    RHI::BufferHandle PropertyFilterWorkspace::Record(RHI::ICommandContext& commands,
        const PropertyFilterGpuInput& input, const PropertyFilterGpuParams& p, const PropertyFilterResidentIo* resident)
    {
        auto& s = *m_Impl;
        const std::size_t C = input.Channels;
        if (C < 1 || C > 4 || input.Values.size() % C) return {};
        const bool residentInput = resident && resident->Input.Valid();
        const std::size_t rows = residentInput ? Impl::Rows(*resident) : input.Values.size() / C;
        const std::size_t edgeCount = input.Weights.size();
        if (rows == 0 || (residentInput ? !input.Values.empty() && input.Values.size() != rows * C : input.Values.empty()) ||
            (resident && !resident->Output.Valid()))
            return {};
        if (rows > (1u << 24) || edgeCount > (1u << 26) || input.Edges.size() != 2 * edgeCount ||
            input.Degree.size() != rows || input.Fixed.size() != rows ||
            DispatchCount(p) > MaxDispatches || !s.Device.IsOperational() || !s.Device.SupportsShaderFloat64())
            return {};
        if (!s.EnsurePipeline()) return {};
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
        const std::size_t valueBytes = rows * C * sizeof(double);
        const std::array values{s.Upload(residentInput ? nullptr : input.Values.data(), valueBytes, "PropertyFilter.Values"),
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
        const std::uint64_t slots = residentInput ? s.SlotsAddress(resident->Slots) : 0u;
        if (std::ranges::any_of(s.Buffers, [](auto b) { return !b.IsValid(); }) || (residentInput && !resident->Slots.empty() && !slots))
            return {};
        const auto address = [&](RHI::BufferHandle b) { return s.Device.GetBufferDeviceAddress(b); };
        Push push{.Edges = address(edges), .Weights = address(weights), .BaseWeights = address(base),
                  .Offsets = address(offsetBuffer), .Incidences = address(incidenceBuffer), .Degree = address(degree),
                  .Fixed = address(fixed), .Rows = std::uint32_t(rows), .Channels = std::uint32_t(C),
                  .EdgeCount = std::uint32_t(edgeCount), .RandomWalk = p.RandomWalk ? 1u : 0u};
        for (auto buffer : s.Buffers)
            commands.BufferBarrier(buffer, RHI::MemoryAccess::TransferWrite | RHI::MemoryAccess::ShaderRead,
                                   RHI::MemoryAccess::ShaderRead | RHI::MemoryAccess::ShaderWrite);
        // Resident input: gathered on the device into the working layout (uploaded once per revision).
        if (residentInput && !s.Move(commands, *resident, std::uint32_t(C), values[0], address(values[0]), std::uint32_t(C), 1u, false))
            return {};
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
        if (resident && !s.Move(commands, *resident, std::uint32_t(C), current, address(current), std::uint32_t(C), 1u, true))
            return {};
        return current;
    }
}
