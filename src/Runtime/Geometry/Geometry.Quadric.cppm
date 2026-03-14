module;

#include <cmath>
#include <optional>
#include <glm/glm.hpp>

export module Geometry:Quadric;

import :Validation;

namespace Geometry::QuadricDetail
{
    using Validation::IsFinite;

    [[nodiscard]] inline glm::dmat3 SelfOuterProduct(glm::dvec3 const& v) noexcept
    {
        return glm::outerProduct(v, v);
    }

    [[nodiscard]] inline double TraceOfProduct(glm::dmat3 const& A, glm::dmat3 const& B) noexcept
    {
        double trace = 0.0;
        for (int i = 0; i < 3; ++i)
        {
            for (int j = 0; j < 3; ++j)
            {
                trace += A[i][j] * B[j][i];
            }
        }
        return trace;
    }

    [[nodiscard]] inline glm::dmat3 Symmetrize(glm::dmat3 const& M) noexcept
    {
        return 0.5 * (M + glm::transpose(M));
    }

    [[nodiscard]] inline glm::dmat3 SanitizedSymmetricMatrix(glm::dmat3 const& M) noexcept
    {
        glm::dmat3 sanitized{0.0};
        for (int c = 0; c < 3; ++c)
        {
            for (int r = 0; r < 3; ++r)
            {
                sanitized[c][r] = IsFinite(M[c][r]) ? M[c][r] : 0.0;
            }
        }
        return Symmetrize(sanitized);
    }

    [[nodiscard]] inline glm::dmat3 CrossProductSquaredTranspose(glm::dvec3 const& v) noexcept
    {
        const double a = v.x;
        const double b = v.y;
        const double c = v.z;
        const double a2 = a * a;
        const double b2 = b * b;
        const double c2 = c * c;

        glm::dmat3 M{0.0};
        M[0][0] = b2 + c2;
        M[1][1] = a2 + c2;
        M[2][2] = a2 + b2;

        M[1][0] = -a * b;
        M[2][0] = -a * c;
        M[2][1] = -b * c;

        M[0][1] = M[1][0];
        M[0][2] = M[2][0];
        M[1][2] = M[2][1];
        return M;
    }

    [[nodiscard]] inline glm::dmat3 CrossInterferenceMatrix(glm::dmat3 const& A, glm::dmat3 const& B) noexcept
    {
        constexpr int x = 0;
        constexpr int y = 1;
        constexpr int z = 2;

        glm::dmat3 M{0.0};

        const double cxx = A[y][z] * B[y][z];
        const double cyy = A[x][z] * B[x][z];
        const double czz = A[x][y] * B[x][y];

        M[x][x] = A[y][y] * B[z][z] - 2.0 * cxx + A[z][z] * B[y][y];
        M[y][y] = A[x][x] * B[z][z] - 2.0 * cyy + A[z][z] * B[x][x];
        M[z][z] = A[x][x] * B[y][y] - 2.0 * czz + A[y][y] * B[x][x];

        M[x][y] = -A[x][y] * B[z][z] + A[x][z] * B[y][z] + A[y][z] * B[x][z] - A[z][z] * B[x][y];
        M[x][z] = A[x][y] * B[y][z] - A[x][z] * B[y][y] - A[y][y] * B[x][z] + A[y][z] * B[x][y];
        M[y][z] = -A[x][x] * B[y][z] + A[x][y] * B[x][z] + A[x][z] * B[x][y] - A[y][z] * B[x][x];

        M[y][x] = M[x][y];
        M[z][x] = M[x][z];
        M[z][y] = M[y][z];
        return M;
    }

