module;
#include <algorithm>
#include <cmath>

module Runtime.RenderExtraction;

namespace Runtime
{
    namespace
    {
        [[nodiscard]] double SanitizeAlpha(double alpha)
        {
            if (!std::isfinite(alpha))
                return 0.0;

            return std::clamp(alpha, 0.0, 1.0);
        }
    }

    uint32_t SanitizeFrameContextCount(uint32_t requestedCount)
    {
        return std::clamp(requestedCount, MinFrameContexts, MaxFrameContexts);
    }

    FrameContextRing::FrameContextRing(uint32_t framesInFlight)
        : m_FramesInFlight(SanitizeFrameContextCount(framesInFlight))
    {
    }

    void FrameContextRing::Configure(uint32_t framesInFlight)
    {
        m_FramesInFlight = SanitizeFrameContextCount(framesInFlight);
    }

    FrameContext FrameContextRing::BeginFrame(uint64_t frameNumber, RenderViewport viewport) &
    {
        const uint32_t slotIndex = static_cast<uint32_t>(frameNumber % static_cast<uint64_t>(m_FramesInFlight));
        const uint64_t previousFrameNumber = m_LastFramePerSlot[slotIndex];
        m_LastFramePerSlot[slotIndex] = frameNumber;

        return FrameContext{
            .FrameNumber = frameNumber,
            .PreviousFrameNumber = previousFrameNumber,
            .SlotIndex = slotIndex,
            .FramesInFlight = m_FramesInFlight,
            .Viewport = viewport,
        };
    }

    RenderFrameInput MakeRenderFrameInput(const Graphics::CameraComponent& camera,
                                          WorldSnapshot world,
                                          RenderViewport viewport,
                                          double alpha)
    {
        return RenderFrameInput{
            .Alpha = SanitizeAlpha(alpha),
            .Camera = camera,
            .Viewport = viewport,
            .World = world,
        };
    }

    RenderWorld ExtractRenderWorld(const RenderFrameInput& input)
    {
        return RenderWorld{
            .Alpha = SanitizeAlpha(input.Alpha),
            .Camera = input.Camera,
            .Viewport = input.Viewport,
            .World = input.World,
        };
    }
}
