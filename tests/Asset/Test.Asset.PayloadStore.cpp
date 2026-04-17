#include <gtest/gtest.h>
#include <string>
 
import Extrinsic.Asset.PayloadStore;
import Extrinsic.Asset.Registry;
import Extrinsic.Core.Error;
 
using namespace Extrinsic::Assets;
using Extrinsic::Core::ErrorCode;
 
namespace
{
    struct Mesh
    {
        int triangles = 0;
        bool operator==(const Mesh&) const = default;
    };
 
    struct Texture
    {
        std::string name;
        bool operator==(const Texture&) const = default;
    };
 
    constexpr AssetId MakeId(uint32_t idx, uint32_t gen = 1u)
    {
        return AssetId{idx, gen};
    }
}
 
TEST(AssetPayloadStore, PublishAssignsTicket)
{
    AssetPayloadStore store;
    auto ticket = store.Publish(MakeId(1u), Mesh{.triangles = 12});
    ASSERT_TRUE(ticket.has_value());
    EXPECT_TRUE(ticket->IsValid());
    EXPECT_EQ(ticket->generation, 1u);
}
 
TEST(AssetPayloadStore, PublishRejectsInvalidId)
{
    AssetPayloadStore store;
    auto ticket = store.Publish(AssetId{}, Mesh{});
    ASSERT_FALSE(ticket.has_value());
    EXPECT_EQ(ticket.error(), ErrorCode::InvalidArgument);
}
 
TEST(AssetPayloadStore, ReadSpanReturnsStoredValue)
{
    AssetPayloadStore store;
    (void)store.Publish(MakeId(1u), Mesh{.triangles = 12});
 
    auto span = store.ReadSpan<Mesh>(MakeId(1u));
    ASSERT_TRUE(span.has_value());
    ASSERT_EQ(span->size(), 1u);
    EXPECT_EQ((*span)[0].triangles, 12);
}
 
TEST(AssetPayloadStore, ReadSpanMissingIdReturnsAssetNotLoaded)
{
    AssetPayloadStore store;
    auto r = store.ReadSpan<Mesh>(MakeId(42u));
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), ErrorCode::AssetNotLoaded);
}
 
TEST(AssetPayloadStore, ReadSpanWrongTypeIsRejected)
{
    AssetPayloadStore store;
    (void)store.Publish(MakeId(1u), Mesh{.triangles = 7});
 
    auto r = store.ReadSpan<Texture>(MakeId(1u));
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), ErrorCode::TypeMismatch);
}
 
TEST(AssetPayloadStore, PublishTwiceBumpsGenerationAndReplacesValue)
{
    AssetPayloadStore store;
    auto t1 = store.Publish(MakeId(1u), Mesh{.triangles = 1}).value();
    auto t2 = store.Publish(MakeId(1u), Mesh{.triangles = 2}).value();
 
    EXPECT_EQ(t1.slot, t2.slot);
    EXPECT_EQ(t2.generation, t1.generation + 1);
 
    auto span = store.ReadSpan<Mesh>(MakeId(1u)).value();
    EXPECT_EQ(span[0].triangles, 2);
}
 
TEST(AssetPayloadStore, RepublishDifferentTypeClearsOldBucket)
{
    AssetPayloadStore store;
    (void)store.Publish(MakeId(1u), Mesh{.triangles = 1});
    (void)store.Publish(MakeId(1u), Texture{.name = "tex"});
 
    // Old typed read must fail cleanly.
    auto old = store.ReadSpan<Mesh>(MakeId(1u));
    ASSERT_FALSE(old.has_value());
    EXPECT_EQ(old.error(), ErrorCode::TypeMismatch);
 
    // New type is visible.
    auto now = store.ReadSpan<Texture>(MakeId(1u)).value();
    EXPECT_EQ(now[0].name, "tex");
}
 
TEST(AssetPayloadStore, RetireRemovesEntry)
{
    AssetPayloadStore store;
    (void)store.Publish(MakeId(1u), Mesh{.triangles = 3});
    ASSERT_TRUE(store.Retire(MakeId(1u)).has_value());
 
    auto span = store.ReadSpan<Mesh>(MakeId(1u));
    ASSERT_FALSE(span.has_value());
    EXPECT_EQ(span.error(), ErrorCode::AssetNotLoaded);
}
 
TEST(AssetPayloadStore, RetireUnknownIdIsNotFound)
{
    AssetPayloadStore store;
    auto r = store.Retire(MakeId(1u));
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), ErrorCode::ResourceNotFound);
}
 
TEST(AssetPayloadStore, GetTicketMatchesPublish)
{
    AssetPayloadStore store;
    auto published = store.Publish(MakeId(1u), Mesh{}).value();
    auto fetched = store.GetTicket(MakeId(1u)).value();
    EXPECT_EQ(fetched, published);
}
 
TEST(AssetPayloadStore, GetTicketUnknownIsAssetNotLoaded)
{
    AssetPayloadStore store;
    auto t = store.GetTicket(MakeId(1u));
    ASSERT_FALSE(t.has_value());
    EXPECT_EQ(t.error(), ErrorCode::AssetNotLoaded);
}
 
TEST(AssetPayloadStore, DistinctSlotsPerAsset)
{
    AssetPayloadStore store;
    auto a = store.Publish(MakeId(1u), Mesh{.triangles = 1}).value();
    auto b = store.Publish(MakeId(2u), Mesh{.triangles = 2}).value();
    EXPECT_NE(a.slot, b.slot);
}
 
TEST(AssetPayloadStore, SizeTracksPublishRetire)
{
    AssetPayloadStore store;
    EXPECT_EQ(store.Size(), 0u);
    (void)store.Publish(MakeId(1u), Mesh{});
    (void)store.Publish(MakeId(2u), Texture{});
    EXPECT_EQ(store.Size(), 2u);
    (void)store.Retire(MakeId(1u));
    EXPECT_EQ(store.Size(), 1u);
}
 
// -----------------------------------------------------------------------------
// PayloadTicket struct tests
// -----------------------------------------------------------------------------
 
TEST(PayloadTicket, DefaultTicketIsInvalid)
{
    PayloadTicket t;
    EXPECT_FALSE(t.IsValid());
}
 
TEST(PayloadTicket, NonzeroSlotIsValid)
{
    PayloadTicket t{1u, 1u};
    EXPECT_TRUE(t.IsValid());
}
 
TEST(PayloadTicket, ComparesByValue)
{
    PayloadTicket a{1u, 1u};
    PayloadTicket b{1u, 1u};
    PayloadTicket c{2u, 1u};
    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
}