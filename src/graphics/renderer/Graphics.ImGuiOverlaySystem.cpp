module;

#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

module Extrinsic.Graphics.ImGuiOverlaySystem;

namespace Extrinsic::Graphics
{
    struct ImGuiOverlaySystem::Impl
    {
        bool Initialized{false};
        bool HasWork{false};
        bool FontAtlasDirty{false};
        RHI::IDevice* Device{nullptr};
        RHI::TextureManager* TextureManager{nullptr};
        RHI::SamplerManager* SamplerManager{nullptr};
        std::optional<RHI::TextureManager::TextureLease> FontAtlasTexture{};
        std::optional<RHI::SamplerManager::SamplerLease> FontAtlasSampler{};
        RHI::TextureDesc FontAtlasDesc{};
        std::uint32_t FontAtlasAllocationCount{0u};
        std::uint32_t FontAtlasUploadCount{0u};
        std::uint32_t FontAtlasRetainCount{0u};
        ImGuiOverlayDiagnostics Diagnostics{};
        ImGuiOverlayFrame Frame{};
    };

    namespace
    {
        [[nodiscard]] bool FontAtlasPayloadValid(const ImGuiOverlayFontAtlas& atlas) noexcept
        {
            if (!atlas.Valid || atlas.Width == 0u || atlas.Height == 0u ||
                (atlas.BytesPerPixel != 1u && atlas.BytesPerPixel != 4u))
            {
                return false;
            }

            const std::uint64_t expectedBytes =
                static_cast<std::uint64_t>(atlas.Width) *
                static_cast<std::uint64_t>(atlas.Height) *
                static_cast<std::uint64_t>(atlas.BytesPerPixel);
            return expectedBytes > 0u &&
                   expectedBytes == static_cast<std::uint64_t>(atlas.Pixels.size());
        }

        [[nodiscard]] bool FontAtlasMetadataValid(const ImGuiOverlayFontAtlas& atlas) noexcept
        {
            return atlas.Valid && atlas.Width > 0u && atlas.Height > 0u &&
                   (atlas.BytesPerPixel == 1u || atlas.BytesPerPixel == 4u);
        }

        [[nodiscard]] bool FontAtlasMetadataCompatible(
            const ImGuiOverlayFontAtlas& lhs,
            const ImGuiOverlayFontAtlas& rhs) noexcept
        {
            return lhs.Valid == rhs.Valid &&
                   lhs.Width == rhs.Width &&
                   lhs.Height == rhs.Height &&
                   lhs.BytesPerPixel == rhs.BytesPerPixel &&
                   lhs.UseColors == rhs.UseColors;
        }

        [[nodiscard]] RHI::SamplerDesc BuildFontAtlasSamplerDesc() noexcept
        {
            return RHI::SamplerDesc{
                .MagFilter = RHI::FilterMode::Linear,
                .MinFilter = RHI::FilterMode::Linear,
                .MipFilter = RHI::MipmapMode::Nearest,
                .AddressU = RHI::AddressMode::ClampToEdge,
                .AddressV = RHI::AddressMode::ClampToEdge,
                .AddressW = RHI::AddressMode::ClampToEdge,
                .DebugName = "ImGui.FontAtlasSampler",
            };
        }

        [[nodiscard]] RHI::TextureDesc BuildFontAtlasTextureDesc(
            const ImGuiOverlayFontAtlas& atlas) noexcept
        {
            const bool validPayload = FontAtlasPayloadValid(atlas);
            return RHI::TextureDesc{
                .Width = validPayload ? atlas.Width : 4096u,
                .Height = validPayload ? atlas.Height : 4096u,
                .DepthOrArrayLayers = 1u,
                .MipLevels = 1u,
                .Fmt = (validPayload && atlas.BytesPerPixel == 4u)
                    ? RHI::Format::RGBA8_UNORM
                    : RHI::Format::R8_UNORM,
                .Dimension = RHI::TextureDimension::Tex2D,
                .Usage = RHI::TextureUsage::Sampled | RHI::TextureUsage::TransferDst,
                .InitialLayout = RHI::TextureLayout::Undefined,
                .SampleCount = 1u,
                .DebugName = "ImGui.FontAtlas",
            };
        }

