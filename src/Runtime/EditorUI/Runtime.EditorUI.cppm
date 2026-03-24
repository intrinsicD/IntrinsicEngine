module;

#include <cstdint>
#include <functional>
#include <vector>
#include <glm/glm.hpp>
#include <entt/entity/entity.hpp>
#include <entt/entity/registry.hpp>

export module Runtime.EditorUI;

import Runtime.Engine;
import Runtime.PointCloudKMeans;
import Runtime.SceneSerializer;

import Graphics.Components;
import Graphics.BVHDebugDraw;
import Graphics.BoundingDebugDraw;
import Graphics.Colormap;
import Graphics.ConvexHullDebugDraw;
import Graphics.DebugDraw;
import Graphics.Geometry;
import Graphics.KDTreeDebugDraw;
import Graphics.OctreeDebugDraw;
import Graphics.VisualizationConfig;

import Geometry.Handle;
import Geometry.KMeans;
import Geometry.Properties;
import Geometry.Simplification;
import Geometry.Subdivision;
import Geometry.PointCloud;
import Geometry.GraphUtils;
import Geometry.HalfedgeMesh;
import Geometry.KDTree;
import Geometry.AABB;
import Geometry.OBB;

import ECS;

export namespace Runtime::EditorUI
{
    enum class GeometryProcessingDomain : uint32_t
    {
        None = 0u,
        MeshVertices = 1u << 0,
        MeshEdges = 1u << 1,
        MeshHalfedges = 1u << 2,
        MeshFaces = 1u << 3,
        GraphVertices = 1u << 4,
        GraphEdges = 1u << 5,
        GraphHalfedges = 1u << 6,
        PointCloudPoints = 1u << 7,
    };

    [[nodiscard]] constexpr GeometryProcessingDomain operator|(GeometryProcessingDomain a,
                                                               GeometryProcessingDomain b) noexcept
    {
        return static_cast<GeometryProcessingDomain>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
    }

    [[nodiscard]] constexpr GeometryProcessingDomain operator&(GeometryProcessingDomain a,
                                                               GeometryProcessingDomain b) noexcept
    {
        return static_cast<GeometryProcessingDomain>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
    }

    constexpr GeometryProcessingDomain& operator|=(GeometryProcessingDomain& a,
                                                   GeometryProcessingDomain b) noexcept
    {
        a = (a | b);
        return a;
    }

    [[nodiscard]] constexpr bool HasAnyDomain(GeometryProcessingDomain domains,
                                              GeometryProcessingDomain query) noexcept
    {
        return static_cast<uint32_t>(domains & query) != 0u;
    }

    enum class GeometryProcessingAlgorithm : uint8_t
    {
        KMeans = 0,
        Remeshing,
        Simplification,
        Smoothing,
        Subdivision,
        Repair,
    };

    struct GeometryProcessingCapabilities
    {
        GeometryProcessingDomain Domains = GeometryProcessingDomain::None;
        bool HasEditableSurfaceMesh = false;

        [[nodiscard]] bool HasAny() const noexcept
        {
            return HasEditableSurfaceMesh || Domains != GeometryProcessingDomain::None;
        }
    };

    struct GeometryProcessingEntry
    {
        GeometryProcessingAlgorithm Algorithm = GeometryProcessingAlgorithm::KMeans;
        GeometryProcessingDomain Domains = GeometryProcessingDomain::None;
    };

    [[nodiscard]] GeometryProcessingCapabilities GetGeometryProcessingCapabilities(
        const entt::registry& registry,
        entt::entity entity);

    [[nodiscard]] GeometryProcessingDomain GetSupportedDomains(GeometryProcessingAlgorithm algorithm) noexcept;
    [[nodiscard]] bool SupportsDomain(GeometryProcessingAlgorithm algorithm,
                                      GeometryProcessingDomain domain) noexcept;
    [[nodiscard]] std::vector<GeometryProcessingEntry> ResolveGeometryProcessingEntries(
        const entt::registry& registry,
        entt::entity entity);
    [[nodiscard]] std::vector<Runtime::PointCloudKMeans::Domain> GetAvailableKMeansDomains(
        const entt::registry& registry,
        entt::entity entity);
    [[nodiscard]] const char* GeometryDomainLabel(GeometryProcessingDomain domain) noexcept;
    [[nodiscard]] const char* GeometryProcessingAlgorithmLabel(GeometryProcessingAlgorithm algorithm) noexcept;

    // Registers a small set of editor-facing panels and menus to improve
    // discoverability of core engine features (FeatureRegistry, FrameGraph,
    // Selection config).
    //
    // Contract:
    //  - Call once after Engine startup, before the first frame.
    //  - Safe to call multiple times; panels are de-duplicated by name.
    void RegisterDefaultPanels(Engine& engine);

