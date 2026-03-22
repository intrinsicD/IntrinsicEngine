module;
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>

export module Runtime.RenderExtraction;

import Graphics.Camera;
import Runtime.SceneManager;

export namespace Runtime
{
    inline constexpr uint32_t MinFrameContexts = 2;
    inline constexpr uint32_t DefaultFrameContexts = 2;
    inline constexpr uint32_t MaxFrameContexts = 3;
    inline constexpr uint64_t InvalidFrameNumber = std::numeric_limits<uint64_t>::max();

    struct RenderViewport
    {
        uint32_t Width = 0;
        uint32_t Height = 0;

        [[nodiscard]] bool IsValid() const
        {
            return Width > 0 && Height > 0;
        }
    };

    struct RenderFrameInput
    {
        double Alpha = 0.0;
        Graphics::CameraComponent Camera{};
        RenderViewport Viewport{};
        WorldSnapshot World{};

        [[nodiscard]] bool IsValid() const
        {
            return World.IsValid() && Viewport.IsValid();
        }
    };

    struct RenderWorld
    {
        double Alpha = 0.0;
        Graphics::CameraComponent Camera{};
        RenderViewport Viewport{};
        WorldSnapshot World{};

        [[nodiscard]] bool IsValid() const
        {
            return World.IsValid() && Viewport.IsValid();
        }
    };

    struct FrameContext
    {
        uint64_t FrameNumber = 0;
        uint64_t PreviousFrameNumber = InvalidFrameNumber;
        uint32_t SlotIndex = 0;
        uint32_t FramesInFlight = DefaultFrameContexts;
        RenderViewport Viewport{};
        std::optional<RenderWorld> PreparedRenderWorld{};
        bool Prepared = false;
        bool Submitted = false;

        [[nodiscard]] const RenderWorld* GetPreparedRenderWorld() const
        {
            return PreparedRenderWorld ? &*PreparedRenderWorld : nullptr;
        }

        [[nodiscard]] RenderWorld* GetPreparedRenderWorld()
        {
            return PreparedRenderWorld ? &*PreparedRenderWorld : nullptr;
        }

        void ResetPreparedState()
        {
            PreparedRenderWorld.reset();
            Prepared = false;
            Submitted = false;
        }
    };

    [[nodiscard]] uint32_t SanitizeFrameContextCount(uint32_t requestedCount);

    class FrameContextRing
    {
    public:
        explicit FrameContextRing(uint32_t framesInFlight = DefaultFrameContexts);

        void Configure(uint32_t framesInFlight);

        [[nodiscard]] uint32_t GetFramesInFlight() const
        {
            return m_FramesInFlight;
        }

        [[nodiscard]] FrameContext& BeginFrame(uint64_t frameNumber, RenderViewport viewport) &;

    private:
        uint32_t m_FramesInFlight = DefaultFrameContexts;
        std::array<FrameContext, MaxFrameContexts> m_Contexts{};
    };

    [[nodiscard]] RenderFrameInput MakeRenderFrameInput(const Graphics::CameraComponent& camera,
                                                        WorldSnapshot world,
                                                        RenderViewport viewport,
                                                        double alpha = 0.0);

    [[nodiscard]] RenderWorld ExtractRenderWorld(const RenderFrameInput& input);
}
