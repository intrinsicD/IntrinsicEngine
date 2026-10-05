module;
#pragma STDC FP_CONTRACT OFF

#include <algorithm>
#include <cmath>
#include <limits>
#include <span>
#include <glm/glm.hpp>


module Geometry.PCA;

import Geometry.Validation;

namespace Geometry::PCADetail
{
    // GLSL twin: pcaAtan in pca_eigen_double.glsl.
    // Range reduction bounds atan's alternating series by tan(pi/16).
    double Atan(double x)
    {
        const bool reciprocal = x > 1.0;
        if (reciprocal)
            x = 1.0 / x;
        for (int i = 0; i < 2; ++i)
            x = x / (1.0 + std::sqrt(1.0 + x * x));
        const double square = x * x;
        double term = x;
        double sum = x;
        for (int i = 1; i < 24; ++i)
        {
            term *= -square;
            sum += term / static_cast<double>(2 * i + 1);
        }
        sum *= 4.0;
        return reciprocal ? 1.57079632679489661923 - sum : sum;
    }

    // GLSL twin: pcaAcos in pca_eigen_double.glsl.
    double Acos(double x)
    {
        if (x <= -1.0)
            return 3.14159265358979323846;
        if (x >= 1.0)
            return 0.0;
        return 2.0 * Atan(std::sqrt((1.0 - x) / (1.0 + x)));
    }

    // GLSL twin: pcaCos in pca_eigen_double.glsl.
    double Cos(double x)
    {
        double term = 1.0;
        double sum = term;
        const double square = x * x;
        for (int i = 1; i < 24; ++i)
        {
            term *= -square / static_cast<double>((2 * i - 1) * (2 * i));
            sum += term;
        }
        return sum;
    }

    // GLSL twin: pcaStable in pca_eigen_double.glsl. Largest-off-diagonal
    // Jacobi rotations handle repeated roots where row cross products lose rank.
    void StableEigen(glm::dmat3 a, glm::dvec3& values, glm::dmat3& vectors)
    {
        vectors = glm::dmat3(1.0);
        double scale = 0.0;
        for (int c = 0; c < 3; ++c)
            for (int r = 0; r < 3; ++r)
                scale = std::max(scale, std::abs(a[c][r]));
        for (int step = 0; step < 32; ++step)
        {
            int p = 0;
            int q = 1;
            if (std::abs(a[0][2]) > std::abs(a[p][q]))
            {
                p = 0;
                q = 2;
            }
            if (std::abs(a[1][2]) > std::abs(a[p][q]))
            {
                p = 1;
                q = 2;
            }
            if (std::abs(a[p][q]) <= 1e-30 * scale)
                break;
            const double tau = (a[q][q] - a[p][p]) / (2.0 * a[p][q]);
            const double t = (tau >= 0.0 ? 1.0 : -1.0) /
                             (std::abs(tau) + std::sqrt(1.0 + tau * tau));
            const double c = 1.0 / std::sqrt(1.0 + t * t);
            const double s = t * c;
            const double shift = t * a[p][q];
            a[p][p] -= shift;
            a[q][q] += shift;
            a[p][q] = 0.0;
            a[q][p] = 0.0;
            for (int k = 0; k < 3; ++k)
            {
                if (k != p && k != q)
                {
                    const double x = a[k][p];
                    const double y = a[k][q];
                    a[k][p] = c * x - s * y;
                    a[p][k] = a[k][p];
                    a[k][q] = s * x + c * y;
                    a[q][k] = a[k][q];
                }
                const double x = vectors[p][k];
                const double y = vectors[q][k];
                vectors[p][k] = c * x - s * y;
                vectors[q][k] = s * x + c * y;
            }
        }
        values = glm::dvec3(std::max(a[0][0], 0.0), std::max(a[1][1], 0.0), std::max(a[2][2], 0.0));
        for (int j = 0; j < 3; ++j)
        {
            const int p = j == 1 ? 1 : 0;
            const int q = p + 1;
            if (values[p] < values[q])
            {
                const double t = values[p];
                values[p] = values[q];
                values[q] = t;
                const glm::dvec3 v = vectors[p];
                vectors[p] = vectors[q];
                vectors[q] = v;
            }
        }
        if (glm::dot(glm::cross(vectors[0], vectors[1]), vectors[2]) < 0.0)
            vectors[2] = -vectors[2];
    }

    // GLSL twin: pcaVector in pca_eigen_double.glsl.
    glm::dvec3 Eigenvector(glm::dmat3 a, double lambda)
    {
        a[0][0] -= lambda;
        a[1][1] -= lambda;
        a[2][2] -= lambda;
        const glm::dvec3 c01 = glm::cross(a[0], a[1]);
        const glm::dvec3 c02 = glm::cross(a[0], a[2]);
        const glm::dvec3 c12 = glm::cross(a[1], a[2]);
        const double l01 = glm::dot(c01, c01);
        const double l02 = glm::dot(c02, c02);
        const double l12 = glm::dot(c12, c12);
        const glm::dvec3 best = l01 >= l02 && l01 >= l12 ? c01 : l02 >= l12 ? c02 : c12;
        const double length = glm::dot(best, best);
        return length > 1e-30 ? best / std::sqrt(length) : glm::dvec3(0.0);
    }

