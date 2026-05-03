module;

#include <cstdint>
#include <memory>
#include <vector>

export module Extrinsic.Graphics.PostProcessSystem;

export namespace Extrinsic::Graphics
{
    enum class PostProcessAntiAliasing : std::uint8_t
    {
        None = 0,
        FXAA,
        SMAA,
    };

    enum class PostProcessStageKind : std::uint8_t
    {
        Histogram = 0,
        Bloom,
        ToneMap,
        FXAA,
        SMAA,
    };

    struct PostProcessSettings
    {
        bool Enabled{true};
        bool EnableHistogram{false};
        bool EnableBloom{false};
        PostProcessAntiAliasing AntiAliasing{PostProcessAntiAliasing::None};
        float Exposure{1.0f};
        float Gamma{2.2f};
        float BloomIntensity{0.05f};
        std::uint32_t HistogramBinCount{256u};
    };

    struct PostProcessStageDesc
    {
        PostProcessStageKind Kind{PostProcessStageKind::ToneMap};
        const char* Name{"ToneMap"};
        bool ReadsHDR{false};
        bool WritesLDR{false};
        bool UsesIntermediate{false};
    };

    struct PostProcessDiagnostics
    {
        std::uint32_t InvalidSettingCount{0u};
        std::uint32_t UnsupportedCombinationCount{0u};
        bool ChainEnabled{true};
        bool WritesLDR{true};
    };

    struct PostProcessChainDesc
    {
        bool Enabled{true};
        bool WritesLDR{true};
        std::vector<PostProcessStageDesc> Stages{};
        PostProcessDiagnostics Diagnostics{};
    };

    struct PostProcessPushConstants
    {
        float Exposure{1.0f};
        float Gamma{2.2f};
        float BloomIntensity{0.05f};
        std::uint32_t HistogramBinCount{256u};
        std::uint32_t StageKind{0u};
    };

    class PostProcessSystem
    {
    public:
        PostProcessSystem();
        ~PostProcessSystem();

        PostProcessSystem(const PostProcessSystem&)            = delete;
        PostProcessSystem& operator=(const PostProcessSystem&) = delete;

        void Initialize();
        void Shutdown();

        void SetSettings(const PostProcessSettings& settings);

        [[nodiscard]] bool IsInitialized() const noexcept;
        [[nodiscard]] const PostProcessSettings& GetSettings() const noexcept;
        [[nodiscard]] PostProcessDiagnostics GetDiagnostics() const noexcept;
        [[nodiscard]] PostProcessChainDesc DescribeChain() const;
        [[nodiscard]] bool IsStageEnabled(PostProcessStageKind stage) const;
        [[nodiscard]] PostProcessPushConstants BuildPushConstants(PostProcessStageKind stage) const noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
