// ARCH-023: the execution options every step run shares, one typed struct with one check and one field table, so a
// method's config, its request admission, its panel and its MCP arguments use the same names, ranges and meaning:
//   max_iterations       the logical iteration cap (one step is one logical iteration), within the method's range
//   preview_every        live preview cadence in iterations; 0: no live preview (the result does not depend on it)
//   preview_interval_ms  the least time between live previews; 0: no time limit
//   auto_accept          never; converged (accept at convergence); end (a batch: accept at convergence or the cap)
//                        Stop never accepts: a stopped run waits for Accept or Discard
//   start                run (compute at once) or ready (initialized, nothing computed until run or step n)
//   preview_buffers      live device ring capacity (2 or 3); no device ring is used by a CPU-only preview
// A method embeds the struct in its config and keeps its algorithm parameters beside it (a derived config section binds
// the rows to that member with BindStepRunExecution). An axis a path cannot honour is refused or reported by that path,
// never accepted silently (a one-shot is one step without live preview and binds no cap or preview rows). Kept apart
// from Extrinsic.Runtime.StepRuns so config and request types (and agent argument readers) need not import the run
// driver.
module;

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

export module Extrinsic.Runtime.StepRunExecution;

import Extrinsic.Runtime.ConfigFieldSpec;

export namespace Extrinsic::Runtime
{
    enum class StepRunAutoAccept : std::uint8_t
    {
        Never,
        Converged,
        End
    };
    enum class StepRunStart : std::uint8_t
    {
        Ready,
        Run
    };

    inline constexpr std::array<std::string_view, 3> kStepRunAutoAcceptTokens{"never", "converged", "end"};
    inline constexpr std::array<std::string_view, 2> kStepRunStartTokens{"ready", "run"};
    inline constexpr std::uint32_t kMaxStepRunPreviewEvery = 1'000'000u;
    inline constexpr std::uint32_t kMaxStepRunPreviewIntervalMs = 60'000u;

    // The defaults are a run's that nothing configures: ready, every step previewed at most about 30 times a second.
    struct StepRunExecution
    {
        std::uint32_t IterationCap{100u};
        std::uint32_t PreviewEvery{1u};
        std::uint32_t PreviewIntervalMs{33u};
        StepRunAutoAccept AutoAccept{StepRunAutoAccept::Never};
        StepRunStart Start{StepRunStart::Ready};
        std::uint32_t PreviewBuffers{3u}; // device preview rings only; CPU-only previews allocate no ring
        friend constexpr bool operator==(const StepRunExecution&, const StepRunExecution&) = default;
    };

    [[nodiscard]] constexpr std::string_view ToString(const StepRunAutoAccept value) noexcept
    {
        return kStepRunAutoAcceptTokens[static_cast<std::size_t>(value)];
    }
    [[nodiscard]] constexpr std::string_view ToString(const StepRunStart value) noexcept
    {
        return kStepRunStartTokens[static_cast<std::size_t>(value)];
    }
    [[nodiscard]] constexpr std::optional<StepRunAutoAccept> ParseStepRunAutoAccept(
        const std::string_view token) noexcept
    {
        for (std::size_t i = 0; i < kStepRunAutoAcceptTokens.size(); ++i)
            if (kStepRunAutoAcceptTokens[i] == token)
                return static_cast<StepRunAutoAccept>(i);
        return std::nullopt;
    }
    [[nodiscard]] constexpr std::optional<StepRunStart> ParseStepRunStart(const std::string_view token) noexcept
    {
        for (std::size_t i = 0; i < kStepRunStartTokens.size(); ++i)
            if (kStepRunStartTokens[i] == token)
                return static_cast<StepRunStart>(i);
        return std::nullopt;
    }

    // The six rows, in the order above, with the method's own iteration limit (its existing numerical bound). Every
    // codec, validator, schema and panel hint of the options reads these rows.
    [[nodiscard]] constexpr std::array<ConfigFieldSpec, 6> StepRunExecutionFields(
        const std::uint32_t maxIterationCap, const std::uint32_t minIterationCap = 1u)
    {
        return {{
            {.Name = "max_iterations",
                .Type = ConfigFieldType::UInt,
                .Description = "Logical iteration cap: one step is one iteration.",
                .Min = minIterationCap,
                .Max = maxIterationCap},
            {.Name = "preview_every",
                .Type = ConfigFieldType::UInt,
                .Description = "Iterations between live previews; 0 turns live preview off (the result is unchanged).",
                .Min = 0,
                .Max = kMaxStepRunPreviewEvery},
            {.Name = "preview_interval_ms",
                .Type = ConfigFieldType::UInt,
                .Description = "Least time between live previews in milliseconds; 0: no time limit.",
                .Min = 0,
                .Max = kMaxStepRunPreviewIntervalMs},
            {.Name = "auto_accept",
                .Type = ConfigFieldType::StringEnum,
                .Description = "never: wait for Accept; converged: accept at convergence; end: accept at convergence "
                               "or the iteration cap. A stopped run always waits.",
                .EnumValues = kStepRunAutoAcceptTokens},
            {.Name = "start",
                .Type = ConfigFieldType::StringEnum,
                .Description = "run: compute at once; ready: initialize only, then run or step n.",
                .EnumValues = kStepRunStartTokens},
            {.Name = "preview_buffers",
                .Type = ConfigFieldType::UInt,
                .Description =
                    "Device live-preview ring slots: 2 (double) or 3 (triple); CPU-only previews use no device ring.",
                .Min = 2,
                .Max = 3},
        }};
    }

