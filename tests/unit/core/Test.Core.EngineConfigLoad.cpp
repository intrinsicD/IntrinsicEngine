#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Core.Config.Render;
import Extrinsic.Core.Config.Window;

using namespace Extrinsic::Core::Config;

namespace
{
    inline constexpr std::string_view kFakeSectionName = "test.fake";
    inline constexpr std::string_view kFakeSchemaId = "test.fake-config";
    inline constexpr std::uint32_t kFakeSchemaVersion = 3u;
    inline constexpr std::string_view kFakeDefaultPayload =
        R"json({"enabled":false,"limit":2})json";
    inline constexpr std::string_view kFakeChangedPayload =
        R"json({"enabled":true,"limit":4})json";
    [[nodiscard]] EngineConfigSectionValidationResult ValidateFakeSection(
        const std::string_view document,
        const std::string_view reference,
        const std::string_view subject)
    {
        if (document == kFakeDefaultPayload || document == kFakeChangedPayload)
        {
            return EngineConfigSectionValidationResult{
                .State = EngineConfigState::Valid,
                .CanonicalPayloadJson = std::string{document},
                .ParsedFieldCount = 2u,
            };
        }
        return EngineConfigSectionValidationResult{
            .State = EngineConfigState::FallbackApplied,
            .CanonicalPayloadJson = std::string{reference},
            .Diagnostics = {
                EngineConfigDiagnostic{
                    .State = EngineConfigState::FallbackApplied,
                    .Severity = EngineConfigDiagnosticSeverity::Warning,
                    .Code = EngineConfigDiagnosticCode::InvalidValue,
                    .Subject = std::string{subject},
                    .Message = "Fake section values are invalid.",
                },
            },
        };
    }

    [[nodiscard]] EngineConfigSectionRegistration FakeRegistration(
        std::string name = std::string{kFakeSectionName},
        std::string schema = std::string{kFakeSchemaId})
    {
        return EngineConfigSectionRegistration{
            .DefaultSection =
                EngineConfigSection{
                    .Name = std::move(name),
                    .SchemaId = std::move(schema),
                    .SchemaVersion = kFakeSchemaVersion,
                    .PayloadJson = std::string{kFakeDefaultPayload},
                },
            .Validate = ValidateFakeSection,
        };
    }

    [[nodiscard]] EngineConfigSectionRegistry FakeRegistry()
    {
        EngineConfigSectionRegistry registry{};
        EXPECT_TRUE(registry.Register(FakeRegistration()));
        return registry;
    }

    [[nodiscard]] std::filesystem::path TempConfigPath()
    {
        return std::filesystem::temp_directory_path() /
            "intrinsic_core_generic_engine_config.json";
    }

    void WriteTextFile(
        const std::filesystem::path& path,
        const std::string& text)
    {
        std::ofstream file{path, std::ios::binary | std::ios::trunc};
        ASSERT_TRUE(file.is_open());
        file << text;
    }

    [[nodiscard]] const EngineConfigSection& RequireSection(
        const EngineConfig& config,
        const std::string_view name = kFakeSectionName)
    {
        const EngineConfigSection* section =
            FindEngineConfigSection(config.AppSections, name);
        EXPECT_NE(section, nullptr);
        static const EngineConfigSection fallback{};
        return section != nullptr ? *section : fallback;
    }

    [[nodiscard]] std::string DocumentWithSectionRecord(
        const std::string_view record)
    {
        return std::string{
            R"json({"schema":"intrinsic.core.engine-config","version":1,"app":{"sections":[)json"} +
            std::string{record} +
            R"json(]}})json";
    }
}

