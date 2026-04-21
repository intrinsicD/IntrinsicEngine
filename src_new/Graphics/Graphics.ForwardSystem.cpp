module;

#include <memory>

module Extrinsic.Graphics.ForwardSystem;

namespace Extrinsic::Graphics
{
	struct ForwardSystem::Impl
	{
		bool Initialized{false};
	};

	ForwardSystem::ForwardSystem()
		: m_Impl(std::make_unique<Impl>())
	{}

	ForwardSystem::~ForwardSystem() = default;

	void ForwardSystem::Initialize()
	{
		m_Impl->Initialized = true;
	}

	void ForwardSystem::Shutdown()
	{
		m_Impl->Initialized = false;
	}

	bool ForwardSystem::IsInitialized() const noexcept
	{
		return m_Impl->Initialized;
	}
}

