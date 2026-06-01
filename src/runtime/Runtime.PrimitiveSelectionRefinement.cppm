module;

#include <cstdint>

#include <glm/glm.hpp>

export module Extrinsic.Runtime.PrimitiveSelectionRefinement;

import Extrinsic.ECS.Components.GeometrySources;
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
        // only: it is validated against authoritative CPU geometry.
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

        [[nodiscard]] bool Resolved() const noexcept { return IsResolved(Status); }
    };

    // Refine one graphics primitive hint into an authoritative selection result
    // against the entity's promoted `GeometrySources` view. Pure CPU, stateless,
    // and side-effect free: the caller owns any selection-cache mutation. A
    // failure status leaves the primitive id fields at `kInvalidPrimitiveIndex`
    // and `HasHitPosition` false.
    [[nodiscard]] PrimitiveSelectionResult RefinePrimitiveSelection(
        const ECS::Components::GeometrySources::ConstSourceView& view,
        const PrimitiveRefineRequest& request);
}
