#include <gtest/gtest.h>
#include <atomic>
#include <cstdlib>
#include <expected>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
 
import Extrinsic.Asset.Service;
import Extrinsic.Asset.Registry;
import Extrinsic.Asset.EventBus;
import Extrinsic.Core.Error;
 
using namespace Extrinsic::Assets;
using Extrinsic::Core::ErrorCode;
using Extrinsic::Core::Expected;
 
namespace
{
    struct Mesh
    {
        int triangles = 0;
    };
 
    struct Texture
    {
        std::string name;
    };
 
    // Write a small marker file to /tmp so that Core::Filesystem::GetAbsolutePath
    // has something to canonicalize. Tests do not depend on the file contents -
    // the loader synthesises the payload.
    struct TmpFile
    {
        std::filesystem::path path;
        TmpFile(std::string_view name)
        {
            path = std::filesystem::temp_directory_path() / name;
            std::ofstream os(path);
            os << "test";
        }
        ~TmpFile()
        {
            std::error_code ec;
            std::filesystem::remove(path, ec);
        }
    };
 
    auto MeshLoader(int triangles)
    {
        return [triangles](std::string_view, AssetId) -> Expected<Mesh>
        {
            return Mesh{.triangles = triangles};
        };
    }
 
    auto FailingLoader(ErrorCode err)
    {
        return [err](std::string_view, AssetId) -> Expected<Mesh>
        {
            return std::unexpected(err);
        };
    }
}
 
// -----------------------------------------------------------------------------
// Load
// -----------------------------------------------------------------------------
 
TEST(AssetService, LoadRejectsEmptyPath)
{
    AssetService svc;
    auto r = svc.Load<Mesh>("", MeshLoader(1));
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), ErrorCode::InvalidArgument);
}
 
TEST(AssetService, LoadReturnsValidIdOnSuccess)
{
    TmpFile f("svc_load_success.bin");
    AssetService svc;
    auto r = svc.Load<Mesh>(f.path.string(), MeshLoader(10));
    ASSERT_TRUE(r.has_value());
    EXPECT_TRUE(r->IsValid());
    EXPECT_TRUE(svc.IsAlive(*r));
}
 
TEST(AssetService, LoadSamePathReturnsSameId)
{
    TmpFile f("svc_load_same.bin");
    AssetService svc;
    auto a = svc.Load<Mesh>(f.path.string(), MeshLoader(1)).value();
    auto b = svc.Load<Mesh>(f.path.string(), MeshLoader(99)).value();
    EXPECT_EQ(a, b);
}
 
TEST(AssetService, LoadDifferentPathsReturnDistinctIds)
{
    TmpFile f1("svc_load_distinct_a.bin");
    TmpFile f2("svc_load_distinct_b.bin");
    AssetService svc;
    auto a = svc.Load<Mesh>(f1.path.string(), MeshLoader(1)).value();
    auto b = svc.Load<Mesh>(f2.path.string(), MeshLoader(2)).value();
    EXPECT_NE(a, b);
}
 
TEST(AssetService, LoaderFailurePropagatesErrorAndMarksFailed)
{
    TmpFile f("svc_load_fail.bin");
    AssetService svc;
    auto r = svc.Load<Mesh>(f.path.string(), FailingLoader(ErrorCode::AssetDecodeFailed));
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), ErrorCode::AssetDecodeFailed);
}
 
// -----------------------------------------------------------------------------
// Read
// -----------------------------------------------------------------------------
 
TEST(AssetService, ReadReturnsLoadedPayload)
{
    TmpFile f("svc_read_ok.bin");
    AssetService svc;
    auto id = svc.Load<Mesh>(f.path.string(), MeshLoader(42)).value();
    auto span = svc.Read<Mesh>(id);
    ASSERT_TRUE(span.has_value());
    ASSERT_EQ(span->size(), 1u);
    EXPECT_EQ((*span)[0].triangles, 42);
}
 
TEST(AssetService, ReadDeadHandleReturnsResourceNotFound)
{
    AssetService svc;
    auto r = svc.Read<Mesh>(AssetId{});
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), ErrorCode::ResourceNotFound);
}
 
TEST(AssetService, ReadWrongTypeRejected)
{
    TmpFile f("svc_read_wrongtype.bin");
    AssetService svc;
    auto id = svc.Load<Mesh>(f.path.string(), MeshLoader(1)).value();
    auto wrong = svc.Read<Texture>(id);
    ASSERT_FALSE(wrong.has_value());
    EXPECT_EQ(wrong.error(), ErrorCode::TypeMismatch);
}
 
