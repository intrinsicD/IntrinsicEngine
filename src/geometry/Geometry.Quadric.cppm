module;

#include <optional>

#include <glm/glm.hpp>

export module Geometry.Quadric;

export namespace Geometry
{
    // =====================================================================
    // Quadric - symmetric 3x3 + linear + constant: Q(x) = x^T A x - 2 b^T x + c
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

        [[nodiscard]] static Quadric CoefficientsQuadric(const glm::dmat3& A, const glm::dvec3& b, double c);

        [[nodiscard]] static Quadric PlaneQuadric(const glm::vec3& point, const glm::vec3& normal) noexcept;

        [[nodiscard]] static Quadric ProbabilisticPlaneQuadric(
            glm::dvec3 const& meanPoint,
            glm::dvec3 const& meanNormal,
            double positionStdDev,
            double normalStdDev) noexcept;

        [[nodiscard]] static Quadric ProbabilisticPlaneQuadric(
            glm::dvec3 const& meanPoint,
            glm::dvec3 const& meanNormal,
            glm::dmat3 const& sigmaPoint,
            glm::dmat3 const& sigmaNormal) noexcept;

        [[nodiscard]] static Quadric TriangleQuadric(const glm::vec3& p, const glm::vec3& q, const glm::vec3& r);

        [[nodiscard]] static Quadric ProbabilisticTriangleQuadric(
            glm::dvec3 const& meanP,
            glm::dvec3 const& meanQ,
            glm::dvec3 const& meanR,
            double positionStdDev) noexcept;

        [[nodiscard]] static Quadric ProbabilisticTriangleQuadric(
            glm::dvec3 const& meanP,
            glm::dvec3 const& meanQ,
            glm::dvec3 const& meanR,
            glm::dmat3 const& sigmaP,
            glm::dmat3 const& sigmaQ,
            glm::dmat3 const& sigmaR) noexcept;

        [[nodiscard]] static Quadric PointQuadric(const glm::dvec3& p);

        Quadric& operator+=(const Quadric& rhs) noexcept;

        Quadric& operator-=(const Quadric& rhs);

        Quadric& operator*=(double s) noexcept;

        Quadric operator+() const;

        Quadric operator-() const;

        Quadric& operator/=(double s);

        Quadric operator+(Quadric const& b) const;

        Quadric operator-(const Quadric& b) const;

        Quadric operator*(double b) const;

        Quadric operator/(double b) const;

        [[nodiscard]] double Evaluate(glm::dvec3 const& p) const noexcept;

        [[nodiscard]] glm::dmat3 Matrix() const noexcept;

        [[nodiscard]] glm::dvec3 LinearTerm() const noexcept;

        [[nodiscard]] std::optional<glm::vec3> TryMinimizer(double determinantEpsilon = 1e-15) const noexcept;

        [[nodiscard]] glm::vec3 Minimizer() const;

        [[nodiscard]] double Evaluate(glm::vec3 const& p) const noexcept;


    };
}
