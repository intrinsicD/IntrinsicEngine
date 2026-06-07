module;

#include <cstdint>
#include <optional>
#include <span>

#include <glm/glm.hpp>

export module Geometry.Sphere;

export namespace Geometry
{
    struct Sphere
    {
        glm::vec3 Center{0.0f};
        float Radius{0.0f};

        [[nodiscard]] float GetDiameter() const;
        [[nodiscard]] float GetSurfaceArea() const;
        [[nodiscard]] float GetVolume() const;
    };

    [[nodiscard]] glm::vec3 ClosestPoint(const Sphere& sphere, const glm::vec3& point);
    [[nodiscard]] double SignedDistance(const Sphere& sphere, const glm::vec3& point);
    [[nodiscard]] double Distance(const Sphere& sphere, const glm::vec3& point);
    [[nodiscard]] double SquaredDistance(const Sphere& sphere, const glm::vec3& point);
    [[nodiscard]] double Volume(const Sphere& sphere);

    struct FittingParams
    {
        enum class FittingMethod : uint8_t
        {
            None,
            LeastSquares,
            Bounding,
            Hybrid,
        };

        FittingMethod Method{FittingMethod::Hybrid};
        float MinimumRadius{0.0f};
        float ContainmentSlack{1.0e-5f};
        float SingularThreshold{1.0e-10f};
        bool EnforceContainment{false};
    };

    [[nodiscard]] std::optional<Sphere> ToSphere(std::span<const glm::vec3> points, const FittingParams& params = {});
}
