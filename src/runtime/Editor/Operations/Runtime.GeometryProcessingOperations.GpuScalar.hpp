// Private adapter into the compiled resident scalar lifecycle; captures retain method publication semantics.
#pragma once
extern "C++" {
namespace Extrinsic::Runtime::GeometryProcessingDetail
{
    [[nodiscard]] bool AdmitPointScalarGpu(const EditorProcessingContext&,const PointScalarCapture&,
        const Graphics::PointScalarGpuParams&,std::string&);
    [[nodiscard]] EditorPointScalarTransactionHandle StartPointScalarGpu(const EditorProcessingContext&,
        std::shared_ptr<PointScalarCapture>,
        entt::entity,
        std::uint32_t stableId,
        GeometryPropertyRef positions,
        const Graphics::PointScalarGpuParams&,
        std::string label,
        // The operation's queued-job label, as its CPU run names it in a duplicate refusal.
        std::string_view jobLabel,
        EditorPointScalarTransactionSnapshot&,
        std::function<void(EditorPointScalarTransactionSnapshot)>,
        const StepRunExecution& execution,
        Graphics::GpuPropertyResidency* testResidency = nullptr,
        const EditorPointScalarTransactionSnapshot& testResult = {},
        // Called before the sink at the terminal delivery and kept when Accept replaces the sink.
        std::function<void(EditorPointScalarTransactionSnapshot)> retain = {},
        std::string owner = "Runtime.GpuTransaction");
}
}
