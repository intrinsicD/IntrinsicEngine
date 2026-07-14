//
// Created by alex on 22.04.26.
//

module;

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <unordered_map>
#include <glm/glm.hpp>

module Extrinsic.Graphics.TransformSyncSystem;

import Extrinsic.RHI.Types;

namespace Extrinsic::Graphics
{
    struct TransformSyncSystem::Impl
    {
        // Generation stamp: entries touched by the current SyncGpuBuffer call
        // carry the call's generation; the stale sweep erases everything
        // older in one pass instead of re-scanning `records` per entry.
        struct PreviousModelEntry
        {
            glm::mat4 Model{1.0f};
            std::uint64_t Generation{0u};
        };

        bool Initialized{false};
        std::uint64_t Generation{0u};
        std::unordered_map<std::uint32_t, PreviousModelEntry> PreviousModelByInstance{};
    };

    TransformSyncSystem::TransformSyncSystem()
        : m_Impl(std::make_unique<Impl>())
    {}

    TransformSyncSystem::~TransformSyncSystem() = default;

    void TransformSyncSystem::Initialize()
    {
        m_Impl->Initialized = true;
    }

    void TransformSyncSystem::Shutdown()
    {
        m_Impl->PreviousModelByInstance.clear();
        m_Impl->Initialized = false;
    }

    void TransformSyncSystem::SyncGpuBuffer(std::span<const TransformSyncRecord> records,
                                            GpuWorld&                            gpuWorld)
    {
        if (!m_Impl->Initialized)
        {
            return;
        }

        const std::uint64_t generation = ++m_Impl->Generation;

        for (const TransformSyncRecord& record : records)
        {
            if (!record.Instance.IsValid())
            {
                continue;
            }

            const auto instance = record.Instance;
            const glm::mat4 model = record.Model;
            glm::mat4 prevModel = model;
            const auto key = instance.Index;

            if (auto it = m_Impl->PreviousModelByInstance.find(key);
                it != m_Impl->PreviousModelByInstance.end())
            {
                prevModel = it->second.Model;
                it->second.Model = model;
                it->second.Generation = generation;
            }
            else
            {
                m_Impl->PreviousModelByInstance.emplace(
                    key, Impl::PreviousModelEntry{model, generation});
            }

            gpuWorld.SetInstanceTransform(instance, model, prevModel);
            gpuWorld.SetInstanceRenderFlags(instance, record.RenderFlags);
            if (record.HasMaterialSlot)
            {
                gpuWorld.SetInstanceMaterialSlot(instance, record.MaterialSlot);
            }
            gpuWorld.SetBounds(instance, record.Bounds);
        }

        // Single-pass stale sweep: any entry not stamped by this call's
        // records is no longer live and drops its previous-model state.
        for (auto it = m_Impl->PreviousModelByInstance.begin();
             it != m_Impl->PreviousModelByInstance.end();)
        {
            if (it->second.Generation != generation)
            {
                it = m_Impl->PreviousModelByInstance.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    bool TransformSyncSystem::IsInitialized() const noexcept
    {
        return m_Impl->Initialized;
    }

    std::size_t TransformSyncSystem::GetTrackedInstanceCountForTest() const noexcept
    {
        return m_Impl->PreviousModelByInstance.size();
    }
}
