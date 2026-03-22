module;
#include <algorithm>
#include <cmath>
#include <cstdint>

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
        Configure(m_FramesInFlight);
    }

    void FrameContextRing::Configure(uint32_t framesInFlight)
    {
        m_FramesInFlight = SanitizeFrameContextCount(framesInFlight);
        for (uint32_t slotIndex = 0; slotIndex < MaxFrameContexts; ++slotIndex)
        {
            FrameContext& frame = m_Contexts[slotIndex];
            frame = FrameContext{};
            frame.SlotIndex = slotIndex;
            frame.FramesInFlight = m_FramesInFlight;
        }
    }

    FrameContext& FrameContextRing::BeginFrame(uint64_t frameNumber, RenderViewport viewport) &
    {
        const uint32_t slotIndex = static_cast<uint32_t>(frameNumber % static_cast<uint64_t>(m_FramesInFlight));
        FrameContext& frame = m_Contexts[slotIndex];
        const uint64_t previousFrameNumber = frame.FrameNumber;
        const bool hasPreviousFrame = frame.Prepared || frame.Submitted || frame.Viewport.IsValid() ||
                                      frame.FrameNumber != 0u || frame.PreviousFrameNumber != InvalidFrameNumber;

        frame.ResetPreparedState();
        frame.FrameNumber = frameNumber;
        frame.PreviousFrameNumber = hasPreviousFrame ? previousFrameNumber : InvalidFrameNumber;
        frame.SlotIndex = slotIndex;
        frame.FramesInFlight = m_FramesInFlight;
        frame.Viewport = viewport;
        return frame;
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
