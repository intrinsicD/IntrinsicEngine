#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.EngineConfigBoot;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.FramePacingDiagnostics;
import Extrinsic.Runtime.AssetWorkflowModule;
import Extrinsic.Runtime.AsyncWorkModule;
import Extrinsic.Runtime.CameraModule;
import Extrinsic.Runtime.ClusteringModule;
import Extrinsic.Runtime.EditorUiModule;
import Extrinsic.Runtime.Module;
import Extrinsic.Runtime.SceneDocumentModule;
import Extrinsic.Runtime.SceneInteractionModule;
import Extrinsic.Runtime.TextureBakeModule;

import Extrinsic.Sandbox;
import Extrinsic.Sandbox.ConfigSections;

namespace
{
    struct FramePacingCaptureOptions
    {
        bool Enabled = false;
        std::filesystem::path ReportPath{};
        std::uint32_t TargetFrames = 120u;
    };

    struct ParsedCli
    {
        FramePacingCaptureOptions Capture{};
        bool Valid = true;
        std::string Error{};
    };

    [[nodiscard]] bool ParsePositiveUInt(const std::string_view text,
                                         std::uint32_t& out) noexcept
    {
        if (text.empty())
        {
            return false;
        }

        std::uint32_t value = 0u;
        const char* begin = text.data();
        const char* end = text.data() + text.size();
        const auto result = std::from_chars(begin, end, value);
        if (result.ec != std::errc{} || result.ptr != end || value == 0u)
        {
            return false;
        }

        out = value;
        return true;
    }

    [[nodiscard]] ParsedCli ParseFramePacingArgs(
        const std::vector<std::string_view>& args)
    {
        ParsedCli parsed{};

        for (std::size_t index = 1; index < args.size(); ++index)
        {
            const std::string_view arg = args[index];
            if (arg == "--frame-pacing-report")
            {
                if (index + 1u >= args.size())
                {
                    parsed.Valid = false;
                    parsed.Error = "--frame-pacing-report requires a path";
                    return parsed;
                }
                parsed.Capture.Enabled = true;
                parsed.Capture.ReportPath = std::filesystem::path{
                    std::string{args[++index]}};
            }
            else if (arg.starts_with("--frame-pacing-report="))
            {
                parsed.Capture.Enabled = true;
                parsed.Capture.ReportPath = std::filesystem::path{
                    std::string{arg.substr(22u)}};
            }
            else if (arg == "--frame-pacing-frames")
            {
                if (index + 1u >= args.size())
                {
                    parsed.Valid = false;
                    parsed.Error = "--frame-pacing-frames requires a positive integer";
                    return parsed;
                }
                if (!ParsePositiveUInt(args[++index], parsed.Capture.TargetFrames))
                {
                    parsed.Valid = false;
                    parsed.Error = "--frame-pacing-frames requires a positive integer";
                    return parsed;
                }
            }
            else if (arg.starts_with("--frame-pacing-frames="))
            {
                if (!ParsePositiveUInt(arg.substr(22u), parsed.Capture.TargetFrames))
                {
                    parsed.Valid = false;
                    parsed.Error = "--frame-pacing-frames requires a positive integer";
                    return parsed;
                }
            }
        }

        if (parsed.Capture.Enabled && parsed.Capture.ReportPath.empty())
        {
            parsed.Valid = false;
            parsed.Error = "--frame-pacing-report requires a non-empty path";
        }

        return parsed;
    }

    struct FramePacingCaptureSnapshot
    {
        bool DeviceOperational{false};
        Extrinsic::Runtime::RuntimeFramePacingDiagnostics Diagnostics{};
    };

    struct FramePacingCaptureState
    {
        bool FinalDeviceOperational{false};
        std::optional<std::uint64_t> LastCapturedFrame{};
        std::vector<Extrinsic::Runtime::RuntimeFramePacingDiagnostics> Samples{};
    };

    class FramePacingCaptureModule final : public Extrinsic::Runtime::IRuntimeModule
    {
    public:
        FramePacingCaptureModule(FramePacingCaptureState& state, const std::uint32_t targetFrames,
                                 std::function<FramePacingCaptureSnapshot()> readLatest,
                                 std::function<void()> requestExit) noexcept
            : m_State(state)
            , m_TargetFrames(targetFrames)
            , m_ReadLatest(std::move(readLatest))
            , m_RequestExit(std::move(requestExit))
        {
        }

