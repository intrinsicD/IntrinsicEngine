module;

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

module Extrinsic.Runtime.ObjectSpaceNormalBakeService;

import Extrinsic.Asset.Registry;
import Extrinsic.Asset.Service;
import Extrinsic.Core.Error;
import Extrinsic.Core.Filesystem.PathResolver;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Graphics.GpuAssetCache;
import Extrinsic.Graphics.GpuWorld;
import Extrinsic.Graphics.ObjectSpaceNormalTextureBake;
import Extrinsic.Graphics.Renderer;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.PipelineManager;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.ObjectSpaceNormalBakeBinding;
import Extrinsic.Runtime.ObjectSpaceNormalBakeQueue;
import Extrinsic.Runtime.ObjectSpaceNormalBakeSubmission;
import Extrinsic.Runtime.RenderExtraction;
import Extrinsic.Runtime.WorldHandle;

namespace Extrinsic::Runtime
{
    namespace
    {
        constexpr std::size_t kMaxActiveBakeEntries = 64u;
        constexpr std::size_t kMaxRetainedIdentityBytes =
            256u * 1024u * 1024u;
        constexpr std::size_t kMaxProvenReadyEntries = 32u;
        constexpr std::size_t kMaxProvenReadyBytes =
            256u * 1024u * 1024u;
        constexpr std::size_t kMaxDilationEntries = 4u;
        constexpr std::size_t kMaxDilationBytes =
            256u * 1024u * 1024u;
        constexpr std::uint32_t kMaxMetadataPathProbes = 64u;
        constexpr std::size_t kMaxSubmissionsPerFrame = 1u;
        constexpr std::size_t kMaxBindingsPerDrain = 8u;

        enum class ObjectSpaceNormalBakePlanStatus : std::uint8_t
        {
            Ready,
            NotReady,
            Invalid,
            DeviceLost,
        };

        struct ObjectSpaceNormalBakePlanResult
        {
            ObjectSpaceNormalBakePlanStatus Status{
                ObjectSpaceNormalBakePlanStatus::NotReady};
            Graphics::ObjectSpaceNormalTextureBakePlan Plan{};
            Graphics::GpuGeometryHandle Geometry{};
            std::uint64_t GeometryContentRevision{0u};
            std::optional<std::size_t> DilationEntry{};

            [[nodiscard]] bool Succeeded() const noexcept
            {
                return Status == ObjectSpaceNormalBakePlanStatus::Ready &&
                       Plan.Succeeded() &&
                       GeometryContentRevision != 0u;
            }
        };

        using ObjectSpaceNormalBakePlanProvider =
            std::function<ObjectSpaceNormalBakePlanResult(
                const RuntimeObjectSpaceNormalBakeSubmission&)>;
        using ObjectSpaceNormalBakeMarkReady =
            std::function<Core::Result(
                Graphics::GpuAssetCache&,
                const RuntimeObjectSpaceNormalBakeGpuSubmissionTicket&,
                std::uint64_t)>;

        struct GeneratedNormalBakeMetadata
        {
            std::uint32_t SchemaVersion{
                kRuntimeObjectSpaceNormalBakeIdentitySchemaVersion};
            std::shared_ptr<const RuntimeObjectSpaceNormalBakeIdentity>
                Identity{};
            std::uint64_t NonReusableSerial{0u};
        };

        struct GeneratedAssetAllocation
        {
            Assets::AssetId Asset{};
            bool ReusedPath{false};
        };

        enum class BakeWorkPhase : std::uint8_t
        {
            Queued,
            PendingGpu,
            ProvenReady,
            RecordedFailureHold,
        };

        struct BakeWaiter
        {
            RuntimeObjectSpaceNormalBakeStaleKey StaleKey{};
            RuntimeObjectSpaceNormalBakeAssetSelection Selection{
                RuntimeObjectSpaceNormalBakeAssetSelection::None};
            std::uint64_t GeometryContentRevision{0u};
        };

        struct DilationEntry
        {
            std::uint32_t Width{0u};
            std::uint32_t Height{0u};
            std::size_t Bytes{0u};
            std::uint64_t LastUseFrame{0u};
            std::uint64_t SafeRetireFrame{0u};
            std::uint32_t References{0u};
            RHI::TextureLayout ScratchLayout{
                RHI::TextureLayout::Undefined};
            Graphics::ObjectSpaceNormalTextureBakeDilationResourceLease
                Resources{};
        };

        struct BakeWork
        {
            std::uint64_t WorkSerial{0u};
            std::optional<RuntimeObjectSpaceNormalBakeIdentity> Identity{};
            Assets::AssetId Asset{};
            BakeWorkPhase Phase{BakeWorkPhase::Queued};
            std::vector<BakeWaiter> Waiters{};
            std::vector<RuntimeObjectSpaceNormalBakeTarget> BoundTargets{};
            RuntimeObjectSpaceNormalBakeGpuSubmissionTicket Ticket{};
            std::optional<std::size_t> DilationEntry{};
            std::uint64_t CacheGeneration{0u};
            std::uint64_t ReleaseFrame{0u};
            std::size_t IdentityBytes{0u};
            std::size_t OutputBytes{0u};
            std::uint64_t LastUseSerial{0u};
        };

        struct ObjectSpaceNormalBakeGpuDiagnostics
        {
            std::uint64_t PendingStaleDiscards{0u};
            std::uint64_t PlanNotReady{0u};
            std::uint64_t PlanRejected{0u};
            std::uint64_t CacheRejected{0u};
            std::uint64_t RecordedSubmissions{0u};
            std::uint64_t RecordFailures{0u};
            std::uint64_t ReadyFrameFailures{0u};
            std::uint64_t WaitingBindings{0u};
            std::uint64_t BoundCompletions{0u};
            std::uint64_t StaleCompletions{0u};
            std::uint64_t InvalidCompletions{0u};
            std::uint64_t ShutdownDiscards{0u};
            std::uint64_t LastRecordAttempted{0u};
            std::uint64_t LastRecordSubmitted{0u};
            std::uint64_t LastDrainProcessed{0u};
            std::uint64_t LastDrainBound{0u};
            std::uint64_t AllocatedAssets{0u};
            std::uint64_t PendingIdentityReuses{0u};
            std::uint64_t ProvenReadyReuses{0u};
            std::uint64_t CapacityRejected{0u};
        };

        [[nodiscard]] std::size_t IdentityByteCount(
            const RuntimeObjectSpaceNormalBakeIdentity& identity) noexcept
        {
            return identity.PackedPositionBytes.size() +
                   identity.SurfaceIndexBytes.size() +
                   identity.ResolvedTexcoordBytes.size() +
                   identity.ResolvedNormalBytes.size();
        }

        [[nodiscard]] std::size_t OutputByteCount(
            const RuntimeObjectSpaceNormalBakeIdentity& identity) noexcept
        {
            constexpr std::size_t kBytesPerPixel = 4u;
            if (identity.Width == 0u || identity.Height == 0u ||
                static_cast<std::size_t>(identity.Width) >
                    std::numeric_limits<std::size_t>::max() /
                        static_cast<std::size_t>(identity.Height))
            {
                return 0u;
            }
            const std::size_t pixels =
                static_cast<std::size_t>(identity.Width) *
                static_cast<std::size_t>(identity.Height);
            if (pixels >
                std::numeric_limits<std::size_t>::max() / kBytesPerPixel)
            {
                return 0u;
            }
            return pixels * kBytesPerPixel;
        }

        [[nodiscard]] std::string HexDigest(const std::uint64_t digest)
        {
            std::array<char, 16u> digits{};
            digits.fill('0');
            std::array<char, 16u> encoded{};
            const auto converted =
                std::to_chars(
                    encoded.data(),
                    encoded.data() + encoded.size(),
                    digest,
                    16);
            if (converted.ec != std::errc{})
                return std::string(digits.data(), digits.size());
            const std::size_t count =
                static_cast<std::size_t>(converted.ptr - encoded.data());
            std::copy(
                encoded.data(),
                converted.ptr,
                digits.end() - static_cast<std::ptrdiff_t>(count));
            return std::string(digits.data(), digits.size());
        }

