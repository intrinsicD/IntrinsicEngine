#include "Bench.SphFluidReferenceSmoke.hpp"

#include "SphFluidReference.hpp"

#include <chrono>
#include <cmath>
#include <vector>

namespace Intrinsic::Bench::Physics
{
    namespace
    {
        constexpr int kWarmupIterations = 1;
        constexpr int kMeasuredIterations = 4;
        constexpr int kColumnSteps = 30;
        constexpr double kSpacing = 0.05;

        using namespace Intrinsic::Methods::Physics::SphFluidReference;

        [[nodiscard]] auto MakeGrid(std::size_t side, double yOffset) -> std::vector<ParticleState>
        {
            std::vector<ParticleState> particles;
            particles.reserve(side * side * side);
            for (std::size_t z = 0; z < side; ++z)
            {
                for (std::size_t y = 0; y < side; ++y)
                {
                    for (std::size_t x = 0; x < side; ++x)
                    {
                        ParticleState particle{};
                        particle.Position = Vec3{static_cast<double>(x) * kSpacing,
                                                 static_cast<double>(y) * kSpacing + yOffset,
                                                 static_cast<double>(z) * kSpacing};
                        particles.push_back(particle);
                    }
                }
            }
            return particles;
        }

        [[nodiscard]] auto BaseParams() -> StepParams
        {
            StepParams params{};
            params.SmoothingLength = 2.0 * kSpacing;
            params.RestDensity = 1000.0;
            params.ParticleMass = params.RestDensity * kSpacing * kSpacing * kSpacing;
            params.DeltaTime = 1.0 / 240.0;
            return params;
        }

        // Quality: interior-particle relative density error on a static 5^3
        // lattice (kernel discretization accuracy), plus a dynamic 3^3 toy
        // column drop for stability diagnostics.
        [[nodiscard]] auto RunWorkload() -> SphFluidReferenceSmokeMetrics
        {
            SphFluidReferenceSmokeMetrics metrics{};

            const auto grid = MakeGrid(5, 0.0);
            StepParams gridParams = BaseParams();
            gridParams.Gravity = Vec3{};
            const std::vector<double> densities = ComputeDensities(grid, gridParams);
            const std::size_t center = (2 * 5 + 2) * 5 + 2;
            metrics.QualityErrorL2 =
                std::abs(densities[center] - gridParams.RestDensity) / gridParams.RestDensity;

            auto column = MakeGrid(3, 0.2);
            StepParams columnParams = BaseParams();
            columnParams.Gravity = Vec3{0.0, -9.80665, 0.0};
            columnParams.Viscosity = 0.2;
            columnParams.Boundaries = {MakeBoundaryPlane(Vec3{0.0, 1.0, 0.0}, 0.0)};

            bool valid = true;
            Diagnostics lastDiagnostics{};
            for (int i = 0; i < kColumnSteps; ++i)
            {
                const StepResult result = Step(column, columnParams);
                valid = valid && result.Diagnostics.Code == ValidationCode::Valid &&
                        result.Diagnostics.Stable;
                lastDiagnostics = result.Diagnostics;
                column = result.Particles;
            }

            metrics.ColumnMaxCompression = lastDiagnostics.MaxCompression;
            metrics.ColumnAverageDensityError = lastDiagnostics.AverageDensityError;
            metrics.ColumnMaxNeighborCount = lastDiagnostics.MaxNeighborCount;
            metrics.ColumnStable = valid;
            metrics.Succeeded = valid && metrics.QualityErrorL2 <= 5.0e-2;
            return metrics;
        }
    } // namespace

    SphFluidReferenceSmokeMetrics RunSphFluidReferenceSmoke()
    {
        for (int i = 0; i < kWarmupIterations; ++i)
        {
            (void)RunWorkload();
        }

        SphFluidReferenceSmokeMetrics last{};
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < kMeasuredIterations; ++i)
        {
            last = RunWorkload();
        }
        const auto t1 = std::chrono::steady_clock::now();

        const auto totalNs = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
        last.RuntimeMilliseconds = (static_cast<double>(totalNs) / static_cast<double>(kMeasuredIterations)) * 1.0e-6;
        return last;
    }
} // namespace Intrinsic::Bench::Physics
