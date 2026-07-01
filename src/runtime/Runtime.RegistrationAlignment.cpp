module;

#include <algorithm>
#include <cstddef>
#include <functional>
#include <span>
#include <vector>

#include <glm/glm.hpp>
#include <glm/mat4x4.hpp>

module Extrinsic.Runtime.RegistrationAlignment;

import Geometry.Registration;

namespace Extrinsic::Runtime
{
    RegistrationAlignmentOutcome AlignPointClouds(
        std::span<const glm::vec3> sourcePoints,
        std::span<const glm::vec3> targetPoints,
        std::span<const glm::vec3> targetNormals,
        const Geometry::Registration::RegistrationParams& params)
    {
        RegistrationAlignmentOutcome outcome;
        outcome.Traces.reserve(params.MaxIterations);

        // The observer only reads state, so an observed run is identical to an
        // unobserved one; here it simply records the per-iteration snapshot.
        const auto result = Geometry::Registration::AlignICP(
            sourcePoints,
            targetPoints,
            targetNormals,
            params,
            [&outcome](const Geometry::Registration::IterationTrace& trace)
            {
                outcome.Traces.push_back(trace);
            });

        if (result)
        {
            outcome.HasResult = true;
            outcome.Result = *result;
        }
        return outcome;
    }

    glm::mat4 TrajectoryPose(
        const RegistrationAlignmentOutcome& outcome, std::size_t index)
    {
        if (index == 0 || outcome.Traces.empty())
            return glm::mat4(1.0f);

        const std::size_t clamped = std::min(index, outcome.Traces.size());
        return glm::mat4(outcome.Traces[clamped - 1].Transform);
    }
}
