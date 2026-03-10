module;

#include <functional>
#include <vector>
#include <glm/glm.hpp>
#include <entt/entity/entity.hpp>
#include <entt/entity/registry.hpp>

export module Runtime.EditorUI;

import Runtime.Engine;
import Runtime.SceneSerializer;
import Graphics;
import Geometry;
import ECS;

export namespace Runtime::EditorUI
{
    // Registers a small set of editor-facing panels and menus to improve
    // discoverability of core engine features (FeatureRegistry, FrameGraph,
    // Selection config).
    //
    // Contract:
    //  - Call once after Engine startup, before the first frame.
    //  - Safe to call multiple times; panels are de-duplicated by name.
    void RegisterDefaultPanels(Engine& engine);

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
                           const Geometry::PropertySet* ps, const char* suffix);

    // Vector field overlay manager: add/remove/configure vector field entries.
    // Returns true if any value changed.
    bool VectorFieldWidget(Graphics::VisualizationConfig& config,
                           const Geometry::PropertySet* ps, const char* suffix);

    // =========================================================================
    // InspectorController — component property inspector panel
    // =========================================================================

    class InspectorController
    {
    public:
        InspectorController() = default;
        InspectorController(const InspectorController&) = delete;
        InspectorController& operator=(const InspectorController&) = delete;

        void Init(Runtime::Engine& engine, entt::entity& cachedSelected);
        void Draw();

    private:
        Runtime::Engine* m_Engine = nullptr;
        entt::entity* m_CachedSelected = nullptr;
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

    private:
        Runtime::Engine* m_Engine = nullptr;
        entt::entity* m_CachedSelected = nullptr;

        struct RemeshingUiState
        {
            float TargetLength = 0.05f;
            int Iterations = 5;
            bool PreserveBoundary = true;
        };

        struct SimplificationUiState
        {
            int TargetFaces = 1000;
            bool PreserveBoundary = true;
        };

        struct SmoothingUiState
        {
            int Iterations = 10;
            float Lambda = 0.5f;
            bool PreserveBoundary = true;
        };

        struct SubdivisionUiState
        {
            int Iterations = 1;
        };

        RemeshingUiState m_RemeshingUi{};
        SimplificationUiState m_SimplificationUi{};
        SmoothingUiState m_SmoothingUi{};
        SubdivisionUiState m_SubdivisionUi{};

        struct SelectionContext
        {
            entt::entity Selected = entt::null;
            bool HasSelection = false;
            bool HasSurface = false;
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

        void ApplyOperator(entt::entity entity, const std::function<void(Geometry::Halfedge::Mesh&)>& op);

        void DrawMenu();
        void DrawWorkflowPanel();
        void DrawRemeshingPanel();
        void DrawSimplificationPanel();
        void DrawSmoothingPanel();
        void DrawSubdivisionPanel();
        void DrawRepairPanel();
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
