// JSON of the shared step-run execution options (StepRunExecutionFields' rows) for sections validated by the
// declared-field tables: their values are checked by the rows, so decoding a validated document cannot fail.
// Include after <nlohmann/json.hpp> and `import Extrinsic.Runtime.StepRunExecution;`.
#pragma once

namespace Extrinsic::Runtime::ConfigDetail
{
    inline void EncodeStepRunExecutionJson(nlohmann::json& doc, const StepRunExecution& options)
    {
        doc["max_iterations"] = options.IterationCap;
        doc["preview_every"] = options.PreviewEvery;
        doc["preview_interval_ms"] = options.PreviewIntervalMs;
        doc["auto_accept"] = std::string{ToString(options.AutoAccept)};
        doc["start"] = std::string{ToString(options.Start)};
        doc["preview_buffers"] = options.PreviewBuffers;
    }

    // `doc` passed its section's declared-field validation (the six rows included).
    [[nodiscard]] inline StepRunExecution DecodeStepRunExecutionJson(const nlohmann::json& doc)
    {
        return {.IterationCap = doc.at("max_iterations").get<std::uint32_t>(),
            .PreviewEvery = doc.at("preview_every").get<std::uint32_t>(),
            .PreviewIntervalMs = doc.at("preview_interval_ms").get<std::uint32_t>(),
            .AutoAccept = ParseStepRunAutoAccept(doc.at("auto_accept").get<std::string>()).value(),
            .Start = ParseStepRunStart(doc.at("start").get<std::string>()).value(),
            .PreviewBuffers = doc.at("preview_buffers").get<std::uint32_t>()};
    }
}
