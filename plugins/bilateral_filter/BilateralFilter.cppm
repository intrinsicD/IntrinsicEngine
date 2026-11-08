// ARCH-022: the bilateral point filter as a plugin. It moves each live point along its fixed normal by the
// bilateral average of its neighbours' offsets (Geometry.PointCloud.Utils), on CPU octree, CPU LBVH, Vulkan LBVH or
// Vulkan brute-force neighbourhoods, and writes the result to an output property with one undo entry. ARCH-023: a run
// is a step run, one filter pass a step, controlled by the host's run row (the window) and run_control (MCP), with the
// shared execution options; nothing is published before Accept. A CPU backend steps on a worker; a Vulkan backend is
// a hybrid driven by the plugin (each pass: the GPU kNN rows on the main thread, then the CPU update on a worker). The
// plugin owns its config section, its MCP tools and its window; the point-cloud editing service does the capture, the
// neighbourhood rows, the preview and the publication.
module;

#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <string_view>

export module Intrinsic.Plugin.BilateralFilter;

export import Extrinsic.Runtime.DerivedConfigSections;
export import Extrinsic.Runtime.GeometryProperty.Types;
export import Extrinsic.Runtime.StepRunExecution;
import Extrinsic.Runtime.AgentOperations;
import Extrinsic.Runtime.PluginContract;

export namespace Intrinsic::Plugins::BilateralFilter
{
    namespace R = Extrinsic::Runtime;

    // The section keeps the name and schema id it had before it moved into the plugin; ARCH-023 replaced its
    // `iterations` field by the shared execution options.
    inline constexpr std::string_view kSectionName = "sandbox.bilateral_filter";
    inline constexpr std::string_view kSectionSchemaId = "intrinsic.runtime.sandbox.bilateral_filter";

    // The Vulkan backends share one paged kNN path and differ only in the GPU query strategy.
    enum class FilterBackend : std::uint8_t
    {
        CpuOctree,
        CpuLBVH,
        VulkanLBVH,
        VulkanBruteForce
    };
    [[nodiscard]] const char* ToString(FilterBackend backend) noexcept;
    [[nodiscard]] constexpr bool IsVulkan(const FilterBackend backend) noexcept
    {
        return backend == FilterBackend::VulkanLBVH || backend == FilterBackend::VulkanBruteForce;
    }

    struct Config
    {
        std::uint32_t StableEntityId{};
        FilterBackend Backend{FilterBackend::CpuOctree};
        R::GeometryPropertyRef Positions{.Domain = R::GeometryElementDomain::Unknown, .Name = "v:position",
                                         .ValueKind = Geometry::PropertyValueKind::Vec3};
        R::GeometryPropertyRef Normals{.Domain = R::GeometryElementDomain::Unknown, .Name = "v:normal",
                                       .ValueKind = Geometry::PropertyValueKind::Vec3};
        R::GeometryPropertyRef Output{.Domain = R::GeometryElementDomain::Unknown, .Name = "filtered_positions",
                                      .ValueKind = Geometry::PropertyValueKind::Vec3};
        std::uint32_t KNeighbors{15}, GpuQueryBatchSize{4096};
        float SpatialSigma{}, NormalSigma{.25f}; // spatial sigma 0: twice the average spacing
        // The shared execution options: max_iterations filter passes (one step each); live preview shows the moving
        // points only for an in-place output (output = positions) on an operational device. A run computes at once
        // and waits for Accept by default.
        R::StepRunExecution Execution{.IterationCap = 1u,
            .PreviewEvery = 1u,
            .PreviewIntervalMs = 33u,
            .AutoAccept = R::StepRunAutoAccept::Never,
            .Start = R::StepRunStart::Run};
    };
    inline constexpr std::uint32_t kMinIterations = 0u; // the initialized state is the zero-pass result
    inline constexpr std::uint32_t kMaxIterations = 100u; // filter passes of one run

