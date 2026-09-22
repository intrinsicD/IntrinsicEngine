#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <expected>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>
 
import Extrinsic.Asset.Service;
import Extrinsic.Asset.LoadPipeline;
import Extrinsic.Asset.Registry;
import Extrinsic.Asset.EventBus;
import Extrinsic.Asset.PayloadStore;
import Extrinsic.Core.Error;
import Extrinsic.Core.Tasks;
 
using namespace Extrinsic::Assets;
using Extrinsic::Core::ErrorCode;
using Extrinsic::Core::Expected;
namespace Core = Extrinsic::Core;
namespace Tasks = Extrinsic::Core::Tasks;
 
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

    class SchedulerScope
    {
    public:
        explicit SchedulerScope(const unsigned workerCount)
        {
            if (!Tasks::Scheduler::IsInitialized())
            {
                Tasks::Scheduler::Initialize(workerCount);
                m_Owned = true;
            }
        }

        ~SchedulerScope()
        {
            if (m_Owned)
            {
                Tasks::Scheduler::Shutdown();
            }
        }

        SchedulerScope(const SchedulerScope&) = delete;
        SchedulerScope& operator=(const SchedulerScope&) = delete;

        [[nodiscard]] bool Owned() const noexcept { return m_Owned; }

    private:
        bool m_Owned{false};
    };

    void WaitUntilTrue(const std::atomic<bool>& flag)
    {
        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (!flag.load(std::memory_order_acquire) &&
               std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    struct PausedCpuCompletion
    {
        std::mutex mutex;
        std::condition_variable changed;
        bool entered = false;
        bool released = false;
        bool completionStarted = false;
        bool completionReturned = false;

        void Pause()
        {
            std::unique_lock lock(mutex);
            entered = true;
            changed.notify_all();
            changed.wait(lock, [&] { return released; });
        }

        void Release()
        {
            std::scoped_lock lock(mutex);
            released = true;
            changed.notify_all();
        }
    };

    template <class Operation>
    Core::Result RunWhileWinnerPaused(const std::shared_ptr<PausedCpuCompletion>& pause,
                                     Operation operation)
    {
        bool returnedWhilePaused = false;
        std::thread controller([&]
        {
            std::unique_lock lock(pause->mutex);
            pause->changed.wait(lock, [&] { return pause->completionStarted; });
            // The winner is deterministically stopped inside its transition.
            // This bounded observation checks that completion cannot return
            // before release; it is not a budget for finishing the load.
            returnedWhilePaused = pause->changed.wait_for(
                lock, std::chrono::milliseconds(100),
                [&] { return pause->completionReturned; });
            pause->released = true;
            pause->changed.notify_all();
        });

        {
            std::scoped_lock lock(pause->mutex);
            pause->completionStarted = true;
            pause->changed.notify_all();
        }
        const auto completed = operation();
        {
            std::scoped_lock lock(pause->mutex);
            pause->completionReturned = true;
            pause->changed.notify_all();
        }
        controller.join();

        EXPECT_FALSE(returnedWhilePaused);
        return completed;
    }

    void VerifyCompleteCpuLoadJoinsWinner(const bool pauseBeforeReadyEvent)
    {
        ASSERT_FALSE(Tasks::Scheduler::IsInitialized());
        SchedulerScope scheduler(1u);
        ASSERT_TRUE(scheduler.Owned());

        auto pause = std::make_shared<PausedCpuCompletion>();
        AssetLoadPipelineTestHooks hooks;
        auto pauseWinner = [pause](AssetId) { pause->Pause(); };
        if (pauseBeforeReadyEvent)
            hooks.BeforeCpuReadyEvent = pauseWinner;
        else
            hooks.AfterCpuDecodeClaim = pauseWinner;

        AssetService svc(std::move(hooks));
        TmpFile file(pauseBeforeReadyEvent
            ? "svc_complete_paused_ready_event.bin"
            : "svc_complete_paused_decode_claim.bin");
        std::vector<AssetEvent> events;
        (void)svc.SubscribeAll([&](AssetId, AssetEvent event)
        {
            events.push_back(event);
        });

        auto id = svc.Load<Mesh>(file.path.string(), MeshLoader(10));
        if (!id.has_value())
            pause->Release();
        ASSERT_TRUE(id.has_value());

        bool entered = false;
        {
            std::unique_lock lock(pause->mutex);
            entered = pause->changed.wait_for(lock, std::chrono::seconds(2),
                [&] { return pause->entered; });
        }
        if (!entered)
            pause->Release();
        ASSERT_TRUE(entered);

        const auto completed = RunWhileWinnerPaused(pause, [&]
        {
            return svc.CompleteCpuLoadAndFlushEvent(*id);
        });
        ASSERT_TRUE(completed.has_value());
        EXPECT_EQ(svc.GetMeta(*id).value().state, AssetState::Ready);
        ASSERT_EQ(events.size(), 1u);
        EXPECT_EQ(events.front(), AssetEvent::Ready);
        svc.Tick();
        EXPECT_EQ(events.size(), 1u);
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

TEST(AssetService, CompleteCpuLoadAndFlushEventIgnoresUnrelatedSchedulerWork)
{
    ASSERT_FALSE(Tasks::Scheduler::IsInitialized());
    SchedulerScope scheduler(1u);
    ASSERT_TRUE(scheduler.Owned());

    std::atomic<bool> blockerStarted{false};
    std::atomic<bool> blockerFinished{false};
    Tasks::Scheduler::Dispatch([&]()
    {
        blockerStarted.store(true, std::memory_order_release);
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        blockerFinished.store(true, std::memory_order_release);
    });
    WaitUntilTrue(blockerStarted);
    ASSERT_TRUE(blockerStarted.load(std::memory_order_acquire));

    TmpFile f("svc_targeted_cpu_load_flush.bin");
    AssetService svc;
    std::vector<AssetEvent> events;
    (void)svc.SubscribeAll([&](AssetId observed, AssetEvent event)
    {
        if (!events.empty())
        {
            return;
        }
        events.push_back(event);
        EXPECT_TRUE(observed.IsValid());
    });

    auto id = svc.Load<Mesh>(f.path.string(), MeshLoader(10));
    ASSERT_TRUE(id.has_value());

    const auto before = std::chrono::steady_clock::now();
    Core::Result completed = svc.CompleteCpuLoadAndFlushEvent(*id);
    const auto after = std::chrono::steady_clock::now();

    ASSERT_TRUE(completed.has_value());
    EXPECT_EQ(svc.GetMeta(*id).value().state, AssetState::Ready);
    EXPECT_EQ(svc.Read<Mesh>(*id).value()[0].triangles, 10);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0], AssetEvent::Ready);
    EXPECT_FALSE(blockerFinished.load(std::memory_order_acquire));
    EXPECT_LT(after - before, std::chrono::milliseconds(150));

    WaitUntilTrue(blockerFinished);
    EXPECT_TRUE(blockerFinished.load(std::memory_order_acquire));
}
 
TEST(AssetService, CompleteCpuLoadAndFlushEventWaitsForClaimedDecode)
{
    VerifyCompleteCpuLoadJoinsWinner(false);
}

TEST(AssetService, CompleteCpuLoadAndFlushEventWaitsForReadyPublication)
{
    VerifyCompleteCpuLoadJoinsWinner(true);
}

TEST(AssetService, CompleteCpuLoadAndFlushEventRejectsFailedAsset)
{
    ASSERT_FALSE(Tasks::Scheduler::IsInitialized());
    TmpFile file("svc_complete_failed.bin");
    AssetService svc;
    const auto id = svc.Load<Mesh>(file.path.string(), MeshLoader(10));
    ASSERT_TRUE(id.has_value());
    ASSERT_TRUE(svc.ForceAssetState(*id, AssetState::Ready, AssetState::Failed).has_value());

    const auto completed = svc.CompleteCpuLoadAndFlushEvent(*id);
    ASSERT_FALSE(completed.has_value());
    EXPECT_EQ(completed.error(), ErrorCode::InvalidState);
    EXPECT_EQ(svc.GetMeta(*id).value().state, AssetState::Failed);
}

TEST(AssetService, CompleteCpuLoadAndFlushEventRejectsDestroyedAndStaleIds)
{
    ASSERT_FALSE(Tasks::Scheduler::IsInitialized());
    TmpFile file("svc_complete_stale.bin");
    AssetService svc;
    const auto id = svc.Load<Mesh>(file.path.string(), MeshLoader(10));
    ASSERT_TRUE(id.has_value());
    ASSERT_TRUE(svc.Destroy(*id).has_value());

    const auto destroyed = svc.CompleteCpuLoadAndFlushEvent(*id);
    ASSERT_FALSE(destroyed.has_value());
    EXPECT_EQ(destroyed.error(), ErrorCode::ResourceNotFound);

    const auto replacement = svc.Load<Mesh>(file.path.string(), MeshLoader(20));
    ASSERT_TRUE(replacement.has_value());
    ASSERT_EQ(replacement->Index, id->Index);
    ASSERT_NE(replacement->Generation, id->Generation);
    const auto stale = svc.CompleteCpuLoadAndFlushEvent(*id);
    ASSERT_FALSE(stale.has_value());
    EXPECT_EQ(stale.error(), ErrorCode::ResourceNotFound);
    EXPECT_TRUE(svc.CompleteCpuLoadAndFlushEvent(*replacement).has_value());
}

TEST(AssetService, DestroyWaitsForReadyPublication)
{
    ASSERT_FALSE(Tasks::Scheduler::IsInitialized());
    SchedulerScope scheduler(1u);
    auto pause = std::make_shared<PausedCpuCompletion>();
    AssetLoadPipelineTestHooks hooks;
    hooks.BeforeCpuReadyEvent = [pause](AssetId) { pause->Pause(); };
    AssetService svc(std::move(hooks));
    TmpFile file("svc_destroy_paused_ready_event.bin");
    std::vector<AssetEvent> events;
    bool destroyReturned = false;
    (void)svc.SubscribeAll([&](AssetId, AssetEvent event)
    {
        if (event == AssetEvent::Ready)
            EXPECT_FALSE(destroyReturned);
        events.push_back(event);
    });
    const auto id = svc.Load<Mesh>(file.path.string(), MeshLoader(10));
    if (!id.has_value())
        pause->Release();
    ASSERT_TRUE(id.has_value());
    bool entered = false;
    {
        std::unique_lock lock(pause->mutex);
        entered = pause->changed.wait_for(lock, std::chrono::seconds(2),
            [&] { return pause->entered; });
    }
    if (!entered)
        pause->Release();
    ASSERT_TRUE(entered);

    const auto destroyed = RunWhileWinnerPaused(pause, [&]
    {
        const auto result = svc.Destroy(*id);
        destroyReturned = true;
        return result;
    });
    ASSERT_TRUE(destroyed.has_value());
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0], AssetEvent::Ready);
    // Ensure the scheduled winner has exited before checking for late events.
    Tasks::Scheduler::WaitForAll();
    svc.Tick();
    ASSERT_EQ(events.size(), 2u);
    EXPECT_EQ(events[1], AssetEvent::Destroyed);
    EXPECT_FALSE(svc.IsAlive(*id));
}

