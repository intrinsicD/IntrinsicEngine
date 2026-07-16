module;

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

export module Extrinsic.Sandbox.Editor.Shell;

import Extrinsic.Runtime.EditorUiHost;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.SandboxEditorFacades;

export namespace Extrinsic::Sandbox::Editor
{
    void DrawDisabledReasonTooltip(std::string_view disabledReason);

    struct EditorWindowDescriptor
    {
        std::string Id{};
        std::vector<std::string> MenuPath{};
        std::string Title{};
        bool OpenByDefault{false};
        std::function<void(
            bool&,
            const Runtime::SandboxEditorContext&)> Draw{};
        std::function<void(bool)> OpenStateChanged{};
    };

    class EditorShell final
    {
    public:
        EditorShell();
        ~EditorShell();

        EditorShell(const EditorShell&) = delete;
        EditorShell& operator=(const EditorShell&) = delete;
        EditorShell(EditorShell&&) = delete;
        EditorShell& operator=(EditorShell&&) = delete;

        void Attach(Runtime::Engine& engine);
        void Detach();

        [[nodiscard]] Runtime::EditorWindowHandle RegisterEditorWindow(
            EditorWindowDescriptor descriptor);
        [[nodiscard]] bool UnregisterEditorWindow(
            Runtime::EditorWindowHandle handle);
        [[nodiscard]] Runtime::EditorUiVisibilityCommandResult
        ApplyEditorUiVisibilityCommand(
            Runtime::EditorUiVisibilityCommand command) noexcept;
        [[nodiscard]] bool IsEditorVisible() const noexcept;
        [[nodiscard]] std::vector<Runtime::EditorWindowMenuEntry>
        BuildEditorWindowMenuModel() const;
        [[nodiscard]] bool SetEditorWindowOpen(
            std::string_view id,
            bool open);
        [[nodiscard]] bool IsAttached() const noexcept;
        [[nodiscard]] const Runtime::SandboxEditorPanelFrame&
        GetLastFrame() const noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
