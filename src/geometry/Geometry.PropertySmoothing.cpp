// Graph filters share a fixed, nonnegative neighborhood and operate on all channels together.
module;
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>
#include <glm/glm.hpp>
module Geometry.HalfedgeMesh.Smoothing;
import Geometry.PointLBVH;
import Geometry.Sparse;

namespace Geometry::Smoothing
{
    bool ValidatePropertyFilterParams(const PropertyFilterParams& p) noexcept
    {
        return p.Method <= PropertyFilter::VariationalFit && p.Laplacian <= PropertyLaplacian::LumpedMass &&
            p.Solver <= PropertySolver::ConjugateGradient &&
            (p.Laplacian != PropertyLaplacian::LumpedMass || p.Method == PropertyFilter::Implicit ||
             p.Method == PropertyFilter::VariationalFit) &&
            p.SmoothnessPenalty <= FitPenalty::L1 && p.DataPenalty <= FitPenalty::L1 &&
            p.Fidelity <= FitFidelity::NoiseLevel && p.Bound <= FitBound::PerRow &&
            std::isfinite(p.FitWeight) && p.FitWeight > 0 && p.FitWeight <= 1e12 &&
            std::isfinite(p.NoiseLevel) && p.NoiseLevel > 0 &&
            p.FitAlgorithm <= FitSolver::Admm && std::isfinite(p.PenaltyDelta) && p.PenaltyDelta >= 0 &&
            p.BoundNorm <= FitBoundNorm::Euclidean && p.SmoothnessOrder <= FitOrder::Second &&
            std::isfinite(p.SecondOrderWeight) && p.SecondOrderWeight > 0 &&
            // Solver-specific fit combinations only matter when the variational fit runs; other
            // methods ignore these fields, so a draft kept from an earlier fit stays usable.
            (p.Method != PropertyFilter::VariationalFit ||
             ((p.BoundNorm == FitBoundNorm::PerChannel || p.FitAlgorithm == FitSolver::Admm) &&
              (p.SmoothnessOrder == FitOrder::First || p.FitAlgorithm == FitSolver::Admm) &&
              (p.PenaltyDelta > 0 || (p.FitAlgorithm == FitSolver::Admm &&
                                      p.SmoothnessPenalty != FitPenalty::Huber && p.DataPenalty != FitPenalty::Huber)))) &&
            std::isfinite(p.BoundRadius) && p.BoundRadius >= 0 &&
            p.MaxFitIterations >= 1 && p.MaxFitIterations <= 100000 &&
            std::isfinite(p.FitTolerance) && p.FitTolerance > 0 && p.FitTolerance < 1 &&
            std::isfinite(p.TimeStep) && p.TimeStep > 0 &&
            std::isfinite(p.SolverTolerance) && p.SolverTolerance > 0 && p.SolverTolerance < 1 &&
            p.MaxSolverIterations > 0 && p.MaxSolverIterations <= 100000 &&
            p.Iterations >= 1 && p.Iterations <= 10000 &&
            std::isfinite(p.Lambda) && p.Lambda > 0 && p.Lambda <= 1 &&
            std::isfinite(p.Mu) && p.Mu >= -1 && p.Mu < 0 &&
            std::isfinite(p.HeatTime) && p.HeatTime > 0 && p.HeatTime <= 1000 &&
            std::isfinite(p.RangeSigma) && p.RangeSigma > 0;
    }

    Sparse::SparseBuildResult BuildGraphLaplacian(const std::size_t count, const std::span<const PropertyEdge> edges)
    {
        std::vector<double> degree(count, 0.0);
        Sparse::SparseBuilder builder(count, count);
        builder.Reserve(2 * edges.size() + count);
        for (const PropertyEdge& edge : edges)
        {
            builder.Add(edge.A, edge.B, -edge.Weight);
            builder.Add(edge.B, edge.A, -edge.Weight);
            if (edge.A < count && edge.B < count)
            {
                degree[edge.A] += edge.Weight;
                degree[edge.B] += edge.Weight;
            }
        }
        for (std::size_t i = 0; i < count; ++i)
            builder.Add(i, i, degree[i]);
        return builder.Build();
    }