        [[nodiscard]] std::string_view Name() const noexcept override
        {
            return "Sandbox.FramePacingCapture";
        }

        [[nodiscard]] Extrinsic::Runtime::RuntimeModuleResult
        OnRegister(Extrinsic::Runtime::EngineSetup& setup) override
        {
            return setup.RegisterFrameHook(Extrinsic::Runtime::FramePhase::UiBuild,
                                           [this](Extrinsic::Runtime::RuntimeFrameHookContext&)
                                           {
                                               CaptureLatest();
                                               ++m_Frames;
                                               if (m_Frames >= m_TargetFrames && m_RequestExit)
                                                   m_RequestExit();
                                           });
        }

        void OnShutdown(Extrinsic::Runtime::RuntimeModuleShutdownContext&) override {}

        void CaptureLatest()
        {
            if (!m_ReadLatest)
                return;
            const FramePacingCaptureSnapshot latest = m_ReadLatest();
            m_State.FinalDeviceOperational          = latest.DeviceOperational;
            const Extrinsic::Runtime::RuntimeFramePacingDiagnostics& sample = latest.Diagnostics;
            if (!sample.Valid)
            {
                return;
            }
            if (m_State.LastCapturedFrame.has_value() &&
                *m_State.LastCapturedFrame == sample.FrameIndex)
            {
                return;
            }

            m_State.Samples.push_back(sample);
            m_State.LastCapturedFrame = sample.FrameIndex;
        }

    private:
        FramePacingCaptureState& m_State;
        std::uint32_t m_TargetFrames{1u};
        std::uint32_t m_Frames{0u};
        std::function<FramePacingCaptureSnapshot()> m_ReadLatest{};
        std::function<void()> m_RequestExit{};
    };

    using FramePacingDiagnostics =
        Extrinsic::Runtime::RuntimeFramePacingDiagnostics;

    struct PhaseField
    {
        std::string_view Name;
        std::uint64_t FramePacingDiagnostics::*Member;
    };

    constexpr std::array<PhaseField, 24u> kPhaseFields{{
        {"platform_begin_micros", &FramePacingDiagnostics::PlatformBeginMicros},
        {"resize_micros", &FramePacingDiagnostics::ResizeMicros},
        {"operational_transition_micros",
         &FramePacingDiagnostics::OperationalTransitionMicros},
        {"fixed_step_micros", &FramePacingDiagnostics::FixedStepMicros},
        {"imgui_begin_micros", &FramePacingDiagnostics::ImGuiBeginMicros},
        {"variable_tick_micros", &FramePacingDiagnostics::VariableTickMicros},
        {"imgui_end_micros", &FramePacingDiagnostics::ImGuiEndMicros},
        {"imgui_editor_callback_micros",
         &FramePacingDiagnostics::ImGuiEditorCallbackMicros},
        {"imgui_draw_data_copy_micros",
         &FramePacingDiagnostics::ImGuiDrawDataCopyMicros},
        {"pre_render_setup_micros", &FramePacingDiagnostics::PreRenderSetupMicros},
        {"pre_render_transform_flush_micros",
         &FramePacingDiagnostics::PreRenderTransformFlushMicros},
        {"selection_pick_drain_micros",
         &FramePacingDiagnostics::SelectionPickDrainMicros},
        {"render_contract_micros", &FramePacingDiagnostics::RenderContractMicros},
        {"render_begin_frame_micros",
         &FramePacingDiagnostics::RenderBeginFrameMicros},
        {"render_extraction_micros",
         &FramePacingDiagnostics::RenderExtractionMicros},
        {"render_prepare_micros", &FramePacingDiagnostics::RenderPrepareMicros},
        {"render_execute_micros", &FramePacingDiagnostics::RenderExecuteMicros},
        {"render_end_frame_micros", &FramePacingDiagnostics::RenderEndFrameMicros},
        {"render_graph_compile_micros",
         &FramePacingDiagnostics::RenderGraphCompileMicros},
        {"render_graph_execute_micros",
         &FramePacingDiagnostics::RenderGraphExecuteMicros},
        {"present_micros", &FramePacingDiagnostics::PresentMicros},
        {"maintenance_micros", &FramePacingDiagnostics::MaintenanceMicros},
        {"selection_readback_micros",
         &FramePacingDiagnostics::SelectionReadbackMicros},
        {"release_render_world_micros",
         &FramePacingDiagnostics::ReleaseRenderWorldMicros},
    }};

