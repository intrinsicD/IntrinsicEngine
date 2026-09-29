module;
#include <algorithm>
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
        RHI::IDevice& Device;
        RHI::PipelineHandle Pipeline{};
        std::vector<RHI::BufferHandle> Buffers{};
        // Owned copy of the problem.
        std::uint32_t Rows{}, Solves{}, ChainStride{}, MaxIterations{}, Groups{};
        double Tolerance{};
        std::vector<std::uint32_t> Offsets, Columns;
        std::vector<double> Values, Rhs, Guesses, RhsDiagonal, RhsConstant;
        RHI::BufferHandle RhsBuffer{}, Work{}, PartialBuffer{}, Result{}, DiagonalBuffer{}, ConstantBuffer{};
        Push Base{};
        // Progress: the solve being recorded, whether it started, and its recorded iterations.
        std::uint32_t Solve{}, Iteration{}, ChunkCount{};
        bool Started{}, Uploaded{}, Failed{};

        explicit Impl(RHI::IDevice& device) : Device(device) {}
        ~Impl()
        {
            for (auto buffer : Buffers)
                if (buffer.IsValid()) Device.DestroyBuffer(buffer);
            if (Pipeline.IsValid()) Device.DestroyPipeline(Pipeline);
        }
        RHI::BufferHandle Create(const void* data, std::size_t bytes, const char* name)
        {
            const auto buffer = Device.CreateBuffer({.SizeBytes = std::max<std::size_t>(bytes, 8),
                .Usage = RHI::BufferUsage::Storage | RHI::BufferUsage::TransferSrc | RHI::BufferUsage::TransferDst,
                .HostVisible = true, .DebugName = name});
            if (!buffer.IsValid()) { Failed = true; return {}; }
            Buffers.push_back(buffer);
            if (data && bytes) Device.WriteBuffer(buffer, data, bytes);
            return buffer;
        }
        std::uint64_t Address(RHI::BufferHandle b) const { return Device.GetBufferDeviceAddress(b); }
        std::size_t ReportBytes() const { return std::size_t(Solves) * sizeof(SparseCgReport); }

        bool Upload()
        {
            if (!Pipeline.IsValid())
            {
                Pipeline = CreateComputePipeline(Device, "shaders/sparse_cg.comp.spv", sizeof(Push), "SparseConjugateGradient");
                if (!Pipeline.IsValid()) return false;
            }
            const std::size_t n = Rows;
            // Reports start zeroed; each solve's slot of the solutions starts at its initial guess.
            std::vector<std::byte> output(ReadbackBytes(Rows, Solves), std::byte{0});
            std::memcpy(output.data() + ReportBytes(), Guesses.data(), Guesses.size() * sizeof(double));
            std::vector<double> rhs(std::size_t(Solves) * n, 0.0);
            std::copy(Rhs.begin(), Rhs.end(), rhs.begin());
            const auto offsets = Create(Offsets.data(), Offsets.size() * 4, "SparseCG.RowOffsets");
            const auto columns = Create(Columns.data(), Columns.size() * 4, "SparseCG.Columns");
            const auto values = Create(Values.data(), Values.size() * 8, "SparseCG.Values");
            RhsBuffer = Create(rhs.data(), rhs.size() * 8, "SparseCG.RightHandSides");
            DiagonalBuffer = Create(RhsDiagonal.data(), RhsDiagonal.size() * 8, "SparseCG.RhsDiagonal");
            ConstantBuffer = Create(RhsConstant.data(), RhsConstant.size() * 8, "SparseCG.RhsConstant");
            Work = Create(nullptr, 5 * n * 8, "SparseCG.Work");
            PartialBuffer = Create(nullptr, 3 * std::size_t(Groups) * 8, "SparseCG.Partials");
            Result = Create(output.data(), output.size(), "SparseCG.Result");
            if (Failed) return false;
            Base = {.Offsets = Address(offsets), .Columns = Address(columns), .Values = Address(values),
                    .Work = Address(Work), .Partials = Address(PartialBuffer), .Rows = Rows, .Groups = Groups,
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
        commands.BufferBarrier(s.Result, RHI::MemoryAccess::ShaderRead | RHI::MemoryAccess::ShaderWrite,
                               RHI::MemoryAccess::TransferRead);
        return s.Result;
    }

    bool SparseConjugateGradientWorkspace::Begin(const SparseCgProblem& p)
    {
        auto& s = *m_Impl;
        const auto& m = p.Matrix;
        const std::uint64_t n = m.Rows;
        const bool chained = p.ChainStride > 0;
        const std::uint64_t seeds = chained ? p.ChainStride : p.Solves;
        if (n == 0 || n > (1u << 24) || p.Solves == 0 || m.RowOffsets.size() != n + 1 ||
            m.RowOffsets[0] != 0 || m.RowOffsets[n] != m.Columns.size() || m.Columns.size() != m.Values.size() ||
            m.Values.size() > (1u << 26) || p.RightHandSides.size() != seeds * n || p.InitialGuesses.size() != seeds * n ||
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
        s.Solve = s.Iteration = s.ChunkCount = 0;
        s.Started = s.Uploaded = s.Failed = false;
        return true;
    }

    RHI::BufferHandle SparseConjugateGradientWorkspace::RecordNext(RHI::ICommandContext& commands)
    {
        auto& s = *m_Impl;
        if (s.Failed || s.Rows == 0 || Finished() || !s.Device.IsOperational()) return {};
        std::uint32_t budget = ChunkDispatches;
        const std::uint64_t n = s.Rows;
        std::vector<RHI::BufferHandle> touched;
        if (!s.Uploaded)
        {
            if (!s.Upload()) return {};
            s.Uploaded = true;
            for (auto buffer : s.Buffers)
                commands.BufferBarrier(buffer, RHI::MemoryAccess::TransferWrite | RHI::MemoryAccess::ShaderRead,
                                       RHI::MemoryAccess::ShaderRead | RHI::MemoryAccess::ShaderWrite);
        }
        commands.BindPipeline(s.Pipeline);
        Push push = s.Base;
        const auto dispatch = [&](std::uint32_t mode, bool single = false) {
            push.Mode = mode;
            commands.PushConstants(&push, sizeof(push), 0);
            commands.Dispatch(single ? 1u : s.Groups, 1, 1);
            for (auto buffer : {s.RhsBuffer, s.Work, s.PartialBuffer, s.Result})
                commands.BufferBarrier(buffer, RHI::MemoryAccess::ShaderRead | RHI::MemoryAccess::ShaderWrite,
                                       RHI::MemoryAccess::ShaderRead | RHI::MemoryAccess::ShaderWrite);
            --budget;
        };
        const auto result = s.Address(s.Result);
        if (s.ChunkCount == 0) dispatch(Diagonal);
        while (s.Solve < s.Solves && budget > kIterationDispatches + 5)
        {
            push.State = result + s.Solve * sizeof(SparseCgReport);
            push.X = result + s.ReportBytes() + std::uint64_t(s.Solve) * n * sizeof(double);
            push.B = s.Address(s.RhsBuffer) + std::uint64_t(s.Solve) * n * sizeof(double);
            if (!s.Started)
            {
                if (s.ChainStride && s.Solve >= s.ChainStride)
                {
                    // Form the chained right-hand side and warm start from the previous solution.
                    const std::uint32_t lane = s.Solve % s.ChainStride;
                    push.Previous = result + s.ReportBytes() + std::uint64_t(s.Solve - s.ChainStride) * n * sizeof(double);
                    push.RhsDiagonal = s.Address(s.DiagonalBuffer) + std::uint64_t(lane) * n * sizeof(double);
                    push.RhsConstant = s.Address(s.ConstantBuffer) + std::uint64_t(lane) * n * sizeof(double);
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
        commands.BufferBarrier(s.Result, RHI::MemoryAccess::ShaderRead | RHI::MemoryAccess::ShaderWrite,
                               RHI::MemoryAccess::TransferRead);
        return s.Result;
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
    std::uint32_t SparseConjugateGradientWorkspace::Chunks() const noexcept { return m_Impl->ChunkCount; }
}