    // Draw DebugDraw overlays for sub-element selection (selected vertices as
    // spheres, selected edges as highlighted lines, selected faces as tinted triangles).
    // Call once per frame from OnUpdate, after selection and before rendering.
    void DrawSubElementHighlights(Engine& engine);

    // Access the shared dirty tracker for scene save/load state.
    // The tracker records whether unsaved changes exist and the path
    // of the current scene file (if any).
    [[nodiscard]] SceneDirtyTracker& GetSceneDirtyTracker();

    // =========================================================================
    // Editor utility types
    // =========================================================================

    // Tag component to mark editor-internal visualization entities that should
    // be hidden from the scene hierarchy panel.
    struct HiddenEditorEntityTag {};

    // Pairs an entity with a geometry handle for retained line overlays.
    struct RetainedLineOverlaySlot
    {
        entt::entity Entity = entt::null;
        Geometry::GeometryHandle Geometry{};
    };

    // =========================================================================
    // Reusable ImGui widgets for PropertySet-driven visualization
    // =========================================================================

    // Color source selector: property combo + colormap + range + binning.
    // Returns true if any value changed.
    bool ColorSourceWidget(const char* label, Graphics::ColorSource& src,
                           const Geometry::ConstPropertySet* ps, const char* suffix);

    // Vector field overlay manager: add/remove/configure vector field entries.
    // Returns true if any value changed.
    bool VectorFieldWidget(Graphics::VisualizationConfig& config,
                           const Geometry::ConstPropertySet* ps, const char* suffix);

    // Point render mode combo (FlatDisc/Surfel/EWA/Sphere). Returns true on change.
    bool PointRenderModeCombo(const char* label,
                              Geometry::PointCloud::RenderMode& mode);

    // ImGui ColorEdit4 that reads/writes a glm::vec4 directly. Returns true on change.
    bool ColorEdit4(const char* label, glm::vec4& color);

    struct PropertySetBrowserState
    {
        int SelectedProperty = 0;
        int PreviewRows = 8;
        bool ShowIndices = true;
        bool ShowAllRows = false;
    };

    bool DrawPropertySetBrowserWidget(const char* label,
                                      const Geometry::ConstPropertySet* ps,
                                      PropertySetBrowserState& state,
                                      const char* suffix);

    void DrawDomainBadges(GeometryProcessingDomain domains);

    struct MeshSpectralWidgetState
    {
        int ModeCount = 2;
        int MaxIterations = 24;
        float Shift = 1.0f;
        float SolverTolerance = 1.0e-6f;
        bool NormalizePublishedModes = true;
        char Mode0Property[64] = "v:spectral_mode_0";
        char Mode1Property[64] = "v:spectral_mode_1";
        std::size_t LastActiveVertices = 0;
        std::uint32_t LastIterations = 0;
        bool LastConverged = false;
        double LastEigenvalue0 = 0.0;
        double LastEigenvalue1 = 0.0;
        double LastResidual0 = 0.0;
        double LastResidual1 = 0.0;
    };

    struct GraphSpectralWidgetState
    {
        int Variant = static_cast<int>(Geometry::Graph::SpectralLayoutParams::LaplacianVariant::NormalizedSymmetric);
        int MaxIterations = 96;
        float StepScale = 0.85f;
        float ConvergenceTolerance = 1.0e-5f;
        float MinNormEpsilon = 1.0e-8f;
        float AreaExtent = 2.0f;
        bool PreserveExistingZ = true;
        float OutputZ = 0.0f;
        char UProperty[64] = "v:spectral_u";
        char VProperty[64] = "v:spectral_v";
        char RadiusProperty[64] = "v:spectral_radius";
        std::size_t LastActiveVertices = 0;
        std::size_t LastActiveEdges = 0;
        std::uint32_t LastIterations = 0;
        float LastSubspaceDelta = 0.0f;
        bool LastConverged = false;
        bool LastCrossingCountValid = false;
        std::size_t LastCrossingCount = 0;
    };

    struct KMeansWidgetState
    {
        int ClusterCount = 8;
        int MaxIterations = 32;
        int Backend = static_cast<int>(Geometry::KMeans::Backend::CPU);
        int Initialization = static_cast<int>(Geometry::KMeans::Initialization::Hierarchical);
        int Seed = 42;
        Runtime::PointCloudKMeans::Domain SelectedDomain = Runtime::PointCloudKMeans::Domain::Auto;
    };

