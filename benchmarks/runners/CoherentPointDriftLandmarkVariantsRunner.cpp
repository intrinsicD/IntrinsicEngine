// METHOD-077: one Bayesian CPD registration per call with a chosen Nystroem landmark variant
// (METHOD-076), for the landmark-variant experiment. Not a CTest; driven by
// tools/benchmark/cpd_landmark_variants.py, which owns the run plan, timeouts and merging.
//
//   IntrinsicCoherentPointDriftLandmarkVariants --fixture full|partial --seed S --points N
//       --variant exact|<source>/<schedule>/<pattern> [--landmarks L] [--error-limit E]
//       [--threads T] --out <json> [--positions <bin>] [--reference <bin>] [--dump K]
//
// Fixture: N points on a bumpy closed surface (seeded); truth = similarity(bend(source));
// target = truth + N(0, 0.004^2) noise with every 20th point replaced by uniform clutter;
// "partial" keeps only target points with truth x < 0.2. "exact" runs the exact E-step (Auto)
// and may write its registered points (--positions, only when complete) as the reference other
// variants are compared with (--reference, exactly N positions). Writes one JSON object; exit 0
// complete, 1 incomplete result, 2 usage, 3 no comparison with the reference possible.
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "CoherentPointDriftScalingFixture.hpp"

import Geometry.Registration.CoherentPointDrift;

namespace
{
    namespace CPD = Geometry::CoherentPointDrift;
    namespace E = CPD::EStep;

    struct Fixture
    {
        std::vector<glm::vec3> Source, Target, Truth; // Truth[i]: image of Source[i]
        std::vector<bool> Clutter;                     // per target point
    };

    Fixture Make(const std::string& kind, const std::size_t count, const std::uint32_t seed)
    {
        Fixture f;
        f.Source = CpdScaling::Surface(count, seed);
        const glm::dmat3 rotation(glm::rotate(glm::dmat4(1.0), 0.3, glm::normalize(glm::dvec3(1.0, 2.0, 0.5))));
        const double scale = 1.1;
        const glm::dvec3 translation{0.3, -0.2, 0.1};
        std::mt19937 random(seed ^ 0x9e3779b9u);
        std::normal_distribution<double> noise(0.0, 0.004);
        std::uniform_real_distribution<double> clutter(-1.6, 1.6);
        for (std::size_t i = 0; i < count; ++i)
        {
            const glm::dvec3 p(f.Source[i]);
            const glm::dvec3 bent = p + glm::dvec3(0.0, 0.12 * std::sin(1.8 * p.x), 0.1 * std::cos(1.4 * p.y));
            const glm::dvec3 q = scale * (rotation * bent) + translation;
            f.Truth.push_back(glm::vec3(q));
            const bool isClutter = i % 20 == 0;
            const glm::dvec3 x = isClutter ? glm::dvec3(clutter(random), clutter(random), clutter(random))
                                           : q + glm::dvec3(noise(random), noise(random), noise(random));
            if (kind == "partial" && !isClutter && q.x >= 0.2) continue;
            f.Target.push_back(glm::vec3(x));
            f.Clutter.push_back(isClutter);
        }
        return f;
    }

    // A reference of exactly `count` double3 positions (any other size is refused).
    std::optional<std::vector<glm::dvec3>> ReadPositions(const std::string& path, const std::size_t count)
    {
        std::ifstream in(path, std::ios::binary | std::ios::ate);
        if (!in || std::size_t(in.tellg()) != count * sizeof(glm::dvec3)) return std::nullopt;
        in.seekg(0);
        std::vector<glm::dvec3> out(count);
        if (!in.read(reinterpret_cast<char*>(out.data()), std::streamsize(count * sizeof(glm::dvec3)))) return std::nullopt;
        return out;
    }

    bool ParseVariant(const std::string& text, CPD::Params& params)
    {
        if (text == "exact") return true;
        std::stringstream parts(text);
        std::string source, schedule, pattern;
        if (!std::getline(parts, source, '/') || !std::getline(parts, schedule, '/') || !std::getline(parts, pattern)) return false;
        const std::map<std::string, E::NystromLandmarkSource> sources{{"points", E::NystromLandmarkSource::Points},
                                                                      {"box", E::NystromLandmarkSource::BoundingBox}};
        const std::map<std::string, E::NystromLandmarkSchedule> schedules{{"mixed", E::NystromLandmarkSchedule::Mixed},
                                                                          {"fixed", E::NystromLandmarkSchedule::Fixed},
                                                                          {"resampled", E::NystromLandmarkSchedule::Resampled}};
        const std::map<std::string, E::NystromBoxPattern> patterns{{"random", E::NystromBoxPattern::Random},
                                                                   {"halton", E::NystromBoxPattern::Halton}};
        if (!sources.contains(source) || !schedules.contains(schedule) || !patterns.contains(pattern)) return false;
        params.EStep = CPD::EStepPolicy::Nystrom;
        params.NystromSource = sources.at(source);
        params.NystromSchedule = schedules.at(schedule);
        params.NystromPattern = patterns.at(pattern);
        return true;
    }
}