    [[nodiscard]] inline glm::dmat3 FirstOrderTriQuad(glm::dvec3 const& a, glm::dmat3 const& sigma) noexcept
    {
        const double xx = a.x * a.x;
        const double xy = a.x * a.y;
        const double xz = a.x * a.z;
        const double yy = a.y * a.y;
        const double yz = a.y * a.z;
        const double zz = a.z * a.z;

        glm::dmat3 M{0.0};

        M[0][0] = -sigma[1][1] * zz + 2.0 * sigma[1][2] * yz - sigma[2][2] * yy;
        M[0][1] = sigma[0][1] * zz - sigma[0][2] * yz - sigma[1][2] * xz + sigma[2][2] * xy;
        M[0][2] = -sigma[0][1] * yz + sigma[0][2] * yy + sigma[1][1] * xz - sigma[1][2] * xy;
        M[1][1] = -sigma[0][0] * zz + 2.0 * sigma[0][2] * xz - sigma[2][2] * xx;
        M[1][2] = sigma[0][0] * yz - sigma[0][1] * xz - sigma[0][2] * xy + sigma[1][2] * xx;
        M[2][2] = -sigma[0][0] * yy + 2.0 * sigma[0][1] * xy - sigma[1][1] * xx;

        M[1][0] = M[0][1];
        M[2][0] = M[0][2];
        M[2][1] = M[1][2];
        return M;
    }
}

export namespace Geometry
{
    // =====================================================================
    // Quadric — symmetric 3x3 + linear + constant: Q(x) = x^T A x - 2 b^T x + c
    // =====================================================================

    struct Quadric
    {
        double A00{0.0};
        double A01{0.0};
        double A02{0.0};
        double A11{0.0};
        double A12{0.0};
        double A22{0.0};
        double b0{0.0};
        double b1{0.0};
        double b2{0.0};
        double c{0.0};

        [[nodiscard]] static Quadric CoefficientsQuadric(const glm::dmat3& A, const glm::dvec3& b, double c)
        {
            Quadric q;
            q.A00 = A[0][0];
            q.A01 = A[0][1];
            q.A02 = A[0][2];
            q.A11 = A[1][1];
            q.A12 = A[1][2];
            q.A22 = A[2][2];
            q.b0 = b[0];
            q.b1 = b[1];
            q.b2 = b[2];
            q.c = c;
            return q;
        }

        [[nodiscard]] static Quadric PlaneQuadric(const glm::vec3& point, const glm::vec3& normal) noexcept
        {
            auto const d = glm::dot(point, normal);
            return CoefficientsQuadric(glm::outerProduct(normal, normal), normal * d, d * d);
        }

        [[nodiscard]] static Quadric ProbabilisticPlaneQuadric(
            glm::dvec3 const& meanPoint,
            glm::dvec3 const& meanNormal,
            double positionStdDev,
            double normalStdDev) noexcept
        {
            using namespace Geometry::QuadricDetail;

            if (!IsFinite(meanPoint) || !IsFinite(meanNormal) || !IsFinite(positionStdDev) || !IsFinite(normalStdDev))
            {
                return {};
            }

            const double sigmaNormal = normalStdDev * normalStdDev;
            const double sigmaPosition = positionStdDev * positionStdDev;
            const double d = glm::dot(meanPoint, meanNormal);

            glm::dmat3 A = SelfOuterProduct(meanNormal);
            A[0][0] += sigmaNormal;
            A[1][1] += sigmaNormal;
            A[2][2] += sigmaNormal;

            const glm::dvec3 b = meanNormal * d + meanPoint * sigmaNormal;
            const double c = d * d
                + sigmaNormal * glm::dot(meanPoint, meanPoint)
                + sigmaPosition * glm::dot(meanNormal, meanNormal)
                + 3.0 * sigmaPosition * sigmaNormal;

            return CoefficientsQuadric(A, b, c);
        }

