// ARCH-022: the bilateral point filter as a plugin. It moves each live point along its fixed normal by the
// bilateral average of its neighbours' offsets (Geometry.PointCloud.Utils), on CPU octree, CPU LBVH or Vulkan LBVH
// neighbourhoods, and writes the result to an output property with one undo entry. The plugin owns its config
// section, its MCP tools and its window; the point-cloud editing service does the capture and the publication.
module;

#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

export module Intrinsic.Plugin.BilateralFilter;

export import Extrinsic.Runtime.DerivedConfigSections;
export import Extrinsic.Runtime.GeometryProperty.Types;
import Extrinsic.Runtime.AgentOperations;
import Extrinsic.Runtime.PluginContract;

export namespace Intrinsic::Plugins::BilateralFilter
{
    namespace R = Extrinsic::Runtime;

    // The section keeps the name and schema id it had before it moved into the plugin, so saved configs load.
    inline constexpr std::string_view kSectionName = "sandbox.bilateral_filter";
    inline constexpr std::string_view kSectionSchemaId = "intrinsic.runtime.sandbox.bilateral_filter";

    enum class FilterBackend : std::uint8_t { CpuOctree, CpuLBVH, VulkanLBVH };
    [[nodiscard]] const char* ToString(FilterBackend backend) noexcept;

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
        std::uint32_t Iterations{1};
    };

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
        R::Bind<&Config::Iterations>({.Name = "iterations",
                                      .Type = R::ConfigFieldType::UInt,
                                      .Description = "Iterations.",
                                      .Min = 0,
                                      .Max = 100,
                                      .Refusal = "Bilateral iterations must be 0..100."}),
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
        R::Bind<&Config::Normals>(
            {.Name = "normals", .Type = R::ConfigFieldType::PropertyRef, .Description = "Normals.", .RefKinds = kVec3Kind}),
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

    // A run's outcome. Status: pending, applied, invalid, failed, stale or cancelled. The diagnostics describe the
    // last pass over the live samples.
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
        bool IndexReused{};
        double CpuComputeMilliseconds{}, GpuNeighborhoodMilliseconds{};
    };

    // A run, shared by the window and a waiting MCP call: `Final` is set exactly once, on the main thread.
    struct Run
    {
        Result Pending{};
        std::optional<Result> Final{};
        R::PluginJobHandle Handle{};
    };

    // Readiness: empty when a run can start, else why not (copies no values).
    [[nodiscard]] std::string Preview(const Config& config);
    // Starts a run (main thread). An immediate failure sets `Final` at once.
    [[nodiscard]] std::shared_ptr<Run> Start(const Config& config);
    // Cancels the run's jobs; false when nothing is active.
    bool Cancel(const Run& run);

    // The plugin's entry points (the v1 symbols forward to them; static builds call them in-process).
    [[nodiscard]] bool Register(R::PluginRegistrations& staged, std::string& error) noexcept;
    void Shutdown() noexcept;

    // Its parts, registered by Register.
    void AddTools(R::PluginRegistrations& staged);
    void AddWindow(R::PluginRegistrations& staged);
    void ShutdownWindow() noexcept;
}
