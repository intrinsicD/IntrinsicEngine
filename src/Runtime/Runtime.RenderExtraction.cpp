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
