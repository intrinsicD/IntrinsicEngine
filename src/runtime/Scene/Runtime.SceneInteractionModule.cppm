module;

#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>

export module Extrinsic.Runtime.SceneInteractionModule;

import Extrinsic.Core.Error;
import Extrinsic.ECS.Component.StableId;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.Runtime.GizmoInteraction;
import Extrinsic.Runtime.Module;
import Extrinsic.Runtime.PrimitiveSelectionRefinement;
import Extrinsic.Runtime.StableEntityLookup;

namespace Extrinsic::Runtime
{
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
        [[nodiscard]] GizmoUndoStack& UndoStack() noexcept;
        [[nodiscard]] const GizmoUndoStack& UndoStack() const noexcept;

        [[nodiscard]] const std::optional<PrimitiveSelectionResult>&
            LastRefinedPrimitive() const noexcept;
        [[nodiscard]] std::uint64_t
            LastRefinedPrimitiveGeneration() const noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl{};
    };
}
