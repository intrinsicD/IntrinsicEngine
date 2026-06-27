module;

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>
#include <glm/glm.hpp>

module Geometry.Rotation;

import Geometry.Linalg;

namespace Geometry::Rotation
{
    namespace
    {
        constexpr double kPi = 3.14159265358979323846;

        [[nodiscard]] glm::dmat3 HatD(const glm::dvec3& w)
        {
            glm::dmat3 k(0.0);
            // column-major: k[col][row]
            k[0] = glm::dvec3(0.0, w.z, -w.y);
            k[1] = glm::dvec3(-w.z, 0.0, w.x);
            k[2] = glm::dvec3(w.y, -w.x, 0.0);
            return k;
        }

        [[nodiscard]] glm::dvec3 VeeD(const glm::dmat3& m)
        {
            return glm::dvec3(
                0.5 * (m[1][2] - m[2][1]),
                0.5 * (m[2][0] - m[0][2]),
                0.5 * (m[0][1] - m[1][0]));
        }

        [[nodiscard]] bool Finite(const glm::dmat3& m)
        {
            for (int c = 0; c < 3; ++c)
                for (int r = 0; r < 3; ++r)
                    if (!std::isfinite(m[c][r])) return false;
            return true;
        }

        [[nodiscard]] glm::dmat3 ExpD(const glm::dvec3& w)
        {
            const double theta2 = glm::dot(w, w);
            const double theta = std::sqrt(theta2);
            const glm::dmat3 k = HatD(w);
            const glm::dmat3 k2 = k * k;

            // R = I + a*K + b*K^2, a = sin(t)/t, b = (1-cos(t))/t^2, with stable
            // small-angle series limits a->1, b->1/2.
            double a, b;
            if (theta < 1e-8)
            {
                a = 1.0 - theta2 / 6.0;
                b = 0.5 - theta2 / 24.0;
            }
            else
            {
                a = std::sin(theta) / theta;
                b = (1.0 - std::cos(theta)) / theta2;
            }
            return glm::dmat3(1.0) + a * k + b * k2;
        }

        [[nodiscard]] glm::dvec3 LogD(const glm::dmat3& r)
        {
            const double trace = r[0][0] + r[1][1] + r[2][2];
            const double cosTheta = std::clamp((trace - 1.0) * 0.5, -1.0, 1.0);
            const double theta = std::acos(cosTheta);
            const glm::dvec3 axial = VeeD(r); // = sin(theta) * axis

            if (theta < 1e-7)
            {
                return axial; // ~ w for small angles
            }
            if (theta < kPi - 1e-4)
            {
                return axial * (theta / std::sin(theta));
            }

            // Near pi: sin(theta) ~ 0. Use (R + I)/2 ~ axis*axis^T.
            const glm::dmat3 a = (r + glm::dmat3(1.0)) * 0.5;
            int m = 0;
            if (a[1][1] > a[m][m]) m = 1;
            if (a[2][2] > a[m][m]) m = 2;
            const double diag = std::max(0.0, a[m][m]);
            const double axm = std::sqrt(diag);
            glm::dvec3 axis(0.0);
            if (axm > 1e-12)
            {
                axis[m] = axm;
                for (int j = 0; j < 3; ++j)
                    if (j != m) axis[j] = a[m][j] / axm; // a[col=m][row=j]
                axis = glm::normalize(axis);
            }
            else
            {
                axis = glm::dvec3(1.0, 0.0, 0.0);
            }
            return axis * theta;
        }

        [[nodiscard]] glm::dmat3 ToGlm3(const Geometry::Linalg::DenseMatrix& d)
        {
            glm::dmat3 g(0.0);
            for (std::size_t row = 0; row < 3; ++row)
                for (std::size_t col = 0; col < 3; ++col)
                    g[static_cast<int>(col)][static_cast<int>(row)] = d(row, col);
            return g;
        }

        [[nodiscard]] Geometry::Linalg::DenseMatrix ToDense3(const glm::dmat3& g)
        {
            Geometry::Linalg::DenseMatrix d(3, 3);
            for (std::size_t row = 0; row < 3; ++row)
                for (std::size_t col = 0; col < 3; ++col)
                    d(row, col) = g[static_cast<int>(col)][static_cast<int>(row)];
            return d;
        }

        // Nearest rotation to M via SVD with reflection correction.
        [[nodiscard]] glm::dmat3 NearestRotationD(const glm::dmat3& m)
        {
            if (!Finite(m))
            {
                return glm::dmat3(1.0);
            }
            const Geometry::Linalg::SVDResult svd = Geometry::Linalg::ComputeSVD(ToDense3(m));
            if (!svd.Diagnostics.Succeeded() || svd.U.Rows != 3 || svd.Vt.Rows != 3)
            {
                return glm::dmat3(1.0);
            }
            const glm::dmat3 u = ToGlm3(svd.U);
            const glm::dmat3 vt = ToGlm3(svd.Vt);
            double d = glm::determinant(u * vt);
            d = (d < 0.0) ? -1.0 : 1.0;
            glm::dmat3 dDiag(1.0);
            dDiag[2][2] = d;
            return u * dDiag * vt;
        }

        [[nodiscard]] glm::mat3 ToFloat(const glm::dmat3& g)
        {
            glm::mat3 f(1.0f);
            for (int c = 0; c < 3; ++c)
                for (int r = 0; r < 3; ++r)
                    f[c][r] = static_cast<float>(g[c][r]);
            return f;
        }