        [[nodiscard]] bool TargetAddressMatches(
            const RuntimeObjectSpaceNormalBakeTarget& lhs,
            const RuntimeObjectSpaceNormalBakeTarget& rhs) noexcept
        {
            return lhs.World == rhs.World &&
                   lhs.BindingEpoch == rhs.BindingEpoch &&
                   lhs.Entity == rhs.Entity &&
                   lhs.StableEntityId == rhs.StableEntityId &&
                   lhs.PresentationKey == rhs.PresentationKey &&
                   lhs.Semantic == rhs.Semantic;
        }

        [[nodiscard]] Graphics::ObjectSpaceNormalTextureBakeOptions
        OptionsForIdentity(
            const RuntimeObjectSpaceNormalBakeIdentity& identity) noexcept
        {
            return Graphics::ObjectSpaceNormalTextureBakeOptions{
                .Width = identity.Width,
                .Height = identity.Height,
                .PaddingTexels = identity.PaddingTexels,
                .AtlasUvEpsilon =
                    std::bit_cast<float>(identity.AtlasUvEpsilonBits),
                .DegenerateUvAreaEpsilon =
                    std::bit_cast<float>(
                        identity.DegenerateUvAreaEpsilonBits),
                .DegenerateNormalLengthEpsilon =
                    std::bit_cast<float>(
                        identity.DegenerateNormalLengthEpsilonBits),
                .Space = identity.Space,
            };
        }

        [[nodiscard]] Graphics::ObjectSpaceNormalTextureBakePlan
        BuildDeterministicTestPlan(
            const RuntimeObjectSpaceNormalBakeSubmission& submission)
        {
            if (!submission.Identity.has_value())
                return {};

            const RuntimeObjectSpaceNormalBakeIdentity& identity =
                *submission.Identity;
            return Graphics::BuildObjectSpaceNormalTextureBakePlan(
                Graphics::ObjectSpaceNormalTextureBakePlanRequest{
                    .GeneratedTextureAsset =
                        submission.GeneratedTextureAsset,
                    .Geometry =
                        Graphics::ObjectSpaceNormalTextureBakeGeometryBuffers{
                            .IndexBuffer = RHI::BufferHandle{40u, 1u},
                            .TexcoordBDA = 0x1000u,
                            .NormalBDA = 0x2000u,
                            .VertexCount = identity.VertexCount,
                            .FirstIndex = 0u,
                            .IndexCount = identity.SurfaceIndexCount,
                        },
                    .Options = OptionsForIdentity(identity),
                    .SourceKey =
                        MakeObjectSpaceNormalBakeGraphicsCompletionKey(
                            submission)
                            .Source,
                    .Pipeline = RHI::PipelineHandle{5u, 1u},
                    .DebugName =
                        "runtime-object-space-normal-bake-service-test",
                });
        }
    }

    struct ObjectSpaceNormalBakeService::Impl
    {
        RuntimeObjectSpaceNormalBakeQueue Queue{};
        ObjectSpaceNormalBakeServiceDependencies Dependencies{};
        ObjectSpaceNormalBakePlanProvider BuildPlan{};
        ObjectSpaceNormalBakeMarkReady MarkReady{};
        std::vector<BakeWork> Work{};
        std::vector<DilationEntry> Dilation{};
        std::optional<RHI::PipelineManager::PipelineLease> RasterPipeline{};
        ObjectSpaceNormalBakeGpuDiagnostics Diagnostics{};
        ObjectSpaceNormalBakeServiceTestHooks TestHooks{};
        WorldHandle BoundWorld{};
        std::uint64_t BindingEpoch{0u};
        ECS::Scene::Registry* Scene{nullptr};
        std::uint64_t NextWorkSerial{1u};
        std::uint64_t NextNonReusableSerial{1u};
        std::uint64_t LastUseSerial{1u};
        std::uint64_t TestPlanBuildCount{0u};
        std::uint64_t TestReadyPublicationCount{0u};
        GpuQueueParticipantHandle ParticipantHandle{};

        [[nodiscard]] std::uint64_t CurrentFrame() const noexcept
        {
            return Dependencies.Device != nullptr
                ? Dependencies.Device->GetGlobalFrameNumber()
                : 0u;
        }

        [[nodiscard]] std::uint64_t ReadyFrame() const noexcept
        {
            if (Dependencies.Device == nullptr)
                return 0u;
            return ObjectSpaceNormalBakeReadyFrame(
                Dependencies.Device->GetGlobalFrameNumber(),
                Dependencies.Device->GetFramesInFlight());
        }

        [[nodiscard]] std::size_t RetainedIdentityBytes() const noexcept
        {
            std::size_t bytes = 0u;
            for (const BakeWork& work : Work)
                bytes += work.IdentityBytes;
            return bytes;
        }

        [[nodiscard]] std::size_t ProvenReadyCount() const noexcept
        {
            return static_cast<std::size_t>(std::ranges::count_if(
                Work,
                [](const BakeWork& work)
                {
                    return work.Phase == BakeWorkPhase::ProvenReady;
                }));
        }

        [[nodiscard]] std::size_t ProvenReadyBytes() const noexcept
        {
            std::size_t bytes = 0u;
            for (const BakeWork& work : Work)
            {
                if (work.Phase == BakeWorkPhase::ProvenReady)
                    bytes += work.OutputBytes;
            }
            return bytes;
        }

        [[nodiscard]] std::size_t DilationBytes() const noexcept
        {
            std::size_t bytes = 0u;
            for (const DilationEntry& entry : Dilation)
                bytes += entry.Bytes;
            return bytes;
        }

        void ReleaseDilation(
            const std::optional<std::size_t> index,
            const std::uint64_t safeFrame) noexcept
        {
            if (!index.has_value() || *index >= Dilation.size())
                return;
            DilationEntry& entry = Dilation[*index];
            if (entry.References != 0u)
                --entry.References;
            entry.SafeRetireFrame =
                std::max(entry.SafeRetireFrame, safeFrame);
        }

        void DestroyGeneratedAsset(const Assets::AssetId asset)
        {
            if (!asset.IsValid())
                return;
            if (Dependencies.GpuAssets != nullptr)
                Dependencies.GpuAssets->NotifyDestroyed(asset);
            if (Dependencies.Assets != nullptr &&
                Dependencies.Assets->IsAlive(asset))
            {
                (void)Dependencies.Assets->Destroy(asset);
            }
        }

        void RetireWork(
            const std::size_t index,
            const bool afterDeviceIdle = false)
        {
            if (index >= Work.size())
                return;

            BakeWork& work = Work[index];
            for (const BakeWaiter& waiter : work.Waiters)
                (void)Queue.Discard(waiter.StaleKey);

            if (work.Phase == BakeWorkPhase::PendingGpu &&
                Dependencies.GpuAssets != nullptr &&
                work.CacheGeneration != 0u)
            {
                (void)Dependencies.GpuAssets->FailGpuProducedTexture(
                    work.Asset,
                    work.CacheGeneration);
            }
            ReleaseDilation(
                work.DilationEntry,
                afterDeviceIdle ? 0u : work.ReleaseFrame);
            DestroyGeneratedAsset(work.Asset);
            Work.erase(Work.begin() + static_cast<std::ptrdiff_t>(index));
        }

