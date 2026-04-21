module;

#include <memory>

module Extrinsic.Graphics.ShadowSystem;

namespace Extrinsic::Graphics
{
    struct ShadowSystem::Impl
    {
        bool Initialized{false};
    };

    ShadowSystem::ShadowSystem()
        : m_Impl(std::make_unique<Impl>())
    {}

    ShadowSystem::~ShadowSystem() = default;

    void ShadowSystem::Initialize()
    {
        m_Impl->Initialized = true;
    }

    void ShadowSystem::Shutdown()
    {
        m_Impl->Initialized = false;
    }

    bool ShadowSystem::IsInitialized() const noexcept
    {
        return m_Impl->Initialized;
    }
}
