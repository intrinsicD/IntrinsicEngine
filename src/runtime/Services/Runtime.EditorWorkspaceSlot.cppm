// ARCH-022: the editor workspace a module service records its jobs and edits in: the app's while it lends one (the
// Sandbox shell's, so they are rows of its Jobs window), else the service's own, attached when the engine resolves.
export module Extrinsic.Runtime.EditorWorkspaceSlot;

export import Extrinsic.Runtime.EditorProcessing;
export import Extrinsic.Runtime.EditorWorkspaceAttachment;
import Extrinsic.Runtime.ServiceRegistry;
import Extrinsic.Runtime.WorldRegistry;

export namespace Extrinsic::Runtime
{
    // C++ language linkage: a service that owns one names it without importing this module.
    extern "C++" class EditorWorkspaceSlot
    {
    public:
        // At resolve: attaches the own workspace unless the app lent one already.
        void AttachOwn(WorldRegistry& worlds, ServiceRegistry& services);
        // At shutdown: detaches the own workspace while the services it attached to are alive; none is current then.
        void DetachOwn() noexcept;
        void Use(EditorWorkspaceAttachment& workspace) noexcept;
        // Before the lent workspace detaches: back to the own one.
        void Release(const EditorWorkspaceAttachment& workspace) noexcept;
        // The current workspace's commands for one call; the own workspace prepares its frame here (the app's shell
        // prepares one every frame). Unbound without a workspace.
        [[nodiscard]] EditorProcessingCommands Commands();
        // The current workspace's job surface (`Commands()`'s), for a service that queues editor jobs under its own
        // identities (a GPU run's stages); unbound without a workspace.
        [[nodiscard]] EditorJobCommandSurface JobCommands();
        // The current workspace, null without one.
        [[nodiscard]] EditorWorkspaceAttachment* Current() const noexcept { return m_Current; }

    private:
        EditorWorkspaceAttachment m_Own{};
        EditorWorkspaceAttachment* m_Current{nullptr};
    };
}
