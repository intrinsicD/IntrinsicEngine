module;
#include <memory>
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

export module Graphics:Components;

import :Geometry;
import :Material;
import Geometry;
import Core.Assets;

export namespace ECS::MeshRenderer
{
    struct Component
    {
        Geometry::GeometryHandle Geometry;
        Core::Assets::AssetHandle Material;

        // --- Retained Mode Slot ---
        static constexpr uint32_t kInvalidSlot = ~0u;
        uint32_t GpuSlot = kInvalidSlot;

        // --- Render Cache ---
        // Allows RenderSystem to avoid AssetManager lookups once resolved.
        Graphics::MaterialHandle CachedMaterialHandle = {};

        // Cached snapshot used by GPUSceneSync to detect when instance TextureID must be refreshed.
        Graphics::MaterialHandle CachedMaterialHandleForInstance = {};
        uint32_t CachedMaterialRevisionForInstance = 0u;
        bool CachedIsSelectedForInstance = false;
    };
}

export namespace ECS::MeshCollider
{
    struct Component
    {
        std::shared_ptr<Graphics::GeometryCollisionData> CollisionRef;
        Geometry::OBB WorldOBB;
    };
}

// -------------------------------------------------------------------------
// PointCloudRenderer — ECS component for point cloud visualization.
// -------------------------------------------------------------------------
//
// Entities with this component are rendered by PointCloudRenderPass.
// The component holds the actual point data (positions, normals, colors, radii)
// and per-entity rendering parameters.
//
// Point data is uploaded to a consolidated SSBO each frame by the
// PointCloudRenderPass during AddPasses().
//
// Rendering modes:
//   0 = Flat disc (screen-aligned billboard, constant pixel radius)
//   1 = Surfel (oriented disc from surface normal, with lighting)
//   2 = EWA splatting (perspective-correct Gaussian elliptical splats)

export namespace ECS::PointCloudRenderer
{
    struct Component
    {
        // ---- Point Cloud Data ----
        std::vector<glm::vec3> Positions;           // Required.
        std::vector<glm::vec3> Normals;             // Optional (empty = use default up).
        std::vector<glm::vec4> Colors;              // Optional (empty = use DefaultColor).
        std::vector<float>     Radii;               // Optional (empty = use DefaultRadius).

        // ---- Rendering Parameters ----
        uint32_t RenderMode       = 0;              // 0 = flat, 1 = surfel, 2 = EWA.
        float    DefaultRadius    = 0.005f;         // World-space radius when Radii is empty.
        float    SizeMultiplier   = 1.0f;           // Per-entity size multiplier.
        glm::vec4 DefaultColor    = {1.f, 1.f, 1.f, 1.f}; // RGBA when Colors is empty.
        bool     Visible          = true;           // Runtime visibility toggle.

        // ---- Queries ----
        [[nodiscard]] std::size_t PointCount() const noexcept { return Positions.size(); }
        [[nodiscard]] bool HasNormals() const noexcept { return Normals.size() == Positions.size(); }
        [[nodiscard]] bool HasColors() const noexcept { return Colors.size() == Positions.size(); }
        [[nodiscard]] bool HasRadii() const noexcept { return Radii.size() == Positions.size(); }
    };
}
