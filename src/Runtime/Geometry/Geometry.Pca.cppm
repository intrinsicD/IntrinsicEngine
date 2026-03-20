module;

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <span>
#include <utility>

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

    [[nodiscard]] inline Eigen3 SymmetricEigen3(
        double a00, double a01, double a02,
        double a11, double a12, double a22)
    {
        Eigen3 result{};

        const double c0 = a00 * a11 * a22 + 2.0 * a01 * a02 * a12
                        - a00 * a12 * a12 - a11 * a02 * a02 - a22 * a01 * a01;
        const double c1 = a00 * a11 - a01 * a01 + a00 * a22 - a02 * a02 + a11 * a22 - a12 * a12;
        const double c2 = a00 + a11 + a22;

        const double c2Over3 = c2 / 3.0;
        const double aVal = c1 - c2 * c2Over3;
        const double bVal = c0 - c1 * c2Over3 + 2.0 * c2Over3 * c2Over3 * c2Over3;
        const double halfB = bVal / 2.0;
        const double q = halfB * halfB + (aVal / 3.0) * (aVal / 3.0) * (aVal / 3.0);

        double lambda0 = c2Over3;
        double lambda1 = c2Over3;
        double lambda2 = c2Over3;

        if (q <= 0.0)
        {
            const double sqrtMinusA3 = std::sqrt(std::max(0.0, -aVal / 3.0));
            const double r = sqrtMinusA3 * sqrtMinusA3 * sqrtMinusA3;
            double theta = 0.0;
            if (r > 1e-30)
            {
                theta = std::acos(std::clamp(-halfB / r, -1.0, 1.0)) / 3.0;
            }

            const double twoSqrt = 2.0 * sqrtMinusA3;
            lambda0 = c2Over3 + twoSqrt * std::cos(theta + 2.0 * std::numbers::pi / 3.0);
            lambda1 = c2Over3 + twoSqrt * std::cos(theta + 4.0 * std::numbers::pi / 3.0);
            lambda2 = c2Over3 + twoSqrt * std::cos(theta);
        }
        else
        {
            const double sqrtQ = std::sqrt(q);
            const double u = std::cbrt(-halfB + sqrtQ);
            const double v = std::cbrt(-halfB - sqrtQ);
            lambda0 = lambda1 = lambda2 = c2Over3 + u + v;
        }

        std::array<std::pair<double, glm::dvec3>, 3> eig{
            std::pair{lambda0, glm::dvec3{1.0, 0.0, 0.0}},
            std::pair{lambda1, glm::dvec3{0.0, 1.0, 0.0}},
            std::pair{lambda2, glm::dvec3{0.0, 0.0, 1.0}},
        };

        auto computeEigenvector = [&](double lambda) -> glm::dvec3
        {
            const glm::dvec3 row0{a00 - lambda, a01, a02};
            const glm::dvec3 row1{a01, a11 - lambda, a12};
            const glm::dvec3 row2{a02, a12, a22 - lambda};

            const glm::dvec3 c01 = glm::cross(row0, row1);
            const glm::dvec3 c02 = glm::cross(row0, row2);
            const glm::dvec3 c12 = glm::cross(row1, row2);

            const double d01 = glm::dot(c01, c01);
            const double d02 = glm::dot(c02, c02);
            const double d12 = glm::dot(c12, c12);

            glm::dvec3 best{1.0, 0.0, 0.0};
            double bestLengthSq = d01;
            if (d01 >= d02 && d01 >= d12)
            {
                best = c01;
                bestLengthSq = d01;
            }
            else if (d02 >= d12)
            {
                best = c02;
                bestLengthSq = d02;
            }
            else
            {
                best = c12;
                bestLengthSq = d12;
            }

            if (bestLengthSq > 1e-30)
            {
                return best / std::sqrt(bestLengthSq);
            }

            const glm::dvec3 basis[3] = {
                {1.0, 0.0, 0.0},
                {0.0, 1.0, 0.0},
                {0.0, 0.0, 1.0},
            };
            double bestResidual = std::numeric_limits<double>::max();
            glm::dvec3 fallback = basis[0];
            for (const glm::dvec3& axis : basis)
            {
                const glm::dvec3 residual{
                    (a00 - lambda) * axis.x + a01 * axis.y + a02 * axis.z,
                    a01 * axis.x + (a11 - lambda) * axis.y + a12 * axis.z,
                    a02 * axis.x + a12 * axis.y + (a22 - lambda) * axis.z,
                };
                const double residualNorm = glm::dot(residual, residual);
                if (residualNorm < bestResidual)
                {
                    bestResidual = residualNorm;
                    fallback = axis;
                }
            }
            return fallback;
        };

        eig[0].second = computeEigenvector(eig[0].first);
        eig[1].second = computeEigenvector(eig[1].first);
        eig[2].second = computeEigenvector(eig[2].first);

        std::sort(eig.begin(), eig.end(), [](const auto& a, const auto& b)
        {
            return a.first > b.first;
        });

        glm::dvec3 axis0 = eig[0].second;
        if (const double len = glm::length(axis0); len > 1e-15)
        {
            axis0 /= len;
        }
        else
        {
            axis0 = glm::dvec3{1.0, 0.0, 0.0};
        }

        glm::dvec3 axis1 = eig[1].second - glm::dot(eig[1].second, axis0) * axis0;
        if (const double len = glm::length(axis1); len > 1e-15)
        {
            axis1 /= len;
        }
        else
        {
            const glm::dvec3 fallback = std::abs(axis0.x) < 0.9
                ? glm::dvec3{1.0, 0.0, 0.0}
                : glm::dvec3{0.0, 1.0, 0.0};
            axis1 = glm::normalize(glm::cross(axis0, fallback));
        }

        glm::dvec3 axis2 = glm::cross(axis0, axis1);
        if (const double len = glm::length(axis2); len > 1e-15)
        {
            axis2 /= len;
        }
        else
        {
            axis2 = glm::dvec3{0.0, 0.0, 1.0};
        }

        if (glm::dot(glm::cross(axis0, axis1), axis2) < 0.0)
        {
            axis2 = -axis2;
        }

        result.Eigenvalues = glm::dvec3{
            std::max(0.0, eig[0].first),
            std::max(0.0, eig[1].first),
            std::max(0.0, eig[2].first),
        };
        result.Eigenvectors[0] = axis0;
        result.Eigenvectors[1] = axis1;
        result.Eigenvectors[2] = axis2;
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
            mean += glm::dvec3{point};
            ++count;
        }

        if (count == 0)
        {
            return result;
        }

        mean /= static_cast<double>(count);
        result.Valid = true;
        result.Mean = glm::vec3{mean};

        if (count == 1)
        {
            result.Eigenvectors = glm::mat3{1.0f};
            result.Eigenvalues = glm::vec3{0.0f};
            result.Flat = true;
            return result;
        }

        double c00 = 0.0;
        double c01 = 0.0;
        double c02 = 0.0;
        double c11 = 0.0;
        double c12 = 0.0;
        double c22 = 0.0;
        for (const glm::vec3& point : points)
        {
            if (!PcaDetail::IsFinite(point))
            {
                continue;
            }

            const glm::dvec3 delta = glm::dvec3{point} - mean;
            c00 += delta.x * delta.x;
            c01 += delta.x * delta.y;
            c02 += delta.x * delta.z;
            c11 += delta.y * delta.y;
            c12 += delta.y * delta.z;
            c22 += delta.z * delta.z;
        }

        const double invCount = 1.0 / static_cast<double>(count);
        const auto eigen = PcaDetail::SymmetricEigen3(
            c00 * invCount, c01 * invCount, c02 * invCount,
            c11 * invCount, c12 * invCount, c22 * invCount);

        result.Eigenvectors = glm::mat3{
            glm::vec3{eigen.Eigenvectors[0]},
            glm::vec3{eigen.Eigenvectors[1]},
            glm::vec3{eigen.Eigenvectors[2]},
        };
        result.Eigenvalues = glm::vec3{eigen.Eigenvalues};

        const float largest = std::max(result.Eigenvalues.x, 1.0e-12f);
        result.Flat = result.Eigenvalues.z <= largest * 1.0e-5f;
        return result;
    }
}
