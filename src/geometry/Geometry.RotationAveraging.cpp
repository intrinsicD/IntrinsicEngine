module;

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <span>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

module Geometry.RotationAveraging;

import Geometry.Linalg;
import Geometry.Rotation;

namespace Geometry::Rotation
{
    namespace
    {
        constexpr double kPi = 3.14159265358979323846;
        constexpr double kQuatEpsilon = 1e-14;
        constexpr double kDistanceEpsilon = 1e-8;

        struct Sample
        {
            glm::mat3 Rotation{1.0f};
            glm::dvec4 Quaternion{1.0, 0.0, 0.0, 0.0}; // w, x, y, z
            double Weight{1.0};
        };

        [[nodiscard]] bool FiniteMat(const glm::mat3& m)
        {
            for (int c = 0; c < 3; ++c)
                for (int r = 0; r < 3; ++r)
                    if (!std::isfinite(m[c][r])) return false;
            return true;
        }

        [[nodiscard]] bool FiniteVec(const glm::dvec3& v)
        {
            return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
        }

        [[nodiscard]] bool FiniteQuat(const glm::dvec4& q)
        {
            return std::isfinite(q.x) && std::isfinite(q.y) &&
                   std::isfinite(q.z) && std::isfinite(q.w);
        }

        void CanonicalizeQuaternion(glm::dvec4& q)
        {
            for (int i = 0; i < 4; ++i)
            {
                if (std::abs(q[i]) <= kQuatEpsilon)
                {
                    continue;
                }
                if (q[i] < 0.0)
                {
                    q = -q;
                }
                return;
            }
        }

        [[nodiscard]] bool NormalizeQuaternion(glm::dvec4& q)
        {
            if (!FiniteQuat(q))
            {
                return false;
            }
            const double norm = std::sqrt(glm::dot(q, q));
            if (!(norm > kQuatEpsilon) || !std::isfinite(norm))
            {
                return false;
            }
            q /= norm;
            CanonicalizeQuaternion(q);
            return true;
        }

        [[nodiscard]] bool QuaternionLess(const Sample& a, const Sample& b)
        {
            for (int i = 0; i < 4; ++i)
            {
                if (a.Quaternion[i] < b.Quaternion[i]) return true;
                if (a.Quaternion[i] > b.Quaternion[i]) return false;
            }
            return a.Weight < b.Weight;
        }

        [[nodiscard]] glm::mat3 QuaternionToRotation(glm::dvec4 q)
        {
            if (!NormalizeQuaternion(q))
            {
                return glm::mat3(1.0f);
            }
            const glm::quat quat(static_cast<float>(q.x),
                                 static_cast<float>(q.y),
                                 static_cast<float>(q.z),
                                 static_cast<float>(q.w));
            return glm::mat3_cast(glm::normalize(quat));
        }

        [[nodiscard]] bool RotationToQuaternion(const glm::mat3& rotation, glm::dvec4& out)
        {
            const glm::mat3 projected = ProjectOnSO3(rotation);
            const glm::quat q = glm::normalize(glm::quat_cast(projected));
            out = glm::dvec4(static_cast<double>(q.w),
                             static_cast<double>(q.x),
                             static_cast<double>(q.y),
                             static_cast<double>(q.z));
            return NormalizeQuaternion(out);
        }

        [[nodiscard]] RotationAverageResult MakeResult(RotationAverageStatus status,
                                                       glm::mat3 rotation = glm::mat3(1.0f),
                                                       bool valid = false,
                                                       bool converged = false,
                                                       int iterations = 0,
                                                       float residual = 0.0f)
        {
            RotationAverageResult result{};
            result.Rotation = rotation;
            result.Valid = valid;
            result.Converged = converged;
            result.Iterations = iterations;
            result.ResidualRadians = std::isfinite(residual) ? residual : 0.0f;
            result.Status = status;
            return result;
        }

        [[nodiscard]] bool OptionsAreFinite(const RotationAverageOptions& options)
        {
            return options.MaxIterations >= 0 &&
                   std::isfinite(options.Tolerance) && options.Tolerance > 0.0f &&
                   std::isfinite(options.OutlierRejectionRadians);
        }