        [[nodiscard]] static Quadric ProbabilisticPlaneQuadric(
            glm::dvec3 const& meanPoint,
            glm::dvec3 const& meanNormal,
            glm::dmat3 const& sigmaPoint,
            glm::dmat3 const& sigmaNormal) noexcept
        {
            using namespace Geometry::QuadricDetail;

            if (!IsFinite(meanPoint) || !IsFinite(meanNormal))
            {
                return {};
            }

            const glm::dmat3 sigmaP = SanitizedSymmetricMatrix(sigmaPoint);
            const glm::dmat3 sigmaN = SanitizedSymmetricMatrix(sigmaNormal);
            const double d = glm::dot(meanPoint, meanNormal);

            const glm::dmat3 A = SelfOuterProduct(meanNormal) + sigmaN;
            const glm::dvec3 b = meanNormal * d + sigmaN * meanPoint;
            const double c = d * d
                + glm::dot(meanPoint, sigmaN * meanPoint)
                + glm::dot(meanNormal, sigmaP * meanNormal)
                + TraceOfProduct(sigmaN, sigmaP);

            return CoefficientsQuadric(A, b, c);
        }

        [[nodiscard]] static Quadric TriangleQuadric(const glm::vec3& p, const glm::vec3& q, const glm::vec3& r)
        {
            auto const pxq = glm::cross(p, q);
            auto const qxr = glm::cross(q, r);
            auto const rxp = glm::cross(r, p);

            auto const xsum = pxq + qxr + rxp;
            auto const det = glm::dot(pxq, r);

            return CoefficientsQuadric(glm::outerProduct(xsum, xsum), xsum * det, det * det);
        }

        [[nodiscard]] static Quadric ProbabilisticTriangleQuadric(
            glm::dvec3 const& meanP,
            glm::dvec3 const& meanQ,
            glm::dvec3 const& meanR,
            double positionStdDev) noexcept
        {
            using namespace Geometry::QuadricDetail;

            if (!IsFinite(meanP) || !IsFinite(meanQ) || !IsFinite(meanR) || !IsFinite(positionStdDev))
            {
                return {};
            }

            const double sigma = positionStdDev * positionStdDev;

            const glm::dvec3 pxq = glm::cross(meanP, meanQ);
            const glm::dvec3 qxr = glm::cross(meanQ, meanR);
            const glm::dvec3 rxp = glm::cross(meanR, meanP);

            const double detPqr = glm::dot(pxq, meanR);
            const glm::dvec3 crossPqr = pxq + qxr + rxp;

            const glm::dvec3 pMinusQ = meanP - meanQ;
            const glm::dvec3 qMinusR = meanQ - meanR;
            const glm::dvec3 rMinusP = meanR - meanP;

            glm::dmat3 A = SelfOuterProduct(crossPqr)
                + (CrossProductSquaredTranspose(pMinusQ)
                + CrossProductSquaredTranspose(qMinusR)
                + CrossProductSquaredTranspose(rMinusP)) * sigma;

            const double sigma2 = sigma * sigma;
            A[0][0] += 6.0 * sigma2;
            A[1][1] += 6.0 * sigma2;
            A[2][2] += 6.0 * sigma2;

            glm::dvec3 b = crossPqr * detPqr;
            b -= (glm::cross(pMinusQ, pxq) + glm::cross(qMinusR, qxr) + glm::cross(rMinusP, rxp)) * sigma;
            b += (meanP + meanQ + meanR) * (2.0 * sigma2);

            double c = detPqr * detPqr;
            c += sigma * (glm::dot(pxq, pxq) + glm::dot(qxr, qxr) + glm::dot(rxp, rxp));
            c += 2.0 * sigma2 * (glm::dot(meanP, meanP) + glm::dot(meanQ, meanQ) + glm::dot(meanR, meanR));
            c += 6.0 * sigma2 * sigma;

            return CoefficientsQuadric(A, b, c);
        }

