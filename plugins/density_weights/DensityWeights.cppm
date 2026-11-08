// ARCH-022: compact density weights as a plugin. For each live point it writes 1 + the sum of a radial kernel over
// the other points within the support radius (given, or derived from the positions), or its reciprocal, into a
// scalar output with one undo entry; on CPU KD-tree or CPU LBVH radius candidates (any scalar storage, checked
// conversion), or on the GPU (Vulkan LBVH, float storage) with a preview that waits for Accept or Discard. ARCH-023:
// every backend runs as a one-shot step run (the whole estimate is its one step) with the host's run controls and the
// section's auto_accept and start. The plugin owns its config section, its MCP tools and its window; the point-cloud
// editing service does the capture, the runs and the publication.
module;

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <string_view>

export module Intrinsic.Plugin.DensityWeights;

export import Extrinsic.Runtime.DerivedConfigSections;
export import Extrinsic.Runtime.GeometryProperty.Types;
export import Extrinsic.Runtime.PointCloudEditing;
export import Geometry.PointCloud.Kernels.Types;
import Extrinsic.Runtime.AgentOperations;
import Extrinsic.Runtime.PluginContract;

export namespace Intrinsic::Plugins::DensityWeights
{
    namespace R = Extrinsic::Runtime;
    namespace Kernels = Geometry::PointCloud::Kernels;

    // The section keeps the name, schema id and payload it had before it moved into the plugin, so saved configs load.
    inline constexpr std::string_view kSectionName = "sandbox.density_weights";
    inline constexpr std::string_view kSectionSchemaId = "intrinsic.runtime.sandbox.density_weights";

    enum class DensityWeightBackend : std::uint8_t { CpuKDTree, CpuLBVH, VulkanLBVH };
    [[nodiscard]] const char* ToString(DensityWeightBackend backend) noexcept;

    struct Config
    {
        std::uint32_t StableEntityId{};
        DensityWeightBackend Backend{DensityWeightBackend::CpuKDTree};
        R::GeometryPropertyRef Positions{.Domain = R::GeometryElementDomain::Unknown, .Name = "v:position",
                                         .ValueKind = Geometry::PropertyValueKind::Vec3};
        R::GeometryPropertyRef Weights{.Domain = R::GeometryElementDomain::Unknown, .Name = "density_weight",
                                       .ValueKind = Geometry::PropertyValueKind::Float};
        double SupportRadius{0}; // 0: derived from the selected positions
        Kernels::KernelType Kernel{Kernels::KernelType::ThetaLop};
        Kernels::DensityWeightMode Mode{Kernels::DensityWeightMode::Direct};
        std::uint32_t GpuQueryBatchSize{4096}, GpuRadiusCapacity{256};
        // The one-shot options (rows auto_accept and start; one step, no live preview), by default every one-shot's
        // interactive start: ready (nothing computed before run or step), waiting for Accept.
        R::StepRunExecution Execution{R::kOneShotStepRunExecution};
    };

    inline constexpr Geometry::PropertyValueKind kFloatKind[]{Geometry::PropertyValueKind::Float};
    inline constexpr Geometry::PropertyValueKind kVec3Kind[]{Geometry::PropertyValueKind::Vec3};
    inline constexpr std::string_view kBackendTokens[]{"cpu_kdtree", "cpu_lbvh", "vulkan_lbvh"};
    inline constexpr std::string_view kBackendNames[]{"CPU KD-tree", "CPU LBVH (cached)", "Vulkan LBVH"};
    inline constexpr std::string_view kKernelTokens[]{"gaussian", "theta_lop", "wendland_c2"};
    inline constexpr std::string_view kModeTokens[]{"direct", "reciprocal"};
    inline constexpr std::string_view kCapacityRefusal =
        "GPU query batch must be 1..16384 and complete radius capacity 1..1024.";
    inline constexpr auto kExecutionRows = R::BindStepRunExecution<&Config::Execution>(1u);
    inline constexpr std::array kRows{
        R::Bind<&Config::StableEntityId>(
            {.Name = "entity", .Type = R::ConfigFieldType::UInt, .Description = "Entity."}),
        R::Bind<&Config::GpuQueryBatchSize>({.Name = "gpu_query_batch_size",
            .Type = R::ConfigFieldType::UInt,
            .Description = "Gpu query batch size.",
            .Min = 1,
            .Max = 16384,
            .Refusal = kCapacityRefusal}),
        R::Bind<&Config::GpuRadiusCapacity>({.Name = "gpu_radius_capacity",
            .Type = R::ConfigFieldType::UInt,
            .Description = "Gpu radius capacity.",
            .Min = 1,
            .Max = 1024,
            .Refusal = kCapacityRefusal}),
        R::Bind<&Config::Backend>({.Name = "backend",
            .Type = R::ConfigFieldType::StringEnum,
            .Description = "Backend.",
            .EnumNames = kBackendNames,
            .EnumValues = kBackendTokens,
            .Refusal = "Unknown density weight backend."}),
        R::Bind<&Config::Kernel>({.Name = "kernel",
            .Type = R::ConfigFieldType::StringEnum,
            .Description = "Kernel.",
            .EnumValues = kKernelTokens,
            .Refusal = "Unknown radial kernel."}),
        R::Bind<&Config::Mode>({.Name = "mode",
            .Type = R::ConfigFieldType::StringEnum,
            .Description = "Mode.",
            .EnumValues = kModeTokens,
            .Refusal = "Unknown density weight mode."}),
        R::Bind<&Config::SupportRadius>({.Name = "support_radius",
            .Type = R::ConfigFieldType::Float,
            .Description = "Support radius; 0 derives it from the selected positions (support-radius policy), "
                           "positive values are source units.",
            .Min = 0,
            .Max = double(std::numeric_limits<float>::max())}),
        R::Bind<&Config::Positions>({.Name = "positions",
            .Type = R::ConfigFieldType::PropertyRef,
            .Description = "Position and weight bindings must have distinct names on the "
                           "same element domain.",
            .RefKinds = kVec3Kind}),
        R::Bind<&Config::Weights>({.Name = "weights",
            .Type = R::ConfigFieldType::PropertyRef,
            .Description = "Weights.",
            .RefKinds = kFloatKind,
            .AnyScalar = true}),
        kExecutionRows[3], // auto_accept
        kExecutionRows[4], // start
    };
    inline constexpr auto kFields = R::SpecsOf(kRows);
    inline constexpr R::DerivedConfigSection<Config> kSection{
        .Name = kSectionName,
        .SchemaId = kSectionSchemaId,
        .Title = "DensityWeight",
        .Rows = kRows,
        .Fields = kFields,
        .ObjectError = "Density weight config must be an object.",
        .UnknownFieldPrefix = "Unknown density weight field: ",
        .Check = [](const Config& c) -> std::string_view
        {
            return c.Positions.Domain != c.Weights.Domain || c.Positions.Name == c.Weights.Name
                ? "Position and weight bindings need distinct canonical vec3/float properties on the same domain."
                : "";
        }};

