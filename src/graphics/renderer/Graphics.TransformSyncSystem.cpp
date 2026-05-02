//
// Created by alex on 22.04.26.
//

module;

#include <cstdint>
#include <memory>
#include <span>
#include <unordered_map>
#include <vector>
#include <glm/glm.hpp>

module Extrinsic.Graphics.TransformSyncSystem;

import Extrinsic.RHI.Types;

namespace Extrinsic::Graphics
{
    struct TransformSyncSystem::Impl
    {
        bool Initialized{false};
        std::unordered_map<std::uint32_t, glm::mat4> PreviousModelByInstance{};
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
                prevModel = it->second;
                it->second = model;
            }
            else
            {
                m_Impl->PreviousModelByInstance.emplace(key, model);
            }

            gpuWorld.SetInstanceTransform(instance, model, prevModel);
            gpuWorld.SetInstanceRenderFlags(instance, record.RenderFlags);
            if (record.HasMaterialSlot)
            {
                gpuWorld.SetInstanceMaterialSlot(instance, record.MaterialSlot);
            }
            gpuWorld.SetBounds(instance, record.Bounds);
        }

        std::vector<std::uint32_t> stale;
        stale.reserve(m_Impl->PreviousModelByInstance.size());
        for (const auto& [instanceIndex, _] : m_Impl->PreviousModelByInstance)
        {
            bool stillLive = false;
            for (const TransformSyncRecord& record : records)
            {
                if (record.Instance.IsValid() && record.Instance.Index == instanceIndex)
                {
                    stillLive = true;
                    break;
                }
            }
            if (!stillLive)
            {
                stale.push_back(instanceIndex);
            }
        }
        for (const auto idx : stale)
        {
            m_Impl->PreviousModelByInstance.erase(idx);
        }
    }

    bool TransformSyncSystem::IsInitialized() const noexcept
    {
        return m_Impl->Initialized;
    }
}