TEST(CoreEngineConfigSections, UpsertKeepsNameOrderAndReplacesByName)
{
    std::vector<EngineConfigSection> sections{};
    UpsertEngineConfigSection(
        sections,
        EngineConfigSection{
            .Name = "zeta",
            .SchemaId = "zeta.schema",
            .SchemaVersion = 1u,
            .PayloadJson = "{}",
        });
    UpsertEngineConfigSection(
        sections,
        EngineConfigSection{
            .Name = "alpha",
            .SchemaId = "alpha.schema",
            .SchemaVersion = 1u,
            .PayloadJson = "{}",
        });
    UpsertEngineConfigSection(
        sections,
        EngineConfigSection{
            .Name = "zeta",
            .SchemaId = "zeta.schema",
            .SchemaVersion = 2u,
            .PayloadJson = R"json({"changed":true})json",
        });

    ASSERT_EQ(sections.size(), 2u);
    EXPECT_EQ(sections[0].Name, "alpha");
    EXPECT_EQ(sections[1].Name, "zeta");
    EXPECT_EQ(sections[1].SchemaVersion, 2u);
    EXPECT_EQ(
        FindEngineConfigSection(sections, "zeta"),
        &sections[1]);
    EXPECT_EQ(FindEngineConfigSection(sections, "missing"), nullptr);
}

TEST(CoreEngineConfigSections, RegistryValidatesDefaultsAndKeepsNameOrder)
{
    EngineConfigSectionRegistry registry{};
    EXPECT_TRUE(registry.Register(FakeRegistration("zeta", "zeta.schema")));
    EXPECT_TRUE(registry.Register(FakeRegistration("alpha", "alpha.schema")));
    EXPECT_FALSE(registry.Register(FakeRegistration("zeta", "other.schema")));
    EXPECT_FALSE(registry.Register(FakeRegistration("", "empty.schema")));

    EngineConfigSectionRegistration malformed = FakeRegistration(
        "malformed",
        "malformed.schema");
    malformed.DefaultSection.PayloadJson = "[]";
    EXPECT_FALSE(registry.Register(std::move(malformed)));

    EngineConfigSectionRegistration rejected = FakeRegistration(
        "rejected",
        "rejected.schema");
    rejected.Validate =
        [](std::string_view, std::string_view, std::string_view)
        {
            return EngineConfigSectionValidationResult{
                .State = EngineConfigState::Invalid,
                .CanonicalPayloadJson = "{}",
            };
        };
    EXPECT_FALSE(registry.Register(std::move(rejected)));

    const auto entries = registry.Entries();
    ASSERT_EQ(entries.size(), 2u);
    EXPECT_EQ(entries[0].DefaultSection.Name, "alpha");
    EXPECT_EQ(entries[1].DefaultSection.Name, "zeta");
    EXPECT_EQ(registry.Find("alpha"), &entries[0]);
    EXPECT_EQ(registry.Find("missing"), nullptr);
}

TEST(CoreEngineConfigSections, PopulateDefaultsPreservesExplicitValues)
{
    EngineConfigSectionRegistry registry = FakeRegistry();
    EngineConfig config{};
    UpsertEngineConfigSection(
        config.AppSections,
        EngineConfigSection{
            .Name = std::string{kFakeSectionName},
            .SchemaId = std::string{kFakeSchemaId},
            .SchemaVersion = kFakeSchemaVersion,
            .PayloadJson = std::string{kFakeChangedPayload},
        });

    PopulateEngineConfigSectionDefaults(config, registry);
    EXPECT_EQ(config.AppSections.size(), 1u);
    EXPECT_EQ(RequireSection(config).PayloadJson, kFakeChangedPayload);

    EngineConfig defaultsOnly{};
    PopulateEngineConfigSectionDefaults(defaultsOnly, registry);
    ASSERT_EQ(defaultsOnly.AppSections.size(), 1u);
    EXPECT_EQ(RequireSection(defaultsOnly).PayloadJson, kFakeDefaultPayload);
}