// -----------------------------------------------------------------------------
// Metadata / paths
// -----------------------------------------------------------------------------
 
TEST(AssetService, GetMetaReturnsTypeIdAfterLoad)
{
    TmpFile f("svc_meta.bin");
    AssetService svc;
    auto id = svc.Load<Mesh>(f.path.string(), MeshLoader(1)).value();
    auto meta = svc.GetMeta(id);
    ASSERT_TRUE(meta.has_value());
    EXPECT_EQ(meta->typeId, AssetService::TypeIdOf<Mesh>());
}
 
TEST(AssetService, TypeIdOfIsStableAcrossCalls)
{
    EXPECT_EQ(AssetService::TypeIdOf<Mesh>(), AssetService::TypeIdOf<Mesh>());
    EXPECT_NE(AssetService::TypeIdOf<Mesh>(), AssetService::TypeIdOf<Texture>());
}
 
TEST(AssetService, GetPathRoundTrip)
{
    TmpFile f("svc_getpath.bin");
    AssetService svc;
    auto id = svc.Load<Mesh>(f.path.string(), MeshLoader(1)).value();
    auto path = svc.GetPath(id);
    ASSERT_TRUE(path.has_value());
    EXPECT_FALSE(path->empty());
}
 
TEST(AssetService, GetPathUnknownIdIsNotFound)
{
    AssetService svc;
    auto r = svc.GetPath(AssetId{});
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), ErrorCode::ResourceNotFound);
}
 
// -----------------------------------------------------------------------------
// Reload
// -----------------------------------------------------------------------------
 
TEST(AssetService, ReloadReplacesPayload)
{
    TmpFile f("svc_reload.bin");
    AssetService svc;
    auto id = svc.Load<Mesh>(f.path.string(), MeshLoader(1)).value();
    ASSERT_TRUE(svc.Reload<Mesh>(id, MeshLoader(999)).has_value());
 
    auto span = svc.Read<Mesh>(id).value();
    EXPECT_EQ(span[0].triangles, 999);
}
 
TEST(AssetService, ReloadUnknownIdReturnsNotFound)
{
    AssetService svc;
    auto r = svc.Reload<Mesh>(AssetId{}, MeshLoader(1));
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), ErrorCode::ResourceNotFound);
}
 
TEST(AssetService, ReloadWrongTypeRejected)
{
    TmpFile f("svc_reload_wrongtype.bin");
    AssetService svc;
    auto id = svc.Load<Mesh>(f.path.string(), MeshLoader(1)).value();
    auto r = svc.Reload<Texture>(id, [](std::string_view, AssetId) -> Expected<Texture>
    {
        return Texture{.name = "nope"};
    });
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), ErrorCode::TypeMismatch);
}
 
TEST(AssetService, ReloadFailedLoaderLeavesPreviousPayloadIntact)
{
    // Contract (post-B3 fix): if the Reload loader fails, the asset stays
    // in its prior Ready state with its old payload readable.
    TmpFile f("svc_reload_keeps_old.bin");
    AssetService svc;
    auto id = svc.Load<Mesh>(f.path.string(), MeshLoader(42)).value();
 
    auto r = svc.Reload<Mesh>(id, FailingLoader(ErrorCode::AssetDecodeFailed));
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), ErrorCode::AssetDecodeFailed);
 
    // Asset still alive, still Ready, old payload still readable.
    EXPECT_TRUE(svc.IsAlive(id));
    EXPECT_EQ(svc.GetMeta(id).value().state, AssetState::Ready);
    auto span = svc.Read<Mesh>(id).value();
    EXPECT_EQ(span[0].triangles, 42);
}
 
TEST(AssetService, ReloadRejectedFromNonReadyState)
{
    TmpFile f("svc_reload_nonready.bin");
    AssetService svc;
    auto id = svc.Load<Mesh>(f.path.string(), MeshLoader(1)).value();
    ASSERT_TRUE(svc.Registry().SetState(id, AssetState::Ready, AssetState::Failed).has_value());
 
    auto r = svc.Reload<Mesh>(id, MeshLoader(2));
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), ErrorCode::InvalidState);
}

// -----------------------------------------------------------------------------
// Captured loader + reload token
// -----------------------------------------------------------------------------