    inline constexpr Geometry::PropertyValueKind kVec3Kind[]{Geometry::PropertyValueKind::Vec3};
    inline constexpr std::string_view kBackendTokens[]{"cpu_octree", "cpu_lbvh", "vulkan_lbvh", "vulkan_brute_force"};
    inline constexpr std::string_view kBackendNames[]{
        "CPU octree", "CPU LBVH (cached)", "Vulkan LBVH", "Vulkan brute force"};
    inline constexpr auto kExecutionRows = R::BindStepRunExecution<&Config::Execution>(kMaxIterations, kMinIterations);
    inline constexpr std::array kRows{
        R::Bind<&Config::StableEntityId>(
            {.Name = "entity", .Type = R::ConfigFieldType::UInt, .Description = "Entity."}),
        R::Bind<&Config::KNeighbors>(
            {.Name = "k_neighbors", .Type = R::ConfigFieldType::UInt, .Description = "K neighbors."}),
        R::Bind<&Config::GpuQueryBatchSize>({.Name = "gpu_query_batch_size",
            .Type = R::ConfigFieldType::UInt,
            .Description = "Gpu query batch size.",
            .Min = 1,
            .Max = 16384,
            .Refusal = "GPU query batch size must be 1..16384."}),
        kExecutionRows[0],
        kExecutionRows[1],
        kExecutionRows[2],
        kExecutionRows[3],
        kExecutionRows[4],
        kExecutionRows[5],
        R::Bind<&Config::Backend>({.Name = "backend",
            .Type = R::ConfigFieldType::StringEnum,
            .Description = "Backend.",
            .EnumNames = kBackendNames,
            .EnumValues = kBackendTokens,
            .Refusal = "Unknown bilateral backend."}),
        R::Bind<&Config::SpatialSigma>({.Name = "spatial_sigma",
            .Type = R::ConfigFieldType::Float,
            .Description = "Zero selects automatic spatial sigma; positive values must "
                           "remain positive in float storage.",
            .Min = 0,
            .Max = double(std::numeric_limits<float>::max()),
            .Refusal = "spatial_sigma must remain positive in float storage."}),
        R::Bind<&Config::NormalSigma>({.Name = "normal_sigma",
            .Type = R::ConfigFieldType::Float,
            .Description = "Nonnegative normal sigma; positive values must remain positive "
                           "in float storage. The method floors it to 1e-6.",
            .Min = 0,
            .Max = double(std::numeric_limits<float>::max()),
            .Refusal = "normal_sigma must remain positive in float storage."}),
        R::Bind<&Config::Positions>({.Name = "positions",
            .Type = R::ConfigFieldType::PropertyRef,
            .Description = "Positions, normals and output must share an element domain.",
            .RefKinds = kVec3Kind}),
        R::Bind<&Config::Normals>({.Name = "normals",
            .Type = R::ConfigFieldType::PropertyRef,
            .Description = "Normals.",
            .RefKinds = kVec3Kind}),
        R::Bind<&Config::Output>({.Name = "output",
            .Type = R::ConfigFieldType::PropertyRef,
            .Description = "May replace positions; cannot overwrite a normal input distinct from "
                           "the positions input.",
            .RefKinds = kVec3Kind}),
    };
    inline constexpr auto kFields = R::SpecsOf(kRows);
    inline constexpr R::DerivedConfigSection<Config> kSection{
        .Name = kSectionName,
        .SchemaId = kSectionSchemaId,
        .Title = "BilateralFilter",
        .Rows = kRows,
        .Fields = kFields,
        .ObjectError = "Bilateral filter config must be an object.",
        .UnknownFieldPrefix = "Unknown bilateral field: ",
        .Check = [](const Config& c) -> std::string_view
        {
            if (c.Positions.Domain != c.Output.Domain || c.Positions.Domain != c.Normals.Domain)
                return "Positions, normals and output must share an element domain.";
            if (c.Output.Name == c.Normals.Name && c.Positions.Name != c.Normals.Name)
                return "Output cannot overwrite a distinct normal input.";
            return "";
        }};

    // A run's outcome. Status: pending, applied, invalid, failed, stale, discarded or cancelled. The diagnostics describe
    // the last pass over the live samples; the GPU byte counts are the host traffic of the Vulkan neighbourhoods (the
    // queries and the moving points written, the packed rows read back), the positions' update itself runs on the CPU.
    struct Result
    {
        std::string Status{"pending"};
        std::string Message{};
        FilterBackend RequestedBackend{FilterBackend::CpuOctree};
        std::string ActualBackend{};
        R::GeometryPropertyRef Output{};
        std::size_t SlotCount{}, LiveCount{}, WrittenCount{}, PointsFiltered{}, DegenerateNormals{};
        float AverageDisplacement{}, MaxDisplacement{}, SpatialSigmaUsed{};
        std::uint32_t CompletedIterations{};
        std::size_t GpuQueryBatches{}, WorkspaceBuilds{};
        std::uint64_t GpuQueryUploadBytes{}, GpuWorkspaceUploadBytes{}, GpuReadbackBytes{};
        bool IndexReused{};
        double CpuComputeMilliseconds{}, GpuNeighborhoodMilliseconds{};
        std::uint64_t StepRun{}; // the run's step run (run_control, run_observe); 0: it never started
    };

    // A run, shared by the window and a waiting MCP call (the contract's run protocol): `Final` once its step run
    // ended (applied at its Accept), `Handle` the step run's holder job.
    using Run = R::PluginRun<Result>;

    // Readiness: empty when a run can start, else why not (copies no values).
    [[nodiscard]] std::string Preview(const Config& config);
    // Starts a step run with `config.Execution` (main thread): ready, or computing at once. An immediate failure sets
    // `Final` at once; `Pending.StepRun` names the run otherwise.
    [[nodiscard]] std::shared_ptr<Run> Start(const Config& config);
    // The run started last (window or MCP), for the window's run row; null before the first.
    [[nodiscard]] std::shared_ptr<Run> LastRun() noexcept;

    // The plugin's entry points (the v1 symbols forward to them; static builds call them in-process).
    [[nodiscard]] bool Register(R::PluginRegistrations& staged, std::string& error) noexcept;
    void Shutdown() noexcept;

    // Its parts, registered by Register.
    void AddTools(R::PluginRegistrations& staged);
    void AddWindow(R::PluginRegistrations& staged);
    void ShutdownWindow() noexcept;
    void ShutdownRuns() noexcept; // forgets LastRun
}
