// LightSystem: global light state and per-frame light snapshots packed for
// the camera UBO and the GpuWorld light buffer.
module;

#include <memory>
#include <span>
#include <cstdint>

#include <glm/glm.hpp>

export module Extrinsic.Graphics.LightSystem;

import Extrinsic.RHI.Types;
import Extrinsic.Graphics.GpuWorld;

export namespace Extrinsic::Graphics
{
	struct LightState
	{
		glm::vec3 Direction{0.f, -1.f, 0.f};
		float     Intensity{1.f};

		glm::vec3 Color{1.f, 1.f, 1.f};
		float     _pad0{0.f};

		glm::vec3 AmbientColor{0.2f, 0.2f, 0.2f};
		float     AmbientIntensity{1.f};
	};

	struct LightSnapshot
	{
		enum class Type : std::uint8_t
		{
			Directional,
			Point,
			Spot,
		} LightType{Type::Directional};

		glm::vec3 Position{0.f};
		float     Range{10.f};
		glm::vec3 Direction{0.f, -1.f, 0.f};
		float     Intensity{1.f};
		glm::vec3 Color{1.f};
		float     InnerConeCos{0.80f};
		float     OuterConeCos{0.65f};
	};

	struct LightEnvironmentPacket
	{
		LightState State{};
		std::uint32_t UploadedLightCount{0u};
		std::uint32_t DirectionalLightCount{0u};
		std::uint32_t UnsupportedLightCount{0u};
		bool UsedFallbackDirectional{false};
	};

	struct LightSyncDiagnostics
	{
		std::uint32_t UploadedLightCount{0u};
		std::uint32_t UnsupportedLightCount{0u};
		std::uint32_t LightClusterOverflowCount{0u};
		std::uint32_t LightsCulledCount{0u};
		std::uint32_t EmptyClusterCount{0u};
		bool UsedFallbackDirectional{false};
	};

	class LightSystem
	{
	public:
		LightSystem();
		~LightSystem();

		LightSystem(const LightSystem&)            = delete;
		LightSystem& operator=(const LightSystem&) = delete;

		void Initialize();
		void Shutdown();

		[[nodiscard]] LightState GetState() const noexcept;

		void ApplyTo(RHI::CameraUBO& camera) const noexcept;
		[[nodiscard]] LightEnvironmentPacket BuildEnvironmentPacket(std::span<const LightSnapshot> lights) const;
		void SyncGpuBuffer(std::span<const LightSnapshot> lights, GpuWorld& gpuWorld);
		void PublishClusterAssignmentDiagnostics(std::uint32_t overflowCount,
												 std::uint32_t lightsCulledCount,
												 std::uint32_t emptyClusterCount) noexcept;
		[[nodiscard]] LightSyncDiagnostics GetDiagnostics() const noexcept;

		[[nodiscard]] bool IsInitialized() const noexcept;

	private:
		struct Impl;
		std::unique_ptr<Impl> m_Impl;
	};
}
