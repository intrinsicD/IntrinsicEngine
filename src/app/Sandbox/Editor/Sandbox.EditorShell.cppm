// Declares editor window registration and lifecycle without processing views.
module;

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

export module Extrinsic.Sandbox.Editor.Shell;

import Extrinsic.Runtime.EditorWindowRegistry;
import Extrinsic.Runtime.ServiceRegistry;
import Extrinsic.Runtime.WorldRegistry;

export namespace Extrinsic::Sandbox::Editor
{
    // The first declarations here must be exported; keep the GMF forward header
    // in consumers only, since exported redeclarations cannot follow it.
    extern "C++"
    {
        struct SandboxEditorContext;
        struct SandboxEditorFrame;
    }

    struct EditorWindowDescriptor
    {
        std::string Id{};
        std::vector<std::string> MenuPath{};
        std::string Title{};
        bool OpenByDefault{false};
        std::function<void(
            bool&,
            const SandboxEditorContext&)> Draw{};
        std::function<void(bool)> OpenStateChanged{};
    };

    extern "C++"
    {
        class EditorShell final
        {
        public:
            EditorShell();
            ~EditorShell();

            EditorShell(const EditorShell&) = delete;
            EditorShell& operator=(const EditorShell&) = delete;
            EditorShell(EditorShell&&) = delete;
            EditorShell& operator=(EditorShell&&) = delete;

            void Attach(Runtime::WorldRegistry& worlds, Runtime::ServiceRegistry& services);
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
            // Called every attached UI frame before open windows draw, so a
            // panel can react to results (e.g. open its window) while closed.
            [[nodiscard]] std::uint64_t AddFrameObserver(
                std::function<void(const SandboxEditorContext&)> observer);
            void RemoveFrameObserver(std::uint64_t id) noexcept;
            [[nodiscard]] bool IsAttached() const noexcept;
            [[nodiscard]] const SandboxEditorFrame&
            GetLastFrame() const noexcept;

        private:
            struct Impl;
            std::unique_ptr<Impl> m_Impl;
        };
    }
}