    [[nodiscard]] const PhaseField& TopPhaseByTotal(
        const std::vector<Extrinsic::Runtime::RuntimeFramePacingDiagnostics>& samples,
        std::uint64_t& outTotal) noexcept
    {
        const PhaseField* best = &kPhaseFields.front();
        std::uint64_t bestTotal = 0u;

        for (const PhaseField& phase : kPhaseFields)
        {
            std::uint64_t total = 0u;
            for (const auto& sample : samples)
            {
                total += sample.*(phase.Member);
            }
            if (total > bestTotal)
            {
                best = &phase;
                bestTotal = total;
            }
        }

        outTotal = bestTotal;
        return *best;
    }

    void WriteBool(std::ofstream& out, const bool value)
    {
        out << (value ? "true" : "false");
    }

    [[nodiscard]] bool WriteFramePacingReport(
        const std::filesystem::path& path,
        const std::uint32_t requestedFrames,
        const bool finalDeviceOperational,
        const std::vector<Extrinsic::Runtime::RuntimeFramePacingDiagnostics>& samples)
    {
        if (const std::filesystem::path parent = path.parent_path(); !parent.empty())
        {
            std::error_code error;
            std::filesystem::create_directories(parent, error);
            if (error)
            {
                std::cerr << "failed to create frame-pacing report directory: "
                          << error.message() << '\n';
                return false;
            }
        }

        std::ofstream out(path);
        if (!out)
        {
            std::cerr << "failed to open frame-pacing report path: "
                      << path << '\n';
            return false;
        }

        std::uint64_t totalMicros = 0u;
        std::uint64_t maxTotalMicros = 0u;
        for (const auto& sample : samples)
        {
            totalMicros += sample.TotalMicros;
            maxTotalMicros = std::max(maxTotalMicros, sample.TotalMicros);
        }

        std::uint64_t topPhaseTotal = 0u;
        const PhaseField& topPhase = TopPhaseByTotal(samples, topPhaseTotal);
        const std::uint64_t meanTotalMicros =
            samples.empty() ? 0u : totalMicros / samples.size();

        out << "{\n";
        out << "  \"schema\": \"intrinsic.frame_pacing.v1\",\n";
        out << "  \"source\": \"ExtrinsicSandbox\",\n";
        out << "  \"requested_frames\": " << requestedFrames << ",\n";
        out << "  \"frame_count\": " << samples.size() << ",\n";
        out << "  \"summary\": {\n";
        out << "    \"total_micros\": " << totalMicros << ",\n";
        out << "    \"mean_total_micros\": " << meanTotalMicros << ",\n";
        out << "    \"max_total_micros\": " << maxTotalMicros << ",\n";
        out << "    \"final_device_operational\": ";
        WriteBool(out, finalDeviceOperational);
        out << ",\n";
        out << "    \"top_phase_by_total\": \"" << topPhase.Name << "\",\n";
        out << "    \"top_phase_total_micros\": " << topPhaseTotal << ",\n";
        out << "    \"phase_totals\": {\n";
        for (std::size_t index = 0; index < kPhaseFields.size(); ++index)
        {
            const PhaseField& phase = kPhaseFields[index];
            std::uint64_t phaseTotal = 0u;
            for (const auto& sample : samples)
            {
                phaseTotal += sample.*(phase.Member);
            }
            out << "      \"" << phase.Name << "\": " << phaseTotal;
            out << (index + 1u == kPhaseFields.size() ? "\n" : ",\n");
        }
        out << "    }\n";
        out << "  },\n";
        out << "  \"samples\": [\n";
        for (std::size_t sampleIndex = 0; sampleIndex < samples.size(); ++sampleIndex)
        {
            const auto& sample = samples[sampleIndex];
            out << "    {\n";
            out << "      \"frame_index\": " << sample.FrameIndex << ",\n";
            out << "      \"total_micros\": " << sample.TotalMicros << ",\n";
            out << "      \"platform_continue_frame\": ";
            WriteBool(out, sample.PlatformContinueFrame);
            out << ",\n";
            out << "      \"renderer_began_frame\": ";
            WriteBool(out, sample.RendererBeganFrame);
            out << ",\n";
            out << "      \"renderer_completed_frame\": ";
            WriteBool(out, sample.RendererCompletedFrame);
            out << ",\n";
            out << "      \"phases\": {\n";
            for (std::size_t phaseIndex = 0; phaseIndex < kPhaseFields.size(); ++phaseIndex)
            {
                const PhaseField& phase = kPhaseFields[phaseIndex];
                out << "        \"" << phase.Name << "\": "
                    << sample.*(phase.Member);
                out << (phaseIndex + 1u == kPhaseFields.size() ? "\n" : ",\n");
            }
            out << "      }\n";
            out << "    }";
            out << (sampleIndex + 1u == samples.size() ? "\n" : ",\n");
        }
        out << "  ]\n";
        out << "}\n";

        return true;
    }
}