TEST(AssetService, MarkFailedWaitsForClaimedDecode)
{
    ASSERT_FALSE(Tasks::Scheduler::IsInitialized());
    SchedulerScope scheduler(1u);
    auto pause = std::make_shared<PausedCpuCompletion>();
    AssetLoadPipelineTestHooks hooks;
    hooks.AfterCpuDecodeClaim = [pause](AssetId) { pause->Pause(); };
    AssetRegistry registry;
    AssetEventBus bus;
    AssetLoadPipeline pipeline(std::move(hooks));
    pipeline.BindRegistry(&registry);
    pipeline.BindEventBus(&bus);
    const auto id = registry.Create(0u, 0u).value();
    std::vector<AssetEvent> events;
    (void)bus.SubscribeAll([&](AssetId, AssetEvent event) { events.push_back(event); });
    const auto queued = pipeline.EnqueueIO(LoadRequest{.id = id});
    bool entered = false;
    {
        std::unique_lock lock(pause->mutex);
        entered = pause->changed.wait_for(lock, std::chrono::seconds(2),
            [&] { return pause->entered; });
    }
    if (!entered)
        pause->Release();
    ASSERT_TRUE(entered);
    const auto failed = RunWhileWinnerPaused(pause, [&] { return pipeline.MarkFailed(id); });
    ASSERT_TRUE(queued.has_value());
    ASSERT_TRUE(failed.has_value());
    bus.Flush();
    EXPECT_EQ(registry.GetState(id).value(), AssetState::Failed);
    EXPECT_FALSE(pipeline.IsInFlight(id));
    EXPECT_EQ(events, (std::vector<AssetEvent>{AssetEvent::Ready, AssetEvent::Failed}));
}

