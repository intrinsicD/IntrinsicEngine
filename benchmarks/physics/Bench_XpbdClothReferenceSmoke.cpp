#include "Bench.XpbdClothReferenceSmoke.hpp"

#include "XpbdClothReference.hpp"

#include <chrono>
#include <vector>

namespace Intrinsic::Bench::Physics
{
    namespace
    {
        constexpr int kWarmupIterations = 1;
        constexpr int kMeasuredIterations = 8;
        constexpr int kSteps = 24;

        using namespace Intrinsic::Methods::Physics::XpbdClothReference;

        // Pinned 3x3 hanging patch (top row pinned) under gravity with rigid
        // constraints; the final max stretch residual is the quality metric.
        [[nodiscard]] auto RunWorkload() -> XpbdClothReferenceSmokeMetrics
        {
            constexpr double kSpacing = 0.25;
            std::vector<Vec3> positions;
            std::vector<Triangle> triangles;
            for (std::size_t z = 0; z < 3; ++z)
            {
                for (std::size_t x = 0; x < 3; ++x)
                {
                    positions.push_back(Vec3{static_cast<double>(x) * kSpacing, 0.0,
                                             static_cast<double>(z) * kSpacing});
                }
            }
            for (std::size_t z = 0; z < 2; ++z)
            {
                for (std::size_t x = 0; x < 2; ++x)
                {
                    const std::size_t i = z * 3 + x;
                    triangles.push_back(Triangle{i, i + 1, i + 3});
                    triangles.push_back(Triangle{i + 1, i + 4, i + 3});
                }
            }
            ClothState state = BuildClothFromTriangles(positions, triangles, 0.1, 0.0, 0.0);
            for (std::size_t x = 0; x < 3; ++x)
            {
                state.Particles[x].InverseMass = 0.0;
            }

            StepParams params{};
            params.DeltaTime = 1.0 / 60.0;
            params.Gravity = Vec3{0.0, -9.80665, 0.0};
            params.Iterations = 20;
            params.ResidualTolerance = 5.0e-3;

            XpbdClothReferenceSmokeMetrics metrics{};
            bool valid = true;
            Diagnostics lastDiagnostics{};
            for (int i = 0; i < kSteps; ++i)
            {
                const StepResult result = Step(state, params);
                valid = valid && result.Diagnostics.Code == ValidationCode::Valid &&
                        result.Diagnostics.Stable;
                lastDiagnostics = result.Diagnostics;
                state = result.State;
            }

            metrics.QualityErrorL2 = lastDiagnostics.MaxStretchResidual;
            metrics.MaxBendResidual = lastDiagnostics.MaxBendResidual;
            metrics.DegenerateTriangleCount = lastDiagnostics.DegenerateTriangleCount;
            metrics.DegenerateConstraintCount = lastDiagnostics.DegenerateConstraintCount;
            metrics.Converged = lastDiagnostics.Converged;
            metrics.Succeeded = valid && metrics.Converged &&
                                metrics.DegenerateConstraintCount == 0;
            return metrics;
        }
    } // namespace

    XpbdClothReferenceSmokeMetrics RunXpbdClothReferenceSmoke()
    {
        for (int i = 0; i < kWarmupIterations; ++i)
        {
            (void)RunWorkload();
        }

        XpbdClothReferenceSmokeMetrics last{};
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
