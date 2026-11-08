// RUNTIME-313: the one setup/completion contract of queued editor jobs
// (`MeshSupport::ActiveOutputJobRefusal`, `ValidateQueuedJob`, `QueuedJobDelivery`),
// checked per operation through a scripted job lane: duplicate refusal before any
// submission, a rejected submission answered once without the callback, and an
// abandoned run that revalidates as Cancelled and delivers exactly once.
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <utility>
#include <iterator>
#include <map>
#include <mutex>
#include <sstream>
#include <optional>
#include <regex>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>
#include <glm/glm.hpp>
#include <entt/entity/registry.hpp>
#include <gtest/gtest.h>
#include "EditorFeatureTestContext.hpp"
#include "MockRHI.hpp"
#include "SandboxEditorJobHarness.hpp"

import Extrinsic.ECS.Scene.Registry;
import Extrinsic.RHI.Device;
import Extrinsic.Runtime.CommandBus;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.Module;
import Extrinsic.Runtime.ServiceRegistry;
import Extrinsic.Runtime.SpatialIndexCache;
import Extrinsic.Runtime.WorldHandle;
import Extrinsic.Runtime.WorldRegistry;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.Runtime.EditorJobProjection;
import Extrinsic.Runtime.EditorProcessing;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.NormalOperations;
import Extrinsic.Runtime.PointAnalysisOperations;
import Extrinsic.Runtime.PointConstructionOperations;
import Extrinsic.Runtime.PointFieldOperations;
import Extrinsic.Runtime.PointSetOperations;
import Geometry.Properties;

namespace R = Extrinsic::Runtime;
namespace GS = Extrinsic::ECS::Components::GeometrySources;
using D = R::GeometryElementDomain;

namespace
{
    class QueuedEditorJobContract : public ::testing::Test
    {
    protected:
        R::WorldRegistry Worlds;
        R::WorldHandle World{Worlds.CreateWorld("queued jobs")};
        Extrinsic::ECS::Scene::Registry& Scene{*Worlds.Get(World)};
        Extrinsic::Tests::MockDevice Device;
        Extrinsic::Tests::EditorJobHarness Jobs;
        R::SpatialIndexCache Cache{Worlds};
        R::CommandBus Bus;
        R::KernelEventBus Events;
        R::ServiceRegistry Services;
        entt::entity Entity{};
        std::uint32_t Id{};
        R::EditorProcessingContext Context{};
        std::vector<R::JobDesc> Queued;
        std::vector<R::EditorJobIdentity> Identities; // of every accepted submission, in order
        // Submissions accepted before the lane rejects (a later stage of a multi-stage run).
        std::size_t AcceptSubmissions{~std::size_t{0}};
        std::optional<R::EditorJobRecord> Active;

        void SetUp() override
        {
            Entity = Scene.Create();
            auto& props = Scene.Raw().emplace<GS::Vertices>(Entity).Properties;
            props.Resize(8);
            auto samples = props.GetOrAdd<glm::vec3>("samples");
            auto directions = props.GetOrAdd<glm::vec3>("directions");
            for (std::size_t i = 0; i < 8; ++i)
            {
                samples[i] = {float(i % 2), float((i / 2) % 2), float(i / 4)};
                directions[i] = {0, 0, 1};
            }
            Id = R::SelectionController::ToStableEntityId(Entity);
            Device.ShaderFloat64 = true;
            Device.TransferQueue.AcceptBufferUploads = true;
            Services.BeginRegistration();
            ASSERT_TRUE(Services.Provide<Extrinsic::RHI::IDevice>(Device, "test").has_value());
            R::EngineSetup setup{Bus, Events, Jobs.Jobs(), Worlds, Services, [](R::FramePhase, R::RuntimeFrameHook) {}};
            ASSERT_TRUE(Cache.OnRegister(setup).has_value());
            Context.Scene = &Scene;
            Context.World = World;
            Context.Device = &Device;
            Context.SpatialIndices = &Cache;
            Context.JobCommands.Submit = [this](R::JobDesc desc, R::EditorJobIdentity identity) {
                if (Queued.size() >= AcceptSubmissions) return R::JobToken{};
                Queued.push_back(std::move(desc));
                Identities.push_back(std::move(identity));
                return R::JobToken{static_cast<std::uint32_t>(Queued.size()), 1u};
            };
            Context.JobCommands.FindActive = [this](const R::EditorJobIdentity& identity) {
                auto active = Active;
                if (active) active->Identity = identity;
                return active;
            };
        }
        void TearDown() override
        {
            ReleaseFirstPage = true; // never leave a held page's worker blocked
            Queued.clear();
            Jobs.Jobs().CancelAndDrain();
            R::RuntimeModuleShutdownContext shutdown{Bus, Events, Jobs.Jobs(), Worlds, Services};
            Cache.OnShutdown(shutdown);
        }
        [[nodiscard]] R::EditorProcessingCommands Commands() { return R::BindEditorProcessingCommands(Context); }
        [[nodiscard]] R::GeometryPropertyRef Ref(const char* name, Geometry::PropertyValueKind kind) const
        {
            return {D::PointCloudPoint, name, kind};
        }
        [[nodiscard]] R::GeometryPropertyRef Positions() const { return Ref("samples", Geometry::PropertyValueKind::Vec3); }
        [[nodiscard]] R::GeometryPropertyRef Normals() const { return Ref("directions", Geometry::PropertyValueKind::Vec3); }

