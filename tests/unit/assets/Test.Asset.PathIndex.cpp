#include <gtest/gtest.h>
#include <string>
 
import Extrinsic.Asset.PathIndex;
import Extrinsic.Asset.Registry;
import Extrinsic.Core.Error;
 
using namespace Extrinsic::Assets;
using Extrinsic::Core::ErrorCode;
 
namespace
{
    constexpr AssetId MakeId(uint32_t idx, uint32_t gen = 1u)
    {
        return AssetId{idx, gen};
    }
}
 
TEST(AssetPathIndex, EmptyFindReturnsNotFound)
{
    AssetPathIndex idx;
    auto r = idx.Find("/nonexistent/path");
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), ErrorCode::ResourceNotFound);
}
 
TEST(AssetPathIndex, InsertAndFindRoundTrip)
{
    AssetPathIndex idx;
    ASSERT_TRUE(idx.Insert("/a/b.png", MakeId(5u)).has_value());
    auto r = idx.Find("/a/b.png");
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->Index, 5u);
    EXPECT_EQ(r->Generation, 1u);
}
 
TEST(AssetPathIndex, InsertDuplicateRejected)
{
    AssetPathIndex idx;
    ASSERT_TRUE(idx.Insert("/a/b.png", MakeId(1u)).has_value());
    auto again = idx.Insert("/a/b.png", MakeId(2u));
    ASSERT_FALSE(again.has_value());
    EXPECT_EQ(again.error(), ErrorCode::ResourceBusy);
 
    // Original id must remain intact.
    auto r = idx.Find("/a/b.png").value();
    EXPECT_EQ(r.Index, 1u);
}
 
TEST(AssetPathIndex, InsertRejectsEmptyPath)
{
    AssetPathIndex idx;
    auto r = idx.Insert("", MakeId(1u));
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), ErrorCode::InvalidArgument);
}
 
TEST(AssetPathIndex, InsertRejectsInvalidId)
{
    AssetPathIndex idx;
    auto r = idx.Insert("/a/b", AssetId{});
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), ErrorCode::InvalidArgument);
}
 
TEST(AssetPathIndex, EraseRemovesEntry)
{
    AssetPathIndex idx;
    ASSERT_TRUE(idx.Insert("/p", MakeId(5u)).has_value());
    ASSERT_TRUE(idx.Erase("/p", MakeId(5u)).has_value());
    EXPECT_FALSE(idx.Contains("/p"));
}
 
TEST(AssetPathIndex, EraseUnknownPathNotFound)
{
    AssetPathIndex idx;
    auto r = idx.Erase("/missing", MakeId(1u));
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), ErrorCode::ResourceNotFound);
}
 
TEST(AssetPathIndex, EraseIdMismatchRejected)
{
    AssetPathIndex idx;
    ASSERT_TRUE(idx.Insert("/p", MakeId(1u)).has_value());
    auto r = idx.Erase("/p", MakeId(2u));
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), ErrorCode::InvalidArgument);
    // The entry is preserved since the id did not match.
    EXPECT_TRUE(idx.Contains("/p"));
}
 
TEST(AssetPathIndex, ContainsReflectsInsertErase)
{
    AssetPathIndex idx;
    EXPECT_FALSE(idx.Contains("/x"));
    ASSERT_TRUE(idx.Insert("/x", MakeId(1u)).has_value());
    EXPECT_TRUE(idx.Contains("/x"));
    ASSERT_TRUE(idx.Erase("/x", MakeId(1u)).has_value());
    EXPECT_FALSE(idx.Contains("/x"));
}
 
TEST(AssetPathIndex, SizeTracksInserts)
{
    AssetPathIndex idx;
    EXPECT_EQ(idx.Size(), 0u);
    ASSERT_TRUE(idx.Insert("/a", MakeId(1u)).has_value());
    ASSERT_TRUE(idx.Insert("/b", MakeId(2u)).has_value());
    EXPECT_EQ(idx.Size(), 2u);
    ASSERT_TRUE(idx.Erase("/a", MakeId(1u)).has_value());
    EXPECT_EQ(idx.Size(), 1u);
}
 
TEST(AssetPathIndex, HeterogeneousLookupNoAllocation)
{
    // Find/Contains/Erase accept std::string_view directly via transparent
    // heterogeneous lookup - no std::string allocation should be needed.
    AssetPathIndex idx;
    const std::string stored = "/heterogeneous/path.bin";
    ASSERT_TRUE(idx.Insert(stored, MakeId(3u)).has_value());
 
    std::string_view view = stored;
    EXPECT_TRUE(idx.Contains(view));
    EXPECT_TRUE(idx.Find(view).has_value());
    EXPECT_TRUE(idx.Erase(view, MakeId(3u)).has_value());
}