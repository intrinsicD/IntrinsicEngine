module;

#include <cstdint>
#include <optional>
#include <vector>

export module Extrinsic.Runtime.SelectionReadback;

export import Extrinsic.Runtime.PrimitiveSelectionRefinement;

import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Graphics.RenderFrameInput;
import Extrinsic.Graphics.SelectionSystem;
import Extrinsic.Platform.Window;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.WorldHandle;

namespace Extrinsic::Runtime
{
    export class SelectionReadbackState
    {
    public:
        [[nodiscard]] const std::optional<PrimitiveSelectionResult>&
            LastRefinedPrimitive() const noexcept;
        [[nodiscard]] std::uint64_t LastRefinedPrimitiveGeneration() const noexcept;

        void ClearRefinedPrimitiveCache();
        void ClearSceneState();
        void DrainPendingPickForFrame(
            SelectionController& selection,
            Graphics::SelectionSystem& selectionSystem,
            const Platform::Extent2D& viewport,
            Graphics::RenderFrameInput& renderInput,
            WorldHandle world,
            std::uint64_t interactionEpoch);
        void DrainCompletedReadbacksForFrame(
            Graphics::SelectionSystem& selectionSystem,
            SelectionController& selection,
            ECS::Scene::Registry& scene,
            WorldHandle world,
            std::uint64_t interactionEpoch);

    private:
        struct InFlightPickContext
        {
            std::uint64_t Sequence{0u};
            WorldHandle World{};
            std::uint64_t InteractionEpoch{0u};
            std::optional<PickReadbackContext> Context{};
        };

        std::vector<InFlightPickContext> m_InFlightPickContexts{};
        std::optional<PrimitiveSelectionResult> m_LastRefinedPrimitive{};
        std::uint64_t m_LastRefinedPrimitiveGeneration{0u};
    };
}
