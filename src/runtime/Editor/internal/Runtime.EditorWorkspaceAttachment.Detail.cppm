// Owns editor attachment state and session storage behind the public attachment handle.
module;

#include <cstdint>
#include <memory>
#include <functional>
#include <string>

export module Extrinsic.Runtime.Private.EditorWorkspaceAttachment;

import Extrinsic.Runtime.EditorWorkspaceAttachment;

import Extrinsic.Asset.ImportRouter;
import Extrinsic.Runtime.ServiceRegistry;
import Extrinsic.Runtime.WorldRegistry;

namespace Extrinsic::Runtime
{
    // Named only as borrowed references, pointers, or return-by-value results;
    // each family module owns the definition and every consumer that touches a
    // member imports that owner.
    extern "C++"
    {
        struct EditorWorkspaceSnapshotRequest;
        struct EditorWorkspaceSnapshot;
        struct EditorWorkspaceSnapshotContext;
        struct EditorProcessingContext;
        struct EditorSceneEditingContext;
        struct EditorVisualizationEditingContext;
        struct EditorRenderRecipeEditingContext;
        struct EditorPointFieldResultSinks;
        struct EditorPointAnalysisResultSinks;
        struct EditorPointSetResultSinks;
        struct EditorPointConstructionResultSinks;
        struct EditorPointCloudServiceResultSinks;
        struct EditorNormalResultSinks;
        struct EditorRegistrationResultSinks;
        struct EditorMeshFieldResultSinks;
        struct EditorMeshTopologyResultSinks;
        struct EditorParameterizationResultSinks;
        struct EditorPointFieldResultsSnapshot;
        struct EditorPointAnalysisResultsSnapshot;
        struct EditorPointSetResultsSnapshot;
        struct EditorPointConstructionResultsSnapshot;
        struct EditorPointCloudServiceResultsSnapshot;
        struct EditorNormalResultsSnapshot;
        struct EditorRegistrationResultsSnapshot;
        struct EditorMeshFieldResultsSnapshot;
        struct EditorMeshTopologyResultsSnapshot;
        struct EditorParameterizationResultsSnapshot;
        struct EditorParameterizationUvViewCommandSurface;
        struct EditorPointCloudServiceBorrowedServices;
    }
}

namespace Extrinsic::Runtime::EditorFeatureDetail
{
    // The all-family binding record stays private to the implementation units
    // that build contexts from it; the prepared view only borrows it.
    extern "C++" { struct EditorFeatureBindings; }
}

export namespace Extrinsic::Runtime::EditorFeatureDetail
{
    extern "C++"
    {
        struct EditorFeatureResultBindings
        {
            const EditorPointFieldResultSinks* PointFieldResultSinks{};
            const EditorPointAnalysisResultSinks* PointAnalysisResultSinks{};
            const EditorPointSetResultSinks* PointSetResultSinks{};
            const EditorPointConstructionResultSinks* PointConstructionResultSinks{};
            const EditorPointCloudServiceResultSinks* PointCloudServiceResultSinks{};
            const EditorNormalResultSinks* NormalResultSinks{};
            const EditorRegistrationResultSinks* RegistrationResultSinks{};
            const EditorMeshFieldResultSinks* MeshFieldResultSinks{};
            const EditorMeshTopologyResultSinks* MeshTopologyResultSinks{};
            const EditorParameterizationResultSinks* ParameterizationResultSinks{};
            const EditorPointFieldResultsSnapshot* PointFieldResults{};
            const EditorPointAnalysisResultsSnapshot* PointAnalysisResults{};
            const EditorPointSetResultsSnapshot* PointSetResults{};
            const EditorPointConstructionResultsSnapshot* PointConstructionResults{};
            const EditorPointCloudServiceResultsSnapshot* PointCloudServiceResults{};
            const EditorNormalResultsSnapshot* NormalResults{};
            const EditorRegistrationResultsSnapshot* RegistrationResults{};
            const EditorMeshFieldResultsSnapshot* MeshFieldResults{};
            const EditorMeshTopologyResultsSnapshot* MeshTopologyResults{};
            const EditorParameterizationResultsSnapshot* ParameterizationResults{};
            const EditorParameterizationUvViewCommandSurface* ParameterizationUvViewCommands{};
            const EditorPointCloudServiceBorrowedServices* PointCloudServices{};
        };

        // Reference-only view of the frame the session prepared. Every member
        // is borrowed from session storage for the visitor invocation only.
        struct EditorWorkspacePreparedFrame
        {
            const EditorFeatureBindings& Context;
            const EditorWorkspaceSnapshot& Frame;
            const EditorProcessingContext& Geometry;
            const EditorFeatureResultBindings& Results;
        };

        // Mirrors of the declarations in Runtime.EditorFeatures.Internal.hpp so a
        // feature leaf can project the prepared frame onto its own context without
        // including the all-family header. The definitions stay in
        // Runtime.EditorFeatureContextAdapters.cpp; each leaf sees the complete
        // return type through its own module interface.
        [[nodiscard]] EditorSceneEditingContext
        MakeEditorSceneEditingContext(const EditorFeatureBindings& bindings);
        [[nodiscard]] EditorVisualizationEditingContext
        MakeEditorVisualizationEditingContext(const EditorFeatureBindings& bindings);
        [[nodiscard]] EditorRenderRecipeEditingContext
        MakeEditorRenderRecipeEditingContext(const EditorFeatureBindings& bindings);
        // Reuse the session context captured after epoch guards and callbacks.
        [[nodiscard]] EditorWorkspaceSnapshotContext
        MakeEditorWorkspaceSnapshotContext(
            const EditorFeatureBindings& bindings,
            const EditorProcessingContext& geometry);
    }
    using EditorWorkspacePreparedFrameVisitor = std::function<void(EditorWorkspacePreparedFrame)>;

    class EditorWorkspaceSession
    {
    public:
        EditorWorkspaceSession();
        ~EditorWorkspaceSession();
        EditorWorkspaceSession(const EditorWorkspaceSession&) = delete;
        EditorWorkspaceSession& operator=(const EditorWorkspaceSession&) = delete;
        EditorWorkspaceSession(EditorWorkspaceSession&&) = delete;
        EditorWorkspaceSession& operator=(EditorWorkspaceSession&&) = delete;

        void Attach(WorldRegistry& worlds, ServiceRegistry& services);
        void Detach();
        [[nodiscard]] bool PrepareFrame(
            const EditorWorkspaceSnapshotRequest& request,
            std::string pendingAssetImportPath,
            Assets::AssetPayloadKind pendingAssetImportPayloadKind,
            std::string pendingSceneFilePath);
        // Prepared references may only be used within this visitor invocation.
        [[nodiscard]] bool VisitPreparedFrame(const EditorWorkspacePreparedFrameVisitor& visitor);
        [[nodiscard]] bool IsAttached() const noexcept;

    private:
        class Impl;
        std::unique_ptr<Impl> m_Impl;
    };

    struct EditorWorkspaceAttachmentState final
    {
        EditorWorkspaceSession Session{};
    };

    [[nodiscard]] inline std::shared_ptr<EditorWorkspaceAttachmentState>
    ResolveEditorWorkspaceAttachmentState(
        const EditorWorkspaceAttachment& attachment) noexcept
    {
        return std::static_pointer_cast<EditorWorkspaceAttachmentState>(
            attachment.FeatureBindingState());
    }
}
