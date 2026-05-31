#include <gtest/gtest.h>

#include <optional>

import Extrinsic.Graphics.SelectionSystem;
import Extrinsic.Graphics.Pass.Selection.EdgeId;
import Extrinsic.Graphics.Pass.Selection.EntityId;
import Extrinsic.Graphics.Pass.Selection.FaceId;
import Extrinsic.Graphics.Pass.Selection.Outline;
import Extrinsic.Graphics.Pass.Selection.PointId;

using namespace Extrinsic;

TEST(GraphicsSelectionSystemContracts, EncodesPrimitiveDomainAndPayloadDeterministically)
{
    constexpr auto face = Graphics::EncodeSelectionId(Graphics::SelectionPrimitiveDomain::Face, 0x00ABCDEFu);
    static_assert(face.IsHit());
    static_assert(face.Domain() == Graphics::SelectionPrimitiveDomain::Face);
    static_assert(face.Payload() == 0x00ABCDEFu);

    constexpr auto masked = Graphics::EncodeSelectionId(Graphics::SelectionPrimitiveDomain::Point, 0xFFFFFFFFu);
    static_assert(masked.Domain() == Graphics::SelectionPrimitiveDomain::Point);
    static_assert(masked.Payload() == Graphics::EncodedSelectionId::PayloadMask);

    EXPECT_EQ(face.Value, 0x20ABCDEFu);
    EXPECT_EQ(masked.Value, 0x4FFFFFFFu);
}

TEST(GraphicsSelectionSystemContracts, TracksPendingPickReadbackAndDiagnostics)
{
    Graphics::SelectionSystem selection;
    selection.Initialize();
    ASSERT_TRUE(selection.IsInitialized());

    selection.RequestPick(Graphics::PickRequest{.PixelX = 10u, .PixelY = 20u});
    EXPECT_TRUE(selection.HasPendingPick());
    const std::optional<Graphics::PickRequest> consumed = selection.ConsumePick();
    ASSERT_TRUE(consumed.has_value());
    EXPECT_EQ(consumed->PixelX, 10u);
    EXPECT_EQ(consumed->PixelY, 20u);
    EXPECT_FALSE(selection.HasPendingPick());

    selection.PublishPickResult(Graphics::PickReadbackResult{
        .EncodedId = Graphics::EncodeSelectionId(Graphics::SelectionPrimitiveDomain::Edge, 42u),
        .StableEntityId = 77u,
        .Hit = true,
    });
    const auto hit = selection.GetLastPickResult();
    ASSERT_TRUE(hit.has_value());
    EXPECT_TRUE(hit->Hit);
    EXPECT_EQ(hit->EncodedId.Domain(), Graphics::SelectionPrimitiveDomain::Edge);
    EXPECT_EQ(hit->EncodedId.Payload(), 42u);
    EXPECT_EQ(hit->StableEntityId, 77u);

    selection.PublishNoHit();
    const auto noHit = selection.GetLastPickResult();
    ASSERT_TRUE(noHit.has_value());
    EXPECT_FALSE(noHit->Hit);
    EXPECT_FALSE(noHit->EncodedId.IsHit());

    const auto diagnostics = selection.GetDiagnostics();
    EXPECT_EQ(diagnostics.PickRequestCount, 1u);
    EXPECT_EQ(diagnostics.PickConsumeCount, 1u);
    EXPECT_EQ(diagnostics.PickHitCount, 1u);
    EXPECT_EQ(diagnostics.PickNoHitCount, 1u);

    selection.Shutdown();
    EXPECT_FALSE(selection.IsInitialized());
}

