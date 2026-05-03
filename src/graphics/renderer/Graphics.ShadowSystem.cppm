module;

#include <memory>
#include <cstdint>

#include <glm/glm.hpp>

export module Extrinsic.Graphics.ShadowSystem;

import Extrinsic.RHI.Types;

export namespace Extrinsic::Graphics
{
    struct ShadowParams
    {
        bool Enabled{false};
        std::uint32_t CascadeCount{0u};
        std::uint32_t AtlasResolution{2048u};
        float DepthBias{0.001f};
        float NormalBias{0.01f};
        float PcfRadius{1.0f};
        float SplitLambda{0.5f};
    };

    struct ShadowCascadeData
    {
        glm::mat4 ViewProj[RHI::kMaxShadowCascades]{};
        float Splits[RHI::kMaxShadowCascades]{};
        std::uint32_t CascadeCount{0u};
    };

    struct ShadowAtlasDesc
    {
        bool Enabled{false};
        std::uint32_t Width{0u};
        std::uint32_t Height{0u};
        std::uint32_t CascadeCount{0u};
        std::uint32_t Resolution{0u};
    };

    struct ShadowDiagnostics
    {
        std::uint32_t UnsupportedCascadeCount{0u};
        std::uint32_t DisabledShadowStateCount{0u};
    };

    class ShadowSystem
    {
    public:
        ShadowSystem();
        ~ShadowSystem();

        ShadowSystem(const ShadowSystem&)            = delete;
        ShadowSystem& operator=(const ShadowSystem&) = delete;

        void Initialize();
        void Shutdown();

        void SetParams(const ShadowParams& params) noexcept;
        [[nodiscard]] ShadowParams GetParams() const noexcept;

        void SetCascadeData(const ShadowCascadeData& cascades) noexcept;
        [[nodiscard]] ShadowCascadeData GetCascadeData() const noexcept;

        [[nodiscard]] ShadowAtlasDesc BuildAtlasDesc() const noexcept;
        void ApplyTo(RHI::CameraUBO& camera) const noexcept;
        [[nodiscard]] ShadowDiagnostics GetDiagnostics() const noexcept;
        [[nodiscard]] bool IsEnabled() const noexcept;
        [[nodiscard]] bool IsInitialized() const noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
