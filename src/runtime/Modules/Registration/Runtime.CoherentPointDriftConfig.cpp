module;
#include <array>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <nlohmann/json.hpp>
module Extrinsic.Runtime.CoherentPointDriftConfig;

import Geometry.Properties.Types;

#include "Config/internal/Runtime.PointConfigJson.hpp"
#include "Config/internal/Runtime.ConfigFieldJson.hpp"

namespace Extrinsic::Runtime
{
    namespace
    {
        using Json = nlohmann::json;
        using FT = ConfigFieldType;
        using K = Geometry::PropertyValueKind;
        constexpr std::array<K, 1> kVec3{K::Vec3};
        constexpr std::array<std::string_view, 4> kMethodNames{"Rigid (rotation, translation, optional scale)", "Affine",
                                                               "Nonrigid (coherent displacement field)",
                                                               "Bayesian (similarity plus coherent deformation, BCPD)"};
        constexpr std::array<std::string_view, 3> kOutputNames{"Source transform (rigid)", "Overwrite source positions",
                                                               "Displacement property"};
        constexpr std::array<std::string_view, 6> kEStepNames{"Reference (exact, single thread)", "Dense (exact, parallel)",
                                                              "Truncated (bounded error, parallel)",
                                                              "Auto (fast Gauss or dense while wide, then truncated)",
                                                              "Fast Gauss transform (bounded error, parallel)",
                                                              "Nystroem (approximate while wide, then exact)"};
        constexpr std::array kFields{
            ConfigFieldSpec{.Name = "source", .Type = FT::UInt, .Description = "Stable id of the moving entity."},
            ConfigFieldSpec{.Name = "target", .Type = FT::UInt, .Description = "Stable id of the fixed entity."},
            ConfigFieldSpec{.Name = "source_positions", .Type = FT::PropertyRef, .Description = "Moving points: any vec3 position property on a point domain (Unknown picks the primary one).", .RefKinds = kVec3},
            ConfigFieldSpec{.Name = "target_positions", .Type = FT::PropertyRef, .Description = "Fixed points: any vec3 position property on a point domain.", .RefKinds = kVec3},
            ConfigFieldSpec{.Name = "method", .Type = FT::Enum, .Description = "Transformation model fitted by EM.", .EnumNames = kMethodNames},
            ConfigFieldSpec{.Name = "outlier_weight", .Type = FT::Float, .Description = "Weight w of the uniform outlier component: 0 when every target point has a counterpart, 0.05-0.3 for clutter or partial overlap.", .Min = 0, .Max = 1, .ExclusiveMax = true},
            ConfigFieldSpec{.Name = "max_iterations", .Type = FT::UInt, .Description = "EM iteration cap.", .Min = 1, .Max = 10000},
            ConfigFieldSpec{.Name = "tolerance", .Type = FT::Float, .Description = "Stop when the objective changes by at most this fraction; 0 runs to the cap.", .Min = 0, .Max = 1},
            ConfigFieldSpec{.Name = "initial_sigma2", .Type = FT::Float, .Description = "Starting sigma^2 (normalized units); 0 derives it from the data.", .Min = 0},
            ConfigFieldSpec{.Name = "sigma2_floor", .Type = FT::Float, .Description = "Lower bound for sigma^2 (normalized units); reaching it ends the run.", .Min = 0, .Max = 1, .ExclusiveMin = true},
            ConfigFieldSpec{.Name = "normalize", .Type = FT::Bool, .Description = "Center both sets and scale them by one shared factor so beta, lambda and sigma^2 are unitless."},
            ConfigFieldSpec{.Name = "estimate_scale", .Type = FT::Bool, .Description = "Rigid: also fit a uniform scale."},
            ConfigFieldSpec{.Name = "allow_reflection", .Type = FT::Bool, .Description = "Rigid: allow a mirrored solution."},
            ConfigFieldSpec{.Name = "beta", .Type = FT::Float, .Description = "Nonrigid: Gaussian kernel width; larger values move points more jointly.", .Min = 0, .Max = 100, .ExclusiveMin = true},
            ConfigFieldSpec{.Name = "lambda", .Type = FT::Float, .Description = "Nonrigid: coherence weight; larger values give smoother motion.", .Min = 0, .Max = 1e6, .ExclusiveMin = true},
            ConfigFieldSpec{.Name = "output", .Type = FT::Enum, .Description = "Where the result is written; the source transform is available for rigid registration only.", .EnumNames = kOutputNames},
            ConfigFieldSpec{.Name = "displacement_name", .Type = FT::String, .Description = "Name of the vec3 displacement property written on the source domain.", .NonEmpty = true},
            ConfigFieldSpec{.Name = "e_step", .Type = FT::Enum, .Description = "How responsibilities are evaluated each iteration; every option reports its backend and error bound.", .EnumNames = kEStepNames},
            ConfigFieldSpec{.Name = "e_step_tolerance", .Type = FT::Float, .Description = "Truncated, fast Gauss and Auto: bound on each point's relative responsibility error.", .Min = 0, .Max = 1, .ExclusiveMin = true, .ExclusiveMax = true},
            ConfigFieldSpec{.Name = "threads", .Type = FT::UInt, .Description = "Worker threads for the parallel E-step; 0 uses all cores.", .Min = 0, .Max = 256},
            ConfigFieldSpec{.Name = "nystrom_landmarks", .Type = FT::UInt, .Description = "Nystroem: landmark points (half from each set); more landmarks keep the approximation accurate for narrower kernels.", .Min = 2, .Max = 4096},
            ConfigFieldSpec{.Name = "nystrom_error_limit", .Type = FT::Float, .Description = "Nystroem: largest sampled relative responsibility error accepted; iterations above it run exactly. An estimate from exact sample rows, not a bound.", .Min = 0, .Max = 1, .ExclusiveMin = true},
            ConfigFieldSpec{.Name = "low_rank", .Type = FT::UInt, .Description = "Nonrigid and Bayesian: 0 solves with the full kernel (at most 8192 source points); k > 0 uses k kernel eigenpairs and allows large sources.", .Min = 0, .Max = 2000},
            ConfigFieldSpec{.Name = "gamma", .Type = FT::Float, .Description = "Bayesian: factor on the data-derived initial sigma^2.", .Min = 0, .Max = 100, .ExclusiveMin = true},
            ConfigFieldSpec{.Name = "kappa", .Type = FT::Float, .Description = "Bayesian: Dirichlet concentration of the mixing weights; small values adapt them to uneven density, 0 keeps them equal.", .Min = 0, .Max = 1e6},
            ConfigFieldSpec{.Name = "subsample", .Type = FT::UInt, .Description = "Bayesian: register this many farthest-point samples and interpolate the deformation to every point (0 = all points, else at least 4).", .Min = 0, .Max = 1000000},
        };

