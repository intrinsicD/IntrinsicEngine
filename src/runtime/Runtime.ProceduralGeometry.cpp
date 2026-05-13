module;

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

module Extrinsic.Runtime.ProceduralGeometry;

namespace Extrinsic::Runtime
{
    namespace
    {
        constexpr std::uint64_t kFnvOffset64 = 14695981039346656037ull;
        constexpr std::uint64_t kFnvPrime64 = 1099511628211ull;
    }

    std::uint64_t HashProceduralGeometryParams(const ProceduralGeometryParams& params) noexcept
    {
        std::uint64_t h = kFnvOffset64;
        unsigned char bytes[sizeof(ProceduralGeometryParams)];
        std::memcpy(bytes, &params, sizeof(ProceduralGeometryParams));
        for (std::size_t i = 0; i < sizeof(ProceduralGeometryParams); ++i)
        {
            h ^= static_cast<std::uint64_t>(bytes[i]);
            h *= kFnvPrime64;
        }
        return h;
    }

    ProceduralGeometryKey MakeProceduralGeometryKey(ProceduralGeometryKind kind,
                                                    const ProceduralGeometryParams& params) noexcept
    {
        return ProceduralGeometryKey{kind, HashProceduralGeometryParams(params)};
    }

    std::size_t ProceduralGeometryKeyHash::operator()(const ProceduralGeometryKey& key) const noexcept
    {
        std::uint64_t h = static_cast<std::uint64_t>(key.Kind);
        h ^= key.ParamsHash + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
        return static_cast<std::size_t>(h);
    }

    Extrinsic::Graphics::GpuGeometryHandle ProceduralGeometryCache::EnsureResident(
        const ProceduralGeometryKey& key,
        const Extrinsic::Graphics::GpuWorld::GeometryUploadDesc& uploadDesc,
        const UploadFn& upload)
    {
        const auto it = m_Entries.find(key);
        if (it != m_Entries.end())
        {
            if (it->second.RefCount == std::numeric_limits<std::uint32_t>::max())
            {
                ++m_Stats.RefCountSaturated;
                return it->second.Handle;
            }
            if (it->second.RefCount == 0)
            {
                // Entry is queued for retirement but still resident.  Pull it
                // out of the retire queue and resurrect it; the underlying
                // GpuGeometryHandle stays bit-identical so already-extracted
                // sidecars referencing this handle remain valid.
                if (CancelPendingRetire(key))
                {
                    ++m_Stats.RetireCancellations;
                }
            }
            ++it->second.RefCount;
            ++m_Stats.ReuseHits;
            return it->second.Handle;
        }

        if (!upload)
        {
            ++m_Stats.FailedUploads;
            return {};
        }

        Extrinsic::Graphics::GpuGeometryHandle handle = upload(uploadDesc);
        if (!handle.IsValid())
        {
            ++m_Stats.FailedUploads;
            return {};
        }

        ProceduralGeometryCacheEntry entry{};
        entry.Key = key;
        entry.Handle = handle;
        entry.RefCount = 1;
        m_Entries.emplace(key, entry);
        ++m_Stats.Uploads;
        return handle;
    }

    bool ProceduralGeometryCache::Release(const ProceduralGeometryKey& key)
    {
        const auto it = m_Entries.find(key);
        if (it == m_Entries.end())
        {
            return false;
        }
        if (it->second.RefCount == 0)
        {
            return true;
        }
        --it->second.RefCount;
        ++m_Stats.Releases;
        if (it->second.RefCount == 0)
        {
            EnqueueRetire(key);
        }
        return it->second.RefCount == 0;
    }

    void ProceduralGeometryCache::Tick(std::uint64_t currentFrame,
                                       std::uint32_t framesInFlight,
                                       const FreeFn& freeFn)
    {
        const std::uint64_t deadline = currentFrame + std::uint64_t{framesInFlight};
        for (auto& rec : m_Retire)
        {
            if (!rec.DeadlineSet)
            {
                rec.Deadline = deadline;
                rec.DeadlineSet = true;
            }
        }

        auto it = m_Retire.begin();
        while (it != m_Retire.end())
        {
            if (it->DeadlineSet && it->Deadline <= currentFrame)
            {
                const auto entryIt = m_Entries.find(it->Key);
                if (entryIt != m_Entries.end() && entryIt->second.RefCount == 0)
                {
                    if (freeFn)
                    {
                        freeFn(entryIt->second.Handle);
                    }
                    m_Entries.erase(entryIt);
                    ++m_Stats.FreeRetires;
                }
                it = m_Retire.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    const ProceduralGeometryCacheEntry* ProceduralGeometryCache::Find(const ProceduralGeometryKey& key) const noexcept
    {
        const auto it = m_Entries.find(key);
        return it == m_Entries.end() ? nullptr : &it->second;
    }

    bool ProceduralGeometryCache::PrimeRefCountForTest(const ProceduralGeometryKey& key,
                                                       std::uint32_t refCount) noexcept
    {
        const auto it = m_Entries.find(key);
        if (it == m_Entries.end())
        {
            return false;
        }
        it->second.RefCount = refCount;
        return true;
    }

    void ProceduralGeometryCache::EnqueueRetire(const ProceduralGeometryKey& key)
    {
        m_Retire.push_back(RetireRecord{key, 0, false});
    }

    bool ProceduralGeometryCache::CancelPendingRetire(const ProceduralGeometryKey& key)
    {
        const auto it = std::find_if(m_Retire.begin(), m_Retire.end(),
                                     [&key](const RetireRecord& rec) { return rec.Key == key; });
        if (it == m_Retire.end())
        {
            return false;
        }
        m_Retire.erase(it);
        return true;
    }
}
