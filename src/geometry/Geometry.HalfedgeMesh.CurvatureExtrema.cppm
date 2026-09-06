// Extracts detached curvature-extremum curve graphs for surface inspection;
// preserves source geometry and exposes the evidence needed for later cuts.
module;
#include <array>
#include <cstddef>
#include <cstdint>
#include <glm/glm.hpp>
#include <vector>
export module Geometry.HalfedgeMesh.CurvatureExtrema;
import Geometry.HalfedgeMesh;
export namespace Geometry::CurvatureExtrema
{
    enum class Status : std::uint8_t
    {
        Success,
        EmptyMesh,
        InvalidParameters,
        UnsupportedMesh,
        InvalidGeometry,
        WorkLimit
    };
    enum class Kind : std::uint8_t
    {
        PrincipalRidge,
        PrincipalValley,
        MeanRidge,
        MeanValley,
        SharpEdge
    };
    struct Params
    {
        // Radii are fractions of the input bounding-box diagonal.
        double RadiusRatio{0.02};
        std::array<double, 3> ScaleFactors{0.5, 1.0, 2.0};
        double MinimumStrength{0.01};    // radius * absolute curvature
        double MinimumSharpness{0.0001}; // radius^3 * transverse second derivative
        double MinimumAnisotropy{0.1};
        double HardDihedralDegrees{45.0};
        std::uint32_t MaximumNeighbors{2048};
        std::uint64_t MaximumWorkItems{80000000};
    };
    struct Point
    {
        // Position is interpolated on the canonical source edge A <= B;
        // A == B identifies a source vertex, otherwise Fraction is in (0, 1).
        glm::vec3 Position{};
        std::uint32_t VertexA{}, VertexB{};
        double Fraction{};
    };
    struct Segment
    {
        std::uint32_t PointA{}, PointB{}, Face{}, Curve{};
        Kind Signal{Kind::PrincipalRidge};
        std::uint8_t Scale{}, PersistentScaleMask{};
        double Strength{}, Sharpness{}, Confidence{}, FitResidual{};
    };
    struct Curve
    {
        Kind Signal{};
        std::uint8_t Scale{};
        std::size_t SegmentCount{}, Endpoints{}, Junctions{};
        double Length{}; // caller-coordinate units
    };
    struct ScaleDiagnostics
    {
        double Radius{};
        std::size_t SupportedVertices{}, SingularTriangles{}, PlateauTriangles{};
        std::array<std::size_t, 4> SegmentCounts{};
    };
    struct Diagnostics
    {
        Status State{Status::EmptyMesh};
        std::uint64_t EdgeVisits{}, MatchCandidateVisits{};
        std::size_t NeighborhoodSamples{}, PeakNeighborhood{}, BoundaryVertices{}, SharpVertices{};
        double BoundingBoxDiagonal{}, TotalMilliseconds{};
        double NeighborhoodMilliseconds{}, FieldMilliseconds{}, TraceMilliseconds{},
            MatchMilliseconds{};
        std::array<ScaleDiagnostics, 3> Scales{};
    };
    struct Result
    {
        std::vector<Point> Points;
        std::vector<Segment> Segments;
        std::vector<Curve> Curves;
        Diagnostics Diagnostic;
        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Diagnostic.State == Status::Success;
        }
    };
    [[nodiscard]] const char* ToString(Status state) noexcept;
    [[nodiscard]] const char* ToString(Kind kind) noexcept;
    // S = -dN fixes the sign convention. Normal reversal exchanges ridge/valley
    // roles. Failed calls return diagnostics only. Owning triangle meshes only.
    [[nodiscard]] Result Extract(const HalfedgeMesh::Mesh& mesh, const Params& params = {});
} // namespace Geometry::CurvatureExtrema
