module;

#include <memory>

module Extrinsic.Graphics.PostProcessSystem;

namespace Extrinsic::Graphics
{
    struct PostProcessSystem::Impl
    {
        bool Initialized{false};
    };

    PostProcessSystem::PostProcessSystem()
        : m_Impl(std::make_unique<Impl>())
    {}

    PostProcessSystem::~PostProcessSystem() = default;

    void PostProcessSystem::Initialize()
    {
        m_Impl->Initialized = true;
    }

    void PostProcessSystem::Shutdown()
    {
        m_Impl->Initialized = false;
    }

    bool PostProcessSystem::IsInitialized() const noexcept
    {
        return m_Impl->Initialized;
    }
}