        [[nodiscard]] bool FontAtlasDescCompatible(const RHI::TextureDesc& lhs,
                                                   const RHI::TextureDesc& rhs) noexcept
        {
            return lhs.Width == rhs.Width &&
                   lhs.Height == rhs.Height &&
                   lhs.DepthOrArrayLayers == rhs.DepthOrArrayLayers &&
                   lhs.MipLevels == rhs.MipLevels &&
                   lhs.Fmt == rhs.Fmt &&
                   lhs.Dimension == rhs.Dimension &&
                   lhs.Usage == rhs.Usage &&
                   lhs.SampleCount == rhs.SampleCount;
        }

        [[nodiscard]] bool DrawCommandsValid(const ImGuiOverlayDrawList& drawList) noexcept
        {
            if (drawList.Commands.empty())
            {
                return drawList.CommandCount > 0u;
            }

            for (const ImGuiOverlayDrawCommand& command : drawList.Commands)
            {
                if (command.IndexCount == 0u ||
                    command.IndexOffset > drawList.IndexCount ||
                    command.IndexCount > drawList.IndexCount - command.IndexOffset ||
                    command.VertexOffset >= drawList.VertexCount)
                {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] bool DrawListUsesUserTexture(const ImGuiOverlayDrawList& drawList) noexcept
        {
            if (drawList.UsesUserTexture)
            {
                return true;
            }

            for (const ImGuiOverlayDrawCommand& command : drawList.Commands)
            {
                if (command.UsesUserTexture)
                {
                    return true;
                }
            }
            return false;
        }

        template <typename TImpl>
        void RefreshFontAtlasDiagnostics(TImpl& impl) noexcept
        {
            const ImGuiOverlayFontAtlas& atlas = impl.Frame.FontAtlas;
            impl.Diagnostics.FontAtlasAvailable = FontAtlasPayloadValid(atlas);
            impl.Diagnostics.FontAtlasWidth = atlas.Width;
            impl.Diagnostics.FontAtlasHeight = atlas.Height;
            impl.Diagnostics.FontAtlasBytesPerPixel = atlas.BytesPerPixel;
            impl.Diagnostics.FontAtlasByteCount = static_cast<std::uint64_t>(atlas.Pixels.size());
            impl.Diagnostics.FontAtlasRevision = atlas.Revision;
            impl.Diagnostics.FontAtlasRetainCount = impl.FontAtlasRetainCount;
            impl.Diagnostics.FontAtlasAllocationCount = impl.FontAtlasAllocationCount;
            impl.Diagnostics.FontAtlasUploadCount = impl.FontAtlasUploadCount;
            impl.Diagnostics.FontAtlasGpuAllocated = impl.FontAtlasTexture.has_value() &&
                                                     impl.FontAtlasTexture->GetHandle().IsValid();
            impl.Diagnostics.FontAtlasTexture = impl.Diagnostics.FontAtlasGpuAllocated
                ? impl.FontAtlasTexture->GetHandle()
                : RHI::TextureHandle{};
            impl.Diagnostics.FontAtlasBindlessIndex =
                (impl.TextureManager != nullptr && impl.Diagnostics.FontAtlasGpuAllocated)
                    ? impl.TextureManager->GetBindlessIndex(impl.FontAtlasTexture->GetHandle())
                    : RHI::kInvalidBindlessIndex;
        }

        template <typename TImpl>
        [[nodiscard]] bool EnsureFontAtlasTexture(TImpl& impl)
        {
            if (impl.Device == nullptr || impl.TextureManager == nullptr ||
                impl.SamplerManager == nullptr || !impl.Device->IsOperational())
            {
                return false;
            }

            if (!impl.FontAtlasSampler.has_value() ||
                !impl.FontAtlasSampler->GetHandle().IsValid())
            {
                auto sampler = impl.SamplerManager->GetOrCreate(BuildFontAtlasSamplerDesc());
                if (!sampler.has_value())
                {
                    impl.Diagnostics.FontAtlasAllocationFailed = true;
                    return false;
                }
                impl.FontAtlasSampler.emplace(std::move(*sampler));
            }

            const RHI::TextureDesc desc = BuildFontAtlasTextureDesc(impl.Frame.FontAtlas);
            if (impl.FontAtlasTexture.has_value() &&
                impl.FontAtlasTexture->GetHandle().IsValid() &&
                FontAtlasDescCompatible(impl.FontAtlasDesc, desc))
            {
                return true;
            }

            impl.FontAtlasTexture.reset();
            impl.FontAtlasDesc = {};

            auto texture = impl.TextureManager->Create(desc, impl.FontAtlasSampler->GetHandle());
            if (!texture.has_value())
            {
                impl.Diagnostics.FontAtlasAllocationFailed = true;
                RefreshFontAtlasDiagnostics(impl);
                return false;
            }

            impl.FontAtlasTexture.emplace(std::move(*texture));
            impl.FontAtlasDesc = desc;
            ++impl.FontAtlasAllocationCount;
            RefreshFontAtlasDiagnostics(impl);
            return true;
        }
    }

    ImGuiOverlaySystem::ImGuiOverlaySystem()
        : m_Impl(std::make_unique<Impl>())
    {}

    ImGuiOverlaySystem::~ImGuiOverlaySystem() = default;

    void ImGuiOverlaySystem::Initialize()
    {
        m_Impl->Initialized = true;
    }

    void ImGuiOverlaySystem::InitializeGpuResources(RHI::IDevice& device,
                                                    RHI::TextureManager& textureManager,
                                                    RHI::SamplerManager& samplerManager)
    {
        m_Impl->Device = &device;
        m_Impl->TextureManager = &textureManager;
        m_Impl->SamplerManager = &samplerManager;
        (void)EnsureFontAtlasTexture(*m_Impl);
        RefreshFontAtlasDiagnostics(*m_Impl);
    }

    void ImGuiOverlaySystem::Shutdown()
    {
        m_Impl->Initialized = false;
        ClearFrame();
    }

    void ImGuiOverlaySystem::ShutdownGpuResources()
    {
        m_Impl->FontAtlasTexture.reset();
        m_Impl->FontAtlasSampler.reset();
        m_Impl->FontAtlasDesc = {};
        m_Impl->Device = nullptr;
        m_Impl->TextureManager = nullptr;
        m_Impl->SamplerManager = nullptr;
        m_Impl->FontAtlasDirty = false;
        RefreshFontAtlasDiagnostics(*m_Impl);
    }

    void ImGuiOverlaySystem::UploadPendingFontAtlas()
    {
        if (!m_Impl->FontAtlasDirty || !FontAtlasPayloadValid(m_Impl->Frame.FontAtlas))
        {
            return;
        }

        if (!EnsureFontAtlasTexture(*m_Impl) || !m_Impl->FontAtlasTexture.has_value() ||
            m_Impl->Device == nullptr)
        {
            m_Impl->Diagnostics.FontAtlasUploadFailed = true;
            return;
        }

        const ImGuiOverlayFontAtlas& atlas = m_Impl->Frame.FontAtlas;
        m_Impl->Device->WriteTexture(m_Impl->FontAtlasTexture->GetHandle(),
                                     atlas.Pixels.data(),
                                     static_cast<std::uint64_t>(atlas.Pixels.size()),
                                     0u,
                                     0u);
        m_Impl->FontAtlasDirty = false;
        ++m_Impl->FontAtlasUploadCount;
        RefreshFontAtlasDiagnostics(*m_Impl);
        m_Impl->Diagnostics.FontAtlasUploadQueued = true;
    }

    void ImGuiOverlaySystem::SubmitFrame(const ImGuiOverlayFrame& frame)
    {
        SubmitFrame(ImGuiOverlayFrame(frame));
    }

    void ImGuiOverlaySystem::SubmitFrame(ImGuiOverlayFrame&& frame)
    {
        m_Impl->Diagnostics = {};
        m_Impl->HasWork = false;
        ImGuiOverlayFrame acceptedFrame{};
        acceptedFrame.Enabled = frame.Enabled;
        acceptedFrame.DisplayWidth = frame.DisplayWidth;
        acceptedFrame.DisplayHeight = frame.DisplayHeight;
        m_Impl->Diagnostics.Enabled = frame.Enabled;
        m_Impl->Diagnostics.SubmittedDrawListCount = static_cast<std::uint32_t>(frame.DrawLists.size());

        const bool incomingPayloadValid = FontAtlasPayloadValid(frame.FontAtlas);
        const bool incomingMetadataValid = FontAtlasMetadataValid(frame.FontAtlas);
        const bool previousPayloadValid = FontAtlasPayloadValid(m_Impl->Frame.FontAtlas);
        bool fontAtlasChanged = false;
        bool fontAtlasRetained = false;
        if (incomingPayloadValid)
        {
            fontAtlasChanged =
                frame.FontAtlas.Dirty ||
                !previousPayloadValid ||
                !FontAtlasMetadataCompatible(m_Impl->Frame.FontAtlas, frame.FontAtlas) ||
                m_Impl->Frame.FontAtlas.Revision != frame.FontAtlas.Revision ||
                m_Impl->Frame.FontAtlas.Pixels != frame.FontAtlas.Pixels;
            acceptedFrame.FontAtlas = std::move(frame.FontAtlas);
        }
        else if (incomingMetadataValid && !frame.FontAtlas.Dirty &&
                 previousPayloadValid &&
                 FontAtlasMetadataCompatible(m_Impl->Frame.FontAtlas, frame.FontAtlas) &&
                 (frame.FontAtlas.Revision == 0u ||
                  frame.FontAtlas.Revision == m_Impl->Frame.FontAtlas.Revision))
        {
            acceptedFrame.FontAtlas = std::move(m_Impl->Frame.FontAtlas);
            acceptedFrame.FontAtlas.Dirty = false;
            acceptedFrame.FontAtlas.Revision = frame.FontAtlas.Revision;
            fontAtlasRetained = true;
            ++m_Impl->FontAtlasRetainCount;
        }
        else
        {
            acceptedFrame.FontAtlas = std::move(frame.FontAtlas);
        }

        if (!frame.Enabled)
        {
            m_Impl->Frame = std::move(acceptedFrame);
            RefreshFontAtlasDiagnostics(*m_Impl);
            m_Impl->Diagnostics.FontAtlasRetained = fontAtlasRetained;
            return;
        }

        if (frame.DisplayWidth == 0u || frame.DisplayHeight == 0u)
        {
            m_Impl->Diagnostics.InvalidDisplaySize = true;
            m_Impl->Frame = std::move(acceptedFrame);
            RefreshFontAtlasDiagnostics(*m_Impl);
            m_Impl->Diagnostics.FontAtlasRetained = fontAtlasRetained;
            return;
        }

        for (ImGuiOverlayDrawList& drawList : frame.DrawLists)
        {
            if (drawList.VertexCount == 0u || drawList.IndexCount == 0u ||
                !DrawCommandsValid(drawList))
            {
                ++m_Impl->Diagnostics.RejectedDrawListCount;
                continue;
            }

            ImGuiOverlayDrawList acceptedList = std::move(drawList);
            if (!acceptedList.Commands.empty())
            {
                acceptedList.CommandCount = static_cast<std::uint32_t>(acceptedList.Commands.size());
                acceptedList.UsesUserTexture = DrawListUsesUserTexture(acceptedList);
            }

            ++m_Impl->Diagnostics.AcceptedDrawListCount;
            m_Impl->Diagnostics.DrawCommandCount += acceptedList.CommandCount;
            m_Impl->Diagnostics.VertexCount += acceptedList.VertexCount;
            m_Impl->Diagnostics.IndexCount += acceptedList.IndexCount;
            m_Impl->Diagnostics.HasUserTextures =
                m_Impl->Diagnostics.HasUserTextures || acceptedList.UsesUserTexture;
            acceptedFrame.DrawLists.push_back(std::move(acceptedList));
        }

        m_Impl->HasWork = m_Impl->Diagnostics.DrawCommandCount > 0u && m_Impl->Diagnostics.IndexCount > 0u;
        m_Impl->Frame = std::move(acceptedFrame);
        if (fontAtlasChanged)
        {
            m_Impl->FontAtlasDirty = true;
        }
        RefreshFontAtlasDiagnostics(*m_Impl);
        m_Impl->Diagnostics.FontAtlasRetained = fontAtlasRetained;
    }

    void ImGuiOverlaySystem::ClearFrame()
    {
        m_Impl->HasWork = false;
        m_Impl->Diagnostics = {};
        m_Impl->Frame = {};
        RefreshFontAtlasDiagnostics(*m_Impl);
    }

    void ImGuiOverlaySystem::RecordDrawCalls(const std::uint32_t drawCalls) noexcept
    {
        m_Impl->Diagnostics.DrawCalls += drawCalls;
    }

    bool ImGuiOverlaySystem::IsInitialized() const noexcept
    {
        return m_Impl->Initialized;
    }

    bool ImGuiOverlaySystem::HasOverlayWork() const noexcept
    {
        return m_Impl->Initialized && m_Impl->HasWork;
    }

    ImGuiOverlayDiagnostics ImGuiOverlaySystem::GetDiagnostics() const noexcept
    {
        return m_Impl->Diagnostics;
    }

    const ImGuiOverlayFrame* ImGuiOverlaySystem::GetCurrentFrame() const noexcept
    {
        return &m_Impl->Frame;
    }

    ImGuiOverlayPushConstants ImGuiOverlaySystem::BuildPushConstants(
        const std::uint64_t vertexBufferBDA,
        const std::uint32_t firstVertex,
        const std::uint32_t indexCount,
        const RHI::BindlessIndex textureBindlessIndex,
        const std::uint32_t flags) const noexcept
    {
        const RHI::BindlessIndex selectedTexture =
            textureBindlessIndex != RHI::kInvalidBindlessIndex
                ? textureBindlessIndex
                : m_Impl->Diagnostics.FontAtlasBindlessIndex;
        const std::uint32_t atlasFlags =
            m_Impl->Frame.FontAtlas.UseColors ? kImGuiOverlayPushFlagFontAtlasColor : 0u;
        return ImGuiOverlayPushConstants{
            .VertexBufferBDA = vertexBufferBDA,
            .FirstVertex = firstVertex,
            .IndexCount = indexCount == 0u ? m_Impl->Diagnostics.IndexCount : indexCount,
            .FontAtlasBindlessIndex = m_Impl->Diagnostics.FontAtlasBindlessIndex,
            .TextureBindlessIndex = selectedTexture,
            .Flags = flags | atlasFlags,
            .Scale = {
                m_Impl->Frame.DisplayWidth > 0u ? 2.0f / static_cast<float>(m_Impl->Frame.DisplayWidth) : 1.0f,
                m_Impl->Frame.DisplayHeight > 0u ? 2.0f / static_cast<float>(m_Impl->Frame.DisplayHeight) : 1.0f,
            },
            .Translate = {-1.0f, -1.0f},
        };
    }

    std::string ImGuiOverlaySystem::FormatDiagnostics() const
    {
        std::ostringstream out;
        out << "imgui-overlay: enabled=" << (m_Impl->Diagnostics.Enabled ? "true" : "false")
            << " accepted_lists=" << m_Impl->Diagnostics.AcceptedDrawListCount
            << " rejected_lists=" << m_Impl->Diagnostics.RejectedDrawListCount
            << " commands=" << m_Impl->Diagnostics.DrawCommandCount
            << " vertices=" << m_Impl->Diagnostics.VertexCount
            << " indices=" << m_Impl->Diagnostics.IndexCount
            << " draws=" << m_Impl->Diagnostics.DrawCalls
            << " invalid_display=" << (m_Impl->Diagnostics.InvalidDisplaySize ? "true" : "false")
            << " user_textures=" << (m_Impl->Diagnostics.HasUserTextures ? "true" : "false")
            << " font_atlas=" << (m_Impl->Diagnostics.FontAtlasAvailable ? "true" : "false")
            << " font_atlas_gpu=" << (m_Impl->Diagnostics.FontAtlasGpuAllocated ? "true" : "false")
            << " font_atlas_uploads=" << m_Impl->Diagnostics.FontAtlasUploadCount;
        return out.str();
    }

    PresentFinalizationDiagnostics ImGuiOverlaySystem::ValidatePresentFinalization(
        const PresentFinalizationInputs& inputs) const noexcept
    {
        PresentFinalizationDiagnostics diagnostics{};
        diagnostics.MissingPresentationSource = !inputs.PresentationSourceAvailable;
        diagnostics.MissingBackbuffer = !inputs.BackbufferImported;
        diagnostics.PresentPassDisabled = !inputs.PresentPassEnabled;
        diagnostics.CanFinalize = !diagnostics.MissingPresentationSource &&
                                  !diagnostics.MissingBackbuffer &&
                                  !diagnostics.PresentPassDisabled;
        return diagnostics;
    }
}
