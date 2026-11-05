// ARCH-022: kernel density as a plugin. For each live point it writes a Gaussian kernel density estimate over its
// nearest candidates (bandwidth given or from the nearest-other spacing) into a scalar output, with one undo entry;
// on CPU octree or CPU LBVH neighbourhoods (any scalar storage, checked conversion), or on the GPU (Vulkan LBVH, float
// storage) with a preview that waits for Accept or Discard. The plugin owns its config section, its MCP tools and its
// window; the point-cloud editing service does the capture, the GPU run and the publication.
module;

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <string_view>

export module Intrinsic.Plugin.KernelDensity;

export import Extrinsic.Runtime.DerivedConfigSections;
export import Extrinsic.Runtime.GeometryProperty.Types;
export import Extrinsic.Runtime.PointCloudEditing;
import Extrinsic.Runtime.AgentOperations;
import Extrinsic.Runtime.PluginContract;

export namespace Intrinsic::Plugins::KernelDensity
{
    namespace R = Extrinsic::Runtime;

    // The section keeps the name, schema id and payload it had before it moved into the plugin, so saved configs load.
    inline constexpr std::string_view kSectionName = "sandbox.kernel_density";
    inline constexpr std::string_view kSectionSchemaId = "intrinsic.runtime.sandbox.kernel_density";

    enum class DensityBackend : std::uint8_t { CpuOctree, CpuLBVH, VulkanLBVH };
    [[nodiscard]] const char* ToString(DensityBackend backend) noexcept;

    struct Config
    {
        std::uint32_t StableEntityId{};
        DensityBackend Backend{DensityBackend::CpuOctree};
        R::GeometryPropertyRef Positions{.Domain = R::GeometryElementDomain::Unknown, .Name = "v:position",
                                         .ValueKind = Geometry::PropertyValueKind::Vec3};
        R::GeometryPropertyRef Density{.Domain = R::GeometryElementDomain::Unknown, .Name = "density",
                                       .ValueKind = Geometry::PropertyValueKind::Float};
        std::uint32_t KNeighbors{15}, GpuQueryBatchSize{4096};
        float Bandwidth{0}; // 0: from the nearest-other spacing
    };

    inline constexpr Geometry::PropertyValueKind kFloatKind[]{Geometry::PropertyValueKind::Float};
    inline constexpr Geometry::PropertyValueKind kVec3Kind[]{Geometry::PropertyValueKind::Vec3};
    inline constexpr std::string_view kBackendTokens[]{"cpu_octree", "cpu_lbvh", "vulkan_lbvh"};
    inline constexpr std::string_view kBackendNames[]{"CPU octree", "CPU LBVH (cached)", "Vulkan LBVH"};
    inline constexpr std::array kRows{
        R::Bind<&Config::StableEntityId>({.Name = "entity", .Type = R::ConfigFieldType::UInt, .Description = "Entity."}),
        R::Bind<&Config::KNeighbors>(
            {.Name = "k_neighbors", .Type = R::ConfigFieldType::UInt, .Description = "K neighbors."}),
        R::Bind<&Config::GpuQueryBatchSize>({.Name = "gpu_query_batch_size",
                                             .Type = R::ConfigFieldType::UInt,
                                             .Description = "Gpu query batch size.",
                                             .Min = 1,
                                             .Max = 16384,
                                             .Refusal = "GPU query batch size must be 1..16384."}),
        R::Bind<&Config::Backend>({.Name = "backend",
                                   .Type = R::ConfigFieldType::StringEnum,
                                   .Description = "Backend.",
                                   .EnumNames = kBackendNames,
                                   .EnumValues = kBackendTokens,
                                   .Refusal = "Unknown density backend."}),
        R::Bind<&Config::Bandwidth>({.Name = "bandwidth",
                                     .Type = R::ConfigFieldType::Float,
                                     .Description = "Zero selects automatic bandwidth; positive values must remain "
                                                    "positive in float storage.",
                                     .Min = 0,
                                     .Max = double(std::numeric_limits<float>::max()),
                                     .Refusal = "Positive bandwidth must remain positive in float storage."}),
        R::Bind<&Config::Positions>({.Name = "positions",
                                     .Type = R::ConfigFieldType::PropertyRef,
                                     .Description = "Position and density bindings must have distinct names on the "
                                                    "same element domain.",
                                     .RefKinds = kVec3Kind}),
        R::Bind<&Config::Density>({.Name = "density",
                                 .Type = R::ConfigFieldType::PropertyRef,
                                 .Description = "Density.",
                                 .RefKinds = kFloatKind,
                                 .AnyScalar = true}),
    };
    inline constexpr auto kFields = R::SpecsOf(kRows);
    inline constexpr R::DerivedConfigSection<Config> kSection{
        .Name = kSectionName,
        .SchemaId = kSectionSchemaId,
        .Title = "KernelDensity",
        .Rows = kRows,
        .Fields = kFields,
        .ObjectError = "Kernel density config must be an object.",
        .UnknownFieldPrefix = "Unknown density field: ",
        .Check = [](const Config& c) -> std::string_view
        {
            return c.Positions.Domain != c.Density.Domain || c.Positions.Name == c.Density.Name
                ? "Position and density must be distinct properties on the same domain."
                : "";
        }};

    // A run's outcome. Status: pending, applied, invalid, busy, failed, stale, discarded or cancelled.
    struct Result
    {
        std::string Status{"pending"};
        std::string Message{};
        DensityBackend RequestedBackend{DensityBackend::CpuOctree};
        std::string ActualBackend{};
        R::GeometryPropertyRef Density{};
        std::size_t SlotCount{}, LiveCount{}, WrittenCount{};
        float UsedBandwidth{}, MeanDensity{}, MinDensity{}, MaxDensity{};
        std::uint64_t GpuInputUploadBytes{}, GpuInputCacheHits{}, CpuStageReadbackBytes{};
        bool IndexReused{};
        std::size_t GpuQueryBatches{};
        double CpuComputeMilliseconds{};
    };

    // A run, shared by the window and a waiting MCP call. A Vulkan run is the service's GPU run (`Analysis`); its
    // state reaches `Final` through Refresh. `Session` is the editor session it belongs to.
    struct Run : R::PluginRun<Result>
    {
        R::ScalarAnalysis Analysis{};
        R::PointSession Session{};
    };

    // Readiness: empty when a run can start, else why not (copies no values).
    [[nodiscard]] std::string Preview(const Config& config);
    // Starts a run (main thread). An immediate failure sets `Final` at once. A Vulkan run waits for Accept or Discard
    // unless `autoAccept`.
    [[nodiscard]] std::shared_ptr<Run> Start(const Config& config, bool autoAccept);
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