        // ARCH-023: the mock compute lane of a construction run's prepared kNN session, without frames. The
        // preparation (0 bytes) is accepted; every page answers the `width` exact nearest samples (CPU float
        // distance, then source ID) of the rows it queries, the active prefix of the page's mapped upload, and its
        // pulled bytes complete at once. `CorruptPage` names an unknown source slot in that page; with `HoldFirstPage`
        // the first page's wait blocks until `ReleaseFirstPage` and then completes (it never reports Pending).
        struct PackedNeighbor
        {
            std::uint32_t Index;
            float SquaredDistance;
        };
        std::mutex Gpu;
        std::map<std::uint64_t, std::vector<std::byte>> Pulled;
        std::vector<std::size_t> Pages; // the query count of every page submitted
        std::uint64_t LaneTokens{}, HeldToken{};
        std::size_t CorruptPage{~std::size_t{0}};
        std::atomic<bool> HoldFirstPage{false}, ReleaseFirstPage{false};
        [[nodiscard]] std::size_t PageCount()
        {
            std::scoped_lock lock{Gpu};
            return Pages.size();
        }
        [[nodiscard]] std::size_t MappedUploads()
        {
            std::scoped_lock writes{Device.MappedWriteMutex};
            return Device.MappedWrites.size();
        }
        void UseConstructionLane(const std::uint32_t width)
        {
            const auto samples =
                std::as_const(Scene.Raw().get<GS::Vertices>(Entity).Properties).Get<glm::vec3>("samples").Vector();
            Device.TransferQueue.ComputeWait = [this](Extrinsic::RHI::ReadbackToken token, std::uint64_t)
            {
                while (HoldFirstPage && token.IsValid() && token.Value == HeldToken && !ReleaseFirstPage)
                    std::this_thread::sleep_for(std::chrono::microseconds(100));
                return Extrinsic::RHI::ComputeWaitStatus::Complete;
            };
            Device.TransferQueue.TakeReadback = [this](Extrinsic::RHI::ReadbackToken token, std::span<std::byte> out)
            {
                std::scoped_lock lock{Gpu};
                const auto found = Pulled.find(token.Value);
                if (found == Pulled.end() || found->second.size() != out.size())
                    return false;
                std::ranges::copy(found->second, out.begin());
                Pulled.erase(found);
                return true;
            };
            Device.ComputeReadback = [this, samples, width](
                                         auto record, std::uint64_t bytes, Extrinsic::RHI::ReadbackSink)
            {
                std::scoped_lock lock{Gpu};
                if (!record(Device.AsyncComputeContext).IsValid())
                    return Extrinsic::RHI::ReadbackToken{};
                const Extrinsic::RHI::ReadbackToken token{(1ull << 63u) | ++LaneTokens};
                if (bytes == 0u)
                    return token; // the session's preparation (its sink reads nothing)
                const auto count = bytes / (width * sizeof(PackedNeighbor) + 2u * sizeof(std::uint32_t));
                std::vector<glm::vec3> queries(count);
                {
                    std::scoped_lock writes{Device.MappedWriteMutex};
                    const auto& upload = Device.MappedWrites.back().Data;
                    EXPECT_GE(upload.size(), count * sizeof(glm::vec3));
                    std::memcpy(queries.data(), upload.data(), count * sizeof(glm::vec3));
                }
                std::vector<PackedNeighbor> rows;
                for (const auto query : queries)
                {
                    std::vector<std::uint32_t> order(samples.size());
                    for (std::uint32_t i = 0; i < order.size(); ++i)
                        order[i] = i;
                    const auto distance = [&](std::uint32_t i)
                    {
                        const auto d = samples[i] - query;
                        return glm::dot(d, d);
                    };
                    std::ranges::sort(order,
                        [&](auto a, auto b)
                        {
                            return distance(a) < distance(b) || (distance(a) == distance(b) && a < b);
                        });
                    for (std::uint32_t j = 0; j < width; ++j)
                        rows.push_back({order[j], distance(order[j])});
                }
                if (Pages.size() == CorruptPage)
                    rows.front().Index = 99u;
                std::vector<std::uint32_t> headers(count * 2u, 0u);
                for (std::size_t i = 0; i < count; ++i)
                    headers[2u * i] = width;
                const auto rowBytes = std::as_bytes(std::span(rows)), headerBytes = std::as_bytes(std::span(headers));
                auto& data = Pulled[token.Value];
                data.assign(rowBytes.begin(), rowBytes.end());
                data.insert(data.end(), headerBytes.begin(), headerBytes.end());
                EXPECT_EQ(data.size(), bytes);
                if (Pages.empty())
                    HeldToken = token.Value;
                Pages.push_back(count);
                return token;
            };
        }

        // Every queued point operation, by its shared label.
        template <class Check>
        void ForEachOperation(Check check)
        {
            const auto commands = Commands();
            check("Outlier estimation", [&](auto done) {
                return R::ApplyEditorOutlierAnalysisCommand(commands, {.StableEntityId = Id, .Positions = Positions(),
                    .Mask = Ref("outliers", Geometry::PropertyValueKind::UInt32),
                    .Score = Ref("scores", Geometry::PropertyValueKind::Float), .KNeighbors = 2}, done); });
            check("Normal estimation", [&](auto done) {
                return R::ApplyEditorNormalEstimationCommand(commands, {.StableEntityId = Id, .Positions = Positions(),
                    .Output = Ref("normals", Geometry::PropertyValueKind::Vec3)}, done); });
            check("Keypoint analysis", [&](auto done) {
                return R::ApplyEditorKeypointAnalysisCommand(commands, {.StableEntityId = Id, .Positions = Positions(),
                    .Mask = Ref("keypoints", Geometry::PropertyValueKind::UInt32),
                    .Score = Ref("saliency", Geometry::PropertyValueKind::Float), .MinimumNeighbors = 1}, done); });
            check("Point construction", [&](auto done) {
                return R::ApplyEditorPointConstructionCommand(commands, {.StableEntityId = Id, .Positions = Positions(),
                    .Method = R::PointConstructionMethod::KnnGraph, .KNeighbors = 2}, done); });
        }
    };
}

TEST_F(QueuedEditorJobContract, DuplicateOutputIsRefusedBeforeSubmissionWithTheSharedMessage)
{
    Active = R::EditorJobRecord{.Token = R::JobToken{3u, 1u}, .State = R::JobState::Running};
    ForEachOperation([&](const std::string& label, auto apply) {
        SCOPED_TRACE(label);
        Queued.clear();
        unsigned calls{0u};
        const auto result = apply([&](auto) { ++calls; });
        EXPECT_EQ(result.Status, R::EditorCommandStatus::Pending);
        EXPECT_EQ(result.Message, label + " already has an active running job (job 3:1).");
        EXPECT_TRUE(Queued.empty());
        EXPECT_EQ(calls, 0u);
    });
}

