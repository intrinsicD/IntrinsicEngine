module;
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <vector>
module Extrinsic.Graphics.SparseConjugateGradient;

import Extrinsic.Graphics.ComputeParallelPrimitives;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Types;

namespace Extrinsic::Graphics
{
    namespace
    {
        enum Mode : std::uint32_t
        {
            Diagonal, Initialize, InitialDots, InitialScalar, Spmv, DotPAp, Alpha, UpdateXR, DotRR,
            Residual, Precondition, Beta, UpdateP, Finalize, ChainRhs
        };
        struct Push
        {
            std::uint64_t Offsets{}, Columns{}, Values{}, B{}, X{}, State{}, Work{}, Partials{},
                RhsDiagonal{}, RhsConstant{}, Previous{};
            std::uint32_t Rows{}, Mode{}, Groups{}, Reserved{};
            double Tolerance{};
        };
        static_assert(sizeof(Push) == 112 && offsetof(Push, Tolerance) == 104);
        static_assert(sizeof(SparseCgReport) == 80);
        constexpr std::uint32_t kGroup = 256;
        constexpr std::uint32_t kIterationDispatches = 9;
    }

    struct SparseConjugateGradientWorkspace::Impl
    {
        // Retained device buffers: a run grows only an undersized one and writes every region it
        // reads (host uploads, or device passes before their first read).
        enum Slot : std::size_t { OffsetSlot, ColumnSlot, ValueSlot, RhsSlot, DiagonalSlot, ConstantSlot, WorkSlot,
                                  PartialSlot, ResultSlot, SlotCount };
        static constexpr std::array<const char*, SlotCount> kNames{"SparseCG.RowOffsets", "SparseCG.Columns",
            "SparseCG.Values", "SparseCG.RightHandSides", "SparseCG.RhsDiagonal", "SparseCG.RhsConstant", "SparseCG.Work",
            "SparseCG.Partials", "SparseCG.Result"};
        RHI::IDevice& Device;
        RHI::PipelineHandle Pipeline{};
        std::array<RHI::BufferHandle, SlotCount> Buffers{};
        std::array<std::uint64_t, SlotCount> Capacity{};
        // Owned copy of the problem.
        std::uint32_t Rows{}, Solves{}, ChainStride{}, MaxIterations{}, Groups{};
        double Tolerance{};
        std::vector<std::uint32_t> Offsets, Columns;
        std::vector<double> Values, Rhs, Guesses, RhsDiagonal, RhsConstant;
        Push Base{};
        // Progress: the solve being recorded, whether it started, and its recorded iterations.
        std::uint32_t Solve{}, Iteration{}, ChunkCount{};
        bool Started{}, Uploaded{}, Failed{}, SeedsOnDevice{};

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
            if (!Buffers[slot].IsValid()) Failed = true;
            return Buffers[slot].IsValid();
        }
        bool Write(const Slot slot, const void* data, const std::size_t bytes, const std::uint64_t capacity)
        {
            if (!Ensure(slot, capacity)) return false;
            if (data && bytes) Device.WriteBuffer(Buffers[slot], data, bytes, 0u);
            return true;
        }
        std::uint64_t Address(RHI::BufferHandle b) const { return Device.GetBufferDeviceAddress(b); }
        std::uint64_t Address(const Slot slot) const { return Address(Buffers[slot]); }
        std::size_t ReportBytes() const { return std::size_t(Solves) * sizeof(SparseCgReport); }

