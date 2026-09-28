// sandbox.point_sampling section codec (RUNTIME-274): base fields plus the shared sampling
// block without a prefix, declared once in field specs for validation, schema and panel.
module;
#include <array>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <nlohmann/json.hpp>
module Extrinsic.Runtime.PointSamplingConfig;

import Geometry.Properties.Types;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;

#include "Config/internal/Runtime.PointConfigJson.hpp"
#include "Config/internal/Runtime.ConfigFieldJson.hpp"
#include "Config/internal/Runtime.PointSamplingConfigJson.hpp"

namespace Extrinsic::Runtime
{
    namespace
    {
        using Json = nlohmann::json;
        using FT = ConfigFieldType;
        using K = Geometry::PropertyValueKind;
        constexpr std::array<K, 1> kVec3{K::Vec3};
        constexpr std::array<std::string_view, 2> kOutputNames{"Rank and selection properties", "New point cloud"};
        constexpr std::array kBaseFields{
            ConfigFieldSpec{.Name = "source", .Type = FT::UInt, .Description = "Stable id of the entity whose points are sampled."},
            ConfigFieldSpec{.Name = "positions", .Type = FT::PropertyRef, .Description = "Points: any vec3 property on a point domain (Unknown picks the primary one).", .RefKinds = kVec3},
            ConfigFieldSpec{.Name = "count", .Type = FT::UInt, .Description = "Samples to select; 0 orders every point.", .Min = 0, .Max = 100000000},
            ConfigFieldSpec{.Name = "weights", .Type = FT::String, .Description = "Optional float property on the points' domain: importance weights (farthest point, coupled sieve; positive) or priority scores (Poisson feature priority). Empty for none."},
            ConfigFieldSpec{.Name = "output", .Type = FT::Enum, .Description = "Where the samples go: rank and selection properties on the source, or a new point-cloud entity.", .EnumNames = kOutputNames},
            ConfigFieldSpec{.Name = "rank_name", .Type = FT::String, .Description = "Float property with each point's rank in the order (-1 when not ranked).", .NonEmpty = true},
            ConfigFieldSpec{.Name = "selected_name", .Type = FT::String, .Description = "Bool property marking the first `count` samples.", .NonEmpty = true},
        };

        std::span<const ConfigFieldSpec> Fields()
        {
            static const std::vector<ConfigFieldSpec> fields = [] {
                std::vector<ConfigFieldSpec> all(kBaseFields.begin(), kBaseFields.end());
                const auto block = PointSamplingFieldSpecs("");
                all.insert(all.end(), block.begin(), block.end());
                return all;
            }();
            return fields;
        }

        Json Encode(const PointSamplingOperationConfig& c)
        {
            Json doc{{"source", c.SourceStableEntityId}, {"positions", ConfigDetail::EncodePointPropertyRef(c.Positions)},
                     {"count", c.Count}, {"weights", c.WeightsName}, {"output", unsigned(c.Output)},
                     {"rank_name", c.RankName}, {"selected_name", c.SelectedName}};
            ConfigDetail::EncodePointSampling(doc, "", c.Sampling);
            return doc;
        }

        PointSamplingOperationConfig Decode(const Json& doc)
        {
            PointSamplingOperationConfig c;
            c.SourceStableEntityId = doc.at("source").get<std::uint32_t>();
            ConfigDetail::DecodePointPropertyRef(doc.at("positions"), c.Positions);
            c.Count = doc.at("count").get<std::uint32_t>();
            c.WeightsName = doc.at("weights").get<std::string>();
            c.Output = PointSamplingOutput(doc.at("output").get<unsigned>());
            c.RankName = doc.at("rank_name").get<std::string>();
            c.SelectedName = doc.at("selected_name").get<std::string>();
            ConfigDetail::DecodePointSampling(doc, "", c.Sampling);
            return c;
        }

