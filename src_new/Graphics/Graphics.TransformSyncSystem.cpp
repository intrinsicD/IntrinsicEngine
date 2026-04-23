//
// Created by alex on 22.04.26.
//

module;

#include <memory>

module Extrinsic.Graphics.TransformSyncSystem;

namespace Extrinsic::Graphics
{
    struct TransformSyncSystem::Impl
    {
        bool Initialized{false};
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
        m_Impl->Initialized = false;
    }

    void TransformSyncSystem::SyncGpuBuffer()
    {
    }

    bool TransformSyncSystem::IsInitialized() const noexcept
    {
        return m_Impl->Initialized;
    }
}
