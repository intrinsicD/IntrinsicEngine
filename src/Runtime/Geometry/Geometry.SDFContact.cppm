module;
#include <glm/glm.hpp>
#include <optional>
#include <functional>
#include <concepts>

export module Runtime.Geometry.SDF.General;

import Runtime.Geometry.Contact;

export namespace Runtime::Geometry::SDF
{
    // Concept: A callable that takes a vec3 (World Space) and returns float (Signed Distance)
    template<typename T>
    concept SDFFunc = requires(T f, glm::vec3 p) {
        { f(p) } -> std::convertible_to<float>;
    };

    // Numerical Gradient (Finite Difference)
    // Calculates the normal vector of the SDF at point p
    template<SDFFunc Func>
    glm::vec3 CalculateGradient(const glm::vec3& p, Func&& sdf)
    {
        const float h = 0.0001f; // Step size
        const float dx = sdf(p + glm::vec3(h, 0, 0)) - sdf(p - glm::vec3(h, 0, 0));
        const float dy = sdf(p + glm::vec3(0, h, 0)) - sdf(p - glm::vec3(0, h, 0));
        const float dz = sdf(p + glm::vec3(0, 0, h)) - sdf(p - glm::vec3(0, 0, h));

        return glm::normalize(glm::vec3(dx, dy, dz));
    }

    // THE SOLVER
    // Iteratively finds the contact point between two Implicit Surfaces
    template<SDFFunc FuncA, SDFFunc FuncB>
    std::optional<ContactManifold> Contact_General_SDF(
        FuncA&& sdfA,
        FuncB&& sdfB,
        glm::vec3 guess)
    {
        const int MAX_ITER = 32;
        const float TOLERANCE = 0.001f;
        const float LEARNING_RATE = 0.4f;

        glm::vec3 currentPos = guess;

        for(int i = 0; i < MAX_ITER; ++i)
        {
            float dA = sdfA(currentPos);
            float dB = sdfB(currentPos);

            // 1. Intersection Detection
            // If we are inside both shapes (distance < 0), we have a collision.
            // We include a small tolerance to catch touching surfaces.
            if(dA < TOLERANCE && dB < TOLERANCE)
            {
                ContactManifold m;

                glm::vec3 gradA = CalculateGradient(currentPos, sdfA);
                glm::vec3 gradB = CalculateGradient(currentPos, sdfB);

                // The contact normal is the direction to separate B from A.
                // We use the difference of gradients to find the "average" separation axis.
                glm::vec3 separationAxis = gradB - gradA;

                // Fallback for concentric/symmetric cases where gradients cancel out
                if (glm::dot(separationAxis, separationAxis) < 1e-6f) {
                    m.Normal = glm::vec3(0, 1, 0);
                } else {
                    m.Normal = glm::normalize(separationAxis);
                }

                // Approximate Penetration Depth
                // Sum of negative distances roughly equals the overlap thickness at this point
                m.PenetrationDepth = -(dA + dB);

                // Clamp to avoid crazy values in shallow grazes
                if (m.PenetrationDepth < 0.0f) m.PenetrationDepth = 0.0f;

                // Project point back to surfaces to get contact points
                m.ContactPointA = currentPos - gradA * dA;
                m.ContactPointB = currentPos - gradB * dB;

                return m;
            }

            // 2. Optimization Step
            // We want to minimize potential = max(dA, 0) + max(dB, 0)
            // But actually, we just want to walk downhill on both surfaces.

            glm::vec3 gradA = CalculateGradient(currentPos, sdfA);
            glm::vec3 gradB = CalculateGradient(currentPos, sdfB);

            // Simple weighted descent
            glm::vec3 move = -(gradA * dA + gradB * dB) * LEARNING_RATE;

            if (glm::length(move) < 1e-5f) break; // Converged without intersection

            currentPos += move;
        }

        return std::nullopt;
    }
}