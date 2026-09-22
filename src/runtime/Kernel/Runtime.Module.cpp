module;

#include <algorithm>
#include <cmath>
#include <functional>
#include <utility>

module Extrinsic.Runtime.Module;

namespace Extrinsic::Runtime
{
    Core::Rect2D ResolveSceneViewportPixels(
        const Core::Extent2D windowExtent,
        const Core::Extent2D framebufferExtent,
        const EditorInputCaptureSnapshot& capture) noexcept
    {
        const Core::Rect2D whole{{0, 0}, framebufferExtent};
        if (!capture.HasSceneViewport || Core::IsEmpty(framebufferExtent) ||
            Core::IsEmpty(windowExtent))
        {
            return whole;
        }
        const EditorSceneViewportRect& rect = capture.SceneViewport;
        if (!std::isfinite(rect.X) || !std::isfinite(rect.Y) ||
            !std::isfinite(rect.Width) || !std::isfinite(rect.Height) ||
            rect.Width <= 0.0f || rect.Height <= 0.0f)
        {
            return whole;
        }
        const double scaleX = static_cast<double>(framebufferExtent.Width) /
                              static_cast<double>(windowExtent.Width);
        const double scaleY = static_cast<double>(framebufferExtent.Height) /
                              static_cast<double>(windowExtent.Height);
        // Round edges, not extents, so adjacent panes share one pixel seam.
        const auto edge = [](const double value, const int limit) {
            return static_cast<int>(std::clamp(std::lround(value), 0l, static_cast<long>(limit)));
        };
        const int x0 = edge(rect.X * scaleX, framebufferExtent.Width);
        const int y0 = edge(rect.Y * scaleY, framebufferExtent.Height);
        const int x1 = edge((static_cast<double>(rect.X) + rect.Width) * scaleX, framebufferExtent.Width);
        const int y1 = edge((static_cast<double>(rect.Y) + rect.Height) * scaleY, framebufferExtent.Height);
        if (x1 <= x0 || y1 <= y0)
            return whole;
        return {{x0, y0}, {x1 - x0, y1 - y0}};
    }

    SceneViewportCursor MapWindowCursorToSceneViewport(
        const float cursorX,
        const float cursorY,
        const Core::Extent2D windowExtent,
        const Core::Extent2D framebufferExtent,
        const Core::Rect2D sceneViewport) noexcept
    {
        if (!std::isfinite(cursorX) || !std::isfinite(cursorY))
            return {};
        float x = cursorX;
        float y = cursorY;
        if (!Core::IsEmpty(windowExtent) && !Core::IsEmpty(framebufferExtent))
        {
            x *= static_cast<float>(framebufferExtent.Width) /
                 static_cast<float>(windowExtent.Width);
            y *= static_cast<float>(framebufferExtent.Height) /
                 static_cast<float>(windowExtent.Height);
        }
        x -= static_cast<float>(sceneViewport.Offset.X);
        y -= static_cast<float>(sceneViewport.Offset.Y);
        return SceneViewportCursor{
            .X = x,
            .Y = y,
            .Inside = !Core::IsEmpty(sceneViewport.Extent) &&
                      x >= 0.0f && y >= 0.0f &&
                      x < static_cast<float>(sceneViewport.Extent.Width) &&
                      y < static_cast<float>(sceneViewport.Extent.Height),
        };
    }

    extern "C++"
    {
    EngineSetup::EngineSetup(
        CommandBus& commands,
        KernelEventBus& events,
        JobService& jobs,
        WorldRegistry& worlds,
        ServiceRegistry& services,
        FrameHookRegistrar frameHookRegistrar,
        RuntimeRenderRecipeActivationKernel renderRecipeActivation,
        ViewportInputHookRegistrar viewportInputHookRegistrar,
        const bool* initializedState)
        : m_Commands(commands)
        , m_Events(events)
        , m_Jobs(jobs)
        , m_Worlds(worlds)
        , m_Services(services)
        , m_FrameHookRegistrar(std::move(frameHookRegistrar))
        , m_RenderRecipeActivation(std::move(renderRecipeActivation))
        , m_ViewportInputHookRegistrar(std::move(viewportInputHookRegistrar))
        , m_InitializedState(initializedState)
    {
    }

    Core::Result EngineSetup::RegisterFrameHook(
        FramePhase phase,
        RuntimeFrameHook hook)
    {
        if (!hook)
            return Core::Err(Core::ErrorCode::InvalidArgument);
        if (!m_FrameHookRegistrar)
            return Core::Err(Core::ErrorCode::InvalidState);
        m_FrameHookRegistrar(phase, std::move(hook));
        return Core::Ok();
    }

    Core::Result EngineSetup::RegisterViewportInputHook(
        RuntimeViewportInputHook hook)
    {
        if (!hook)
            return Core::Err(Core::ErrorCode::InvalidArgument);
        if (!m_ViewportInputHookRegistrar)
            return Core::Err(Core::ErrorCode::InvalidState);
        m_ViewportInputHookRegistrar(std::move(hook));
        return Core::Ok();
    }
    }
}