        [[nodiscard]] bool EvictOneUnusedProven()
        {
            std::optional<std::size_t> candidate{};
            for (std::size_t index = 0u; index < Work.size(); ++index)
            {
                const BakeWork& work = Work[index];
                if (work.Phase != BakeWorkPhase::ProvenReady ||
                    !work.Waiters.empty() ||
                    !work.BoundTargets.empty())
                {
                    continue;
                }
                if (!candidate.has_value() ||
                    work.LastUseSerial < Work[*candidate].LastUseSerial ||
                    (work.LastUseSerial ==
                         Work[*candidate].LastUseSerial &&
                     work.WorkSerial < Work[*candidate].WorkSerial))
                {
                    candidate = index;
                }
            }
            if (!candidate.has_value())
                return false;
            RetireWork(*candidate);
            return true;
        }

        [[nodiscard]] bool EnsureWorkCapacity(
            const std::size_t identityBytes)
        {
            if (identityBytes > kMaxRetainedIdentityBytes)
                return false;
            while ((Work.size() >= kMaxActiveBakeEntries ||
                    RetainedIdentityBytes() >
                        kMaxRetainedIdentityBytes - identityBytes) &&
                   EvictOneUnusedProven())
            {
            }
            return Work.size() < kMaxActiveBakeEntries &&
                   identityBytes <= kMaxRetainedIdentityBytes &&
                   RetainedIdentityBytes() <=
                       kMaxRetainedIdentityBytes - identityBytes;
        }

        [[nodiscard]] bool EnsureProvenCapacity(
            const std::size_t candidateOutputBytes)
        {
            if (candidateOutputBytes == 0u ||
                candidateOutputBytes > kMaxProvenReadyBytes)
            {
                return false;
            }
            while ((ProvenReadyCount() >= kMaxProvenReadyEntries ||
                    ProvenReadyBytes() >
                        kMaxProvenReadyBytes - candidateOutputBytes) &&
                   EvictOneUnusedProven())
            {
            }
            return ProvenReadyCount() < kMaxProvenReadyEntries &&
                   candidateOutputBytes <= kMaxProvenReadyBytes &&
                   ProvenReadyBytes() <=
                       kMaxProvenReadyBytes - candidateOutputBytes;
        }

        [[nodiscard]] Core::Expected<GeneratedAssetAllocation>
        AllocateGeneratedAsset(
            const std::optional<RuntimeObjectSpaceNormalBakeIdentity>&
                identity)
        {
            if (Dependencies.Assets == nullptr)
            {
                return Core::Err<GeneratedAssetAllocation>(
                    Core::ErrorCode::InvalidState);
            }

            if (!identity.has_value())
            {
                std::uint64_t serial = NextNonReusableSerial++;
                if (serial == 0u)
                    serial = NextNonReusableSerial++;
                const std::string path =
                    "intrinsic-runtime-generated/object-space-normal-bake/v1/"
                    "non-reusable-" +
                    std::to_string(serial) + ".metadata";
                const GeneratedNormalBakeMetadata payload{
                    .NonReusableSerial = serial,
                };
                auto loaded =
                    Dependencies.Assets
                        ->Load<GeneratedNormalBakeMetadata>(
                            path,
                            [payload](
                                std::string_view,
                                Assets::AssetId)
                                -> Core::Expected<
                                    GeneratedNormalBakeMetadata>
                            {
                                return payload;
                            });
                if (!loaded.has_value())
                {
                    return Core::Err<GeneratedAssetAllocation>(
                        loaded.error());
                }
                if (Core::Result completed =
                        Dependencies.Assets
                            ->CompleteCpuLoadAndFlushEvent(*loaded);
                    !completed.has_value())
                {
                    return Core::Err<GeneratedAssetAllocation>(
                        completed.error());
                }
                ++Diagnostics.AllocatedAssets;
                return GeneratedAssetAllocation{
                    .Asset = *loaded,
                };
            }

            auto retainedIdentity =
                std::make_shared<const RuntimeObjectSpaceNormalBakeIdentity>(
                    *identity);
            const std::uint64_t computed =
                ComputeRuntimeObjectSpaceNormalBakeIdentityDigest(
                    *retainedIdentity);
            const std::uint64_t digest =
                TestHooks.IdentityDigestOverride != 0u
                    ? TestHooks.IdentityDigestOverride
                    : computed;
            const std::string base =
                "intrinsic-runtime-generated/object-space-normal-bake/v1/" +
                HexDigest(digest);

            for (std::uint32_t probe = 0u;
                 probe < kMaxMetadataPathProbes;
                 ++probe)
            {
                const std::string path =
                    base +
                    (probe == 0u
                         ? std::string{}
                         : "-probe-" + std::to_string(probe)) +
                    ".metadata";
                const GeneratedNormalBakeMetadata payload{
                    .Identity = retainedIdentity,
                };
                auto loaded =
                    Dependencies.Assets
                        ->Load<GeneratedNormalBakeMetadata>(
                            path,
                            [payload](
                                std::string_view,
                                Assets::AssetId)
                                -> Core::Expected<
                                    GeneratedNormalBakeMetadata>
                            {
                                return payload;
                            });
                if (!loaded.has_value())
                {
                    if (loaded.error() == Core::ErrorCode::TypeMismatch)
                        continue;
                    return Core::Err<GeneratedAssetAllocation>(
                        loaded.error());
                }

                const auto metadata =
                    Dependencies.Assets
                        ->Read<GeneratedNormalBakeMetadata>(*loaded);
                if (!metadata.has_value() || metadata->size() != 1u)
                {
                    return Core::Err<GeneratedAssetAllocation>(
                        metadata.has_value()
                            ? Core::ErrorCode::InvalidState
                            : metadata.error());
                }
                if ((*metadata)[0].SchemaVersion !=
                        kRuntimeObjectSpaceNormalBakeIdentitySchemaVersion ||
                    (*metadata)[0].Identity == nullptr ||
                    *(*metadata)[0].Identity != *retainedIdentity)
                {
                    continue;
                }

                if (Core::Result completed =
                        Dependencies.Assets
                            ->CompleteCpuLoadAndFlushEvent(*loaded);
                    !completed.has_value())
                {
                    return Core::Err<GeneratedAssetAllocation>(
                        completed.error());
                }
                ++Diagnostics.AllocatedAssets;
                return GeneratedAssetAllocation{
                    .Asset = *loaded,
                    .ReusedPath =
                        (*metadata)[0].Identity != retainedIdentity,
                };
            }

            return Core::Err<GeneratedAssetAllocation>(
                Core::ErrorCode::OutOfRange);
        }

        [[nodiscard]] std::optional<std::size_t> FindIdentityWork(
            const RuntimeObjectSpaceNormalBakeIdentity& identity) const
        {
            for (std::size_t index = 0u; index < Work.size(); ++index)
            {
                if (Work[index].Identity.has_value() &&
                    *Work[index].Identity == identity)
                {
                    return index;
                }
            }
            return std::nullopt;
        }

        [[nodiscard]] bool HasExactReadyView(
            const BakeWork& work) const
        {
            if (Dependencies.Assets == nullptr ||
                Dependencies.GpuAssets == nullptr ||
                !Dependencies.Assets->IsAlive(work.Asset) ||
                Dependencies.GpuAssets->GetState(work.Asset) !=
                    Graphics::GpuAssetState::Ready)
            {
                return false;
            }
            const auto view = Dependencies.GpuAssets->GetView(work.Asset);
            return view.has_value() &&
                   view->Kind == Graphics::GpuAssetKind::Texture &&
                   view->Generation == work.CacheGeneration;
        }

        void RemoveBoundTargetReferences(
            const RuntimeObjectSpaceNormalBakeTarget& target)
        {
            for (BakeWork& work : Work)
            {
                std::erase_if(
                    work.BoundTargets,
                    [&target](
                        const RuntimeObjectSpaceNormalBakeTarget& bound)
                    {
                        return TargetAddressMatches(bound, target);
                    });
            }
        }