    std::optional<std::vector<PropertyEdge>> BuildPropertyNeighborhood(
        std::span<const glm::vec3> positions, std::uint32_t k,
        PropertyWeight weight, double sigma)
    {
        if (!k || k > 1024 || positions.empty() || positions.size() > std::numeric_limits<std::uint32_t>::max() || weight > PropertyWeight::InverseDistance ||
            !std::isfinite(sigma) || sigma <= 0 ||
            !std::ranges::all_of(positions, PointLBVH::ValidPoint)) return {};
        std::vector<PropertyEdge> edges;
        edges.reserve(positions.size() * k);
        PointLBVH::Index index;
        if (!index.Build(positions)) return {};
        for (std::uint32_t i = 0; i < positions.size(); ++i)
            for (const auto neighbor : index.KNearest(positions[i], k, i))
            {
                const auto delta = glm::dvec3(positions[i]) - glm::dvec3(positions[neighbor.Index]);
                const double distance = glm::length(delta);
                const double w = weight == PropertyWeight::Gaussian ? std::exp(-0.5 * (distance / sigma) * (distance / sigma))
                    : weight == PropertyWeight::InverseDistance ? 1.0 / std::max(distance, sigma * 1e-12) : 1.0;
                if (!std::isfinite(w)) return {};
                edges.push_back({std::min<std::size_t>(i, neighbor.Index), std::max<std::size_t>(i, neighbor.Index), w});
            }
        // Mutual neighbors produce the same pair twice with bitwise-equal weights; keep one, ordered by pair.
        const auto key = [](const PropertyEdge& e) { return std::pair{e.A, e.B}; };
        std::ranges::sort(edges, {}, key);
        const auto duplicates = std::ranges::unique(edges, {}, key);
        edges.erase(duplicates.begin(), duplicates.end());
        return edges;
    }

    std::optional<PropertyFilterPlan> PlanPropertyFilter(std::span<const double> input, std::size_t channels,
        std::span<const PropertyEdge> edges, const PropertyFilterParams& p,
        std::span<const std::size_t> fixedRows, std::span<const double> lumpedMass, std::string& diagnostic)
    {
        const auto fail = [&](const char* message) -> std::optional<PropertyFilterPlan> {
            diagnostic = message;
            return {};
        };
        if (p.Method == PropertyFilter::VariationalFit)
            return fail("Variational fitting is solved by HarmonicField::FitProperty.");
        if (!ValidatePropertyFilterParams(p) || channels < 1 || channels > 4 || input.empty() || input.size() % channels)
            return fail("Invalid filter parameters or property shape.");
        if (!std::ranges::all_of(input, [](double x) { return std::isfinite(x); }))
            return fail("Property contains non-finite live values.");
        PropertyFilterPlan plan;
        plan.Channels = channels;
        const std::size_t count = plan.Count = input.size() / channels;
        plan.Fixed.assign(count, false);
        for (auto row : fixedRows)
        {
            if (row >= count || plan.Fixed[row]) return fail("Invalid or duplicate fixed row.");
            plan.Fixed[row] = true;
        }
        if (p.Laplacian == PropertyLaplacian::LumpedMass &&
            (lumpedMass.size() != count || !std::ranges::all_of(lumpedMass,
                [](double m) { return std::isfinite(m) && m > 0; })))
            return fail("Lumped-mass Laplacian requires positive finite row masses.");
        plan.Degree.assign(count, 0.0);
        for (const auto& edge : edges)
        {
            if (edge.A >= count || edge.B >= count || edge.A == edge.B || !std::isfinite(edge.Weight) || edge.Weight < 0)
                return fail("Invalid nonnegative property neighborhood.");
            plan.Degree[edge.A] += edge.Weight;
            plan.Degree[edge.B] += edge.Weight;
        }
        if (!std::ranges::all_of(plan.Degree, [](double x) { return std::isfinite(x); }))
            return fail("Neighborhood degree overflow.");
        plan.Isolated.resize(count);
        for (std::size_t i = 0; i < count; ++i) plan.Isolated[i] = plan.Degree[i] == 0;
        const double maxDegree = *std::max_element(plan.Degree.begin(), plan.Degree.end());
        plan.Rate = p.Laplacian == PropertyLaplacian::RandomWalk ? 1.0 : maxDegree;
        if (p.Method == PropertyFilter::SpectralHeat && plan.Rate > 0)
        {
            // exp(-t L) = exp(-a) sum a^k/k! (I-L/rate)^k, a=t*rate.
            // Split a into <=1 intervals so all terms are nonnegative and well conditioned.
            const double total = p.HeatTime * plan.Rate;
            if (!std::isfinite(total) || total > 10000) return fail("Heat time times maximum degree exceeds 10000; reduce time or use random-walk Laplacian.");
            plan.HeatSplits = static_cast<std::size_t>(std::max(1.0, std::ceil(total)));
            const double a = total / double(plan.HeatSplits);
            double coefficient = std::exp(-a);
            plan.HeatCoefficients[0] = plan.HeatMass = coefficient;
            for (unsigned k = 1; k <= 18; ++k)
            {
                coefficient *= a / k;
                plan.HeatCoefficients[k] = coefficient;
                plan.HeatMass += coefficient;
            }
        }
        return plan;
    }

