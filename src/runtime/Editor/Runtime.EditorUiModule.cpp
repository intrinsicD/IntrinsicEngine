module;

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string_view>

module Extrinsic.Runtime.EditorUiModule;

import Extrinsic.Core.Error;
import Extrinsic.Graphics.ImGuiOverlaySystem;
import Extrinsic.Graphics.Renderer;
import Extrinsic.Platform.Input;
import Extrinsic.Platform.Window;
import Extrinsic.Runtime.FramePacingDiagnostics;
import Extrinsic.Runtime.ImGuiAdapter;
import Extrinsic.Runtime.InputActions;
import Extrinsic.Runtime.Module;
import Extrinsic.Runtime.ServiceRegistry;

namespace Extrinsic::Runtime
{
    namespace
    {
        [[nodiscard]] std::uint64_t ElapsedMicros(
            const std::chrono::steady_clock::time_point start) noexcept
        {
            return static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - start).count());
        }

        void MirrorImGuiDiagnostics(
            RuntimeFramePacingDiagnostics& pacing,
            const ImGuiAdapterDiagnostics& imgui) noexcept
        {
            pacing.ImGuiEditorCallbackMicros =
                imgui.LastEditorCallbackMicros;
            pacing.ImGuiDrawDataCopyMicros =
                imgui.LastDrawDataCopyMicros;
            pacing.ImGuiDrawListCount = imgui.LastDrawListCount;
            pacing.ImGuiVertexCount = imgui.LastVertexCount;
            pacing.ImGuiIndexCount = imgui.LastIndexCount;
            pacing.ImGuiCommandCount = imgui.LastCommandCount;
            pacing.ImGuiFontAtlasCopyCount = imgui.FontAtlasCopyCount;
            pacing.ImGuiFontAtlasReuseCount = imgui.FontAtlasReuseCount;
            pacing.ImGuiFontAtlasCopied =
                imgui.LastFrameFontAtlasCopied;
            pacing.ImGuiFrameUsedUserTexture =
                imgui.LastFrameUsedUserTexture;
            pacing.ImGuiFontAtlasByteCount =
                imgui.LastFontAtlasByteCount;
            pacing.ImGuiFontAtlasCopyBytes =
                imgui.LastFrameFontAtlasCopyBytes;
            pacing.ImGuiVertexCopyBytes =
                imgui.LastFrameVertexCopyBytes;
            pacing.ImGuiIndexCopyBytes =
                imgui.LastFrameIndexCopyBytes;
            pacing.ImGuiCommandCopyBytes =
                imgui.LastFrameCommandCopyBytes;
            pacing.ImGuiOverlayCopyBytes =
                imgui.LastFrameOverlayCopyBytes;
        }

        [[nodiscard]] EditorUiDiagnostics CopyEditorUiDiagnostics(
            const ImGuiAdapter& adapter) noexcept
        {
            const ImGuiAdapterDiagnostics& imgui =
                adapter.GetDiagnostics();
            return EditorUiDiagnostics{
                .Initialized = imgui.Initialized,
                .FramesProduced = imgui.FramesProduced,
                .LastDrawListCount = imgui.LastDrawListCount,
                .LastVertexCount = imgui.LastVertexCount,
                .LastIndexCount = imgui.LastIndexCount,
                .LastCommandCount = imgui.LastCommandCount,
                .LastFrameUsedUserTexture =
                    imgui.LastFrameUsedUserTexture,
                .CapturesViewportInput =
                    adapter.CaptureSnapshot().CapturesViewportInput(),
                .PumpedEventCount = imgui.PumpedEventCount,
                .ContextRebuilds = imgui.ContextRebuilds,
                .EditorCallbackInvocations =
                    imgui.EditorCallbackInvocations,
                .CaptureSnapshots = imgui.CaptureSnapshots,
                .DisplayWidth = imgui.DisplayWidth,
                .DisplayHeight = imgui.DisplayHeight,
                .FontAtlasCopyCount = imgui.FontAtlasCopyCount,
                .FontAtlasReuseCount = imgui.FontAtlasReuseCount,
                .LastFontAtlasByteCount =
                    imgui.LastFontAtlasByteCount,
                .LastFrameFontAtlasCopyBytes =
                    imgui.LastFrameFontAtlasCopyBytes,
                .LastFrameVertexCopyBytes =
                    imgui.LastFrameVertexCopyBytes,
                .LastFrameIndexCopyBytes =
                    imgui.LastFrameIndexCopyBytes,
                .LastFrameCommandCopyBytes =
                    imgui.LastFrameCommandCopyBytes,
                .LastFrameOverlayCopyBytes =
                    imgui.LastFrameOverlayCopyBytes,
                .LastFrameFontAtlasCopied =
                    imgui.LastFrameFontAtlasCopied,
                .LastBeginFrameMicros =
                    imgui.LastBeginFrameMicros,
                .LastEditorCallbackMicros =
                    imgui.LastEditorCallbackMicros,
                .LastImGuiRenderMicros =
                    imgui.LastImGuiRenderMicros,
                .LastDrawDataCopyMicros =
                    imgui.LastDrawDataCopyMicros,
                .LastEndFrameMicros = imgui.LastEndFrameMicros,
            };
        }
    }

    struct EditorUiModule::Impl
    {
        Graphics::ImGuiOverlaySystem Overlay{};
        EditorUiHost Host{};
        EditorUiHostOwnerControl HostOwner{
            Host.ClaimOwnerControl()};
        std::unique_ptr<ImGuiAdapter> Adapter{};
        Platform::IWindow* Window{nullptr};
        Graphics::IRenderer* Renderer{nullptr};
        RuntimeInputActionRegistry* InputActions{nullptr};
        RuntimeInputActionHandle VisibilityAction{};
        bool HostPublished{false};
    };

    EditorUiModule::EditorUiModule() = default;
    EditorUiModule::~EditorUiModule() = default;

    std::string_view EditorUiModule::Name() const noexcept
    {
        return "Runtime.EditorUiModule";
    }

    Core::Result EditorUiModule::OnRegister(EngineSetup& setup)
    {
        if (m_Impl ||
            setup.Services().Phase() !=
                ServiceRegistryPhase::Registration ||
            setup.Services().Find<EditorUiHost>() != nullptr)
        {
            return Core::Err(Core::ErrorCode::InvalidState);
        }

        m_Impl = std::make_unique<Impl>();
        if (Core::Result provided =
                setup.Services().Provide<EditorUiHost>(
                    m_Impl->Host, Name());
            !provided.has_value())
        {
            m_Impl.reset();
            return provided;
        }
        m_Impl->HostPublished = true;

        if (Core::Result registered = setup.RegisterFrameHook(
                FramePhase::UiBegin,
                [this](RuntimeFrameHookContext& context)
                {
                    RunUiBegin(context);
                });
            !registered.has_value())
        {
            ShutdownAndReset(&setup.Services());
            return registered;
        }
        if (Core::Result registered = setup.RegisterFrameHook(
                FramePhase::UiBuild,
                [this](RuntimeFrameHookContext& context)
                {
                    RunUiBuild(context);
                });
            !registered.has_value())
        {
            ShutdownAndReset(&setup.Services());
            return registered;
        }
        if (Core::Result registered = setup.RegisterFrameHook(
                FramePhase::UiEndCapture,
                [this](RuntimeFrameHookContext& context)
                {
                    RunUiEndCapture(context);
                });
            !registered.has_value())
        {
            ShutdownAndReset(&setup.Services());
            return registered;
        }
        return Core::Ok();
    }

    Core::Result EditorUiModule::OnResolve(EngineSetup& setup)
    {
        if (!m_Impl || !m_Impl->HostPublished ||
            setup.Services().Find<EditorUiHost>() != &m_Impl->Host)
        {
            return Core::Err(Core::ErrorCode::InvalidState);
        }

        auto window = setup.Services().Require<Platform::IWindow>(Name());
        auto renderer =
            setup.Services().Require<Graphics::IRenderer>(Name());
        auto inputActions =
            setup.Services().Require<RuntimeInputActionRegistry>(Name());
        if (!window.has_value() ||
            !renderer.has_value() ||
            !inputActions.has_value())
        {
            ShutdownAndReset(&setup.Services());
            return Core::Err(Core::ErrorCode::ResourceNotFound);
        }

        m_Impl->Window = &window->get();
        m_Impl->Renderer = &renderer->get();
        m_Impl->InputActions = &inputActions->get();
        m_Impl->Adapter = std::make_unique<ImGuiAdapter>(
            *m_Impl->Window, m_Impl->Overlay);
        if (!m_Impl->Adapter->Initialize())
        {
            ShutdownAndReset(&setup.Services());
            return Core::Err(Core::ErrorCode::InvalidState);
        }

        m_Impl->Adapter->SetEditorCallback(
            [this]
            {
                if (m_Impl)
                {
                    (void)m_Impl->HostOwner
                        .DrawFrameContributions();
                }
            });
        m_Impl->HostOwner.SetVisibilityChangedCallback(
            [this](const bool visible)
            {
                if (m_Impl && m_Impl->Adapter)
                {
                    m_Impl->Adapter->SetEditorVisible(visible);
                    m_Impl->HostOwner.PublishDiagnostics(
                        CopyEditorUiDiagnostics(
                            *m_Impl->Adapter));
                }
            });
        m_Impl->Adapter->SetEditorVisible(m_Impl->Host.IsVisible());
        m_Impl->Renderer->SetImGuiOverlaySystem(&m_Impl->Overlay);

        m_Impl->VisibilityAction = m_Impl->InputActions->Register(
            RuntimeInputActionDesc{
                .DebugName = "EditorUi.ToggleVisibility",
                .Binding =
                    RuntimeInputActionBinding{
                        .KeyCode = Platform::Input::Key::G,
                        .Trigger =
                            RuntimeInputActionTrigger::KeyJustPressed,
                        .SuppressWhenImGuiCapturesKeyboard = false,
                    },
                .Execute =
                    [this](const RuntimeInputActionContext&,
                           RuntimeInputActionServices&) -> Core::Result
                    {
                        if (!m_Impl)
                            return Core::Err(
                                Core::ErrorCode::InvalidState);
                        (void)m_Impl->Host.ApplyVisibilityCommand(
                            EditorUiVisibilityCommand{
                                EditorUiVisibilityCommandKind::Toggle});
                        return Core::Ok();
                    },
            });
        if (!m_Impl->VisibilityAction.IsValid())
        {
            ShutdownAndReset(&setup.Services());
            return Core::Err(Core::ErrorCode::InvalidState);
        }

        m_Impl->HostOwner.SetOperational(true);
        m_Impl->HostOwner.PublishDiagnostics(
            CopyEditorUiDiagnostics(*m_Impl->Adapter));
        return Core::Ok();
    }

    void EditorUiModule::OnShutdown(
        RuntimeModuleShutdownContext& context)
    {
        ShutdownAndReset(&context.Services);
    }

    void EditorUiModule::RunUiBegin(
        RuntimeFrameHookContext& context)
    {
        if (!m_Impl || !m_Impl->Adapter)
            return;

        m_Impl->Adapter->SetEditorVisible(m_Impl->Host.IsVisible());
        const auto begin = std::chrono::steady_clock::now();
        m_Impl->Adapter->BeginFrame(context.FrameDeltaSeconds);
        context.Pacing.ImGuiBeginMicros = ElapsedMicros(begin);
    }

    void EditorUiModule::RunUiBuild(
        RuntimeFrameHookContext&)
    {
        if (m_Impl && m_Impl->Adapter)
            m_Impl->Adapter->BuildEditorFrame();
    }

    void EditorUiModule::RunUiEndCapture(
        RuntimeFrameHookContext& context)
    {
        if (!m_Impl || !m_Impl->Adapter)
            return;

        m_Impl->Adapter->SetEditorVisible(m_Impl->Host.IsVisible());
        const auto end = std::chrono::steady_clock::now();
        m_Impl->Adapter->EndFrame();
        context.Pacing.ImGuiEndMicros = ElapsedMicros(end);
        context.EditorCapture = m_Impl->Adapter->CaptureSnapshot();

        const ImGuiAdapterDiagnostics& diagnostics =
            m_Impl->Adapter->GetDiagnostics();
        MirrorImGuiDiagnostics(context.Pacing, diagnostics);
        m_Impl->HostOwner.PublishDiagnostics(
            CopyEditorUiDiagnostics(*m_Impl->Adapter));
    }

    void EditorUiModule::ShutdownAndReset(
        ServiceRegistry* const services) noexcept
    {
        if (!m_Impl)
            return;

        m_Impl->HostOwner.SetOperational(false);
        m_Impl->HostOwner.SetVisibilityChangedCallback({});
        if (m_Impl->InputActions &&
            m_Impl->VisibilityAction.IsValid())
        {
            m_Impl->InputActions->Unregister(
                m_Impl->VisibilityAction);
        }
        m_Impl->VisibilityAction = {};
        if (m_Impl->Adapter)
            m_Impl->Adapter->SetEditorCallback({});
        if (m_Impl->Renderer)
            m_Impl->Renderer->SetImGuiOverlaySystem(nullptr);
        m_Impl->Adapter.reset();

        if (services && m_Impl->HostPublished)
            (void)services->Withdraw<EditorUiHost>(m_Impl->Host);
        m_Impl->HostPublished = false;
        m_Impl.reset();
    }
}
