module;

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string_view>
#include <vector>

export module Extrinsic.Runtime.EditorUiHost;

export import Extrinsic.Runtime.EditorWindowRegistry;

export namespace Extrinsic::Runtime
{
struct EditorUiDiagnostics
{
    bool Initialized{false};
    std::uint32_t FramesProduced{0u};
    std::uint32_t LastDrawListCount{0u};
    std::uint32_t LastVertexCount{0u};
    std::uint32_t LastIndexCount{0u};
    std::uint32_t LastCommandCount{0u};
    bool LastFrameUsedUserTexture{false};
    bool CapturesViewportInput{false};
    std::uint32_t PumpedEventCount{0u};
    std::uint32_t ContextRebuilds{0u};
    std::uint32_t EditorCallbackInvocations{0u};
    std::uint32_t CaptureSnapshots{0u};
    std::uint32_t DisplayWidth{0u};
    std::uint32_t DisplayHeight{0u};
    std::uint32_t FontAtlasCopyCount{0u};
    std::uint32_t FontAtlasReuseCount{0u};
    std::uint64_t LastFontAtlasByteCount{0u};
    std::uint64_t LastFrameFontAtlasCopyBytes{0u};
    std::uint64_t LastFrameVertexCopyBytes{0u};
    std::uint64_t LastFrameIndexCopyBytes{0u};
    std::uint64_t LastFrameCommandCopyBytes{0u};
    std::uint64_t LastFrameOverlayCopyBytes{0u};
    bool LastFrameFontAtlasCopied{false};
    std::uint64_t LastBeginFrameMicros{0u};
    std::uint64_t LastEditorCallbackMicros{0u};
    std::uint64_t LastImGuiRenderMicros{0u};
    std::uint64_t LastDrawDataCopyMicros{0u};
    std::uint64_t LastEndFrameMicros{0u};
};

struct EditorUiFrameContributionHandle
{
    std::uint64_t Value{0u};

    [[nodiscard]] bool IsValid() const noexcept { return Value != 0u; }
    [[nodiscard]] friend bool operator==(
        EditorUiFrameContributionHandle,
        EditorUiFrameContributionHandle) noexcept = default;
};

class EditorUiHost;

class EditorUiHostOwnerControl final
{
public:
    EditorUiHostOwnerControl() = default;
    ~EditorUiHostOwnerControl() = default;

    EditorUiHostOwnerControl(
        EditorUiHostOwnerControl&& other) noexcept;
    EditorUiHostOwnerControl& operator=(
        EditorUiHostOwnerControl&& other) noexcept;
    EditorUiHostOwnerControl(
        const EditorUiHostOwnerControl&) = delete;
    EditorUiHostOwnerControl& operator=(
        const EditorUiHostOwnerControl&) = delete;

    [[nodiscard]] bool IsValid() const noexcept;
    [[nodiscard]] std::size_t DrawFrameContributions();
    void SetOperational(bool operational) noexcept;
    void PublishDiagnostics(EditorUiDiagnostics diagnostics) noexcept;
    void SetVisibilityChangedCallback(
        std::function<void(bool)> callback);

private:
    friend class EditorUiHost;
    explicit EditorUiHostOwnerControl(EditorUiHost& host) noexcept;

    EditorUiHost* m_Host{nullptr};
};

class EditorUiHost final
{
public:
    EditorUiHost();
    ~EditorUiHost();

    EditorUiHost(const EditorUiHost&) = delete;
    EditorUiHost& operator=(const EditorUiHost&) = delete;
    EditorUiHost(EditorUiHost&&) = delete;
    EditorUiHost& operator=(EditorUiHost&&) = delete;

    [[nodiscard]] EditorUiFrameContributionHandle RegisterFrameContribution(
        std::function<void()> draw);
    [[nodiscard]] bool UnregisterFrameContribution(
        EditorUiFrameContributionHandle handle);

    [[nodiscard]] EditorWindowHandle RegisterWindow(
        EditorWindowDescriptor descriptor);
    [[nodiscard]] bool UnregisterWindow(EditorWindowHandle handle);
    [[nodiscard]] bool SetWindowOpen(std::string_view id, bool open);
    [[nodiscard]] std::vector<EditorWindowMenuEntry>
    BuildWindowMenuModel() const;

    [[nodiscard]] EditorUiVisibilityCommandResult ApplyVisibilityCommand(
        EditorUiVisibilityCommand command) noexcept;
    [[nodiscard]] bool IsVisible() const noexcept;
    [[nodiscard]] bool IsOperational() const noexcept;
    [[nodiscard]] const EditorUiDiagnostics& GetDiagnostics() const noexcept;

    [[nodiscard]] EditorWindowRegistry& Windows() noexcept;
    [[nodiscard]] const EditorWindowRegistry& Windows() const noexcept;

    // One control may be claimed before publication. EditorUiModule claims it
    // while constructing fresh boot state, so service consumers cannot obtain
    // owner mutation rights for the published host.
    [[nodiscard]] EditorUiHostOwnerControl ClaimOwnerControl() noexcept;

private:
    friend class EditorUiHostOwnerControl;

    [[nodiscard]] std::size_t DrawFrameContributions();
    void SetOperational(bool operational) noexcept;
    void PublishDiagnostics(EditorUiDiagnostics diagnostics) noexcept;
    void SetVisibilityChangedCallback(
        std::function<void(bool)> callback);

    struct Impl;
    std::unique_ptr<Impl> m_Impl;
};
} // namespace Extrinsic::Runtime