    struct RemeshingWidgetState
    {
        float TargetLength = 0.05f;
        int Iterations = 5;
        bool PreserveBoundary = true;
    };

    struct SimplificationWidgetState
    {
        int TargetFaces = 1000;
        bool PreserveBoundary = true;
        float HausdorffError = 0.0f;
        float MaxNormalDeviationDeg = 0.0f;
        int QuadricType = static_cast<int>(Geometry::Simplification::QuadricType::Plane);
        int ProbabilisticMode = static_cast<int>(Geometry::Simplification::QuadricProbabilisticMode::Deterministic);
        int Residence = static_cast<int>(Geometry::Simplification::QuadricResidence::Vertices);
        int PlacementPolicy = static_cast<int>(Geometry::Simplification::CollapsePlacementPolicy::KeepSurvivor);
        bool AverageVertexQuadrics = true;
        bool AverageFaceQuadrics = false;
        float PositionStdDev = 0.0f;
        float NormalStdDev = 0.0f;
        char VertexPositionCovarianceProperty[64] = "v:quadric_sigma_p";
        char FacePositionCovarianceProperty[64] = "f:quadric_sigma_p";
        char FaceNormalCovarianceProperty[64] = "f:quadric_sigma_n";
    };

    struct SmoothingWidgetState
    {
        int Iterations = 10;
        float Lambda = 0.5f;
        bool PreserveBoundary = true;
    };

    struct SubdivisionWidgetState
    {
        int Iterations = 1;
    };

    struct MeshAnalysisWidgetState
    {
        bool HasResults = false;
        std::size_t ProblemVertices = 0;
        std::size_t ProblemEdges = 0;
        std::size_t ProblemHalfedges = 0;
        std::size_t ProblemFaces = 0;
        std::size_t BoundaryVertices = 0;
        std::size_t BoundaryEdges = 0;
        std::size_t BoundaryFaces = 0;
        std::size_t NonManifoldVertices = 0;
        std::size_t DegenerateFaces = 0;
        std::size_t NonTriangleFaces = 0;
        std::size_t NonFiniteElements = 0;
    };

    [[nodiscard]] bool DrawKMeansWidget(Runtime::Engine& engine,
                                        entt::entity entity,
                                        KMeansWidgetState& state);
    [[nodiscard]] bool DrawMeshSpectralWidget(Runtime::Engine& engine,
                                              entt::entity entity,
                                              MeshSpectralWidgetState& state);
    [[nodiscard]] bool DrawGraphSpectralWidget(Runtime::Engine& engine,
                                               entt::entity entity,
                                               GraphSpectralWidgetState& state);
    [[nodiscard]] bool DrawRemeshingWidget(Runtime::Engine& engine,
                                           entt::entity entity,
                                           RemeshingWidgetState& state);
    [[nodiscard]] bool DrawSimplificationWidget(Runtime::Engine& engine,
                                                entt::entity entity,
                                                SimplificationWidgetState& state);
    [[nodiscard]] bool DrawSmoothingWidget(Runtime::Engine& engine,
                                           entt::entity entity,
                                           SmoothingWidgetState& state);
    [[nodiscard]] bool DrawSubdivisionWidget(Runtime::Engine& engine,
                                             entt::entity entity,
                                             SubdivisionWidgetState& state);
    [[nodiscard]] bool DrawRepairWidget(Runtime::Engine& engine,
                                        entt::entity entity);

    // =========================================================================
    // InspectorController — component property inspector panel
    // =========================================================================

    class GeometryWorkflowController;

    class InspectorController
    {
    public:
        InspectorController() = default;
        InspectorController(const InspectorController&) = delete;
        InspectorController& operator=(const InspectorController&) = delete;

        void Init(Runtime::Engine& engine,
                  entt::entity& cachedSelected,
                  GeometryWorkflowController* geometryWorkflow = nullptr);
        void Draw();

    private:
        Runtime::Engine* m_Engine = nullptr;
        entt::entity* m_CachedSelected = nullptr;
        GeometryWorkflowController* m_GeometryWorkflow = nullptr;
        PropertySetBrowserState m_MeshVertexPropertiesUi{};
        PropertySetBrowserState m_MeshEdgePropertiesUi{};
        PropertySetBrowserState m_MeshHalfedgePropertiesUi{};
        PropertySetBrowserState m_MeshFacePropertiesUi{};
        PropertySetBrowserState m_GraphVertexPropertiesUi{};
        PropertySetBrowserState m_GraphEdgePropertiesUi{};
        PropertySetBrowserState m_GraphHalfedgePropertiesUi{};
        PropertySetBrowserState m_PointCloudPropertiesUi{};
        MeshSpectralWidgetState m_MeshSpectralUi{};
        GraphSpectralWidgetState m_GraphSpectralUi{};
        KMeansWidgetState m_KMeansUi{};
        RemeshingWidgetState m_RemeshingUi{};
        SimplificationWidgetState m_SimplificationUi{};
        SmoothingWidgetState m_SmoothingUi{};
        SubdivisionWidgetState m_SubdivisionUi{};
        MeshAnalysisWidgetState m_MeshAnalysisUi{};
    };

