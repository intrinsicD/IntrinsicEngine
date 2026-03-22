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

    [[nodiscard]] inline Eigen3 SymmetricEigen3(
        double a00, double a01, double a02,
        double a11, double a12, double a22)
    {
        Eigen3 result{};

        const double mean = (a00 + a11 + a22) / 3.0;
        const double b00 = a00 - mean;
        const double b11 = a11 - mean;
        const double b22 = a22 - mean;
        const double p2 = (b00 * b00 + b11 * b11 + b22 * b22
                          + 2.0 * (a01 * a01 + a02 * a02 + a12 * a12)) / 6.0;
        const double scale = std::max({std::abs(a00), std::abs(a11), std::abs(a22), std::abs(a01), std::abs(a02), std::abs(a12), 1.0});
        if (!(p2 > std::numeric_limits<double>::epsilon() * scale * scale))
        {
            result.Eigenvalues = {mean, mean, mean};
            result.Eigenvectors[0] = {1.0, 0.0, 0.0};
            result.Eigenvectors[1] = {0.0, 1.0, 0.0};
            result.Eigenvectors[2] = {0.0, 0.0, 1.0};
        }
        else
        {
            const double p = std::sqrt(p2);
            const double invP = 1.0 / p;
            const double c00 = b00 * invP;
            const double c01 = a01 * invP;
            const double c02 = a02 * invP;
            const double c11 = b11 * invP;
            const double c12 = a12 * invP;
            const double c22 = b22 * invP;
            const double detC = c00 * c11 * c22 + 2.0 * c01 * c02 * c12
                              - c00 * c12 * c12 - c11 * c02 * c02 - c22 * c01 * c01;
            const double phi = std::acos(std::clamp(detC * 0.5, -1.0, 1.0)) / 3.0;

            double lambda0 = mean + 2.0 * p * std::cos(phi);
            double lambda2 = mean + 2.0 * p * std::cos(phi + 2.0 * std::acos(-1.0) / 3.0);
            double lambda1 = 3.0 * mean - lambda0 - lambda2;

            if (lambda0 > lambda1) std::swap(lambda0, lambda1);
            if (lambda1 > lambda2) std::swap(lambda1, lambda2);
            if (lambda0 > lambda1) std::swap(lambda0, lambda1);

            result.Eigenvalues = {lambda2, lambda1, lambda0};

            auto computeEigenvector = [&](double lambda) -> glm::dvec3
            {
                const glm::dvec3 row0{a00 - lambda, a01, a02};
                const glm::dvec3 row1{a01, a11 - lambda, a12};
                const glm::dvec3 row2{a02, a12, a22 - lambda};

                const glm::dvec3 cross01 = glm::cross(row0, row1);
                const glm::dvec3 cross02 = glm::cross(row0, row2);
                const glm::dvec3 cross12 = glm::cross(row1, row2);

                const double lenSq01 = glm::dot(cross01, cross01);
                const double lenSq02 = glm::dot(cross02, cross02);
                const double lenSq12 = glm::dot(cross12, cross12);

                glm::dvec3 best{1.0, 0.0, 0.0};
                double bestLenSq = lenSq01;
                if (lenSq01 >= lenSq02 && lenSq01 >= lenSq12)
                {
                    best = cross01;
                    bestLenSq = lenSq01;
                }
                else if (lenSq02 >= lenSq12)
                {
                    best = cross02;
                    bestLenSq = lenSq02;
                }
                else
                {
                    best = cross12;
                    bestLenSq = lenSq12;
                }

                if (bestLenSq > 1e-30)
                    return best / std::sqrt(bestLenSq);

                return {1.0, 0.0, 0.0};
            };

            result.Eigenvectors[2] = computeEigenvector(lambda0);
            result.Eigenvectors[1] = computeEigenvector(lambda1);
            result.Eigenvectors[0] = computeEigenvector(lambda2);

            const double dot01 = glm::dot(result.Eigenvectors[0], result.Eigenvectors[1]);
            result.Eigenvectors[1] -= dot01 * result.Eigenvectors[0];
            const double len1 = glm::length(result.Eigenvectors[1]);
            if (len1 > 1e-15)
                result.Eigenvectors[1] /= len1;

            result.Eigenvectors[2] = glm::cross(result.Eigenvectors[0], result.Eigenvectors[1]);
            const double len2 = glm::length(result.Eigenvectors[2]);
            if (len2 > 1e-15)
                result.Eigenvectors[2] /= len2;
        }

        result.Eigenvalues.x = std::max(0.0, result.Eigenvalues.x);
        result.Eigenvalues.y = std::max(0.0, result.Eigenvalues.y);
        result.Eigenvalues.z = std::max(0.0, result.Eigenvalues.z);
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

            const glm::dvec3 delta = glm::dvec3{static_cast<double>(point.x), static_cast<double>(point.y), static_cast<double>(point.z)} - mean;
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
