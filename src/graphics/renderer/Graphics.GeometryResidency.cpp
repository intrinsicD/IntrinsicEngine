module;

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

module Extrinsic.Graphics.GeometryResidency;

namespace Extrinsic::Graphics
{
    namespace
    {
        [[nodiscard]] std::uint64_t SaturatingDeadline(
            const std::uint64_t frame,
            const std::uint32_t framesInFlight) noexcept
        {
            const std::uint64_t delta = framesInFlight;
            return frame > std::numeric_limits<std::uint64_t>::max() - delta
                ? std::numeric_limits<std::uint64_t>::max()
                : frame + delta;
        }

        [[nodiscard]] bool ByteCountMatches(
            const std::size_t byteCount,
            const std::uint32_t vertexCount,
            const std::size_t elementBytes) noexcept
        {
            return byteCount == static_cast<std::size_t>(vertexCount) * elementBytes;
        }
    }

    std::size_t GeometryResidencyKeyHash::operator()(
        const GeometryResidencyKey& key) const noexcept
    {
        std::uint64_t hash = key.Namespace;
        hash ^= key.Identity + 0x9e3779b97f4a7c15ull + (hash << 6u) + (hash >> 2u);
        hash ^= static_cast<std::uint64_t>(key.Lane) +
            0x9e3779b97f4a7c15ull + (hash << 6u) + (hash >> 2u);
        return static_cast<std::size_t>(hash);
    }

    GpuWorld::GeometryUploadDesc GeometryUploadPlan::UploadDesc() const noexcept
    {
        return GpuWorld::GeometryUploadDesc{
            .PackedVertexBytes = PackedVertexBytes,
            .PositionBytes = PositionBytes,
            .TexcoordBytes = TexcoordBytes,
            .NormalBytes = NormalBytes,
            .SurfaceIndices = SurfaceIndices,
            .LineIndices = LineIndices,
            .VertexCount = VertexCount,
            .LocalBounds = LocalBounds,
            .DebugName = DebugName.empty() ? nullptr : DebugName.c_str(),
            .PackedVertexColors = PackedVertexColors,
        };
    }

    GeometryUploadPlan MakeGeometryUploadPlan(
        const GeometryResidencyKey key,
        const std::uint64_t generation,
        const GpuWorld::GeometryUploadDesc& upload,
        const GeometryUploadUpdateClass updateClass,
        const GpuWorld::GeometryChannelUpdateMask updateChannels,
        const GpuWorld::GeometryStorageHint storageHint)
    {
        GeometryUploadPlan plan{};
        plan.Key = key;
        plan.Generation = generation;
        plan.PackedVertexBytes.assign(
            upload.PackedVertexBytes.begin(), upload.PackedVertexBytes.end());
        plan.PositionBytes.assign(
            upload.PositionBytes.begin(), upload.PositionBytes.end());
        plan.TexcoordBytes.assign(
            upload.TexcoordBytes.begin(), upload.TexcoordBytes.end());
        plan.NormalBytes.assign(
            upload.NormalBytes.begin(), upload.NormalBytes.end());
        plan.PackedVertexColors.assign(
            upload.PackedVertexColors.begin(), upload.PackedVertexColors.end());
        plan.SurfaceIndices.assign(
            upload.SurfaceIndices.begin(), upload.SurfaceIndices.end());
        plan.LineIndices.assign(
            upload.LineIndices.begin(), upload.LineIndices.end());
        plan.VertexCount = upload.VertexCount;
        plan.LocalBounds = upload.LocalBounds;
        if (upload.DebugName != nullptr)
        {
            plan.DebugName = upload.DebugName;
        }
        plan.UpdateClass = updateClass;
        plan.UpdateChannels = updateChannels;
        plan.StorageHint = storageHint;
        return plan;
    }