        bool Upload()
        {
            if (!Pipeline.IsValid())
            {
                Pipeline = CreateComputePipeline(Device, "shaders/sparse_cg.comp.spv", sizeof(Push), "SparseConjugateGradient");
                if (!Pipeline.IsValid()) return false;
            }
            const std::uint64_t n = Rows;
            const auto bytes = [](const auto& v) { return v.size() * sizeof(v[0]); };
            // Host right-hand sides cover only the unchained (or first ChainStride) solves; every
            // later one, and every device-seeded one, is formed by ChainRhs before it is read.
            if (!Write(OffsetSlot, Offsets.data(), bytes(Offsets), bytes(Offsets)) ||
                !Write(ColumnSlot, Columns.data(), bytes(Columns), bytes(Columns)) ||
                !Write(ValueSlot, Values.data(), bytes(Values), bytes(Values)) ||
                !Write(RhsSlot, Rhs.data(), bytes(Rhs), std::uint64_t(Solves) * n * 8) ||
                !Write(DiagonalSlot, RhsDiagonal.data(), bytes(RhsDiagonal), bytes(RhsDiagonal)) ||
                !Write(ConstantSlot, RhsConstant.data(), bytes(RhsConstant), bytes(RhsConstant)) ||
                !Ensure(WorkSlot, 5 * n * 8) || !Ensure(PartialSlot, 3 * std::uint64_t(Groups) * 8) ||
                !Ensure(ResultSlot, ReadbackBytes(Rows, Solves)))
                return false;
            // Reports start zeroed every run; each solve's solution starts at its initial guess
            // (host guesses, a device seed, or the chained previous solution).
            const std::vector<std::byte> reports(ReportBytes(), std::byte{0});
            Device.WriteBuffer(Buffers[ResultSlot], reports.data(), reports.size(), 0u);
            if (!Guesses.empty()) Device.WriteBuffer(Buffers[ResultSlot], Guesses.data(), bytes(Guesses), ReportBytes());
            Base = {.Offsets = Address(OffsetSlot), .Columns = Address(ColumnSlot), .Values = Address(ValueSlot),
                    .Work = Address(WorkSlot), .Partials = Address(PartialSlot), .Rows = Rows, .Groups = Groups,
                    .Tolerance = Tolerance};
            return true;
        }
    };

    SparseConjugateGradientWorkspace::SparseConjugateGradientWorkspace(RHI::IDevice& device)
        : m_Impl(std::make_unique<Impl>(device)) {}
    SparseConjugateGradientWorkspace::~SparseConjugateGradientWorkspace() = default;

    std::uint64_t SparseConjugateGradientWorkspace::ReadbackBytes(std::uint32_t rows, std::uint32_t solves)
    {
        return std::uint64_t(solves) * (sizeof(SparseCgReport) + std::uint64_t(rows) * sizeof(double));
    }

    std::uint64_t SparseConjugateGradientWorkspace::ReportReadbackBytes(std::uint32_t solves)
    {
        return std::uint64_t(solves) * sizeof(SparseCgReport);
    }

    RHI::BufferHandle SparseConjugateGradientWorkspace::RecordFinal(RHI::ICommandContext& commands)
    {
        auto& s = *m_Impl;
        if (s.Failed || !s.Uploaded || !Finished()) return {};
        commands.BufferBarrier(s.Buffers[Impl::ResultSlot], RHI::MemoryAccess::ShaderRead | RHI::MemoryAccess::ShaderWrite,
                               RHI::MemoryAccess::TransferRead);
        return s.Buffers[Impl::ResultSlot];
    }

    bool SparseConjugateGradientWorkspace::Begin(const SparseCgProblem& p)
    {
        auto& s = *m_Impl;
        // A refused problem leaves no earlier run recordable.
        s.Rows = s.Solves = 0;
        s.Started = s.Uploaded = false;
        const auto& m = p.Matrix;
        const std::uint64_t n = m.Rows;
        const bool chained = p.ChainStride > 0;
        const std::uint64_t seeds = chained ? p.ChainStride : p.Solves;
        const bool deviceSeeds = p.SeedsOnDevice && chained;
        const std::uint64_t seedCount = deviceSeeds ? 0u : seeds * n;
        if (n == 0 || n > (1u << 24) || p.Solves == 0 || m.RowOffsets.size() != n + 1 ||
            m.RowOffsets[0] != 0 || m.RowOffsets[n] != m.Columns.size() || m.Columns.size() != m.Values.size() ||
            m.Values.size() > (1u << 26) || p.RightHandSides.size() != seedCount || p.InitialGuesses.size() != seedCount ||
            (chained && (p.Solves % p.ChainStride != 0 || p.RhsDiagonal.size() != p.ChainStride * n ||
                         p.RhsConstant.size() != p.ChainStride * n)) ||
            p.MaxIterations == 0 || !std::isfinite(p.Tolerance) || p.Tolerance <= 0 ||
            ReadbackBytes(m.Rows, p.Solves) > (std::uint64_t{1} << 28) ||
            !s.Device.IsOperational() || !s.Device.SupportsShaderFloat64())
            return false;
        for (std::uint64_t r = 0; r < n; ++r)
            if (m.RowOffsets[r] > m.RowOffsets[r + 1]) return false;
        if (std::ranges::any_of(m.Columns, [&](std::uint32_t c) { return c >= n; })) return false;
        const auto finite = [](std::span<const double> v) { return std::ranges::all_of(v, [](double x) { return std::isfinite(x); }); };
        if (!finite(m.Values) || !finite(p.RightHandSides) || !finite(p.InitialGuesses) ||
            !finite(p.RhsDiagonal) || !finite(p.RhsConstant))
            return false;
        s.Rows = m.Rows; s.Solves = p.Solves; s.ChainStride = p.ChainStride; s.MaxIterations = p.MaxIterations;
        s.Tolerance = p.Tolerance; s.Groups = std::uint32_t((n + kGroup - 1) / kGroup);
        s.Offsets.assign(m.RowOffsets.begin(), m.RowOffsets.end());
        s.Columns.assign(m.Columns.begin(), m.Columns.end());
        s.Values.assign(m.Values.begin(), m.Values.end());
        s.Rhs.assign(p.RightHandSides.begin(), p.RightHandSides.end());
        s.Guesses.assign(p.InitialGuesses.begin(), p.InitialGuesses.end());
        s.RhsDiagonal.assign(p.RhsDiagonal.begin(), p.RhsDiagonal.end());
        s.RhsConstant.assign(p.RhsConstant.begin(), p.RhsConstant.end());
        s.SeedsOnDevice = deviceSeeds;
        s.Solve = s.Iteration = s.ChunkCount = 0;
        s.Started = s.Uploaded = s.Failed = false;
        return true;
    }

    bool SparseConjugateGradientWorkspace::RecordUpload(RHI::ICommandContext& commands)
    {
        auto& s = *m_Impl;
        if (s.Failed || s.Rows == 0 || !s.Device.IsOperational()) return false;
        if (s.Uploaded) return true;
        if (!s.Upload()) return false;
        s.Uploaded = true;
        // A reused buffer's previous reads (an earlier run's readback included) precede this run.
        for (auto buffer : s.Buffers)
            if (buffer.IsValid())
                commands.BufferBarrier(buffer, RHI::MemoryAccess::TransferWrite | RHI::MemoryAccess::TransferRead |
                                               RHI::MemoryAccess::ShaderRead | RHI::MemoryAccess::ShaderWrite,
                                       RHI::MemoryAccess::ShaderRead | RHI::MemoryAccess::ShaderWrite);
        return true;
    }

    RHI::BufferHandle SparseConjugateGradientWorkspace::RecordNext(RHI::ICommandContext& commands)
    {
        auto& s = *m_Impl;
        if (s.Failed || s.Rows == 0 || Finished() || !s.Device.IsOperational()) return {};
        std::uint32_t budget = ChunkDispatches;
        const std::uint64_t n = s.Rows;
        if (!RecordUpload(commands)) return {};
        commands.BindPipeline(s.Pipeline);
        Push push = s.Base;
        const auto dispatch = [&](std::uint32_t mode, bool single = false) {
            push.Mode = mode;
            commands.PushConstants(&push, sizeof(push), 0);
            commands.Dispatch(single ? 1u : s.Groups, 1, 1);
            for (const auto slot : {Impl::RhsSlot, Impl::WorkSlot, Impl::PartialSlot, Impl::ResultSlot})
                commands.BufferBarrier(s.Buffers[slot], RHI::MemoryAccess::ShaderRead | RHI::MemoryAccess::ShaderWrite,
                                       RHI::MemoryAccess::ShaderRead | RHI::MemoryAccess::ShaderWrite);
            --budget;
        };
        const auto result = s.Address(Impl::ResultSlot);
        if (s.ChunkCount == 0) dispatch(Diagonal);
        while (s.Solve < s.Solves && budget > kIterationDispatches + 5)
        {
            push.State = result + s.Solve * sizeof(SparseCgReport);
            push.X = result + s.ReportBytes() + std::uint64_t(s.Solve) * n * sizeof(double);
            push.B = s.Address(Impl::RhsSlot) + std::uint64_t(s.Solve) * n * sizeof(double);
            if (!s.Started)
            {
                if (s.ChainStride && (s.Solve >= s.ChainStride || s.SeedsOnDevice))
                {
                    // Form the chained right-hand side and warm start from the previous solution
                    // (a device-seeded first solve reads its own seed).
                    const std::uint32_t lane = s.Solve % s.ChainStride;
                    push.Previous = s.Solve >= s.ChainStride
                        ? result + s.ReportBytes() + std::uint64_t(s.Solve - s.ChainStride) * n * sizeof(double)
                        : push.X;
                    push.RhsDiagonal = s.Address(Impl::DiagonalSlot) + std::uint64_t(lane) * n * sizeof(double);
                    push.RhsConstant = s.Address(Impl::ConstantSlot) + std::uint64_t(lane) * n * sizeof(double);
                    dispatch(ChainRhs);
                }
                dispatch(Initialize);
                dispatch(InitialDots);
                dispatch(InitialScalar, true);
                s.Started = true;
                s.Iteration = 0;
            }
            while (s.Iteration < s.MaxIterations && budget > kIterationDispatches + 1)
            {
                for (const auto mode : {Spmv, DotPAp, Alpha, UpdateXR, DotRR, Residual, Precondition, Beta, UpdateP})
                    dispatch(mode, mode == Alpha || mode == Residual || mode == Beta);
                ++s.Iteration;
            }
            if (s.Iteration < s.MaxIterations) break;
            dispatch(Finalize, true);
            ++s.Solve;
            s.Started = false;
        }
        ++s.ChunkCount;
        commands.BufferBarrier(s.Buffers[Impl::ResultSlot], RHI::MemoryAccess::ShaderRead | RHI::MemoryAccess::ShaderWrite,
                               RHI::MemoryAccess::TransferRead);
        return s.Buffers[Impl::ResultSlot];
    }

    void SparseConjugateGradientWorkspace::Observe(std::span<const std::byte> readback)
    {
        auto& s = *m_Impl;
        if (s.Solve >= s.Solves || !s.Started || readback.size() < s.ReportBytes()) return;
        SparseCgReport report{};
        std::memcpy(&report, readback.data() + std::size_t(s.Solve) * sizeof(report), sizeof(report));
        if (report.Done != 0)
        {
            // The device finished this solve early; skip its remaining iterations.
            ++s.Solve;
            s.Started = false;
        }
    }

    bool SparseConjugateGradientWorkspace::Finished() const noexcept { return m_Impl->Solve >= m_Impl->Solves; }
    RHI::BufferHandle SparseConjugateGradientWorkspace::ResultBuffer() const noexcept { return m_Impl->Uploaded ? m_Impl->Buffers[Impl::ResultSlot] : RHI::BufferHandle{}; }
    std::uint64_t SparseConjugateGradientWorkspace::SolutionsAddress() const
    {
        return m_Impl->Uploaded ? m_Impl->Address(Impl::ResultSlot) + m_Impl->ReportBytes() : 0u;
    }
    std::uint32_t SparseConjugateGradientWorkspace::CompletedSolves() const noexcept { return m_Impl->Solve; }
    std::uint32_t SparseConjugateGradientWorkspace::Chunks() const noexcept { return m_Impl->ChunkCount; }
}