        void PrepareScheduledRequests()
        {
            std::vector<RuntimeObjectSpaceNormalBakeSubmission> pending =
                Queue.TakePendingSubmissions();

            for (RuntimeObjectSpaceNormalBakeSubmission& submission : pending)
            {
                if (!Queue.IsLatest(submission.StaleKey))
                {
                    ++Diagnostics.PendingStaleDiscards;
                    continue;
                }

                RemoveBoundTargetReferences(submission.Target);

                if (submission.Identity.has_value())
                {
                    if (const auto found =
                            FindIdentityWork(*submission.Identity);
                        found.has_value())
                    {
                        BakeWork& work = Work[*found];
                        RuntimeObjectSpaceNormalBakeAssetSelection selection =
                            RuntimeObjectSpaceNormalBakeAssetSelection::
                                PendingIdentityReuse;
                        if (work.Phase == BakeWorkPhase::ProvenReady &&
                            HasExactReadyView(work))
                        {
                            selection =
                                RuntimeObjectSpaceNormalBakeAssetSelection::
                                    ProvenReadyReuse;
                            ++Diagnostics.ProvenReadyReuses;
                        }
                        else
                        {
                            ++Diagnostics.PendingIdentityReuses;
                            if (work.Phase ==
                                BakeWorkPhase::ProvenReady)
                            {
                                if (Dependencies.GpuAssets != nullptr)
                                {
                                    Dependencies.GpuAssets->NotifyDestroyed(
                                        work.Asset);
                                }
                                work.Phase = BakeWorkPhase::Queued;
                                work.CacheGeneration = 0u;
                                work.Ticket = {};
                            }
                        }
                        work.Waiters.push_back(BakeWaiter{
                            .StaleKey = submission.StaleKey,
                            .Selection = selection,
                        });
                        work.LastUseSerial = LastUseSerial++;
                        continue;
                    }
                }

                const std::size_t identityBytes =
                    submission.Identity.has_value()
                        ? IdentityByteCount(*submission.Identity)
                        : 0u;
                if (!EnsureWorkCapacity(identityBytes))
                {
                    ++Diagnostics.CapacityRejected;
                    (void)Queue.Discard(submission.StaleKey);
                    continue;
                }

                auto allocated =
                    AllocateGeneratedAsset(submission.Identity);
                if (!allocated.has_value())
                {
                    ++Diagnostics.CapacityRejected;
                    (void)Queue.Discard(submission.StaleKey);
                    continue;
                }

                std::uint64_t workSerial = NextWorkSerial++;
                if (workSerial == 0u)
                    workSerial = NextWorkSerial++;
                const bool hasIdentity = submission.Identity.has_value();
                BakeWork work{
                    .WorkSerial = workSerial,
                    .Identity = std::move(submission.Identity),
                    .Asset = allocated->Asset,
                    .Waiters =
                        {BakeWaiter{
                            .StaleKey = submission.StaleKey,
                            .Selection =
                                hasIdentity
                                    ? RuntimeObjectSpaceNormalBakeAssetSelection::
                                          IdentityInserted
                                    : RuntimeObjectSpaceNormalBakeAssetSelection::
                                          NonReusableAllocated,
                        }},
                    .IdentityBytes = identityBytes,
                    .LastUseSerial = LastUseSerial++,
                };
                if (work.Identity.has_value())
                    work.OutputBytes = OutputByteCount(*work.Identity);
                Work.push_back(std::move(work));
            }
        }

        [[nodiscard]] std::optional<std::size_t> AcquireDilation(
            const RuntimeObjectSpaceNormalBakeIdentity& identity)
        {
            if (identity.PaddingTexels == 0u)
                return std::nullopt;
            if (Dependencies.Device == nullptr ||
                !Dependencies.Device->IsOperational())
            {
                return std::nullopt;
            }

            for (std::size_t index = 0u;
                 index < Dilation.size();
                 ++index)
            {
                DilationEntry& entry = Dilation[index];
                if (entry.Width == identity.Width &&
                    entry.Height == identity.Height &&
                    entry.References == 0u &&
                    CurrentFrame() >= entry.SafeRetireFrame)
                {
                    ++entry.References;
                    entry.LastUseFrame = CurrentFrame();
                    return index;
                }
            }

            const std::size_t bytes = OutputByteCount(identity);
            if (bytes == 0u || bytes > kMaxDilationBytes)
            {
                return std::nullopt;
            }

            std::optional<std::size_t> replacement{};
            if (Dilation.size() >= kMaxDilationEntries ||
                DilationBytes() > kMaxDilationBytes - bytes)
            {
                for (std::size_t index = 0u;
                     index < Dilation.size();
                     ++index)
                {
                    const DilationEntry& entry = Dilation[index];
                    if (entry.References != 0u ||
                        CurrentFrame() < entry.SafeRetireFrame ||
                        DilationBytes() - entry.Bytes >
                            kMaxDilationBytes - bytes)
                    {
                        continue;
                    }
                    if (!replacement.has_value() ||
                        entry.LastUseFrame <
                            Dilation[*replacement].LastUseFrame ||
                        (entry.LastUseFrame ==
                             Dilation[*replacement].LastUseFrame &&
                         index < *replacement))
                    {
                        replacement = index;
                    }
                }
                if (!replacement.has_value())
                    return std::nullopt;
            }

            DilationEntry entry{
                .Width = identity.Width,
                .Height = identity.Height,
                .Bytes = bytes,
                .LastUseFrame = CurrentFrame(),
                .References = 1u,
            };
            const Core::Result initialized =
                entry.Resources.Initialize(
                    *Dependencies.Device,
                    Graphics::
                        MakeObjectSpaceNormalTextureBakeDilationResourceDesc(
                            OptionsForIdentity(identity),
                            Core::Filesystem::GetShaderPath(
                                "shaders/post_fullscreen.vert.spv"),
                            Core::Filesystem::GetShaderPath(
                                "shaders/object_space_normal_dilate.frag.spv"),
                            "Runtime.ObjectSpaceNormalBake.DilationScratch"));
            if (!initialized.has_value())
                return std::nullopt;

            if (replacement.has_value())
            {
                Dilation[*replacement] = std::move(entry);
                return replacement;
            }
            Dilation.push_back(std::move(entry));
            return Dilation.size() - 1u;
        }

        [[nodiscard]] bool EnsureRasterPipeline()
        {
            if (RasterPipeline.has_value() &&
                RasterPipeline->IsValid())
            {
                return true;
            }
            if (Dependencies.Renderer == nullptr ||
                Dependencies.Device == nullptr ||
                !Dependencies.Device->IsOperational())
            {
                return false;
            }

            auto lease =
                Dependencies.Renderer->GetPipelineManager().Create(
                    Graphics::MakeObjectSpaceNormalTextureBakePipelineDesc(
                        Core::Filesystem::GetShaderPath(
                            "shaders/object_space_normal_bake.vert.spv"),
                        Core::Filesystem::GetShaderPath(
                            "shaders/object_space_normal_bake.frag.spv")));
            if (!lease.has_value())
                return false;
            RasterPipeline.emplace(std::move(*lease));
            return true;
        }