    GeometryUploadPlanValidation ValidateGeometryUploadPlan(
        const GeometryUploadPlan& plan) noexcept
    {
        const auto invalid = [](const GeometryUploadPlanStatus status,
                                const std::string_view diagnostic)
        {
            return GeometryUploadPlanValidation{status, diagnostic};
        };

        if (!plan.Key.IsValid())
        {
            return invalid(GeometryUploadPlanStatus::InvalidKey,
                           "residency key requires nonzero namespace and identity");
        }
        if (plan.Generation == 0u)
        {
            return invalid(GeometryUploadPlanStatus::MissingGeneration,
                           "generation must be nonzero");
        }
        if (plan.VertexCount == 0u)
        {
            return invalid(GeometryUploadPlanStatus::EmptyGeometry,
                           "vertex count must be nonzero");
        }
        if (!plan.PackedVertexBytes.empty() &&
            (plan.PackedVertexBytes.size() % plan.VertexCount) != 0u)
        {
            return invalid(GeometryUploadPlanStatus::InvalidPackedVertexBytes,
                           "packed vertex bytes are not divisible by vertex count");
        }
        if (plan.PositionBytes.empty())
        {
            const std::size_t packedStride = plan.PackedVertexBytes.empty()
                ? 0u
                : plan.PackedVertexBytes.size() / plan.VertexCount;
            if (packedStride < sizeof(float) * 3u)
            {
                return invalid(GeometryUploadPlanStatus::MissingPositions,
                               "position stream is missing");
            }
        }
        else if (!ByteCountMatches(plan.PositionBytes.size(), plan.VertexCount,
                                   sizeof(float) * 3u))
        {
            return invalid(GeometryUploadPlanStatus::InvalidPositionBytes,
                           "position byte count does not match RGB32 vertex count");
        }
        if (!plan.TexcoordBytes.empty() &&
            !ByteCountMatches(plan.TexcoordBytes.size(), plan.VertexCount,
                              sizeof(float) * 2u))
        {
            return invalid(GeometryUploadPlanStatus::InvalidTexcoordBytes,
                           "texcoord byte count does not match RG32 vertex count");
        }
        if (!plan.NormalBytes.empty() &&
            !ByteCountMatches(plan.NormalBytes.size(), plan.VertexCount,
                              sizeof(float) * 3u))
        {
            return invalid(GeometryUploadPlanStatus::InvalidNormalBytes,
                           "normal byte count does not match RGB32 vertex count");
        }
        if (!plan.PackedVertexColors.empty() &&
            plan.PackedVertexColors.size() != plan.VertexCount)
        {
            return invalid(GeometryUploadPlanStatus::InvalidColorCount,
                           "packed color count does not match vertex count");
        }
        const auto invalidIndex = [&plan](const std::vector<std::uint32_t>& indices)
        {
            return std::ranges::any_of(indices, [&plan](const std::uint32_t index)
            {
                return index >= plan.VertexCount;
            });
        };
        if (invalidIndex(plan.SurfaceIndices) || invalidIndex(plan.LineIndices))
        {
            return invalid(GeometryUploadPlanStatus::InvalidIndex,
                           "index is outside the submitted vertex range");
        }
        if (plan.Formats.Position != RHI::Format::RGB32_FLOAT ||
            (!plan.TexcoordBytes.empty() &&
             plan.Formats.Texcoord != RHI::Format::RG32_FLOAT) ||
            (!plan.NormalBytes.empty() &&
             plan.Formats.Normal != RHI::Format::RGB32_FLOAT) ||
            (!plan.PackedVertexColors.empty() &&
             plan.Formats.Color != RHI::Format::R32_UINT) ||
            (!plan.SurfaceIndices.empty() &&
             plan.Formats.SurfaceIndex != RHI::Format::R32_UINT) ||
            (!plan.LineIndices.empty() &&
             plan.Formats.LineIndex != RHI::Format::R32_UINT))
        {
            return invalid(GeometryUploadPlanStatus::UnsupportedFormat,
                           "GpuWorld supports RGB32/RG32/R32 geometry streams only");
        }
        if (plan.UpdateClass == GeometryUploadUpdateClass::PartialPreferred &&
            !plan.UpdateChannels.Any())
        {
            return invalid(GeometryUploadPlanStatus::InvalidPartialUpdate,
                           "partial update requires at least one channel");
        }
        return {};
    }

    bool GeometryResidencyResult::Succeeded() const noexcept
    {
        return Handle.IsValid() &&
            Status != GeometryResidencyStatus::InvalidPlan &&
            Status != GeometryResidencyStatus::StalePlan &&
            Status != GeometryResidencyStatus::UploadFailed;
    }

    struct GeometryResidencyCoordinator::Impl
    {
        struct Entry
        {
            GpuGeometryHandle Handle{};
            std::uint64_t Generation = 0u;
            std::uint32_t RefCount = 0u;
            GpuWorld::GeometryStoragePlan StoragePlan{};
            bool PendingRetire = false;
        };

        struct RetireRecord
        {
            GeometryResidencyKey Key{};
            GpuGeometryHandle Handle{};
            bool OwnsEntry = false;
            std::uint64_t Deadline = 0u;
            bool DeadlineSet = false;
        };

        explicit Impl(GpuWorld& world) : World(&world) {}

        [[nodiscard]] bool CancelRetire(
            const GeometryResidencyKey key,
            const GpuGeometryHandle handle)
        {
            const auto it = std::find_if(
                Retire.begin(), Retire.end(),
                [key, handle](const RetireRecord& record)
                {
                    return record.OwnsEntry && record.Key == key &&
                           record.Handle == handle;
                });
            if (it == Retire.end())
            {
                return false;
            }
            Retire.erase(it);
            return true;
        }

