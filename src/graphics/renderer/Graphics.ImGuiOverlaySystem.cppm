module;

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

export module Extrinsic.Graphics.ImGuiOverlaySystem;

export namespace Extrinsic::Graphics
{
    struct ImGuiOverlayDrawList
    {
        std::uint32_t CommandCount{0u};
        std::uint32_t VertexCount{0u};
        std::uint32_t IndexCount{0u};
        bool UsesUserTexture{false};
    };

    struct ImGuiOverlayFrame
    {
        bool Enabled{false};
        std::uint32_t DisplayWidth{0u};
        std::uint32_t DisplayHeight{0u};
        std::vector<ImGuiOverlayDrawList> DrawLists{};
    };

    struct ImGuiOverlayDiagnostics
    {
        std::uint32_t SubmittedDrawListCount{0u};
        std::uint32_t AcceptedDrawListCount{0u};
        std::uint32_t RejectedDrawListCount{0u};
        std::uint32_t DrawCommandCount{0u};
        std::uint32_t VertexCount{0u};
        std::uint32_t IndexCount{0u};
        bool Enabled{false};
        bool InvalidDisplaySize{false};
        bool HasUserTextures{false};
    };

    struct ImGuiOverlayPushConstants
    {
        std::uint32_t DrawCommandCount{0u};
        std::uint32_t VertexCount{0u};
        std::uint32_t IndexCount{0u};
        std::uint32_t Flags{0u};
    };

    struct PresentFinalizationInputs
    {
        bool PresentationSourceAvailable{false};
        bool BackbufferImported{false};
        bool PresentPassEnabled{true};
    };

    struct PresentFinalizationDiagnostics
    {
        bool CanFinalize{false};
        bool MissingPresentationSource{false};
        bool MissingBackbuffer{false};
        bool PresentPassDisabled{false};
    };

    class ImGuiOverlaySystem
    {
    public:
        ImGuiOverlaySystem();
        ~ImGuiOverlaySystem();

        ImGuiOverlaySystem(const ImGuiOverlaySystem&) = delete;
        ImGuiOverlaySystem& operator=(const ImGuiOverlaySystem&) = delete;

        void Initialize();
        void Shutdown();
        void SubmitFrame(const ImGuiOverlayFrame& frame);
        void ClearFrame();

        [[nodiscard]] bool IsInitialized() const noexcept;
        [[nodiscard]] bool HasOverlayWork() const noexcept;
        [[nodiscard]] ImGuiOverlayDiagnostics GetDiagnostics() const noexcept;
        [[nodiscard]] ImGuiOverlayPushConstants BuildPushConstants() const noexcept;
        [[nodiscard]] std::string FormatDiagnostics() const;
        [[nodiscard]] PresentFinalizationDiagnostics ValidatePresentFinalization(
            const PresentFinalizationInputs& inputs) const noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}

