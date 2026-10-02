// Private adapter into the compiled resident scalar lifecycle; captures retain method publication semantics.
#pragma once
extern "C++" {
namespace Extrinsic::Runtime::GeometryProcessingDetail
{
    [[nodiscard]] bool AdmitPointScalarGpu(const EditorProcessingContext&,const PointScalarCapture&,
        const Graphics::PointScalarGpuParams&,std::string&);
    [[nodiscard]] EditorPointScalarTransactionHandle StartPointScalarGpu(
        const EditorProcessingContext&,std::shared_ptr<PointScalarCapture>,entt::entity,std::uint32_t stableId,
        GeometryPropertyRef positions,const Graphics::PointScalarGpuParams&,std::string label,
        // The operation's queued-job label, as its CPU run names it in a duplicate refusal.
        std::string_view jobLabel,
        EditorPointScalarTransactionSnapshot&,std::function<void(EditorPointScalarTransactionSnapshot)>,bool automatic,
        Graphics::GpuPropertyResidency* testResidency=nullptr,
        const EditorPointScalarTransactionSnapshot& testResult = {});
    // A publication transaction's Accept stage joins the run that computes its front: it is
    // submitted under `identity` (the output, with `Run` set to that run's first job).
    void JoinPointScalarRun(const EditorPointScalarTransactionHandle&, EditorJobIdentity identity);
}
}
