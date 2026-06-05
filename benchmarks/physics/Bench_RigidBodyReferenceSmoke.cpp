#include "Bench.RigidBodyReferenceSmoke.hpp"

#include "RigidBodyReference.hpp"

#include <chrono>
#include <cmath>
#include <vector>

namespace Intrinsic::Bench::Physics
{
    namespace
    {
        constexpr int kWarmupIterations = 1;
        constexpr int kMeasuredIterations = 8;

        using namespace Intrinsic::Methods::Physics::RigidBodyReference;

        [[nodiscard]] auto RunWorkload() -> RigidBodyReferenceSmokeMetrics
        {
            std::vector<BodyState> bodies{
                MakeDynamicBody(Vec3{-0.75, 0.0, 0.0}, 1.0, {MakeSphere(1.0)}),
                MakeDynamicBody(Vec3{0.75, 0.0, 0.0}, 1.0, {MakeSphere(1.0)}),
            };
            bodies[0].LinearVelocity = Vec3{1.0, 0.0, 0.0};
            bodies[1].LinearVelocity = Vec3{-1.0, 0.0, 0.0};

            StepParams params{};
            params.DeltaTime = 0.01;
            params.Gravity = Vec3{};
            params.Restitution = 1.0;
            params.SolverIterations = 8;
            params.PenetrationSlop = 0.0;
            params.PositionCorrectionPercent = 1.0;

            const StepResult result = Step(bodies, params);

            const double errorA = result.Bodies.size() > 0 ? result.Bodies[0].LinearVelocity.X + 1.0 : 1.0;
            const double errorB = result.Bodies.size() > 1 ? result.Bodies[1].LinearVelocity.X - 1.0 : 1.0;

            RigidBodyReferenceSmokeMetrics metrics{};
            metrics.QualityErrorL2 = std::sqrt(errorA * errorA + errorB * errorB);
            metrics.ContactCount = result.Diagnostics.ContactCount;
            metrics.UnsupportedPairCount = result.Diagnostics.UnsupportedPairCount;
            metrics.FinalVelocityA = result.Bodies.size() > 0 ? result.Bodies[0].LinearVelocity.X : 0.0;
            metrics.FinalVelocityB = result.Bodies.size() > 1 ? result.Bodies[1].LinearVelocity.X : 0.0;
            metrics.Succeeded = result.Diagnostics.Code == ValidationCode::Valid && metrics.ContactCount == 1 &&
                                metrics.UnsupportedPairCount == 0 && metrics.QualityErrorL2 <= 1.0e-12;
            return metrics;
        }
    } // namespace

    RigidBodyReferenceSmokeMetrics RunRigidBodyReferenceSmoke()
    {
        for (int i = 0; i < kWarmupIterations; ++i)
        {
            (void)RunWorkload();
        }

        RigidBodyReferenceSmokeMetrics last{};
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