    bool PropertyFilterRun::Begin(std::span<const double> input,
        std::size_t channels,
        std::span<const PropertyEdge> edges,
        const PropertyFilterParams& p,
        std::span<const std::size_t> fixedRows,
        std::span<const double> lumpedMass,
        std::string& diagnostic)
    {
        *this = PropertyFilterRun{}; // a run begun again starts over
        auto plan = PlanPropertyFilter(input, channels, edges, p, fixedRows, lumpedMass, diagnostic);
        if (!plan)
            return false;
        m_Params = p;
        m_Plan = std::move(*plan);
        m_Input.assign(input.begin(), input.end());
        m_Edges.assign(edges.begin(), edges.end());
        m_Degree = m_Plan.Degree;
        m_Weights.reserve(edges.size());
        for (const auto& edge : edges)
            m_Weights.push_back(edge.Weight);
        m_Values = m_Input;
        m_Next.resize(input.size());
        m_Delta.resize(input.size());
        if (p.Method == PropertyFilter::Implicit)
        {
            auto system = AssemblePropertyImplicitSystem(m_Plan, input, edges, p, lumpedMass, diagnostic);
            if (!system)
                return false;
            m_System = std::move(*system);
            m_Direct = p.Solver == PropertySolver::Direct && m_Cholesky.factor(m_System.Matrix).Succeeded();
            m_Rhs.resize(m_Plan.Count);
            m_Solved.resize(m_Plan.Count);
            m_Backend = m_Direct                     ? "cpu_sparse_cholesky"
                : p.Solver == PropertySolver::Direct ? "cpu_reference (Cholesky factorization failed; used CG)"
                                                     : "cpu_reference";
        }
        else
        {
            if (p.Method == PropertyFilter::SpectralHeat)
            {
                m_Term.resize(input.size());
                m_Sum.resize(input.size());
            }
            m_Backend = "cpu_reference";
        }
        m_Began = true;
        return true;
    }

    void PropertyFilterRun::Apply(const std::vector<double>& source, std::vector<double>& target, const double step)
    {
        const std::size_t channels = m_Plan.Channels;
        std::fill(m_Delta.begin(), m_Delta.end(), 0.0);
        for (std::size_t e = 0; e < m_Edges.size(); ++e)
            for (std::size_t c = 0; c < channels; ++c)
            {
                const auto a = m_Edges[e].A * channels + c, b = m_Edges[e].B * channels + c;
                const double difference = m_Weights[e] * (source[b] - source[a]);
                m_Delta[a] += difference;
                m_Delta[b] -= difference;
            }
        for (std::size_t i = 0; i < m_Plan.Count; ++i)
            for (std::size_t c = 0; c < channels; ++c)
            {
                const auto j = i * channels + c;
                const double divisor = m_Params.Laplacian == PropertyLaplacian::RandomWalk ? m_Degree[i] : 1.0;
                target[j] = source[j] + (!m_Plan.Fixed[i] && divisor > 0 ? step * m_Delta[j] / divisor : 0.0);
            }
        ++m_Applications;
    }

