module;
#include <algorithm>
#include <cmath>
#include <cstdint>

export module Runtime.RenderExtraction;

import Graphics.Camera;
import Runtime.SceneManager;

export namespace Runtime
{
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
        RenderViewport Viewport{};
        bool Prepared = false;
        bool Submitted = false;
    };

    [[nodiscard]] RenderFrameInput MakeRenderFrameInput(const Graphics::CameraComponent& camera,
                                                        WorldSnapshot world,
                                                        RenderViewport viewport,
                                                        double alpha = 0.0);

    [[nodiscard]] RenderWorld ExtractRenderWorld(const RenderFrameInput& input);
}