    // GLSL twin: pcaEigen in pca_eigen_double.glsl. Keep the expression order,
    // thresholds and repeated-root fallback synchronized with that implementation.
    void Eigen(double a00, double a01, double a02, double a11, double a12, double a22,
               glm::dvec3& values, glm::dmat3& vectors)
    {
        const glm::dmat3 a(glm::dvec3(a00, a01, a02), glm::dvec3(a01, a11, a12), glm::dvec3(a02, a12, a22));
        const double mean = (a00 + a11 + a22) / 3.0;
        const double b00 = a00 - mean;
        const double b11 = a11 - mean;
        const double b22 = a22 - mean;
        const double p2 = (b00 * b00 + b11 * b11 + b22 * b22 +
                           2.0 * (a01 * a01 + a02 * a02 + a12 * a12)) / 6.0;
        double scale = 1.0;
        for (int c = 0; c < 3; ++c)
            for (int r = 0; r < 3; ++r)
                scale = std::max(scale, std::abs(a[c][r]));
        if (!(p2 > 2.2204460492503131e-16 * scale * scale))
        {
            values = glm::dvec3(std::max(mean, 0.0));
            vectors = glm::dmat3(1.0);
            return;
        }
        const double p = std::sqrt(p2);
        const double inverse = 1.0 / p;
        const double c00 = b00 * inverse;
        const double c01 = a01 * inverse;
        const double c02 = a02 * inverse;
        const double c11 = b11 * inverse;
        const double c12 = a12 * inverse;
        const double c22 = b22 * inverse;
        const double determinant = c00 * c11 * c22 + 2.0 * c01 * c02 * c12 -
                                   c00 * c12 * c12 - c11 * c02 * c02 - c22 * c01 * c01;
        const double phi = Acos(std::max(-1.0, std::min(1.0, determinant * 0.5))) / 3.0;
        double l0 = mean + 2.0 * p * Cos(phi);
        double l2 = mean + 2.0 * p * Cos(phi + 2.09439510239319549231);
        double l1 = 3.0 * mean - l0 - l2;
        if (l0 > l1)
            std::swap(l0, l1);
        if (l1 > l2)
            std::swap(l1, l2);
        if (l0 > l1)
            std::swap(l0, l1);
        const double tolerance = 8.0 * std::sqrt(2.2204460492503131e-16) *
                                 std::max(std::abs(l0), std::abs(l2));
        if (l1 - l0 <= tolerance || l2 - l1 <= tolerance)
        {
            StableEigen(a, values, vectors);
            return;
        }
        vectors = glm::dmat3(Eigenvector(a, l2), Eigenvector(a, l1), Eigenvector(a, l0));
        for (int i = 0; i < 3; ++i)
        {
            if (glm::dot(vectors[i], vectors[i]) == 0.0)
            {
                StableEigen(a, values, vectors);
                return;
            }
        }
        const double projection = glm::dot(vectors[0], vectors[1]);
        vectors[1] -= projection * vectors[0];
        double length = std::sqrt(glm::dot(vectors[1], vectors[1]));
        if (!(length > 1e-15))
        {
            StableEigen(a, values, vectors);
            return;
        }
        vectors[1] /= length;
        vectors[2] = glm::cross(vectors[0], vectors[1]);
        length = std::sqrt(glm::dot(vectors[2], vectors[2]));
        if (!(length > 1e-15))
        {
            StableEigen(a, values, vectors);
            return;
        }
        vectors[2] /= length;
        values = glm::dvec3(std::max(l2, 0.0), std::max(l1, 0.0), std::max(l0, 0.0));
    }

}
namespace Geometry::PCA
{
    Eigen3 SymmetricEigen3(double a00, double a01, double a02, double a11, double a12, double a22)
    {
        Eigen3 result;
        glm::dmat3 vectors;
        PCADetail::Eigen(a00, a01, a02, a11, a12, a22, result.Eigenvalues, vectors);
        for (int i = 0; i < 3; ++i)
            result.Eigenvectors[i] = vectors[i];
        return result;
    }
}

namespace Geometry
{

    [[nodiscard]] PCAResult ToPCA(std::span<const glm::vec3> points)
    {
        PCAResult result{};

        glm::dvec3 mean{0.0};
        std::size_t count = 0;
        for (const glm::vec3& point : points)
        {
            if (!Geometry::Validation::IsFinite(point))
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
            if (!Geometry::Validation::IsFinite(point))
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
        const auto eigen = PCA::SymmetricEigen3(
            c00 * invCount, c01 * invCount, c02 * invCount,
            c11 * invCount, c12 * invCount, c22 * invCount);

        if (!std::isfinite(eigen.Eigenvalues.x) || !std::isfinite(eigen.Eigenvalues.y) ||
            !std::isfinite(eigen.Eigenvalues.z)) return {};

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