        [[nodiscard]] RotationAverageStatus CollectSamples(std::span<const glm::mat3> rotations,
                                                           const RotationAverageOptions& options,
                                                           std::vector<Sample>& samples)
        {
            samples.clear();
            if (!OptionsAreFinite(options))
            {
                return RotationAverageStatus::InvalidOptions;
            }
            if (rotations.empty())
            {
                return RotationAverageStatus::EmptyInput;
            }
            if (!options.Weights.empty() && options.Weights.size() != rotations.size())
            {
                return RotationAverageStatus::WeightSizeMismatch;
            }

            samples.reserve(rotations.size());
            for (std::size_t i = 0; i < rotations.size(); ++i)
            {
                if (!FiniteMat(rotations[i]))
                {
                    return RotationAverageStatus::NonFiniteInput;
                }

                const double weight = options.Weights.empty()
                    ? 1.0
                    : static_cast<double>(options.Weights[i]);
                if (!std::isfinite(weight) || !(weight > 0.0))
                {
                    return RotationAverageStatus::InvalidWeight;
                }

                const glm::mat3 projected = ProjectOnSO3(rotations[i]);
                glm::dvec4 q{};
                if (!RotationToQuaternion(projected, q))
                {
                    return RotationAverageStatus::DegenerateInput;
                }
                samples.push_back(Sample{projected, q, weight});
            }

            std::sort(samples.begin(), samples.end(), QuaternionLess);
            return RotationAverageStatus::Success;
        }

        [[nodiscard]] RotationAverageResult Prepare(std::span<const glm::mat3> rotations,
                                                    const RotationAverageOptions& options,
                                                    std::vector<Sample>& samples)
        {
            const RotationAverageStatus status = CollectSamples(rotations, options, samples);
            if (status != RotationAverageStatus::Success)
            {
                return MakeResult(status);
            }
            if (samples.size() == 1)
            {
                return MakeResult(RotationAverageStatus::SingleSample,
                                  rotations.front(),
                                  true,
                                  true);
            }

            if (samples.size() == 2)
            {
                const double weightScale = std::max({1.0, samples[0].Weight, samples[1].Weight});
                const bool equalWeights = std::abs(samples[0].Weight - samples[1].Weight) <=
                    static_cast<double>(options.Tolerance) * weightScale;
                const bool cutLocusPair =
                    AngularDistance(samples[0].Rotation, samples[1].Rotation) >=
                    static_cast<float>(kPi - 1e-5);
                if (equalWeights && cutLocusPair)
                {
                    return MakeResult(RotationAverageStatus::DegenerateInput);
                }
            }

            return MakeResult(RotationAverageStatus::Success, glm::mat3(1.0f), true);
        }

        [[nodiscard]] bool IncludeSample(const Sample& sample,
                                         const glm::mat3* center,
                                         const RotationAverageOptions& options)
        {
            if (center == nullptr || !(options.OutlierRejectionRadians > 0.0f))
            {
                return true;
            }
            return AngularDistance(*center, sample.Rotation) <= options.OutlierRejectionRadians;
        }

        [[nodiscard]] float MeanAngularResidual(const std::vector<Sample>& samples,
                                                const glm::mat3& rotation,
                                                const RotationAverageOptions& options)
        {
            double total = 0.0;
            double residual = 0.0;
            for (const Sample& sample : samples)
            {
                if (!IncludeSample(sample, &rotation, options))
                {
                    continue;
                }
                residual += sample.Weight * static_cast<double>(AngularDistance(rotation, sample.Rotation));
                total += sample.Weight;
            }
            return (total > 0.0) ? static_cast<float>(residual / total) : 0.0f;
        }

