// Shared execution dependencies and attachment-checked commands, independent of method records.
module;
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
export module Extrinsic.Runtime.EditorProcessing;
export import Extrinsic.Runtime.GeometryAvailability;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Runtime.WorldHandle;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.EditorJobProjection;
import Extrinsic.Runtime.EngineConfigControl;
// These services are borrowed only; their existing owners expose matching C++ linkage.
extern "C++" {
    namespace Extrinsic::ECS::Scene { class Registry; }
    namespace Extrinsic::RHI { class IDevice; }
    namespace Extrinsic::Runtime {
        class SpatialIndexCache;
        class SelectionController;
        struct EditorProcessingCommandsAccess;
        struct EditorPointInputReadinessState;
    }
}
export namespace Extrinsic::Runtime
{
    // C++ language linkage so the private attachment interface can borrow the
    // one context the session prepares without importing this module.
    extern "C++"
    {
        struct EditorProcessingContext
        {
            ECS::Scene::Registry* Scene{};
            WorldHandle World{DefaultWorldHandle};
            EditorCommandHistory* CommandHistory{};
            SpatialIndexCache* SpatialIndices{};
            // Borrowed render device. Operations that offer a GPU backend gate on
            // IsOperational() and fall back to CPU; a null device is not an error.
            RHI::IDevice* Device{};
            EditorJobCommandSurface JobCommands{};
            const RuntimeEngineConfigControlState* EngineConfigControlState{};
            std::function<Core::Config::EngineConfigLoadResult(const std::string&, const std::string&)> PreviewEngineConfigDocument{};
            std::function<RuntimeEngineConfigApplyResult(const Core::Config::EngineConfigLoadResult&)> ApplyEngineConfigHotSubset{};
            std::function<bool()> AttachmentActive{};
            std::function<void()> InvalidateWorkspaceSnapshotCache{};
            bool EngineConfigCommandsAvailable{};
            SelectionController* Selection{};
            // Host-declared kernel availability. These gate execution inside the
            // mesh families and feed the editor model's method availability, so
            // they are plain shared data rather than a per-method capability
            // surface. A false flag means the operation refuses and reports why.
            bool MeshDenoiseKernelAvailable{true};
            bool MeshCurvatureKernelAvailable{true};
            bool MeshCurvatureDirectionsAvailable{true};
            bool CurvatureSegmentationKernelAvailable{true};
            bool MeshRemeshUniformKernelAvailable{true};
            bool MeshRemeshAdaptiveKernelAvailable{true};
            bool MeshRemeshProjectToSurfaceAvailable{true};
            bool MeshRemeshErrorBoundedSizingAvailable{true};
            bool MeshSubdivideLoopKernelAvailable{true};
            bool MeshSubdivideCatmullClarkKernelAvailable{true};
            bool MeshSubdivideSqrt3KernelAvailable{true};
            bool MeshSubdivideLoopFeatureEdgesAvailable{true};
            bool MeshSimplifyKernelAvailable{true};
            // Prepared sessions share deferred point-input verdicts. Standalone
            // contexts without this state perform synchronous preflight.
            std::shared_ptr<EditorPointInputReadinessState> PointInputReadiness{};
        };
    }

    // Copied presentation state; commands still validate current inputs at apply time.
    struct ActionReadiness
    {
        bool Enabled{};
        std::string DisabledReason{};
    };

    struct EditorPointInputReadinessStats
    {
        std::uint64_t ChecksQueued{}, PropertyScans{};
    };

    class EditorProcessingCommands final
    {
    public:
        EditorProcessingCommands() = default;
        [[nodiscard]] bool IsBound() const noexcept;
    private:
        std::shared_ptr<const EditorProcessingContext> m_Context{};
        friend struct EditorProcessingCommandsAccess;
        friend EditorProcessingCommands BindEditorProcessingCommands(EditorProcessingContext);
    };
    [[nodiscard]] EditorProcessingCommands BindEditorProcessingCommands(EditorProcessingContext);
    // True when an attached handle can preview and hot-apply an engine config
    // document. Every family's `ApplyEditor*Config` needs exactly this, so panels
    // gate their controls on it instead of discovering the rejection.
    [[nodiscard]] bool AreEditorProcessingConfigCommandsAvailable(
        const EditorProcessingCommands&) noexcept;
    // Config-backed actions need both the live config lane and their method preflight.
    // Missing config commands take priority; otherwise preserve the method's reason.
    [[nodiscard]] ActionReadiness ResolveEditorProcessingActionReadiness(
        const EditorProcessingCommands&, ActionReadiness method);
    // Live finite vec3 rows on every resolved element domain of the entity, with
    // each entry's property revision and membership folded into the generation.
    // Prepared sessions omit pending entries until the command drain validates
    // them; standalone contexts validate synchronously. Every
    // method whose point input accepts any such property shares this catalog;
    // methods that additionally trial-capture an output or require a minimum
    // sample count own their own narrower catalog.
    [[nodiscard]] GeometryPropertyCatalogSnapshot GetEditorPointInputCatalog(
        const EditorProcessingCommands&, std::uint32_t stableId);
    [[nodiscard]] EditorPointInputReadinessStats GetEditorPointInputReadinessStats(
        const EditorProcessingCommands&);
}
