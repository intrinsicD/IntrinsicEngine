module;
#include <cstdint>
#include <limits>
#include <set>
#include <glm/glm.hpp>
#include <entt/entity/entity.hpp>

export module Runtime.Selection;

import ECS;
import Geometry.Ray;
import Graphics.Components;
import Graphics.Camera;

export namespace Runtime::Selection
{
    struct Points
    {
        glm::vec3 World{0.0f};
        glm::vec3 Local{0.0f};
        glm::vec3 WorldNormal{0.0f};
        glm::vec3 Barycentric{0.0f};
    };

    struct Picked
    {
        struct Entity
        {
            static constexpr uint32_t InvalidIndex = std::numeric_limits<uint32_t>::max();

            entt::entity id = entt::null;
            bool is_background = true;

            constexpr explicit operator bool() const
            {
                return !is_background && id != entt::null;
            }

            // Sub-element IDs resolved for the pick. The ID corresponding to the
            // active ElementMode is the only selection-critical guarantee.
            // Remaining IDs are best-effort context that may be filled by CPU
            // refinement when enough information is available.
            uint32_t vertex_idx = InvalidIndex;
            uint32_t edge_idx = InvalidIndex;
            uint32_t face_idx = InvalidIndex;
            float pick_radius = 0.0f;
        } entity{};

        Points spaces{};
    };

    enum class PickBackend : uint8_t
    {
        CPU = 0,
        GPU = 1
    };

    enum class PickMode : uint8_t
    {
        Replace = 0,
        Add = 1,
        Toggle = 2
    };

    // Element-level selection mode: determines what clicking on geometry selects.
    // In Entity mode, whole entities are selected (existing behavior).
    // In Vertex/Edge/Face mode, sub-elements are selected on the picked entity.
    enum class ElementMode : uint8_t
    {
        Entity = 0,
        Vertex = 1,
        Edge = 2,
        Face = 3
    };

    // Per-entity sub-element selection state.
    // Tracks which vertices, edges, or faces are selected on a given entity.
    struct SubElementSelection
    {
        entt::entity Entity = entt::null;
        std::set<uint32_t> SelectedVertices{};
        std::set<uint32_t> SelectedEdges{};
        std::set<uint32_t> SelectedFaces{};

        void Clear()
        {
            Entity = entt::null;
            SelectedVertices.clear();
            SelectedEdges.clear();
            SelectedFaces.clear();
        }

        [[nodiscard]] bool Empty() const
        {
            return SelectedVertices.empty() && SelectedEdges.empty() && SelectedFaces.empty();
        }
    };

    struct PickRequest
    {
        Geometry::Ray WorldRay{};
        glm::mat4 ViewMatrix{1.0f};
        glm::mat4 ProjectionMatrix{1.0f};
        glm::vec2 CursorPositionPixels{0.0f};
        PickBackend Backend = PickBackend::CPU;
        PickMode Mode = PickMode::Replace;

        // If true, also update HoveredTag (for UI hover highlight).
        bool UpdateHover = false;

        // Optional: max distance.
        float MaxDistance = std::numeric_limits<float>::infinity();

        // Screen-space radius used to resolve sub-element picks (vertex/edge/face)
        // into a world-space tolerance at the hit depth.
        float PickRadiusPixels = 12.0f;

        // Camera snapshot captured when the pick request is issued.
        float CameraFovYRadians = glm::radians(45.0f);
        float ViewportWidthPixels = 1600.0f;
        float ViewportHeightPixels = 900.0f;
    };

    struct PickResult
    {
        entt::entity Entity = entt::null;
        float T = std::numeric_limits<float>::infinity();
        Picked PickedData{};
    };

    inline constexpr uint32_t InvalidPrimitiveID = Picked::Entity::InvalidIndex;

    [[nodiscard]] constexpr bool IsValidPrimitiveID(uint32_t primitiveID)
    {
        return primitiveID != InvalidPrimitiveID;
    }

    // CPU picking: uses MeshCollider broadphase (WorldOBB) and watertight ray/triangle on mesh data.
    [[nodiscard]] PickResult PickCPU(const ECS::Scene& scene, const PickRequest& request);
    [[nodiscard]] PickResult PickEntityCPU(const ECS::Scene& scene, entt::entity entity, const PickRequest& request);

    // GPU picking resolves entity ID on the GPU, then completes the picked
    // primitive tuple on the CPU using the self-describing primitive hint from
    // the winning rendered primitive (surface triangle / line segment / point).
    // Contract: the ID corresponding to `elementMode` is guaranteed when that
    // domain can be resolved for the picked object. Additional IDs are
    // best-effort context only and must not be treated as API guarantees.
    [[nodiscard]] Picked ResolveGpuSubElementPick(const ECS::Scene& scene,
                                                  entt::entity entity,
                                                  uint32_t primitiveID,
                                                  ElementMode elementMode,
                                                  const PickRequest* request);

    // Apply selection state changes on the registry (SelectedTag/HoveredTag).
    void ApplySelection(ECS::Scene& scene, entt::entity hitEntity, PickMode mode);
    void ApplyHover(ECS::Scene& scene, entt::entity hoveredEntity);

    // Helper: build a world ray from a normalized pixel coordinate in NDC (-1..1) using camera matrices.
    [[nodiscard]] Geometry::Ray RayFromNDC(const Graphics::CameraComponent& camera, const glm::vec2& ndc);
}