        [[nodiscard]] RotationAverageResult ChordalMeanFromSamples(const std::vector<Sample>& samples,
                                                                   const RotationAverageOptions& options,
                                                                   const glm::mat3* center)
        {
            Geometry::Linalg::DenseMatrix moment(4, 4);
            glm::dmat3 matrixSum(0.0);
            double total = 0.0;

            for (const Sample& sample : samples)
            {
                if (!IncludeSample(sample, center, options))
                {
                    continue;
                }
                for (std::size_t row = 0; row < 4; ++row)
                {
                    for (std::size_t col = 0; col < 4; ++col)
                    {
                        moment(row, col) += sample.Weight * sample.Quaternion[static_cast<int>(row)] *
                                            sample.Quaternion[static_cast<int>(col)];
                    }
                }
                matrixSum += sample.Weight * glm::dmat3(sample.Rotation);
                total += sample.Weight;
            }

            if (!(total > 0.0))
            {
                return MakeResult(RotationAverageStatus::DegenerateInput);
            }

            for (std::size_t row = 0; row < 4; ++row)
            {
                for (std::size_t col = 0; col < 4; ++col)
                {
                    moment(row, col) /= total;
                }
            }

            const Geometry::Linalg::SymmetricEigenResult eigen =
                Geometry::Linalg::ComputeSymmetricEigen(moment);
            if (eigen.Diagnostics.Status == Geometry::Linalg::NumericStatus::Success &&
                eigen.Eigenvectors.Rows == 4 &&
                eigen.Eigenvectors.Cols == 4 &&
                eigen.Eigenvalues.size() == 4)
            {
                std::size_t top = 0;
                for (std::size_t i = 1; i < eigen.Eigenvalues.size(); ++i)
                {
                    if (eigen.Eigenvalues[i] > eigen.Eigenvalues[top])
                    {
                        top = i;
                    }
                }

                double second = -std::numeric_limits<double>::infinity();
                for (std::size_t i = 0; i < eigen.Eigenvalues.size(); ++i)
                {
                    if (i != top)
                    {
                        second = std::max(second, eigen.Eigenvalues[i]);
                    }
                }
                const double gap = eigen.Eigenvalues[top] - second;
                if (gap <= std::max(1e-10, static_cast<double>(options.Tolerance)))
                {
                    return MakeResult(RotationAverageStatus::DegenerateInput);
                }

                glm::dvec4 q(eigen.Eigenvectors(0, top),
                             eigen.Eigenvectors(1, top),
                             eigen.Eigenvectors(2, top),
                             eigen.Eigenvectors(3, top));
                if (NormalizeQuaternion(q))
                {
                    const glm::mat3 rotation = QuaternionToRotation(q);
                    return MakeResult(RotationAverageStatus::Success,
                                      rotation,
                                      true,
                                      true,
                                      0,
                                      MeanAngularResidual(samples, rotation, options));
                }
            }

            const glm::mat3 rotation = ProjectOnSO3(glm::mat3(matrixSum / total));
            return MakeResult(RotationAverageStatus::Success,
                              rotation,
                              true,
                              true,
                              0,
                              MeanAngularResidual(samples, rotation, options));
        }

        [[nodiscard]] RotationAverageResult QuaternionMeanFromSamples(const std::vector<Sample>& samples,
                                                                      const RotationAverageOptions& options,
                                                                      const glm::mat3* center)
        {
            bool haveReference = false;
            glm::dvec4 reference(1.0, 0.0, 0.0, 0.0);
            glm::dvec4 accumulator(0.0);
            double total = 0.0;

            for (const Sample& sample : samples)
            {
                if (!IncludeSample(sample, center, options))
                {
                    continue;
                }
                glm::dvec4 q = sample.Quaternion;
                if (!haveReference)
                {
                    reference = q;
                    haveReference = true;
                }
                if (glm::dot(q, reference) < 0.0)
                {
                    q = -q;
                }
                accumulator += sample.Weight * q;
                total += sample.Weight;
            }

            if (!(total > 0.0) || !NormalizeQuaternion(accumulator))
            {
                return MakeResult(RotationAverageStatus::DegenerateInput);
            }

            const glm::mat3 rotation = QuaternionToRotation(accumulator);
            return MakeResult(RotationAverageStatus::Success,
                              rotation,
                              true,
                              true,
                              0,
                              MeanAngularResidual(samples, rotation, options));
        }

