module;

#include <compare>
#include <cstddef>
#include <cstdint>

export module Extrinsic.ECS.Component.StableId;

export namespace Extrinsic::ECS::Components
{
    // 128-bit UUID-shaped durable identity for an authoring/runtime entity.
    //
    // Use `StableId` when a reference must survive `entt::entity` recycling,
    // scene save/load, undo/redo, prefab references, or hot reload. The
    // volatile `entt::entity` value is unsuitable for any of those because it
    // is insertion-order-dependent and gets recycled across destruction.
    //
    // `StableId` is **optional authoring metadata**, not a registry-wide
    // field: the default scene bootstrap does not assign one, and transient
    // entities (procedural geometry instances, debug overlays, render-
    // extracted snapshots) skip the 16-byte cost entirely. Author/runtime
    // code emplaces a `StableId` only when a serializer / undo / prefab /
    // external-reference consumer needs durability.
    //
    // ECS owns only this value type plus the sentinel, validity check,
    // equality, ordering, and the exported hasher. Any
    // `StableId -> entt::entity` lookup sidecar (scene-local map,
    // prefab-aware resolver, …) lives in `src/runtime/`; see
    // `tasks/done/HARDEN-068-ecs-stable-identity-and-scene-metadata.md`
    // Decision 3.
    //
    // The payload is CPU-only and intentionally imports neither `entt`,
    // `geometry`, `assets`, `runtime`, `graphics`, nor `platform`. The
    // contract test
    // `tests/contract/ecs/Test.ECS.LayeringBoundaries.cpp`
    // (`StableIdPayloadStaysCpuOnly`) enforces this so the value type
    // remains usable outside ECS (serializer, editor, runtime helpers).
    struct StableId
    {
        std::uint64_t High = 0u;
        std::uint64_t Low = 0u;

        auto operator<=>(const StableId&) const = default;
    };

    // Sentinel value for an entity that has no assigned durable identity.
    // `kInvalidStableId.High == 0 && kInvalidStableId.Low == 0`.
    inline constexpr StableId kInvalidStableId{0u, 0u};

    [[nodiscard]] constexpr bool IsValid(StableId id) noexcept
    {
        return id != kInvalidStableId;
    }

    // Exported hasher for use in unordered containers across module
    // boundaries. Mirrors the `Extrinsic.Core.StrongHandle` exported-hasher
    // pattern (`src/core/Core.StrongHandle.cppm`): a std::hash specialization
    // in a module purview is not reliably visible to consumers that
    // instantiate `std::unordered_map` in their own GMF, so callers parameterize
    // their unordered containers with `StableIdHash` explicitly.
    struct StableIdHash
    {
        std::size_t operator()(StableId const& id) const noexcept
        {
            // splitmix64-style finalizer over each half, then xor-combine.
            // Distinct from a plain xor so two ids with the same value in
            // both halves do not collapse to zero.
            auto mix = [](std::uint64_t v) noexcept -> std::uint64_t
            {
                v ^= v >> 33;
                v *= 0xff51afd7ed558ccdULL;
                v ^= v >> 33;
                v *= 0xc4ceb9fe1a85ec53ULL;
                v ^= v >> 33;
                return v;
            };
            const std::uint64_t h = mix(id.High);
            const std::uint64_t l = mix(id.Low);
            return static_cast<std::size_t>(h ^ (l + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2)));
        }
    };
}
