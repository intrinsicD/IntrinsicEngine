#include "Bench.CoherentPointDriftReferenceSmoke.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
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
        constexpr int kWarmup = 1;
        constexpr int kMeasured = 3;

        struct Fixture
        {
            std::vector<glm::vec3> Source, Target, Truth; // Truth[i] = ground-truth image of Source[i]
            CPD::Params Params{};
        };

        std::vector<glm::vec3> Cloud(std::size_t count, std::uint32_t seed)
        {
            std::mt19937 random(seed);
            std::uniform_real_distribution<float> uniform(-1.0f, 1.0f);
            std::vector<glm::vec3> points(count);
            for (auto& p : points) p = {uniform(random), uniform(random), 0.6f * uniform(random)};
            return points;
        }

        Fixture Linear(const glm::dmat3& linear, const glm::dvec3& translation, CPD::Variant variant, std::uint32_t seed)
        {
            Fixture f;
            f.Source = Cloud(400, seed);
            std::mt19937 random(seed + 1);
            std::normal_distribution<double> noise(0.0, 0.005);
            for (const auto& p : f.Source)
            {
                const glm::dvec3 q = linear * glm::dvec3(p) + translation;
                f.Truth.push_back(glm::vec3(q));
                f.Target.push_back(glm::vec3(q + glm::dvec3(noise(random), noise(random), noise(random))));
            }
            f.Params.Method = variant;
            f.Params.OutlierWeight = 0.05;
            return f;
        }

        Fixture Bend()
        {
            Fixture f;
            for (int y = 0; y < 12; ++y)
                for (int x = 0; x < 12; ++x)
                {
                    const glm::vec3 p{float(x) / 11.0f * 2.0f - 1.0f, float(y) / 11.0f * 2.0f - 1.0f, 0.0f};
                    f.Source.push_back(p);
                    f.Truth.push_back(p + glm::vec3(0.1f * std::sin(2.0f * p.y), 0.0f, 0.1f * std::cos(1.5f * p.x)));
                }
            f.Target = f.Truth;
            f.Params.Method = CPD::Variant::Nonrigid;
            f.Params.MaxIterations = 100;
            return f;
        }

        CoherentPointDriftVariantMetrics Measure(const Fixture& f, const CPD::Result& r)
        {
            CoherentPointDriftVariantMetrics m;
            m.Succeeded = r.Succeeded();
            m.Iterations = r.Iterations;
            m.Sigma2 = r.Sigma2;
            m.NegativeLogLikelihood = r.NegativeLogLikelihood;
            m.Termination = CPD::ToString(r.Stop).data();
            if (!m.Succeeded) return m;
            double error = 0.0, initial = 0.0;
            for (std::size_t i = 0; i < f.Source.size(); ++i)
            {
                const glm::dvec3 d = r.TransformedSource[i] - glm::dvec3(f.Truth[i]);
                const glm::dvec3 d0 = glm::dvec3(f.Source[i]) - glm::dvec3(f.Truth[i]);
                error += glm::dot(d, d);
                initial += glm::dot(d0, d0);
            }
            m.RmsError = std::sqrt(error / double(f.Source.size()));
            m.InitialRmsError = std::sqrt(initial / double(f.Source.size()));
            return m;
        }
    }

    CoherentPointDriftReferenceSmokeResult RunCoherentPointDriftReferenceSmoke()
    {
        const glm::dmat3 rotation = glm::dmat3(glm::rotate(glm::dmat4(1.0), 0.6, glm::normalize(glm::dvec3(1, 2, 0.5))));
        const Fixture rigid = Linear(1.3 * rotation, {0.4, -0.2, 0.7}, CPD::Variant::Rigid, 101);
        const Fixture affine = Linear(glm::dmat3{1.2, 0.1, -0.05, 0.2, 0.8, 0.1, -0.1, 0.15, 1.1}, {-0.3, 0.5, 0.2},
                                      CPD::Variant::Affine, 202);
        const Fixture bend = Bend();
        CoherentPointDriftReferenceSmokeResult result;
        result.Succeeded = true;
        for (int run = 0; run < kWarmup + kMeasured; ++run)
        {
            const auto start = std::chrono::steady_clock::now();
            const auto r = CPD::Register(rigid.Target, rigid.Source, rigid.Params);
            const auto a = CPD::Register(affine.Target, affine.Source, affine.Params);
            const auto n = CPD::Register(bend.Target, bend.Source, bend.Params);
            const auto end = std::chrono::steady_clock::now();
            if (run < kWarmup) continue;
            result.RuntimeMilliseconds += std::chrono::duration<double, std::milli>(end - start).count() / kMeasured;
            result.Rigid = Measure(rigid, r);
            result.Affine = Measure(affine, a);
            result.Nonrigid = Measure(bend, n);
        }
        for (const auto* m : {&result.Rigid, &result.Affine, &result.Nonrigid})
        {
            result.Succeeded = result.Succeeded && m->Succeeded;
            result.MaxRmsError = std::max(result.MaxRmsError, m->RmsError);
        }
        return result;
    }
}
