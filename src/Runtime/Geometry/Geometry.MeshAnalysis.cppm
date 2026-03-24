module;

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

export module Geometry.MeshAnalysis;

import Geometry.HalfedgeMesh;
import Geometry.Properties;

export namespace Geometry::MeshAnalysis
{
    // Public property names so callers/editor tooling can discover and color
    // problem regions without hard-coding strings.
    inline constexpr const char* kVertexProblemPropertyName = "v:analysis_problem";
    inline constexpr const char* kEdgeProblemPropertyName = "e:analysis_problem";
    inline constexpr const char* kHalfedgeProblemPropertyName = "h:analysis_problem";
    inline constexpr const char* kFaceProblemPropertyName = "f:analysis_problem";

    inline constexpr const char* kVertexIssueMaskPropertyName = "v:analysis_issue_mask";
    inline constexpr const char* kEdgeIssueMaskPropertyName = "e:analysis_issue_mask";
    inline constexpr const char* kHalfedgeIssueMaskPropertyName = "h:analysis_issue_mask";
    inline constexpr const char* kFaceIssueMaskPropertyName = "f:analysis_issue_mask";

    // Per-domain issue masks. The boolean marker properties are the broad
    // selectable/visible lane; the masks are the compact defect taxonomy.
    inline constexpr std::uint32_t kVertexIssueIsolated = 1u << 0;
    inline constexpr std::uint32_t kVertexIssueBoundary = 1u << 1;
    inline constexpr std::uint32_t kVertexIssueNonManifold = 1u << 2;
    inline constexpr std::uint32_t kVertexIssueNonFinitePosition = 1u << 3;

    inline constexpr std::uint32_t kEdgeIssueBoundary = 1u << 0;
    inline constexpr std::uint32_t kEdgeIssueZeroLength = 1u << 1;
    inline constexpr std::uint32_t kEdgeIssueNonFiniteGeometry = 1u << 2;

    inline constexpr std::uint32_t kHalfedgeIssueBoundary = 1u << 0;
    inline constexpr std::uint32_t kHalfedgeIssueFaceProblem = 1u << 1;
    inline constexpr std::uint32_t kHalfedgeIssueNonFiniteGeometry = 1u << 2;

    inline constexpr std::uint32_t kFaceIssueBoundary = 1u << 0;
    inline constexpr std::uint32_t kFaceIssueNonTriangle = 1u << 1;
    inline constexpr std::uint32_t kFaceIssueDegenerateArea = 1u << 2;
    inline constexpr std::uint32_t kFaceIssueSkinny = 1u << 3;
    inline constexpr std::uint32_t kFaceIssueNonFiniteGeometry = 1u << 4;

    struct AnalysisParams
    {
        // Geometric thresholds are intentionally conservative; callers can tune
        // them when they want to bias the analysis toward only hard failures.
        double ZeroLengthEdgeEpsilon{1.0e-12};
        double DegenerateFaceAreaEpsilon{1.0e-12};
        double SmallAngleThresholdDegrees{10.0};
        double LargeAngleThresholdDegrees{170.0};
        double MaxAspectRatio{8.0};
    };

    struct AnalysisResult
    {
        VertexProperty<bool> ProblemVertex{};
        EdgeProperty<bool> ProblemEdge{};
        HalfedgeProperty<bool> ProblemHalfedge{};
        FaceProperty<bool> ProblemFace{};

        VertexProperty<std::uint32_t> VertexIssueMask{};
        EdgeProperty<std::uint32_t> EdgeIssueMask{};
        HalfedgeProperty<std::uint32_t> HalfedgeIssueMask{};
        FaceProperty<std::uint32_t> FaceIssueMask{};

        std::vector<VertexHandle> ProblemVertices{};
        std::vector<EdgeHandle> ProblemEdges{};
        std::vector<HalfedgeHandle> ProblemHalfedges{};
        std::vector<FaceHandle> ProblemFaces{};

        std::size_t IsolatedVertexCount{0};
        std::size_t BoundaryVertexCount{0};
        std::size_t NonManifoldVertexCount{0};
        std::size_t NonFiniteVertexCount{0};

        std::size_t BoundaryEdgeCount{0};
        std::size_t ZeroLengthEdgeCount{0};
        std::size_t NonFiniteEdgeCount{0};

        std::size_t BoundaryHalfedgeCount{0};

        std::size_t BoundaryFaceCount{0};
        std::size_t NonTriangleFaceCount{0};
        std::size_t DegenerateFaceCount{0};
        std::size_t SkinnyFaceCount{0};
        std::size_t NonFiniteFaceCount{0};
    };

    [[nodiscard]] std::optional<AnalysisResult> Analyze(
        Halfedge::Mesh& mesh,
        const AnalysisParams& params = {});
} // namespace Geometry::MeshAnalysis


