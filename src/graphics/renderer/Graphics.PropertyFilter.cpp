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
        // Retained device buffers: a run grows only an undersized one and rewrites every input it
        // reads, so nothing of an earlier run's data or capacity is observable.
        enum Slot : std::size_t
        {
            Values, Next, Term, Sum, Edges, BaseWeights, Weights, Offsets, Incidences, Degree, Fixed, SlotMap, RestoreMap,
            SlotCount
        };
        static constexpr std::array<const char*, SlotCount> kNames{"PropertyFilter.Values", "PropertyFilter.Next",
            "PropertyFilter.Term", "PropertyFilter.Sum", "PropertyFilter.Edges", "PropertyFilter.BaseWeights",
            "PropertyFilter.Weights", "PropertyFilter.Offsets", "PropertyFilter.Incidences", "PropertyFilter.Degree",
            "PropertyFilter.Fixed", "PropertyFilter.Slots", "PropertyFilter.Restore"};
        RHI::IDevice& Device;
        RHI::PipelineHandle Pipeline{};
        std::array<RHI::BufferHandle, SlotCount> Buffers{};
        std::array<std::uint64_t, SlotCount> Capacity{};
        // The resident row map and restore mask of the current run (Record or RecordLoad starts
        // one), uploaded once and shared by its later stores; 0: none.
        bool RunStarted{};
        std::uint64_t SlotsAddress{}, RestoreAddress{};
        std::size_t SlotRows{}, RestoreRows{};
        explicit Impl(RHI::IDevice& device) : Device(device) {}
        ~Impl()
        {
            for (auto buffer : Buffers)
                if (buffer.IsValid()) Device.DestroyBuffer(buffer);
            if (Pipeline.IsValid()) Device.DestroyPipeline(Pipeline);
        }
        bool Ensure(const Slot slot, const std::uint64_t bytes)
        {
            const auto size = std::max<std::uint64_t>(bytes, 8);
            if (Buffers[slot].IsValid() && Capacity[slot] >= size) return true;
            if (Buffers[slot].IsValid()) Device.DestroyBuffer(Buffers[slot]);
            Buffers[slot] = Device.CreateBuffer({.SizeBytes = size,
                .Usage = RHI::BufferUsage::Storage | RHI::BufferUsage::TransferSrc | RHI::BufferUsage::TransferDst,
                .HostVisible = true, .DebugName = kNames[slot]});
            Capacity[slot] = Buffers[slot].IsValid() ? size : 0u;
            return Buffers[slot].IsValid();
        }
        bool Write(const Slot slot, const void* data, const std::size_t bytes)
        {
            if (!Ensure(slot, bytes)) return false;
            if (data && bytes) Device.WriteBuffer(Buffers[slot], data, bytes, 0u);
            return true;
        }
        std::uint64_t Address(const Slot slot) const { return Device.GetBufferDeviceAddress(Buffers[slot]); }
        bool EnsurePipeline()
        {
            if (!Pipeline.IsValid())
                Pipeline = CreateComputePipeline(Device, "shaders/property_filter.comp.spv", sizeof(Push), "PropertyFilter");
            return Pipeline.IsValid();
        }
        // Starts a run's resident endpoints: its row map and restore mask replace the previous run's.
        bool BeginRun(const PropertyFilterResidentIo& io)
        {
            RunStarted = false;
            SlotsAddress = RestoreAddress = 0u;
            SlotRows = io.Slots.size();
            RestoreRows = io.RestoreMask.size();
            if (!io.Slots.empty())
            {
                if (!Write(SlotMap, io.Slots.data(), io.Slots.size_bytes())) return false;
                SlotsAddress = Address(SlotMap);
            }
            if (!io.RestoreMask.empty())
            {
                if (!Write(RestoreMap, io.RestoreMask.data(), io.RestoreMask.size_bytes())) return false;
                RestoreAddress = Address(RestoreMap);
            }
            RunStarted = true;
            return true;
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
        // Load or Store between a working block and the resident property (see the shader), over
        // the current run's row map; a store whose endpoints do not match that run is refused.
        bool Move(RHI::ICommandContext& commands, const PropertyFilterResidentIo& io, const std::uint32_t channels,
                  const RHI::BufferHandle working, const std::uint64_t workingAddress,
                  const std::uint32_t rowStride, const std::uint32_t channelStride, const bool store)
        {
            const auto rows = Rows(io);
            const auto& property = store ? io.Output : io.Input;
            if (!RunStarted || io.Slots.size() != SlotRows || io.RestoreMask.size() != RestoreRows ||
                !property.Valid() || rows == 0u || channels < 1u || channels > 4u || !working.IsValid() || !workingAddress ||
                !Device.IsOperational() || !EnsurePipeline())
                return false;
            const bool restore = store && RestoreAddress && io.Input.Valid();
            if (store)
            {
                if (restore)
                    commands.BufferBarrier(io.Input.Buffer, RHI::MemoryAccess::TransferWrite | kShaderAccess, RHI::MemoryAccess::ShaderRead);
                PrepareTarget(commands, io.Output, io.OutputBytes, io.Base);
                PrepareTarget(commands, io.Presentation, io.PresentationBytes);
                commands.BufferBarrier(working, kShaderAccess | RHI::MemoryAccess::TransferRead, RHI::MemoryAccess::ShaderRead);
            }
            else
            {
                commands.BufferBarrier(io.Input.Buffer, RHI::MemoryAccess::TransferWrite | kShaderAccess, RHI::MemoryAccess::ShaderRead);
                commands.BufferBarrier(working, RHI::MemoryAccess::TransferWrite | kShaderAccess, RHI::MemoryAccess::ShaderWrite);
            }
            if (SlotsAddress)
                commands.BufferBarrier(Buffers[SlotMap], RHI::MemoryAccess::TransferWrite | RHI::MemoryAccess::ShaderRead, RHI::MemoryAccess::ShaderRead);
            if (restore)
                commands.BufferBarrier(Buffers[RestoreMap], RHI::MemoryAccess::TransferWrite | RHI::MemoryAccess::ShaderRead, RHI::MemoryAccess::ShaderRead);
            commands.BindPipeline(Pipeline);
            Push push{.Src = store ? workingAddress : io.Input.Address, .Dst = store ? io.Output.Address : workingAddress,
                      .Degree = restore ? io.Input.Address : 0u, .Fixed = restore ? RestoreAddress : 0u, .Slots = SlotsAddress,
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
        return m_Impl->BeginRun(resident) &&
               m_Impl->Move(commands, resident, channels, target, targetAddress, rowStride, channelStride, false);
    }

    RHI::BufferHandle PropertyFilterWorkspace::Record(RHI::ICommandContext& commands,
        const PropertyFilterGpuInput& input, const PropertyFilterGpuParams& p, const PropertyFilterResidentIo* resident)
    {
        auto& s = *m_Impl;
        s.RunStarted = false; // a refused run leaves no earlier run's row map for RecordStore
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
        using I = Impl;
        const std::size_t valueBytes = rows * C * sizeof(double);
        // Bilateral passes rebuild the weights and degree from the base weights before every
        // apply; the other filters read the base weights and the uploaded degree directly.
        const bool bilateral = p.Method == PropertyFilterGpuMethod::Bilateral;
        if (!s.Write(I::Values, residentInput ? nullptr : input.Values.data(), valueBytes) ||
            !s.Ensure(I::Next, valueBytes) || !s.Ensure(I::Term, valueBytes) || !s.Ensure(I::Sum, valueBytes) ||
            !s.Write(I::Edges, input.Edges.data(), input.Edges.size_bytes()) ||
            !s.Write(I::BaseWeights, input.Weights.data(), input.Weights.size_bytes()) ||
            (bilateral && !s.Ensure(I::Weights, input.Weights.size_bytes())) ||
            !s.Write(I::Offsets, offsets.data(), offsets.size() * 4) ||
            !s.Write(I::Incidences, incidences.data(), incidences.size() * 4) ||
            !s.Write(I::Degree, bilateral ? nullptr : input.Degree.data(), input.Degree.size_bytes()) ||
            !s.Write(I::Fixed, input.Fixed.data(), input.Fixed.size_bytes()) ||
            (resident && !s.BeginRun(*resident)))
            return {};
        const auto address = [&](RHI::BufferHandle b) { return s.Device.GetBufferDeviceAddress(b); };
        const auto weights = s.Buffers[bilateral ? I::Weights : I::BaseWeights], degree = s.Buffers[I::Degree];
        Push push{.Edges = s.Address(I::Edges), .Weights = address(weights), .BaseWeights = s.Address(I::BaseWeights),
                  .Offsets = s.Address(I::Offsets), .Incidences = s.Address(I::Incidences), .Degree = address(degree),
                  .Fixed = s.Address(I::Fixed), .Rows = std::uint32_t(rows), .Channels = std::uint32_t(C),
                  .EdgeCount = std::uint32_t(edgeCount), .RandomWalk = p.RandomWalk ? 1u : 0u};
        // A reused buffer's previous reads (an earlier run's readback included) precede this run.
        for (std::size_t slot = I::Values; slot <= I::Fixed; ++slot)
            if (s.Buffers[slot].IsValid() && (slot != I::Weights || bilateral))
                commands.BufferBarrier(s.Buffers[slot], RHI::MemoryAccess::TransferWrite | RHI::MemoryAccess::TransferRead | kShaderAccess,
                                       kShaderAccess);
        const std::array values{s.Buffers[I::Values], s.Buffers[I::Next], s.Buffers[I::Term], s.Buffers[I::Sum]};
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
