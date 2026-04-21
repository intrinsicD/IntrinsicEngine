module;

#include <memory>

#include <glm/glm.hpp>

export module Extrinsic.Graphics.LightSystem;

import Extrinsic.RHI.Types;

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

	class LightSystem
	{
	public:
		LightSystem();
		~LightSystem();

		LightSystem(const LightSystem&)            = delete;
		LightSystem& operator=(const LightSystem&) = delete;

		void Initialize();
		void Shutdown();

		void SetState(const LightState& state) noexcept;
		[[nodiscard]] LightState GetState() const noexcept;

		void SetDirectionalLight(glm::vec3 direction,
								 float     intensity,
								 glm::vec3 color) noexcept;

		void SetAmbientLight(glm::vec3 color, float intensity) noexcept;

		void ApplyTo(RHI::CameraUBO& camera) const noexcept;

		[[nodiscard]] bool IsInitialized() const noexcept;

	private:
		struct Impl;
		std::unique_ptr<Impl> m_Impl;
	};
}

