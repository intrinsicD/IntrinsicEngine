module;

#include <cstdint>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

export module Extrinsic.Graphics.ImGuiOverlaySystem;

import Extrinsic.RHI.Bindless;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.SamplerManager;
import Extrinsic.RHI.TextureManager;

export namespace Extrinsic::Graphics
{
    struct ImGuiOverlayVertex
    {
        float Position[2]{0.0f, 0.0f};
        float UV[2]{0.0f, 0.0f};
        std::uint32_t Color{0xffffffffu};
    };

    struct ImGuiOverlayDrawCommand
    {
        std::uint32_t IndexOffset{0u};
        std::uint32_t VertexOffset{0u};
        std::uint32_t IndexCount{0u};
        RHI::BindlessIndex TextureBindlessIndex{RHI::kInvalidBindlessIndex};
        bool UsesUserTexture{false};
    };

    struct ImGuiOverlayFontAtlas
    {
        bool Valid{false};
        std::uint32_t Width{0u};
        std::uint32_t Height{0u};
        std::uint32_t BytesPerPixel{1u};
        bool UseColors{false};
        bool Dirty{true};
        std::uint64_t Revision{0u};
        std::vector<std::byte> Pixels{};
    };

    struct ImGuiOverlayDrawList
    {
        std::uint32_t CommandCount{0u};
        std::uint32_t VertexCount{0u};
        std::uint32_t IndexCount{0u};
        bool UsesUserTexture{false};
        std::vector<ImGuiOverlayVertex> Vertices{};
        std::vector<std::uint32_t> Indices{};
        std::vector<ImGuiOverlayDrawCommand> Commands{};
    };

    struct ImGuiOverlayFrame
    {
        bool Enabled{false};
        std::uint32_t DisplayWidth{0u};
        std::uint32_t DisplayHeight{0u};
        ImGuiOverlayFontAtlas FontAtlas{};
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
        std::uint32_t DrawCalls{0u};
        bool Enabled{false};
        bool InvalidDisplaySize{false};
        bool HasUserTextures{false};
        bool FontAtlasAvailable{false};
        bool FontAtlasGpuAllocated{false};
        bool FontAtlasUploadQueued{false};
        bool FontAtlasRetained{false};
        bool FontAtlasAllocationFailed{false};
        bool FontAtlasUploadFailed{false};
        std::uint32_t FontAtlasWidth{0u};
        std::uint32_t FontAtlasHeight{0u};
        std::uint32_t FontAtlasBytesPerPixel{0u};
        std::uint64_t FontAtlasByteCount{0u};
        std::uint64_t FontAtlasRevision{0u};
        std::uint32_t FontAtlasRetainCount{0u};
        std::uint32_t FontAtlasAllocationCount{0u};
        std::uint32_t FontAtlasUploadCount{0u};
        RHI::TextureHandle FontAtlasTexture{};
        RHI::BindlessIndex FontAtlasBindlessIndex{RHI::kInvalidBindlessIndex};
    };

    struct ImGuiOverlayPushConstants
    {
        std::uint64_t VertexBufferBDA{0u};
        std::uint32_t FirstVertex{0u};
        std::uint32_t IndexCount{0u};
        std::uint32_t FontAtlasBindlessIndex{RHI::kInvalidBindlessIndex};
        std::uint32_t TextureBindlessIndex{RHI::kInvalidBindlessIndex};
        std::uint32_t Flags{0u};
        std::uint32_t Reserved0{0u};
        float Scale[2]{1.0f, 1.0f};
        float Translate[2]{0.0f, 0.0f};
    };

    inline constexpr std::uint32_t kImGuiOverlayPushFlagUserTexture = 1u << 0u;
    inline constexpr std::uint32_t kImGuiOverlayPushFlagFontAtlasColor = 1u << 1u;
    static_assert(sizeof(ImGuiOverlayPushConstants) == 48u);

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
        void InitializeGpuResources(RHI::IDevice& device,
                                    RHI::TextureManager& textureManager,
                                    RHI::SamplerManager& samplerManager);
        void Shutdown();
        void ShutdownGpuResources();
        void UploadPendingFontAtlas();
        void SubmitFrame(const ImGuiOverlayFrame& frame);
        void SubmitFrame(ImGuiOverlayFrame&& frame);
        void ClearFrame();
        void RecordDrawCalls(std::uint32_t drawCalls) noexcept;

        [[nodiscard]] bool IsInitialized() const noexcept;
        [[nodiscard]] bool HasOverlayWork() const noexcept;
        [[nodiscard]] ImGuiOverlayDiagnostics GetDiagnostics() const noexcept;
        [[nodiscard]] const ImGuiOverlayFrame* GetCurrentFrame() const noexcept;
        [[nodiscard]] ImGuiOverlayPushConstants BuildPushConstants(
            std::uint64_t vertexBufferBDA = 0u,
            std::uint32_t firstVertex = 0u,
            std::uint32_t indexCount = 0u,
            RHI::BindlessIndex textureBindlessIndex = RHI::kInvalidBindlessIndex,
            std::uint32_t flags = 0u) const noexcept;
        [[nodiscard]] std::string FormatDiagnostics() const;
        [[nodiscard]] PresentFinalizationDiagnostics ValidatePresentFinalization(
            const PresentFinalizationInputs& inputs) const noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
