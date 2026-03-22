module;

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <span>

#include <glm/glm.hpp>

export module Geometry.Pca;

namespace Geometry::PcaDetail
{
    [[nodiscard]] inline bool IsFinite(const glm::vec3& value)
    {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
    }

    struct Eigen3
    {
        glm::dvec3 Eigenvalues{0.0};
        glm::dvec3 Eigenvectors[3]{
            glm::dvec3{1.0, 0.0, 0.0},
            glm::dvec3{0.0, 1.0, 0.0},
            glm::dvec3{0.0, 0.0, 1.0},
        };
    };

    [[nodiscard]] inline Eigen3 SymmetricEigen3(const glm::dmat3& matrix)
    {
        Eigen3 result{};

        auto normalizeOr = [](glm::dvec3 axis, glm::dvec3 fallback) -> glm::dvec3
        {
            const double len = glm::length(axis);
            if (len > 1e-15)
            {
                return axis / len;
            }
            return fallback;
        };

        auto orthogonalize = [](glm::dvec3 v, std::span<const glm::dvec3> basis) -> glm::dvec3
        {
            for (const glm::dvec3& axis : basis)
            {
                v -= glm::dot(v, axis) * axis;
            }
            return v;
        };

        auto orthogonalSeed = [&](std::span<const glm::dvec3> basis) -> glm::dvec3
        {
            const glm::dvec3 seeds[3] = {
                {1.0, 0.0, 0.0},
                {0.0, 1.0, 0.0},
                {0.0, 0.0, 1.0},
            };
            for (const glm::dvec3& seed : seeds)
            {
                const glm::dvec3 candidate = orthogonalize(seed, basis);
                if (glm::dot(candidate, candidate) > 1e-15)
                {
                    return candidate;
                }
            }
            return {1.0, 0.0, 0.0};
        };

        auto powerIteration = [&](glm::dvec3 seed, std::span<const glm::dvec3> basis) -> glm::dvec3
        {
            glm::dvec3 v = orthogonalize(seed, basis);
            if (glm::dot(v, v) <= 1e-15)
            {
                v = orthogonalSeed(basis);
            }
            v = normalizeOr(v, glm::dvec3{1.0, 0.0, 0.0});

            for (int iter = 0; iter < 48; ++iter)
            {
                glm::dvec3 w = matrix * v;
                w = orthogonalize(w, basis);
                const double len2 = glm::dot(w, w);
                if (len2 <= 1e-30)
                {
                    break;
                }

                const glm::dvec3 next = w / std::sqrt(len2);
                if (glm::dot(next - v, next - v) <= 1e-28)
                {
                    v = next;
                    break;
                }
                v = next;
            }

            v = orthogonalize(v, basis);
            return normalizeOr(v, orthogonalSeed(basis));
        };

        const double frobeniusSq = glm::dot(glm::dvec3{matrix[0][0], matrix[1][1], matrix[2][2]}, glm::dvec3{matrix[0][0], matrix[1][1], matrix[2][2]})
                                 + 2.0 * glm::dot(glm::dvec3{matrix[1][0], matrix[2][0], matrix[2][1]}, glm::dvec3{matrix[1][0], matrix[2][0], matrix[2][1]});
        if (!(frobeniusSq > std::numeric_limits<double>::epsilon()))
        {
            result.Eigenvalues = {0.0, 0.0, 0.0};
            result.Eigenvectors[0] = {1.0, 0.0, 0.0};
            result.Eigenvectors[1] = {0.0, 1.0, 0.0};
            result.Eigenvectors[2] = {0.0, 0.0, 1.0};
            return result;
        }

        glm::dvec3 seed0 = glm::dvec3{matrix[0][0], matrix[1][1], matrix[2][2]};
        if (glm::dot(seed0, seed0) <= 1e-15)
        {
            seed0 = {1.0, 1.0, 1.0};
        }

        glm::dvec3 axis0 = powerIteration(seed0, {});
        const double lambda0 = glm::dot(axis0, matrix * axis0);

        glm::dmat3 deflated = matrix;
        for (int c = 0; c < 3; ++c)
        {
            for (int r = 0; r < 3; ++r)
            {
                deflated[c][r] -= lambda0 * axis0[r] * axis0[c];
            }
        }

        glm::dvec3 seed1 = orthogonalSeed(std::span<const glm::dvec3>{&axis0, 1});
        glm::dvec3 axis1 = powerIteration(seed1, std::span<const glm::dvec3>{&axis0, 1});
        axis1 = orthogonalize(axis1, std::span<const glm::dvec3>{&axis0, 1});
        axis1 = normalizeOr(axis1, orthogonalSeed(std::span<const glm::dvec3>{&axis0, 1}));
        const double lambda1 = glm::dot(axis1, matrix * axis1);

        glm::dvec3 axis2 = glm::cross(axis0, axis1);
        if (glm::dot(axis2, axis2) <= 1e-15)
        {
            axis2 = orthogonalize(orthogonalSeed(std::span<const glm::dvec3>{&axis0, 1}), std::span<const glm::dvec3>{&axis0, 1});
            axis2 = orthogonalize(axis2, std::span<const glm::dvec3>{&axis0, 1});
            axis2 = orthogonalize(axis2, std::span<const glm::dvec3>{&axis1, 1});
        }
        axis2 = normalizeOr(axis2, glm::dvec3{0.0, 0.0, 1.0});
        axis1 = normalizeOr(glm::cross(axis2, axis0), axis1);
        const double lambda2 = glm::dot(axis2, matrix * axis2);

        std::array<std::pair<double, glm::dvec3>, 3> eig{
            std::pair{lambda0, axis0},
            std::pair{lambda1, axis1},
            std::pair{lambda2, axis2},
        };
        std::sort(eig.begin(), eig.end(), [](const auto& a, const auto& b)
        {
            return a.first > b.first;
        });

        result.Eigenvalues = glm::dvec3{
            std::max(0.0, eig[0].first),
            std::max(0.0, eig[1].first),
            std::max(0.0, eig[2].first),
        };
        result.Eigenvectors[0] = normalizeOr(eig[0].second, glm::dvec3{1.0, 0.0, 0.0});
        result.Eigenvectors[1] = normalizeOr(orthogonalize(eig[1].second, std::span<const glm::dvec3>{&result.Eigenvectors[0], 1}), orthogonalSeed(std::span<const glm::dvec3>{&result.Eigenvectors[0], 1}));
        result.Eigenvectors[2] = normalizeOr(glm::cross(result.Eigenvectors[0], result.Eigenvectors[1]), glm::dvec3{0.0, 0.0, 1.0});
        return result;
    }
}

