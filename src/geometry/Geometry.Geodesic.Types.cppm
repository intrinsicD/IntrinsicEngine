// Owned virtual-source results shared by geometry execution and runtime reports.
// Consumers can inspect complete diagnostics without importing mesh algorithms.
module;
#include <cstddef>
#include <cstdint>
#include <vector>
export module Geometry.Geodesic.Types;

export namespace Geometry::Geodesic
{
    enum class VirtualSourceStatus : std::uint8_t
    {
        Success,
        EmptyMesh,
        UnsupportedSubmeshView,
        InvalidParameters,
        InvalidSources,
        InvalidPositions,
        NonTriangleFace,
        DegenerateFace,
        ExpansionLimit,
        NumericalFailure,
    };

    struct VirtualSourceResult
    {
        VirtualSourceStatus Status{VirtualSourceStatus::EmptyMesh};
        std::size_t Iterations{0};
        std::size_t HalfedgeExpansions{0};
        std::size_t TriangleUpdates{0};
        std::size_t SourceCount{0};
        std::size_t UnreachableVertexCount{0};
        double MeanEdgeLength{0};
        // Absolute vertex slots, in position units. Unreachable/deleted slots
        // are +infinity; sources, including isolated sources, are exactly zero.
        // Failure returns no distance field and never changes mesh properties.
        std::vector<double> Distances{};
        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == VirtualSourceStatus::Success;
        }
    };
}