        [[nodiscard]] ObjectSpaceNormalBakePlanResult
        BuildProductionPlan(
            const RuntimeObjectSpaceNormalBakeSubmission& submission)
        {
            if (Dependencies.Device == nullptr ||
                Dependencies.Renderer == nullptr ||
                Dependencies.RenderExtraction == nullptr ||
                !submission.Identity.has_value())
            {
                return ObjectSpaceNormalBakePlanResult{
                    .Status = ObjectSpaceNormalBakePlanStatus::Invalid,
                };
            }
            if (!Dependencies.Device->IsOperational())
            {
                return ObjectSpaceNormalBakePlanResult{
                    .Status = ObjectSpaceNormalBakePlanStatus::DeviceLost,
                };
            }

            const auto renderable =
                Dependencies.RenderExtraction
                    ->FindGpuRenderableAvailability(
                        submission.Target.StableEntityId);
            if (!renderable.has_value() ||
                !renderable->HasRenderable ||
                !renderable->Surface.HasGeometry)
            {
                return ObjectSpaceNormalBakePlanResult{
                    .Status = ObjectSpaceNormalBakePlanStatus::NotReady,
                };
            }

            Graphics::GpuGeometryResidencyView residency{};
            Graphics::GpuWorld& gpuWorld =
                Dependencies.Renderer->GetGpuWorld();
            if (!gpuWorld.TryGetGeometryResidencyView(
                    renderable->Surface.Geometry,
                    residency))
            {
                return ObjectSpaceNormalBakePlanResult{
                    .Status = ObjectSpaceNormalBakePlanStatus::NotReady,
                };
            }
            if (!ValidateObjectSpaceNormalBakeResidency(
                     *submission.Identity,
                     residency)
                     .Succeeded())
            {
                return ObjectSpaceNormalBakePlanResult{
                    .Status = ObjectSpaceNormalBakePlanStatus::Invalid,
                };
            }

            if (!EnsureRasterPipeline())
            {
                return ObjectSpaceNormalBakePlanResult{
                    .Status =
                        Dependencies.Device->IsOperational()
                            ? ObjectSpaceNormalBakePlanStatus::Invalid
                            : ObjectSpaceNormalBakePlanStatus::DeviceLost,
                };
            }

            std::optional<std::size_t> dilation{};
            if (submission.Identity->PaddingTexels != 0u)
            {
                dilation = AcquireDilation(*submission.Identity);
                if (!dilation.has_value())
                {
                    return ObjectSpaceNormalBakePlanResult{
                        .Status =
                            Dependencies.Device->IsOperational()
                                ? ObjectSpaceNormalBakePlanStatus::NotReady
                                : ObjectSpaceNormalBakePlanStatus::DeviceLost,
                    };
                }
            }

            if (!Dependencies.Device->IsOperational())
            {
                ReleaseDilation(dilation, 0u);
                return ObjectSpaceNormalBakePlanResult{
                    .Status = ObjectSpaceNormalBakePlanStatus::DeviceLost,
                };
            }

            Graphics::GpuGeometryResidencyView revalidated{};
            if (!gpuWorld.TryGetGeometryResidencyView(
                    renderable->Surface.Geometry,
                    revalidated) ||
                !ValidateObjectSpaceNormalBakeResidency(
                     *submission.Identity,
                     revalidated,
                     residency.ContentRevision)
                     .Succeeded())
            {
                ReleaseDilation(dilation, 0u);
                return ObjectSpaceNormalBakePlanResult{
                    .Status = ObjectSpaceNormalBakePlanStatus::Invalid,
                };
            }

            Graphics::ObjectSpaceNormalTextureBakeDilationResources
                dilationResources{};
            if (dilation.has_value())
            {
                DilationEntry& entry = Dilation[*dilation];
                dilationResources = entry.Resources.GetResources();
                dilationResources.ScratchInitialLayout =
                    entry.ScratchLayout;
            }

            RHI::PipelineManager& pipelines =
                Dependencies.Renderer->GetPipelineManager();
            const RHI::PipelineHandle pipeline =
                pipelines.GetDeviceHandle(
                    RasterPipeline->GetHandle());
            const auto key =
                MakeObjectSpaceNormalBakeGraphicsCompletionKey(
                    submission);
            Graphics::ObjectSpaceNormalTextureBakePlan plan =
                Graphics::BuildObjectSpaceNormalTextureBakePlan(
                    Graphics::ObjectSpaceNormalTextureBakePlanRequest{
                        .GeneratedTextureAsset =
                            submission.GeneratedTextureAsset,
                        .Geometry =
                            Graphics::
                                ObjectSpaceNormalTextureBakeGeometryBuffers{
                                    .IndexBuffer =
                                        revalidated.IndexBuffer,
                                    .TexcoordBDA =
                                        revalidated.Record
                                            .TexcoordBufferBDA,
                                    .NormalBDA =
                                        revalidated.Record.NormalBufferBDA,
                                    .VertexCount =
                                        submission.Identity->VertexCount,
                                    .FirstIndex =
                                        revalidated.Record
                                            .SurfaceFirstIndex,
                                    .IndexCount =
                                        revalidated.Record
                                            .SurfaceIndexCount,
                                },
                        .Options =
                            OptionsForIdentity(*submission.Identity),
                        .SourceKey = key.Source,
                        .Pipeline = pipeline,
                        .Dilation = dilationResources,
                        .AdditionalTextureUsage =
                            RHI::TextureUsage::TransferSrc,
                        .InitialLayout =
                            RHI::TextureLayout::Undefined,
                        .FinalLayout =
                            RHI::TextureLayout::ShaderReadOnly,
                        .DebugName =
                            "Runtime.ObjectSpaceNormalBake.Output",
                    });
            if (!plan.Succeeded())
            {
                ReleaseDilation(dilation, 0u);
                return ObjectSpaceNormalBakePlanResult{
                    .Status = ObjectSpaceNormalBakePlanStatus::Invalid,
                };
            }

            return ObjectSpaceNormalBakePlanResult{
                .Status = ObjectSpaceNormalBakePlanStatus::Ready,
                .Plan = std::move(plan),
                .Geometry = renderable->Surface.Geometry,
                .GeometryContentRevision = revalidated.ContentRevision,
                .DilationEntry = dilation,
            };
        }

        void ConfigureDependencies()
        {
            BuildPlan = {};
            if (TestHooks.EnableDeterministicPlan)
            {
                BuildPlan =
                    [this](
                        const RuntimeObjectSpaceNormalBakeSubmission&
                            submission)
                    {
                        Graphics::ObjectSpaceNormalTextureBakePlan plan =
                            BuildDeterministicTestPlan(submission);
                        if (TestHooks.InvalidateFirstRecord &&
                            TestPlanBuildCount == 0u)
                        {
                            plan.RecordTemplate.Pipeline = {};
                        }
                        ++TestPlanBuildCount;
                        return ObjectSpaceNormalBakePlanResult{
                            .Status = plan.Succeeded()
                                ? ObjectSpaceNormalBakePlanStatus::Ready
                                : ObjectSpaceNormalBakePlanStatus::Invalid,
                            .Plan = std::move(plan),
                            .GeometryContentRevision = 1u,
                        };
                    };
            }
            else
            {
                BuildPlan =
                    [this](
                        const RuntimeObjectSpaceNormalBakeSubmission&
                            submission)
                    {
                        return BuildProductionPlan(submission);
                    };
            }

            MarkReady = {};
            if (TestHooks.RejectFirstReadyPublication)
            {
                MarkReady =
                    [this](
                        Graphics::GpuAssetCache& gpuAssets,
                        const RuntimeObjectSpaceNormalBakeGpuSubmissionTicket&
                            ticket,
                        const std::uint64_t readyFrame) -> Core::Result
                    {
                        if (TestReadyPublicationCount++ == 0u)
                            return Core::Err(Core::ErrorCode::InvalidState);
                        return MarkObjectSpaceNormalBakeGpuSubmissionReady(
                            gpuAssets,
                            ticket,
                            readyFrame);
                    };
            }
        }

        [[nodiscard]] RuntimeObjectSpaceNormalBakeSubmission
        MakeSubmission(
            const BakeWork& work,
            const BakeWaiter& waiter) const
        {
            return RuntimeObjectSpaceNormalBakeSubmission{
                .Identity = work.Identity,
                .Target = waiter.StaleKey.Target,
                .GeneratedTextureAsset = work.Asset,
                .AssetSelection = waiter.Selection,
                .StaleKey = waiter.StaleKey,
            };
        }