int main(int argc, char** argv)
{
    std::map<std::string, std::string> args;
    for (int i = 1; i + 1 < argc; i += 2) args[argv[i]] = argv[i + 1];
    const auto get = [&](const std::string& key, const std::string& fallback) {
        return args.contains(key) ? args.at(key) : fallback;
    };
    const std::string kind = get("--fixture", ""), variant = get("--variant", ""), out = get("--out", "");
    if ((kind != "full" && kind != "partial") || ((variant.empty() || out.empty()) && !args.contains("--dump")))
    {
        std::cerr << "usage: see the header of CoherentPointDriftLandmarkVariantsRunner.cpp\n";
        return 2;
    }
    const auto seed = std::uint32_t(std::stoul(get("--seed", "1")));
    const auto count = std::size_t(std::stoul(get("--points", "100000")));
    const Fixture f = Make(kind, count, seed);
    if (args.contains("--dump"))
    {
        // Raw data check: counts, clutter share and the first and last points of each set.
        const auto k = std::size_t(std::stoul(args.at("--dump")));
        std::size_t clutter = 0;
        for (const bool c : f.Clutter) clutter += c;
        std::cout << "source " << f.Source.size() << " target " << f.Target.size() << " clutter " << clutter << "\n";
        const auto show = [&](const char* name, const std::vector<glm::vec3>& v) {
            for (std::size_t i = 0; i < v.size(); ++i)
                if (i < k || i + k >= v.size())
                    std::cout << name << "[" << i << "] " << v[i].x << " " << v[i].y << " " << v[i].z << "\n";
        };
        show("source", f.Source);
        show("truth", f.Truth);
        show("target", f.Target);
        return 0;
    }

    // Bayesian CPD in normalized units (NormalizeInputs): beta, lambda and omega are unitless.
    CPD::Params params;
    params.Method = CPD::Variant::Bayesian;
    params.OutlierWeight = 0.1;
    params.Beta = 1.0;
    params.Lambda = 2.0;
    params.MaxIterations = 150;
    params.LowRank = 100;
    params.EStep = CPD::EStepPolicy::Auto;
    params.Threads = std::uint32_t(std::stoul(get("--threads", "16")));
    params.NystromLandmarks = std::uint32_t(std::stoul(get("--landmarks", "256")));
    params.NystromErrorLimit = std::stod(get("--error-limit", "0.001"));
    if (!ParseVariant(variant, params))
    {
        std::cerr << "unknown variant " << variant << "\n";
        return 2;
    }

    std::uint32_t nystromIterations = 0u, iterations = 0u;
    double maxSampled = 0.0;
    const auto start = std::chrono::steady_clock::now();
    const CPD::Result result = CPD::Register(f.Target, f.Source, params, [&](const CPD::IterationTrace& t) {
        ++iterations;
        nystromIterations += t.EStep == CPD::EStepPolicy::Nystrom;
        maxSampled = std::max(maxSampled, t.EStepSampledError);
    });
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();

    // Metrics only for a complete result (every source point registered); otherwise null.
    const bool complete = result.Succeeded() && result.TransformedSource.size() == f.Source.size();
    std::ostringstream json;
    json.precision(17);
    json << "{\"fixture\":\"" << kind << "\",\"seed\":" << seed << ",\"points\":" << count << ",\"targets\":" << f.Target.size()
         << ",\"variant\":\"" << variant << "\",\"landmarks\":" << params.NystromLandmarks
         << ",\"error_limit\":" << params.NystromErrorLimit << ",\"threads\":" << params.Threads
         << ",\"status\":\"" << CPD::ToString(result.State) << "\",\"termination\":\"" << CPD::ToString(result.Stop)
         << "\",\"iterations\":" << result.Iterations << ",\"nystrom_iterations\":" << nystromIterations
         << ",\"observed_iterations\":" << iterations << ",\"max_sampled_error\":" << maxSampled
         << ",\"kernel_evaluations\":" << result.KernelEvaluations << ",\"seconds\":" << seconds
         << ",\"sigma2\":" << result.Sigma2 << ",\"scale\":" << result.Scale << ",\"backend\":\"" << result.Backend << "\"";
    if (complete)
    {
        double truthSum = 0.0;
        for (std::size_t i = 0; i < f.Source.size(); ++i)
        {
            const glm::dvec3 d = result.TransformedSource[i] - glm::dvec3(f.Truth[i]);
            truthSum += glm::dot(d, d);
        }
        json << ",\"rms_to_truth\":" << std::sqrt(truthSum / double(f.Source.size()));
    }
    else
        json << ",\"rms_to_truth\":null";
    int code = complete ? 0 : 1;
    if (args.contains("--reference"))
    {
        const auto reference = ReadPositions(args.at("--reference"), f.Source.size());
        if (!reference || !complete)
        {
            json << ",\"rms_to_exact\":null,\"max_to_exact\":null,\"reference_error\":\""
                 << (reference ? "incomplete result" : "reference missing or of the wrong size") << "\"";
            code = 3;
        }
        else
        {
            double sum = 0.0, worst = 0.0;
            for (std::size_t i = 0; i < f.Source.size(); ++i)
            {
                const double d = glm::length(result.TransformedSource[i] - (*reference)[i]);
                sum += d * d;
                worst = std::max(worst, d);
            }
            json << ",\"rms_to_exact\":" << std::sqrt(sum / double(f.Source.size())) << ",\"max_to_exact\":" << worst;
        }
    }
    json << "}";
    // Only a complete result becomes a reference.
    if (args.contains("--positions") && complete)
    {
        std::ofstream positions(args.at("--positions"), std::ios::binary);
        positions.write(reinterpret_cast<const char*>(result.TransformedSource.data()),
                        std::streamsize(result.TransformedSource.size() * sizeof(glm::dvec3)));
    }
    std::ofstream(out) << json.str() << "\n";
    std::cout << json.str() << "\n";
    return code;
}
