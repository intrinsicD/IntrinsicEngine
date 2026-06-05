module;

#include <memory>
#include <span>
#include <vector>

#include <glm/glm.hpp>
#include <glm/geometric.hpp>

module Extrinsic.Graphics.LightSystem;

import Extrinsic.RHI.Types;
import Extrinsic.Graphics.GpuWorld;

namespace Extrinsic::Graphics
{
	struct LightSystem::Impl
	{
		LightState State{};
		LightSyncDiagnostics Diagnostics{};
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

		constexpr float kDirectionalType = 0.0f;
		constexpr float kPointType       = 1.0f;
		constexpr float kSpotType        = 2.0f;
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

	LightEnvironmentPacket LightSystem::BuildEnvironmentPacket(std::span<const LightSnapshot> snapshots) const
	{
		LightEnvironmentPacket packet{};
		packet.State = m_Impl->State;

		for (const LightSnapshot& snapshot : snapshots)
		{
			switch (snapshot.LightType)
			{
			case LightSnapshot::Type::Directional:
				++packet.DirectionalLightCount;
				++packet.UploadedLightCount;
				break;
			case LightSnapshot::Type::Point:
			case LightSnapshot::Type::Spot:
				++packet.UploadedLightCount;
				break;
			default:
				++packet.UnsupportedLightCount;
				break;
			}
		}

		packet.UsedFallbackDirectional = packet.DirectionalLightCount == 0u;
		if (packet.UsedFallbackDirectional)
		{
			++packet.UploadedLightCount;
		}
		return packet;
	}

    void LightSystem::SyncGpuBuffer(std::span<const LightSnapshot> snapshots, GpuWorld& gpuWorld)
    {
        if (!m_Impl->Initialized)
        {
            return;
        }

        const LightEnvironmentPacket packet = BuildEnvironmentPacket(snapshots);
        std::vector<RHI::GpuLight> lights;
        lights.reserve(packet.UploadedLightCount);

        bool directionalFromSnapshots = false;
        for (const LightSnapshot& snapshot : snapshots)
        {
            RHI::GpuLight light{};

            switch (snapshot.LightType)
            {
            case LightSnapshot::Type::Directional:
                light.Position_Range  = glm::vec4(0.f, 0.f, 0.f, 0.f);
                light.Direction_Type  = glm::vec4(NormalizeOrFallback(snapshot.Direction), kDirectionalType);
                directionalFromSnapshots = true;
                break;
            case LightSnapshot::Type::Point:
                light.Position_Range  = glm::vec4(snapshot.Position, snapshot.Range);
                light.Direction_Type  = glm::vec4(0.f, 0.f, 0.f, kPointType);
                break;
            case LightSnapshot::Type::Spot:
                light.Position_Range  = glm::vec4(snapshot.Position, snapshot.Range);
                light.Direction_Type  = glm::vec4(NormalizeOrFallback(snapshot.Direction), kSpotType);
                light.Params          = glm::vec4(snapshot.InnerConeCos, snapshot.OuterConeCos, 0.f, 0.f);
                break;
	  default:
		continue;
            }

            light.Color_Intensity = glm::vec4(snapshot.Color, snapshot.Intensity);
            lights.push_back(light);
        }

        if (!directionalFromSnapshots)
        {
            RHI::GpuLight fallback{};
            fallback.Direction_Type  = glm::vec4(m_Impl->State.Direction, kDirectionalType);
            fallback.Color_Intensity = glm::vec4(m_Impl->State.Color, m_Impl->State.Intensity);
            lights.push_back(fallback);
        }

        gpuWorld.SetLights(lights);
		m_Impl->Diagnostics.UploadedLightCount = static_cast<std::uint32_t>(lights.size());
		m_Impl->Diagnostics.UnsupportedLightCount = packet.UnsupportedLightCount;
		m_Impl->Diagnostics.UsedFallbackDirectional = !directionalFromSnapshots;
    }

	void LightSystem::PublishClusterAssignmentDiagnostics(const std::uint32_t overflowCount,
														 const std::uint32_t lightsCulledCount,
														 const std::uint32_t emptyClusterCount) noexcept
	{
		m_Impl->Diagnostics.LightClusterOverflowCount = overflowCount;
		m_Impl->Diagnostics.LightsCulledCount = lightsCulledCount;
		m_Impl->Diagnostics.EmptyClusterCount = emptyClusterCount;
	}

  LightSyncDiagnostics LightSystem::GetDiagnostics() const noexcept
  {
	return m_Impl->Diagnostics;
  }

	bool LightSystem::IsInitialized() const noexcept
	{
		return m_Impl->Initialized;
	}
}
