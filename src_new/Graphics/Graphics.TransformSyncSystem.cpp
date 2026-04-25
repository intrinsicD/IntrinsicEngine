//
// Created by alex on 22.04.26.
//

module;

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>
#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>

module Extrinsic.Graphics.TransformSyncSystem;

import Extrinsic.Graphics.MaterialSystem;
import Extrinsic.ECS.Component.Transform.WorldMatrix;
import Extrinsic.ECS.Component.Culling.Local;
import Extrinsic.Graphics.Component.GpuSceneSlot;
import Extrinsic.Graphics.Component.Material;
import Extrinsic.Graphics.Component.RenderGeometry;
import Extrinsic.Graphics.Material;
import Extrinsic.RHI.Types;

namespace Extrinsic::Graphics
{
    namespace
    {
        [[nodiscard]] float MaxAxisScale(const glm::mat4& m) noexcept
        {
            const float sx = glm::length(glm::vec3(m[0]));
            const float sy = glm::length(glm::vec3(m[1]));
            const float sz = glm::length(glm::vec3(m[2]));
            return std::max({sx, sy, sz});
        }
    }

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

    void TransformSyncSystem::SyncGpuBuffer(entt::registry&   registry,
                                            GpuWorld&         gpuWorld,
                                            MaterialSystem&   materialSystem)
    {
        using namespace Components;
        using namespace ECS::Components;

        if (!m_Impl->Initialized)
        {
            return;
        }

        auto view = registry.view<Transform::WorldMatrix, GpuSceneSlot>();
        for (const auto [entity, world, gpuSlot] : view.each())
        {
            if (!gpuSlot.HasInstance())
            {
                continue;
            }

            const auto instance = gpuSlot.ToInstanceHandle();
            if (!instance.IsValid())
            {
                continue;
            }

            const glm::mat4 model = world.Matrix;
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

            std::uint32_t renderFlags = RHI::GpuRender_Visible;
            if (registry.all_of<RenderSurface>(entity))
                renderFlags |= RHI::GpuRender_Surface;
            if (registry.all_of<RenderLines>(entity))
                renderFlags |= RHI::GpuRender_Line;
            if (registry.all_of<RenderPoints>(entity))
                renderFlags |= RHI::GpuRender_Point;

            if (const auto* matInst = registry.try_get<MaterialInstance>(entity))
            {
                gpuWorld.SetInstanceMaterialSlot(instance, matInst->EffectiveSlot);

                if (matInst->Lease.IsValid())
                {
                    const MaterialParams params = materialSystem.GetParams(matInst->Lease.GetHandle());
                    if (HasFlag(params.Flags, MaterialFlags::AlphaMask))
                        renderFlags |= RHI::GpuRender_AlphaMask;
                    else if (HasFlag(params.Flags, MaterialFlags::AlphaBlend))
                        renderFlags |= RHI::GpuRender_Transparent;
                    else
                        renderFlags |= RHI::GpuRender_Opaque;

                    if (HasFlag(params.Flags, MaterialFlags::Unlit))
                        renderFlags |= RHI::GpuRender_Unlit;
                }
                else
                {
                    renderFlags |= RHI::GpuRender_Opaque;
                }
            }
            else
            {
                renderFlags |= RHI::GpuRender_Opaque;
            }

            gpuWorld.SetInstanceRenderFlags(instance, renderFlags);

            RHI::GpuBounds bounds{};
            if (const auto* local = registry.try_get<Culling::Bounds>(entity))
            {
                const glm::vec3 localCenter = local->LocalBoundingSphere.Center;
                const float localRadius = std::max(local->LocalBoundingSphere.Radius, 0.0f);

                const glm::vec3 worldCenter = glm::vec3(model * glm::vec4(localCenter, 1.0f));
                const float worldRadius = localRadius * MaxAxisScale(model);

                bounds.LocalSphere = glm::vec4(localCenter, localRadius);
                bounds.WorldSphere = glm::vec4(worldCenter, worldRadius);

                const auto& localAabb = local->LocalBoundingAABB;
                if (localAabb.IsValid())
                {
                    const glm::vec3 corners[8] = {
                        {localAabb.Min.x, localAabb.Min.y, localAabb.Min.z},
                        {localAabb.Max.x, localAabb.Min.y, localAabb.Min.z},
                        {localAabb.Min.x, localAabb.Max.y, localAabb.Min.z},
                        {localAabb.Max.x, localAabb.Max.y, localAabb.Min.z},
                        {localAabb.Min.x, localAabb.Min.y, localAabb.Max.z},
                        {localAabb.Max.x, localAabb.Min.y, localAabb.Max.z},
                        {localAabb.Min.x, localAabb.Max.y, localAabb.Max.z},
                        {localAabb.Max.x, localAabb.Max.y, localAabb.Max.z},
                    };

                    glm::vec3 worldMin = glm::vec3(model * glm::vec4(corners[0], 1.0f));
                    glm::vec3 worldMax = worldMin;
                    for (std::size_t i = 1; i < 8; ++i)
                    {
                        const glm::vec3 p = glm::vec3(model * glm::vec4(corners[i], 1.0f));
                        worldMin = glm::min(worldMin, p);
                        worldMax = glm::max(worldMax, p);
                    }

                    bounds.WorldAabbMin = glm::vec4(worldMin, 0.0f);
                    bounds.WorldAabbMax = glm::vec4(worldMax, 0.0f);
                }
            }
            gpuWorld.SetBounds(instance, bounds);
        }

        std::vector<std::uint32_t> stale;
        stale.reserve(m_Impl->PreviousModelByInstance.size());
        for (const auto& [instanceIndex, _] : m_Impl->PreviousModelByInstance)
        {
            bool stillLive = false;
            for (const auto [entity, gpuSlot] : registry.view<GpuSceneSlot>().each())
            {
                if (gpuSlot.HasInstance() && gpuSlot.InstanceSlot == instanceIndex)
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
