module;

#include <cstdint>
#include <optional>

#include <glm/glm.hpp>

export module Extrinsic.Runtime.PrimitiveSelectionRefinement;

import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Graphics.SelectionSystem;

export namespace Extrinsic::Runtime
{
    // Sentinel for an unresolved primitive index in `PrimitiveSelectionResult`.
    inline constexpr std::uint32_t kInvalidPrimitiveIndex = 0xFFFFFFFFu;

    // Which authoritative CPU primitive the runtime resolved a GPU hint to.
    // A graph node is reported through the `Vertex` kind (nodes are stored in
    // the `Nodes` PropertySet but indexed like vertices); a point-cloud point is
    // reported through `Point`.
    enum class RefinedPrimitiveKind : std::uint8_t
    {
        None = 0,
        Entity, // whole-entity selection; no sub-primitive resolved.
        Face,   // mesh face.
        Edge,   // mesh or graph edge.
        Vertex, // mesh vertex or graph node.
        Point,  // point-cloud point.
    };

    // Fail-closed taxonomy for `RefinePrimitiveSelection`. Each value is a
    // deterministic, diagnosable outcome; `IsResolved` distinguishes the two
    // success-like states from the rejections.
    enum class PrimitiveRefineStatus : std::uint8_t
    {
        Success,                 // GPU hint resolved to an authoritative primitive.
        CpuFallbackResolved,     // hint absent/insufficient; resolved by CPU query (Slice B).
        UnsupportedDomain,       // geometry domain unsupported, or hint primitive domain
                                 // not applicable to this geometry domain.
        StaleEntity,             // request flagged the entity as no longer live/selectable.
        MissingGeometrySource,   // required PropertySet / canonical property absent or wrong-typed.
        InvalidPrimitivePayload, // hinted primitive index out of range / malformed topology.
        CpuFallbackMiss,         // CPU fallback ran but found nothing within tolerance (Slice B).
    };

    [[nodiscard]] const char* DebugNameForPrimitiveRefineStatus(PrimitiveRefineStatus status) noexcept;

    [[nodiscard]] constexpr bool IsResolved(PrimitiveRefineStatus status) noexcept
    {
        return status == PrimitiveRefineStatus::Success ||
               status == PrimitiveRefineStatus::CpuFallbackResolved;
    }

    // Input to `RefinePrimitiveSelection`. The runtime owns the bridge between a
    // graphics `EncodedSelectionId` (a hint produced by the ID pass) and the
    // authoritative CPU `GeometrySources` data, so this request carries the hint
    // plus the entity transform and an optional local-space hit anchor used to
    // pick the nearest sub-primitive on the hinted face/edge.
    struct PrimitiveRefineRequest
    {
        // Echoed verbatim into the result so a caller can correlate the refined
        // primitive with the entity it picked. `EntityId` mirrors the render id
        // used by `RenderExtractionCache` / `SelectionController`.
        std::uint32_t EntityId{0u};
        std::uint32_t StableId{0u};

        // GPU primitive hint (4-bit domain + 28-bit payload). Treated as a hint
        // only: it is validated against authoritative CPU geometry. The payload
        // semantics follow the ID pass — a `Face` payload is the per-draw surface
        // triangle index (`gl_PrimitiveID`), which refinement maps back to a face
        // row through the mesh packer's surface-triangle table because n-gon
        // faces fan-triangulate to multiple GPU triangles.
        Extrinsic::Graphics::EncodedSelectionId Hint{};

        // `SelectionController`-signalled liveness. A stale entity is rejected by
        // the single runtime authority rather than mis-resolving.
        bool EntityIsLive{true};

        // Optional pick anchor in entity-local space. When present it drives the
        // nearest-vertex/edge refinement on the hinted mesh face / graph edge and
        // becomes the reported hit position; otherwise the resolved primitive's
        // representative local position is reported.
        bool HasLocalHit{false};
        glm::vec3 LocalHit{0.0f};