    // =========================================================================
    // GeometryWorkflowController — geometry operator UI and pipeline
    // =========================================================================

    class GeometryWorkflowController
    {
    public:
        GeometryWorkflowController() = default;
        GeometryWorkflowController(const GeometryWorkflowController&) = delete;
        GeometryWorkflowController& operator=(const GeometryWorkflowController&) = delete;

        void Init(Runtime::Engine& engine, entt::entity& cachedSelected);
        void RegisterPanelsAndMenu();
        void OpenAlgorithmPanel(GeometryProcessingAlgorithm algorithm);
        void OpenMeshSpectralPanel();
        void OpenGraphSpectralPanel();

    private:
        Runtime::Engine* m_Engine = nullptr;
        entt::entity* m_CachedSelected = nullptr;
        MeshSpectralWidgetState m_MeshSpectralUi{};
        GraphSpectralWidgetState m_GraphSpectralUi{};
        RemeshingWidgetState m_RemeshingUi{};
        SimplificationWidgetState m_SimplificationUi{};
        SmoothingWidgetState m_SmoothingUi{};
        SubdivisionWidgetState m_SubdivisionUi{};

        struct SelectionContext
        {
            entt::entity Selected = entt::null;
            bool HasSelection = false;
            bool HasSurface = false;
            bool HasGraph = false;
        };

        [[nodiscard]] SelectionContext GetSelectionContext() const;
        [[nodiscard]] static bool DrawOperatorPanelHeader(const SelectionContext& context,
                                                          const char* description);

        void OpenWorkflowPanel();
        void OpenRemeshingPanel();
        void OpenSimplificationPanel();
        void OpenSmoothingPanel();
        void OpenSubdivisionPanel();
        void OpenRepairPanel();
        void OpenWorkflowStack();


        void DrawMenu();
        void DrawWorkflowPanel();
        void DrawRemeshingPanel();
        void DrawSimplificationPanel();
        void DrawSmoothingPanel();
        void DrawSubdivisionPanel();
        void DrawRepairPanel();
        void DrawMeshSpectralPanel();
        void DrawGraphSpectralPanel();
    };

    // =========================================================================
    // SpatialDebugController — spatial structure debug visualization
    // =========================================================================

    class SpatialDebugController
    {
    public:
        // Toggle flags (bound to UI checkboxes).
        bool DrawOctree = false;
        bool DrawBounds = false;
        bool DrawKDTree = false;
        bool DrawBVH = false;
        bool DrawConvexHull = false;
        bool DrawContacts = false;

        // Per-structure debug settings.
        Graphics::OctreeDebugDrawSettings OctreeSettings{};
        Graphics::BoundingDebugDrawSettings BoundsSettings{};
        Graphics::KDTreeDebugDrawSettings KDTreeSettings{};
        Graphics::BVHDebugDrawSettings BVHSettings{};
        Graphics::ConvexHullDebugDrawSettings ConvexHullSettings{};

        // Contact manifold settings.
        bool ContactOverlay = true;
        float ContactNormalScale = 0.3f;
        float ContactPointRadius = 0.03f;

        [[nodiscard]] bool AnyActive() const;

        void Update(Runtime::Engine& engine, entt::entity selected);
        void ReleaseAll(Runtime::Engine& engine);
        void DrawUI(Runtime::Engine& engine);

    private:
        // Octree overlay (specialized upload).
        entt::entity m_OctreeOverlayEntity = entt::null;
        Geometry::GeometryHandle m_OctreeOverlayGeometry{};
        entt::entity m_OctreeOverlaySourceEntity = entt::null;
        Graphics::OctreeDebugDrawSettings m_CachedOctreeSettings{};
        glm::mat4 m_CachedOctreeWorld{1.0f};
        Geometry::AABB m_CachedOctreeLocalAABB{};
        bool m_HasCachedOctreeAabb = false;

        // Bounds overlay.
        RetainedLineOverlaySlot m_BoundsOverlay{};
        entt::entity m_BoundsOverlaySourceEntity = entt::null;
        Graphics::BoundingDebugDrawSettings m_CachedBoundsSettings{};
        glm::mat4 m_CachedBoundsWorld{1.0f};
        Geometry::AABB m_CachedBoundsLocalAabb{};
        bool m_HasCachedBoundsAabb = false;

