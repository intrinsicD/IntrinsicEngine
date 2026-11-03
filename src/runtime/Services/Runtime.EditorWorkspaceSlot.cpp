module;

#include <concepts>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>

module Extrinsic.Runtime.EditorWorkspaceSlot;

import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.EditorWorkspaceSnapshots;
import Extrinsic.Runtime.GeometryProcessingOperations;

#include "Editor/internal/Runtime.EditorProcessingAccess.hpp"

namespace Extrinsic::Runtime
{
    void EditorWorkspaceSlot::AttachOwn(WorldRegistry& worlds, ServiceRegistry& services)
    {
        if (m_Current != nullptr) return;
        m_Own.Attach(worlds, services);
        m_Current = &m_Own;
    }

    void EditorWorkspaceSlot::DetachOwn() noexcept
    {
        m_Own.Detach();
        m_Current = nullptr;
    }

    void EditorWorkspaceSlot::Use(EditorWorkspaceAttachment& workspace) noexcept { m_Current = &workspace; }

    void EditorWorkspaceSlot::Release(const EditorWorkspaceAttachment& workspace) noexcept
    {
        if (m_Current == &workspace) m_Current = m_Own.IsAttached() ? &m_Own : nullptr;
    }

    EditorProcessingCommands EditorWorkspaceSlot::Commands()
    {
        if (m_Current == nullptr) return {};
        if (m_Current == &m_Own && m_Own.IsAttached()) (void)PrepareEditorWorkspaceSnapshotFrame(m_Own);
        return PrepareEditorProcessingCommands(*m_Current);
    }

    EditorJobCommandSurface EditorWorkspaceSlot::JobCommands()
    {
        return EditorProcessingCommandsAccess::Resolve(Commands()).JobCommands;
    }
}