        Json Encode(const CoherentPointDriftConfig& c)
        {
            return Json{{"source", c.SourceStableEntityId}, {"target", c.TargetStableEntityId},
                        {"source_positions", ConfigDetail::EncodePointPropertyRef(c.SourcePositions)},
                        {"target_positions", ConfigDetail::EncodePointPropertyRef(c.TargetPositions)},
                        {"method", unsigned(c.Method)}, {"outlier_weight", c.OutlierWeight},
                        {"max_iterations", c.MaxIterations}, {"tolerance", c.Tolerance},
                        {"initial_sigma2", c.InitialSigma2}, {"sigma2_floor", c.Sigma2Floor},
                        {"normalize", c.NormalizeInputs}, {"estimate_scale", c.EstimateScale},
                        {"allow_reflection", c.AllowReflection}, {"beta", c.Beta}, {"lambda", c.Lambda},
                        {"output", unsigned(c.Output)}, {"displacement_name", c.DisplacementName},
                        {"e_step", unsigned(c.EStep)}, {"e_step_tolerance", c.EStepTolerance},
                        {"threads", c.Threads}, {"nystrom_landmarks", c.NystromLandmarks},
                        {"nystrom_error_limit", c.NystromErrorLimit}, {"low_rank", c.LowRank}, {"gamma", c.Gamma}, {"kappa", c.Kappa},
                        {"subsample", c.Subsample}};
        }

        CoherentPointDriftConfig Decode(const Json& doc)
        {
            CoherentPointDriftConfig c;
            c.SourceStableEntityId = doc.at("source").get<std::uint32_t>();
            c.TargetStableEntityId = doc.at("target").get<std::uint32_t>();
            ConfigDetail::DecodePointPropertyRef(doc.at("source_positions"), c.SourcePositions);
            ConfigDetail::DecodePointPropertyRef(doc.at("target_positions"), c.TargetPositions);
            c.Method = CoherentPointDriftMethod(doc.at("method").get<unsigned>());
            c.OutlierWeight = doc.at("outlier_weight").get<double>();
            c.MaxIterations = doc.at("max_iterations").get<std::uint32_t>();
            c.Tolerance = doc.at("tolerance").get<double>();
            c.InitialSigma2 = doc.at("initial_sigma2").get<double>();
            c.Sigma2Floor = doc.at("sigma2_floor").get<double>();
            c.NormalizeInputs = doc.at("normalize").get<bool>();
            c.EstimateScale = doc.at("estimate_scale").get<bool>();
            c.AllowReflection = doc.at("allow_reflection").get<bool>();
            c.Beta = doc.at("beta").get<double>();
            c.Lambda = doc.at("lambda").get<double>();
            c.Output = CoherentPointDriftOutput(doc.at("output").get<unsigned>());
            c.DisplacementName = doc.at("displacement_name").get<std::string>();
            c.EStep = CoherentPointDriftEStep(doc.at("e_step").get<unsigned>());
            c.EStepTolerance = doc.at("e_step_tolerance").get<double>();
            c.Threads = doc.at("threads").get<std::uint32_t>();
            c.NystromLandmarks = doc.at("nystrom_landmarks").get<std::uint32_t>();
            c.NystromErrorLimit = doc.at("nystrom_error_limit").get<double>();
            c.LowRank = doc.at("low_rank").get<std::uint32_t>();
            c.Gamma = doc.at("gamma").get<double>();
            c.Kappa = doc.at("kappa").get<double>();
            c.Subsample = doc.at("subsample").get<std::uint32_t>();
            return c;
        }