        // KD-Tree overlay.
        RetainedLineOverlaySlot m_KDTreeOverlay{};
        entt::entity m_KDTreeOverlaySourceEntity = entt::null;
        Graphics::KDTreeDebugDrawSettings m_CachedKDTreeSettings{};
        glm::mat4 m_CachedKDTreeWorld{1.0f};
        Geometry::KDTree m_SelectedColliderKDTree{};
        entt::entity m_SelectedKDTreeEntity = entt::null;

        // BVH overlay.
        RetainedLineOverlaySlot m_BVHOverlay{};
        entt::entity m_BVHOverlaySourceEntity = entt::null;
        Graphics::BVHDebugDrawSettings m_CachedBVHSettings{};
        glm::mat4 m_CachedBVHWorld{1.0f};
        size_t m_CachedBVHPositionCount = 0;
        size_t m_CachedBVHIndexCount = 0;

        // Convex Hull overlay.
        RetainedLineOverlaySlot m_ConvexHullOverlay{};
        entt::entity m_ConvexHullOverlaySourceEntity = entt::null;
        Graphics::ConvexHullDebugDrawSettings m_CachedConvexHullSettings{};
        glm::mat4 m_CachedConvexHullWorld{1.0f};
        Geometry::Halfedge::Mesh m_SelectedColliderHullMesh{};
        entt::entity m_SelectedHullEntity = entt::null;

        // Contact manifold overlay (transient, released every frame).
        RetainedLineOverlaySlot m_ContactOverlay{};

        // Shared overlay lifecycle helpers.
        void ReleaseRetainedLineOverlay(Runtime::Engine& engine, RetainedLineOverlaySlot& slot);
        bool UpdateRetainedLineOverlay(Runtime::Engine& engine, RetainedLineOverlaySlot& slot,
                                       const std::function<void(Graphics::DebugDraw&)>& emit);

        // Cached collider helpers.
        bool EnsureSelectedColliderKDTree(entt::entity selected,
                                          const Graphics::GeometryCollisionData& collision);
        bool EnsureSelectedColliderConvexHull(entt::entity selected,
                                              const Graphics::GeometryCollisionData& collision);

        // Per-structure overlay management.
        void ReleaseCachedOctreeOverlay(Runtime::Engine& engine);
        bool EnsureRetainedOctreeOverlay(Runtime::Engine& engine, entt::entity selected,
                                         const Graphics::GeometryCollisionData& collision,
                                         const ECS::Components::Transform::Component& xf);
        bool EnsureRetainedBoundsOverlay(Runtime::Engine& engine, entt::entity selected,
                                         const Geometry::AABB& localAabb,
                                         const Geometry::OBB& worldObb,
                                         const ECS::Components::Transform::Component& xf);
        bool EnsureRetainedKDTreeOverlay(Runtime::Engine& engine, entt::entity selected,
                                         const Graphics::GeometryCollisionData& collision,
                                         const ECS::Components::Transform::Component& xf);
        bool EnsureRetainedBVHOverlay(Runtime::Engine& engine, entt::entity selected,
                                      const Graphics::GeometryCollisionData& collision,
                                      const ECS::Components::Transform::Component& xf);
        bool EnsureRetainedConvexHullOverlay(Runtime::Engine& engine, entt::entity selected,
                                             const Graphics::GeometryCollisionData& collision,
                                             const ECS::Components::Transform::Component& xf);
        void EmitContactManifolds(Runtime::Engine& engine, entt::entity selected,
                                  const ECS::MeshCollider::Component& selectedCollider);
    };

    // =========================================================================
    // Utility functions for spatial debug overlays
    // =========================================================================

    constexpr std::size_t kMaxOctreeTraversalStack = 512;

    [[nodiscard]] bool MatricesNearlyEqual(const glm::mat4& a, const glm::mat4& b, float eps = 1e-4f);
    [[nodiscard]] bool Vec3NearlyEqual(const glm::vec3& a, const glm::vec3& b, float eps = 1e-4f);
    [[nodiscard]] bool OctreeSettingsEqual(const Graphics::OctreeDebugDrawSettings& a,
                                           const Graphics::OctreeDebugDrawSettings& b);
    [[nodiscard]] glm::vec3 DepthRamp(float t);
    [[nodiscard]] uint32_t PackWithAlpha(const glm::vec3& rgb, float alpha);
    void TransformAABB(const glm::vec3& lo, const glm::vec3& hi, const glm::mat4& m,
                       glm::vec3& outLo, glm::vec3& outHi);
}