        [[nodiscard]] float QuaternionAngularStep(glm::dvec4 a, glm::dvec4 b)
        {
            if (!NormalizeQuaternion(a) || !NormalizeQuaternion(b))
            {
                return 0.0f;
            }
            const double dot = std::clamp(std::abs(glm::dot(a, b)), 0.0, 1.0);
            return static_cast<float>(2.0 * std::acos(dot));
        }
    }

    RotationAverageResult ChordalMean(std::span<const glm::mat3> rotations,
                                      RotationAverageOptions options)
    {
        std::vector<Sample> samples;
        RotationAverageResult prepared = Prepare(rotations, options, samples);
        if (prepared.Status != RotationAverageStatus::Success)
        {
            return prepared;
        }

        RotationAverageResult result = ChordalMeanFromSamples(samples, options, nullptr);
        if (result.Valid && options.OutlierRejectionRadians > 0.0f)
        {
            result = ChordalMeanFromSamples(samples, options, &result.Rotation);
        }
        return result;
    }

    RotationAverageResult QuaternionMean(std::span<const glm::mat3> rotations,
                                         RotationAverageOptions options)
    {
        std::vector<Sample> samples;
        RotationAverageResult prepared = Prepare(rotations, options, samples);
        if (prepared.Status != RotationAverageStatus::Success)
        {
            return prepared;
        }

        RotationAverageResult result = QuaternionMeanFromSamples(samples, options, nullptr);
        if (result.Valid && options.OutlierRejectionRadians > 0.0f)
        {
            result = QuaternionMeanFromSamples(samples, options, &result.Rotation);
        }
        return result;
    }

    RotationAverageResult KarcherMean(std::span<const glm::mat3> rotations,
                                      RotationAverageOptions options)
    {
        std::vector<Sample> samples;
        RotationAverageResult prepared = Prepare(rotations, options, samples);
        if (prepared.Status != RotationAverageStatus::Success)
        {
            return prepared;
        }

        RotationAverageResult seed = ChordalMeanFromSamples(samples, options, nullptr);
        if (!seed.Valid)
        {
            return seed;
        }
        glm::mat3 mean = seed.Rotation;
        float residual = seed.ResidualRadians;

        for (int iter = 0; iter < options.MaxIterations; ++iter)
        {
            const glm::mat3 meanT = glm::transpose(mean);
            glm::dvec3 accumulated(0.0);
            double total = 0.0;
            for (const Sample& sample : samples)
            {
                if (!IncludeSample(sample, &mean, options))
                {
                    continue;
                }
                const glm::dvec3 tangent(Log(meanT * sample.Rotation));
                if (!FiniteVec(tangent))
                {
                    return MakeResult(RotationAverageStatus::NonFiniteInput);
                }
                accumulated += sample.Weight * tangent;
                total += sample.Weight;
            }
            if (!(total > 0.0))
            {
                return MakeResult(RotationAverageStatus::DegenerateInput);
            }

            const glm::dvec3 delta = accumulated / total;
            if (!FiniteVec(delta))
            {
                return MakeResult(RotationAverageStatus::NonFiniteInput);
            }
            residual = static_cast<float>(glm::length(delta));
            mean = mean * Exp(glm::vec3(delta));
            if (residual <= options.Tolerance)
            {
                return MakeResult(RotationAverageStatus::Success,
                                  mean,
                                  true,
                                  true,
                                  iter + 1,
                                  residual);
            }
        }

        return MakeResult(RotationAverageStatus::NoConvergence,
                          mean,
                          true,
                          false,
                          options.MaxIterations,
                          residual);
    }