export namespace Geometry
{
    struct PcaResult
    {
        bool Valid{false}; // True if at least one finite sample contributed.
        bool Flat{false}; // True if the point set is numerically rank deficient.
        glm::vec3 Mean{0.0f};
        glm::mat3 Eigenvectors{1.0f}; // Columns are eigenvectors
        glm::vec3 Eigenvalues{1.0f}; // Corresponding eigenvalues
    };

    [[nodiscard]] inline PcaResult ToPca(std::span<const glm::vec3> points)
    {
        PcaResult result{};

        glm::dvec3 mean{0.0};
        std::size_t count = 0;
        for (const glm::vec3& point : points)
        {
            if (!PcaDetail::IsFinite(point))
            {
                continue;
            }
            mean += glm::dvec3{static_cast<double>(point.x), static_cast<double>(point.y), static_cast<double>(point.z)};
            ++count;
        }

        if (count == 0)
        {
            return result;
        }

        mean /= static_cast<double>(count);
        result.Valid = true;
        result.Mean = glm::vec3{static_cast<float>(mean.x), static_cast<float>(mean.y), static_cast<float>(mean.z)};

        if (count == 1)
        {
            result.Eigenvectors = glm::mat3{1.0f};
            result.Eigenvalues = glm::vec3{0.0f};
            result.Flat = true;
            return result;
        }

        glm::dmat3 covariance{0.0};
        for (const glm::vec3& point : points)
        {
            if (!PcaDetail::IsFinite(point))
            {
                continue;
            }

            const glm::dvec3 delta = glm::dvec3{static_cast<double>(point.x), static_cast<double>(point.y), static_cast<double>(point.z)} - mean;
            covariance[0][0] += delta.x * delta.x;
            covariance[1][0] += delta.x * delta.y;
            covariance[2][0] += delta.x * delta.z;
            covariance[0][1] += delta.y * delta.x;
            covariance[1][1] += delta.y * delta.y;
            covariance[2][1] += delta.y * delta.z;
            covariance[0][2] += delta.z * delta.x;
            covariance[1][2] += delta.z * delta.y;
            covariance[2][2] += delta.z * delta.z;
        }

        const double invCount = 1.0 / static_cast<double>(count);
        covariance *= invCount;
        const auto eigen = PcaDetail::SymmetricEigen3(covariance);

        result.Eigenvectors = glm::mat3{
            glm::vec3{static_cast<float>(eigen.Eigenvectors[0].x), static_cast<float>(eigen.Eigenvectors[0].y), static_cast<float>(eigen.Eigenvectors[0].z)},
            glm::vec3{static_cast<float>(eigen.Eigenvectors[1].x), static_cast<float>(eigen.Eigenvectors[1].y), static_cast<float>(eigen.Eigenvectors[1].z)},
            glm::vec3{static_cast<float>(eigen.Eigenvectors[2].x), static_cast<float>(eigen.Eigenvectors[2].y), static_cast<float>(eigen.Eigenvectors[2].z)},
        };
        result.Eigenvalues = glm::vec3{
            static_cast<float>(eigen.Eigenvalues.x),
            static_cast<float>(eigen.Eigenvalues.y),
            static_cast<float>(eigen.Eigenvalues.z),
        };

        const float largest = std::max(result.Eigenvalues.x, 1.0e-12f);
        result.Flat = result.Eigenvalues.z <= largest * 1.0e-5f;
        return result;
    }
}