TEST_F(QueuedEditorJobContract, RejectedSubmissionAnswersOnceWithoutTheCallback)
{
    AcceptSubmissions = 0u;
    ForEachOperation([&](const std::string& label, auto apply) {
        SCOPED_TRACE(label);
        unsigned calls{0u};
        const auto result = apply([&](auto) { ++calls; });
        EXPECT_EQ(result.Status, R::EditorCommandStatus::GeometryProcessingFailed) << result.Message;
        EXPECT_EQ(result.Message.rfind(label + " job submission was rejected", 0), 0u) << result.Message;
        // The immediate answer is the report (panels publish it); a callback would report twice.
        EXPECT_EQ(calls, 0u);
    });
}

TEST_F(QueuedEditorJobContract, AbandonedRunRevalidatesAsCancelledAndDeliversOnce)
{
    ForEachOperation([&](const std::string& label, auto apply) {
        SCOPED_TRACE(label);
        Queued.clear();
        unsigned calls{0u};
        R::EditorCommandStatus status{};
        std::string message;
        const auto result = apply([&](auto delivered) {
            ++calls;
            status = delivered.Status;
            message = delivered.Message;
        });
        ASSERT_EQ(result.Status, R::EditorCommandStatus::Pending) << result.Message;
        ASSERT_FALSE(Queued.empty());
        auto& last = Queued.back();
        ASSERT_EQ(last.ValidateBeforeApply(), R::JobApplyValidation::Current);
        last.FinalizeUnpublishedOnMainThread();
        EXPECT_EQ(calls, 1u);
        EXPECT_EQ(status, R::EditorCommandStatus::StaleEntity);
        EXPECT_EQ(message, label + " was cancelled or its source became stale; nothing was applied.");
        // An abandoned run never publishes: any stage revalidated afterwards is Cancelled.
        EXPECT_EQ(last.ValidateBeforeApply(), R::JobApplyValidation::Cancelled);
        last.FinalizeUnpublishedOnMainThread();
        EXPECT_EQ(calls, 1u);
    });
}

// ARCH-023: Vulkan construction is a worker step run (its kNN rows paged by the step through a prepared query session,
// no frame); a composition without the session's step runs refuses it at capture with that reason, queues nothing and
// calls no sink: there is no frame-paced chain to fall back to. Mutation: restoring the chain queues stages here and
// answers Pending.
TEST_F(QueuedEditorJobContract, VulkanConstructionWithoutStepRunsIsRefusedNotChained)
{
    Queued.clear();
    unsigned calls{0u};
    const auto result = R::ApplyEditorPointConstructionCommand(Commands(),
        {.StableEntityId = Id,
            .Method = R::PointConstructionMethod::KnnGraph,
            .Backend = R::PointConstructionBackend::VulkanLBVH,
            .Positions = Positions(),
            .KNeighbors = 2},
        [&](auto)
        {
            ++calls;
        });
    EXPECT_EQ(result.Status, R::EditorCommandStatus::InvalidProcessingParameters) << result.Message;
    EXPECT_EQ(result.Message, "Vulkan construction requires the session's step runs.");
    EXPECT_TRUE(Queued.empty());
    EXPECT_EQ(calls, 0u);
}

// ARCH-023: Vulkan LBVH keypoints are a worker step run (their radius rows paged by the step, no frame); a composition
// without the session's step runs refuses them with that reason, queues nothing and calls no sink: there is no
// frame-paced chain to fall back to. Mutation: restoring the chain queues stages here and answers Pending.
TEST_F(QueuedEditorJobContract, VulkanKeypointsWithoutStepRunsAreRefusedNotChained)
{
    Queued.clear();
    unsigned calls{0u};
    const auto result = R::ApplyEditorKeypointAnalysisCommand(Commands(),
        {.StableEntityId = Id,
            .Positions = Positions(),
            .Mask = Ref("keypoints", Geometry::PropertyValueKind::UInt32),
            .Score = Ref("saliency", Geometry::PropertyValueKind::Float),
            .MinimumNeighbors = 1,
            .Backend = R::KeypointAnalysisBackend::VulkanLBVH},
        [&](auto)
        {
            ++calls;
        });
    EXPECT_EQ(result.Status, R::EditorCommandStatus::InvalidProcessingParameters) << result.Message;
    EXPECT_NE(result.Message.find("step runs"), std::string::npos) << result.Message;
    EXPECT_TRUE(Queued.empty());
    EXPECT_EQ(calls, 0u);
}

// RUNTIME-317: a queued job reports the backend domain its config requested (at submit) and,
// once its run delivered, the domain it resolved to and the run's diagnostic, through the shared
// submit identity and result guard. A CPU run resolves to the CPU and carries its message.
TEST_F(QueuedEditorJobContract, CpuRunReportsTheRequestedAndResolvedDomainAndItsDiagnostic)
{
    Jobs.Attach(Context);
    std::optional<R::EditorOutlierAnalysisResult> delivered;
    const auto queued = R::ApplyEditorOutlierAnalysisCommand(Commands(), {.StableEntityId = Id, .Positions = Positions(),
        .Mask = Ref("outliers", Geometry::PropertyValueKind::UInt32),
        .Score = Ref("scores", Geometry::PropertyValueKind::Float), .KNeighbors = 2},
        [&](R::EditorOutlierAnalysisResult result) { delivered = std::move(result); });
    ASSERT_EQ(queued.Status, R::EditorCommandStatus::Pending) << queued.Message;
    const auto submitted = Context.JobCommands.SnapshotAll();
    ASSERT_EQ(submitted.size(), 1u);
    EXPECT_EQ(submitted[0].RequestedJobDomain, R::EditorJobDomain::Cpu);
    EXPECT_TRUE(submitted[0].Diagnostic.empty()) << "nothing to report before the run ends";
    ASSERT_TRUE(Jobs.DrainUntilTerminal());
    ASSERT_TRUE(delivered.has_value());
    ASSERT_TRUE(delivered->Succeeded()) << delivered->Message;
    // ARCH-023: the CPU run is a one-shot step run: its holder (the run's row, which names the output and takes the
    // outcome) and the job of its one step, which joins it.
    const auto rows = Context.JobCommands.SnapshotAll();
    ASSERT_EQ(rows.size(), 2u);
    const auto holder = std::ranges::find_if(rows,
        [&](const auto& row)
        {
            return row.Token == submitted[0].Token;
        });
    ASSERT_NE(holder, rows.end());
    EXPECT_EQ(holder->State, R::JobState::Published);
    EXPECT_EQ(holder->RequestedJobDomain, R::EditorJobDomain::Cpu);
    EXPECT_EQ(holder->ResolvedJobDomain, R::EditorJobDomain::Cpu);
    EXPECT_EQ(holder->Diagnostic, delivered->Message);
    EXPECT_FALSE(holder->Diagnostic.empty());
    EXPECT_NE(delivered->StepRun, 0u) << "the delivered result names its run";
}