int main(int argc, char** argv)
{
    std::vector<std::string_view> args{};
    args.reserve(static_cast<std::size_t>(argc));
    for (int index = 0; index < argc; ++index)
    {
        args.emplace_back(argv[index]);
    }

    const ParsedCli cli = ParseFramePacingArgs(args);
    if (!cli.Valid)
    {
        std::cerr << cli.Error << '\n';
        return 2;
    }

    auto configControl =
        std::make_unique<Extrinsic::Runtime::EngineConfigControl>(
            Extrinsic::Sandbox::CreateSandboxConfigSectionRegistry());
    auto boot = Extrinsic::Runtime::ResolveEngineConfigForBoot(
        args,
        configControl->SectionRegistry());
    auto config = std::move(boot.Config);

    Extrinsic::Runtime::Engine engine{config};
    FramePacingCaptureState captureState{};
    FramePacingCaptureModule* capture = nullptr;
    if (cli.Capture.Enabled)
    {
        auto captureModule = std::make_unique<FramePacingCaptureModule>(
            captureState, cli.Capture.TargetFrames,
            [&engine]
            {
                return FramePacingCaptureSnapshot{
                    .DeviceOperational = engine.GetDevice().IsOperational(),
                    .Diagnostics       = engine.GetLastFramePacingDiagnostics(),
                };
            },
            [&engine] { engine.RequestExit(); });
        capture = captureModule.get();
        engine.AddModule(std::move(captureModule));
    }

    engine.AddModule(std::move(configControl));
    engine.EmplaceModule<Extrinsic::Runtime::AsyncWorkModule>();
    engine.EmplaceModule<Extrinsic::Runtime::CameraModule>();
    engine.EmplaceModule<Extrinsic::Runtime::ClusteringModule>();
    engine.EmplaceModule<Extrinsic::Runtime::EditorUiModule>();
    engine.EmplaceModule<Extrinsic::Runtime::SceneDocumentModule>();
    engine.EmplaceModule<Extrinsic::Runtime::SceneInteractionModule>();
    engine.EmplaceModule<Extrinsic::Runtime::AssetWorkflowModule>();
    engine.EmplaceModule<Extrinsic::Runtime::TextureBakeModule>();
    engine.Initialize();
    Extrinsic::Sandbox::SandboxSession sandbox{};
    sandbox.Initialize(config, engine.Worlds(), engine.Services());
    engine.Run();
    engine.BeginShutdown();
    if (capture != nullptr)
    {
        capture->CaptureLatest();
    }
    sandbox.Shutdown();
    engine.Shutdown();

    if (cli.Capture.Enabled)
    {
        const auto& samples = captureState.Samples;
        if (samples.empty())
        {
            std::cerr << "frame-pacing capture produced no samples\n";
            return 4;
        }
        if (!WriteFramePacingReport(cli.Capture.ReportPath, cli.Capture.TargetFrames,
                                    captureState.FinalDeviceOperational, samples))
        {
            return 3;
        }
        std::cout << "wrote frame-pacing report: "
                  << cli.Capture.ReportPath << '\n';
    }

    return 0;
}
