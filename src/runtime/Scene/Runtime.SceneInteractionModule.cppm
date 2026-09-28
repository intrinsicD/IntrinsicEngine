// Owns active-world viewport interaction and publishes selection/gizmo snapshots.
module;

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

export module Extrinsic.Runtime.SceneInteractionModule;

import Extrinsic.Core.Error;
import Extrinsic.ECS.Component.StableId;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Runtime.WorldHandle;
import Extrinsic.Runtime.GizmoInteraction;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.RenderExtraction;
import Extrinsic.Runtime.ModuleLifecycle;
import Extrinsic.Runtime.PrimitiveSelectionRefinement;
import Extrinsic.Runtime.StableEntityLookup;

namespace Extrinsic::Runtime
{
    export RuntimeSceneInteractionRenderSnapshot BuildPrimitiveSelectionRenderSnapshot(
        const ECS::Scene::Registry& scene, const SelectionController& selection, WorldHandle world);

    // Optional app-composed owner for every active-world interaction record.
    // The object has app-global lifetime; its mutable cohort binds to exactly
    // one WorldHandle/Registry pair and never retains per-world history.
    export class SceneInteractionModule final : public IRuntimeModule
    {
    public:
        SceneInteractionModule();
        ~SceneInteractionModule() override;

        SceneInteractionModule(const SceneInteractionModule&) = delete;
        SceneInteractionModule& operator=(
            const SceneInteractionModule&) = delete;

        [[nodiscard]] std::string_view Name() const noexcept override;
        [[nodiscard]] Core::Result OnRegister(EngineSetup& setup) override;
        [[nodiscard]] Core::Result OnResolve(EngineSetup& setup) override;
        void OnShutdown(RuntimeModuleShutdownContext& context) override;

        [[nodiscard]] std::optional<ECS::EntityHandle>
            ResolveEntityByStableId(ECS::Components::StableId id);
        [[nodiscard]] const StableEntityLookupDiagnostics&
            LookupDiagnostics() const noexcept;

        [[nodiscard]] GizmoInteraction& Interaction() noexcept;
        [[nodiscard]] const GizmoInteraction& Interaction() const noexcept;

        [[nodiscard]] const std::optional<PrimitiveSelectionResult>&
            LastRefinedPrimitive() const noexcept;
        [[nodiscard]] std::uint64_t
            LastRefinedPrimitiveGeneration() const noexcept;

        // Transient points and lines an editor tool draws over the scene (for example a
        // registration preview) until it clears them. Each owner's overlay is replaced
        // as a whole; all overlays are dropped when the bound world changes.
        struct PreviewPoint
        {
            glm::vec3 Position{0.0f};
            glm::vec4 Color{1.0f};
            float Radius{0.01f}; // world units
            bool DepthTested{true};
        };
        struct PreviewLine
        {
            glm::vec3 Start{0.0f}, End{0.0f};
            glm::vec4 Color{1.0f};
            bool DepthTested{true};
        };
        void SetPreviewOverlay(std::string_view owner, std::span<const PreviewPoint> points,
                               std::span<const PreviewLine> lines = {});
        void ClearPreviewOverlay(std::string_view owner);

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl{};
    };
}
