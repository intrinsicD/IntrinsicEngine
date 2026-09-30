// Cross-cutting geometry-processing discovery: which algorithms and element
// domains a selected entity supports, the editor model that projects that for
// panels, and the primitive-selection commands every domain window shares.
// Method execution and results belong to the per-family operation modules.
module;

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>
#include <glm/vec3.hpp>

export module Extrinsic.Runtime.GeometryProcessingOperations;
export import Extrinsic.Runtime.EditorProcessing;
export import Extrinsic.Runtime.EditorCommon;
export import Extrinsic.Runtime.SelectionController;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.Graphics.GpuPropertyResidency;
import Extrinsic.Runtime.EditorJobProjection;
import Extrinsic.Runtime.EditorWorkspaceAttachment;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.JobService;
import Geometry.Properties;

extern "C++" { namespace Extrinsic::ECS::Scene { class Registry; } }

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

    [[nodiscard]] const char*
    DebugNameForEditorGeometryProcessingDomain(EditorGeometryProcessingDomain domain) noexcept;

    [[nodiscard]] const char* DebugNameForEditorGeometryProcessingAlgorithm(
        EditorGeometryProcessingAlgorithm algorithm) noexcept;

    // Shared selection and discovery metadata. Method admission and results
    // belong to each operation family.
    struct EditorGeometryProcessingModel
    {
        bool HasSelectedEntity{false};
        bool DirectMeshEnrichmentPending{false};
        JobState DirectMeshEnrichmentStatus{JobState::Invalid};
        std::string DirectMeshEnrichmentDiagnostic{};
        EditorGeometryProcessingCapabilities Capabilities{};
        std::vector<EditorGeometryProcessingEntry> Entries{};
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

    // ---- Accept of GPU-authored positions (RUNTIME-293, ADR 0030 decision 6) ------------
    // A GPU method that writes an entity's `v:position` ring begins a run before its first
    // write (the capture: every row's value, the live rows, the positions' revision) and
    // keeps the handle in its job state. When the user accepts (or at once for batch and
    // agent commands) Accept reads the ring front back once, publishes every row through
    // one undoable history entry, lets the render side keep the copied front for 1:1 domains
    // (point clouds, graphs: the GpuWorld shadow is patched, extraction acknowledges the
    // revision, nothing is uploaded again) and binds the front as the canonical slot of the
    // new revision. Meshes commit through the ordinary revision-delta upload. The result is
    // delivered once the CPU publication ran; "Applied" is never reported before. A run whose
    // positions or entity changed meanwhile is stale: Accept is refused and the caller
    // discards the run. Discard (`DiscardEditorGpuPositionRun`) abandons the run, so a readback
    // still in flight publishes nothing, and releases the ring the run acquired at Begin (a
    // terminal run discards nothing: a later run's ring on the same property is its own);
    // the residency's own Discard is not the contract. Authored culling bounds move with
    // the live rows in the same history entry: the local bounds are history state and the
    // world bounds are derived from the entity's world matrix at every mutation; rows that
    // admit no finite bounds are refused before anything is written.
    struct EditorGpuPositionRun;
    using EditorGpuPositionRunHandle = std::shared_ptr<EditorGpuPositionRun>;
    struct EditorGpuPositionAcceptResult
    {
        EditorCommandStatus Status{EditorCommandStatus::Pending};
        std::string Message{};
        // The block kept the front: no position upload follows the publication.
        bool RenderAcknowledged{};
    };
    // Creates the positions' ring with its first write slot (depth 2) and owns it. Null with
    // `diagnostic` when the positions do not resolve on a live entity, a ring already waits
    // for Accept or Discard, or the residency refuses a slot.
    [[nodiscard]] EditorGpuPositionRunHandle BeginEditorGpuPositionRun(
        const EditorProcessingCommands&, std::uint32_t stableEntityId, GeometryPropertyRef positions,
        Graphics::GpuPropertyResidency&, std::string& diagnostic);
    // The write slot Begin acquired, handed out once (later slots come from
    // `AcquireGpuPropertyOutput` as usual); the method fills it and publishes.
    [[nodiscard]] std::optional<Graphics::GpuPropertyView> EditorGpuPositionRunFirstBack(const EditorGpuPositionRunHandle&);
    [[nodiscard]] Graphics::GpuPropertyKey EditorGpuPositionRunKey(const EditorGpuPositionRunHandle&);
    [[nodiscard]] std::uint32_t EditorGpuPositionRunRowCount(const EditorGpuPositionRunHandle&);
    // False once the captured inputs changed (Accept would be refused as stale).
    [[nodiscard]] bool EditorGpuPositionRunCurrent(const EditorProcessingCommands&, const EditorGpuPositionRunHandle&);
    // Pending while the readback and publication are under way; `onComplete` receives the
    // final result. `frontForTest` (every row) replaces the device readback on a null device.
    [[nodiscard]] EditorGpuPositionAcceptResult AcceptEditorGpuPositionRun(
        const EditorProcessingCommands&, const EditorGpuPositionRunHandle&, Graphics::GpuPropertyResidency&,
        std::string label, std::function<void(EditorGpuPositionAcceptResult)> onComplete,
        std::span<const glm::vec3> frontForTest = {});
    void DiscardEditorGpuPositionRun(const EditorProcessingCommands&, const EditorGpuPositionRunHandle&,
                                     Graphics::GpuPropertyResidency&);
}
