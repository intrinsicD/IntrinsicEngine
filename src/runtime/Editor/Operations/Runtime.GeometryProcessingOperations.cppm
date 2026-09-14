// Cross-cutting geometry-processing discovery: which algorithms and element
// domains a selected entity supports, the editor model that projects that for
// panels, and the primitive-selection commands every domain window shares.
// Method execution and results belong to the per-family operation modules.
module;

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

export module Extrinsic.Runtime.GeometryProcessingOperations;
export import Extrinsic.Runtime.EditorProcessing;
export import Extrinsic.Runtime.EditorCommon;
export import Extrinsic.Runtime.SelectionController;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Runtime.EditorJobProjection;
import Extrinsic.Runtime.EditorWorkspaceAttachment;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.JobService;
import Geometry.Properties;

export namespace Extrinsic::Runtime
{
    enum class EditorGeometryProcessingDomain : std::uint32_t
    {
        None = 0,
        MeshVertices = 1u << 0u,
        MeshEdges = 1u << 1u,
        MeshHalfedges = 1u << 2u,
        MeshFaces = 1u << 3u,
        GraphVertices = 1u << 4u,
        GraphEdges = 1u << 5u,
        GraphHalfedges = 1u << 6u,
        PointCloudPoints = 1u << 7u,
    };

    [[nodiscard]] constexpr EditorGeometryProcessingDomain
    operator|(const EditorGeometryProcessingDomain lhs,
              const EditorGeometryProcessingDomain rhs) noexcept
    {
        return static_cast<EditorGeometryProcessingDomain>(static_cast<std::uint32_t>(lhs) |
                                                           static_cast<std::uint32_t>(rhs));
    }

    [[nodiscard]] constexpr EditorGeometryProcessingDomain
    operator&(const EditorGeometryProcessingDomain lhs,
              const EditorGeometryProcessingDomain rhs) noexcept
    {
        return static_cast<EditorGeometryProcessingDomain>(static_cast<std::uint32_t>(lhs) &
                                                           static_cast<std::uint32_t>(rhs));
    }

    constexpr EditorGeometryProcessingDomain&
    operator|=(EditorGeometryProcessingDomain& lhs,
               const EditorGeometryProcessingDomain rhs) noexcept
    {
        lhs = lhs | rhs;
        return lhs;
    }

    [[nodiscard]] constexpr bool
    HasAnyEditorGeometryProcessingDomain(const EditorGeometryProcessingDomain domains,
                                         const EditorGeometryProcessingDomain query) noexcept
    {
        return static_cast<std::uint32_t>(domains & query) != 0u;
    }

    enum class EditorGeometryProcessingAlgorithm : std::uint8_t
    {
        KMeans,
        MeshDenoise,
        Curvature,
        CurvatureSegmentation,
        Remeshing,
        Simplification,
        Smoothing,
        Subdivision,
        Repair,
        NormalEstimation,
        ShortestPath,
        ConvexHull,
        SurfaceReconstruction,
        VectorHeat,
        Parameterization,
        BooleanCSG,
        Registration,
        BilateralFilter,
        OutlierEstimation,
        KernelDensity,
        StatisticalOutlierRemoval,
        RadiusOutlierRemoval,
        ProgressivePoissonSampling,
        Geodesics,
        KnnGraphConstruction,
    };

    struct EditorGeometryProcessingCapabilities
    {
        EditorGeometryProcessingDomain Domains{EditorGeometryProcessingDomain::None};
        bool HasEditableSurfaceMesh{false};

        [[nodiscard]] bool HasAny() const noexcept
        {
            return HasEditableSurfaceMesh || Domains != EditorGeometryProcessingDomain::None;
        }
    };

    struct EditorGeometryProcessingEntry
    {
        EditorGeometryProcessingAlgorithm Algorithm{EditorGeometryProcessingAlgorithm::KMeans};
        EditorGeometryProcessingDomain Domains{EditorGeometryProcessingDomain::None};
    };

    struct EditorGeometryProcessingMenuItem
    {
        EditorGeometryProcessingDomain Domain{EditorGeometryProcessingDomain::None};
        const char* Label{""};
        bool HasNormalsMethod{false};
        bool HasDenoiseMethod{false};
        bool HasCurvatureMethod{false};
        bool HasRemeshMethod{false};
        bool HasSubdivideMethod{false};
        bool HasSimplifyMethod{false};
    };

    [[nodiscard]] std::vector<EditorGeometryProcessingMenuItem>
    GetEditorGeometryProcessingMenuItems(EditorDomainWindowKind kind);

