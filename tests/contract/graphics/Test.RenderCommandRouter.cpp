#include <gtest/gtest.h>

#include <cstdint>
#include <string_view>

import Extrinsic.Graphics.FrameRecipe;
import Extrinsic.Graphics.RenderCommandRouter;
import Extrinsic.Graphics.RenderGraph;
import Extrinsic.RHI.CommandContext;

#include "MockRHI.hpp"

namespace
{
    using namespace Extrinsic;
    using namespace Extrinsic::Graphics;
}

TEST(RenderCommandRouter, DispatchUsesTypedPassIdNotDebugName)
{
    Tests::MockCommandContext cmd;
    RenderCommandRouter router;

    std::uint32_t callCount = 0u;
    std::string_view observedDebugName{};
    router.Register(ToFramePassId(FrameRecipePassKind::Surface),
                    [&](const RenderCommandRoute& route,
                        RHI::ICommandContext&,
                        void* context) {
                        ++*static_cast<std::uint32_t*>(context);
                        observedDebugName = route.DebugName;
                    });

    EXPECT_TRUE(router.HasRoute(ToFramePassId(FrameRecipePassKind::Surface)));
    EXPECT_EQ(router.RouteCount(), 1u);
    EXPECT_TRUE(router.Dispatch(RenderCommandRoute{
                                    .PassId = ToFramePassId(FrameRecipePassKind::Surface),
                                    .DebugName = "RenamedSurfacePass",
                                },
                                cmd,
                                &callCount));
    EXPECT_EQ(callCount, 1u);
    EXPECT_EQ(observedDebugName, std::string_view{"RenamedSurfacePass"});

    callCount = 0u;
    observedDebugName = {};
    EXPECT_FALSE(router.Dispatch(RenderCommandRoute{
                                     .PassId = ToFramePassId(FrameRecipePassKind::Present),
                                     .DebugName = "SurfacePass",
                                 },
                                 cmd,
                                 &callCount));
    EXPECT_EQ(callCount, 0u);
    EXPECT_TRUE(observedDebugName.empty());
}

TEST(RenderCommandRouter, UnknownRoutesExposeSkippedUnavailableFallback)
{
    Tests::MockCommandContext cmd;
    RenderCommandRouter router;

    EXPECT_FALSE(router.Dispatch(RenderCommandRoute{
                                     .PassId = FramePassId{9999u},
                                     .DebugName = "FuturePass",
                                 },
                                 cmd));
    EXPECT_EQ(MissingRenderCommandRouteStatus(true),
              RenderCommandPassStatus::SkippedUnavailable);
    EXPECT_EQ(MissingRenderCommandRouteStatus(false),
              RenderCommandPassStatus::SkippedNonOperational);
}

TEST(RenderCommandRouter, DuplicateRegistrationReplacesRecorder)
{
    Tests::MockCommandContext cmd;
    RenderCommandRouter router;

    std::uint32_t routeValue = 0u;
    const FramePassId passId = ToFramePassId(FrameRecipePassKind::DepthPrepass);
    router.Register(passId,
                    [&](const RenderCommandRoute&, RHI::ICommandContext&, void*) {
                        routeValue = 1u;
                    });
    router.Register(passId,
                    [&](const RenderCommandRoute&, RHI::ICommandContext&, void*) {
                        routeValue = 2u;
                    });

    EXPECT_EQ(router.RouteCount(), 1u);
    EXPECT_TRUE(router.Dispatch(RenderCommandRoute{
                                    .PassId = passId,
                                    .DebugName = "DepthPrepass",
                                },
                                cmd));
    EXPECT_EQ(routeValue, 2u);
}