// A Vulkan request that cannot run on the device falls back to the CPU reference: the job keeps
// the GPU request, resolves to the CPU and reports the fallback reason as its diagnostic.
TEST_F(QueuedEditorJobContract, GpuRequestThatFellBackReportsTheCpuItRanAndWhy)
{
    Jobs.Attach(Context);
    auto context = Context;
    context.Device = nullptr; // no Vulkan execution available
    R::EditorProgressivePoissonCommand command{.StableEntityId = Id};
    command.Config.Backend = R::ProgressivePoissonPlaygroundBackend::VulkanCompute;
    command.Config.Positions = Positions();
    std::optional<R::EditorProgressivePoissonResult> delivered;
    const auto queued = R::ApplyEditorProgressivePoissonCommand(R::BindEditorProcessingCommands(context), command,
        [&](R::EditorProgressivePoissonResult result) { delivered = std::move(result); });
    ASSERT_EQ(queued.Status, R::EditorCommandStatus::Pending) << queued.Message;
    ASSERT_TRUE(Jobs.DrainUntilTerminal());
    ASSERT_TRUE(delivered.has_value());
    ASSERT_TRUE(delivered->FellBackToCpu) << delivered->Message;
    ASSERT_FALSE(delivered->BackendFallbackReason.empty());
    const auto rows = Context.JobCommands.SnapshotAll();
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0].RequestedJobDomain, R::EditorJobDomain::GpuCompute);
    EXPECT_EQ(rows[0].ResolvedJobDomain, R::EditorJobDomain::Cpu);
    EXPECT_EQ(rows[0].Diagnostic, delivered->BackendFallbackReason);
}

// ARCH-023 (formerly RUNTIME-314's frame cursor): Vulkan construction is a one-shot step run whose worker step pages
// its kNN rows through one prepared query session (an Uploads session of one fixed page), with no frame. The mock
// lane answers each page with the exact neighbours of the rows that page uploaded, so the run publishes the CPU
// reference graph. Predictions, pages of 3 over 8 samples (width 3): pages {3, 3, 2}; every page uploads the full
// fixed page of 3 rows (36 B, the last page's unused tail included) and reads back count * (3 + 1) * 8 B; the reported
// traffic excludes the session's 36 B of initial zero rows; batches count this run's consumed pages; the pages allocate
// nothing after the start; a page naming an unknown source slot stops the run with the existing decoder's message
// after pages {3, 3}, only the consumed first page counted. Mutations: decoding or consuming the whole fixed page (the
// last partial page fails the layout check); counting Session->Pages() (2 when corrupt); reporting the session's
// totals (+36 B); the frame pager (no frame is recorded here: nothing is delivered).
TEST_F(QueuedEditorJobContract, VulkanConstructionPagesItsQuerySessionOnTheWorker)
{
    constexpr std::uint32_t width = 3u; // two neighbors plus the query sample itself
    R::PointConstructionConfig config{.StableEntityId = Id,
        .Method = R::PointConstructionMethod::KnnGraph,
        .Positions = Positions(),
        .KNeighbors = 2};
    config.GpuQueryBatchSize = 3u;
    auto synchronous = Context;
    synchronous.JobCommands = {};
    const auto expected = R::ApplyEditorPointConstructionCommand(R::BindEditorProcessingCommands(synchronous), config);
    ASSERT_TRUE(expected.Succeeded()) << expected.Message;
    config.Backend = R::PointConstructionBackend::VulkanLBVH;
    Jobs.Attach(Context);
    UseConstructionLane(width);
    for (const bool corruptSecondPage : {false, true})
    {
        SCOPED_TRACE(corruptSecondPage);
        {
            std::scoped_lock lock{Gpu};
            Pages.clear();
            CorruptPage = corruptSecondPage ? 1u : ~std::size_t{0};
        }
        const std::size_t earlierJobs = Context.JobCommands.SnapshotAll().size();
        std::optional<R::EditorPointConstructionResult> delivered;
        const auto queued = R::ApplyEditorPointConstructionCommand(Commands(), config, [&](auto result) { delivered = result; });
        ASSERT_EQ(queued.Status, R::EditorCommandStatus::Pending) << queued.Message;
        EXPECT_NE(queued.StepRun, 0u) << "the batch is a step run";
        // The session was prepared before the start: whatever the pages need exists already.
        const auto allocations = Cache.Stats().GpuBatchAllocations, scratch = Cache.Stats().ScratchMisses;
        const auto buffers = Device.CreateBufferCount;
        for (int drain = 0; drain < 400 && !delivered; ++drain)
            (void)Jobs.DrainUntilTerminal(std::chrono::milliseconds{2});
        ASSERT_TRUE(delivered);
        ASSERT_TRUE(Jobs.DrainUntilTerminal()); // the run retired: the next one may hold the output
        EXPECT_EQ(delivered->ActualBackend, "vulkan_lbvh");
        EXPECT_EQ(delivered->StepRun, queued.StepRun);
        // RUNTIME-317: every job of the run (its holder and its step) requested the GPU and, once it produced its
        // result, resolved to it; a failed run resolved to nothing. The run's terminal message is the diagnostic.
        const auto rows = Context.JobCommands.SnapshotAll();
        ASSERT_GT(rows.size(), earlierJobs);
        for (std::size_t i = earlierJobs; i < rows.size(); ++i)
        {
            SCOPED_TRACE(rows[i].Name);
            EXPECT_EQ(rows[i].RequestedJobDomain, R::EditorJobDomain::GpuCompute);
            EXPECT_EQ(rows[i].ResolvedJobDomain,
                      corruptSecondPage ? std::nullopt : std::optional{R::EditorJobDomain::GpuCompute});
            EXPECT_EQ(rows[i].Diagnostic, delivered->Message);
        }
        const auto pages = [&]
        {
            std::scoped_lock lock{Gpu};
            return Pages;
        }();
        if (corruptSecondPage)
        {
            EXPECT_EQ(delivered->Status, R::EditorCommandStatus::GeometryProcessingFailed);
            EXPECT_EQ(delivered->Message, "Vulkan construction returned an unknown source slot.");
            EXPECT_EQ(delivered->GpuQueryBatches, 1u) << "only the consumed first page counts";
            EXPECT_EQ(pages, (std::vector<std::size_t>{3u, 3u}));
            EXPECT_EQ(delivered->CpuStageUploadBytes, 2u * 3u * 12u);
            EXPECT_EQ(delivered->CpuStageReadbackBytes, 6u * (width + 1u) * 8u);
            continue;
        }
        ASSERT_TRUE(delivered->Succeeded()) << delivered->Message;
        EXPECT_EQ(pages, (std::vector<std::size_t>{3u, 3u, 2u}));
        EXPECT_EQ(delivered->GpuQueryBatches, 3u);
        EXPECT_EQ(delivered->CpuStageUploadBytes, 3u * 3u * 12u)
            << "every page uploads the full fixed page; the session's initial rows are not counted";
        EXPECT_EQ(delivered->CpuStageReadbackBytes, 8u * (width + 1u) * 8u);
        EXPECT_EQ(Cache.Stats().GpuBatchAllocations, allocations) << "the pages allocate no query batch";
        EXPECT_EQ(Cache.Stats().ScratchMisses, scratch) << "nor method scratch";
        EXPECT_EQ(Device.CreateBufferCount, buffers) << "nor any device buffer";
        EXPECT_EQ(delivered->OutputVertexCount, expected.OutputVertexCount);
        EXPECT_EQ(delivered->OutputEdgeCount, expected.OutputEdgeCount);
    }
}