TEST(CoreEngineConfigLoad, CoreAndRegisteredSectionRoundTrip)
{
    EngineConfigSectionRegistry registry = FakeRegistry();
    EngineConfig config{};
    config.Window.Title = "Generic Section Test";
    config.Window.Width = 1280;
    config.Window.Height = 720;
    config.Window.Resizable = false;
    config.Window.Backend = WindowBackend::Null;
    config.Render.Backend = GraphicsBackend::Vulkan;
    config.Render.EnablePromotedVulkanDevice = true;
    config.Render.EnableValidation = false;
    config.Render.EnableVSync = false;
    config.Render.FramesInFlight = 3u;
    config.Render.DefaultRecipeConfigPath = "config/test-recipe.json";
    config.Render.SynchronousExtraction = false;
    config.Simulation.WorkerThreadCount = 4u;
    config.ReferenceScene.Enabled = true;
    config.ReferenceScene.Selector = ReferenceSceneSelector::Triangle;
    config.Camera.Enabled = false;
    config.Camera.Controller = CameraControllerKind::TopDown;
    PopulateEngineConfigSectionDefaults(config, registry);
    UpsertEngineConfigSection(
        config.AppSections,
        EngineConfigSection{
            .Name = std::string{kFakeSectionName},
            .SchemaId = std::string{kFakeSchemaId},
            .SchemaVersion = kFakeSchemaVersion,
            .PayloadJson = std::string{kFakeChangedPayload},
        });

    const std::string document = SerializeEngineConfig(config);
    const EngineConfigLoadResult preview = PreviewEngineConfig(
        document,
        EngineConfig{},
        EngineConfigParseOptions{
            .SourceId = "generic-round-trip",
            .SectionRegistry = &registry,
        });

    ASSERT_EQ(preview.State, EngineConfigState::Valid);
    EXPECT_FALSE(HasErrors(preview));
    EXPECT_TRUE(preview.Preview.SideEffectFree);
    EXPECT_EQ(preview.Preview.ParsedFieldCount, 19u);
    EXPECT_EQ(preview.Preview.Config.Window.Title, "Generic Section Test");
    EXPECT_EQ(preview.Preview.Config.Window.Width, 1280);
    EXPECT_EQ(preview.Preview.Config.Render.FramesInFlight, 3u);
    EXPECT_EQ(
        preview.Preview.Config.Render.DefaultRecipeConfigPath,
        "config/test-recipe.json");
    EXPECT_EQ(
        RequireSection(preview.Preview.Config).PayloadJson,
        kFakeChangedPayload);
    EXPECT_EQ(SerializeEngineConfig(preview.Preview.Config), document);

    const std::filesystem::path path = TempConfigPath();
    WriteTextFile(path, document);
    const EngineConfigLoadResult loaded = LoadEngineConfigFile(
        path.string(),
        EngineConfig{},
        EngineConfigParseOptions{.SectionRegistry = &registry});
    std::filesystem::remove(path);

    ASSERT_EQ(loaded.State, EngineConfigState::Valid);
    EXPECT_EQ(loaded.SchemaVersion, kEngineConfigSchemaVersion);
    EXPECT_EQ(RequireSection(loaded.Preview.Config).PayloadJson, kFakeChangedPayload);
}

TEST(CoreEngineConfigLoad, UnknownSectionFallsBackWithOrWithoutRegistry)
{
    const std::string document = DocumentWithSectionRecord(
        R"json({"name":"unknown","schema":"unknown.schema","version":1,"payload":{"enabled":true}})json");

    const EngineConfigLoadResult withoutRegistry =
        PreviewEngineConfig(document);
    ASSERT_EQ(withoutRegistry.State, EngineConfigState::FallbackApplied);
    EXPECT_TRUE(HasDiagnostic(
        withoutRegistry,
        EngineConfigDiagnosticCode::UnknownField));
    EXPECT_TRUE(withoutRegistry.Preview.Config.AppSections.empty());

    EngineConfigSectionRegistry registry = FakeRegistry();
    const EngineConfigLoadResult withRegistry = PreviewEngineConfig(
        document,
        EngineConfig{},
        EngineConfigParseOptions{.SectionRegistry = &registry});
    ASSERT_EQ(withRegistry.State, EngineConfigState::FallbackApplied);
    EXPECT_TRUE(HasDiagnostic(
        withRegistry,
        EngineConfigDiagnosticCode::UnknownField));
    EXPECT_EQ(RequireSection(withRegistry.Preview.Config).PayloadJson,
              kFakeDefaultPayload);
}