    // A run's outcome. Status: pending, applied, invalid, busy, failed, stale, discarded or cancelled.
    struct Result
    {
        std::string Status{"pending"};
        std::string Message{};
        DensityWeightBackend RequestedBackend{DensityWeightBackend::CpuKDTree};
        std::string ActualBackend{};
        R::GeometryPropertyRef Weights{};
        std::size_t SlotCount{}, LiveCount{}, WrittenCount{};
        Kernels::DensityWeightDiagnostics Diagnostics{};
        double SupportRadius{}; // the radius used, in source-property units (given, or derived when 0)
        float QueryRadius{}, MinWeight{}, MaxWeight{};
        std::size_t MaximumNeighbors{};
        std::uint64_t GpuInputUploadBytes{}, GpuInputCacheHits{}, CpuStageReadbackBytes{};
        bool IndexReused{};
        std::size_t GpuQueryBatches{};
        double CpuComputeMilliseconds{};
        std::uint64_t StepRun{}; // the run's step run (run_control, run_observe); 0: it never started
    };

    // A run, shared by the window and a waiting MCP call. A Vulkan run is the service's GPU run (`Analysis`), whose
    // state reaches `Final` through Refresh; a CPU run is the service's ScalarCpuRun (`Pending.StepRun`, `Handle` its
    // holder job), which sets `Final` itself. `Session` is the editor session it belongs to; `Row` the window's
    // run-control row state.
    struct Run : R::PluginRun<Result>
    {
        R::ScalarAnalysis Analysis{};
        R::PointSession Session{};
        R::StepRunControlsState Row{};
    };

    // Readiness: empty when a run can start, else why not (copies no values).
    [[nodiscard]] std::string Preview(const Config& config);
    // Starts a one-shot step run with `config.Execution` (main thread): ready (nothing computed before run or step),
    // or computing at once; then it waits for Accept or Discard unless auto_accept. An immediate failure sets `Final`
    // at once.
    [[nodiscard]] std::shared_ptr<Run> Start(const Config& config);
    // Main thread: folds a Vulkan run's state into `Pending`, and into `Final` once it ended.
    void Refresh(Run& run);
    void Accept(Run& run);
    // Discards a Vulkan run's preview, or cancels a CPU run; false when nothing was active.
    bool Cancel(Run& run);
    // The newest run either the window or a tool started (a busy refusal excepted), while its session is current.
    [[nodiscard]] std::shared_ptr<Run> Latest();
    // The runs not yet ended, oldest first.
    [[nodiscard]] std::span<const std::shared_ptr<Run>> Active();
    // Forgets `run` as the latest (the window's Dismiss); another run stays.
    void Dismiss(const std::shared_ptr<Run>& run);

    // The plugin's entry points (the v1 symbols forward to them; static builds call them in-process).
    [[nodiscard]] bool Register(R::PluginRegistrations& staged, std::string& error) noexcept;
    void Shutdown() noexcept;

    // Its parts, registered by Register.
    void AddTools(R::PluginRegistrations& staged);
    void AddWindow(R::PluginRegistrations& staged);
    void ShutdownRuns() noexcept;
    void ShutdownWindow() noexcept;
}