TEST(AssetService, GetReloadTokenReturnsValidTokenAfterLoad)
{
    TmpFile f("svc_token_after_load.bin");
    AssetService svc;
    auto id = svc.Load<Mesh>(f.path.string(), MeshLoader(1)).value();

    auto token = svc.GetReloadToken(id);
    ASSERT_TRUE(token.has_value());
    EXPECT_TRUE(token->IsValid());
    EXPECT_TRUE(svc.LoaderCallbacks().Contains(*token));
}

TEST(AssetService, GetReloadTokenUnknownIdReturnsNotFound)
{
    AssetService svc;
    auto r = svc.GetReloadToken(AssetId{});
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), ErrorCode::ResourceNotFound);
}

TEST(AssetService, ParameterlessReloadReinvokesCapturedLoader)
{
    // The captured loader is stateful (counter-based) so Reload(id) must
    // re-run it and the payload must reflect the next value produced by
    // the same loader object.
    TmpFile f("svc_reload_captured.bin");
    AssetService svc;

    auto counter = std::make_shared<int>(0);
    auto loader = [counter](std::string_view, AssetId) -> Expected<Mesh>
    {
        return Mesh{.triangles = ++(*counter)};
    };

    auto id = svc.Load<Mesh>(f.path.string(), loader).value();
    EXPECT_EQ(svc.Read<Mesh>(id).value()[0].triangles, 1);

    ASSERT_TRUE(svc.Reload(id).has_value());
    EXPECT_EQ(svc.Read<Mesh>(id).value()[0].triangles, 2);

    ASSERT_TRUE(svc.Reload(id).has_value());
    EXPECT_EQ(svc.Read<Mesh>(id).value()[0].triangles, 3);
}

TEST(AssetService, ParameterlessReloadOnUnknownIdReturnsNotFound)
{
    AssetService svc;
    auto r = svc.Reload(AssetId{});
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), ErrorCode::ResourceNotFound);
}

TEST(AssetService, ParameterlessReloadFailedLoaderKeepsPreviousPayload)
{
    TmpFile f("svc_reload_captured_keeps_old.bin");
    AssetService svc;

    auto shouldFail = std::make_shared<bool>(false);
    auto loader = [shouldFail](std::string_view, AssetId) -> Expected<Mesh>
    {
        if (*shouldFail) return std::unexpected(ErrorCode::AssetDecodeFailed);
        return Mesh{.triangles = 42};
    };

    auto id = svc.Load<Mesh>(f.path.string(), loader).value();

    *shouldFail = true;
    auto r = svc.Reload(id);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), ErrorCode::AssetDecodeFailed);

    // Old payload intact, asset still Ready.
    EXPECT_EQ(svc.GetMeta(id).value().state, AssetState::Ready);
    EXPECT_EQ(svc.Read<Mesh>(id).value()[0].triangles, 42);
}

TEST(AssetService, ParameterlessReloadRejectedFromNonReadyState)
{
    TmpFile f("svc_reload_captured_nonready.bin");
    AssetService svc;
    auto id = svc.Load<Mesh>(f.path.string(), MeshLoader(1)).value();

    ASSERT_TRUE(svc.Registry().SetState(id, AssetState::Ready, AssetState::Failed).has_value());
    auto r = svc.Reload(id);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), ErrorCode::InvalidState);
}

TEST(AssetService, DestroyUnregistersCapturedLoader)
{
    TmpFile f("svc_destroy_unregisters.bin");
    AssetService svc;

    auto id = svc.Load<Mesh>(f.path.string(), MeshLoader(1)).value();
    auto token = svc.GetReloadToken(id).value();
    ASSERT_TRUE(svc.LoaderCallbacks().Contains(token));

    ASSERT_TRUE(svc.Destroy(id).has_value());
    EXPECT_FALSE(svc.LoaderCallbacks().Contains(token));
    EXPECT_EQ(svc.LoaderCallbacks().Size(), 0u);

    // Token lookup on the destroyed id must now report not-found.
    auto r = svc.GetReloadToken(id);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), ErrorCode::ResourceNotFound);
}