// ARCH-023: a discard while a page is on the device stops the run at the next page boundary. The first page's wait is
// held, the run discarded and its owner pump delivers Abort, then that page completes. Predictions: one page submitted and one
// mapped upload (the boundary stops before the second page's upload), the run Discarded, the shared cancelled result
// at its settled end, no new entity. Mutation: without the run's Abort signal all three pages are uploaded and
// submitted, despite the discard; the result still must not publish.
TEST_F(QueuedEditorJobContract, VulkanConstructionDiscardStopsAtThePageBoundary)
{
    R::PointConstructionConfig config{.StableEntityId = Id,
        .Method = R::PointConstructionMethod::KnnGraph,
        .Backend = R::PointConstructionBackend::VulkanLBVH,
        .Positions = Positions(),
        .KNeighbors = 2};
    config.GpuQueryBatchSize = 3u;
    config.Execution.Start = R::StepRunStart::Run; // computes at once, then waits for Accept
    Jobs.Attach(Context);
    UseConstructionLane(3u);
    HoldFirstPage = true; // TearDown releases it whatever fails
    const auto uploads = MappedUploads();
    std::optional<R::EditorPointConstructionResult> delivered;
    const auto started = R::StartEditorPointConstructionRun(Commands(),
        config,
        [&](auto result)
        {
            delivered = result;
        });
    ASSERT_EQ(started.Status, R::EditorCommandStatus::Pending) << started.Message;
    // Only the lane's completions drain here: a pump waits for the workers, and this one is held.
    for (int i = 0; i < 5000 && PageCount() == 0u; ++i)
    {
        (void)Jobs.Jobs().DrainCompletions(Jobs.Events());
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    const auto waiting = Jobs.StepRuns().Observe(started.StepRun);
    EXPECT_EQ(PageCount(), 1u) << "start=" << started.Message
                               << "; observation=" << (waiting ? waiting->Diagnostic : "missing")
                               << "; delivered=" << (delivered ? delivered->Message : "pending");
    std::string refusal;
    EXPECT_TRUE(Jobs.StepRuns().Control(started.StepRun, R::StepRunAction::Discard, 1u, refusal)) << refusal;
    (void)Jobs.Jobs().DrainCompletions(Jobs.Events()); // the owner pump delivers Discard before the page returns
    ReleaseFirstPage = true;
    for (int drain = 0; drain < 400 && !delivered; ++drain)
        (void)Jobs.DrainUntilTerminal(std::chrono::milliseconds{2});
    ASSERT_TRUE(delivered);
    ASSERT_TRUE(Jobs.DrainUntilTerminal());
    EXPECT_EQ(delivered->Status, R::EditorCommandStatus::StaleEntity) << delivered->Message;
    EXPECT_EQ(delivered->Message, "Point construction was cancelled or its source became stale; nothing was applied.");
    EXPECT_EQ(delivered->OutputEntityId, 0u);
    EXPECT_EQ(PageCount(), 1u);
    EXPECT_EQ(MappedUploads() - uploads, 1u) << "the page boundary stopped before the second page's upload";
    const auto run = Jobs.StepRuns().Observe(started.StepRun);
    ASSERT_TRUE(run);
    EXPECT_EQ(run->Phase, R::StepRunPhase::Discarded);
    EXPECT_EQ(Scene.Raw().view<GS::Vertices>().size(), 1u) << "no generated entity";
}

// A page of Hoppe grid queries outside the shared LBVH coordinate range is refused on the worker before its upload:
// the session's preparation is the only submission, the run reports the existing per-page message (UploadQueries
// would word its own refusal differently) and no page is consumed. Mutation: dropping the per-page GpuPoint check
// delivers UploadQueries' "GPU query rows must keep the session's count and be finite within +/-1e18." instead.
TEST_F(QueuedEditorJobContract, VulkanConstructionRefusesGridQueriesOutsideTheCoordinateRange)
{
    auto samples = Scene.Raw().get<GS::Vertices>(Entity).Properties.Get<glm::vec3>("samples");
    for (auto& sample : samples.Vector())
        sample *= 9.5e17f; // padding puts the far grid corner past 1e18
    R::PointConstructionConfig config{.StableEntityId = Id, .Method = R::PointConstructionMethod::Hoppe,
        .Backend = R::PointConstructionBackend::VulkanLBVH, .Positions = Positions(), .Normals = Normals(),
        .EstimateNormals = false, .Resolution = 4u};
    Jobs.Attach(Context);
    UseConstructionLane(8u);
    std::optional<R::EditorPointConstructionResult> delivered;
    const auto queued = R::ApplyEditorPointConstructionCommand(Commands(), config, [&](auto result) { delivered = result; });
    ASSERT_EQ(queued.Status, R::EditorCommandStatus::Pending) << queued.Message;
    EXPECT_EQ(
        [&]
        {
            std::scoped_lock lock{Gpu};
            return LaneTokens;
        }(),
        1u)
        << "the session's preparation";
    for (int drain = 0; drain < 400 && !delivered; ++drain)
        (void)Jobs.DrainUntilTerminal(std::chrono::milliseconds{2});
    ASSERT_TRUE(delivered);
    EXPECT_EQ(delivered->Status, R::EditorCommandStatus::GeometryProcessingFailed);
    EXPECT_EQ(delivered->Message, "Vulkan grid queries require normal-or-zero components within 1e18.");
    EXPECT_EQ(delivered->GpuQueryBatches, 0u);
    EXPECT_EQ(PageCount(), 0u) << "no page submitted";
}

// Drift guard: a queued editor operation reuses the shared helper instead of hand-writing the
// prologue/epilogue this task consolidated. Scans the operation sources (like the layering
// tests): no hand-written "already active" refusal or duplicate wording, no direct active-job
// lookup or message builder outside its owner, no ad hoc deliver-once flag, and the guarded-sink +
// unpublished-finalize pattern only in the files listed below, each with the reason it does not
// use `QueuedJobDelivery`. A listed file that no longer matches fails too, so the list shrinks.
#ifndef INTRINSIC_SOURCE_DIR
#error "INTRINSIC_SOURCE_DIR must be defined for the queued-job drift guard"
#endif
TEST(QueuedEditorJobDriftGuard, OperationsUseTheSharedQueuedJobHelper)
{
    namespace fs = std::filesystem;
    const auto root = fs::path{INTRINSIC_SOURCE_DIR} / "src" / "runtime" / "Editor" / "Operations";
    ASSERT_TRUE(fs::exists(root)) << root;
    // The helper's definitions and its declaring header.
    constexpr std::array<std::string_view, 2> owners{"Runtime.GeometryProcessingOperations.MeshSupport.cpp",
                                                     "Runtime.GeometryProcessingOperations.JobFailure.hpp"};
    // Files that own a job lifecycle the helper does not cover (keep this list short; a new
    // queued operation belongs on `QueuedJobDelivery`).
    constexpr std::array<std::string_view, 5> handWritten{
        // The resident keypoint run guards the sink of the scalar publication it drives
        // (the GPU Run/Accept transactions themselves are on Runtime.GpuTransactionLifecycle).
        "Runtime.PointAnalysisOperations.Keypoints.cpp",
        // Mesh-family jobs that keep their result in a typed state struct with the shared
        // `BuildUnpublishedEditorJobFailure` wording and `ActiveOutputJobRefusal`.
        "Runtime.PointSetOperations.Processing.cpp",
        "Runtime.ParameterizationOperations.Processing.cpp",
        "Runtime.MeshFieldOperations.Curvature.cpp",
        "Runtime.MeshTopologyOperations.Topology.cpp",
    };
    // RUNTIME-311: Accept front readbacks belong to the shared GPU transaction lifecycle, which
    // defines the readback primitive its header declares.
    constexpr std::array<std::string_view, 2> frontReadbackOwners{"Runtime.GpuTransactionLifecycle.cpp",
                                                                  "Runtime.GeometryProcessingOperations.GpuFront.hpp"};
    std::size_t scanned = 0, matched = 0;
    for (const auto& entry : fs::directory_iterator(root))
    {
        const auto name = entry.path().filename().string();
        const auto extension = entry.path().extension();
        if ((extension != ".cpp" && extension != ".hpp") ||
            std::find(owners.begin(), owners.end(), name) != owners.end()) continue;
        std::ifstream file(entry.path());
        const std::string text{std::istreambuf_iterator<char>(file), {}};
        ++scanned;
        SCOPED_TRACE(name);
        EXPECT_EQ(text.find("already active"), std::string::npos)
            << "a duplicate refusal comes from MeshSupport::ActiveOutputJobRefusal";
        EXPECT_EQ(text.find("FindActiveEditorJob("), std::string::npos)
            << "look up an active output job through MeshSupport::ActiveOutputJobRefusal";
        EXPECT_EQ(text.find("BuildActiveDerivedJobMessage("), std::string::npos)
            << "the duplicate wording comes from MeshSupport::ActiveOutputJobRefusal";
        EXPECT_EQ(text.find("already has an active"), std::string::npos)
            << "the duplicate wording comes from MeshSupport::ActiveOutputJobRefusal";
        EXPECT_EQ(text.find("job submission was rejected"), std::string::npos)
            << "the rejected-submission wording comes from MeshSupport::QueuedJobRejectedMessage";
        // A job-lane rejection is worded by MeshSupport::QueuedJobRejectedMessage; only a
        // refused device submission (compute, query, sampling) is worded by its method.
        for (const std::string_view phrase : {"submission rejected", "submission refused"})
            for (auto at = text.find(phrase); at != std::string::npos; at = text.find(phrase, at + 1))
            {
                const auto before = text.substr(at >= 9 ? at - 9 : 0, at >= 9 ? 9 : at);
                const bool device = before.ends_with("compute ") || before.ends_with("query ") || before.ends_with("sampling ");
                EXPECT_TRUE(device) << "hand-written job submission rejection near: " << text.substr(at >= 40 ? at - 40 : 0, 60);
            }
        const bool deliverOnce = text.find("make_shared<bool>") != std::string::npos;
        EXPECT_FALSE(deliverOnce) << "deliver-once state belongs to MeshSupport::QueuedJobDelivery";
        // A transaction on the shared lifecycle guards its typed sink; the lifecycle finalizes.
        const bool onLifecycle = text.find("GpuTransactionCore") != std::string::npos;
        const bool handWrites = !onLifecycle && text.find("GuardEditorProcessingResult(") != std::string::npos &&
                                text.find("FinalizeUnpublishedOnMainThread") != std::string::npos;
        if (std::find(frontReadbackOwners.begin(), frontReadbackOwners.end(), name) == frontReadbackOwners.end())
            EXPECT_EQ(text.find("BeginGpuFrontReadback("), std::string::npos)
                << "a GPU transaction's Accept readback belongs to Runtime.GpuTransactionLifecycle";
        const bool listed = std::find(handWritten.begin(), handWritten.end(), name) != handWritten.end();
        if (handWrites)
            EXPECT_TRUE(listed)
                << "a queued operation uses MeshSupport::QueuedJobDelivery instead of a hand-written guarded sink and finalizer";
        else
            EXPECT_FALSE(listed) << "stale allowlist entry: the file no longer hand-writes its job lifecycle; remove it";
        if (listed) ++matched;
    }
    EXPECT_GT(scanned, 20u);
    EXPECT_EQ(matched, handWritten.size()) << "an allowlisted file is missing";
}

// RUNTIME-314 drift guard: runtime sources reuse `Geometry::Validation::IsFinite` for glm vectors,
// the canonical `GeometrySources::PropertyNames::kPosition` and the undoable scalar publisher
// instead of re-writing them locally. (GPU neighbour queries: the source rule
// RuntimeReuseDriftGuard.GpuNeighbourQueriesUseWorkerQuerySessions in tools/repo/source_rules.py.)
TEST(RuntimeReuseDriftGuard, RuntimeUsesTheSharedFiniteCheckAndPositionName)
{
    namespace fs = std::filesystem;
    const auto source = fs::path{INTRINSIC_SOURCE_DIR};
    const auto root = source / "src" / "runtime";
    ASSERT_TRUE(fs::exists(root)) << root;
    const auto read = [](const fs::path& path) {
        std::ifstream file(path);
        return std::string{std::istreambuf_iterator<char>(file), {}};
    };
    // Component-wise finite checks that must stay, each for a type or semantics
    // Geometry::Validation::IsFinite does not cover; each site carries a one-line reason.
    constexpr std::array<std::string_view, 6> allowedFiniteChecks{
        "GeometryIntegration/Runtime.VertexAttributeBinding.cpp",          // glm::vec4 attribute rows
        "ImGui/Runtime.ImGuiAdapter.cpp",                                  // ImVec2/ImVec4 clip rectangles
        "Modules/TextureBake/Runtime.TextureBakeModule.cpp",               // glm::vec4 texel values
        "AssetWorkflow/Runtime.AssetWorkflowModelMaterialization.cpp",     // glm::quat / glm::mat4 node transforms
        "Visualization/Runtime.VisualizationRecipes.cpp",                  // float overload also serves templated scalars
        "Editor/Operations/Runtime.VisualizationEditingOperations.Actions.cpp", // glm::vec4 presentation default
    };
    // Sources that keep the serialized "v:position" default because a module boundary forbids
    // them from importing Extrinsic.ECS.Components.GeometrySources. Every entry must be a
    // --check-source of one of these boundary tests in tests/CMakeLists.txt.
    constexpr std::array<std::string_view, 2> positionBoundaries{
        "ProcessingCompilationLocality.ConfigPropertyTypes",
        "ProcessingCompilationLocality.ConsolidationContracts",
    };
    // RUNTIME-349: kernel density and point spacing left the boundary and use kPosition; density weights left the
    // engine (ARCH-022 Q90).
    constexpr std::array<std::string_view, 17> allowedPositionLiterals{
        "GeometryIntegration/Runtime.GeometryProperty.Types.cpp",
        "Modules/Clustering/Runtime.ClusteringTypes.cppm",
        "Modules/Curvature/Runtime.MeshCurvatureConfig.cpp",
        "Modules/Curvature/Runtime.MeshCurvatureConfig.cppm",
        "Modules/CurvatureSegmentation/Runtime.CurvatureSegmentationConfig.cppm",
        "Modules/Geodesics/Runtime.GeodesicsConfig.cpp",
        "Modules/Geodesics/Runtime.GeodesicsConfig.cppm",
        "Modules/KeypointAnalysis/Runtime.KeypointAnalysisConfig.cppm",
        "Modules/NormalEstimation/Runtime.NormalEstimationConfig.cppm",
        "Modules/OutlierAnalysis/Runtime.OutlierAnalysisConfig.cppm",
        "Modules/Parameterization/Runtime.ParameterizationConfig.cppm",
        "Modules/PointCloudConsolidation/Runtime.PointCloudConsolidationTypes.cppm",
        "Modules/PointConstruction/Runtime.PointConstructionConfig.cppm",
        "Modules/PointSampling/Runtime.PointSamplingConfig.cppm",
        "Modules/ProgressivePoisson/Runtime.ProgressivePoissonConfig.cppm",
        "Modules/Registration/Runtime.CoherentPointDriftConfig.cppm",
        "Modules/Registration/Runtime.RegistrationConfig.cppm",
    };
    const std::string cmake = read(source / "tests" / "CMakeLists.txt");
    std::string boundarySources;
    for (const std::string_view boundary : positionBoundaries)
    {
        const auto begin = cmake.find("intrinsic_add_module_boundary_test(" + std::string{boundary} + "\n");
        ASSERT_NE(begin, std::string::npos) << boundary;
        boundarySources += cmake.substr(begin, cmake.find(")\n", begin) - begin);
    }
    for (const std::string_view allowed : allowedPositionLiterals)
        EXPECT_NE(boundarySources.find("--check-source src/runtime/" + std::string{allowed} + "\n"), std::string::npos)
            << allowed << " keeps \"v:position\" but is not inside a GeometrySources-forbidding boundary; use kPosition";

    // Member chains (`sphere.Center.x`, `a->b[i].x`) and scalar wrappers (`IsFinite(v.x) && IsFinite(v.y)`) count.
    const std::regex handWrittenFinite{
        R"((?:std::isfinite|\bIsFinite)\(([\w.\[\]>-]+)\.x\)\s*(?:&&|\|\|)\s*!?\s*(?:std::isfinite|\bIsFinite)\(\1\.y\))"};
    const std::regex executesHistory{R"(CommandHistory\s*->\s*Execute\s*\()"};
    // Undoable scalar outputs publish through PublishPointScalarField; these files may prepare
    // scalar snapshots and execute history themselves.
    constexpr std::array<std::string_view, 4> allowedScalarPublishers{
        "Editor/Operations/Runtime.GeometryProcessingOperations.PointProperties.cpp", // the publisher
        "Modules/OutlierAnalysis/Runtime.OutlierAnalysisOperation.cpp", // two columns + stamp (RUNTIME-354)
        "Editor/Operations/Runtime.MeshFieldOperations.Geodesics.cpp", // double distances with infinity sentinels
        "Editor/Operations/Runtime.ScalarRidgeOperations.cpp", // vertex and edge outputs, two slot sets
    };
    std::size_t scanned = 0;
    std::size_t finiteMatched = 0;
    std::size_t positionMatched = 0;
    std::size_t publisherMatched = 0;
    for (const auto& entry : fs::recursive_directory_iterator(root))
    {
        const auto extension = entry.path().extension();
        if (!entry.is_regular_file() || (extension != ".cpp" && extension != ".cppm" && extension != ".hpp")) continue;
        const std::string text = read(entry.path());
        const auto name = entry.path().lexically_relative(root).generic_string();
        ++scanned;
        SCOPED_TRACE(name);
        const bool positionLiteral = text.find("\"v:position\"") != std::string::npos;
        if (std::find(allowedPositionLiterals.begin(), allowedPositionLiterals.end(), name) != allowedPositionLiterals.end())
        {
            EXPECT_TRUE(positionLiteral) << "stale allowlist entry: the file no longer keeps \"v:position\"; remove it";
            ++positionMatched;
        }
        else
            EXPECT_FALSE(positionLiteral) << "use GeometrySources::PropertyNames::kPosition";
        const bool finite = std::regex_search(text, handWrittenFinite);
        if (std::find(allowedFiniteChecks.begin(), allowedFiniteChecks.end(), name) != allowedFiniteChecks.end())
        {
            EXPECT_TRUE(finite) << "stale allowlist entry: the file no longer hand-writes a finite check; remove it";
            ++finiteMatched;
        }
        else
            EXPECT_FALSE(finite) << "use Geometry::Validation::IsFinite for glm vectors";
        const bool publishes = text.find("PrepareGeometryScalarProperty(") != std::string::npos &&
            std::regex_search(text, executesHistory);
        if (std::find(allowedScalarPublishers.begin(), allowedScalarPublishers.end(), name) != allowedScalarPublishers.end())
        {
            EXPECT_TRUE(publishes) << "stale allowlist entry: the file no longer publishes scalars itself; remove it";
            ++publisherMatched;
        }
        else
            EXPECT_FALSE(publishes) << "publish undoable scalar outputs through PublishPointScalarField";
    }
    EXPECT_GT(scanned, 100u);
    EXPECT_EQ(publisherMatched, allowedScalarPublishers.size()) << "an allowlisted scalar publisher is missing";
    EXPECT_EQ(positionMatched, allowedPositionLiterals.size()) << "an allowlisted position-literal file is missing";
    EXPECT_EQ(finiteMatched, allowedFiniteChecks.size()) << "an allowlisted finite-check file is missing";
}

// RUNTIME-317 drift guard: every editor job identity an operation builds names the backend domain
// its config requested, so the Jobs window and jobs_list never show an unknown request for a new
// operation, including the identities GPU transactions assign to their core.
TEST(QueuedEditorJobDriftGuard, EveryEditorJobIdentityNamesItsRequestedDomain)
{
    namespace fs = std::filesystem;
    const auto root = fs::path{INTRINSIC_SOURCE_DIR} / "src" / "runtime" / "Editor" / "Operations";
    ASSERT_TRUE(fs::exists(root)) << root;
    // A constructed identity, or a GPU transaction's designated `Identity = {.EntityId = ...}`.
    const std::regex construction{R"(EditorJobIdentity(\s+\w+)?\s*\{|(\.|->)Identity\s*=\s*\{(?=\s*\.))"};
    std::size_t identities = 0;
    // RUNTIME-354: operations also live in their modules, and the runtime's shared identity is in a .cppm.
    for (const auto& dir : {root, fs::path{INTRINSIC_SOURCE_DIR} / "src" / "runtime" / "Modules"})
        for (const auto& entry : fs::recursive_directory_iterator(dir))
        {
            const auto extension = entry.path().extension();
            if (extension != ".cpp" && extension != ".hpp" && extension != ".cppm")
                continue;
            std::ifstream file(entry.path());
            const std::string text{std::istreambuf_iterator<char>(file), {}};
            SCOPED_TRACE(entry.path().filename().string());
            for (auto it = std::sregex_iterator(text.begin(), text.end(), construction); it != std::sregex_iterator();
                ++it)
            {
                // The initializer runs to the brace that closes the one the match opened.
                std::size_t at = static_cast<std::size_t>(it->position() + it->length());
                for (int depth = 1; depth > 0 && at < text.size(); ++at)
                    depth += text[at] == '{' ? 1 : text[at] == '}' ? -1 : 0;
                const std::string initializer = text.substr(
                    static_cast<std::size_t>(it->position()), at - static_cast<std::size_t>(it->position()));
                if (initializer.ends_with("{}"))
                    continue; // a default member, filled before submission
                ++identities;
                EXPECT_NE(initializer.find(".RequestedDomain"), std::string::npos)
                    << "an editor job identity names its config's requested backend domain: " << initializer;
            }
        }
    EXPECT_GE(identities, 20u) << "the scan found the operations' identities"; // bilateral filter, descriptors: plugins
}