    bool PropertyFilterRun::Step(std::string& diagnostic)
    {
        const auto& p = m_Params;
        if (!m_Began || m_Failed || m_Steps >= p.Iterations)
        {
            diagnostic = m_Failed ? "The filter run failed earlier." : "The filter run has no step left.";
            return false;
        }
        const std::size_t count = m_Plan.Count, channels = m_Plan.Channels;
        const double rate = m_Plan.Rate;
        if (p.Method == PropertyFilter::Implicit)
        {
            const Sparse::CGParams solver{p.MaxSolverIterations, p.SolverTolerance};
            for (std::size_t c = 0; c < channels; ++c)
            {
                for (std::size_t i = 0; i < count; ++i)
                {
                    m_Solved[i] = m_Values[i * channels + c];
                    m_Rhs[i] = m_System.RhsDiagonal[i] * m_Solved[i] + m_System.RhsConstant[c * count + i];
                }
                if (m_Direct)
                {
                    if (!m_Cholesky.solve(m_Rhs, m_Solved).Succeeded())
                    {
                        m_Failed = true;
                        diagnostic = "Implicit Cholesky solve failed; no property was changed.";
                        return false;
                    }
                    ++m_Applications;
                }
                else
                {
                    const auto status = Sparse::SolveCG(m_System.Matrix, m_Rhs, m_Solved, solver);
                    m_Applications += status.Iterations + 1;
                    if (!status.Converged)
                    {
                        m_Failed = true;
                        diagnostic = "Implicit solver failed to converge; no property was changed.";
                        return false;
                    }
                }
                for (std::size_t i = 0; i < count; ++i)
                    m_Values[i * channels + c] = m_Solved[i];
            }
        }
        else if (p.Method == PropertyFilter::SpectralHeat && rate > 0)
            for (std::size_t split = 0; split < m_Plan.HeatSplits; ++split)
            {
                m_Term = m_Values;
                for (std::size_t j = 0; j < m_Values.size(); ++j)
                    m_Sum[j] = m_Plan.HeatCoefficients[0] * m_Term[j];
                for (unsigned k = 1; k <= 18; ++k)
                {
                    Apply(m_Term, m_Next, 1.0 / rate);
                    m_Term.swap(m_Next);
                    for (std::size_t j = 0; j < m_Values.size(); ++j)
                        m_Sum[j] += m_Plan.HeatCoefficients[k] * m_Term[j];
                }
                for (std::size_t j = 0; j < m_Values.size(); ++j)
                    m_Values[j] = m_Sum[j] / m_Plan.HeatMass;
            }
        else if (p.Method != PropertyFilter::SpectralHeat && rate > 0)
        {
            if (p.Method == PropertyFilter::Bilateral)
            {
                std::fill(m_Degree.begin(), m_Degree.end(), 1.0);
                for (std::size_t e = 0; e < m_Edges.size(); ++e)
                {
                    double norm = 0;
                    for (std::size_t c = 0; c < channels; ++c)
                    {
                        const double d =
                            (m_Values[m_Edges[e].A * channels + c] - m_Values[m_Edges[e].B * channels + c]) /
                            p.RangeSigma;
                        norm += d * d;
                    }
                    m_Weights[e] = m_Edges[e].Weight * std::exp(-0.5 * norm);
                    m_Degree[m_Edges[e].A] += m_Weights[e];
                    m_Degree[m_Edges[e].B] += m_Weights[e];
                }
            }
            // Combinatorial explicit steps use the maximum-degree stability bound.
            const double scale = p.Laplacian == PropertyLaplacian::Combinatorial ? rate : 1.0;
            Apply(m_Values, m_Next, p.Lambda / scale);
            m_Values.swap(m_Next);
            if (p.Method == PropertyFilter::Taubin)
            {
                Apply(m_Values, m_Next, p.Mu / scale);
                m_Values.swap(m_Next);
            }
        }
        ++m_Steps;
        return true;
    }

    PropertyFilterResult PropertyFilterRun::Finish() const
    {
        auto completed = CompletePropertyFilter(m_Plan, m_Input, m_Values, m_Backend);
        completed.OperatorApplications = m_Applications;
        return completed;
    }

    PropertyFilterResult FilterProperty(std::span<const double> input,
        std::size_t channels,
        std::span<const PropertyEdge> edges,
        const PropertyFilterParams& p,
        std::span<const std::size_t> fixedRows,
        std::span<const double> lumpedMass)
    {
        PropertyFilterResult result;
        PropertyFilterRun run;
        if (!run.Begin(input, channels, edges, p, fixedRows, lumpedMass, result.Diagnostic))
            return result;
        while (run.Steps() < p.Iterations)
            if (!run.Step(result.Diagnostic))
                return result;
        return run.Finish();
    }

