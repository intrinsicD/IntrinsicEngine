// ARCH-022: the FPFH descriptor analysis as a plugin. It computes the 33-bin fast point feature histogram of every
// live point (Geometry.PointCloud.Features) on CPU k-d tree, CPU LBVH or Vulkan LBVH radius neighbourhoods and
// writes the bins to 33 scalar outputs with one undo entry. ARCH-023: a run is a one-step step run (the whole
// analysis is its one logical step) with the host's run row and run_control; its 33 columns wait for Accept, nothing
// is published before. It has no live preview and no iteration cap, so its execution options are auto_accept and
// start. The CPU backends step on a worker; Vulkan LBVH is a hybrid the plugin drives (the scale on a worker, the
// radius rows on the device, read back to the CPU, then the histograms on a worker). The plugin owns its config
// section, its MCP tools and its window; the point-cloud editing service does the capture, the neighbourhood rows and
// the publication.
module;

#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <string_view>

export module Intrinsic.Plugin.DescriptorAnalysis;

export import Extrinsic.Runtime.DerivedConfigSections;
export import Extrinsic.Runtime.GeometryProperty.Types;
export import Extrinsic.Runtime.StepRunExecution;
import Extrinsic.Runtime.PluginContract;

export namespace Intrinsic::Plugins::DescriptorAnalysis
{
    namespace R = Extrinsic::Runtime;

    // The section keeps the name, schema id and payload it had as a built-in feature, so saved configs load.
    inline constexpr std::string_view kSectionName = "sandbox.descriptor_analysis";
    inline constexpr std::string_view kSectionSchemaId = "intrinsic.runtime.sandbox.descriptor_analysis";
    inline constexpr std::size_t kBins = 33; // three 11-bin histograms: alpha, phi, theta

    enum class DescriptorBackend : std::uint8_t { CpuKDTree, CpuLBVH, VulkanLBVH };
    [[nodiscard]] const char* ToString(DescriptorBackend backend) noexcept;

    // prefix.alpha0 .. prefix.alpha10, prefix.phi0 .., prefix.theta0 .. as float columns on `domain`.
    [[nodiscard]] std::array<R::GeometryPropertyRef, kBins> MakeOutputs(
        R::GeometryElementDomain domain = R::GeometryElementDomain::Unknown, std::string_view prefix = "fpfh");

    struct Config
    {
        std::uint32_t StableEntityId{};
        DescriptorBackend Backend{DescriptorBackend::CpuKDTree};
        R::GeometryPropertyRef Positions{.Domain = R::GeometryElementDomain::Unknown, .Name = "v:position",
                                         .ValueKind = Geometry::PropertyValueKind::Vec3};
        R::GeometryPropertyRef Normals{.Domain = R::GeometryElementDomain::Unknown, .Name = "v:normal",
                                       .ValueKind = Geometry::PropertyValueKind::Vec3};
        std::array<R::GeometryPropertyRef, kBins> Outputs{MakeOutputs()};
        std::uint32_t MaxNeighbors{}, GpuQueryBatchSize{4096}, GpuRadiusCapacity{256};
        float FeatureRadius{}; // 0: five times the mean nearest-neighbour spacing
        // The shared execution options of a one-step run: one step, no live preview (both fixed, not config rows);
        // auto_accept and start are rows, by default every one-shot's: ready, waiting for Accept.
        R::StepRunExecution Execution{R::kOneShotStepRunExecution};
    };

