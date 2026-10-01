// RUNTIME-269: device Jacobi-preconditioned CG against Geometry::Sparse::SolveCG on the systems its
// consumers solve (heat step, regularized Poisson, implicit smoothing with warm starts and chained
// steps) and on every failure status, recorded through the framed runtime GPU queue.
#include "RuntimeTestModule.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <memory>
#include <random>
#include <string>
#include <utility>
#include <vector>
#include <glm/glm.hpp>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
import Extrinsic.Platform.Backend.Glfw;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.CommandContext;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.EngineConfigBoot;
import Extrinsic.Runtime.SpatialIndexCache;
import Extrinsic.Runtime.ServiceRegistry;
import Extrinsic.Graphics.SparseConjugateGradient;
import Geometry.HalfedgeMesh;
import Geometry.DEC;
import Geometry.Sparse;

namespace
{
    namespace Runtime = Extrinsic::Runtime;
    namespace G = Extrinsic::Graphics;
    namespace Sp = Geometry::Sparse;

    struct Shutdown
    {
        Runtime::Engine& Engine;
        ~Shutdown() { Engine.Shutdown(); }
    };

    struct Problem
    {
        std::string Name;
        Sp::SparseMatrix Matrix;
        std::uint32_t Solves{1}, ChainStride{0}, MaxIterations{2000};
        double Tolerance{1e-8};
        std::vector<double> Rhs, Guesses, RhsDiagonal, RhsConstant;
        bool WellConditioned{true};
        // GPU copies of the CSR structure.
        std::vector<std::uint32_t> Offsets, Columns;
        std::shared_ptr<G::SparseConjugateGradientWorkspace> Workspace;
        std::shared_ptr<Runtime::SpatialGpuResult> Gpu;
    };

    // alpha M + beta A exactly as Sparse::SolveCGShifted assembles it.
    Sp::SparseMatrix Shifted(const Sp::DiagonalMatrix& M, double alpha, const Sp::SparseMatrix& A, double beta)
    {
        Sp::SparseBuilder builder(A.Rows, A.Cols);
        for (std::size_t row = 0; row < A.Rows; ++row)
        {
            if (alpha != 0.0) builder.Add(row, row, alpha * M.Diagonal[row]);
            for (std::size_t k = A.RowOffsets[row]; k < A.RowOffsets[row + 1]; ++k)
                builder.Add(row, A.ColIndices[k], beta * A.Values[k]);
        }
        return builder.Build(0.0).Matrix;
    }