TEST(AssetService, ReloadPublishesReloadedEvent)
{
    TmpFile f("svc_reload_event.bin");
    AssetService svc;
    std::atomic<int> reloaded{0};
    (void)svc.EventBus().SubscribeAll([&](AssetId, AssetEvent e)
    {
        if (e == AssetEvent::Reloaded) ++reloaded;
    });

    auto id = svc.Load<Mesh>(f.path.string(), MeshLoader(1)).value();
    ASSERT_TRUE(svc.Reload(id).has_value());
    svc.Tick();
    EXPECT_EQ(reloaded.load(), 1);
}

TEST(AssetService, ReloadTokenDirectlyInvokableViaRegistry)
{
    // Demonstrates the use case: a FileWatcher (or similar) could hold the
    // token and eventually drive AssetService::Reload(id). This test
    // verifies only that the token is observably live in the registry -
    // direct Invoke must NOT be used to bypass the state machine, but the
    // registry surface area is exposed for test / diagnostic inspection.
    TmpFile f("svc_reload_token_live.bin");
    AssetService svc;
    auto id = svc.Load<Mesh>(f.path.string(), MeshLoader(7)).value();
    auto token = svc.GetReloadToken(id).value();

    EXPECT_TRUE(svc.LoaderCallbacks().Contains(token));
    EXPECT_EQ(svc.LoaderCallbacks().Size(), 1u);
}
 
// -----------------------------------------------------------------------------
// Destroy
// -----------------------------------------------------------------------------
 
TEST(AssetService, DestroyUnknownIdReturnsNotFound)
{
    AssetService svc;
    auto r = svc.Destroy(AssetId{});
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), ErrorCode::ResourceNotFound);
}
 
TEST(AssetService, DestroyRemovesAssetPathPayload)
{
    TmpFile f("svc_destroy.bin");
    AssetService svc;
    auto id = svc.Load<Mesh>(f.path.string(), MeshLoader(1)).value();
    ASSERT_TRUE(svc.Destroy(id).has_value());
 
    EXPECT_FALSE(svc.IsAlive(id));
    auto span = svc.Read<Mesh>(id);
    ASSERT_FALSE(span.has_value());
 
    // Path is no longer known to the service.
    auto p = svc.GetPath(id);
    ASSERT_FALSE(p.has_value());
}
 
TEST(AssetService, DestroyThenLoadSamePathYieldsFreshId)
{
    TmpFile f("svc_destroy_reload.bin");
    AssetService svc;
    auto first = svc.Load<Mesh>(f.path.string(), MeshLoader(1)).value();
    ASSERT_TRUE(svc.Destroy(first).has_value());
    auto second = svc.Load<Mesh>(f.path.string(), MeshLoader(1)).value();
    EXPECT_NE(first, second);
    EXPECT_TRUE(svc.IsAlive(second));
}
 
// -----------------------------------------------------------------------------
// Tick / event bus integration
// -----------------------------------------------------------------------------
 
TEST(AssetService, TickFlushesEventBus)
{
    TmpFile f("svc_tick.bin");
    AssetService svc;
    std::atomic<int> destroyed{0};
    (void)svc.EventBus().SubscribeAll([&](AssetId, AssetEvent e)
    {
        if (e == AssetEvent::Destroyed) ++destroyed;
    });
 
    auto id = svc.Load<Mesh>(f.path.string(), MeshLoader(1)).value();
    ASSERT_TRUE(svc.Destroy(id).has_value());
    svc.Tick();
    EXPECT_EQ(destroyed.load(), 1);
}
 
TEST(AssetService, FailedLoadLeavesNoGhostAsset)
{
    // Contract (post-B1 fix): a failed loader must not leave behind a
    // Registry entry, a PathIndex row, or a payload. The caller can retry
    // Load() with a healthy loader and it must create a fresh asset.
    TmpFile f("svc_failed_no_ghost.bin");
    AssetService svc;
 
    auto failedResult = svc.Load<Mesh>(f.path.string(), FailingLoader(ErrorCode::AssetInvalidData));
    ASSERT_FALSE(failedResult.has_value());
 
    // Registry is empty, PathIndex has no entry for this path.
    EXPECT_EQ(svc.Registry().LiveCount(), 0u);
    EXPECT_FALSE(svc.PathIndex().Contains(std::filesystem::absolute(f.path).string()));
 
    // Retrying with a good loader must succeed.
    auto ok = svc.Load<Mesh>(f.path.string(), MeshLoader(7));
    ASSERT_TRUE(ok.has_value());
    auto span = svc.Read<Mesh>(*ok).value();
    EXPECT_EQ(span[0].triangles, 7);
}