TEST(CoreEngineConfigLoad, DuplicateSectionNamesRetainReference)
{
    std::uint32_t validationCalls = 0u;
    EngineConfigSectionRegistry registry{};
    EngineConfigSectionRegistration registration = FakeRegistration();
    registration.Validate =
        [&validationCalls](
            const std::string_view document,
            const std::string_view reference,
            const std::string_view subject)
        {
            ++validationCalls;
            return ValidateFakeSection(document, reference, subject);
        };
    ASSERT_TRUE(registry.Register(std::move(registration)));
    validationCalls = 0u;

    const std::string document = DocumentWithSectionRecord(
        R"json({"name":"test.fake","schema":"test.fake-config","version":3,"payload":{"enabled":true,"limit":4}},{"name":"test.fake","schema":"test.fake-config","version":3,"payload":{"enabled":false,"limit":2}})json");
    const EngineConfigLoadResult result = PreviewEngineConfig(
        document,
        EngineConfig{},
        EngineConfigParseOptions{.SectionRegistry = &registry});

    ASSERT_EQ(result.State, EngineConfigState::FallbackApplied);
    EXPECT_TRUE(HasDiagnostic(result, EngineConfigDiagnosticCode::InvalidValue));
    EXPECT_EQ(validationCalls, 0u);
    EXPECT_EQ(RequireSection(result.Preview.Config).PayloadJson,
              kFakeDefaultPayload);
}

TEST(CoreEngineConfigLoad, SectionMetadataMismatchRetainsReference)
{
    EngineConfigSectionRegistry registry = FakeRegistry();
    struct Case
    {
        std::string Record;
        EngineConfigDiagnosticCode Code;
    };
    for (const Case& testCase : {
             Case{
                 R"json({"name":"test.fake","schema":"wrong","version":3,"payload":{"enabled":true,"limit":4}})json",
                 EngineConfigDiagnosticCode::InvalidSchema,
             },
             Case{
                 R"json({"name":"test.fake","schema":"test.fake-config","version":4,"payload":{"enabled":true,"limit":4}})json",
                 EngineConfigDiagnosticCode::UnsupportedVersion,
             },
             Case{
                 R"json({"name":"test.fake","schema":"test.fake-config","version":3,"payload":[]})json",
                 EngineConfigDiagnosticCode::InvalidValue,
             },
         })
    {
        const EngineConfigLoadResult result = PreviewEngineConfig(
            DocumentWithSectionRecord(testCase.Record),
            EngineConfig{},
            EngineConfigParseOptions{.SectionRegistry = &registry});
        ASSERT_EQ(result.State, EngineConfigState::FallbackApplied);
        EXPECT_TRUE(HasDiagnostic(result, testCase.Code));
        EXPECT_EQ(RequireSection(result.Preview.Config).PayloadJson,
                  kFakeDefaultPayload);
    }
}

TEST(CoreEngineConfigLoad,
     ValidatorFallbackCanonicalIsAcceptedAndMalformedOutputRetainsReference)
{
    EngineConfigSectionRegistry registry = FakeRegistry();
    const EngineConfigLoadResult invalidValue = PreviewEngineConfig(
        DocumentWithSectionRecord(
            R"json({"name":"test.fake","schema":"test.fake-config","version":3,"payload":{"enabled":"yes","limit":4}})json"),
        EngineConfig{},
        EngineConfigParseOptions{.SectionRegistry = &registry});
    ASSERT_EQ(invalidValue.State, EngineConfigState::FallbackApplied);
    EXPECT_TRUE(HasDiagnostic(
        invalidValue,
        EngineConfigDiagnosticCode::InvalidValue));
    EXPECT_EQ(RequireSection(invalidValue.Preview.Config).PayloadJson,
              kFakeDefaultPayload);

    EngineConfigSectionRegistry fallbackRegistry{};
    EngineConfigSectionRegistration fallback = FakeRegistration();
    fallback.Validate =
        [](const std::string_view document,
           const std::string_view,
           const std::string_view)
        {
            if (document == kFakeDefaultPayload)
            {
                return EngineConfigSectionValidationResult{
                    .State = EngineConfigState::Valid,
                    .CanonicalPayloadJson = std::string{document},
                    .ParsedFieldCount = 2u,
                };
            }
            return EngineConfigSectionValidationResult{
                .State = EngineConfigState::FallbackApplied,
                .CanonicalPayloadJson = std::string{document},
                .ParsedFieldCount = 2u,
            };
        };
    ASSERT_TRUE(fallbackRegistry.Register(std::move(fallback)));
    const EngineConfigLoadResult fallbackOutput = PreviewEngineConfig(
        DocumentWithSectionRecord(
            R"json({"name":"test.fake","schema":"test.fake-config","version":3,"payload":{"enabled":true,"limit":4}})json"),
        EngineConfig{},
        EngineConfigParseOptions{.SectionRegistry = &fallbackRegistry});
    ASSERT_EQ(fallbackOutput.State, EngineConfigState::FallbackApplied);
    EXPECT_EQ(RequireSection(fallbackOutput.Preview.Config).PayloadJson,
              kFakeChangedPayload);
    EXPECT_EQ(fallbackOutput.Preview.ParsedFieldCount, 2u);

    EngineConfigSectionRegistry malformedRegistry{};
    EngineConfigSectionRegistration malformed = FakeRegistration();
    malformed.Validate =
        [](const std::string_view document,
           const std::string_view,
           const std::string_view)
        {
            return EngineConfigSectionValidationResult{
                .State = EngineConfigState::Valid,
                .CanonicalPayloadJson =
                    document == kFakeDefaultPayload ? std::string{document}
                                                    : std::string{"[]"},
                .ParsedFieldCount = 2u,
            };
        };
    ASSERT_TRUE(malformedRegistry.Register(std::move(malformed)));
    const EngineConfigLoadResult malformedOutput = PreviewEngineConfig(
        DocumentWithSectionRecord(
            R"json({"name":"test.fake","schema":"test.fake-config","version":3,"payload":{"enabled":true,"limit":4}})json"),
        EngineConfig{},
        EngineConfigParseOptions{.SectionRegistry = &malformedRegistry});
    ASSERT_EQ(malformedOutput.State, EngineConfigState::FallbackApplied);
    EXPECT_EQ(RequireSection(malformedOutput.Preview.Config).PayloadJson,
              kFakeDefaultPayload);
}

