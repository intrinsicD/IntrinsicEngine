module;

#include <memory>

export module Extrinsic.Runtime.EditorWorkspaceAttachment;

import Extrinsic.Runtime.ServiceRegistry;
import Extrinsic.Runtime.WorldRegistry;

export namespace Extrinsic::Runtime
{
    // Shared runtime lifecycle only. Feature owners consume the opaque state
    // token in their implementation units and publish their own prepared
    // capabilities; the app composes those feature records explicitly.
    class EditorWorkspaceAttachment final
    {
    public:
        EditorWorkspaceAttachment();
        ~EditorWorkspaceAttachment();

        EditorWorkspaceAttachment(const EditorWorkspaceAttachment&) = delete;
        EditorWorkspaceAttachment& operator=(const EditorWorkspaceAttachment&) = delete;
        EditorWorkspaceAttachment(EditorWorkspaceAttachment&&) = delete;
        EditorWorkspaceAttachment& operator=(EditorWorkspaceAttachment&&) = delete;

        void Attach(WorldRegistry& worlds, ServiceRegistry& services);
        void Detach();

        [[nodiscard]] bool IsAttached() const noexcept;

        // Runtime feature implementation units use this type-erased token to
        // reach the shared attachment state without exposing ECS/assets/RHI
        // pointers or an all-feature command aggregate to app callers.
        [[nodiscard]] std::shared_ptr<void>
        FeatureBindingState() const noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