    Geometry::HalfedgeMesh::Mesh Grid(int size, std::mt19937& random)
    {
        std::uniform_real_distribution<float> jitter(-0.15f, 0.15f);
        Geometry::HalfedgeMesh::Mesh mesh;
        std::vector<Geometry::VertexHandle> v;
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x)
                v.push_back(mesh.AddVertex({float(x) + jitter(random), float(y) + jitter(random), 0.3f * std::sin(0.5f * float(x + y))}));
        for (int y = 0; y + 1 < size; ++y)
            for (int x = 0; x + 1 < size; ++x)
            {
                const auto at = [&](int i, int j) { return v[std::size_t(j * size + i)]; };
                (void)mesh.AddTriangle(at(x, y), at(x + 1, y), at(x + 1, y + 1));
                (void)mesh.AddTriangle(at(x, y), at(x + 1, y + 1), at(x, y + 1));
            }
        return mesh;
    }

    class SolveApp final : public Intrinsic::Tests::RuntimeTestModule
    {
    public:
        void Resolve() override
        {
            Started = std::chrono::steady_clock::now();
            Cache = Kernel().Services().Find<Runtime::SpatialIndexCache>();
            std::mt19937 random(269);
            std::uniform_real_distribution<double> unit(-1, 1);
            const auto mesh = Grid(18, random);
            const auto ops = Geometry::DEC::BuildOperators(mesh);
            const std::size_t n = ops.Laplacian.Rows;
            double h = 0;
            for (std::size_t i = 0; i < n; ++i) h += ops.Hodge0.Diagonal[i];
            const double t = h / double(n); // squared mean edge length scale of the grid
            {
                Problem p{.Name = "heat_step", .Matrix = Shifted(ops.Hodge0, 1.0, ops.Laplacian, t)};
                p.Rhs.assign(n, 0.0);
                p.Rhs[7] = p.Rhs[n - 11] = 1.0;
                p.Guesses.assign(n, 0.0);
                Problems.push_back(std::move(p));
            }
            {
                Sp::DiagonalMatrix identity{n, std::vector<double>(n, 1.0)};
                Problem p{.Name = "regularized_poisson", .Matrix = Shifted(identity, 1e-8, ops.Laplacian, 1.0)};
                p.Rhs.resize(n);
                double mean = 0;
                for (auto& b : p.Rhs) mean += (b = unit(random));
                for (auto& b : p.Rhs) b -= mean / double(n);
                p.Guesses.assign(n, 0.0);
                p.WellConditioned = false;
                Problems.push_back(std::move(p));
            }
            // Implicit smoothing: M + dt (D - W) on a random nonnegative graph.
            const std::size_t m = 300;
            Sp::SparseBuilder graph(m, m);
            std::vector<double> degree(m, 0.0), mass(m);
            std::uniform_int_distribution<std::size_t> pick(0, m - 1);
            std::uniform_real_distribution<double> weight(0.1, 2.0);
            for (std::size_t e = 0; e < 4 * m; ++e)
            {
                const auto a = pick(random), b = pick(random);
                if (a == b) continue;
                const double w = weight(random);
                graph.Add(a, b, -w); graph.Add(b, a, -w);
                degree[a] += w; degree[b] += w;
            }
            for (std::size_t i = 0; i < m; ++i) { graph.Add(i, i, degree[i]); mass[i] = 0.5 + 0.5 * std::abs(unit(random)); }
            const auto laplacian = graph.Build(0.0).Matrix;
            const Sp::DiagonalMatrix massMatrix{m, mass};
            const auto implicit = Shifted(massMatrix, 1.0, laplacian, 3.0);
            {
                Problem p{.Name = "implicit_three_warm_starts", .Matrix = implicit, .Solves = 3};
                for (int k = 0; k < 3; ++k)
                    for (std::size_t i = 0; i < m; ++i)
                    {
                        p.Rhs.push_back(mass[i] * unit(random));
                        p.Guesses.push_back(0.1 * unit(random));
                    }
                Problems.push_back(std::move(p));
            }
            {
                // Four chained backward-Euler steps of two channels: b_k = M x_(k-1).
                Problem p{.Name = "implicit_chained_steps", .Matrix = implicit, .Solves = 8, .ChainStride = 2};
                for (int c = 0; c < 2; ++c)
                    for (std::size_t i = 0; i < m; ++i)
                    {
                        const double x0 = unit(random);
                        p.Rhs.push_back(mass[i] * x0);
                        p.Guesses.push_back(x0);
                        p.RhsDiagonal.push_back(mass[i]);
                        p.RhsConstant.push_back(0.0);
                    }
                Problems.push_back(std::move(p));
            }
            {
                Problem p{.Name = "max_iterations", .Matrix = Problems[0].Matrix, .MaxIterations = 3};
                p.Rhs = Problems[0].Rhs;
                p.Guesses.assign(n, 0.0);
                Problems.push_back(std::move(p));
            }
            {
                Sp::SparseBuilder zero(4, 4);
                zero.Add(0, 1, 0.0);
                Problem p{.Name = "breakdown_zero_matrix", .Matrix = zero.Build(0.0).Matrix};
                p.Rhs.assign(4, 1.0);
                p.Guesses.assign(4, 0.0);
                Problems.push_back(std::move(p));
            }
            {
                Sp::SparseBuilder tiny(1, 1);
                tiny.Add(0, 0, 1e-200);
                Problem p{.Name = "non_finite", .Matrix = tiny.Build(0.0).Matrix};
                p.Rhs = {1e200};
                p.Guesses = {0.0};
                Problems.push_back(std::move(p));
            }
            if (Shared)
            {
                // After the 1-row system, the first one again: the shared workspace grows back.
                auto again = Problems[0];
                again.Name = "heat_step_after_shrink";
                Problems.push_back(std::move(again));
            }
            for (auto& p : Problems)
            {
                for (const auto o : p.Matrix.RowOffsets) p.Offsets.push_back(std::uint32_t(o));
                for (const auto c : p.Matrix.ColIndices) p.Columns.push_back(std::uint32_t(c));
            }
        }

        void Frame(double, double) override
        {
            if (std::chrono::steady_clock::now() - Started > std::chrono::seconds(100))
            {
                ADD_FAILURE() << "RUNTIME-269 timeout";
                TimedOut = true;
                Kernel().RequestExit();
                return;
            }
            if (!Kernel().GetDevice().IsOperational() || !Cache) return;
            if (Shared)
            {
                SharedFrame();
                return;
            }
            if (!Queued)
            {
                Queued = true;
                QueuedAt = std::chrono::steady_clock::now();
                for (auto& p : Problems)
                {
                    p.Workspace = std::make_shared<G::SparseConjugateGradientWorkspace>(Kernel().GetDevice());
                    ASSERT_TRUE(p.Workspace->Begin({.Matrix = {.Rows = std::uint32_t(p.Matrix.Rows), .RowOffsets = p.Offsets,
                        .Columns = p.Columns, .Values = p.Matrix.Values}, .Solves = p.Solves, .RightHandSides = p.Rhs,
                        .InitialGuesses = p.Guesses, .RhsDiagonal = p.RhsDiagonal, .RhsConstant = p.RhsConstant,
                        .ChainStride = p.ChainStride, .MaxIterations = p.MaxIterations, .Tolerance = p.Tolerance})) << p.Name;
                    Queue(p);
                }
                return;
            }
            // One chunk per framed submission; observe each readback before recording the next.
            bool pending = false;
            for (auto& p : Problems)
            {
                if (p.Gpu->State == Runtime::SpatialQueryState::Failed) continue;
                if (p.Gpu->State != Runtime::SpatialQueryState::Ready) { pending = true; continue; }
                if (p.Workspace->Finished()) continue;
                p.Workspace->Observe(p.Gpu->Data);
                if (!p.Workspace->Finished()) { Queue(p); pending = true; }
            }
            if (pending) return;
            GpuMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - QueuedAt).count();
            for (auto& p : Problems) Compare(p);
            Done = true;
            Kernel().RequestExit();
        }

        // One workspace solves the problems in turn, each Begin after the previous run's last
        // readback: smaller and larger systems, chained and failing solves share its buffers.
        void SharedFrame()
        {
            if (!Workspace) Workspace = std::make_shared<G::SparseConjugateGradientWorkspace>(Kernel().GetDevice());
            auto& p = Problems[Current];
            if (!p.Gpu)
            {
                p.Workspace = Workspace;
                ASSERT_TRUE(Workspace->Begin({.Matrix = {.Rows = std::uint32_t(p.Matrix.Rows), .RowOffsets = p.Offsets,
                    .Columns = p.Columns, .Values = p.Matrix.Values}, .Solves = p.Solves, .RightHandSides = p.Rhs,
                    .InitialGuesses = p.Guesses, .RhsDiagonal = p.RhsDiagonal, .RhsConstant = p.RhsConstant,
                    .ChainStride = p.ChainStride, .MaxIterations = p.MaxIterations, .Tolerance = p.Tolerance})) << p.Name;
                Queue(p);
                return;
            }
            if (p.Gpu->State != Runtime::SpatialQueryState::Ready && p.Gpu->State != Runtime::SpatialQueryState::Failed) return;
            if (p.Gpu->State == Runtime::SpatialQueryState::Ready && !Workspace->Finished())
            {
                Workspace->Observe(p.Gpu->Data);
                if (!Workspace->Finished())
                {
                    Queue(p);
                    return;
                }
            }
            Compare(p);
            if (++Current < Problems.size()) return;
            Done = true;
            Kernel().RequestExit();
        }

        void Queue(Problem& p)
        {
            const auto bytes = G::SparseConjugateGradientWorkspace::ReadbackBytes(std::uint32_t(p.Matrix.Rows), p.Solves);
            p.Gpu = Cache->QueueGpuCompute(bytes, [workspace = p.Workspace](Extrinsic::RHI::ICommandContext& commands,
                                                                           const Runtime::SpatialGpuIndexView&) {
                return workspace->RecordNext(commands);
            });
            Chunks += 1;
        }

        void Compare(const Problem& p)
        {
            SCOPED_TRACE(p.Name);
            ASSERT_EQ(p.Gpu->State, Runtime::SpatialQueryState::Ready) << p.Gpu->Diagnostic;
            const std::size_t n = p.Matrix.Rows;
            const std::size_t reportBytes = p.Solves * sizeof(G::SparseCgReport);
            std::vector<double> previous;
            for (std::uint32_t solve = 0; solve < p.Solves; ++solve)
            {
                G::SparseCgReport report{};
                std::memcpy(&report, p.Gpu->Data.data() + solve * sizeof(report), sizeof(report));
                std::vector<double> gpu(n);
                std::memcpy(gpu.data(), p.Gpu->Data.data() + reportBytes + solve * n * sizeof(double), n * sizeof(double));
                // CPU oracle for the same solve; chained solves start from the CPU's own chain.
                std::vector<double> b(n), x(n);
                if (p.ChainStride && solve >= p.ChainStride)
                {
                    const auto lane = solve % p.ChainStride;
                    const auto& prior = CpuSolutions[CpuSolutions.size() - p.ChainStride];
                    for (std::size_t i = 0; i < n; ++i)
                    {
                        b[i] = p.RhsDiagonal[lane * n + i] * prior[i] + p.RhsConstant[lane * n + i];
                        x[i] = prior[i];
                    }
                }
                else
                {
                    std::copy_n(p.Rhs.begin() + std::ptrdiff_t(solve * n), n, b.begin());
                    std::copy_n(p.Guesses.begin() + std::ptrdiff_t(solve * n), n, x.begin());
                }
                const auto cpuStart = std::chrono::steady_clock::now();
                const auto cpu = Sp::SolveCG(p.Matrix, b, x, {.MaxIterations = p.MaxIterations, .Tolerance = p.Tolerance});
                CpuMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - cpuStart).count();
                CpuSolutions.push_back(x);
                EXPECT_EQ(std::uint32_t(report.Status), std::uint32_t(cpu.Reason)) << "solve " << solve;
                const auto iterationGap = std::abs(double(report.Iterations) - double(cpu.Iterations));
                MaxIterationGap = std::max(MaxIterationGap, iterationGap);
                EXPECT_LE(iterationGap, 2.0) << "solve " << solve << ": gpu " << report.Iterations << " cpu " << cpu.Iterations;
                if (cpu.Reason != Sp::CGConvergenceReason::Converged)
                {
                    if (cpu.Reason == Sp::CGConvergenceReason::MaxIterations) EXPECT_EQ(report.Iterations, cpu.Iterations);
                    continue;
                }
                // The device solution satisfies the stopping rule when re-evaluated on the CPU.
                std::vector<double> ax(n);
                p.Matrix.Multiply(gpu, ax);
                double residual = 0, bNorm = 0, xNorm = 1, delta = 0;
                for (std::size_t i = 0; i < n; ++i)
                {
                    residual += (b[i] - ax[i]) * (b[i] - ax[i]);
                    bNorm += b[i] * b[i];
                    xNorm = std::max(xNorm, std::abs(x[i]));
                    delta = std::max(delta, std::abs(gpu[i] - x[i]));
                }
                EXPECT_LE(std::sqrt(residual), p.Tolerance * std::max(std::sqrt(bNorm), 1.0) * (1 + 1e-6)) << "solve " << solve;
                EXPECT_NEAR(report.ResidualNorm, std::sqrt(residual), 1e-6 * p.Tolerance * std::max(std::sqrt(bNorm), 1.0));
                const double relative = delta / xNorm;
                if (p.WellConditioned)
                {
                    MaxSolutionDelta = std::max(MaxSolutionDelta, relative);
                    EXPECT_LE(relative, 1e-6) << "solve " << solve;
                }
                ++Compared;
            }
        }

        void Shutdown() override
        {
            Problems.clear();
            Workspace.reset();
        }

        Runtime::SpatialIndexCache* Cache{};
        std::vector<Problem> Problems;
        std::vector<std::vector<double>> CpuSolutions;
        std::chrono::steady_clock::time_point Started{}, QueuedAt{};
        double GpuMs{}, CpuMs{}, MaxSolutionDelta{}, MaxIterationGap{};
        std::size_t Compared{}, Chunks{}, Current{};
        std::shared_ptr<G::SparseConjugateGradientWorkspace> Workspace;
        bool Queued{}, Done{}, TimedOut{}, Shared{};
    };
}

