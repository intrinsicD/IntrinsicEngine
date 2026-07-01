module;

#include <cstddef>
#include <span>
#include <vector>

#include <glm/glm.hpp>
#include <glm/mat4x4.hpp>

export module Extrinsic.Runtime.RegistrationAlignment;

import Geometry.Registration;

export namespace Extrinsic::Runtime
{
    // Runtime-side helper that runs ICP between two point clouds and captures the
    // full per-iteration convergence trajectory via the AlignICP observer seam
    // (see docs/architecture/geometry-pipeline-modularity.md §3.4). It exists so
    // editor/visualization code can watch the source shape converge onto the
    // target without the geometry layer knowing anything about ECS, rendering, or
    // the UI. Layer: runtime -> geometry only.
    struct RegistrationAlignmentOutcome
    {
        // False only when AlignICP rejected its inputs (too few points, invalid
        // params). When false, Result/Traces are empty defaults.
        bool HasResult{false};

        // Final alignment + convergence diagnostics from AlignICP.
        Geometry::Registration::RegistrationResult Result{};

        // One trace per completed ICP iteration, in order (from the observer).
        // Traces.size() == Result.IterationsPerformed.
        std::vector<Geometry::Registration::IterationTrace> Traces{};

        [[nodiscard]] std::size_t IterationCount() const noexcept
        {
            return Traces.size();
        }
    };

    // Run ICP source -> target, capturing every iteration's cumulative transform.
    // Pure CPU; forwards to Geometry::Registration::AlignICP with a trace-collecting
    // observer. targetNormals may be empty (point-to-point / fallback).
    [[nodiscard]] RegistrationAlignmentOutcome AlignPointClouds(
        std::span<const glm::vec3> sourcePoints,
        std::span<const glm::vec3> targetPoints,
        std::span<const glm::vec3> targetNormals = {},
        const Geometry::Registration::RegistrationParams& params = {});

    // Renderer-facing cumulative source->target pose to preview at trajectory
    // step `index`:
    //   index 0            -> identity (the un-registered starting pose)
    //   index 1..N         -> Traces[index - 1].Transform (after that iteration)
    //   index > N          -> clamped to the final pose
    // Returned as a float matrix suitable for an ECS Transform / renderer. A UI
    // slider over [0, IterationCount()] scrubs the whole convergence.
    [[nodiscard]] glm::mat4 TrajectoryPose(
        const RegistrationAlignmentOutcome& outcome, std::size_t index);
}