    // The six rows bound to a config's StepRunExecution member, in StepRunExecutionFields' order, for a derived config
    // section to list beside its own rows (each, or only the ones its path honours).
    template <auto Member>
    [[nodiscard]] constexpr auto BindStepRunExecution(
        const std::uint32_t maxIterationCap, const std::uint32_t minIterationCap = 1u)
    {
        using Cfg = typename BoundMember<decltype(Member)>::Command;
        const auto fields = StepRunExecutionFields(maxIterationCap, minIterationCap);
        return std::array<BoundField<Cfg>, 6>{{
            {.Spec = fields[0],
                .Get =
                    [](const Cfg& c)
                {
                    return double((c.*Member).IterationCap);
                },
                .Set =
                    [](Cfg& c, const double v)
                {
                    (c.*Member).IterationCap = static_cast<std::uint32_t>(v);
                }},
            {.Spec = fields[1],
                .Get =
                    [](const Cfg& c)
                {
                    return double((c.*Member).PreviewEvery);
                },
                .Set =
                    [](Cfg& c, const double v)
                {
                    (c.*Member).PreviewEvery = static_cast<std::uint32_t>(v);
                }},
            {.Spec = fields[2],
                .Get =
                    [](const Cfg& c)
                {
                    return double((c.*Member).PreviewIntervalMs);
                },
                .Set =
                    [](Cfg& c, const double v)
                {
                    (c.*Member).PreviewIntervalMs = static_cast<std::uint32_t>(v);
                }},
            {.Spec = fields[3],
                .Get =
                    [](const Cfg& c)
                {
                    return double(std::to_underlying((c.*Member).AutoAccept));
                },
                .Set =
                    [](Cfg& c, const double v)
                {
                    (c.*Member).AutoAccept = static_cast<StepRunAutoAccept>(static_cast<std::uint8_t>(v));
                }},
            {.Spec = fields[4],
                .Get =
                    [](const Cfg& c)
                {
                    return double(std::to_underlying((c.*Member).Start));
                },
                .Set =
                    [](Cfg& c, const double v)
                {
                    (c.*Member).Start = static_cast<StepRunStart>(static_cast<std::uint8_t>(v));
                }},
            {.Spec = fields[5],
                .Get =
                    [](const Cfg& c)
                {
                    return double((c.*Member).PreviewBuffers);
                },
                .Set =
                    [](Cfg& c, const double v)
                {
                    (c.*Member).PreviewBuffers = static_cast<std::uint32_t>(v);
                }},
        }};
    }

    // Empty when `options` is inside the rows' ranges (with the method's limit), else the first violated rule.
    [[nodiscard]] constexpr std::string_view StepRunExecutionRefusal(const StepRunExecution& options,
        const std::uint32_t maxIterationCap,
        const std::uint32_t minIterationCap = 1u) noexcept
    {
        if (options.IterationCap < minIterationCap || options.IterationCap > maxIterationCap)
            return "max_iterations is outside the method's range.";
        if (options.PreviewEvery > kMaxStepRunPreviewEvery)
            return "preview_every is too large.";
        if (options.PreviewBuffers < 2u || options.PreviewBuffers > 3u)
            return "preview_buffers must be 2 or 3.";
        if (options.PreviewIntervalMs > kMaxStepRunPreviewIntervalMs)
            return "preview_interval_ms is too large.";
        if (static_cast<std::size_t>(options.AutoAccept) >= kStepRunAutoAcceptTokens.size())
            return "auto_accept is unknown.";
        if (static_cast<std::size_t>(options.Start) >= kStepRunStartTokens.size())
            return "start is unknown.";
        return {};
    }

    // A one-shot method's execution (ARCH-023): one logical step (bounded GPU pages inside it), so its cap is 1 and
    // it makes no live preview copy (its result shows once the step ended); auto_accept and start remain its axes.
    // These defaults are every one-shot section's and interactive start's (panel, plugin window, agent start): ready
    // (nothing computed before run or step n), waiting for Accept.
    inline constexpr StepRunExecution kOneShotStepRunExecution{.IterationCap = 1u,
        .PreviewEvery = 0u,
        .PreviewIntervalMs = 0u,
        .AutoAccept = StepRunAutoAccept::Never,
        .Start = StepRunStart::Ready};
    // A one-shot's execution from a caller's options: their auto_accept and start, the cap 1 and no live preview.
    [[nodiscard]] constexpr StepRunExecution OneShotExecution(const StepRunExecution& options) noexcept
    {
        auto execution = kOneShotStepRunExecution;
        execution.AutoAccept = options.AutoAccept;
        execution.Start = options.Start;
        return execution;
    }
    // A one-shot batch (a command's Apply, an agent call that waits): computes at once and accepts at its end.
    inline constexpr StepRunExecution kOneShotBatchExecution =
        OneShotExecution({.AutoAccept = StepRunAutoAccept::End, .Start = StepRunStart::Run});

    // MCP arguments of the shared execution options (StepRunExecutionFields): the JSON Schema properties, without
    // braces, for a start tool to splice into its own, and their reader. The reader keeps `options`' value for an
    // absent argument and refuses (false, with the reason) a value outside the rows or a non-object. A cap of 1 is a
    // one-shot: its arguments are auto_accept and start only (the cap and the live preview are fixed, not inert
    // arguments).
    [[nodiscard]] std::string StepRunExecutionSchemaProperties(
        std::uint32_t maxIterationCap, std::uint32_t minIterationCap = 1u);
    [[nodiscard]] bool ReadStepRunExecutionArguments(std::string_view argumentsJson,
        std::uint32_t maxIterationCap,
        StepRunExecution& options,
        std::string& refusal,
        std::uint32_t minIterationCap = 1u);
    // Whether the arguments name any execution argument the reader reads for `maxIterationCap` (an object only).
    [[nodiscard]] bool NamesStepRunExecutionArguments(std::string_view argumentsJson, std::uint32_t maxIterationCap);
}
