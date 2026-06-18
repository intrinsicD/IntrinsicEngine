module;

#include <optional>
#include <span>

export module Extrinsic.Runtime.CameraFocusCommand;

import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Runtime.CameraControllers;
import Extrinsic.Runtime.SelectionController;
import Geometry.Sphere;

// Reusable "focus the camera on objects" command (RUNTIME-116).
//
// Repositions the active camera so a chosen set of objects is centered in view
// and completely visible. The framing math itself already lives in each
// `ICameraController::Focus(CameraFocusTarget)`; this module computes a good
// `CameraFocusTarget` from a set of world-space bounding spheres and routes it
// to a controller slot. Multi-object focus uses the center of mass of the
// selected bounds and the largest enclosing extent so every object fits.
//
// Layering: runtime-owned. Reads ECS world bounds + runtime selection and drives
// the runtime camera controllers; never imports graphics or platform.
export namespace Extrinsic::Runtime
{
    // Pure aggregation: center of mass (mean of sphere centers) and the largest
    // enclosing radius `max_i(|C - Cᵢ| + Rᵢ)` over the finite input spheres, so
    // (Center, Radius) contains every input sphere. Returns nullopt when no
    // finite sphere is provided. Non-finite spheres are skipped; the radius is
    // floored to a small positive minimum.
    [[nodiscard]] std::optional<CameraFocusTarget> ComputeFocusTargetForBoundingSpheres(
        std::span<const Geometry::Sphere> worldSpheres) noexcept;

    // Gathers the world bounding sphere of each valid entity that carries
    // `ECS::Components::Culling::World::Bounds` and aggregates them. Entities that
    // are invalid or have no world bounds are skipped. Returns nullopt when none
    // contribute.
    [[nodiscard]] std::optional<CameraFocusTarget> ComputeFocusTargetForEntities(
        const ECS::Scene::Registry& scene,
        std::span<const ECS::EntityHandle> entities) noexcept;

    // Applies a focus target to the controller in `slot`: calls Focus() and marks
    // an explicit camera transition. Returns false when the slot has no
    // controller.
    bool ApplyCameraFocus(CameraControllerRegistry& cameras,
                          CameraControllerSlot slot,
                          const CameraFocusTarget& target) noexcept;

    // Reusable command: frame `entities` with the `slot` camera. Returns true when
    // the camera was repositioned (≥1 bounded entity and a controller present).
    bool FocusCameraOnEntities(CameraControllerRegistry& cameras,
                               const ECS::Scene::Registry& scene,
                               std::span<const ECS::EntityHandle> entities,
                               CameraControllerSlot slot = CameraControllerSlot::Main) noexcept;

    // Selection-driven wrapper (the `F`-key command): frames the controller in
    // `slot` on the current selection. Returns true when the camera was
    // repositioned; a no-op returning false when nothing selectable is bounded.
    bool FocusCameraOnSelection(CameraControllerRegistry& cameras,
                                const SelectionController& selection,
                                const ECS::Scene::Registry& scene,
                                CameraControllerSlot slot = CameraControllerSlot::Main) noexcept;
}
