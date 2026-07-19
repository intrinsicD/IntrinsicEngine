module;

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

module Extrinsic.Runtime.EditorUiHost;

namespace Extrinsic::Runtime
{
namespace
{
    struct EditorUiFrameContributionRecord
    {
        EditorUiFrameContributionHandle Handle{};
        std::function<void()> Draw{};
    };
}

struct EditorUiHost::Impl
{
    EditorWindowRegistry WindowRegistry{};
    std::vector<EditorUiFrameContributionRecord> Contributions{};
    EditorUiDiagnostics Diagnostics{};
    std::function<void(bool)> VisibilityChanged{};
    std::uint64_t NextContributionHandle{1u};
    bool Operational{false};
};

EditorUiHost::EditorUiHost() : m_Impl(std::make_unique<Impl>()) {}

EditorUiHost::~EditorUiHost() = default;

EditorUiFrameContributionHandle EditorUiHost::RegisterFrameContribution(
    std::function<void()> draw)
{
    if (!draw)
        return {};

    EditorUiFrameContributionRecord record{
        .Handle = EditorUiFrameContributionHandle{
            m_Impl->NextContributionHandle++},
        .Draw = std::move(draw),
    };
    m_Impl->Contributions.push_back(std::move(record));
    return m_Impl->Contributions.back().Handle;
}

bool EditorUiHost::UnregisterFrameContribution(
    const EditorUiFrameContributionHandle handle)
{
    if (!handle.IsValid())
        return false;

    const auto found = std::ranges::find(
        m_Impl->Contributions,
        handle,
        &EditorUiFrameContributionRecord::Handle);
    if (found == m_Impl->Contributions.end())
        return false;
    m_Impl->Contributions.erase(found);
    return true;
}

EditorWindowHandle
EditorUiHost::RegisterWindow(EditorWindowDescriptor descriptor)
{
    return m_Impl->WindowRegistry.Register(std::move(descriptor));
}

bool EditorUiHost::UnregisterWindow(const EditorWindowHandle handle)
{
    return m_Impl->WindowRegistry.Unregister(handle);
}

bool EditorUiHost::SetWindowOpen(
    const std::string_view id,
    const bool open)
{
    return m_Impl->WindowRegistry.SetOpen(id, open);
}

std::vector<EditorWindowMenuEntry>
EditorUiHost::BuildWindowMenuModel() const
{
    return m_Impl->WindowRegistry.BuildMenuModel();
}

EditorUiVisibilityCommandResult EditorUiHost::ApplyVisibilityCommand(
    const EditorUiVisibilityCommand command) noexcept
{
    const EditorUiVisibilityCommandResult result =
        ApplyEditorUiVisibilityCommand(
        m_Impl->WindowRegistry, command);
    if (m_Impl->VisibilityChanged)
        m_Impl->VisibilityChanged(result.IsVisible);
    return result;
}

bool EditorUiHost::IsVisible() const noexcept
{
    return m_Impl->WindowRegistry.IsVisible();
}

bool EditorUiHost::IsOperational() const noexcept
{
    return m_Impl->Operational;
}

const EditorUiDiagnostics&
EditorUiHost::GetDiagnostics() const noexcept
{
    return m_Impl->Diagnostics;
}

EditorWindowRegistry& EditorUiHost::Windows() noexcept
{
    return m_Impl->WindowRegistry;
}

const EditorWindowRegistry& EditorUiHost::Windows() const noexcept
{
    return m_Impl->WindowRegistry;
}

std::size_t EditorUiHost::DrawFrameContributions()
{
    if (!m_Impl->Operational || !m_Impl->WindowRegistry.IsVisible())
        return 0u;

    std::vector<EditorUiFrameContributionHandle> handles;
    handles.reserve(m_Impl->Contributions.size());
    for (const EditorUiFrameContributionRecord& contribution :
         m_Impl->Contributions)
    {
        handles.push_back(contribution.Handle);
    }

    std::size_t invoked = 0u;
    for (const EditorUiFrameContributionHandle handle : handles)
    {
        const auto found = std::ranges::find(
            m_Impl->Contributions,
            handle,
            &EditorUiFrameContributionRecord::Handle);
        if (found == m_Impl->Contributions.end() || !found->Draw)
            continue;
        std::function<void()> draw = found->Draw;
        draw();
        ++invoked;
    }
    return invoked;
}

void EditorUiHost::SetOperational(const bool operational) noexcept
{
    m_Impl->Operational = operational;
}

void EditorUiHost::PublishDiagnostics(
    EditorUiDiagnostics diagnostics) noexcept
{
    m_Impl->Diagnostics = std::move(diagnostics);
}

void EditorUiHost::SetVisibilityChangedCallback(
    std::function<void(bool)> callback)
{
    m_Impl->VisibilityChanged = std::move(callback);
}
} // namespace Extrinsic::Runtime