        void QueueReplacement(const GpuGeometryHandle handle)
        {
            if (handle.IsValid())
            {
                Retire.push_back(RetireRecord{
                    .Handle = handle,
                    .OwnsEntry = false,
                });
            }
        }

        [[nodiscard]] GeometryResidencyResult Submit(
            const GeometryUploadPlan& plan,
            const bool acquire)
        {
            GeometryResidencyResult result{};
            result.Validation = ValidateGeometryUploadPlan(plan);
            result.Diagnostic = result.Validation.Diagnostic;
            if (!result.Validation.Valid())
            {
                ++Counters.InvalidPlans;
                return result;
            }

            const GpuWorld::GeometryUploadDesc upload = plan.UploadDesc();
            result.StoragePlan =
                GpuWorld::PlanGeometryStorage(upload, plan.StorageHint);

            auto found = Entries.find(plan.Key);
            if (found == Entries.end())
            {
                const GpuGeometryHandle handle = World->UploadGeometry(upload);
                if (!handle.IsValid())
                {
                    result.Status = GeometryResidencyStatus::UploadFailed;
                    result.Diagnostic = "GpuWorld rejected geometry upload";
                    ++Counters.FailedUploads;
                    return result;
                }
                Entries.emplace(plan.Key, Entry{
                    .Handle = handle,
                    .Generation = plan.Generation,
                    .RefCount = 1u,
                    .StoragePlan = result.StoragePlan,
                });
                result.Status = GeometryResidencyStatus::Uploaded;
                result.Handle = handle;
                result.Diagnostic = "uploaded";
                ++Counters.Uploads;
                return result;
            }

            Entry& entry = found->second;
            result.Handle = entry.Handle;
            result.StoragePlan = entry.StoragePlan;
            if (plan.Generation < entry.Generation)
            {
                result.Status = GeometryResidencyStatus::StalePlan;
                result.Diagnostic = "plan generation is older than resident generation";
                ++Counters.StalePlans;
                return result;
            }

            if (plan.Generation == entry.Generation)
            {
                if (entry.RefCount == 0u)
                {
                    if (CancelRetire(plan.Key, entry.Handle))
                    {
                        ++Counters.RetireCancellations;
                    }
                    entry.PendingRetire = false;
                    entry.RefCount = 1u;
                }
                else if (acquire)
                {
                    if (entry.RefCount == std::numeric_limits<std::uint32_t>::max())
                    {
                        result.Status = GeometryResidencyStatus::RefCountSaturated;
                        result.Diagnostic = "residency reference count saturated";
                        ++Counters.RefCountSaturated;
                        return result;
                    }
                    ++entry.RefCount;
                }
                result.Status = GeometryResidencyStatus::Reused;
                result.Diagnostic = "reused resident generation";
                ++Counters.ReuseHits;
                return result;
            }

            if (plan.UpdateClass == GeometryUploadUpdateClass::PartialPreferred)
            {
                const auto update = World->UpdateGeometryChannels(
                    entry.Handle, upload, plan.UpdateChannels);
                result.ChannelUpdateStatus = update.Status;
                if (update.Succeeded())
                {
                    entry.Generation = plan.Generation;
                    entry.StoragePlan = result.StoragePlan;
                    result.Status = GeometryResidencyStatus::PartiallyUpdated;
                    result.Handle = entry.Handle;
                    result.Diagnostic = "updated resident channels";
                    ++Counters.PartialUpdates;
                    return result;
                }
            }

            const GpuGeometryHandle replacement = World->UploadGeometry(upload);
            if (!replacement.IsValid())
            {
                result.Status = GeometryResidencyStatus::UploadFailed;
                result.Handle = {};
                result.Diagnostic = "GpuWorld rejected replacement upload";
                ++Counters.FailedUploads;
                return result;
            }

            const GpuGeometryHandle retiring = entry.Handle;
            QueueReplacement(retiring);
            entry.Handle = replacement;
            entry.Generation = plan.Generation;
            entry.StoragePlan = result.StoragePlan;
            entry.PendingRetire = false;
            if (entry.RefCount == 0u)
            {
                entry.RefCount = 1u;
            }
            else if (acquire &&
                     entry.RefCount != std::numeric_limits<std::uint32_t>::max())
            {
                ++entry.RefCount;
            }
            result.Status = GeometryResidencyStatus::FullyReuploaded;
            result.Handle = replacement;
            result.RetiringHandle = retiring;
            result.Diagnostic = "replaced resident allocation";
            ++Counters.FullReuploads;
            return result;
        }