    inline constexpr Geometry::PropertyValueKind kVec3Kind[]{Geometry::PropertyValueKind::Vec3};
    inline constexpr Geometry::PropertyValueKind kFloatKind[]{Geometry::PropertyValueKind::Float};
    inline constexpr std::string_view kBackendTokens[]{"cpu_kdtree", "cpu_lbvh", "vulkan_lbvh"};
    inline constexpr std::string_view kBackendNames[]{"CPU KD-tree", "CPU LBVH (cached)", "Vulkan LBVH"};
    // Scalar columns: float, double and checked integer storage are accepted (AnyScalar).
    inline constexpr R::ConfigFieldSpec kOutputItem{.Name = "output", .Type = R::ConfigFieldType::PropertyRef,
                                                    .RefKinds = kFloatKind, .AnyScalar = true};
    inline constexpr auto kExecutionRows = R::BindStepRunExecution<&Config::Execution>(1u);
    inline constexpr std::array kRows{
        R::Bind<&Config::StableEntityId>(
            {.Name = "entity", .Type = R::ConfigFieldType::UInt, .Description = "Entity."}),
        R::Bind<&Config::MaxNeighbors>(
            {.Name = "max_neighbors", .Type = R::ConfigFieldType::UInt, .Description = "Max neighbors."}),
        R::Bind<&Config::GpuQueryBatchSize>({.Name = "gpu_query_batch_size",
            .Type = R::ConfigFieldType::UInt,
            .Description = "Gpu query batch size.",
            .Min = 1,
            .Max = 16384,
            .Refusal = "GPU query batch size must be 1..16384."}),
        R::Bind<&Config::GpuRadiusCapacity>({.Name = "gpu_radius_capacity",
            .Type = R::ConfigFieldType::UInt,
            .Description = "Gpu radius capacity.",
            .Min = 1,
            .Max = 1024,
            .Refusal = "GPU radius capacity must be 1..1024."}),
        R::Bind<&Config::Backend>({.Name = "backend",
            .Type = R::ConfigFieldType::StringEnum,
            .Description = "Backend.",
            .EnumNames = kBackendNames,
            .EnumValues = kBackendTokens,
            .Refusal = "Unknown descriptor backend."}),
        R::Bind<&Config::FeatureRadius>({.Name = "feature_radius",
            .Type = R::ConfigFieldType::Float,
            .Description = "Zero selects an automatic radius; positive values must "
                           "remain positive in float storage.",
            .Min = 0,
            .Max = double(std::numeric_limits<float>::max()),
            .Refusal = "feature_radius must be zero (automatic) or a positive "
                       "representable float."}),
        R::Bind<&Config::Positions>({.Name = "positions",
            .Type = R::ConfigFieldType::PropertyRef,
            .Description = "Position and normal inputs must share an element domain.",
            .RefKinds = kVec3Kind}),
        R::Bind<&Config::Normals>({.Name = "normals",
            .Type = R::ConfigFieldType::PropertyRef,
            .Description = "Normals.",
            .RefKinds = kVec3Kind}),
        R::Bind<&Config::Outputs>({.Name = "outputs",
            .Type = R::ConfigFieldType::Array,
            .Description = "Exactly 33 uniquely named scalar outputs on the input domain, "
                           "distinct from both input names.",
            .Item = &kOutputItem}), // 33 items: the array's length
        kExecutionRows[3], // auto_accept
        kExecutionRows[4], // start
    };
    inline constexpr auto kFields = R::SpecsOf(kRows);
    inline constexpr R::DerivedConfigSection<Config> kSection{
        .Name = kSectionName,
        .SchemaId = kSectionSchemaId,
        .Title = "DescriptorAnalysis",
        .Rows = kRows,
        .Fields = kFields,
        .ObjectError = "Descriptor analysis config must be an object.",
        .UnknownFieldPrefix = "Unknown descriptor field: ",
        .Check = [](const Config& c) -> std::string_view
        {
            if (c.Positions.Domain != c.Normals.Domain)
                return "Positions and normals need canonical vec3 references on the same domain.";
            for (std::size_t i = 0; i < kBins; ++i)
            {
                const auto& output = c.Outputs[i];
                if (output.Domain != c.Positions.Domain || output.Name == c.Positions.Name ||
                    output.Name == c.Normals.Name)
                    return "Descriptor outputs must be float properties on the input domain, distinct from inputs.";
                for (std::size_t j = 0; j < i; ++j)
                    if (c.Outputs[j].Name == output.Name) return "Descriptor output names must be unique.";
            }
            return "";
        }};

    // A run's outcome. Status: pending, applied, invalid, failed, stale, discarded or cancelled. Spacing and radius are
    // in the positions' units; the GPU byte counts are the host traffic of the Vulkan radius rows (queries written,
    // packed rows read back).
    struct Result
    {
        std::string Status{"pending"};
        std::string Message{};
        DescriptorBackend RequestedBackend{DescriptorBackend::CpuKDTree};
        std::string ActualBackend{};
        std::array<R::GeometryPropertyRef, kBins> Outputs{};
        std::size_t SlotCount{}, LiveCount{}, WrittenCount{};
        float MeanSpacing{}, FeatureRadius{};
        std::size_t MaximumNeighbors{}; // the largest radius support found
        bool IndexReused{};
        std::size_t GpuQueryBatches{};
        std::uint64_t GpuQueryUploadBytes{}, GpuReadbackBytes{};
        double GpuNeighborhoodMilliseconds{}, CpuComputeMilliseconds{};
        std::uint64_t StepRun{}; // the run's step run (run_control, run_observe); 0: it never started
    };

    // A run, shared by the window and a waiting MCP call (the contract's run protocol): `Final` once its step run
    // ended (applied at its Accept), `Handle` the step run's holder job.
    using Run = R::PluginRun<Result>;

    // Readiness: empty when a run can start, else why not (copies no values).
    [[nodiscard]] std::string Preview(const Config& config);
    // Starts a one-step step run with `config.Execution` (main thread): ready, or computing at once. An immediate
    // failure sets `Final` at once; `Pending.StepRun` names the run otherwise.
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
