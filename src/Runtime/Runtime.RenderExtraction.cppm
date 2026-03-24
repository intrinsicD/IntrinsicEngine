module;
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>
#include <glm/glm.hpp>

export module Runtime.RenderExtraction;

import Graphics.Camera;
import Graphics.RenderPipeline;
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

    struct RenderViewPacket
    {
        Graphics::CameraComponent Camera{};
        glm::mat4 ViewMatrix{1.0f};
        glm::mat4 ProjectionMatrix{1.0f};
        glm::mat4 ViewProjectionMatrix{1.0f};
        glm::mat4 InverseViewMatrix{1.0f};
        glm::mat4 InverseProjectionMatrix{1.0f};
        glm::mat4 InverseViewProjectionMatrix{1.0f};
        glm::vec3 CameraPosition{0.0f};
        float NearPlane = 0.0f;
        glm::vec3 CameraForward{0.0f, 0.0f, -1.0f};
        float FarPlane = 0.0f;
        RenderViewport Viewport{};
        float AspectRatio = 1.0f;
        float VerticalFieldOfViewDegrees = 45.0f;

        [[nodiscard]] bool IsValid() const
        {
            return Viewport.IsValid();
        }
    };

    struct RenderFrameInput
    {
        double Alpha = 0.0;
        RenderViewPacket View{};
        WorldSnapshot World{};

        [[nodiscard]] bool IsValid() const
        {
            return World.IsValid() && View.IsValid();
        }
    };

    struct RenderWorld
    {
        double Alpha = 0.0;
        RenderViewPacket View{};
        WorldSnapshot World{};
        std::vector<Graphics::PickingSurfacePacket> SurfacePicking{};
        std::vector<Graphics::PickingLinePacket> LinePicking{};
        std::vector<Graphics::PickingPointPacket> PointPicking{};

        [[nodiscard]] bool IsValid() const
        {
            return World.IsValid() && View.IsValid();
        }
    };

    struct FrameContext
    {
        uint64_t FrameNumber = 0;
        uint64_t PreviousFrameNumber = InvalidFrameNumber;
        uint64_t LastSubmittedTimelineValue = 0;
        uint32_t SlotIndex = 0;
        uint32_t FramesInFlight = DefaultFrameContexts;
        RenderViewport Viewport{};
        std::optional<RenderWorld> PreparedRenderWorld{};
        bool Prepared = false;
        bool Submitted = false;
        bool ReusedSubmittedSlot = false;

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

    [[nodiscard]] RenderViewPacket MakeRenderViewPacket(const Graphics::CameraComponent& camera,
                                                        RenderViewport viewport);

    [[nodiscard]] RenderWorld ExtractRenderWorld(const RenderFrameInput& input);
}
