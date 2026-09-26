#include "Bench.HarmonicFieldSmoke.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <vector>
import Geometry.HarmonicField;
namespace Intrinsic::Bench::Geometry
{
    HarmonicFieldSmokeMetrics RunHarmonicFieldSmoke()
    {
        namespace H = ::Geometry::HarmonicField;
        using Edge = ::Geometry::Smoothing::PropertyEdge;
        // Harmonic fields on a path are linear between hard rows; interior biharmonic rows are
        // fourth differences, so four pins reproduce a cubic; a constant source gives a quadratic
        // (Poisson); unit flux through a free path gives a zero-mean line (grounded Neumann); a
        // total-variation fit shrinks a step without blurring it; seeds split labels at the midpoint.
        constexpr std::size_t n = 2048, channels = 3;
        // The biharmonic system's condition number grows like rows^4, so the cubic runs on a
        // shorter path to keep the 1e-7 accuracy bound meaningful.
        constexpr std::size_t nb = 256;
        std::vector<Edge> path;
        for (std::size_t i = 0; i + 1 < n; ++i) path.push_back({i, i + 1, 1.0});
        const std::vector<Edge> shortPath(path.begin(), path.begin() + (nb - 1));
        const auto cubic = [](double t) { return t * t * t - 1.5 * t * t + 0.25 * t; };
        HarmonicFieldSmokeMetrics metrics{.Succeeded = true};
        const auto tick = [&] {
            std::vector<double> linear(n * channels, 0.0);
            for (std::size_t c = 0; c < channels; ++c) { linear[c] = double(c); linear[(n - 1) * channels + c] = double(c) + 1; }
            const std::vector<std::size_t> ends{0, n - 1};
            const auto harmonic = H::Solve(linear, channels, path, {}, ends);
            if (!harmonic.Success) { metrics.Succeeded = false; return; }
            for (std::size_t i = 0; i < n; ++i)
                for (std::size_t c = 0; c < channels; ++c)
                    metrics.MaxError = std::max(metrics.MaxError,
                        std::abs(harmonic.Values[i * channels + c] - (double(c) + double(i) / double(n - 1))));

            std::vector<double> values(nb, 0.0);
            const std::vector<std::size_t> pins{0, 1, nb - 2, nb - 1};
            for (auto p : pins) values[p] = cubic(double(p) / double(nb - 1));
            const auto biharmonic = H::Solve(values, 1, shortPath, {.Order = H::FieldOrder::Biharmonic}, pins);
            if (!biharmonic.Success) { metrics.Succeeded = false; return; }
            for (std::size_t i = 0; i < nb; ++i)
                metrics.MaxError = std::max(metrics.MaxError, std::abs(biharmonic.Values[i] - cubic(double(i) / double(nb - 1))));

            const double h = 1.0 / double(n - 1);
            std::vector<double> ends2(n, 0.0), source(n, -2.0 * h * h);
            ends2[n - 1] = 1.0;
            const auto poisson = H::Solve(ends2, 1, path, {}, ends, {}, {}, source);
            if (!poisson.Success) { metrics.Succeeded = false; return; }
            for (std::size_t i = 0; i < n; ++i)
                metrics.MaxError = std::max(metrics.MaxError, std::abs(poisson.Values[i] - double(i) * h * double(i) * h));

            std::vector<double> flux(n, 0.0);
            flux[0] = h; flux[n - 1] = -h;
            const auto neumann = H::Solve(std::vector<double>(n, 0.0), 1, path,
                                          {.Unconstrained = H::UnconstrainedPolicy::ZeroMean}, {}, {}, {}, flux);
            if (!neumann.Success) { metrics.Succeeded = false; return; }
            for (std::size_t i = 0; i < n; ++i)
                metrics.MaxError = std::max(metrics.MaxError, std::abs(neumann.Values[i] - (0.5 - double(i) * h)));

            // Total-variation fit of a unit step: each plateau of m rows moves 1 / (2 lambda m)
            // toward the other and stays flat.
            std::vector<double> step(nb, 0.0);
            std::fill(step.begin() + nb / 2, step.end(), 1.0);
            ::Geometry::Smoothing::PropertyFilterParams tv{.Method = ::Geometry::Smoothing::PropertyFilter::VariationalFit,
                .Laplacian = ::Geometry::Smoothing::PropertyLaplacian::Combinatorial};
            tv.SmoothnessPenalty = ::Geometry::Smoothing::FitPenalty::L1;
            tv.PenaltyDelta = 1e-9; // bias ~ delta * rows; smaller deltas lose Cholesky accuracy (weights ~ 1 / delta)
            tv.FitTolerance = 1e-12;
            const auto fit = H::FitProperty(step, 1, shortPath, tv);
            if (!fit.Success) { metrics.Succeeded = false; return; }
            const double shift = 1.0 / double(nb);
            for (std::size_t i = 0; i < nb; ++i)
                metrics.MaxError = std::max(metrics.MaxError, std::abs(fit.Values[i] - (i < nb / 2 ? shift : 1.0 - shift)));

            std::vector<std::int32_t> labels(n, 0);
            labels[0] = 1; labels[n - 1] = 2;
            const auto split = H::PropagateLabels(labels, 0, path, {});
            if (!split.Success) { metrics.Succeeded = false; return; }
            metrics.MislabeledRows = 0;
            for (std::size_t i = 0; i < n; ++i)
                if (split.Labels[i] != (2 * i < n - 1 ? 1 : 2)) ++metrics.MislabeledRows;
        };
        tick();
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < 8; ++i) tick();
        metrics.RuntimeMilliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() / 8;
        metrics.Succeeded = metrics.Succeeded && metrics.MaxError <= 1e-7 && metrics.MislabeledRows == 0;
        return metrics;
    }
}
