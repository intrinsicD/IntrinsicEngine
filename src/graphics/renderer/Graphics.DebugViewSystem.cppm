module;

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

export module Extrinsic.Graphics.DebugViewSystem;

import Extrinsic.Graphics.FrameRecipe;

export namespace Extrinsic::Graphics
{
    enum class DebugViewResourceClass : std::uint8_t
    {
        Texture = 0,
        DepthTexture,
        Buffer,
        Backbuffer,
        Alias,
        Unknown,
    };

    enum class DebugViewFallbackReason : std::uint8_t
    {
        None = 0,
        DebugViewDisabled,
        ResourceMissing,
        ResourceDisabled,
        UnsupportedResourceClass,
        FallbackUnavailable,
    };

    struct DebugViewSettings
    {
        bool Enabled{false};
        std::string RequestedResourceName{"FrameRecipe.PresentSource"};
    };

    struct DebugViewInspectableResource
    {
        std::string Name{};
        FrameRecipeResourceKind Kind{FrameRecipeResourceKind::SceneColorHDR};
        DebugViewResourceClass ResourceClass{DebugViewResourceClass::Unknown};
        bool Enabled{false};
        bool Sampleable{false};
        bool Previewable{false};
        bool Imported{false};
        bool Backbuffer{false};
    };

    struct DebugViewResolvedSelection
    {
        bool Enabled{false};
        std::string RequestedResourceName{};
        std::string SelectedResourceName{};
        FrameRecipeResourceKind SelectedKind{FrameRecipeResourceKind::SceneColorHDR};
        DebugViewResourceClass SelectedClass{DebugViewResourceClass::Unknown};
        bool UsedFallback{false};
        DebugViewFallbackReason FallbackReason{DebugViewFallbackReason::None};
    };

    struct DebugViewDiagnostics
    {
        std::uint32_t MissingResourceCount{0u};
        std::uint32_t DisabledResourceCount{0u};
        std::uint32_t UnsupportedResourceCount{0u};
        bool UsedFallback{false};
        DebugViewFallbackReason LastFallbackReason{DebugViewFallbackReason::None};
    };

    struct DebugViewPushConstants
    {
        std::uint32_t ResourceKind{0u};
        std::uint32_t ResourceClass{0u};
        std::uint32_t UsedFallback{0u};
        std::uint32_t Reserved{0u};
    };

    class DebugViewSystem
    {
    public:
        DebugViewSystem();
        ~DebugViewSystem();

        DebugViewSystem(const DebugViewSystem&) = delete;
        DebugViewSystem& operator=(const DebugViewSystem&) = delete;

        void Initialize();
        void Shutdown();
        void SetSettings(DebugViewSettings settings);

        [[nodiscard]] bool IsInitialized() const noexcept;
        [[nodiscard]] const DebugViewSettings& GetSettings() const noexcept;
        [[nodiscard]] DebugViewDiagnostics GetDiagnostics() const noexcept;
        [[nodiscard]] DebugViewResolvedSelection GetResolvedSelection() const;

        [[nodiscard]] std::vector<DebugViewInspectableResource> BuildInspectionTable(
            const FrameRecipeIntrospection& frameRecipe) const;
        [[nodiscard]] DebugViewResolvedSelection ResolveSelection(const FrameRecipeIntrospection& frameRecipe,
                                                                  std::string fallbackResourceName = "SceneColorLDR");
        [[nodiscard]] std::string FormatInspectionDump(const FrameRecipeIntrospection& frameRecipe) const;
        [[nodiscard]] DebugViewPushConstants BuildPushConstants() const noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}

