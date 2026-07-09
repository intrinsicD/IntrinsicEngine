module;

#include <functional>
#include <memory>
#include <utility>

module Extrinsic.Runtime.ImGuiEditorBridge;

namespace Extrinsic::Runtime
{
    ImGuiEditorBridge::~ImGuiEditorBridge() = default;

    void ImGuiEditorBridge::Initialize(
        Platform::IWindow& window,
        Graphics::IRenderer& renderer)
    {
        if (m_Adapter)
            return;

        m_Adapter = std::make_unique<ImGuiAdapter>(window, m_Overlay);
        m_Adapter->Initialize();
        if (m_EditorCallback)
            m_Adapter->SetEditorCallback(m_EditorCallback);
        renderer.SetImGuiOverlaySystem(&m_Overlay);
    }

    void ImGuiEditorBridge::Shutdown(Graphics::IRenderer* renderer) noexcept
    {
        if (renderer)
            renderer->SetImGuiOverlaySystem(nullptr);
        m_Adapter.reset();
    }

    void ImGuiEditorBridge::SetEditorCallback(std::function<void()> callback)
    {
        m_EditorCallback = std::move(callback);
        if (m_Adapter)
            m_Adapter->SetEditorCallback(m_EditorCallback);
    }

    void ImGuiEditorBridge::BeginFrame(const double deltaSeconds)
    {
        if (m_Adapter)
            m_Adapter->BeginFrame(deltaSeconds);
    }

    void ImGuiEditorBridge::EndFrame()
    {
        if (m_Adapter)
            m_Adapter->EndFrame();
    }

    bool ImGuiEditorBridge::IsInitialized() const noexcept
    {
        return m_Adapter != nullptr && m_Adapter->IsInitialized();
    }

    bool ImGuiEditorBridge::WantsMouseCapture() const noexcept
    {
        return m_Adapter != nullptr && m_Adapter->WantsMouseCapture();
    }

    bool ImGuiEditorBridge::WantsKeyboardCapture() const noexcept
    {
        return m_Adapter != nullptr && m_Adapter->WantsKeyboardCapture();
    }

    const ImGuiAdapterDiagnostics*
    ImGuiEditorBridge::Diagnostics() const noexcept
    {
        if (!m_Adapter)
            return nullptr;
        return &m_Adapter->GetDiagnostics();
    }

    const ImGuiAdapter& ImGuiEditorBridge::Adapter() const noexcept
    {
        return *m_Adapter;
    }
}