// RUNTIME-089 — several picking slots can complete in one BeginFrame() drain,
// so completed readbacks are held in a FIFO queue (not a single last-result
// holder that drops all but the newest). PopPickResult drains them oldest-first
// with their correlation Sequence intact; GetLastPickResult stays a
// non-destructive peek at the most recent result.
TEST(GraphicsSelectionSystemContracts, QueuesCompletedReadbacksFifoWithSequence)
{
    Graphics::SelectionSystem selection;
    selection.Initialize();

    selection.PublishPickResult(Graphics::PickReadbackResult{
        .EncodedId = Graphics::EncodeSelectionId(Graphics::SelectionPrimitiveDomain::Entity, 1u),
        .StableEntityId = 11u,
        .Hit = true,
        .Sequence = 101u,
    });
    selection.PublishPickResult(Graphics::PickReadbackResult{
        .StableEntityId = 0u,
        .Hit = false,
        .Sequence = 202u,
    });
    selection.PublishPickResult(Graphics::PickReadbackResult{
        .EncodedId = Graphics::EncodeSelectionId(Graphics::SelectionPrimitiveDomain::Entity, 3u),
        .StableEntityId = 33u,
        .Hit = true,
        .Sequence = 303u,
    });

    // GetLastPickResult peeks the most recent without consuming.
    const auto peek = selection.GetLastPickResult();
    ASSERT_TRUE(peek.has_value());
    EXPECT_EQ(peek->Sequence, 303u);
    EXPECT_EQ(peek->StableEntityId, 33u);

    // PopPickResult drains in FIFO (publish) order — nothing dropped.
    const auto first = selection.PopPickResult();
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(first->Sequence, 101u);
    EXPECT_TRUE(first->Hit);
    EXPECT_EQ(first->StableEntityId, 11u);

    const auto second = selection.PopPickResult();
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(second->Sequence, 202u);
    EXPECT_FALSE(second->Hit);

    const auto third = selection.PopPickResult();
    ASSERT_TRUE(third.has_value());
    EXPECT_EQ(third->Sequence, 303u);
    EXPECT_TRUE(third->Hit);
    EXPECT_EQ(third->StableEntityId, 33u);

    EXPECT_FALSE(selection.PopPickResult().has_value());
    EXPECT_FALSE(selection.GetLastPickResult().has_value());

    // Diagnostics count every publish (2 hits + 1 no-hit), independent of drain.
    const auto diagnostics = selection.GetDiagnostics();
    EXPECT_EQ(diagnostics.PickHitCount, 2u);
    EXPECT_EQ(diagnostics.PickNoHitCount, 1u);

    selection.Shutdown();
}

TEST(GraphicsSelectionSystemContracts, PreservesPointPickCompatibilityWrappers)
{
    Graphics::SelectionSystem selection;
    selection.Initialize();

    selection.RequestPointIdPick(Graphics::PointSelectionRequest{.PixelX = 3u, .PixelY = 5u});
    EXPECT_TRUE(selection.HasPendingPointIdPick());
    const auto request = selection.ConsumePointIdPick();
    ASSERT_TRUE(request.has_value());
    EXPECT_EQ(request->PixelX, 3u);
    EXPECT_EQ(request->PixelY, 5u);

    selection.PublishPointIdResult(Graphics::PointSelectionResult{.PointID = 12u, .EntityID = 34u});
    const auto result = selection.GetLastPointIdResult();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->PointID, 12u);
    EXPECT_EQ(result->EntityID, 34u);
}

TEST(GraphicsSelectionSystemContracts, PromotedSelectionPassClassNamesRemainAvailable)
{
    Graphics::SelectionSystem selection;
    selection.Initialize();

    Graphics::EntityIdPass entityPass{selection};
    Graphics::FaceIdPass facePass{selection};
    Graphics::EdgeIdPass edgePass{selection};
    Graphics::PointIdPass pointPass{selection};
    Graphics::SelectionOutlinePass outlinePass{selection};
    Graphics::SelectionEntityIdPass legacyAlias{selection};

    (void)entityPass;
    (void)facePass;
    (void)edgePass;
    (void)pointPass;
    (void)outlinePass;
    (void)legacyAlias;
}

