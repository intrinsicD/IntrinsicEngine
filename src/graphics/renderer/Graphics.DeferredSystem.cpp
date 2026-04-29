module;

#include <memory>

module Extrinsic.Graphics.DeferredSystem;

namespace Extrinsic::Graphics
{
    struct DeferredSystem::Impl
    {
        bool Initialized{false};
    };

    DeferredSystem::DeferredSystem()
        : m_Impl(std::make_unique<Impl>())
    {}

    DeferredSystem::~DeferredSystem() = default;

    void DeferredSystem::Initialize()
    {
        m_Impl->Initialized = true;
    }

    void DeferredSystem::Shutdown()
    {
        m_Impl->Initialized = false;
    }

    bool DeferredSystem::IsInitialized() const noexcept
    {
        return m_Impl->Initialized;
    }
}
