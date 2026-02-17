module;

#include <cstddef>
#include <optional>

export module Geometry:MeshQuality;

import :Properties;
import :HalfedgeMesh;

export namespace Geometry::MeshQuality
{
    // =========================================================================
    // Mesh Quality Metrics
    // =========================================================================
    //
    // Comprehensive per-element and aggregate quality diagnostics for triangle
    // meshes. Computes angle, aspect ratio, edge length, valence, area, and
    // volume statistics in a single pass where possible.
    //
    // Follows the engine's operator pattern: Params with defaults, Result with
    // diagnostics, std::optional return for degenerate input.

    struct QualityParams
    {
        bool ComputeAngles{true};
        bool ComputeAspectRatios{true};
        bool ComputeEdgeLengths{true};
        bool ComputeValence{true};
        bool ComputeAreas{true};
        bool ComputeVolume{true};

        // Thresholds for counting degenerate elements
        double SmallAngleThreshold{5.0};      // degrees
        double LargeAngleThreshold{170.0};    // degrees
        double DegenerateAreaEpsilon{1e-10};
    };

    struct QualityResult
    {
        // --- Angle statistics (degrees) ---
        double MinAngle{0.0};
        double MaxAngle{0.0};
        double MeanAngle{0.0};

        // --- Aspect ratio: longest_edge / (2 * sqrt(3) * inradius) ---
        // Normalized so equilateral triangle = 1.0
        double MinAspectRatio{0.0};
        double MaxAspectRatio{0.0};
        double MeanAspectRatio{0.0};

        // --- Edge length statistics ---
        double MinEdgeLength{0.0};
        double MaxEdgeLength{0.0};
        double MeanEdgeLength{0.0};
        double StdDevEdgeLength{0.0};

        // --- Valence statistics ---
        std::size_t MinValence{0};
        std::size_t MaxValence{0};
        double MeanValence{0.0};

        // --- Area statistics ---
        double MinFaceArea{0.0};
        double MaxFaceArea{0.0};
        double TotalArea{0.0};
        double MeanFaceArea{0.0};

        // --- Volume (signed, via divergence theorem) ---
        // Only meaningful for closed meshes
        double Volume{0.0};
        bool IsClosed{false};

        // --- Element counts ---
        std::size_t DegenerateFaceCount{0};
        std::size_t SmallAngleCount{0};
        std::size_t LargeAngleCount{0};

        // --- Topology ---
        std::size_t VertexCount{0};
        std::size_t EdgeCount{0};
        std::size_t FaceCount{0};
        std::size_t BoundaryLoopCount{0};
        int EulerCharacteristic{0};
    };

    // -------------------------------------------------------------------------
    // Compute mesh quality metrics.
    //
    // Returns nullopt if the mesh is empty or has no faces.
    // -------------------------------------------------------------------------
    [[nodiscard]] std::optional<QualityResult> ComputeQuality(
        const Halfedge::Mesh& mesh,
        const QualityParams& params = {});

} // namespace Geometry::MeshQuality