TEST(AssetService, CompleteCpuLoadRejectsCanceledQueuedRequest)
{
    ASSERT_FALSE(Tasks::Scheduler::IsInitialized());
    SchedulerScope scheduler(1u);
    auto blocker = std::make_shared<PausedCpuCompletion>();
    Tasks::Scheduler::Dispatch([blocker] { blocker->Pause(); });
    bool entered = false;
    {
        std::unique_lock lock(blocker->mutex);
        entered = blocker->changed.wait_for(lock, std::chrono::seconds(2),
            [&] { return blocker->entered; });
    }
    if (!entered)
        blocker->Release();
    ASSERT_TRUE(entered);

    AssetRegistry registry;
    AssetEventBus bus;
    AssetLoadPipeline pipeline;
    pipeline.BindRegistry(&registry);
    pipeline.BindEventBus(&bus);
    const auto id = registry.Create(0u, 0u);
    if (!id.has_value())
        blocker->Release();
    ASSERT_TRUE(id.has_value());
    const auto queued = pipeline.EnqueueIO(LoadRequest{.id = *id});
    pipeline.Cancel(*id);
    const auto completed = pipeline.CompleteCpuLoad(*id);
    blocker->Release();
    Tasks::Scheduler::WaitForAll();

    ASSERT_TRUE(queued.has_value());
    ASSERT_FALSE(completed.has_value());
    EXPECT_EQ(completed.error(), ErrorCode::InvalidState);
    EXPECT_FALSE(pipeline.IsInFlight(*id));
    EXPECT_EQ(bus.PendingCount(), 0u);
}

