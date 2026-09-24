// Extracts detached ridge/valley curve graphs of any floating-point vertex
// scalar field (Hessian height ridges or gradient-flow watershed separatrices),
// plus the curvature-extremum preset; preserves source geometry and snaps
// selected curves to boolean mesh vertex/edge feature masks.
module;
#include <array>
#include <cstddef>
#include <cstdint>
#include <glm/glm.hpp>
#include <limits>
#include <span>
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
    // Scalar fields support two definitions of a ridge (valley):
    // HessianRidge  - height ridges: the field is maximal (minimal) across its
    //                 dominant Hessian direction, from the multi-scale fit.
    // Watershed     - separatrices of the gradient flow along mesh edges: the
    //                 boundaries between persistence-simplified descending
    //                 (ascending) basins, lying on mesh vertices and edges.
    enum class Method : std::uint8_t
    {
        HessianRidge,
        Watershed
    };
    // Scale index of scale-free segments (sharp edges and watershed curves);
    // their PersistentScaleMask has all three fit scales set.
    inline constexpr std::uint8_t kScaleFree = 3;
    inline constexpr std::uint32_t kInvalidBasin = std::numeric_limits<std::uint32_t>::max();
    struct Params
    {
        Method Algorithm{Method::HessianRidge};
        // Watershed only: basins whose minimum (maximum) lies less than this
        // fraction of the field range below (above) the saddle where they meet
        // an older basin are merged into it.
        double MinimumPersistence{0.05};
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
        // Watershed only: local minima (maxima) before and basins after
        // persistence merging.
        std::size_t Minima{}, Maxima{}, DescendingBasins{}, AscendingBasins{};
    };
    struct Result
    {
        std::vector<Point> Points;
        std::vector<Segment> Segments;
        std::vector<Curve> Curves;
        // Watershed only, vertex-slot aligned: basin of the steepest-descent
        // (ascent) flow; kInvalidBasin for vertices without a finite value.
        // Ridges separate descending basins, valleys ascending ones.
        std::vector<std::uint32_t> DescendingBasin, AscendingBasin;
        Diagnostics Diagnostic;
        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Diagnostic.State == Status::Success;
        }
    };
    [[nodiscard]] const char* ToString(Status state) noexcept;
    [[nodiscard]] const char* ToString(Kind kind) noexcept;
    // Scalar sharpness is a fraction of the field range per squared radius, so
    // the curvature default (1e-4) would admit numerical noise in flat tails.
    inline constexpr Params kScalarDefaults{.MinimumSharpness = 0.01};
    // Ridges (valleys) of a vertex scalar field, selected by params.Algorithm.
    // Heights are normalized by the finite field range, so Strength is the
    // crossing's relative height above the minimum (ridge) or below the maximum
    // (valley) in [0, 1]; Hessian Sharpness is dimensionless, watershed
    // segments report zero sharpness and use kScaleFree. Non-finite values drop
    // their vertex; no sharp edges are emitted. `vertexValues` is aligned to
    // vertex storage slots; a size mismatch fails with MissingProperty, as does
    // a missing or non-float/double `vertexProperty`.
    [[nodiscard]] Result Extract(const HalfedgeMesh::Mesh& mesh,
                                 std::span<const double> vertexValues,
                                 const Params& params = kScalarDefaults);
    [[nodiscard]] Result Extract(const HalfedgeMesh::Mesh& mesh, std::string_view vertexProperty,
                                 const Params& params = kScalarDefaults);
    // Curvature preset: principal ridges/valleys from a multi-scale fitted shape
    // operator S = -dN (normal reversal exchanges ridge/valley roles), mean
    // curvature ridges/valleys and sharp edges. params.Algorithm must be
    // HessianRidge. Failed calls return diagnostics only. Owning triangle
    // meshes only.
    [[nodiscard]] Result ExtractCurvatureExtrema(const HalfedgeMesh::Mesh& mesh,
                                                 const Params& params = {});

    // Mesh-aligned boolean feature masks of selected curve segments. Every
    // point snaps to its source vertex, or to the nearer endpoint of its
    // source edge (ties to the lower index); both points of a segment lie on
    // one triangle, so a segment marks one mesh edge or collapses to a vertex.
    struct MeshFeatures
    {
        std::vector<std::uint8_t> Vertices; // vertex storage slots
        std::vector<std::uint8_t> Edges;    // edge storage slots
        std::size_t VertexCount{}, EdgeCount{};
    };
    [[nodiscard]] MeshFeatures SnapToMesh(const HalfedgeMesh::Mesh& mesh, const Result& result,
                                          std::span<const std::uint32_t> segments);
} // namespace Geometry::ScalarfieldExtrema