        // Optional CPU ray fallback for a *missing* hint (`SelectionPrimitiveDomain::None`,
        // i.e. an `EncodedSelectionId` with no sub-primitive). When `HasPickRay`
        // is set and the hint carries no usable primitive, refinement searches for
        // the entity's nearest mesh vertex / graph node / point-cloud point whose
        // perpendicular distance to the ray is within `FallbackRadius`, reporting
        // `CpuFallbackResolved` (or `CpuFallbackMiss` when nothing qualifies). The
        // ray is entity-local space — the runtime caller, which owns the entity
        // transform, transforms a world-space pick ray into local space so this
        // entry point stays pure and only ever maps local→world for reporting. A
        // valid hint always wins over the ray; the fallback never overrides a
        // resolved `Face`/`Edge`/`Point`/`Entity` hit.
        bool HasPickRay{false};
        glm::vec3 RayOrigin{0.0f};
        glm::vec3 RayDirection{0.0f, 0.0f, -1.0f}; // need not be normalised.
        // Max perpendicular distance (local units) from the ray for a primitive
        // to qualify. `<= 0` yields a deterministic `CpuFallbackMiss` unless a
        // primitive lies exactly on the ray.
        float FallbackRadius{0.0f};

        // Entity local-to-world transform used to report the world-space hit.
        glm::mat4 LocalToWorld{1.0f};
    };

    // Refined primitive selection. `Status`/`Resolved()` report the outcome;
    // `FaceId`/`EdgeId`/`VertexId`/`PointId` carry the resolved primitives
    // (`kInvalidPrimitiveIndex` when not applicable); `LocalHit`/`WorldHit`
    // report consistent local/world hit data when `HasHitPosition` is set.
    struct PrimitiveSelectionResult
    {
        PrimitiveRefineStatus Status{PrimitiveRefineStatus::UnsupportedDomain};

        std::uint32_t EntityId{0u};
        std::uint32_t StableId{0u};

        ECS::Components::GeometrySources::Domain Domain{
            ECS::Components::GeometrySources::Domain::None};
        RefinedPrimitiveKind Kind{RefinedPrimitiveKind::None};

        std::uint32_t FaceId{kInvalidPrimitiveIndex};
        std::uint32_t EdgeId{kInvalidPrimitiveIndex};
        std::uint32_t VertexId{kInvalidPrimitiveIndex};
        std::uint32_t PointId{kInvalidPrimitiveIndex};

        bool HasHitPosition{false};
        glm::vec3 LocalHit{0.0f};
        glm::vec3 WorldHit{0.0f};

        // BUG-026 — cursor position reconstructed from the GPU depth readback,
        // reported in world space (`WorldCursor`) and entity-local space
        // (`LocalCursor`) when `CursorFromDepth` is true. `Depth` echoes the
        // raw [0..1] depth-buffer sample used (1.0 = depth clear / no sample).
        // Distinct from `LocalHit`/`WorldHit`: hint-anchored refinement reports
        // the cursor there too, but the ray-fallback path reports the resolved
        // primitive's position in `LocalHit`/`WorldHit` while the cursor stays
        // available here.
        bool CursorFromDepth{false};
        float Depth{1.0f};
        glm::vec3 LocalCursor{0.0f};
        glm::vec3 WorldCursor{0.0f};

        [[nodiscard]] bool Resolved() const noexcept { return IsResolved(Status); }
    };

    // BUG-026 — per-pick camera/cursor context the runtime captures when it
    // drains a pick into the renderer, replayed when the matching readback
    // arrives so cursor positions are reconstructed against the *issuing*
    // frame's camera (the camera may have moved by the time the GPU readback
    // completes).
    struct PickReadbackContext
    {
        // Inverse of the issuing frame's (projection * view); the same matrix
        // `Graphics::BuildCameraViewSnapshot` derives for the pick ray.
        glm::mat4 InverseViewProjection{1.0f};
        std::uint32_t ViewportWidth{0u};
        std::uint32_t ViewportHeight{0u};
        // World-space pick ray through the pick pixel (from the camera
        // snapshot); drives the missing-hint CPU fallback in entity-local space.
        bool HasWorldRay{false};
        glm::vec3 WorldRayOrigin{0.0f};
        glm::vec3 WorldRayDirection{0.0f, 0.0f, -1.0f};
        // World units spanned by one pixel at view depth 1 —
        // `2 * tan(fovY / 2) / viewportHeight`, derived from the projection —
        // so the pixel pick radius scales with the hit distance.
        float WorldUnitsPerPixelAtUnitDepth{0.0f};
        float PickRadiusPixels{12.0f};
    };