TEST(AssetService, LoadSamePathReturnsSameId)
{
    TmpFile f("svc_load_same.bin");
    AssetService svc;
    auto a = svc.Load<Mesh>(f.path.string(), MeshLoader(1)).value();
    auto b = svc.Load<Mesh>(f.path.string(), MeshLoader(99)).value();
    EXPECT_EQ(a, b);
}

TEST(AssetService, LoadSamePathWithDifferentTypeRejected)
{
    TmpFile f("svc_load_same_different_type.bin");
    AssetService svc;
    auto id = svc.Load<Mesh>(f.path.string(), MeshLoader(1));
    ASSERT_TRUE(id.has_value());

    auto wrongType = svc.Load<Texture>(f.path.string(), [](std::string_view, AssetId) -> Expected<Texture>
    {
        return Texture{.name = "albedo"};
    });
    ASSERT_FALSE(wrongType.has_value());
    EXPECT_EQ(wrongType.error(), ErrorCode::TypeMismatch);
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

TEST(AssetService, ReloadFailurePreservesPayloadTicketAndPublishesNoSuccessEvents)
{
    TmpFile f("svc_reload_failure_ticket.bin");
    AssetService svc;
    auto id = svc.Load<Mesh>(f.path.string(), MeshLoader(42)).value();
    svc.Tick(); // drain initial Ready before observing reload-specific events

    const PayloadTicket before = svc.GetPayloadTicket(id).value();
    std::vector<AssetEvent> events;
    (void)svc.SubscribeAll([&](AssetId observed, AssetEvent event)
    {
        if (observed == id)
        {
            events.push_back(event);
        }
    });

    auto r = svc.Reload<Mesh>(id, FailingLoader(ErrorCode::AssetDecodeFailed));
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), ErrorCode::AssetDecodeFailed);

    const PayloadTicket after = svc.GetPayloadTicket(id).value();
    EXPECT_EQ(after, before);
    EXPECT_EQ(svc.GetMeta(id).value().state, AssetState::Ready);
    EXPECT_EQ(svc.Read<Mesh>(id).value()[0].triangles, 42);

    svc.Tick();
    EXPECT_TRUE(events.empty());
}

TEST(AssetService, ReloadAdvancesPayloadTicketAndQueuesReloadedBeforeReady)
{
    TmpFile f("svc_reload_event_order.bin");
    AssetService svc;
    auto id = svc.Load<Mesh>(f.path.string(), MeshLoader(1)).value();
    svc.Tick(); // drain initial Ready before observing reload order

    const PayloadTicket before = svc.GetPayloadTicket(id).value();
    std::vector<AssetEvent> events;
    (void)svc.SubscribeAll([&](AssetId observed, AssetEvent event)
    {
        if (observed == id)
        {
            events.push_back(event);
        }
    });

    ASSERT_TRUE(svc.Reload<Mesh>(id, MeshLoader(999)).has_value());

    const PayloadTicket after = svc.GetPayloadTicket(id).value();
    EXPECT_EQ(after.slot, before.slot);
    EXPECT_EQ(after.generation, before.generation + 1u);
    EXPECT_EQ(svc.Read<Mesh>(id).value()[0].triangles, 999);

    svc.Tick();
    ASSERT_EQ(events.size(), 2u);
    EXPECT_EQ(events[0], AssetEvent::Reloaded);
    EXPECT_EQ(events[1], AssetEvent::Ready);
}
 