        [[nodiscard]] std::optional<std::uint64_t>
        ResolveTargetRevision(
            const BakeWork& work,
            const RuntimeObjectSpaceNormalBakeTarget& target) const
        {
            if (!work.Identity.has_value())
                return std::nullopt;
            if (TestHooks.EnableDeterministicPlan)
                return 1u;
            if (Dependencies.Renderer == nullptr ||
                Dependencies.RenderExtraction == nullptr)
            {
                return std::nullopt;
            }

            const auto renderable =
                Dependencies.RenderExtraction
                    ->FindGpuRenderableAvailability(
                        target.StableEntityId);
            if (!renderable.has_value() ||
                !renderable->Surface.HasGeometry)
            {
                return std::nullopt;
            }
            Graphics::GpuGeometryResidencyView residency{};
            if (!Dependencies.Renderer->GetGpuWorld()
                     .TryGetGeometryResidencyView(
                         renderable->Surface.Geometry,
                         residency) ||
                !ValidateObjectSpaceNormalBakeResidency(
                     *work.Identity,
                     residency)
                     .Succeeded())
            {
                return std::nullopt;
            }
            return residency.ContentRevision;
        }

        void TerminallyDiscardWaiter(
            BakeWork& work,
            const std::size_t waiterIndex)
        {
            if (waiterIndex >= work.Waiters.size())
                return;
            (void)Queue.Discard(
                work.Waiters[waiterIndex].StaleKey);
            work.Waiters.erase(
                work.Waiters.begin() +
                static_cast<std::ptrdiff_t>(waiterIndex));
        }

        void RecordFrameCommands(RHI::ICommandContext& commandContext)
        {
            Diagnostics.LastRecordAttempted = 0u;
            Diagnostics.LastRecordSubmitted = 0u;

            if (Dependencies.GpuAssets == nullptr ||
                Dependencies.Device == nullptr)
            {
                return;
            }

            std::size_t submittedThisFrame = 0u;
            for (std::size_t workIndex = 0u;
                 workIndex < Work.size() &&
                 submittedThisFrame < kMaxSubmissionsPerFrame;)
            {
                BakeWork& work = Work[workIndex];
                if (work.Phase != BakeWorkPhase::Queued)
                {
                    ++workIndex;
                    continue;
                }

                while (!work.Waiters.empty() &&
                       !Queue.IsLatest(
                           work.Waiters.front().StaleKey))
                {
                    work.Waiters.erase(work.Waiters.begin());
                    ++Diagnostics.PendingStaleDiscards;
                }
                if (work.Waiters.empty())
                {
                    RetireWork(workIndex);
                    continue;
                }

                ++Diagnostics.LastRecordAttempted;
                RuntimeObjectSpaceNormalBakeSubmission submission =
                    MakeSubmission(work, work.Waiters.front());
                ObjectSpaceNormalBakePlanResult plan =
                    BuildPlan
                        ? BuildPlan(submission)
                        : ObjectSpaceNormalBakePlanResult{};
                if (!plan.Succeeded())
                {
                    if (plan.Status ==
                        ObjectSpaceNormalBakePlanStatus::NotReady)
                    {
                        ++Diagnostics.PlanNotReady;
                        ++workIndex;
                        continue;
                    }

                    ++Diagnostics.PlanRejected;
                    TerminallyDiscardWaiter(work, 0u);
                    if (work.Waiters.empty())
                        RetireWork(workIndex);
                    else
                        ++workIndex;
                    continue;
                }

                if (!Dependencies.Device->IsOperational())
                {
                    ReleaseDilation(plan.DilationEntry, 0u);
                    ++Diagnostics.PlanRejected;
                    TerminallyDiscardWaiter(work, 0u);
                    if (work.Waiters.empty())
                        RetireWork(workIndex);
                    else
                        ++workIndex;
                    continue;
                }

                if (!TestHooks.EnableDeterministicPlan)
                {
                    Dependencies.Renderer->GetGpuWorld()
                        .SubmitPendingUploadBarriers(commandContext);
                    Graphics::GpuGeometryResidencyView residency{};
                    if (!Dependencies.Renderer->GetGpuWorld()
                             .TryGetGeometryResidencyView(
                                 plan.Geometry,
                                 residency) ||
                        !ValidateObjectSpaceNormalBakeResidency(
                             *submission.Identity,
                             residency,
                             plan.GeometryContentRevision)
                             .Succeeded() ||
                        !Dependencies.Device->IsOperational())
                    {
                        ReleaseDilation(plan.DilationEntry, 0u);
                        ++Diagnostics.PlanRejected;
                        TerminallyDiscardWaiter(work, 0u);
                        if (work.Waiters.empty())
                            RetireWork(workIndex);
                        else
                            ++workIndex;
                        continue;
                    }
                }

                RuntimeObjectSpaceNormalBakeGpuSubmitResult submitted =
                    BeginObjectSpaceNormalBakeGpuSubmission(
                        *Dependencies.GpuAssets,
                        submission,
                        plan.Plan,
                        plan.GeometryContentRevision);
                if (!submitted.Succeeded())
                {
                    ReleaseDilation(plan.DilationEntry, 0u);
                    if (submitted.Status ==
                        RuntimeObjectSpaceNormalBakeGpuSubmitStatus::
                            CacheRejected)
                    {
                        ++Diagnostics.CacheRejected;
                        ++workIndex;
                    }
                    else
                    {
                        ++Diagnostics.PlanRejected;
                        TerminallyDiscardWaiter(work, 0u);
                        if (work.Waiters.empty())
                            RetireWork(workIndex);
                        else
                            ++workIndex;
                    }
                    continue;
                }

                if (!Dependencies.Device->IsOperational())
                {
                    (void)Dependencies.GpuAssets
                        ->FailGpuProducedTexture(
                            submitted.Ticket.GeneratedTextureAsset,
                            submitted.Ticket.CacheGeneration);
                    ReleaseDilation(plan.DilationEntry, 0u);
                    ++Diagnostics.PlanRejected;
                    TerminallyDiscardWaiter(work, 0u);
                    if (work.Waiters.empty())
                        RetireWork(workIndex);
                    else
                        ++workIndex;
                    continue;
                }

                Core::Result recorded =
                    Graphics::RecordObjectSpaceNormalTextureBake(
                        commandContext,
                        submitted.Ticket.RecordDesc);
                if (!recorded.has_value())
                {
                    ++Diagnostics.RecordFailures;
                    (void)Dependencies.GpuAssets
                        ->FailGpuProducedTexture(
                            submitted.Ticket.GeneratedTextureAsset,
                            submitted.Ticket.CacheGeneration);
                    ReleaseDilation(plan.DilationEntry, 0u);
                    TerminallyDiscardWaiter(work, 0u);
                    if (work.Waiters.empty())
                        RetireWork(workIndex);
                    else
                        ++workIndex;
                    continue;
                }

                if (plan.DilationEntry.has_value())
                {
                    Dilation[*plan.DilationEntry].ScratchLayout =
                        RHI::TextureLayout::ShaderReadOnly;
                }

                const std::uint64_t readyFrame = ReadyFrame();
                Core::Result markedReady = MarkReady
                    ? MarkReady(
                          *Dependencies.GpuAssets,
                          submitted.Ticket,
                          readyFrame)
                    : MarkObjectSpaceNormalBakeGpuSubmissionReady(
                          *Dependencies.GpuAssets,
                          submitted.Ticket,
                          readyFrame);

                work.Ticket = std::move(submitted.Ticket);
                work.CacheGeneration =
                    work.Ticket.CacheGeneration;
                work.DilationEntry = plan.DilationEntry;
                work.ReleaseFrame = readyFrame;
                for (BakeWaiter& waiter : work.Waiters)
                {
                    const auto revision =
                        ResolveTargetRevision(
                            work,
                            waiter.StaleKey.Target);
                    waiter.GeometryContentRevision =
                        revision.value_or(0u);
                }

                if (!markedReady.has_value())
                {
                    ++Diagnostics.ReadyFrameFailures;
                    (void)Dependencies.GpuAssets
                        ->FailGpuProducedTexture(
                            work.Asset,
                            work.CacheGeneration);
                    for (const BakeWaiter& waiter : work.Waiters)
                        (void)Queue.Discard(waiter.StaleKey);
                    work.Waiters.clear();
                    work.Phase =
                        BakeWorkPhase::RecordedFailureHold;
                }
                else
                {
                    work.Phase = BakeWorkPhase::PendingGpu;
                    ++Diagnostics.RecordedSubmissions;
                    ++Diagnostics.LastRecordSubmitted;
                }
                ++submittedThisFrame;
                ++workIndex;
            }
        }

