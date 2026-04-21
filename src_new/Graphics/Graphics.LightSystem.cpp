module;

#include <memory>

#include <glm/geometric.hpp>

module Extrinsic.Graphics.LightSystem;

import Extrinsic.RHI.Types;

namespace Extrinsic::Graphics
{
	struct LightSystem::Impl
	{
		LightState State{};
		bool       Initialized{false};
	};

	namespace
	{
		[[nodiscard]] glm::vec3 NormalizeOrFallback(glm::vec3 direction) noexcept
		{
			const float len2 = glm::dot(direction, direction);
			if (len2 <= 1e-12f)
				return {0.f, -1.f, 0.f};
			return glm::normalize(direction);
		}
	}

	LightSystem::LightSystem()
		: m_Impl(std::make_unique<Impl>())
	{}

	LightSystem::~LightSystem() = default;

	void LightSystem::Initialize()
	{
		m_Impl->State.Direction = NormalizeOrFallback(m_Impl->State.Direction);
		m_Impl->Initialized = true;
	}

	void LightSystem::Shutdown()
	{
		m_Impl->State = {};
		m_Impl->Initialized = false;
	}

	void LightSystem::SetState(const LightState& state) noexcept
	{
		m_Impl->State = state;
		m_Impl->State.Direction = NormalizeOrFallback(m_Impl->State.Direction);
	}

	LightState LightSystem::GetState() const noexcept
	{
		return m_Impl->State;
	}

	void LightSystem::SetDirectionalLight(glm::vec3 direction,
										  float     intensity,
										  glm::vec3 color) noexcept
	{
		m_Impl->State.Direction = NormalizeOrFallback(direction);
		m_Impl->State.Intensity = intensity;
		m_Impl->State.Color     = color;
	}

	void LightSystem::SetAmbientLight(glm::vec3 color, float intensity) noexcept
	{
		m_Impl->State.AmbientColor     = color;
		m_Impl->State.AmbientIntensity = intensity;
	}

	void LightSystem::ApplyTo(RHI::CameraUBO& camera) const noexcept
	{
		const LightState state = GetState();
		camera.LightDirAndIntensity      = {state.Direction, state.Intensity};
		camera.LightColor                = {state.Color, 0.f};
		camera.AmbientColorAndIntensity  = {state.AmbientColor, state.AmbientIntensity};
	}

	bool LightSystem::IsInitialized() const noexcept
	{
		return m_Impl->Initialized;
	}
}
