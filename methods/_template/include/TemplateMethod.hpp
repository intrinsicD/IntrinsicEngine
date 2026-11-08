// ARCH-023: the executable future-method template, a tiny concrete adapter on the shared owners. Copy and rename it,
// replace the math callbacks and the request, keep the lifecycle. A request runs as one step run of Runtime.StepRuns,
// so the host's run-control row and MCP run_control/run_observe drive it with every other method:
//   Init (Start)  captures the input through PointCloudEditingService on the main thread; nothing else (a ready
//                 start computes nothing).
//   Step          one logical iteration of the math on a worker; the first step of a generation first builds the
//                 neighbour rows with the explicit query choice and the initial state there. Run repeats the same
//                 steps up to the cap.
//   No preview    the editing service displays positions only, so a scalar state has no live view: preview_every is
//                 normalized to 0 (run_observe reports it) and no copy is made, as CPU smoothing does.
//   Accept        publishes once, as one undo entry, after the service's revision check of the capture (a stale
//                 capture is refused; a capture that went stale before discards the run).
//   Discard       publishes nothing; Restart resets the state from the same capture (a new generation).
// A one-shot method (one logical step: the whole result at once) runs on both backends with one request, one set of
// run controls and one publication owner (StartTemplateOneShotCpuRun, StartTemplateOneShotGpuRun):
//   CPU   the copy's Compute on a worker over the template's query rows (PointCloudEditingService::StartScalarCpuRun,
//         a one-shot worker step run); Accept publishes the column with one undo entry.
//   GPU   an existing resident producer, not a new kernel or executor: the point scalar transaction
//         (PointCloudEditingService::StartScalarAnalysis) on GpuTransactionCore. It runs one of that producer's
//         existing kernels (Graphics.PointScalarAnalysis, chosen by `Params.Method`), not the copy's Compute: a copy
//         with its own math adds its kernel there. Its input views, workspace and output ring stay resident until
//         Accept, Discard or the end of the run; Restart records again into them; Accept reads the front back once
//         and publishes it.
// Both step runs are the same run_control target. docs/methods/method-template.md#executable-template describes them.
// Include after `import Extrinsic.Runtime.PointCloudEditing;` (it exports the step-run and scalar-analysis types)
// and <functional>, <cstdint>, <span>, <string>, <string_view>, <vector> and <glm/vec3.hpp>; compiled in
// TemplateMethod.cpp.
#pragma once

namespace Intrinsic::Methods::Template
{
    namespace R = Extrinsic::Runtime;

    // The neighbour query of a run, an explicit and reported choice with one contract (the k nearest others,
    // ascending by squared distance, then index; self excluded by index): an exhaustive scan, or the service's cached
    // LBVH of the capture.
    enum class TemplateQuery : std::uint8_t
    {
        BruteForce,
        CachedLbvh
    };

    // The method's math, supplied by the copy. Init: the state from the captured live points (one value each). Step:
    // one logical iteration over `rows` (k compact neighbour indices per point); true once converged.
    struct TemplateMath
    {
        std::function<std::vector<float>(std::span<const glm::vec3> points)> Init{};
        std::function<bool(std::vector<float>& state, std::span<const std::uint32_t> rows, std::uint32_t k)> Step{};
    };

    struct TemplateRequest
    {
        std::uint32_t StableEntityId{0u};
        R::GeometryPropertyRef Positions{};
        R::GeometryPropertyRef Output{}; // a scalar column on the positions' domain; unrelated properties stay
        TemplateQuery Query{TemplateQuery::CachedLbvh};
        std::uint32_t K{4u}; // at most the live count minus one
        std::string History{"Template method"}; // the undo entry
        // The shared options: cap, auto_accept, start (preview_every is normalized to 0: no live preview).
        R::StepRunExecution Execution{.PreviewEvery = 0u, .PreviewIntervalMs = 0u};
    };

    // Starts the run in the owner's context: `jobs` is PluginStepRunJobs() for a plugin, EditorStepRunJobs(...) for a
    // runtime module. The run id, or 0 with `refusal` (an invalid request, a capture the service refuses, a row the
    // query cannot fill, or the driver's refusal).
    [[nodiscard]] std::uint64_t StartTemplateCpuRun(R::PointCloudEditingService& service,
        R::StepRunsService& runs,
        R::StepRunJobs jobs,
        const TemplateRequest& request,
        TemplateMath math,
        std::string& refusal);

    // A one-shot method's request, the same for both backends. The output is a scalar column on the positions'
    // domain (float on the GPU); `Execution`'s auto_accept and start apply (one step, no live preview). `Params.K` is
    // the one k: the CPU query rows' and the GPU producer's.
    struct TemplateOneShotRequest
    {
        std::uint32_t StableEntityId{0u};
        R::GeometryPropertyRef Positions{};
        R::GeometryPropertyRef Output{};
        TemplateQuery Query{TemplateQuery::CachedLbvh}; // the CPU rows
        std::string History{"Template one-shot method"};
        R::StepRunExecution Execution{R::kOneShotStepRunExecution}; // ready, waiting for Accept
        // The GPU producer's method and parameters; its K is also the CPU rows' k.
        Extrinsic::Graphics::PointScalarGpuParams Params{.K = 4u};
    };
    // The copy's CPU math: one value per live point from the k query rows (compact indices); false when it fails.
    using TemplateOneShotCompute = std::function<bool(std::span<const glm::vec3> points,
        std::span<const std::uint32_t> rows,
        std::uint32_t k,
        std::vector<float>& values)>;
    // The CPU counterpart, its jobs through `jobs` (as StartTemplateCpuRun's): the run id, or 0 with `refusal`.
    // `finished(status, message, ownResult)` is the run's one end (applied, stale, invalid, failed, discarded or
    // cancelled; ScalarCpuRun::Finish) on the main thread, always after the step returned, so a copy frees its step's
    // state there, never earlier (a discard arrives while the worker may still compute). Two ways it comes: an end
    // without a publication is the settled end (StepRunCallbacks::Finished): the holder job, which owns the output
    // while the run goes (a second run there is refused), already released it, so the next run on the output may start
    // from inside `finished`. A publication's answer (applied, invalid, failed, stale at Accept) comes from inside the
    // holder's completion: the output is still held, so start the next run on it only after `finished` returned.
    // `ownResult`: the step's own answer (its publication, or Compute failing) decided it.
    [[nodiscard]] std::uint64_t StartTemplateOneShotCpuRun(R::PointCloudEditingService& service,
        R::StepRunJobs jobs,
        const TemplateOneShotRequest& request,
        TemplateOneShotCompute compute,
        std::function<void(std::string_view status, std::string message, bool ownResult)> finished,
        std::string& refusal);
    // The GPU path: the handle owns the run's resident output until Accept, Discard or its destruction (which
    // discards an unaccepted result); its Poll().StepRun names the run. Empty with `refusal` (the producer's
    // admission: a float output, k <= 63, an operational float64 device, ...).
    [[nodiscard]] R::ScalarAnalysis StartTemplateOneShotGpuRun(
        R::PointCloudEditingService& service, const TemplateOneShotRequest& request, std::string& refusal);
}