        [[nodiscard]] static Quadric ProbabilisticTriangleQuadric(
            glm::dvec3 const& meanP,
            glm::dvec3 const& meanQ,
            glm::dvec3 const& meanR,
            glm::dmat3 const& sigmaP,
            glm::dmat3 const& sigmaQ,
            glm::dmat3 const& sigmaR) noexcept
        {
            using namespace Geometry::QuadricDetail;

            if (!IsFinite(meanP) || !IsFinite(meanQ) || !IsFinite(meanR))
            {
                return {};
            }

            const glm::dmat3 SigmaP = SanitizedSymmetricMatrix(sigmaP);
            const glm::dmat3 SigmaQ = SanitizedSymmetricMatrix(sigmaQ);
            const glm::dmat3 SigmaR = SanitizedSymmetricMatrix(sigmaR);

            const glm::dvec3 pxq = glm::cross(meanP, meanQ);
            const glm::dvec3 qxr = glm::cross(meanQ, meanR);
            const glm::dvec3 rxp = glm::cross(meanR, meanP);

            const double detPqr = glm::dot(pxq, meanR);
            const glm::dvec3 crossPqr = pxq + qxr + rxp;

            const glm::dvec3 pMinusQ = meanP - meanQ;
            const glm::dvec3 qMinusR = meanQ - meanR;
            const glm::dvec3 rMinusP = meanR - meanP;

            const glm::dmat3 ciPQ = CrossInterferenceMatrix(SigmaP, SigmaQ);
            const glm::dmat3 ciQR = CrossInterferenceMatrix(SigmaQ, SigmaR);
            const glm::dmat3 ciRP = CrossInterferenceMatrix(SigmaR, SigmaP);

            glm::dmat3 A = SelfOuterProduct(crossPqr);
            A -= FirstOrderTriQuad(pMinusQ, SigmaR);
            A -= FirstOrderTriQuad(qMinusR, SigmaP);
            A -= FirstOrderTriQuad(rMinusP, SigmaQ);
            A += ciPQ + ciQR + ciRP;
            A = Symmetrize(A);

            glm::dvec3 b = crossPqr * detPqr;
            b -= glm::cross(pMinusQ, SigmaR * pxq);
            b -= glm::cross(qMinusR, SigmaP * qxr);
            b -= glm::cross(rMinusP, SigmaQ * rxp);
            b += ciPQ * meanR;
            b += ciQR * meanP;
            b += ciRP * meanQ;

            double c = detPqr * detPqr;
            c += glm::dot(pxq, SigmaR * pxq);
            c += glm::dot(qxr, SigmaP * qxr);
            c += glm::dot(rxp, SigmaQ * rxp);
            c += glm::dot(meanP, ciQR * meanP);
            c += glm::dot(meanQ, ciRP * meanQ);
            c += glm::dot(meanR, ciPQ * meanR);
            c += TraceOfProduct(SigmaR, ciPQ);

            return CoefficientsQuadric(A, b, c);
        }

        [[nodiscard]] static Quadric PointQuadric(const glm::dvec3& p)
        {
            return CoefficientsQuadric(glm::dmat3(1.0), p, glm::dot(p, p));
        }

        Quadric& operator+=(const Quadric& rhs) noexcept
        {
            A00 += rhs.A00;
            A01 += rhs.A01;
            A02 += rhs.A02;
            A11 += rhs.A11;
            A12 += rhs.A12;
            A22 += rhs.A22;

            b0 += rhs.b0;
            b1 += rhs.b1;
            b2 += rhs.b2;

            c += rhs.c;

            return *this;
        }

        Quadric& operator-=(const Quadric& rhs)
        {
            A00 -= rhs.A00;
            A01 -= rhs.A01;
            A02 -= rhs.A02;
            A11 -= rhs.A11;
            A12 -= rhs.A12;
            A22 -= rhs.A22;

            b0 -= rhs.b0;
            b1 -= rhs.b1;
            b2 -= rhs.b2;

            c -= rhs.c;

            return *this;
        }

        Quadric& operator*=(double s) noexcept
        {
            A00 *= s;
            A01 *= s;
            A02 *= s;
            A11 *= s;
            A12 *= s;
            A22 *= s;
            b0 *= s;
            b1 *= s;
            b2 *= s;
            c *= s;
            return *this;
        }

        Quadric operator+() const { return *this; }