        GpuWorld* World = nullptr;
        std::unordered_map<GeometryResidencyKey, Entry, GeometryResidencyKeyHash>
            Entries{};
        std::vector<RetireRecord> Retire{};
        GeometryResidencyStats Counters{};
    };

    GeometryResidencyCoordinator::GeometryResidencyCoordinator(GpuWorld& world)
        : m_Impl(std::make_unique<Impl>(world))
    {
    }

    GeometryResidencyCoordinator::~GeometryResidencyCoordinator() = default;

    GeometryResidencyResult GeometryResidencyCoordinator::Reconcile(
        const GeometryUploadPlan& plan)
    {
        return m_Impl->Submit(plan, false);
    }

    GeometryResidencyResult GeometryResidencyCoordinator::Acquire(
        const GeometryUploadPlan& plan)
    {
        return m_Impl->Submit(plan, true);
    }

    bool GeometryResidencyCoordinator::Release(const GeometryResidencyKey key)
    {
        const auto found = m_Impl->Entries.find(key);
        if (found == m_Impl->Entries.end())
        {
            return false;
        }
        auto& entry = found->second;
        if (entry.RefCount == 0u)
        {
            return true;
        }
        --entry.RefCount;
        ++m_Impl->Counters.Releases;
        if (entry.RefCount == 0u)
        {
            entry.PendingRetire = true;
            m_Impl->Retire.push_back(Impl::RetireRecord{
                .Key = key,
                .Handle = entry.Handle,
                .OwnsEntry = true,
            });
        }
        return entry.RefCount == 0u;
    }

    void GeometryResidencyCoordinator::Tick(
        const std::uint64_t currentFrame,
        const std::uint32_t framesInFlight)
    {
        const std::uint64_t deadline =
            SaturatingDeadline(currentFrame, framesInFlight);
        for (auto& record : m_Impl->Retire)
        {
            if (!record.DeadlineSet)
            {
                record.Deadline = deadline;
                record.DeadlineSet = true;
            }
        }

        auto it = m_Impl->Retire.begin();
        while (it != m_Impl->Retire.end())
        {
            if (!it->DeadlineSet || it->Deadline > currentFrame)
            {
                ++it;
                continue;
            }

            bool freeHandle = !it->OwnsEntry;
            if (it->OwnsEntry)
            {
                const auto entry = m_Impl->Entries.find(it->Key);
                if (entry != m_Impl->Entries.end() &&
                    entry->second.RefCount == 0u &&
                    entry->second.Handle == it->Handle)
                {
                    m_Impl->Entries.erase(entry);
                    freeHandle = true;
                }
            }
            if (freeHandle && it->Handle.IsValid())
            {
                m_Impl->World->FreeGeometry(it->Handle);
                ++m_Impl->Counters.FreeRetires;
            }
            it = m_Impl->Retire.erase(it);
        }
    }

    void GeometryResidencyCoordinator::Shutdown()
    {
        for (const auto& record : m_Impl->Retire)
        {
            if (!record.OwnsEntry && record.Handle.IsValid())
            {
                m_Impl->World->FreeGeometry(record.Handle);
            }
        }
        for (const auto& [_, entry] : m_Impl->Entries)
        {
            if (entry.Handle.IsValid())
            {
                m_Impl->World->FreeGeometry(entry.Handle);
            }
        }
        m_Impl->Retire.clear();
        m_Impl->Entries.clear();
    }

    std::optional<GeometryResidencyView> GeometryResidencyCoordinator::Find(
        const GeometryResidencyKey key) const noexcept
    {
        const auto found = m_Impl->Entries.find(key);
        if (found == m_Impl->Entries.end())
        {
            return std::nullopt;
        }
        return GeometryResidencyView{
            .Key = key,
            .Handle = found->second.Handle,
            .Generation = found->second.Generation,
            .RefCount = found->second.RefCount,
            .StoragePlan = found->second.StoragePlan,
            .PendingRetire = found->second.PendingRetire,
        };
    }

    std::size_t GeometryResidencyCoordinator::Size() const noexcept
    {
        return m_Impl->Entries.size();
    }

    std::size_t GeometryResidencyCoordinator::PendingRetireCount() const noexcept
    {
        return m_Impl->Retire.size();
    }

    const GeometryResidencyStats& GeometryResidencyCoordinator::Stats() const noexcept
    {
        return m_Impl->Counters;
    }

    bool GeometryResidencyCoordinator::PrimeRefCountForTest(
        const GeometryResidencyKey key,
        const std::uint32_t refCount) noexcept
    {
        const auto found = m_Impl->Entries.find(key);
        if (found == m_Impl->Entries.end())
        {
            return false;
        }
        found->second.RefCount = refCount;
        return true;
    }
}
