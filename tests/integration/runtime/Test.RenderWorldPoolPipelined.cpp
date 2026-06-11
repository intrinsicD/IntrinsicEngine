#include <cstdint>
#include <memory>

#include <entt/entity/entity.hpp>
#include <gtest/gtest.h>
#include <glm/glm.hpp>

import Extrinsic.ECS.Component.Transform.WorldMatrix;
import Extrinsic.ECS.Component.ProceduralGeometryRef;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Core.Config.Render;
import Extrinsic.Graphics.Component.RenderGeometry;
import Extrinsic.Graphics.RenderFrameInput;
import Extrinsic.Graphics.RenderWorld;
import Extrinsic.Graphics.Renderer;
import Extrinsic.RHI.FrameHandle;
import Extrinsic.Runtime.RenderExtraction;
import Extrinsic.Runtime.RenderWorldPool;
import Extrinsic.Runtime.StableEntityLookup;

#include "MockRHI.hpp"

using namespace Extrinsic;

namespace
{
    constexpr std::uint32_t kInvalid = Runtime::RenderWorldPool::kInvalidSlot;

    struct RendererFixture
    {
        Tests::MockDevice Device{};
        std::unique_ptr<Graphics::IRenderer> Renderer{Graphics::CreateRenderer()};
        Runtime::RenderExtractionCache Extraction{};

        RendererFixture()
        {
            Renderer->Initialize(Device);
        }

        ~RendererFixture()
        {
            Extraction.Shutdown(*Renderer);
            Renderer->Shutdown();
        }
    };

    [[nodiscard]] std::uint32_t StableId(const entt::entity entity) noexcept
    {
        return Extrinsic::Runtime::StableEntityLookup::ToRenderId(entity);
    }

    void WriteFrameMarker(ECS::Components::Transform::WorldMatrix& world,
                          const std::uint64_t frameIndex)
    {
        world.Matrix = glm::mat4{1.f};
        world.Matrix[3] = glm::vec4{
            static_cast<float>(frameIndex),
            100.f + static_cast<float>(frameIndex),
            0.f,
            1.f,
        };
    }
}

TEST(RenderWorldPoolPipelined, ConsumesRenderNMinusOneWhileExtractionWritesN)
{
    RendererFixture fixture;
    Core::Config::RenderConfig renderConfig{};
    renderConfig.SynchronousExtraction = false;
    Runtime::RenderWorldPool pool(
        renderConfig.SynchronousExtraction ? 1u : Runtime::RenderWorldPool::kDefaultBuffers);
    ASSERT_FALSE(renderConfig.SynchronousExtraction);
    ASSERT_FALSE(pool.IsSynchronous());
    ECS::Scene::Registry scene;
    auto& registry = scene.Raw();

    const entt::entity entity = scene.Create();
    auto& world = registry.emplace<ECS::Components::Transform::WorldMatrix>(entity);
    registry.emplace<Graphics::Components::RenderSurface>(entity);
    registry.emplace<ECS::Components::ProceduralGeometryRef>(entity);

    std::uint32_t previousPublishedFront = kInvalid;
    Runtime::RuntimeRenderExtractionStats stats{};

    constexpr std::uint64_t kFrameCount = 5u;
    for (std::uint64_t frameIndex = 0u; frameIndex < kFrameCount; ++frameIndex)
    {
        WriteFrameMarker(world, frameIndex);

        RHI::FrameHandle frame{};
        ASSERT_TRUE(fixture.Renderer->BeginFrame(frame)) << "frame " << frameIndex;

        const std::uint32_t backSlot = pool.AcquireBack(frameIndex);
        ASSERT_NE(backSlot, kInvalid) << "frame " << frameIndex;

        stats = fixture.Extraction.ExtractAndSubmit(
            scene,
            *fixture.Renderer,
            nullptr,
            nullptr,
            backSlot);
        ASSERT_EQ(stats.SubmittedTransformCount, 1u) << "frame " << frameIndex;

        pool.PublishFront(backSlot);

        const std::uint32_t frontSlot = pool.AcquirePreviousFront(frameIndex);
        ASSERT_NE(frontSlot, kInvalid) << "frame " << frameIndex;
        if (frameIndex == 0u)
            EXPECT_EQ(frontSlot, backSlot) << "bootstrap reads current frame";
        else
            EXPECT_EQ(frontSlot, previousPublishedFront) << "frame " << frameIndex;

        const Graphics::RenderWorld renderWorld =
            fixture.Renderer->ExtractRenderWorld(Graphics::RenderFrameInput{}, frontSlot);

        ASSERT_EQ(renderWorld.Renderables.size(), 1u) << "frame " << frameIndex;
        const Graphics::RenderableSnapshot& renderable = renderWorld.Renderables.front();
        EXPECT_EQ(renderable.StableId, StableId(entity));

        const float expectedFrameMarker =
            frameIndex == 0u ? 0.f : static_cast<float>(frameIndex - 1u);
        EXPECT_FLOAT_EQ(renderable.Model[3].x, expectedFrameMarker) << "frame " << frameIndex;
        EXPECT_FLOAT_EQ(renderable.Model[3].y, 100.f + expectedFrameMarker) << "frame " << frameIndex;

        Runtime::MirrorRenderWorldPoolDiagnostics(pool, stats);
        EXPECT_EQ(stats.RenderWorldPipelineStallCount, 0u);
        EXPECT_EQ(stats.RenderWorldExtractionSkipCount, 0u);
        if (frameIndex == 0u)
            EXPECT_EQ(stats.RenderWorldFrameAgeFrames, 0u);
        else
            EXPECT_EQ(stats.RenderWorldFrameAgeFrames, 1u);

        pool.ReleaseFront(frontSlot);
        EXPECT_EQ(pool.RefCount(frontSlot), 0u) << "frame " << frameIndex;

        previousPublishedFront = backSlot;
    }

    EXPECT_EQ(pool.GetDiagnostics().PipelineStallCount, 0u);
    EXPECT_EQ(pool.GetDiagnostics().ExtractionSkipCount, 0u);
    EXPECT_EQ(pool.GetDiagnostics().LastConsumedFrameAge, 1u);
}
