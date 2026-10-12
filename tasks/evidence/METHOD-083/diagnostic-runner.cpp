// Loads identical serialized float coordinates for the old and adapted engines.
// METHOD-083 owns the normalized frame, frozen run plan and endpoint analysis.
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <map>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>



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

    [[noreturn]] void Fail(const std::string& message) { std::cerr << message << "\n"; std::exit(2); }

    std::vector<glm::vec3> ReadPoints(const std::string& path)
    {
        std::ifstream in(path);
        if (!in) Fail("cannot open points: " + path);
        std::vector<glm::vec3> points;
        std::string line;
        while (std::getline(in, line))
        {
            std::istringstream row(line);
            glm::vec3 p;
            std::string extra;
            if (!(row >> p.x >> p.y >> p.z) || row >> extra ||
                !std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z))
                Fail("invalid point row: " + path);
            points.push_back(p);
        }
        if (points.empty()) Fail("empty point set: " + path);
        return points;
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
        std::map<std::string, E::NystromLandmarkSource> sources{{"points", E::NystromLandmarkSource::Points},
                                                                      {"box", E::NystromLandmarkSource::BoundingBox}};
#ifdef METHOD083
        sources.emplace("union", E::NystromLandmarkSource::UnionRandom);
#endif
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
    if (argc % 2 != 1) Fail("every option needs a value");
    std::map<std::string, std::string> args;
    for (int i = 1; i + 1 < argc; i += 2) args[argv[i]] = argv[i + 1];
    const auto get = [&](const std::string& key, const std::string& fallback) {
        return args.contains(key) ? args.at(key) : fallback;
    };
    const std::string kind = get("--fixture", ""), variant = get("--variant", ""), out = get("--out", "");
    if ((kind != "full" && kind != "partial") || variant.empty() || out.empty())
    {
        std::cerr << "usage: see the header of CoherentPointDriftLandmarkVariantsRunner.cpp\n";
        return 2;
    }
    const auto seed = std::uint32_t(std::stoul(get("--seed", "1")));
    const auto count = std::size_t(std::stoul(get("--points", "100000")));
    const Fixture f{ReadPoints(get("--source", "")), ReadPoints(get("--target", "")), ReadPoints(get("--truth", "")), {}};
    if (f.Source.size() != count || f.Truth.size() != count) Fail("point count mismatch");
    // All three input sets already use the externally frozen solver frame.
    CPD::Params params;
    params.Method = CPD::Variant::Bayesian;
    params.NormalizeInputs = false;
    params.Tolerance = std::stod(get("--tolerance", "0.00001"));
    params.OutlierWeight = 0.1;
    params.Beta = std::stod(get("--beta", "1"));
    params.Lambda = 2.0;
    params.MaxIterations = std::uint32_t(std::stoul(get("--iterations", "150")));
    params.LowRank = std::uint32_t(std::stoul(get("--rank", "100")));
    params.EStep = CPD::EStepPolicy::Auto;
    params.Threads = std::uint32_t(std::stoul(get("--threads", "16")));
    params.NystromLandmarks = std::uint32_t(std::stoul(get("--landmarks", "256")));
    params.NystromErrorLimit = std::stod(get("--error-limit", "0.001"));
    // Landmark draws follow the fixture seed (box streams and Halton shifts, resampled seeds);
    // farthest point ignores the seed, so G's landmarks and point landmarks stay as they were.
    params.LandmarkSampling.Seed = seed;
#ifdef METHOD083
    params.NystromLocalSigma = std::stod(get("--switch", "0.2"));
#endif
    if (!ParseVariant(variant, params))
    {
        std::cerr << "unknown variant " << variant << "\n";
        return 2;
    }

    std::uint32_t nystromIterations = 0u, iterations = 0u;
    double maxSampled = 0.0;
    const auto start = std::chrono::steady_clock::now();
    if (args.contains("--trace"))
        params.StageObserver = [&](CPD::Stage stage) {
            std::cerr << std::setprecision(17) << "CPD_STAGE " << CPD::ToString(stage) << " "
                      << std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() << "\n";
        };
    const CPD::Result result = CPD::Register(f.Target, f.Source, params, [&](const CPD::IterationTrace& t) {
        if (args.contains("--trace"))
        {
            std::cerr << std::setprecision(17) << "CPD_ITER " << t.Iteration << " " << t.Sigma2 << " "
                      << t.NegativeLogLikelihood << " " << t.Objective << " " << t.MatchedWeight << " "
                      << int(t.EStep) << " " << t.EStepSampledError << " " << t.KernelEvaluations << " "
                      << std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count()
#ifdef METHOD083
                      << " " << t.NystromLocal
#else
                      << " 0"
#endif
                      << "\n";
        }
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
         << ",\"sigma2\":" << result.Sigma2 << ",\"scale\":" << result.Scale << ",\"backend\":\"" << result.Backend << "\""
         << ",\"kernel_rank\":" << result.KernelRank << ",\"kernel_error\":" << result.KernelApproximationError;
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
        positions.close();
        if (!positions) Fail("failed to write positions");
    }
    std::ofstream output(out);
    output << json.str() << "\n";
    output.close();
    if (!output) Fail("failed to write result JSON");
    std::cout << json.str() << "\n";
    return code;
}