TEST(RUNTIME269VulkanSparseSolve, MatchesTheCpuConjugateGradientOnConsumerSystemsAndFailures)
{
    auto config = Runtime::CreateReferenceEngineConfig();
    config.Window.Width = 64;
    config.Window.Height = 64;
    config.Render.EnableValidation = true;
    config.Render.EnableVSync = false;
    config.ReferenceScene.Enabled = false;
    if (!Extrinsic::Platform::Backends::Glfw::CanInitialize()) GTEST_SKIP() << "GLFW unavailable";
    auto app = std::make_unique<SolveApp>();
    auto* run = app.get();
    Intrinsic::Tests::RuntimeTestKernel engine(config, std::move(app));
    engine.EmplaceModule<Runtime::SpatialIndexCache>();
    engine.Initialize();
    Shutdown shutdown{engine};
    if (!engine.GetDevice().SupportsShaderFloat64()) GTEST_SKIP() << "Shader float64 unavailable";
    engine.Run();
    ASSERT_TRUE(engine.GetDevice().IsOperational());
    ASSERT_FALSE(run->TimedOut);
    ASSERT_TRUE(run->Done);
    EXPECT_GE(run->Compared, 12u);
    std::printf("RUNTIME-269 parity: %zu converged solves compared, max relative solution delta %.3g, max iteration gap %.0f; "
                "%zu chunks, GPU %.1f ms (frame-paced), CPU %.1f ms\n",
                run->Compared, run->MaxSolutionDelta, run->MaxIterationGap, run->Chunks, run->GpuMs, run->CpuMs);
    if (const auto* output = std::getenv("INTRINSIC_RUNTIME269_BENCHMARK_OUTPUT"))
    {
        const nlohmann::json json{{"benchmark_id", "runtime.sparse_cg.vulkan_parity"},
            {"method", "geometry.sparse_cg"}, {"backend", "gpu_vulkan_compute"},
            {"dataset", "builtin.sparse_cg.consumer_systems.seed269"}, {"commit", "local-dev"},
            {"metrics", {{"runtime_ms", run->GpuMs}, {"quality_error_linf", run->MaxSolutionDelta}}},
            {"diagnostics", {{"runner", "IntrinsicPointLBVHGpuTests"}, {"mode", "smoke"}, {"converged_solves", run->Compared},
                {"cpu_reference_total_ms", run->CpuMs}, {"max_iteration_gap", run->MaxIterationGap}, {"chunks", run->Chunks},
                {"cpu_stages", "matrix assembly, CSR narrowing, upload"}, {"speedup_claimed", false},
                {"warmup_iterations", 0}, {"measured_iterations", 1}}},
            {"status", ::testing::Test::HasFailure() ? "failed" : "passed"}};
        std::ofstream stream(output);
        stream << json.dump(2) << '\n';
    }
}

