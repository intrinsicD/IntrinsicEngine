// Internal resolution of the shared command handle; expires before borrowing live services.
#pragma once
extern "C++"
{
namespace Extrinsic::Runtime
{
    struct EditorProcessingCommandsAccess
    {
        [[nodiscard]] static const EditorProcessingContext& Resolve(const EditorProcessingCommands&) noexcept;
    };

    [[nodiscard]] RuntimeEngineConfigApplyResult ApplyEditorProcessingConfig(
        const EditorProcessingCommands&, const Core::Config::EngineConfigSectionValidationResult&,
        const std::string& sourceId, const std::function<void(Core::Config::EngineConfig&)>& update);

    template <typename Result>
    std::function<void(Result)> GuardEditorProcessingResult(
        const EditorProcessingContext& context, std::function<void(Result)> sink)
    {
        if (!sink) return {};
        return [active = context.AttachmentActive, sink = std::move(sink)](Result result)
        {
            if (!active || active()) sink(std::move(result));
        };
    }
}
}