TEST(AssetService, ReloadRejectedFromNonReadyState)
{
    TmpFile f("svc_reload_nonready.bin");
    AssetService svc;
    auto id = svc.Load<Mesh>(f.path.string(), MeshLoader(1)).value();
    ASSERT_TRUE(svc.ForceAssetState(id, AssetState::Ready, AssetState::Failed).has_value());
 
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
    EXPECT_TRUE(svc.HasLoaderCallback(*token));
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

TEST(AssetService, ParameterlessReloadQueuesReloadedBeforeReadyAndAdvancesTicket)
{
    TmpFile f("svc_reload_captured_event_order.bin");
    AssetService svc;

    auto counter = std::make_shared<int>(0);
    auto loader = [counter](std::string_view, AssetId) -> Expected<Mesh>
    {
        return Mesh{.triangles = ++(*counter)};
    };

    auto id = svc.Load<Mesh>(f.path.string(), loader).value();
    svc.Tick(); // drain initial Ready before observing reload order
    const PayloadTicket before = svc.GetPayloadTicket(id).value();

    std::vector<AssetEvent> events;
    (void)svc.SubscribeAll([&](AssetId observed, AssetEvent event)
    {
        if (observed == id)
        {
            events.push_back(event);
        }
    });

    ASSERT_TRUE(svc.Reload(id).has_value());
    EXPECT_EQ(svc.Read<Mesh>(id).value()[0].triangles, 2);

    const PayloadTicket after = svc.GetPayloadTicket(id).value();
    EXPECT_EQ(after.slot, before.slot);
    EXPECT_EQ(after.generation, before.generation + 1u);

    svc.Tick();
    ASSERT_EQ(events.size(), 2u);
    EXPECT_EQ(events[0], AssetEvent::Reloaded);
    EXPECT_EQ(events[1], AssetEvent::Ready);
}

TEST(AssetService, ParameterlessReloadRejectedFromNonReadyState)
{
    TmpFile f("svc_reload_captured_nonready.bin");
    AssetService svc;
    auto id = svc.Load<Mesh>(f.path.string(), MeshLoader(1)).value();

    ASSERT_TRUE(svc.ForceAssetState(id, AssetState::Ready, AssetState::Failed).has_value());
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
    ASSERT_TRUE(svc.HasLoaderCallback(token));

    ASSERT_TRUE(svc.Destroy(id).has_value());
    EXPECT_FALSE(svc.HasLoaderCallback(token));
    EXPECT_EQ(svc.LoaderCallbackCount(), 0u);

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
    (void)svc.SubscribeAll([&](AssetId, AssetEvent e)
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

    EXPECT_TRUE(svc.HasLoaderCallback(token));
    EXPECT_EQ(svc.LoaderCallbackCount(), 1u);
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

TEST(AssetService, DestroyFlushesPendingReadyBeforeRetiringPayload)
{
    TmpFile f("svc_destroy_flushes_ready.bin");
    AssetService svc;
    std::vector<AssetEvent> events;
    bool readyPayloadReadable = false;
    int readyTriangles = 0;
    (void)svc.SubscribeAll([&](AssetId observed, AssetEvent event)
    {
        events.push_back(event);
        if (event == AssetEvent::Ready)
        {
            auto span = svc.Read<Mesh>(observed);
            readyPayloadReadable = span.has_value();
            if (span.has_value())
            {
                readyTriangles = (*span)[0].triangles;
            }
        }
    });

    auto id = svc.Load<Mesh>(f.path.string(), MeshLoader(7)).value();
    ASSERT_TRUE(svc.Destroy(id).has_value());

    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0], AssetEvent::Ready);
    EXPECT_TRUE(readyPayloadReadable);
    EXPECT_EQ(readyTriangles, 7);

    svc.Tick();
    ASSERT_EQ(events.size(), 2u);
    EXPECT_EQ(events[1], AssetEvent::Destroyed);
}
 
// -----------------------------------------------------------------------------
// Tick / event bus integration
// -----------------------------------------------------------------------------
 
TEST(AssetService, TickFlushesEventBus)
{
    TmpFile f("svc_tick.bin");
    AssetService svc;
    std::atomic<int> destroyed{0};
    (void)svc.SubscribeAll([&](AssetId, AssetEvent e)
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
    EXPECT_EQ(svc.LiveAssetCount(), 0u);
    EXPECT_FALSE(svc.PathIndexContains(std::filesystem::absolute(f.path).string()));

    // Retrying with a good loader must succeed.
    auto ok = svc.Load<Mesh>(f.path.string(), MeshLoader(7));
    ASSERT_TRUE(ok.has_value());
    auto span = svc.Read<Mesh>(*ok).value();
    EXPECT_EQ(span[0].triangles, 7);
}
