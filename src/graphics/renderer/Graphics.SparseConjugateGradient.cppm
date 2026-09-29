// Device-owned Jacobi-preconditioned conjugate gradient for real SPD CSR systems in double
// precision, mirroring Geometry::Sparse::SolveCG. Inputs are plain arrays prepared by the
// caller; complex, indefinite and rectangular systems are not supported.
module;
#include <cstdint>
#include <memory>
#include <span>
export module Extrinsic.Graphics.SparseConjugateGradient;
import Extrinsic.RHI.Handles;

extern "C++" { namespace Extrinsic::RHI { class IDevice; class ICommandContext; } }

export namespace Extrinsic::Graphics
{
    // Mirrors Geometry::Sparse::CGConvergenceReason.
    enum class SparseCgStatus : std::uint32_t { NotRun, Converged, MaxIterations, InvalidInput, Breakdown, NonFinite };

    // One per solve at the start of the readback, followed by every solution (rows doubles each).
    struct SparseCgReport
    {
        SparseCgStatus Status{};
        std::uint32_t Iterations{}, Done{}, Reserved{};
        double InitialResidualNorm{}, ResidualNorm{}, RelativeResidual{};
        double Rz{}, Alpha{}, Beta{}, BNorm{}, Tolerance{};
    };

    struct SparseCgMatrix
    {
        std::uint32_t Rows{};
        std::span<const std::uint32_t> RowOffsets; // Rows + 1, column-sorted rows
        std::span<const std::uint32_t> Columns;
        std::span<const double> Values;
    };

    struct SparseCgProblem
    {
        SparseCgMatrix Matrix{};
        std::uint32_t Solves{1};
        // Solves x Rows each: right-hand sides and initial guesses (warm starts).
        std::span<const double> RightHandSides;
        std::span<const double> InitialGuesses;
        // Optional chaining for sequences of solves such as implicit time steps: when set, solve
        // k > 0 uses b = RhsDiagonal * x_(k - ChainStride) + RhsConstant (per row) and starts
        // from that previous solution; the first ChainStride solves use RightHandSides.
        std::span<const double> RhsDiagonal, RhsConstant;
        std::uint32_t ChainStride{0};
        std::uint32_t MaxIterations{1000};
        double Tolerance{1e-8};
    };

    class SparseConjugateGradientWorkspace
    {
    public:
        explicit SparseConjugateGradientWorkspace(RHI::IDevice& device);
        ~SparseConjugateGradientWorkspace();
        // Dispatches recorded per chunk at most; one CG iteration costs nine.
        static constexpr std::uint32_t ChunkDispatches = 2048;
        [[nodiscard]] static std::uint64_t ReadbackBytes(std::uint32_t rows, std::uint32_t solves);
        // The reports alone (the head of the result buffer): what a chunk's readback needs for
        // Observe (GRAPHICS-153); the solutions are read once, after RecordFinal.
        [[nodiscard]] static std::uint64_t ReportReadbackBytes(std::uint32_t solves);
        // Validates and copies the problem; false on malformed CSR, shapes, parameters, or a device
        // without operational state or shader double support.
        [[nodiscard]] bool Begin(const SparseCgProblem& problem);
        // Records the next bounded chunk (uploading the operator on the first call) and returns the
        // buffer holding every report and solution, ready for transfer reads; invalid on failure.
        // Read back ReportReadbackBytes per chunk.
        // Record one chunk per framed submission and Observe its readback before the next.
        [[nodiscard]] RHI::BufferHandle RecordNext(RHI::ICommandContext& commands);
        // Consumes a chunk's readback (at least the reports): a finished solve skips its remaining
        // iterations.
        void Observe(std::span<const std::byte> readback);
        // Once Finished: the result buffer (reports and solutions) ready for transfer reads, for
        // the single final readback of ReadbackBytes; invalid before.
        [[nodiscard]] RHI::BufferHandle RecordFinal(RHI::ICommandContext& commands);
        [[nodiscard]] bool Finished() const noexcept;
        [[nodiscard]] std::uint32_t Chunks() const noexcept;
    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