TEST(RUNTIME269VulkanSparseSolve, OneWorkspaceSolvesEveryProblemInTurnAcrossShrinkAndGrowth)
{
    auto config = Runtime::CreateReferenceEngineConfig();
    config.Window.Width = 64;
    config.Window.Height = 64;
    config.Render.EnableValidation = true;
    config.Render.EnableVSync = false;
    config.ReferenceScene.Enabled = false;
    if (!Extrinsic::Platform::Backends::Glfw::CanInitialize()) GTEST_SKIP() << "GLFW unavailable";
    auto app = std::make_unique<SolveApp>();
    auto* run = app.get();
    run->Shared = true;
    Intrinsic::Tests::RuntimeTestKernel engine(config, std::move(app));
    engine.EmplaceModule<Runtime::SpatialIndexCache>();
    engine.Initialize();
    Shutdown shutdown{engine};
    if (!engine.GetDevice().SupportsShaderFloat64()) GTEST_SKIP() << "Shader float64 unavailable";
    engine.Run();
    ASSERT_TRUE(engine.GetDevice().IsOperational());
    ASSERT_FALSE(run->TimedOut);
    ASSERT_TRUE(run->Done);
    // The per-problem test's twelve converged solves plus the regrown heat step.
    EXPECT_GE(run->Compared, 13u);
}
