//
// Created by alex on 22.04.26.
//

module;

#include <memory>
#include <entt/entity/registry.hpp>

export module Extrinsic.Graphics.TransformSyncSystem;

import Extrinsic.Graphics.GpuWorld;

export namespace Extrinsic::Graphics
{
    class TransformSyncSystem
    {
    public:
        TransformSyncSystem();
        ~TransformSyncSystem();

        TransformSyncSystem(const TransformSyncSystem&)            = delete;
        TransformSyncSystem& operator=(const TransformSyncSystem&) = delete;

        void Initialize();
        void Shutdown();
        void SyncGpuBuffer(entt::registry& registry, GpuWorld& gpuWorld);

        [[nodiscard]] bool IsInitialized() const noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