    RotationAverageResult GeodesicMedian(std::span<const glm::mat3> rotations,
                                         RotationAverageOptions options)
    {
        std::vector<Sample> samples;
        RotationAverageResult prepared = Prepare(rotations, options, samples);
        if (prepared.Status != RotationAverageStatus::Success)
        {
            return prepared;
        }

        RotationAverageResult seed = KarcherMean(rotations, options);
        if (!seed.Valid)
        {
            return seed;
        }
        glm::mat3 median = seed.Rotation;
        float residual = seed.ResidualRadians;

        for (int iter = 0; iter < options.MaxIterations; ++iter)
        {
            const glm::mat3 medianT = glm::transpose(median);
            glm::dvec3 numerator(0.0);
            double denominator = 0.0;
            for (const Sample& sample : samples)
            {
                if (!IncludeSample(sample, &median, options))
                {
                    continue;
                }
                const glm::dvec3 tangent(Log(medianT * sample.Rotation));
                if (!FiniteVec(tangent))
                {
                    return MakeResult(RotationAverageStatus::NonFiniteInput);
                }
                const double distance = glm::length(tangent);
                const double inverseDistanceWeight = sample.Weight / std::max(distance, kDistanceEpsilon);
                numerator += inverseDistanceWeight * tangent;
                denominator += inverseDistanceWeight;
            }
            if (!(denominator > 0.0))
            {
                return MakeResult(RotationAverageStatus::DegenerateInput);
            }

            const glm::dvec3 delta = numerator / denominator;
            if (!FiniteVec(delta))
            {
                return MakeResult(RotationAverageStatus::NonFiniteInput);
            }
            residual = static_cast<float>(glm::length(delta));
            median = median * Exp(glm::vec3(delta));
            if (residual <= options.Tolerance)
            {
                return MakeResult(RotationAverageStatus::Success,
                                  median,
                                  true,
                                  true,
                                  iter + 1,
                                  residual);
            }
        }

        return MakeResult(RotationAverageStatus::NoConvergence,
                          median,
                          true,
                          false,
                          options.MaxIterations,
                          residual);
    }

    RotationAverageResult QuaternionMedian(std::span<const glm::mat3> rotations,
                                           RotationAverageOptions options)
    {
        std::vector<Sample> samples;
        RotationAverageResult prepared = Prepare(rotations, options, samples);
        if (prepared.Status != RotationAverageStatus::Success)
        {
            return prepared;
        }

        RotationAverageResult seed = QuaternionMeanFromSamples(samples, options, nullptr);
        if (!seed.Valid)
        {
            return seed;
        }

        glm::dvec4 current{};
        if (!RotationToQuaternion(seed.Rotation, current))
        {
            return MakeResult(RotationAverageStatus::DegenerateInput);
        }
        glm::mat3 currentRotation = seed.Rotation;
        float residual = seed.ResidualRadians;

        for (int iter = 0; iter < options.MaxIterations; ++iter)
        {
            glm::dvec4 accumulator(0.0);
            double denominator = 0.0;
            for (const Sample& sample : samples)
            {
                if (!IncludeSample(sample, &currentRotation, options))
                {
                    continue;
                }
                glm::dvec4 q = sample.Quaternion;
                if (glm::dot(q, current) < 0.0)
                {
                    q = -q;
                }
                const glm::dvec4 difference = q - current;
                const double distance = std::sqrt(glm::dot(difference, difference));
                const double inverseDistanceWeight = sample.Weight / std::max(distance, kDistanceEpsilon);
                accumulator += inverseDistanceWeight * q;
                denominator += inverseDistanceWeight;
            }

            if (!(denominator > 0.0))
            {
                return MakeResult(RotationAverageStatus::DegenerateInput);
            }
            accumulator /= denominator;
            if (!NormalizeQuaternion(accumulator))
            {
                return MakeResult(RotationAverageStatus::DegenerateInput);
            }
            if (glm::dot(accumulator, current) < 0.0)
            {
                accumulator = -accumulator;
            }

            residual = QuaternionAngularStep(current, accumulator);
            current = accumulator;
            currentRotation = QuaternionToRotation(current);
            if (residual <= options.Tolerance)
            {
                return MakeResult(RotationAverageStatus::Success,
                                  currentRotation,
                                  true,
                                  true,
                                  iter + 1,
                                  residual);
            }
        }

        return MakeResult(RotationAverageStatus::NoConvergence,
                          currentRotation,
                          true,
                          false,
                          options.MaxIterations,
                          residual);
    }
}