        Core::Config::EngineConfigSection Section(const PointSamplingOperationConfig& c)
        {
            return {.Name = std::string{kPointSamplingConfigSectionName},
                    .SchemaId = std::string{kPointSamplingConfigSectionSchemaId}, .SchemaVersion = 1u,
                    .PayloadJson = SerializePointSamplingOperationConfig(c)};
        }
    }

    std::string SerializePointSamplingOperationConfig(const PointSamplingOperationConfig& config)
    {
        return ConfigDetail::SerializeConfigJson(Encode(config));
    }

    Core::Config::EngineConfigSectionValidationResult ValidatePointSamplingOperationConfigSection(
        std::string_view payload, std::string_view, std::string_view subject)
    {
        const auto input = ConfigDetail::ParseConfigJson(payload, false);
        auto merged = Encode({});
        if (auto error = ConfigDetail::ValidateDeclaredFields(input, merged, Fields(),
                "Point sampling config must be an object.", "Unknown point sampling field: "))
            return ConfigDetail::RejectConfigSection(subject, *error);
        const auto c = Decode(merged);
        const auto reject = [&](std::string message) { return ConfigDetail::RejectConfigSection(subject, std::move(message)); };
        const bool priority = c.Sampling.Method == PointSamplingMethod::ProgressivePoisson &&
                              c.Sampling.PoissonSelection == PointSamplingPoissonSelection::FeaturePriority;
        if (priority && c.WeightsName.empty())
            return reject("Feature-priority Poisson sampling needs a score property (weights).");
        if (!priority)
            if (auto error = ValidatePointSamplingConfig(c.Sampling)) return reject(*error);
        if (c.RankName == c.SelectedName || c.RankName == c.Positions.Name || c.SelectedName == c.Positions.Name)
            return reject("The rank and selection properties need their own names.");
        if (IsTopologyProperty(c.Positions.Domain, c.RankName) || IsTopologyProperty(c.Positions.Domain, c.SelectedName))
            return reject("The rank and selection properties cannot replace topology or deletion data.");
        return {.State = Core::Config::EngineConfigState::Valid,
                .CanonicalPayloadJson = SerializePointSamplingOperationConfig(c),
                .ParsedFieldCount = static_cast<std::uint32_t>(input.is_object() ? input.size() : 0u)};
    }

    std::optional<PointSamplingOperationConfig> DecodePointSamplingOperationConfig(std::string_view payload)
    {
        const auto validated = ValidatePointSamplingOperationConfigSection(payload, {}, kPointSamplingConfigSectionName);
        if (!validated.Usable()) return std::nullopt;
        return Decode(ConfigDetail::ParseConfigJson(validated.CanonicalPayloadJson, false));
    }

    std::optional<PointSamplingOperationConfig> GetPointSamplingOperationConfig(const Core::Config::EngineConfig& config)
    {
        const auto payload = ConfigDetail::FindValidatedCanonicalPayload(
            config, kPointSamplingConfigSectionName, kPointSamplingConfigSectionSchemaId, 1u, nullptr,
            ValidatePointSamplingOperationConfigSection);
        if (!payload) return std::nullopt;
        return Decode(ConfigDetail::ParseConfigJson(*payload, false));
    }

    void SetPointSamplingOperationConfig(Core::Config::EngineConfig& config, const PointSamplingOperationConfig& value)
    {
        Core::Config::UpsertEngineConfigSection(config.AppSections, Section(value));
    }

    Core::Config::EngineConfigSectionRegistration MakePointSamplingConfigSectionRegistration()
    {
        return {.DefaultSection = Section({}), .Validate = ValidatePointSamplingOperationConfigSection,
                .SchemaJson = ConfigDetail::BuildSectionSchemaJson(kPointSamplingConfigSectionSchemaId, "Point Sampling",
                    "Orders an entity's points with a selectable sampling method (random, exact or relaxed farthest "
                    "point, progressive Poisson, greedy batches, sample elimination) and publishes the selection.",
                    Fields(), Encode({}))};
    }

    std::span<const ConfigFieldSpec> PointSamplingOperationFieldSpecs() noexcept { return Fields(); }
}
