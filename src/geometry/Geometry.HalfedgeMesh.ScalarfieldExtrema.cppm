// Extracts detached curvature-extremum curve graphs, or ridge/valley graphs of
// any vertex scalar property, for surface inspection; preserves source geometry
// and exposes the evidence needed for later cuts.
module;
#include <array>
#include <cstddef>
#include <cstdint>
#include <glm/glm.hpp>
#include <string_view>
#include <vector>
export module Geometry.HalfedgeMesh.ScalarfieldExtrema;
import Geometry.HalfedgeMesh;
export namespace Geometry::ScalarfieldExtrema
{
    enum class Status : std::uint8_t
    {
        Success,
        EmptyMesh,
        InvalidParameters,
        UnsupportedMesh,
        InvalidGeometry,
        WorkLimit,
        MissingProperty
    };
    enum class Kind : std::uint8_t
    {
        PrincipalRidge,
        PrincipalValley,
        MeanRidge,
        MeanValley,
        SharpEdge,
        ScalarRidge,
        ScalarValley
    };
    inline constexpr std::size_t kKindCount = 7;
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
        std::array<std::size_t, kKindCount> SegmentCounts{};
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
    // Scalar sharpness is a fraction of the field range per squared radius, so
    // the curvature default (1e-4) would admit numerical noise in flat tails.
    inline constexpr Params kScalarDefaults{.MinimumSharpness = 0.01};
    // Ridges (valleys) of the float or double vertex property `vertexProperty`:
    // curves where the field is maximal (minimal) across its dominant Hessian
    // direction, from the same multi-scale quadratic fit as mean-curvature
    // curves. Heights are normalized by the finite field range, so Strength is
    // the crossing's relative height above the minimum (ridge) or below the
    // maximum (valley) in [0, 1] and Sharpness is dimensionless. Non-finite
    // values drop their vertex; no sharp edges are emitted. A missing or
    // non-scalar property fails with MissingProperty.
    [[nodiscard]] Result ExtractScalarExtrema(const HalfedgeMesh::Mesh& mesh,
                                              std::string_view vertexProperty,
                                              const Params& params = kScalarDefaults);
} // namespace Geometry::ScalarfieldExtrema
