module;
#include <glm/glm.hpp>
#include <optional>
#include <functional>
#include <concepts>

export module Geometry:SDFContact;

import :Contact;

export namespace Geometry::SDF
{
    template <typename T>
    concept SDFFunc = requires(T f, glm::vec3 p)
    {
        { f(p) } -> std::convertible_to<float>;
    };

    template <SDFFunc Func>
    glm::vec3 CalculateGradient(const glm::vec3& p, Func&& sdf)
    {
        const float h = 0.0001f;
        const float dx = sdf(p + glm::vec3(h, 0, 0)) - sdf(p - glm::vec3(h, 0, 0));
        const float dy = sdf(p + glm::vec3(0, h, 0)) - sdf(p - glm::vec3(0, h, 0));
        const float dz = sdf(p + glm::vec3(0, 0, h)) - sdf(p - glm::vec3(0, 0, h));
        return glm::normalize(glm::vec3(dx, dy, dz));
    }

    template <SDFFunc FuncA, SDFFunc FuncB>
    std::optional<ContactManifold> Contact_General_SDF(
        FuncA&& sdfA,
        FuncB&& sdfB,
        glm::vec3 guess)
    {
        const int MAX_ITER = 32;
        const float TOLERANCE = 0.001f;

        // POCS (Projection on Convex Sets) Relaxation factor
        // 1.0 = instant snap (unstable), 0.5-0.8 = stable convergence
        const float RELAXATION = 0.8f;

        glm::vec3 currentPos = guess;

        for (int i = 0; i < MAX_ITER; ++i)
        {
            float dA = sdfA(currentPos);
            float dB = sdfB(currentPos);

            // 1. Intersection Detection
            // We include a small tolerance to catch touching surfaces.
            if (dA < TOLERANCE && dB < TOLERANCE)
            {
                ContactManifold m;
                glm::vec3 gradA = CalculateGradient(currentPos, sdfA);
                glm::vec3 gradB = CalculateGradient(currentPos, sdfB);

                // Normal A -> B
                glm::vec3 separationAxis = gradA - gradB;
                if (glm::dot(separationAxis, separationAxis) < 1e-6f)
                {
                    m.Normal = glm::vec3(0, 1, 0);
                }
                else
                {
                    m.Normal = glm::normalize(separationAxis);
                }

                m.PenetrationDepth = -(dA + dB);
                if (m.PenetrationDepth < 0.0f) m.PenetrationDepth = 0.0f;

                m.ContactPointA = currentPos - gradA * dA;
                m.ContactPointB = currentPos - gradB * dB;

                return m;
            }

            // 2. Projection Step
            // Instead of weighted sum, we project to the surfaces we are OUTSIDE of.
            // If inside a shape, it contributes zero correction vector.

            glm::vec3 move(0.0f);
            int contributors = 0;

            if (dA > TOLERANCE)
            {
                glm::vec3 gradA = CalculateGradient(currentPos, sdfA);
                move -= gradA * dA;
                contributors++;
            }
            if (dB > TOLERANCE)
            {
                glm::vec3 gradB = CalculateGradient(currentPos, sdfB);
                move -= gradB * dB;
                contributors++;
            }

            // If we are "inside" both (but failed tolerance check?), implies shallow intersection or noise.
            // Just average the move.
            if (contributors > 0)
            {
                currentPos += (move / (float)contributors) * RELAXATION;
            }
            else
            {
                // Should be caught by check above, but as a fallback, nudge towards average gradient
                glm::vec3 gradA = CalculateGradient(currentPos, sdfA);
                glm::vec3 gradB = CalculateGradient(currentPos, sdfB);
                currentPos -= (gradA + gradB) * 0.001f;
            }
        }

        return std::nullopt;
    }
}
