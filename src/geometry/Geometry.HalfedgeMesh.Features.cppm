module;

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

export module Geometry.HalfedgeMesh.Features;

import Geometry.HalfedgeMesh;
import Geometry.Properties;

export namespace Geometry::HalfedgeMesh::Features
{
    inline constexpr std::string_view kDefaultEdgeFeatureProperty = "e:feature";

    enum class ClassificationStatus : std::uint8_t
    {
        Success,
        UnsupportedSubmeshView,
        InvalidDihedralThreshold,
        FaceNormalSlotCountMismatch,
    };

    // The categories preserve the one-edge endpoint case instead of folding it
    // into either the smooth or crease cases. Consumers decide what each fact
    // means for their own stencil or topology policy.
    enum class VertexIncidenceCategory : std::uint8_t
    {
        None,
        Endpoint,
        Crease,
        Junction,
    };

    struct Params
    {
        bool BoundaryIsFeature{true};

        // A live interior edge is sharp only when its unsigned normal angle is
        // strictly greater than this threshold. Must be finite and in [0, 180].
        double DihedralThresholdDegrees{45.0};
    };

    struct Classification
    {
        ClassificationStatus Status{ClassificationStatus::Success};

        // Snapshots indexed by the mesh's existing storage slots. Deleted edge
        // slots are zero. The result owns these arrays and retains no mesh or
        // normal borrow.
        std::vector<std::uint8_t> EdgeFeatureMask{};
        std::vector<std::size_t> IncidentFeatureEdgeCount{};
        std::vector<VertexIncidenceCategory> VertexIncidence{};

        std::size_t FeatureEdgeCount{0};
        std::size_t BoundaryFeatureEdgeCount{0};
        std::size_t InvalidNormalFeatureEdgeCount{0};
        std::size_t InvalidTopologyFeatureEdgeCount{0};
        std::size_t DeletedEdgeSlotCount{0};
    };

    // Full owning meshes only; submesh views are rejected because their
    // handle-based connectivity uses absolute backing-storage indices.
    // `faceNormals` is borrowed only for the duration of the call and must have
    // exactly mesh.FacesSize() entries. Each usable finite nonzero normal is
    // normalized with component scaling, so classification is independent of
    // every representable finite magnitude.
    // A live edge with missing/dangling adjacency or an unusable adjacent
    // normal is classified as a feature (fail closed).
    [[nodiscard]] Classification Classify(
        const Mesh& mesh,
        std::span<const glm::dvec3> faceNormals,
        const Params& params = {});

    // Evolving-topology legality predicate. Unlike the snapshot classifier,
    // submesh views, invalid/deleted edges, and invalid params also return true
    // so a caller cannot accidentally permit a collapse through an unsafe edge.
    [[nodiscard]] bool IsFeatureEdgeFailClosed(
        const Mesh& mesh,
        EdgeHandle edge,
        std::span<const glm::dvec3> faceNormals,
        const Params& params = {}) noexcept;

    // Materialize a validated snapshot into a caller-named bool edge property.
    // Returns nullopt for a submesh view, invalid/incompatible snapshot, empty
    // name, or a property type collision. Existing slots are overwritten exactly.
    [[nodiscard]] std::optional<EdgeProperty<bool>> MaterializeEdgeProperty(
        Mesh& mesh,
        const Classification& classification,
        std::string_view propertyName = kDefaultEdgeFeatureProperty);
} // namespace Geometry::HalfedgeMesh::Features