    [[nodiscard]] EditorGeometryProcessingDomain GetEditorSupportedGeometryProcessingDomains(
        EditorGeometryProcessingAlgorithm algorithm) noexcept;

    [[nodiscard]] bool
    SupportsEditorGeometryProcessingDomain(EditorGeometryProcessingAlgorithm algorithm,
                                           EditorGeometryProcessingDomain domain) noexcept;

    [[nodiscard]] EditorGeometryProcessingCapabilities
    GetEditorGeometryProcessingCapabilities(const ECS::Scene::Registry& registry,
                                            ECS::EntityHandle entity);

    [[nodiscard]] std::vector<EditorGeometryProcessingEntry>
    ResolveEditorGeometryProcessingEntries(EditorGeometryProcessingCapabilities capabilities);

    [[nodiscard]] std::vector<EditorGeometryProcessingEntry>
    ResolveEditorGeometryProcessingEntries(const ECS::Scene::Registry& registry,
                                           ECS::EntityHandle entity);

    [[nodiscard]] std::vector<EditorGeometryProcessingDomain>
    GetAvailableEditorKMeansDomains(const ECS::Scene::Registry& registry, ECS::EntityHandle entity);

    [[nodiscard]] const char*
    DebugNameForEditorGeometryProcessingDomain(EditorGeometryProcessingDomain domain) noexcept;

    [[nodiscard]] const char* DebugNameForEditorGeometryProcessingAlgorithm(
        EditorGeometryProcessingAlgorithm algorithm) noexcept;

    // Availability flags are host kernel capability combined with the selected
    // entity's resolved domains; panels disable their action and show the
    // matching reason rather than failing at execution time. Per-method results
    // live in the owning family's prepared frame, not here.
    struct EditorGeometryProcessingModel
    {
        bool HasSelectedEntity{false};
        bool DirectMeshEnrichmentPending{false};
        JobState DirectMeshEnrichmentStatus{JobState::Invalid};
        std::string DirectMeshEnrichmentDiagnostic{};
        EditorGeometryProcessingCapabilities Capabilities{};
        std::vector<EditorGeometryProcessingEntry> Entries{};
        std::vector<EditorGeometryProcessingDomain> KMeansDomains{};
        bool MeshDenoiseAvailable{false};
        bool MeshCurvatureAvailable{false};
        bool MeshCurvatureDirectionsAvailable{false};
        bool CurvatureSegmentationAvailable{false};
        bool MeshRemeshAvailable{false};
        bool MeshRemeshUniformAvailable{false};
        bool MeshRemeshAdaptiveAvailable{false};
        bool MeshRemeshProjectToSurfaceAvailable{false};
        bool MeshRemeshErrorBoundedSizingAvailable{false};
        bool MeshSubdivideAvailable{false};
        bool MeshSubdivideLoopAvailable{false};
        bool MeshSubdivideCatmullClarkAvailable{false};
        bool MeshSubdivideSqrt3Available{false};
        bool MeshSubdivideLoopFeatureEdgesAvailable{false};
        bool MeshSimplifyAvailable{false};
        bool MeshVertexNormalsAvailable{false};
        bool GraphVertexNormalsAvailable{false};
        bool PointCloudVertexNormalsAvailable{false};
        bool ProgressivePoissonAvailable{false};
        std::string ProgressivePoissonDisabledReason{};
        std::vector<EditorDiagnostic> Diagnostics{};
    };

    // Composition entry for hosts that need the shared processing handle without
    // binding a method family first. Family prepared frames carry their own copy.
    [[nodiscard]] EditorProcessingCommands
    PrepareEditorProcessingCommands(const EditorWorkspaceAttachment& attachment);

    [[nodiscard]] Geometry::ConstPropertySet
    ResolveEditorSelectedMeshVertexProperties(const EditorProcessingCommands& commands);

    [[nodiscard]] PrimitiveSelectionSnapshot ReadEditorPrimitiveSelection(
        const EditorProcessingCommands& commands, std::uint32_t entityId,
        GeometryElementDomain domain);
    [[nodiscard]] PrimitiveSelectionSnapshot ApplyEditorPrimitiveSelection(
        const EditorProcessingCommands& commands, std::uint32_t entityId,
        GeometryElementDomain domain, PrimitiveSelectionEdit edit,
        std::span<const std::uint32_t> indices = {});
    [[nodiscard]] SelectionInteractionConfig GetEditorSelectionInteractionConfig(
        const EditorProcessingCommands& commands);
    [[nodiscard]] RuntimeEngineConfigApplyResult ApplyEditorSelectionInteractionConfig(
        const EditorProcessingCommands& commands, const SelectionInteractionConfig& config);
}
