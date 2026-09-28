#include "Bench.CoherentPointDriftAcceleratedSmoke.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <random>
#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
import Geometry.Registration.CoherentPointDrift;
namespace Intrinsic::Bench::Geometry
{
    namespace
    {
        namespace CPD = ::Geometry::CoherentPointDrift;

        std::vector<glm::vec3> Cloud(std::size_t count, std::uint32_t seed)
        {
            std::mt19937 random(seed);
            std::uniform_real_distribution<float> uniform(-1.0f, 1.0f);
            std::vector<glm::vec3> points(count);
            for (auto& p : points) p = {uniform(random), uniform(random), 0.6f * uniform(random)};
            return points;
        }

        double MaxDifference(const CPD::Result& a, const CPD::Result& b)
        {
            if (!a.Succeeded() || !b.Succeeded() || a.TransformedSource.size() != b.TransformedSource.size()) return 1.0e30;
            double worst = 0.0;
            for (std::size_t i = 0; i < a.TransformedSource.size(); ++i)
                worst = std::max(worst, glm::length(a.TransformedSource[i] - b.TransformedSource[i]));
            return worst;
        }

        template <class F>
        double Timed(F&& f)
        {
            const auto start = std::chrono::steady_clock::now();
            f();
            return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        }
    }

    CoherentPointDriftAcceleratedSmokeResult RunCoherentPointDriftAcceleratedSmoke()
    {
        CoherentPointDriftAcceleratedSmokeResult result;
        const auto source = Cloud(1000, 101);
        const glm::dmat3 rotation(glm::rotate(glm::dmat4(1.0), 0.5, glm::normalize(glm::dvec3(1.0, 2.0, 0.5))));
        std::mt19937 random(102);
        std::normal_distribution<double> noise(0.0, 0.004);
        std::vector<glm::vec3> target;
        for (const auto& p : source)
            target.push_back(glm::vec3(rotation * glm::dvec3(p) + glm::dvec3(0.3, -0.1, 0.2) +
                                       glm::dvec3(noise(random), noise(random), noise(random))));
        const CPD::Params reference{.OutlierWeight = 0.05, .MaxIterations = 80};
        CPD::Result exact, dense, truncated, automatic;
        result.ReferenceMilliseconds = Timed([&] { exact = CPD::Register(target, source, reference); });
        CPD::Params p = reference;
        p.EStepTolerance = 1e-8;
        p.EStep = CPD::EStepPolicy::Dense;
        result.DenseMilliseconds = Timed([&] { dense = CPD::Register(target, source, p); });
        p.EStep = CPD::EStepPolicy::Truncated;
        result.TruncatedMilliseconds = Timed([&] { truncated = CPD::Register(target, source, p); });
        p.EStep = CPD::EStepPolicy::Auto;
        result.AutoMilliseconds = Timed([&] { automatic = CPD::Register(target, source, p); });
        result.MaxRigidParity = std::max({MaxDifference(exact, dense), MaxDifference(exact, truncated),
                                          MaxDifference(exact, automatic)});
        result.TruncatedErrorBound = truncated.EStepErrorBound;

        const auto sheet = Cloud(300, 103);
        std::vector<glm::vec3> bent;
        for (const auto& q : sheet) bent.push_back(q + glm::vec3(0.0f, 0.1f * std::sin(2.0f * q.x), 0.08f * std::cos(1.5f * q.y)));
        const CPD::Params full{.Method = CPD::Variant::Nonrigid, .MaxIterations = 40, .Tolerance = 0.0};
        CPD::Result fullResult, lowRank;
        result.FullNonrigidMilliseconds = Timed([&] { fullResult = CPD::Register(bent, sheet, full); });
        CPD::Params low = full;
        low.LowRank = 60;
        result.LowRankMilliseconds = Timed([&] { lowRank = CPD::Register(bent, sheet, low); });
        result.LowRankParity = MaxDifference(fullResult, lowRank);
        result.KernelApproximationError = lowRank.KernelApproximationError;

        result.RuntimeMilliseconds = result.ReferenceMilliseconds + result.DenseMilliseconds + result.TruncatedMilliseconds +
                                     result.AutoMilliseconds + result.FullNonrigidMilliseconds + result.LowRankMilliseconds;
        result.Succeeded = exact.Succeeded() && dense.Succeeded() && truncated.Succeeded() && automatic.Succeeded() &&
                           fullResult.Succeeded() && lowRank.Succeeded() && result.TruncatedErrorBound <= 1e-8;
        return result;
    }
}
