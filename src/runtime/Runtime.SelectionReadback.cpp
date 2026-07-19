module;

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>

module Extrinsic.Runtime.SelectionReadback;

import Extrinsic.Graphics.CameraSnapshots;

namespace Extrinsic::Runtime
{
    namespace
    {
        [[nodiscard]] std::optional<PickReadbackContext> BuildPickReadbackContextForFrame(
            const Graphics::RenderFrameInput& renderInput,
            const Platform::Extent2D& viewport)
        {
            const Graphics::CameraViewSnapshot pickCamera =
                Graphics::BuildCameraViewSnapshot(renderInput.Camera,
                                                  viewport,
                                                  renderInput.Pick);
            if (!pickCamera.Valid)
                return std::nullopt;

            const std::uint32_t viewportWidth =
                viewport.Width > 0 ? static_cast<std::uint32_t>(viewport.Width) : 0u;
            const std::uint32_t viewportHeight =
                viewport.Height > 0 ? static_cast<std::uint32_t>(viewport.Height) : 0u;
            PickReadbackContext context{};
            context.InverseViewProjection = pickCamera.InverseViewProjection;
            context.ViewportWidth         = viewportWidth;
            context.ViewportHeight        = viewportHeight;
            context.HasWorldRay           = pickCamera.HasPickRay;
            context.WorldRayOrigin        = pickCamera.PickRayOrigin;
            context.WorldRayDirection     = pickCamera.PickRayDirection;
            const float projectionScaleY =
                std::abs(renderInput.Camera.Projection[1][1]);
            if (projectionScaleY > 0.000001f && viewportHeight > 0u)
            {
                context.WorldUnitsPerPixelAtUnitDepth =
                    2.0f / (projectionScaleY *
                            static_cast<float>(viewportHeight));
            }
            context.OrthographicProjection =
                IsOrthographicProjection(renderInput.Camera.Projection);
            return context;
        }

    }

    const std::optional<PrimitiveSelectionResult>&
    SelectionReadbackState::LastRefinedPrimitive() const noexcept
    {
        return m_LastRefinedPrimitive;
    }

    std::uint64_t SelectionReadbackState::LastRefinedPrimitiveGeneration() const noexcept
    {
        return m_LastRefinedPrimitiveGeneration;
    }

    void SelectionReadbackState::ClearRefinedPrimitiveCache()
    {
        m_LastRefinedPrimitive.reset();
        ++m_LastRefinedPrimitiveGeneration;
    }

    void SelectionReadbackState::ClearSceneState()
    {
        m_InFlightPickContexts.clear();
        ClearRefinedPrimitiveCache();
    }

    void SelectionReadbackState::DrainPendingPickForFrame(
        SelectionController& selection,
        Graphics::SelectionSystem& selectionSystem,
        const Platform::Extent2D& viewport,
        Graphics::RenderFrameInput& renderInput,
        const WorldHandle world,
        const std::uint64_t interactionEpoch)
    {
        const std::optional<PendingSelectionPick> pick =
            selection.ConsumePendingPick();
        if (!pick.has_value())
            return;

        renderInput.HasPendingPick = true;
        renderInput.Pick = Graphics::PickPixelRequest{
            .X        = pick->PixelX,
            .Y        = pick->PixelY,
            .Pending  = true,
            .Sequence = pick->Sequence,
        };
        selectionSystem.RequestPick(Graphics::PickRequest{
            .PixelX = pick->PixelX,
            .PixelY = pick->PixelY,
        });

        constexpr std::size_t kMaxInFlightPickContexts = 32u;
        if (m_InFlightPickContexts.size() >= kMaxInFlightPickContexts)
        {
            (void)selection.DiscardInFlightPick(
                m_InFlightPickContexts.front().Sequence);
            m_InFlightPickContexts.erase(m_InFlightPickContexts.begin());
        }

        m_InFlightPickContexts.push_back(InFlightPickContext{
            .Sequence = pick->Sequence,
            .World = world,
            .InteractionEpoch = interactionEpoch,
            .Context =
                BuildPickReadbackContextForFrame(renderInput, viewport),
        });
    }

    void SelectionReadbackState::DrainCompletedReadbacksForFrame(
        Graphics::SelectionSystem& selectionSystem,
        SelectionController& selection,
        ECS::Scene::Registry& scene,
        const WorldHandle world,
        const std::uint64_t interactionEpoch)
    {
        while (const std::optional<Graphics::PickReadbackResult> result =
                   selectionSystem.PopPickResult())
        {
            // Production correlation is fail-closed. In particular, never
            // forward zero/unknown sequences into SelectionController's
            // standalone convenience fallback, which intentionally treats an
            // untracked result as a configured click.
            if (result->Sequence == 0u)
                continue;

            const auto contextIt = std::find_if(
                m_InFlightPickContexts.begin(),
                m_InFlightPickContexts.end(),
                [seq = result->Sequence](const InFlightPickContext& entry)
                { return entry.Sequence == seq; });
            if (contextIt == m_InFlightPickContexts.end())
                continue;

            InFlightPickContext context = std::move(*contextIt);
            m_InFlightPickContexts.erase(contextIt);
            if (context.World != world ||
                context.InteractionEpoch != interactionEpoch)
            {
                (void)selection.DiscardInFlightPick(
                    result->Sequence);
                continue;
            }

            const bool consumed = result->Hit
                ? selection.ConsumeHit(
                      scene,
                      result->StableEntityId,
                      result->Sequence)
                : selection.ConsumeNoHit(
                      scene,
                      result->Sequence);
            if (!consumed)
                continue;

            m_LastRefinedPrimitive =
                RefinePickReadbackResult(
                    scene,
                    *result,
                    context.Context ? &*context.Context : nullptr);
            ++m_LastRefinedPrimitiveGeneration;
        }
    }
}
