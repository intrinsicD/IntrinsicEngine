module;

#include <memory>
#include <sstream>
#include <string>

module Extrinsic.Graphics.ImGuiOverlaySystem;

namespace Extrinsic::Graphics
{
    struct ImGuiOverlaySystem::Impl
    {
        bool Initialized{false};
        bool HasWork{false};
        ImGuiOverlayDiagnostics Diagnostics{};
    };

    ImGuiOverlaySystem::ImGuiOverlaySystem()
        : m_Impl(std::make_unique<Impl>())
    {}

    ImGuiOverlaySystem::~ImGuiOverlaySystem() = default;

    void ImGuiOverlaySystem::Initialize()
    {
        m_Impl->Initialized = true;
    }

    void ImGuiOverlaySystem::Shutdown()
    {
        m_Impl->Initialized = false;
        ClearFrame();
    }

    void ImGuiOverlaySystem::SubmitFrame(const ImGuiOverlayFrame& frame)
    {
        m_Impl->Diagnostics = {};
        m_Impl->HasWork = false;
        m_Impl->Diagnostics.Enabled = frame.Enabled;
        m_Impl->Diagnostics.SubmittedDrawListCount = static_cast<std::uint32_t>(frame.DrawLists.size());

        if (!frame.Enabled)
        {
            return;
        }

        if (frame.DisplayWidth == 0u || frame.DisplayHeight == 0u)
        {
            m_Impl->Diagnostics.InvalidDisplaySize = true;
            return;
        }

        for (const ImGuiOverlayDrawList& drawList : frame.DrawLists)
        {
            if (drawList.CommandCount == 0u || drawList.VertexCount == 0u || drawList.IndexCount == 0u)
            {
                ++m_Impl->Diagnostics.RejectedDrawListCount;
                continue;
            }

            ++m_Impl->Diagnostics.AcceptedDrawListCount;
            m_Impl->Diagnostics.DrawCommandCount += drawList.CommandCount;
            m_Impl->Diagnostics.VertexCount += drawList.VertexCount;
            m_Impl->Diagnostics.IndexCount += drawList.IndexCount;
            m_Impl->Diagnostics.HasUserTextures = m_Impl->Diagnostics.HasUserTextures || drawList.UsesUserTexture;
        }

        m_Impl->HasWork = m_Impl->Diagnostics.DrawCommandCount > 0u && m_Impl->Diagnostics.IndexCount > 0u;
    }

    void ImGuiOverlaySystem::ClearFrame()
    {
        m_Impl->HasWork = false;
        m_Impl->Diagnostics = {};
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

    ImGuiOverlayPushConstants ImGuiOverlaySystem::BuildPushConstants() const noexcept
    {
        return ImGuiOverlayPushConstants{
            .DrawCommandCount = m_Impl->Diagnostics.DrawCommandCount,
            .VertexCount = m_Impl->Diagnostics.VertexCount,
            .IndexCount = m_Impl->Diagnostics.IndexCount,
            .Flags = m_Impl->Diagnostics.HasUserTextures ? 1u : 0u,
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
            << " invalid_display=" << (m_Impl->Diagnostics.InvalidDisplaySize ? "true" : "false")
            << " user_textures=" << (m_Impl->Diagnostics.HasUserTextures ? "true" : "false");
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

