module;

#include <cstddef>

export module Geometry:MeshOperator;

export namespace Geometry
{
    // =========================================================================
    // RemeshingOperationResult — shared result type for remeshing-family operators.
    // =========================================================================
    //
    // Both Remeshing::RemeshingResult and AdaptiveRemeshing::AdaptiveRemeshingResult
    // are structurally identical. Each operator declares a type alias to this
    // struct so there is a single definition to maintain.
    //
    // Fields:
    //   IterationsPerformed — actual iterations run (may be less than requested).
    //   SplitCount          — edge splits performed across all iterations.
    //   CollapseCount       — edge collapses performed across all iterations.
    //   FlipCount           — edge flips performed across all iterations.
    //   FinalVertexCount    — vertex count after the last iteration.
    //   FinalEdgeCount      — edge count after the last iteration.
    //   FinalFaceCount      — face count after the last iteration.
    struct RemeshingOperationResult
    {
        std::size_t IterationsPerformed{0};
        std::size_t SplitCount{0};
        std::size_t CollapseCount{0};
        std::size_t FlipCount{0};
        std::size_t FinalVertexCount{0};
        std::size_t FinalEdgeCount{0};
        std::size_t FinalFaceCount{0};
    };

} // namespace Geometry