        Core::Config::EngineConfigSection Section(const CoherentPointDriftConfig& c)
        {
            return {.Name = std::string{kCoherentPointDriftConfigSectionName},
                    .SchemaId = std::string{kCoherentPointDriftConfigSectionSchemaId}, .SchemaVersion = 1u,
                    .PayloadJson = SerializeCoherentPointDriftConfig(c)};
        }
    }

    std::string SerializeCoherentPointDriftConfig(const CoherentPointDriftConfig& config)
    {
        return ConfigDetail::SerializeConfigJson(Encode(config));
    }

    Core::Config::EngineConfigSectionValidationResult ValidateCoherentPointDriftConfigSection(
        std::string_view payload, std::string_view, std::string_view subject)
    {
        const auto input = ConfigDetail::ParseConfigJson(payload, false);
        auto merged = Encode({});
        if (auto error = ConfigDetail::ValidateDeclaredFields(input, merged, kFields,
                "Coherent Point Drift config must be an object.", "Unknown coherent point drift field: "))
            return ConfigDetail::RejectConfigSection(subject, *error);
        const auto c = Decode(merged);
        const auto reject = [&](std::string message) { return ConfigDetail::RejectConfigSection(subject, std::move(message)); };
        if (c.Output == CoherentPointDriftOutput::SourceTransform && c.Method != CoherentPointDriftMethod::Rigid)
            return reject("Affine and nonrigid results cannot be stored in the source transform; write positions or a displacement property.");
        if (c.Output == CoherentPointDriftOutput::DisplacementProperty &&
            (c.DisplacementName == c.SourcePositions.Name || IsTopologyProperty(c.SourcePositions.Domain, c.DisplacementName)))
            return reject("The displacement property needs its own name and cannot replace topology or deletion data.");
        if (c.SourceStableEntityId != 0u && c.SourceStableEntityId == c.TargetStableEntityId)
            return reject("Source and target must be different entities.");
        if (c.Subsample > 0u && c.Subsample < 4u)
            return reject("Subsample needs at least 4 points (or 0 for all points).");
        return {.State = Core::Config::EngineConfigState::Valid,
                .CanonicalPayloadJson = SerializeCoherentPointDriftConfig(c),
                .ParsedFieldCount = static_cast<std::uint32_t>(input.is_object() ? input.size() : 0u)};
    }

    std::optional<CoherentPointDriftConfig> DecodeCoherentPointDriftConfig(std::string_view payload)
    {
        const auto validated = ValidateCoherentPointDriftConfigSection(payload, {}, kCoherentPointDriftConfigSectionName);
        if (!validated.Usable()) return std::nullopt;
        return Decode(ConfigDetail::ParseConfigJson(validated.CanonicalPayloadJson, false));
    }

    std::optional<CoherentPointDriftConfig> GetCoherentPointDriftConfig(const Core::Config::EngineConfig& config)
    {
        const auto payload = ConfigDetail::FindValidatedCanonicalPayload(
            config, kCoherentPointDriftConfigSectionName, kCoherentPointDriftConfigSectionSchemaId, 1u, nullptr,
            ValidateCoherentPointDriftConfigSection);
        if (!payload) return std::nullopt;
        return Decode(ConfigDetail::ParseConfigJson(*payload, false));
    }

    void SetCoherentPointDriftConfig(Core::Config::EngineConfig& config, const CoherentPointDriftConfig& value)
    {
        Core::Config::UpsertEngineConfigSection(config.AppSections, Section(value));
    }

    Core::Config::EngineConfigSectionRegistration MakeCoherentPointDriftConfigSectionRegistration()
    {
        return {.DefaultSection = Section({}), .Validate = ValidateCoherentPointDriftConfigSection,
                .SchemaJson = ConfigDetail::BuildSectionSchemaJson(kCoherentPointDriftConfigSectionSchemaId,
                    "Coherent Point Drift",
                    "Probabilistic point-set registration (Myronenko & Song 2010): rigid, affine or nonrigid "
                    "alignment of a moving entity onto a fixed one.",
                    kFields, Encode({}))};
    }

    std::span<const ConfigFieldSpec> CoherentPointDriftConfigFieldSpecs() noexcept { return kFields; }
}