        [[nodiscard]] RuntimeObjectSpaceNormalBakeBindingContext
        BindingContext() const noexcept
        {
            return RuntimeObjectSpaceNormalBakeBindingContext{
                .Queue =
                    const_cast<RuntimeObjectSpaceNormalBakeQueue*>(
                        &Queue),
                .Extraction = Dependencies.RenderExtraction,
                .GpuAssets = Dependencies.GpuAssets,
                .GpuWorld =
                    Dependencies.Renderer != nullptr
                        ? &Dependencies.Renderer->GetGpuWorld()
                        : nullptr,
                .Scene = Scene,
                .World = BoundWorld,
                .BindingEpoch = BindingEpoch,
            };
        }

        [[nodiscard]] std::uint64_t DrainCompletedTransfers()
        {
            Diagnostics.LastDrainProcessed = 0u;
            Diagnostics.LastDrainBound = 0u;

            for (std::size_t index = 0u; index < Work.size();)
            {
                BakeWork& work = Work[index];
                if (work.Phase ==
                    BakeWorkPhase::RecordedFailureHold)
                {
                    if (Dependencies.Device == nullptr ||
                        !Dependencies.Device->IsOperational() ||
                        CurrentFrame() < work.ReleaseFrame)
                    {
                        ++index;
                        continue;
                    }
                    ReleaseDilation(
                        work.DilationEntry,
                        work.ReleaseFrame);
                    work.DilationEntry.reset();
                    DestroyGeneratedAsset(work.Asset);
                    Work.erase(
                        Work.begin() +
                        static_cast<std::ptrdiff_t>(index));
                    continue;
                }

                if (work.Phase == BakeWorkPhase::PendingGpu)
                {
                    if (!HasExactReadyView(work))
                    {
                        if (Dependencies.GpuAssets != nullptr)
                        {
                            const Graphics::GpuAssetState state =
                                Dependencies.GpuAssets->GetState(
                                    work.Asset);
                            if (state ==
                                    Graphics::GpuAssetState::Failed ||
                                state ==
                                    Graphics::GpuAssetState::
                                        NotRequested)
                            {
                                for (const BakeWaiter& waiter :
                                     work.Waiters)
                                {
                                    (void)Queue.Discard(
                                        waiter.StaleKey);
                                }
                                work.Waiters.clear();
                                RetireWork(index);
                                continue;
                            }
                        }
                        ++Diagnostics.WaitingBindings;
                        ++index;
                        continue;
                    }
                    const std::uint64_t workSerial =
                        work.WorkSerial;
                    const std::size_t outputBytes =
                        work.OutputBytes;
                    const bool hasCapacity =
                        EnsureProvenCapacity(outputBytes);
                    const auto current = std::ranges::find_if(
                        Work,
                        [workSerial](const BakeWork& candidate)
                        {
                            return candidate.WorkSerial == workSerial;
                        });
                    if (current == Work.end())
                    {
                        continue;
                    }
                    index = static_cast<std::size_t>(
                        std::distance(Work.begin(), current));
                    BakeWork& currentWork = Work[index];
                    if (!hasCapacity)
                    {
                        ++Diagnostics.CapacityRejected;
                        for (const BakeWaiter& waiter :
                             currentWork.Waiters)
                            (void)Queue.Discard(waiter.StaleKey);
                        currentWork.Waiters.clear();
                        RetireWork(index);
                        continue;
                    }
                    ReleaseDilation(
                        currentWork.DilationEntry,
                        currentWork.ReleaseFrame);
                    currentWork.DilationEntry.reset();
                    currentWork.Phase =
                        BakeWorkPhase::ProvenReady;
                    currentWork.LastUseSerial = LastUseSerial++;
                    continue;
                }

                if (work.Phase != BakeWorkPhase::ProvenReady ||
                    work.Waiters.empty())
                {
                    ++index;
                    continue;
                }

                std::size_t waiterIndex = 0u;
                while (waiterIndex < work.Waiters.size() &&
                       Diagnostics.LastDrainProcessed <
                           kMaxBindingsPerDrain)
                {
                    BakeWaiter& waiter = work.Waiters[waiterIndex];
                    ++Diagnostics.LastDrainProcessed;

                    if (!Queue.IsLatest(waiter.StaleKey))
                    {
                        work.Waiters.erase(
                            work.Waiters.begin() +
                            static_cast<std::ptrdiff_t>(
                                waiterIndex));
                        ++Diagnostics.StaleCompletions;
                        continue;
                    }
                    if (waiter.GeometryContentRevision == 0u)
                    {
                        const auto revision =
                            ResolveTargetRevision(
                                work,
                                waiter.StaleKey.Target);
                        if (!revision.has_value())
                        {
                            (void)Queue.Discard(
                                waiter.StaleKey);
                            work.Waiters.erase(
                                work.Waiters.begin() +
                                static_cast<std::ptrdiff_t>(
                                    waiterIndex));
                            ++Diagnostics.InvalidCompletions;
                            continue;
                        }
                        waiter.GeometryContentRevision =
                            *revision;
                    }

                    const RuntimeObjectSpaceNormalBakeBindingResult bound =
                        TryBindReadyObjectSpaceNormalBake(
                            BindingContext(),
                            RuntimeObjectSpaceNormalBakeCompletion{
                                .StaleKey = waiter.StaleKey,
                                .Identity =
                                    work.Identity
                                        ? &*work.Identity
                                        : nullptr,
                                .GeneratedTextureAsset =
                                    work.Asset,
                                .CacheGeneration =
                                    work.CacheGeneration,
                                .GeometryContentRevision =
                                    waiter
                                        .GeometryContentRevision,
                                .AssetSelection =
                                    waiter.Selection,
                            });

                    switch (bound.Status)
                    {
                    case RuntimeObjectSpaceNormalBakeBindingStatus::Bound:
                        work.BoundTargets.push_back(
                            waiter.StaleKey.Target);
                        work.Waiters.erase(
                            work.Waiters.begin() +
                            static_cast<std::ptrdiff_t>(
                                waiterIndex));
                        work.LastUseSerial = LastUseSerial++;
                        ++Diagnostics.BoundCompletions;
                        ++Diagnostics.LastDrainBound;
                        break;
                    case RuntimeObjectSpaceNormalBakeBindingStatus::
                        WaitingForGpuTexture:
                        ++Diagnostics.WaitingBindings;
                        ++waiterIndex;
                        break;
                    case RuntimeObjectSpaceNormalBakeBindingStatus::
                        StaleCompletion:
                    case RuntimeObjectSpaceNormalBakeBindingStatus::
                        StaleScene:
                    case RuntimeObjectSpaceNormalBakeBindingStatus::
                        StaleGeometry:
                    case RuntimeObjectSpaceNormalBakeBindingStatus::
                        StaleProgressiveState:
                        (void)Queue.Discard(
                            waiter.StaleKey);
                        work.Waiters.erase(
                            work.Waiters.begin() +
                            static_cast<std::ptrdiff_t>(
                                waiterIndex));
                        ++Diagnostics.StaleCompletions;
                        break;
                    case RuntimeObjectSpaceNormalBakeBindingStatus::
                        InvalidContext:
                    case RuntimeObjectSpaceNormalBakeBindingStatus::
                        InvalidStableEntity:
                    case RuntimeObjectSpaceNormalBakeBindingStatus::
                        InvalidCompletion:
                        (void)Queue.Discard(
                            waiter.StaleKey);
                        work.Waiters.erase(
                            work.Waiters.begin() +
                            static_cast<std::ptrdiff_t>(
                                waiterIndex));
                        ++Diagnostics.InvalidCompletions;
                        break;
                    }
                }
                ++index;
            }
            return Diagnostics.LastDrainProcessed;
        }