TEST(CoreEngineConfigLoad, MalformedStoredPayloadSerializesFailClosed)
{
    EngineConfig config{};
    config.AppSections.push_back(EngineConfigSection{
        .Name = std::string{kFakeSectionName},
        .SchemaId = std::string{kFakeSchemaId},
        .SchemaVersion = kFakeSchemaVersion,
        .PayloadJson = "not-json",
    });
    const std::string document = SerializeEngineConfig(config);
    EXPECT_NE(document.find("\"payload\": null"), std::string::npos);

    EngineConfigSectionRegistry registry = FakeRegistry();
    const EngineConfigLoadResult result = PreviewEngineConfig(
        document,
        EngineConfig{},
        EngineConfigParseOptions{.SectionRegistry = &registry});
    ASSERT_EQ(result.State, EngineConfigState::FallbackApplied);
    EXPECT_TRUE(HasDiagnostic(result, EngineConfigDiagnosticCode::InvalidValue));
    EXPECT_EQ(RequireSection(result.Preview.Config).PayloadJson,
              kFakeDefaultPayload);
}

TEST(CoreEngineConfigLoad, InvalidCoreFieldsRetainReferenceWithDiagnostics)
{
    EngineConfig defaults{};
    defaults.Window.Title = "Reference";
    defaults.Window.Width = 1600;
    defaults.Render.EnableValidation = true;
    defaults.Simulation.WorkerThreadCount = 6u;

    const EngineConfigLoadResult result = PreviewEngineConfig(
        R"json({
          "schema":"intrinsic.core.engine-config",
          "version":1,
          "unknown":true,
          "window":{"title":"","width":0},
          "render":{"enable_validation":"yes"},
          "simulation":{"worker_thread_count":-1}
        })json",
        defaults);

    ASSERT_EQ(result.State, EngineConfigState::FallbackApplied);
    EXPECT_FALSE(HasErrors(result));
    EXPECT_TRUE(IsConfigUsable(result));
    EXPECT_TRUE(HasDiagnostic(result, EngineConfigDiagnosticCode::UnknownField));
    EXPECT_TRUE(HasDiagnostic(result, EngineConfigDiagnosticCode::InvalidValue));
    EXPECT_EQ(result.Preview.Config.Window.Title, "Reference");
    EXPECT_EQ(result.Preview.Config.Window.Width, 1600);
    EXPECT_TRUE(result.Preview.Config.Render.EnableValidation);
    EXPECT_EQ(result.Preview.Config.Simulation.WorkerThreadCount, 6u);
}