        // SplitMix64 for deterministic seeding.
        [[nodiscard]] std::uint64_t SplitMix64(std::uint64_t& state)
        {
            state += 0x9E3779B97F4A7C15ULL;
            std::uint64_t z = state;
            z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
            z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
            return z ^ (z >> 31);
        }

        [[nodiscard]] double NextUnit(std::uint64_t& state)
        {
            // 53-bit mantissa uniform in [0, 1).
            return static_cast<double>(SplitMix64(state) >> 11) * (1.0 / 9007199254740992.0);
        }
    }

    glm::mat3 Hat(const glm::vec3& w) { return ToFloat(HatD(glm::dvec3(w))); }

    glm::vec3 Vee(const glm::mat3& m)
    {
        const glm::dvec3 v = VeeD(glm::dmat3(m));
        return glm::vec3(static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z));
    }

    glm::mat3 Exp(const glm::vec3& axisAngle)
    {
        const glm::dvec3 w(axisAngle);
        if (!std::isfinite(w.x) || !std::isfinite(w.y) || !std::isfinite(w.z))
        {
            return glm::mat3(1.0f);
        }
        return ToFloat(ExpD(w));
    }

    glm::vec3 Log(const glm::mat3& rotation)
    {
        const glm::dmat3 r(rotation);
        if (!Finite(r))
        {
            return glm::vec3(0.0f);
        }
        const glm::dvec3 w = LogD(r);
        return glm::vec3(static_cast<float>(w.x), static_cast<float>(w.y), static_cast<float>(w.z));
    }

    float AngularDistance(const glm::mat3& a, const glm::mat3& b)
    {
        const glm::dmat3 rel = glm::transpose(glm::dmat3(a)) * glm::dmat3(b);
        const double trace = rel[0][0] + rel[1][1] + rel[2][2];
        const double cosTheta = std::clamp((trace - 1.0) * 0.5, -1.0, 1.0);
        return static_cast<float>(std::acos(cosTheta));
    }

    float ChordalDistance(const glm::mat3& a, const glm::mat3& b)
    {
        double sum = 0.0;
        for (int c = 0; c < 3; ++c)
            for (int r = 0; r < 3; ++r)
            {
                const double d = static_cast<double>(a[c][r]) - static_cast<double>(b[c][r]);
                sum += d * d;
            }
        return static_cast<float>(std::sqrt(sum));
    }

    glm::mat3 RandomRotation(std::uint64_t seed)
    {
        std::uint64_t state = seed + 0x123456789ABCDEFULL;
        // Shoemake's uniform random quaternion.
        const double u1 = NextUnit(state);
        const double u2 = NextUnit(state);
        const double u3 = NextUnit(state);
        const double s1 = std::sqrt(1.0 - u1);
        const double s2 = std::sqrt(u1);
        const double t2 = 2.0 * kPi * u2;
        const double t3 = 2.0 * kPi * u3;
        const double x = s1 * std::sin(t2);
        const double y = s1 * std::cos(t2);
        const double z = s2 * std::sin(t3);
        const double w = s2 * std::cos(t3);

        // Quaternion (w,x,y,z) -> rotation matrix (column-major).
        glm::dmat3 r(1.0);
        r[0][0] = 1.0 - 2.0 * (y * y + z * z);
        r[0][1] = 2.0 * (x * y + z * w);
        r[0][2] = 2.0 * (x * z - y * w);
        r[1][0] = 2.0 * (x * y - z * w);
        r[1][1] = 1.0 - 2.0 * (x * x + z * z);
        r[1][2] = 2.0 * (y * z + x * w);
        r[2][0] = 2.0 * (x * z + y * w);
        r[2][1] = 2.0 * (y * z - x * w);
        r[2][2] = 1.0 - 2.0 * (x * x + y * y);
        return ToFloat(r);
    }

    glm::mat3 ProjectOnSO3(const glm::mat3& m)
    {
        return ToFloat(NearestRotationD(glm::dmat3(m)));
    }

    glm::mat3 OptimalRotation(std::span<const glm::vec3> from, std::span<const glm::vec3> to)
    {
        return OptimalRotation(from, to, std::span<const float>{});
    }

    glm::mat3 OptimalRotation(std::span<const glm::vec3> from,
                              std::span<const glm::vec3> to,
                              std::span<const float> weights)
    {
        if (from.empty() || from.size() != to.size())
        {
            return glm::mat3(1.0f);
        }
        const bool useWeights = (weights.size() == from.size());

        // Cross-covariance H = sum w_i * to_i * from_i^T.
        glm::dmat3 h(0.0);
        for (std::size_t i = 0; i < from.size(); ++i)
        {
            const glm::dvec3 f(from[i]);
            const glm::dvec3 t(to[i]);
            const double w = useWeights ? static_cast<double>(weights[i]) : 1.0;
            if (!std::isfinite(f.x) || !std::isfinite(t.x) || !std::isfinite(w))
            {
                continue;
            }
            for (int row = 0; row < 3; ++row)
                for (int col = 0; col < 3; ++col)
                    h[col][row] += w * t[row] * f[col];
        }
        return ToFloat(NearestRotationD(h));
    }
}
