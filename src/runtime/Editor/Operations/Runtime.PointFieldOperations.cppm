// RUNTIME-349: the kernel density and point spacing pilot family: their frame (results from the session
// store) and their modules re-exported. Frozen: later operations do not add to it.
module;
#include <cstddef>
#include <cstdint>
#include <vector>
#include <functional>
#include <optional>
#include <string>
#include <glm/vec3.hpp>
export module Extrinsic.Runtime.PointFieldOperations;
export import Extrinsic.Runtime.EditorProcessing;
export import Extrinsic.Runtime.PointScalarTransaction;
export import Extrinsic.Runtime.EditorCommon;
export import Extrinsic.Runtime.KernelDensityConfig;
export import Extrinsic.Runtime.PointSpacingConfig;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Graphics.GpuPropertyResidency;
import Extrinsic.Runtime.EditorWorkspaceAttachment;
export namespace Extrinsic::Runtime
{
    // RUNTIME-348: the results come from the session's operation result store (a copy for the
    // frame) and are dismissed through it; the operation runtime writes them.
    struct EditorPointFieldPreparedFrame
    {
        EditorProcessingCommands Commands{};
        OperationResultSnapshot Results{};
        OperationResultDismiss DismissResult{};
    };

    [[nodiscard]] EditorPointFieldPreparedFrame PrepareEditorPointFieldFrame(const EditorWorkspaceAttachment&);
}
