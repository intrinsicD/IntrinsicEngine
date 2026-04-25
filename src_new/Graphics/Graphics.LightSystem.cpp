module;

#include <memory>
#include <vector>

#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>
#include <glm/geometric.hpp>

module Extrinsic.Graphics.LightSystem;

import Extrinsic.RHI.Types;
import Extrinsic.Graphics.GpuWorld;
import Extrinsic.ECS.Component.Light;
import Extrinsic.ECS.Component.Transform.WorldMatrix;

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

        [[nodiscard]] glm::vec3 ExtractTranslation(const glm::mat4& m) noexcept
        {
            return {m[3].x, m[3].y, m[3].z};
        }

        [[nodiscard]] glm::vec3 TransformDirection(const glm::mat4& m, glm::vec3 v) noexcept
        {
            return NormalizeOrFallback(glm::vec3(m * glm::vec4(v, 0.f)));
        }

        constexpr float kDirectionalType = 0.0f;
        constexpr float kPointType       = 1.0f;
        constexpr float kSpotType        = 2.0f;
        constexpr float kDefaultRange    = 10.0f;
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

    void LightSystem::SyncGpuBuffer(entt::registry& registry, GpuWorld& gpuWorld)
    {
        using namespace ECS::Components;
        using namespace ECS::Components::Lights;

        if (!m_Impl->Initialized)
        {
            return;
        }

        std::vector<RHI::GpuLight> lights;
        lights.reserve(64);

        bool directionalFromRegistry = false;
        for (const auto [entity, directional, world] : registry.view<DirectionalLight, Transform::WorldMatrix>().each())
        {
            (void)entity;
            RHI::GpuLight light{};
            light.Position_Range  = glm::vec4(0.f, 0.f, 0.f, 0.f);
            light.Direction_Type  = glm::vec4(TransformDirection(world.Matrix, {0.f, -1.f, 0.f}), kDirectionalType);
            light.Color_Intensity = glm::vec4(directional.Color, directional.Intensity);
            lights.push_back(light);
            directionalFromRegistry = true;
        }

        for (const auto [entity, point, world] : registry.view<PointLight, Transform::WorldMatrix>().each())
        {
            (void)entity;
            RHI::GpuLight light{};
            light.Position_Range  = glm::vec4(ExtractTranslation(world.Matrix), kDefaultRange);
            light.Direction_Type  = glm::vec4(0.f, 0.f, 0.f, kPointType);
            light.Color_Intensity = glm::vec4(point.Color, point.Intensity);
            lights.push_back(light);
        }

        for (const auto [entity, spot, world] : registry.view<SpotLight, Transform::WorldMatrix>().each())
        {
            (void)entity;
            const glm::vec3 dir = TransformDirection(world.Matrix, spot.Direction);
            RHI::GpuLight light{};
            light.Position_Range  = glm::vec4(ExtractTranslation(world.Matrix), kDefaultRange);
            light.Direction_Type  = glm::vec4(dir, kSpotType);
            light.Color_Intensity = glm::vec4(spot.Color, spot.Intensity);
            light.Params          = glm::vec4(0.80f, 0.65f, 0.f, 0.f); // cos(inner), cos(outer), reserved
            lights.push_back(light);
        }

        for (const auto [entity, ambient] : registry.view<AmbientLight>().each())
        {
            (void)entity;
            m_Impl->State.AmbientColor = ambient.Color;
            m_Impl->State.AmbientIntensity = 1.0f;
            break;
        }

        if (!directionalFromRegistry)
        {
            RHI::GpuLight fallback{};
            fallback.Direction_Type  = glm::vec4(m_Impl->State.Direction, kDirectionalType);
            fallback.Color_Intensity = glm::vec4(m_Impl->State.Color, m_Impl->State.Intensity);
            lights.push_back(fallback);
        }

        gpuWorld.SetLights(lights);
    }

	bool LightSystem::IsInitialized() const noexcept
	{
		return m_Impl->Initialized;
	}
}