    // Reconstruct the world-space position of `(pixelX, pixelY)` at
    // depth-buffer sample `depth` ([0 (near) .. 1 (far)], Vulkan convention)
    // through `inverseViewProjection`. Pixel -> NDC mapping mirrors
    // `Graphics::BuildCameraViewSnapshot`'s pick-ray derivation (pixel center
    // at +0.5, NDC Y up). Returns std::nullopt for a degenerate viewport, a
    // near-zero homogeneous w, or a non-finite result.
    [[nodiscard]] std::optional<glm::vec3> UnprojectPickDepth(
        const glm::mat4& inverseViewProjection,
        std::uint32_t pixelX,
        std::uint32_t pixelY,
        std::uint32_t viewportWidth,
        std::uint32_t viewportHeight,
        float depth) noexcept;

    // Refine one graphics primitive hint into an authoritative selection result
    // against the entity's promoted `GeometrySources` view. Pure CPU, stateless,
    // and side-effect free: the caller owns any selection-cache mutation. A
    // failure status leaves the primitive id fields at `kInvalidPrimitiveIndex`
    // and `HasHitPosition` false.
    [[nodiscard]] PrimitiveSelectionResult RefinePrimitiveSelection(
        const ECS::Components::GeometrySources::ConstSourceView& view,
        const PrimitiveRefineRequest& request);

    // Frame-loop bridge from a graphics pick readback to an authoritative
    // primitive selection result, owned by `runtime` (the only layer allowed to
    // bridge graphics pick output with the CPU `GeometrySources` authority).
    //
    // The readback's `StableEntityId` is the *render id* owned by
    // `StableEntityLookup::ToRenderId` (`entt::entity` handle cast + 1, with 0
    // reserved for background — BUG-026), shared with `RenderExtractionCache` /
    // `SelectionController`. This function resolves it to a live entity by
    // decoding + a `registry.valid()` version check, so a stale render id naming a
    // recycled/destroyed slot reports a deterministic `StaleEntity` result rather
    // than refining the slot's new occupant. The entity's `Transform::WorldMatrix`
    // (identity when absent) supplies `LocalToWorld`, the `GeometrySources` view is
    // built from the live registry, and `RefinePrimitiveSelection` does the rest.
    //
    // When `context` is non-null and the readback carries a valid depth sample,
    // the bridge reconstructs the world-space cursor position (BUG-026), feeds
    // its entity-local equivalent as the refinement anchor (driving the
    // nearest-vertex/edge resolution on the hinted face/edge), and supplies the
    // entity-local pick ray + distance-scaled radius for the missing-hint CPU
    // fallback. The result then reports the cursor in both spaces
    // (`CursorFromDepth`, `LocalCursor`, `WorldCursor`, `Depth`).
    //
    // A background (no-hit) readback resolves to no sub-primitive (`std::nullopt`).
    // Pure read: it mutates neither the registry nor any selection state — the
    // caller (`Engine::RunFrame`) owns the editor-facing cache it is stored in.
    [[nodiscard]] std::optional<PrimitiveSelectionResult> RefinePickReadbackResult(
        ECS::Scene::Registry& scene,
        const Extrinsic::Graphics::PickReadbackResult& readback,
        const PickReadbackContext* context);
    [[nodiscard]] std::optional<PrimitiveSelectionResult> RefinePickReadbackResult(
        ECS::Scene::Registry& scene,
        const Extrinsic::Graphics::PickReadbackResult& readback);
}