        [[nodiscard]] bool HasInFlightWork() const noexcept
        {
            if (Queue.PendingSubmissionCount() != 0u ||
                !Work.empty() ||
                RasterPipeline.has_value() ||
                !Dilation.empty())
            {
                return true;
            }
            return false;
        }

        void DetachTargets(
            const WorldHandle world,
            const std::uint64_t bindingEpoch)
        {
            (void)Queue.DetachTargets(world, bindingEpoch);
            for (std::size_t index = 0u; index < Work.size();)
            {
                BakeWork& work = Work[index];
                std::erase_if(
                    work.Waiters,
                    [world, bindingEpoch](
                        const BakeWaiter& waiter)
                    {
                        return waiter.StaleKey.Target.World ==
                                   world &&
                               waiter.StaleKey.Target.BindingEpoch ==
                                   bindingEpoch;
                    });
                std::erase_if(
                    work.BoundTargets,
                    [world, bindingEpoch](
                        const RuntimeObjectSpaceNormalBakeTarget& target)
                    {
                        return target.World == world &&
                               target.BindingEpoch == bindingEpoch;
                    });

                if (work.Phase == BakeWorkPhase::Queued &&
                    work.Waiters.empty())
                {
                    RetireWork(index);
                    continue;
                }
                ++index;
            }
            if (BoundWorld == world &&
                BindingEpoch == bindingEpoch)
            {
                BoundWorld = {};
                BindingEpoch = 0u;
                Scene = nullptr;
            }
        }

        [[nodiscard]] GpuQueueParticipantDesc
        MakeGpuQueueParticipantDesc()
        {
            return GpuQueueParticipantDesc{
                .DebugName =
                    "Runtime.ObjectSpaceNormalBakeGpuQueue",
                .RecordFrameCommands =
                    [this](RHI::ICommandContext& commandContext)
                    {
                        RecordFrameCommands(commandContext);
                    },
                .DrainCompletedTransfers =
                    [this]
                    {
                        (void)DrainCompletedTransfers();
                    },
                .HasInFlightWork =
                    [this]
                    {
                        return HasInFlightWork();
                    },
                .ShutdownAfterDeviceIdle =
                    [this]
                    {
                        ShutdownAfterDeviceIdle();
                    },
            };
        }

        void ShutdownAfterDeviceIdle()
        {
            Diagnostics.ShutdownDiscards +=
                Queue.PendingSubmissionCount();
            for (const BakeWork& work : Work)
            {
                Diagnostics.ShutdownDiscards +=
                    work.Waiters.size();
            }

            for (std::size_t index = Work.size();
                 index > 0u;
                 --index)
            {
                RetireWork(index - 1u, true);
            }
            Queue.Clear();
            Dilation.clear();
            RasterPipeline.reset();
            if (Dependencies.Assets != nullptr)
                Dependencies.Assets->Tick();
        }
    };

    std::uint64_t ObjectSpaceNormalBakeReadyFrame(
        const std::uint64_t issueFrame,
        const std::uint32_t framesInFlight) noexcept
    {
        return issueFrame + framesInFlight;
    }

    ObjectSpaceNormalBakeService::ObjectSpaceNormalBakeService()
        : m_Impl(std::make_unique<Impl>())
    {
    }

    ObjectSpaceNormalBakeService::~ObjectSpaceNormalBakeService() =
        default;

    void ObjectSpaceNormalBakeService::SetDependencies(
        ObjectSpaceNormalBakeServiceDependencies deps)
    {
        m_Impl->Dependencies = deps;
        m_Impl->ConfigureDependencies();
    }

    void ObjectSpaceNormalBakeService::ClearDependencies()
    {
        m_Impl->Dependencies = {};
        m_Impl->ConfigureDependencies();
        m_Impl->ParticipantHandle = {};
        m_Impl->BoundWorld = {};
        m_Impl->BindingEpoch = 0u;
        m_Impl->Scene = nullptr;
    }

    void ObjectSpaceNormalBakeService::SetTargetScene(
        const WorldHandle world,
        const std::uint64_t bindingEpoch,
        ECS::Scene::Registry* const scene) noexcept
    {
        m_Impl->BoundWorld = world;
        m_Impl->BindingEpoch = bindingEpoch;
        m_Impl->Scene = scene;
    }

    void ObjectSpaceNormalBakeService::DetachTargets(
        const WorldHandle world,
        const std::uint64_t bindingEpoch)
    {
        m_Impl->DetachTargets(world, bindingEpoch);
    }

    void ObjectSpaceNormalBakeService::PrepareScheduledRequests()
    {
        m_Impl->PrepareScheduledRequests();
    }

    GpuQueueParticipantHandle
    ObjectSpaceNormalBakeService::RegisterGpuQueueParticipant(
        JobService& jobs)
    {
        if (m_Impl->ParticipantHandle.IsValid())
            return m_Impl->ParticipantHandle;

        m_Impl->ParticipantHandle =
            jobs.RegisterGpuQueueParticipant(
                m_Impl->MakeGpuQueueParticipantDesc());
        return m_Impl->ParticipantHandle;
    }

    RuntimeObjectSpaceNormalBakeQueue&
    ObjectSpaceNormalBakeService::Queue() noexcept
    {
        return m_Impl->Queue;
    }

    const RuntimeObjectSpaceNormalBakeQueue&
    ObjectSpaceNormalBakeService::Queue() const noexcept
    {
        return m_Impl->Queue;
    }

    const RuntimeObjectSpaceNormalBakeQueueDiagnostics&
    ObjectSpaceNormalBakeService::QueueDiagnostics() const noexcept
    {
        return m_Impl->Queue.Diagnostics();
    }

    std::size_t ObjectSpaceNormalBakeService::PendingCount() const noexcept
    {
        return m_Impl->Queue.PendingCount();
    }

    void SetObjectSpaceNormalBakeServiceTestHooks(
        ObjectSpaceNormalBakeService& service,
        ObjectSpaceNormalBakeServiceTestHooks hooks)
    {
        service.m_Impl->TestHooks = std::move(hooks);
        service.m_Impl->TestPlanBuildCount = 0u;
        service.m_Impl->TestReadyPublicationCount = 0u;
        service.m_Impl->ConfigureDependencies();
    }

    ObjectSpaceNormalBakeServiceTestDiagnostics
    GetObjectSpaceNormalBakeServiceTestDiagnostics(
        const ObjectSpaceNormalBakeService& service) noexcept
    {
        const ObjectSpaceNormalBakeGpuDiagnostics& diagnostics =
            service.m_Impl->Diagnostics;
        return ObjectSpaceNormalBakeServiceTestDiagnostics{
            .PendingStaleDiscards =
                diagnostics.PendingStaleDiscards,
            .CacheRejected = diagnostics.CacheRejected,
            .RecordedSubmissions =
                diagnostics.RecordedSubmissions,
            .RecordFailures = diagnostics.RecordFailures,
            .ReadyFrameFailures =
                diagnostics.ReadyFrameFailures,
            .WaitingBindings = diagnostics.WaitingBindings,
            .BoundCompletions =
                diagnostics.BoundCompletions,
            .LastRecordAttempted =
                diagnostics.LastRecordAttempted,
            .LastRecordSubmitted =
                diagnostics.LastRecordSubmitted,
            .LastDrainProcessed =
                diagnostics.LastDrainProcessed,
            .LastDrainBound = diagnostics.LastDrainBound,
            .AllocatedAssets = diagnostics.AllocatedAssets,
            .PendingIdentityReuses =
                diagnostics.PendingIdentityReuses,
            .ProvenReadyReuses =
                diagnostics.ProvenReadyReuses,
            .CapacityRejected =
                diagnostics.CapacityRejected,
        };
    }
}
