// Opaque session ownership of point-input verdicts without importing capture internals.
// Include after EditorProcessing, WorldRegistry, CommandBus and JobService imports.
#pragma once
#include <memory>

extern "C++"
{
namespace Extrinsic::Runtime
{
    struct EditorPointInputReadinessState;
    [[nodiscard]] std::shared_ptr<EditorPointInputReadinessState> MakeEditorPointInputReadiness(
        WorldRegistry&, CommandBus*, JobService*);
    void BeginEditorPointInputReadinessFrame(EditorPointInputReadinessState&);
}
}
