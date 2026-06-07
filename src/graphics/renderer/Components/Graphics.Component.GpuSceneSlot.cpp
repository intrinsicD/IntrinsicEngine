module;

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

module Extrinsic.Graphics.Component.GpuSceneSlot;

namespace Extrinsic::Graphics::Components
{
    RHI::BufferHandle GpuSceneSlot::Find(std::string_view name) const noexcept
    {
        if (auto it = NamedBuffers.find(std::string{name}); it != NamedBuffers.end())
            return it->second;
        return {};
    }

    const BufferEntry* GpuSceneSlot::FindEntry(std::string_view name) const noexcept
    {
        if (auto it = NamedBufferEntries.find(std::string{name}); it != NamedBufferEntries.end())
            return &it->second;
        return nullptr;
    }

    void GpuSceneSlot::Upsert(std::string name,
                              RHI::BufferHandle handle,
                              std::uint32_t elementCount,
                              std::uint32_t stride)
    {
        const std::string key = std::move(name);
        NamedBuffers.insert_or_assign(key, handle);
        NamedBufferEntries.insert_or_assign(key,
                                            BufferEntry{
                                                .Name = key,
                                                .Handle = handle,
                                                .ElementCount = elementCount,
                                                .Stride = stride,
                                            });
    }

    void GpuSceneSlot::Remove(std::string_view name)
    {
        const std::string key{name};
        NamedBuffers.erase(key);
        NamedBufferEntries.erase(key);
    }

    void GpuSceneSlot::SetSourceAsset(Assets::AssetId asset, std::uint64_t generation) noexcept
    {
        SourceAsset = asset;
        LastSeenAssetGeneration = generation;
    }

    void GpuSceneSlot::UpdateLastSeenAssetGeneration(std::uint64_t generation) noexcept
    {
        LastSeenAssetGeneration = generation;
    }

    GpuSceneSlotAssetRebindDecision GpuSceneSlot::EvaluateSourceAssetRebind(
        Assets::AssetId observedAsset,
        std::uint64_t observedGeneration) const noexcept
    {
        if (!HasSourceAsset())
            return GpuSceneSlotAssetRebindDecision::NoSourceAsset;

        if (observedAsset != SourceAsset)
            return GpuSceneSlotAssetRebindDecision::AssetMismatch;

        if (observedGeneration == 0)
            return GpuSceneSlotAssetRebindDecision::GenerationUnavailable;

        if (observedGeneration <= LastSeenAssetGeneration)
            return GpuSceneSlotAssetRebindDecision::UpToDate;

        return GpuSceneSlotAssetRebindDecision::RebindRequired;
    }

    bool GpuSceneSlot::NeedsSourceAssetRebind(Assets::AssetId observedAsset,
                                              std::uint64_t observedGeneration) const noexcept
    {
        return EvaluateSourceAssetRebind(observedAsset, observedGeneration)
            == GpuSceneSlotAssetRebindDecision::RebindRequired;
    }

    void GpuSceneSlot::ClearSourceAsset() noexcept
    {
        SourceAsset = {};
        LastSeenAssetGeneration = 0;
    }
}