    std::optional<PropertyImplicitSystem> AssemblePropertyImplicitSystem(const PropertyFilterPlan& plan,
        std::span<const double> input, std::span<const PropertyEdge> edges, const PropertyFilterParams& p,
        std::span<const double> lumpedMass, std::string& diagnostic)
    {
        // Solve (M + dt L) x = M b. Random walk uses M = D, preserving symmetry; combinatorial uses
        // unit mass; custom mass supports area-aware fairing. Fixed rows become identity rows and
        // their coupling moves into the free right-hand side, keeping the system SPD.
        const std::size_t count = plan.Count, channels = plan.Channels;
        if (p.Method != PropertyFilter::Implicit || input.size() != count * channels ||
            (p.Laplacian == PropertyLaplacian::LumpedMass && lumpedMass.size() != count))
        {
            diagnostic = "Implicit system needs the implicit method and a matching plan.";
            return {};
        }
        PropertyImplicitSystem system;
        std::vector<double> mass(count, 1.0), diagonal(count);
        for (std::size_t i = 0; i < count; ++i)
        {
            if (!plan.Isolated[i]) mass[i] = p.Laplacian == PropertyLaplacian::RandomWalk
                ? plan.Degree[i] : p.Laplacian == PropertyLaplacian::LumpedMass ? lumpedMass[i] : 1.0;
            diagonal[i] = plan.Fixed[i] ? 1.0 : mass[i];
        }
        system.RhsDiagonal.resize(count);
        system.RhsConstant.assign(channels * count, 0.0);
        for (std::size_t i = 0; i < count; ++i)
        {
            system.RhsDiagonal[i] = plan.Fixed[i] ? 0.0 : mass[i];
            if (plan.Fixed[i])
                for (std::size_t c = 0; c < channels; ++c) system.RhsConstant[c * count + i] = input[i * channels + c];
        }
        Sparse::SparseBuilder builder(count, count);
        builder.Reserve(2 * edges.size() + count);
        for (const auto& edge : edges)
        {
            const double w = p.TimeStep * edge.Weight;
            for (const auto [row, col] : {std::pair{edge.A, edge.B}, std::pair{edge.B, edge.A}})
            {
                if (plan.Fixed[row]) continue;
                diagonal[row] += w;
                if (plan.Fixed[col])
                    for (std::size_t c = 0; c < channels; ++c)
                        system.RhsConstant[c * count + row] += w * input[col * channels + c];
                else builder.Add(row, col, -w);
            }
        }
        for (std::size_t i = 0; i < count; ++i) builder.Add(i, i, diagonal[i]);
        auto built = builder.Build();
        if (!built.Valid || !std::ranges::all_of(diagonal, [](double x) { return std::isfinite(x); }) ||
            !std::ranges::all_of(system.RhsConstant, [](double x) { return std::isfinite(x); }))
        {
            diagnostic = "Unable to assemble implicit system.";
            return {};
        }
        system.Matrix = std::move(built.Matrix);
        return system;
    }

    PropertyFilterResult CompletePropertyFilter(const PropertyFilterPlan& plan,
        std::span<const double> input, std::vector<double> values, std::string backend)
    {
        PropertyFilterResult result;
        const auto channels = plan.Channels;
        if (values.size() != input.size() || input.size() != plan.Count * channels)
        {
            result.Diagnostic = "Filter result has the wrong shape; no property was changed.";
            return result;
        }
        for (std::size_t i = 0; i < plan.Count; ++i)
            if (plan.Isolated[i] || plan.Fixed[i])
                for (std::size_t c = 0; c < channels; ++c) values[i * channels + c] = input[i * channels + c];
        if (!std::ranges::all_of(values, [](double x) { return std::isfinite(x); }))
        {
            result.Diagnostic = "Filter produced non-finite values; no property was changed.";
            return result;
        }
        result.Success = true;
        result.Values = std::move(values);
        result.Diagnostic = std::move(backend);
        return result;
    }
}