        Quadric operator-() const
        {
            Quadric q;
            q.A00 = -A00;
            q.A01 = -A01;
            q.A02 = -A02;
            q.A11 = -A11;
            q.A12 = -A12;
            q.A22 = -A22;

            q.b0 = -b0;
            q.b1 = -b1;
            q.b2 = -b2;

            q.c = -c;
            return q;
        }

        Quadric& operator/=(double s) { return operator*=(1.0 / s); }

        Quadric operator+(Quadric const& b) const
        {
            auto r = *this; // copy
            r += b;
            return r;
        }

        Quadric operator-(const Quadric& b) const
        {
            auto r = *this; // copy
            r -= b;
            return r;
        }

        Quadric operator*(double b) const
        {
            auto r = *this; // copy
            r *= b;
            return r;
        }

        Quadric operator/(double b) const
        {
            auto r = *this; // copy
            r /= b;
            return r;
        }

        [[nodiscard]] double Evaluate(glm::dvec3 const& p) const noexcept
        {
            const glm::dvec3 Ap{
                A00 * p.x + A01 * p.y + A02 * p.z,
                A01 * p.x + A11 * p.y + A12 * p.z,
                A02 * p.x + A12 * p.y + A22 * p.z
            };
            return glm::dot(p, Ap) - 2.0 * (p.x * b0 + p.y * b1 + p.z * b2) + c;
        }

        [[nodiscard]] glm::dmat3 Matrix() const noexcept
        {
            glm::dmat3 A{0.0};
            A[0][0] = A00;
            A[0][1] = A01;
            A[0][2] = A02;
            A[1][0] = A01;
            A[1][1] = A11;
            A[1][2] = A12;
            A[2][0] = A02;
            A[2][1] = A12;
            A[2][2] = A22;
            return A;
        }

        [[nodiscard]] glm::dvec3 LinearTerm() const noexcept
        {
            return {b0, b1, b2};
        }

        [[nodiscard]] std::optional<glm::vec3> TryMinimizer(double determinantEpsilon = 1e-15) const noexcept
        {
            const glm::dmat3 A = Matrix();
            const double det = glm::determinant(A);
            if (!std::isfinite(det) || std::abs(det) <= determinantEpsilon)
            {
                return std::nullopt;
            }

            const glm::dvec3 x = glm::inverse(A) * LinearTerm();
            if (!std::isfinite(x.x) || !std::isfinite(x.y) || !std::isfinite(x.z))
            {
                return std::nullopt;
            }

            return glm::vec3(x);
        }

        [[nodiscard]] glm::vec3 Minimizer() const {
            // Returns a point minimizing this quadric
            // Solving Ax = r with using an unrolled https://en.wikipedia.org/wiki/Cholesky_decomposition
            // Thanks to @jdumas and @sarah-ek for this optimized implementation!
            using std::fma;

            auto a11 = A11;
            auto a21 = A12;
            auto a22 = A22;
            auto x0 = b0;
            auto x1 = b1;
            auto x2 = b2;

            auto d0 = 1.0 / A00;
            auto l10 = A01 * -d0;
            auto l20 = A02 * -d0;

            a11 = fma(A01, l10, a11);
            a21 = fma(A02, l10, a21);
            a22 = fma(A02, l20, a22);

            auto d1 = 1.0 / a11;
            auto l21 = a21 * -d1;
            a22 = fma(a21, l21, a22);

            auto d2 = 1.0 / a22;

            x1 = fma(l10, x0, x1);
            x2 = fma(l20, x0, x2);
            x2 = fma(l21, x1, x2);

            x0 *= d0;
            x1 *= d1;
            x2 *= d2;

            x0 = fma(l20, x2, x0);
            x1 = fma(l21, x2, x1);
            x0 = fma(l10, x1, x0);

            return {x0, x1, x2};
        }

        [[nodiscard]] double Evaluate(glm::vec3 const& p) const noexcept
        {
            return Evaluate(glm::dvec3(static_cast<double>(p.x), static_cast<double>(p.y), static_cast<double>(p.z)));
        }

    };
}
