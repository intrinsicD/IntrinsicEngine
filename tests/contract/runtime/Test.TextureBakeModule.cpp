#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <glm/glm.hpp>
#include <gtest/gtest.h>

import Extrinsic.Asset.Registry;
import Extrinsic.Asset.Service;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Error;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Graphics.Colormap;
import Extrinsic.Graphics.Component.VisualizationConfig;
import Extrinsic.Graphics.Renderer;
import Extrinsic.RHI.Device;
import Extrinsic.Runtime.AssetWorkflowModule;
import Extrinsic.Runtime.CommandBus;
import Extrinsic.Runtime.GeometryPresentation;
import Extrinsic.Runtime.GeometryProperty.Types;
import Extrinsic.Runtime.EditorWorkspaceSnapshots;
import Extrinsic.Runtime.EditorJobProjection;
import Extrinsic.Runtime.FramePacingDiagnostics;
import Extrinsic.Runtime.EditorWorkspaceAttachment;
import Extrinsic.Runtime.ParameterizationOperations;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.MeshSurfaceTopology;
import Extrinsic.Runtime.Module;
import Extrinsic.Runtime.RenderExtraction;
import Extrinsic.Runtime.SceneDocumentModule;
import Extrinsic.Runtime.SceneEditingOperations;
import Extrinsic.Runtime.GeometryProcessingOperations;
import Extrinsic.Runtime.ServiceRegistry;
import Extrinsic.Runtime.StableEntityLookup;
import Extrinsic.Runtime.VisualizationEditingOperations;
import Extrinsic.Runtime.RenderRecipeEditingOperations;
import Extrinsic.Runtime.TextureBakeModule;
import Extrinsic.Runtime.WorldHandle;
import Extrinsic.Runtime.WorldRegistry;
import Geometry.Properties;

#include "MockRHI.hpp"

namespace Runtime = Extrinsic::Runtime;

namespace
{
    namespace ECS = Extrinsic::ECS;
    namespace GS = Extrinsic::ECS::Components::GeometrySources;
    namespace Core = Extrinsic::Core;
    namespace Graphics = Extrinsic::Graphics;
    namespace RHI = Extrinsic::RHI;

    using Runtime::GeometryElementDomain;
    using Runtime::PropertyTextureBakeFreshness;
    using Runtime::PropertyTextureBakeStatus;

    constexpr std::uint32_t kInvalid = std::numeric_limits<std::uint32_t>::max();
    constexpr float kAtlasExtent = 16.0f;

    [[nodiscard]] glm::vec2 Texel(const float x, const float y)
    {
        return glm::vec2{x / kAtlasExtent, y / kAtlasExtent};
    }

    // Quad v0(0,0) v1(1,0) v2(1,1) v3(0,1) as faces (v0,v1,v2), (v0,v2,v3).
    // h0..h2 / h3..h5 are the face loops (target vertices 1,2,0 / 2,3,0);
    // h6..h9 form the boundary loop. The canonical `h:texcoord` cuts a UV
    // seam along the diagonal (two charts); `h:custom_atlas` is one chart;
    // `v:texcoord` is a third, vertex-domain layout.
    [[nodiscard]] ECS::EntityHandle MakeSeamedQuad(ECS::Scene::Registry& scene)
    {
        const ECS::EntityHandle entity = scene.Create();
        auto& raw = scene.Raw();
        auto& vertices = raw.emplace<GS::Vertices>(entity);
        vertices.Properties.Resize(4u);
        vertices.Properties
            .GetOrAdd<glm::vec3>(std::string{GS::PropertyNames::kPosition}, glm::vec3{0.0f})
            .Vector() = {{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f},
                         {1.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}};
        vertices.Properties.GetOrAdd<float>("v:heat", 0.0f).Vector() =
            {-2.0f, 0.0f, 3.5f, -0.25f};
        vertices.Properties.GetOrAdd<glm::vec2>("v:texcoord", glm::vec2{0.0f}).Vector() =
            {Texel(1.0f, 1.0f), Texel(15.0f, 1.0f), Texel(15.0f, 15.0f), Texel(1.0f, 15.0f)};

        auto& edges = raw.emplace<GS::Edges>(entity);
        edges.Properties.Resize(5u);
        edges.Properties.GetOrAdd<std::uint32_t>(std::string{GS::PropertyNames::kEdgeV0}, kInvalid)
            .Vector() = {0u, 1u, 2u, 2u, 3u};
        edges.Properties.GetOrAdd<std::uint32_t>(std::string{GS::PropertyNames::kEdgeV1}, kInvalid)
            .Vector() = {1u, 2u, 0u, 3u, 0u};

        auto& halfedges = raw.emplace<GS::Halfedges>(entity);
        halfedges.Properties.Resize(10u);
        halfedges.Properties
            .GetOrAdd<std::uint32_t>(std::string{GS::PropertyNames::kHalfedgeToVertex}, kInvalid)
            .Vector() = {1u, 2u, 0u, 2u, 3u, 0u, 0u, 1u, 2u, 3u};
        halfedges.Properties
            .GetOrAdd<std::uint32_t>(std::string{GS::PropertyNames::kHalfedgeNext}, kInvalid)
            .Vector() = {1u, 2u, 0u, 4u, 5u, 3u, 9u, 6u, 7u, 8u};
        halfedges.Properties
            .GetOrAdd<std::uint32_t>(std::string{GS::PropertyNames::kHalfedgeFace}, kInvalid)
            .Vector() = {0u, 0u, 0u, 1u, 1u, 1u, kInvalid, kInvalid, kInvalid, kInvalid};
        const float nan = std::numeric_limits<float>::quiet_NaN();
        // Face 0 texels (v1 7,1)(v2 7,7)(v0 1,1); face 1 (v2 15,7)(v3 9,7)(v0 9,1).
        // Boundary corners carry no triangle; NaN there must not matter.
        halfedges.Properties.GetOrAdd<glm::vec2>("h:texcoord", glm::vec2{0.0f}).Vector() =
            {Texel(7.0f, 1.0f), Texel(7.0f, 7.0f), Texel(1.0f, 1.0f),
             Texel(15.0f, 7.0f), Texel(9.0f, 7.0f), Texel(9.0f, 1.0f),
             glm::vec2{nan}, glm::vec2{nan}, glm::vec2{nan}, glm::vec2{nan}};
        halfedges.Properties.GetOrAdd<glm::vec2>("h:custom_atlas", glm::vec2{0.0f}).Vector() =
            {Texel(14.0f, 2.0f), Texel(14.0f, 14.0f), Texel(2.0f, 2.0f),
             Texel(14.0f, 14.0f), Texel(2.0f, 14.0f), Texel(2.0f, 2.0f),
             glm::vec2{0.0f}, glm::vec2{0.0f}, glm::vec2{0.0f}, glm::vec2{0.0f}};

        auto& faces = raw.emplace<GS::Faces>(entity);
        faces.Properties.Resize(2u);
        faces.Properties.GetOrAdd<std::uint32_t>(std::string{GS::PropertyNames::kFaceHalfedge}, kInvalid)
            .Vector() = {0u, 3u};
        return entity;
    }

    // Composes the bake module exactly as the runtime does, with an
    // operational mock device, so requests run the real validation and
    // scheduling path. GPU recording needs a renderer frame and is covered by
    // the gpu;vulkan smoke.
    struct BakeHarness
    {
        BakeHarness()
            : Renderer(Graphics::CreateRenderer())
        {
            World = Worlds.CreateWorld("TextureBake");
            Renderer->Initialize(Device);
        }

        ~BakeHarness()
        {
            Stop();
            Services.Reset();
            Extraction.Shutdown(*Renderer);
            Renderer->Shutdown();
        }

        [[nodiscard]] Runtime::EngineSetup MakeSetup()
        {
            return Runtime::EngineSetup{
                Commands, Events, Jobs, Worlds, Services,
                [this](const Runtime::FramePhase phase, Runtime::RuntimeFrameHook hook)
                {
                    if (phase == Runtime::FramePhase::Maintenance)
                        MaintenanceHooks.push_back(std::move(hook));
                },
                Runtime::RuntimeRenderRecipeActivationKernel{.ActiveConfig = &Config},
                {},
                &Initialized,
            };
        }

        [[nodiscard]] bool Start()
        {
            Services.BeginRegistration();
            if (!Services.Provide<RHI::IDevice>(Device, "Test.Device").has_value() ||
                !Services.Provide<Graphics::IRenderer>(*Renderer, "Test.Renderer").has_value() ||
                !Services.Provide<Runtime::RenderExtractionCache>(Extraction, "Test.Extraction").has_value())
            {
                return false;
            }
            Runtime::EngineSetup registration = MakeSetup();
            Registered = Asset.OnRegister(registration).has_value() &&
                         Document.OnRegister(registration).has_value();
            // RunMaintenance drives only the texture-bake hooks under test.
            MaintenanceHooks.clear();
            Registered = Registered &&
                         TextureBake.OnRegister(registration).has_value();
            if (!Registered)
                return false;
            Services.BeginResolution();
            Runtime::EngineSetup resolution = MakeSetup();
            if (!Document.OnResolve(resolution).has_value() ||
                !Asset.OnResolve(resolution).has_value() ||
                !TextureBake.OnResolve(resolution).has_value())
            {
                return false;
            }
            Services.Lock();
            Service = Services.Find<Runtime::TextureBakeService>();
            return Service != nullptr && Service->Available();
        }

        void Stop()
        {
            if (!Registered)
                return;
            Initialized = false;
            Events.Publish(Runtime::RuntimeShutdownAnnounced{});
            (void)Events.Pump();
            (void)Jobs.ShutdownGpuQueueParticipants([this] { Device.WaitIdle(); });
            Runtime::RuntimeModuleShutdownContext context{
                .Commands = Commands,
                .Events = Events,
                .Jobs = Jobs,
                .Worlds = Worlds,
                .Services = Services,
            };
            TextureBake.OnShutdown(context);
            Asset.OnShutdown(context);
            Document.OnShutdown(context);
            Services.Reset();
            Registered = false;
            Service = nullptr;
        }

        [[nodiscard]] ECS::Scene::Registry& Scene() { return *Worlds.Get(World); }

        [[nodiscard]] Extrinsic::Assets::AssetService& Assets()
        {
            return *Services.Find<Extrinsic::Assets::AssetService>();
        }

        // Runs the modules' maintenance phase as one engine frame would.
        void RunMaintenance()
        {
            Runtime::EditorInputCaptureSnapshot capture{};
            Runtime::RuntimeFramePacingDiagnostics pacing{};
            Runtime::RuntimeFrameHookContext context{
                .ActiveWorld = Scene(),
                .ActiveWorldHandle = World,
                .Commands = Commands,
                .Events = Events,
                .Jobs = Jobs,
                .Worlds = Worlds,
                .Services = Services,
                .EditorCapture = capture,
                .Pacing = pacing,
            };
            for (const Runtime::RuntimeFrameHook& hook : MaintenanceHooks)
                hook(context);
        }

        Extrinsic::Tests::MockDevice Device{};
        std::unique_ptr<Graphics::IRenderer> Renderer{};
        Runtime::RenderExtractionCache Extraction{};
        Runtime::CommandBus Commands{};
        Runtime::KernelEventBus Events{};
        Runtime::JobService Jobs{};
        Runtime::WorldRegistry Worlds{};
        Runtime::ServiceRegistry Services{};
        Core::Config::EngineConfig Config{};
        bool Initialized{false};
        bool Registered{false};
        Runtime::AssetWorkflowModule Asset{};
        Runtime::SceneDocumentModule Document{};
        Runtime::TextureBakeModule TextureBake{};
        Runtime::TextureBakeService* Service{};
        Runtime::WorldHandle World{};
        std::vector<Runtime::RuntimeFrameHook> MaintenanceHooks{};
    };

    [[nodiscard]] Runtime::PropertyTextureBakeRequest HeatRequest(
        const BakeHarness& harness,
        const ECS::EntityHandle entity,
        std::string outputName,
        Runtime::GeometryPropertyRef texcoords = {})
    {
        return Runtime::PropertyTextureBakeRequest{
            .World = harness.World,
            .StableEntityId = Runtime::StableEntityLookup::ToRenderId(entity),
            .Source = Runtime::GeometryPropertyRef{
                .Domain = GeometryElementDomain::MeshVertex,
                .Name = "v:heat",
            },
            .Texcoords = std::move(texcoords),
            .Width = 16u,
            .Height = 16u,
            .PaddingTexels = 2u,
            .OutputName = std::move(outputName),
        };
    }

    [[nodiscard]] Runtime::PropertyTextureBakeRecord RecordNamed(
        BakeHarness& harness,
        const ECS::EntityHandle entity,
        const std::string_view name)
    {
        const Runtime::TextureBakeSnapshot snapshot = harness.Service->Snapshot(
            Runtime::StableEntityLookup::ToRenderId(entity));
        for (const auto& record : snapshot.Textures)
        {
            if (record.OutputName == name)
                return record;
        }
        ADD_FAILURE() << "missing bake record " << name;
        return {};
    }

    template <class T>
    void Mutate(Geometry::PropertySet& properties, const std::string_view name, auto&& edit)
    {
        auto property = properties.GetOrAdd<T>(std::string{name}, T{});
        edit(property.Vector());
    }

    // Revision lookup over the entity's live property sets, the same inputs
    // render extraction hands to ComputePropertyTextureBakeRevisionToken.
    [[nodiscard]] Runtime::PropertyTextureBakeRevisionLookup LiveLookup(
        ECS::Scene::Registry& scene,
        const ECS::EntityHandle entity)
    {
        auto& raw = scene.Raw();
        return [&raw, entity](const GeometryElementDomain domain, const std::string_view name)
                   -> std::optional<std::uint64_t>
        {
            const Geometry::PropertySet* set = nullptr;
            switch (domain)
            {
            case GeometryElementDomain::MeshVertex: set = &raw.get<GS::Vertices>(entity).Properties; break;
            case GeometryElementDomain::MeshEdge: set = &raw.get<GS::Edges>(entity).Properties; break;
            case GeometryElementDomain::MeshHalfedge: set = &raw.get<GS::Halfedges>(entity).Properties; break;
            case GeometryElementDomain::MeshFace: set = &raw.get<GS::Faces>(entity).Properties; break;
            default: return std::nullopt;
            }
            const auto revision = set->FindPropertyRevision(name);
            return revision.has_value() ? std::optional<std::uint64_t>{*revision} : std::nullopt;
        };
    }

    // Rewrites the six triangle-corner UVs of a MakeSeamedQuad atlas, given
    // in texels of an `extent` x `extent` atlas.
    void SetTriangleCornerUvs(ECS::Scene::Registry& scene,
                              const ECS::EntityHandle entity,
                              const std::string_view name,
                              const float extent,
                              const std::vector<glm::vec2>& texels)
    {
        Mutate<glm::vec2>(scene.Raw().get<GS::Halfedges>(entity).Properties, name,
                          [&](auto& uv)
                          {
                              uv.resize(10u, glm::vec2{0.0f});
                              for (std::size_t corner = 0u; corner < texels.size(); ++corner)
                                  uv[corner] = texels[corner] / extent;
                          });
    }

    // Surface appearance of `v:heat` rendered from its baked texture, as
    // persisted by the visualization editor.
    Graphics::Components::VisualizationConfig& EnableHeatAppearance(
        ECS::Scene::Registry& scene,
        const ECS::EntityHandle entity)
    {
        using Config = Graphics::Components::VisualizationConfig;
        auto& overrides = scene.Raw().emplace_or_replace<Graphics::Components::VisualizationLaneOverrides>(entity);
        overrides.Surface = Config{};
        overrides.Surface->Source = Config::ColorSource::ScalarField;
        overrides.Surface->ScalarFieldName = "v:heat";
        overrides.Surface->ScalarDomain = Config::Domain::Vertex;
        overrides.Surface->Scalar.AutoRange = true;
        overrides.Surface->UseBakedTexture = true;
        return *overrides.Surface;
    }

    void ForceReady(Extrinsic::Assets::AssetService& assets, const Extrinsic::Assets::AssetId asset)
    {
        const auto meta = assets.GetMeta(asset);
        ASSERT_TRUE(meta.has_value());
        if (meta->state != Extrinsic::Assets::AssetState::Ready)
        {
            ASSERT_TRUE(assets.ForceAssetState(asset, meta->state, Extrinsic::Assets::AssetState::Ready)
                            .has_value());
        }
    }
}

TEST(RuntimeTextureBakeModule, DefaultRequestUsesAtlasScaleExtentAndCanonicalAtlas)
{
    const Runtime::PropertyTextureBakeRequest request{};
    EXPECT_EQ(request.Width, 1024u);
    EXPECT_EQ(request.Height, 1024u);
    EXPECT_FALSE(request.Texcoords.HasName())
        << "an empty texcoord reference selects the canonical corner-over-vertex atlas";
    EXPECT_EQ(request.MaxAdaptiveExtent, 0u) << "a request bakes exactly its extent unless it opts in";
}

TEST(RuntimeTextureBakeModule, RepresentationDefaultsPreserveRawScalarData)
{
    const std::vector<Runtime::EditorTextureBakeTarget> targets{
        Runtime::EditorTextureBakeTarget{
            .PresentationKey = "mesh.surface",
            .Semantic =
                Runtime::GeometryPresentationSlotSemantic::Albedo,
        },
        Runtime::EditorTextureBakeTarget{
            .PresentationKey = "mesh.surface",
            .Semantic =
                Runtime::GeometryPresentationSlotSemantic::ScalarField,
        },
    };
    const Runtime::PropertyTextureBakeRepresentation representation =
        Runtime::ResolveEditorTextureBakeTargetRepresentation(
            Geometry::PropertyValueKind::Float,
            Runtime::PropertyTextureBakeStorage::Auto,
            Runtime::PropertyTextureBakeEncoding::Auto,
            targets);

    EXPECT_EQ(
        representation.Storage,
        Runtime::PropertyTextureBakeStorage::RawFloat);
    EXPECT_EQ(
        representation.Encoding,
        Runtime::PropertyTextureBakeEncoding::LinearScalar);
    EXPECT_TRUE(Runtime::IsEditorTextureBakeTargetCompatible(
        targets[0],
        Geometry::PropertyValueKind::Float,
        representation.Storage,
        representation.Encoding));
    EXPECT_TRUE(Runtime::IsEditorTextureBakeTargetCompatible(
        targets[1],
        Geometry::PropertyValueKind::Float,
        representation.Storage,
        representation.Encoding));
}

TEST(RuntimeTextureBakeModule, NormalTargetAndExplicitLabelEncodingChooseEncodedStorage)
{
    const std::vector<Runtime::EditorTextureBakeTarget> normalTargets{
        Runtime::EditorTextureBakeTarget{
            .PresentationKey = "mesh.surface",
            .Semantic =
                Runtime::GeometryPresentationSlotSemantic::Normal,
        },
    };
    const auto normal =
        Runtime::ResolveEditorTextureBakeTargetRepresentation(
            Geometry::PropertyValueKind::Vec3,
            Runtime::PropertyTextureBakeStorage::Auto,
            Runtime::PropertyTextureBakeEncoding::Auto,
            normalTargets);
    EXPECT_EQ(
        normal.Storage,
        Runtime::PropertyTextureBakeStorage::EncodedRgba);
    EXPECT_EQ(
        normal.Encoding,
        Runtime::PropertyTextureBakeEncoding::Normal);
    EXPECT_TRUE(Runtime::IsEditorTextureBakeTargetCompatible(
        normalTargets.front(),
        Geometry::PropertyValueKind::Vec3,
        normal.Storage,
        normal.Encoding));

    const std::vector<Runtime::EditorTextureBakeTarget> albedoTargets{
        Runtime::EditorTextureBakeTarget{
            .PresentationKey = "mesh.surface",
            .Semantic =
                Runtime::GeometryPresentationSlotSemantic::Albedo,
        },
    };
    const auto label =
        Runtime::ResolveEditorTextureBakeTargetRepresentation(
            Geometry::PropertyValueKind::UInt32,
            Runtime::PropertyTextureBakeStorage::Auto,
            Runtime::PropertyTextureBakeEncoding::LabelPalette,
            albedoTargets);
    EXPECT_EQ(
        label.Storage,
        Runtime::PropertyTextureBakeStorage::EncodedRgba);
    EXPECT_EQ(
        label.Encoding,
        Runtime::PropertyTextureBakeEncoding::LabelPalette);
}

TEST(
    RuntimeTextureBakeModule,
    RepresentationMatrixRejectsEncodersThatDoNotMatchStorageAndValueType)
{
    using Runtime::IsPropertyTextureBakeRepresentationCompatible;
    using Runtime::PropertyTextureBakeEncoding;
    using Runtime::PropertyTextureBakeStorage;

    EXPECT_TRUE(IsPropertyTextureBakeRepresentationCompatible(
        Geometry::PropertyValueKind::Float,
        PropertyTextureBakeStorage::RawFloat,
        PropertyTextureBakeEncoding::LinearScalar));
    EXPECT_FALSE(IsPropertyTextureBakeRepresentationCompatible(
        Geometry::PropertyValueKind::Float,
        PropertyTextureBakeStorage::RawFloat,
        PropertyTextureBakeEncoding::Normal));
    EXPECT_TRUE(IsPropertyTextureBakeRepresentationCompatible(
        Geometry::PropertyValueKind::Float,
        PropertyTextureBakeStorage::EncodedRgba,
        PropertyTextureBakeEncoding::ScalarColormap));
    EXPECT_FALSE(IsPropertyTextureBakeRepresentationCompatible(
        Geometry::PropertyValueKind::UInt32,
        PropertyTextureBakeStorage::RawFloat,
        PropertyTextureBakeEncoding::LabelPalette));
    EXPECT_TRUE(IsPropertyTextureBakeRepresentationCompatible(
        Geometry::PropertyValueKind::UInt32,
        PropertyTextureBakeStorage::EncodedRgba,
        PropertyTextureBakeEncoding::LabelPalette));
    EXPECT_TRUE(IsPropertyTextureBakeRepresentationCompatible(
        Geometry::PropertyValueKind::Vec3,
        PropertyTextureBakeStorage::EncodedRgba,
        PropertyTextureBakeEncoding::Normal));
    EXPECT_FALSE(IsPropertyTextureBakeRepresentationCompatible(
        Geometry::PropertyValueKind::Vec4,
        PropertyTextureBakeStorage::EncodedRgba,
        PropertyTextureBakeEncoding::Normal));
    EXPECT_FALSE(IsPropertyTextureBakeRepresentationCompatible(
        Geometry::PropertyValueKind::Vec3,
        PropertyTextureBakeStorage::Auto,
        PropertyTextureBakeEncoding::Vector3));
}

TEST(RuntimeTextureBakeModule, ServiceFailsClosedWithoutGpuComposition)
{
    Runtime::TextureBakeService service{};
    EXPECT_FALSE(service.Available());
    const Runtime::PropertyTextureBakeResult result =
        service.Bake(Runtime::PropertyTextureBakeRequest{});
    EXPECT_EQ(
        result.Status,
        Runtime::PropertyTextureBakeStatus::NonOperationalBackend);
}

TEST(RuntimeTextureBakeModule, ScalarStorageKindsShareExplicitRepresentations)
{
    using K = Geometry::PropertyValueKind;
    using S = Runtime::PropertyTextureBakeStorage;
    using E = Runtime::PropertyTextureBakeEncoding;
    const Runtime::EditorTextureBakeTarget albedo{
        .PresentationKey = "mesh.surface",
        .Semantic = Runtime::GeometryPresentationSlotSemantic::Albedo};
    for (const auto kind : {K::Bool, K::Int32, K::UInt32, K::UInt64, K::Float, K::Double})
    {
        const auto defaults = Runtime::ResolvePropertyTextureBakeRepresentation(kind, S::Auto, E::Auto);
        EXPECT_EQ(defaults.Storage, S::RawFloat);
        EXPECT_EQ(defaults.Encoding, E::LinearScalar);
        EXPECT_TRUE(Runtime::IsPropertyTextureBakeRepresentationCompatible(kind, S::RawFloat, E::LinearScalar));
        EXPECT_TRUE(Runtime::IsPropertyTextureBakeRepresentationCompatible(kind, S::EncodedRgba, E::ScalarColormap));
        EXPECT_TRUE(Runtime::IsPropertyTextureBakeRepresentationCompatible(kind, S::EncodedRgba, E::LabelPalette));
        EXPECT_TRUE(Runtime::IsEditorTextureBakeTargetCompatible(albedo, kind, S::EncodedRgba, E::LabelPalette));
        EXPECT_FALSE(Runtime::IsPropertyTextureBakeRepresentationCompatible(kind, S::EncodedRgba, E::Normal));
    }
}

TEST(RuntimeTextureBakeModule, IdentityComparisonReportsTheMostStructuralChangeFirst)
{
    const Runtime::PropertyTextureBakeSourceIdentity baked{
        .ResolvedTexcoords = {.Domain = GeometryElementDomain::MeshHalfedge, .Name = "h:texcoord"},
        .UvFingerprint = 11u,
        .PositionFingerprint = 22u,
        .TopologyFingerprint = 33u,
        .PropertyFingerprint = 44u,
    };
    using Runtime::ComparePropertyTextureBakeSourceIdentity;
    EXPECT_EQ(ComparePropertyTextureBakeSourceIdentity(baked, baked), PropertyTextureBakeFreshness::Fresh);

    auto current = baked;
    current.PropertyFingerprint = 45u;
    EXPECT_EQ(ComparePropertyTextureBakeSourceIdentity(baked, current), PropertyTextureBakeFreshness::PropertyChanged);
    current.PositionFingerprint = 23u;
    EXPECT_EQ(ComparePropertyTextureBakeSourceIdentity(baked, current), PropertyTextureBakeFreshness::PositionsChanged);
    current.UvFingerprint = 12u;
    EXPECT_EQ(ComparePropertyTextureBakeSourceIdentity(baked, current), PropertyTextureBakeFreshness::UvChanged);
    current.TopologyFingerprint = 34u;
    EXPECT_EQ(ComparePropertyTextureBakeSourceIdentity(baked, current), PropertyTextureBakeFreshness::TopologyChanged);

    auto rebound = baked;
    rebound.ResolvedTexcoords.Name = "h:other";
    EXPECT_EQ(ComparePropertyTextureBakeSourceIdentity(baked, rebound), PropertyTextureBakeFreshness::UvChanged)
        << "equal UV bytes from a different atlas property are a different atlas";

    EXPECT_EQ(
        ComparePropertyTextureBakeSourceIdentity(Runtime::PropertyTextureBakeSourceIdentity{}, baked),
        PropertyTextureBakeFreshness::Unknown)
        << "records without a source identity (legacy or failed) are never reported Fresh";
}

TEST(RuntimeTextureBakeModule, RevisionTokenWatchesExactlyTheRecordDependencies)
{
    std::map<std::pair<GeometryElementDomain, std::string>, std::uint64_t> revisions{
        {{GeometryElementDomain::MeshHalfedge, "h:texcoord"}, 1u},
        {{GeometryElementDomain::MeshHalfedge, "h:custom_atlas"}, 2u},
        {{GeometryElementDomain::MeshVertex, "v:texcoord"}, 3u},
        {{GeometryElementDomain::MeshVertex, "v:position"}, 4u},
        {{GeometryElementDomain::MeshVertex, "v:heat"}, 5u},
        {{GeometryElementDomain::MeshVertex, "v:unrelated"}, 6u},
        {{GeometryElementDomain::MeshHalfedge, "h:to_vertex"}, 7u},
    };
    const Runtime::PropertyTextureBakeRevisionLookup lookup =
        [&revisions](const GeometryElementDomain domain, const std::string_view name)
            -> std::optional<std::uint64_t>
    {
        const auto found = revisions.find({domain, std::string{name}});
        return found != revisions.end() ? std::optional<std::uint64_t>{found->second} : std::nullopt;
    };
    Runtime::PropertyTextureBakeRecord canonical{};
    canonical.Source = {.Domain = GeometryElementDomain::MeshVertex, .Name = "v:heat"};
    Runtime::PropertyTextureBakeRecord custom = canonical;
    custom.Texcoords = {.Domain = GeometryElementDomain::MeshHalfedge, .Name = "h:custom_atlas"};

    const auto token = [&lookup](const Runtime::PropertyTextureBakeRecord& record)
    {
        return Runtime::ComputePropertyTextureBakeRevisionToken(record, lookup);
    };
    const std::uint64_t canonicalBefore = token(canonical);
    const std::uint64_t customBefore = token(custom);
    EXPECT_NE(canonicalBefore, 0u);

    revisions[{GeometryElementDomain::MeshVertex, "v:unrelated"}] = 60u;
    EXPECT_EQ(token(canonical), canonicalBefore);
    EXPECT_EQ(token(custom), customBefore);

    revisions[{GeometryElementDomain::MeshHalfedge, "h:texcoord"}] = 10u;
    EXPECT_NE(token(canonical), canonicalBefore);
    EXPECT_EQ(token(custom), customBefore) << "an explicit atlas ignores the canonical h:texcoord";

    // The canonical binding also follows v:texcoord, which becomes
    // authoritative if h:texcoord disappears.
    const std::uint64_t canonicalMid = token(canonical);
    revisions[{GeometryElementDomain::MeshVertex, "v:texcoord"}] = 30u;
    EXPECT_NE(token(canonical), canonicalMid);

    for (const auto& [key, value] :
         std::vector<std::pair<std::pair<GeometryElementDomain, std::string>, std::uint64_t>>{
             {{GeometryElementDomain::MeshVertex, "v:position"}, 40u},
             {{GeometryElementDomain::MeshVertex, "v:heat"}, 50u},
             {{GeometryElementDomain::MeshHalfedge, "h:to_vertex"}, 70u},
             {{GeometryElementDomain::MeshHalfedge, "h:custom_atlas"}, 20u}})
    {
        const std::uint64_t before = token(custom);
        revisions[key] = value;
        EXPECT_NE(token(custom), before) << key.second;
    }
    const std::uint64_t present = token(custom);
    revisions.erase({GeometryElementDomain::MeshHalfedge, "h:custom_atlas"});
    EXPECT_NE(token(custom), present) << "a removed dependency changes the token";
    EXPECT_EQ(Runtime::ComputePropertyTextureBakeRevisionToken(custom, {}), 0u);
}

TEST(RuntimeTextureBakeModule, OnlyReadyFreshUnchangedRecordsAreBindable)
{
    Runtime::PropertyTextureBakeRecord record{};
    record.Texture = Extrinsic::Assets::AssetId{3u, 1u};
    record.State = Runtime::PropertyTextureBakeOutputState::Ready;
    record.Freshness = PropertyTextureBakeFreshness::Fresh;
    record.ObservedRevisionToken = 77u;
    EXPECT_TRUE(Runtime::IsPropertyTextureBakeRecordBindable(record, 77u));
    EXPECT_FALSE(Runtime::IsPropertyTextureBakeRecordBindable(record, 78u))
        << "a dependency changed after the last evaluation";

    auto pending = record;
    pending.State = Runtime::PropertyTextureBakeOutputState::Pending;
    EXPECT_FALSE(Runtime::IsPropertyTextureBakeRecordBindable(pending, 77u));
    auto stale = record;
    stale.Freshness = PropertyTextureBakeFreshness::UvChanged;
    EXPECT_FALSE(Runtime::IsPropertyTextureBakeRecordBindable(stale, 77u));
    auto unknown = record;
    unknown.Freshness = PropertyTextureBakeFreshness::Unknown;
    EXPECT_FALSE(Runtime::IsPropertyTextureBakeRecordBindable(unknown, 77u));
    auto unobserved = record;
    unobserved.ObservedRevisionToken = 0u;
    EXPECT_FALSE(Runtime::IsPropertyTextureBakeRecordBindable(unobserved, 0u));
}

TEST(RuntimeTextureBakeModule, CanonicalBindingSharesCornerAtlasAndRecordsSourceIdentity)
{
    BakeHarness harness{};
    ASSERT_TRUE(harness.Start());
    const ECS::EntityHandle entity = MakeSeamedQuad(harness.Scene());

    const auto result = harness.Service->Bake(HeatRequest(harness, entity, "heat"));
    ASSERT_EQ(result.Status, PropertyTextureBakeStatus::Scheduled) << result.Diagnostic;

    const auto record = RecordNamed(harness, entity, "heat");
    EXPECT_FALSE(record.Texcoords.HasName());
    EXPECT_EQ(record.ResolvedTexcoords.Domain, GeometryElementDomain::MeshHalfedge);
    EXPECT_EQ(record.ResolvedTexcoords.Name, "h:texcoord");
    EXPECT_NE(record.UvFingerprint, 0u);
    EXPECT_NE(record.PositionFingerprint, 0u);
    EXPECT_NE(record.TopologyFingerprint, 0u);
    EXPECT_NE(record.PropertyFingerprint, 0u);
    EXPECT_EQ(record.Freshness, PropertyTextureBakeFreshness::Fresh);
    EXPECT_EQ(record.ChartCount, 2u) << "the diagonal UV seam splits the quad into two charts";
    EXPECT_GT(record.CoveredTexels, 0u);
    EXPECT_EQ(record.Storage, Runtime::PropertyTextureBakeStorage::RawFloat)
        << "raw float storage accepts chart-aware padding";
    EXPECT_EQ(record.PaddingTexels, 2u);
    EXPECT_EQ(record.State, Runtime::PropertyTextureBakeOutputState::Pending);
    EXPECT_FLOAT_EQ(record.RangeMin, -2.0f);
    EXPECT_FLOAT_EQ(record.RangeMax, 3.5f);

    // Extraction computes the same token from the live mesh, so a completed
    // copy of this record would be bindable, and an in-flight one is not.
    const std::uint64_t token = Runtime::ComputePropertyTextureBakeRevisionToken(
        record, LiveLookup(harness.Scene(), entity));
    EXPECT_EQ(record.ObservedRevisionToken, token);
    EXPECT_FALSE(Runtime::IsPropertyTextureBakeRecordBindable(record, token));
    auto ready = record;
    ready.State = Runtime::PropertyTextureBakeOutputState::Ready;
    EXPECT_TRUE(Runtime::IsPropertyTextureBakeRecordBindable(ready, token));
}

TEST(RuntimeTextureBakeModule, ExplicitAtlasBindingIsExactAndNeverReplacedByCanonicalUvs)
{
    BakeHarness harness{};
    ASSERT_TRUE(harness.Start());
    const ECS::EntityHandle entity = MakeSeamedQuad(harness.Scene());
    const Runtime::GeometryPropertyRef customAtlas{
        .Domain = GeometryElementDomain::MeshHalfedge,
        .Name = "h:custom_atlas",
    };

    ASSERT_TRUE(harness.Service->Bake(HeatRequest(harness, entity, "canonical")).Succeeded());
    const auto custom = harness.Service->Bake(HeatRequest(harness, entity, "custom", customAtlas));
    ASSERT_EQ(custom.Status, PropertyTextureBakeStatus::Scheduled) << custom.Diagnostic;
    const auto vertex = harness.Service->Bake(HeatRequest(
        harness, entity, "vertex",
        {.Domain = GeometryElementDomain::MeshVertex, .Name = "v:texcoord"}));
    ASSERT_EQ(vertex.Status, PropertyTextureBakeStatus::Scheduled) << vertex.Diagnostic;

    const auto canonicalRecord = RecordNamed(harness, entity, "canonical");
    const auto customRecord = RecordNamed(harness, entity, "custom");
    const auto vertexRecord = RecordNamed(harness, entity, "vertex");
    EXPECT_EQ(customRecord.Texcoords, customAtlas);
    EXPECT_EQ(customRecord.ResolvedTexcoords.Name, "h:custom_atlas");
    EXPECT_EQ(customRecord.ChartCount, 1u);
    EXPECT_NE(customRecord.UvFingerprint, canonicalRecord.UvFingerprint);
    EXPECT_EQ(customRecord.PropertyFingerprint, canonicalRecord.PropertyFingerprint);
    EXPECT_EQ(vertexRecord.ResolvedTexcoords.Domain, GeometryElementDomain::MeshVertex);
    EXPECT_EQ(vertexRecord.ResolvedTexcoords.Name, "v:texcoord");

    // Republishing the canonical atlas stales only the bake bound to it.
    Mutate<glm::vec2>(harness.Scene().Raw().get<GS::Halfedges>(entity).Properties, "h:texcoord",
                      [](auto& uv) { uv[0].x += 0.5f / kAtlasExtent; });
    EXPECT_EQ(RecordNamed(harness, entity, "canonical").Freshness, PropertyTextureBakeFreshness::UvChanged);
    EXPECT_EQ(RecordNamed(harness, entity, "custom").Freshness, PropertyTextureBakeFreshness::Fresh);
    EXPECT_EQ(RecordNamed(harness, entity, "vertex").Freshness, PropertyTextureBakeFreshness::Fresh);

    Mutate<glm::vec2>(harness.Scene().Raw().get<GS::Halfedges>(entity).Properties, "h:custom_atlas",
                      [](auto& uv) { uv[4].y -= 1.0f / kAtlasExtent; });
    EXPECT_EQ(RecordNamed(harness, entity, "custom").Freshness, PropertyTextureBakeFreshness::UvChanged);

    // Removing the canonical corner atlas makes v:texcoord authoritative for
    // the canonical binding: a different atlas, still stale.
    auto& halfedgeProperties = harness.Scene().Raw().get<GS::Halfedges>(entity).Properties;
    auto canonicalUv = halfedgeProperties.Get<glm::vec2>("h:texcoord");
    ASSERT_TRUE(canonicalUv.IsValid());
    halfedgeProperties.Remove(canonicalUv);
    EXPECT_EQ(RecordNamed(harness, entity, "canonical").Freshness, PropertyTextureBakeFreshness::UvChanged);
}

TEST(RuntimeTextureBakeModule, InvalidAtlasBindingsFailWithoutFallback)
{
    BakeHarness harness{};
    ASSERT_TRUE(harness.Start());
    const ECS::EntityHandle entity = MakeSeamedQuad(harness.Scene());

    const auto missing = harness.Service->Bake(HeatRequest(
        harness, entity, "missing",
        {.Domain = GeometryElementDomain::MeshHalfedge, .Name = "h:absent"}));
    EXPECT_EQ(missing.Status, PropertyTextureBakeStatus::MissingTexcoords);
    EXPECT_NE(missing.Diagnostic.find("h:absent"), std::string::npos) << missing.Diagnostic;

    const auto wrongDomain = harness.Service->Bake(HeatRequest(
        harness, entity, "face",
        {.Domain = GeometryElementDomain::MeshFace, .Name = "h:texcoord"}));
    EXPECT_EQ(wrongDomain.Status, PropertyTextureBakeStatus::MissingTexcoords);

    const auto wrongKind = harness.Service->Bake(HeatRequest(
        harness, entity, "kind",
        {.Domain = GeometryElementDomain::MeshVertex,
         .Name = "v:position",
         .ValueKind = Geometry::PropertyValueKind::Vec3}));
    EXPECT_EQ(wrongKind.Status, PropertyTextureBakeStatus::MissingTexcoords);

    const auto snapshot = harness.Service->Snapshot(Runtime::StableEntityLookup::ToRenderId(entity));
    EXPECT_TRUE(snapshot.Textures.empty()) << "rejected requests publish no record";
}

TEST(RuntimeTextureBakeModule, PositionTopologyAndPropertyEditsStaleTheBakeAndUndoRestoresIt)
{
    BakeHarness harness{};
    ASSERT_TRUE(harness.Start());
    const ECS::EntityHandle entity = MakeSeamedQuad(harness.Scene());
    ASSERT_TRUE(harness.Service->Bake(HeatRequest(harness, entity, "heat")).Succeeded());
    auto& vertexProperties = harness.Scene().Raw().get<GS::Vertices>(entity).Properties;

    Mutate<glm::vec3>(vertexProperties, "v:position", [](auto& positions) { positions[2].z = 0.25f; });
    EXPECT_EQ(RecordNamed(harness, entity, "heat").Freshness, PropertyTextureBakeFreshness::PositionsChanged);
    Mutate<glm::vec3>(vertexProperties, "v:position", [](auto& positions) { positions[2].z = 0.0f; });
    EXPECT_EQ(RecordNamed(harness, entity, "heat").Freshness, PropertyTextureBakeFreshness::Fresh)
        << "restoring the baked content (undo) restores freshness";

    Mutate<float>(vertexProperties, "v:heat", [](auto& heat) { heat[1] = 1.0f; });
    EXPECT_EQ(RecordNamed(harness, entity, "heat").Freshness, PropertyTextureBakeFreshness::PropertyChanged);
    Mutate<float>(vertexProperties, "v:heat", [](auto& heat) { heat[1] = 0.0f; });
    EXPECT_EQ(RecordNamed(harness, entity, "heat").Freshness, PropertyTextureBakeFreshness::Fresh);

    auto& faceProperties = harness.Scene().Raw().get<GS::Faces>(entity).Properties;
    Mutate<std::uint32_t>(faceProperties, std::string{GS::PropertyNames::kFaceHalfedge},
                          [](auto& firstHalfedge) { firstHalfedge[0] = 1u; });
    EXPECT_EQ(RecordNamed(harness, entity, "heat").Freshness, PropertyTextureBakeFreshness::TopologyChanged)
        << "rotating a face loop reorders its corners";

    auto heat = vertexProperties.Get<float>("v:heat");
    ASSERT_TRUE(heat.IsValid());
    vertexProperties.Remove(heat);
    EXPECT_EQ(RecordNamed(harness, entity, "heat").Freshness, PropertyTextureBakeFreshness::SourceUnavailable);
}

TEST(RuntimeTextureBakeModule, OverlappingUnderresolvedAndUncoveredAtlasesFailExplicitly)
{
    BakeHarness harness{};
    ASSERT_TRUE(harness.Start());
    const ECS::EntityHandle entity = MakeSeamedQuad(harness.Scene());
    auto& halfedgeProperties = harness.Scene().Raw().get<GS::Halfedges>(entity).Properties;
    const Runtime::GeometryPropertyRef probe{
        .Domain = GeometryElementDomain::MeshHalfedge,
        .Name = "h:probe_atlas",
    };
    const auto setProbe = [&](std::vector<glm::vec2> uv)
    {
        uv.resize(10u, glm::vec2{0.0f});
        Mutate<glm::vec2>(halfedgeProperties, probe.Name, [&uv](auto& values) { values = uv; });
    };

    // Face 1 folded inside face 0's texels (a mirrored/stacked chart).
    setProbe({Texel(7.0f, 1.0f), Texel(7.0f, 7.0f), Texel(1.0f, 1.0f),
              Texel(6.0f, 6.0f), Texel(6.0f, 2.0f), Texel(2.0f, 2.0f)});
    const auto overlap = harness.Service->Bake(HeatRequest(harness, entity, "overlap", probe));
    EXPECT_EQ(overlap.Status, PropertyTextureBakeStatus::OverlappingUvCharts) << overlap.Diagnostic;

    // Face 1 is a valid chart that falls between texel centres at 16x16.
    setProbe({Texel(7.0f, 1.0f), Texel(7.0f, 7.0f), Texel(1.0f, 1.0f),
              Texel(12.4f, 12.1f), Texel(12.1f, 12.4f), Texel(12.1f, 12.1f)});
    const auto tiny = harness.Service->Bake(HeatRequest(harness, entity, "tiny", probe));
    EXPECT_EQ(tiny.Status, PropertyTextureBakeStatus::UnderresolvedAtlas) << tiny.Diagnostic;
    EXPECT_NE(tiny.Diagnostic.find("1 of 2 UV charts"), std::string::npos) << tiny.Diagnostic;

    auto uncovered = HeatRequest(harness, entity, "uncovered", probe);
    uncovered.Width = 1u;
    uncovered.Height = 1u;
    setProbe({Texel(1.0f, 1.0f), Texel(3.0f, 1.0f), Texel(1.0f, 3.0f),
              Texel(12.0f, 12.0f), Texel(14.0f, 12.0f), Texel(12.0f, 14.0f)});
    const auto zero = harness.Service->Bake(uncovered);
    EXPECT_EQ(zero.Status, PropertyTextureBakeStatus::ZeroCoverageBake) << zero.Diagnostic;

    EXPECT_TRUE(harness.Service->Snapshot(Runtime::StableEntityLookup::ToRenderId(entity)).Textures.empty());
}

TEST(RuntimeTextureBakeModule, AtlasTextureTabsExposeBindingsCoverageAndAllOutputStates)
{
    BakeHarness harness{};
    ASSERT_TRUE(harness.Start());
    const auto entity = MakeSeamedQuad(harness.Scene());
    const auto id = Runtime::StableEntityLookup::ToRenderId(entity);
    ASSERT_EQ(harness.Service->Bake(HeatRequest(harness, entity, "heat")).Status,
              PropertyTextureBakeStatus::Scheduled);
    Runtime::EditorWorkspaceAttachment attachment{};
    attachment.Attach(harness.Worlds, harness.Services);
    ASSERT_TRUE(Runtime::PrepareEditorWorkspaceSnapshotFrame(attachment));
    const auto commands = Runtime::PrepareEditorParameterizationFrame(attachment).UvViewCommands;
    ASSERT_TRUE(commands.TextureTabs);
    auto tabs = commands.TextureTabs(id);
    ASSERT_EQ(tabs.size(), 1u);
    EXPECT_EQ(tabs[0].Name, "heat");
    EXPECT_EQ(tabs[0].State, Runtime::EditorParameterizationTextureState::Pending);
    EXPECT_EQ(tabs[0].ResolvedTexcoords.Name, "h:texcoord");
    EXPECT_NE(tabs[0].TextureAssetId, tabs[0].CoverageTextureAssetId);
    EXPECT_NE(tabs[0].CoverageTextureAssetId, 0u);
    EXPECT_TRUE(tabs[0].RawFloat);
    EXPECT_FLOAT_EQ(tabs[0].RangeMin, -2.0f);
    EXPECT_FLOAT_EQ(tabs[0].RangeMax, 3.5f);
    // Exercise the query's state projection using its canonical catalog;
    // actual completion and texture residency are covered by the Vulkan smoke.
    auto& record = harness.Scene().Raw().get<Runtime::PropertyTextureBakeOutputs>(entity).Records[0];
    record.State = Runtime::PropertyTextureBakeOutputState::Ready;
    EXPECT_EQ(commands.TextureTabs(id)[0].State, Runtime::EditorParameterizationTextureState::Ready);
    Mutate<float>(harness.Scene().Raw().get<GS::Vertices>(entity).Properties, "v:heat",
                  [](auto& values) { values[0] = 7.0f; });
    EXPECT_EQ(commands.TextureTabs(id)[0].State, Runtime::EditorParameterizationTextureState::Stale);
    record.State = Runtime::PropertyTextureBakeOutputState::Failed;
    record.Diagnostic = "test failure";
    tabs = commands.TextureTabs(id);
    EXPECT_EQ(tabs[0].State, Runtime::EditorParameterizationTextureState::Failed);
    EXPECT_EQ(tabs[0].Diagnostic, "test failure");
    attachment.Detach();
    EXPECT_TRUE(commands.TextureTabs(id).empty());
}

TEST(RuntimeTextureBakeModule, RawVectorDisplayRangeIncludesNegativeComponents)
{
    BakeHarness harness{};
    ASSERT_TRUE(harness.Start());
    const auto entity = MakeSeamedQuad(harness.Scene());
    (void)harness.Scene().Raw().get<GS::Vertices>(entity).Properties.GetOrAdd<glm::vec3>(
        "v:vector", glm::vec3{-5.0f, 0.0f, 3.0f});
    auto request = HeatRequest(harness, entity, "vector");
    request.Source.Name = "v:vector";
    request.Source.ValueKind = Geometry::PropertyValueKind::Vec3;
    request.Storage = Runtime::PropertyTextureBakeStorage::RawFloat;
    ASSERT_EQ(harness.Service->Bake(request).Status, PropertyTextureBakeStatus::Scheduled);
    const auto record = RecordNamed(harness, entity, "vector");
    EXPECT_FLOAT_EQ(record.RangeMin, -5.0f);
    EXPECT_FLOAT_EQ(record.RangeMax, 3.0f);
}

TEST(RuntimeTextureBakeModule, TinyPositiveUvTrianglesReportResolutionInsteadOfDegeneracy)
{
    BakeHarness harness{};
    ASSERT_TRUE(harness.Start());
    const auto entity = MakeSeamedQuad(harness.Scene());
    auto uv = harness.Scene().Raw().get<GS::Halfedges>(entity).Properties.Get<glm::vec2>("h:texcoord");
    uv[0] = {0.0f, 0.0f}; uv[1] = {1.0e-5f, 0.0f}; uv[2] = {0.0f, 1.0e-6f};
    const auto result = harness.Service->Bake(HeatRequest(harness, entity, "tiny"));
    EXPECT_EQ(result.Status, PropertyTextureBakeStatus::UnderresolvedAtlas) << result.Diagnostic;
}

TEST(RuntimeTextureBakeModule, AdaptiveExtentDoublesUntilEveryChartResolvesWithinItsBound)
{
    BakeHarness harness{};
    ASSERT_TRUE(harness.Start());
    const auto entity = MakeSeamedQuad(harness.Scene());
    const Runtime::GeometryPropertyRef probe{
        .Domain = GeometryElementDomain::MeshHalfedge,
        .Name = "h:probe_atlas",
    };
    // Face 1 is a valid chart between texel centres at 16x16 whose interior
    // holds the centre (24.5, 24.5) at 32x32.
    SetTriangleCornerUvs(harness.Scene(), entity, probe.Name, kAtlasExtent,
                         {{7.0f, 1.0f}, {7.0f, 7.0f}, {1.0f, 1.0f},
                          {12.45f, 12.1f}, {12.1f, 12.45f}, {12.1f, 12.1f}});

    const auto fixed = harness.Service->Bake(HeatRequest(harness, entity, "fixed", probe));
    EXPECT_EQ(fixed.Status, PropertyTextureBakeStatus::UnderresolvedAtlas) << fixed.Diagnostic;

    auto bounded = HeatRequest(harness, entity, "bounded", probe);
    bounded.MaxAdaptiveExtent = 16u;
    const auto boundedResult = harness.Service->Bake(bounded);
    EXPECT_EQ(boundedResult.Status, PropertyTextureBakeStatus::UnderresolvedAtlas) << boundedResult.Diagnostic;
    EXPECT_NE(boundedResult.Diagnostic.find("within 16"), std::string::npos) << boundedResult.Diagnostic;

    auto adaptive = HeatRequest(harness, entity, "adaptive", probe);
    adaptive.MaxAdaptiveExtent = 64u;
    const auto adaptiveResult = harness.Service->Bake(adaptive);
    ASSERT_EQ(adaptiveResult.Status, PropertyTextureBakeStatus::Scheduled) << adaptiveResult.Diagnostic;
    const auto record = RecordNamed(harness, entity, "adaptive");
    EXPECT_EQ(record.Width, 32u) << "the smallest doubling that resolves every chart is adopted";
    EXPECT_EQ(record.Height, 32u);
    EXPECT_EQ(record.ChartCount, 2u);

    auto belowRequest = HeatRequest(harness, entity, "below", probe);
    belowRequest.MaxAdaptiveExtent = 8u;
    EXPECT_EQ(harness.Service->Bake(belowRequest).Status, PropertyTextureBakeStatus::InvalidResolution);
    auto aboveCap = HeatRequest(harness, entity, "above", probe);
    aboveCap.MaxAdaptiveExtent = 2u * Runtime::kPropertyTextureBakeMaxExtent;
    EXPECT_EQ(harness.Service->Bake(aboveCap).Status, PropertyTextureBakeStatus::InvalidResolution);
    auto oversized = HeatRequest(harness, entity, "oversized", probe);
    oversized.Width = 2u * Runtime::kPropertyTextureBakeMaxExtent;
    EXPECT_EQ(harness.Service->Bake(oversized).Status, PropertyTextureBakeStatus::InvalidResolution);
}

TEST(RuntimeTextureBakeModule, AppearanceBakeUsesTheRecordedAtlasExtentAndNeverEscalates)
{
    BakeHarness harness{};
    ASSERT_TRUE(harness.Start());
    const auto entity = MakeSeamedQuad(harness.Scene());
    // Face 1 is sub-texel at the default 1024 extent and contains the texel
    // centre (1000.5, 1000.5) of a 2048 atlas.
    SetTriangleCornerUvs(harness.Scene(), entity, "h:texcoord", 1024.0f,
                         {{400.0f, 50.0f}, {400.0f, 400.0f}, {50.0f, 50.0f},
                          {500.45f, 500.1f}, {500.1f, 500.45f}, {500.1f, 500.1f}});
    (void)EnableHeatAppearance(harness.Scene(), entity);
    const auto requests = [&harness] { return harness.Service->Stats().BakeRequests; };

    harness.RunMaintenance();
    const auto unrecorded = RecordNamed(harness, entity, Runtime::kSurfaceAppearanceTextureOutput);
    EXPECT_EQ(unrecorded.State, Runtime::PropertyTextureBakeOutputState::Failed);
    EXPECT_NE(unrecorded.Diagnostic.find("1024x1024"), std::string::npos) << unrecorded.Diagnostic;
    EXPECT_EQ(unrecorded.Width, 1024u) << "UVs without a recorded extent use the default and are not escalated";
    const std::uint64_t rejected = requests();
    harness.RunMaintenance();
    EXPECT_EQ(requests(), rejected);

    // Recording the generating atlas's extent is a new request at that extent.
    ASSERT_TRUE(Runtime::PublishMeshUvAtlasExtent(harness.Scene().Raw(), entity, 2048u, 2048u));
    harness.RunMaintenance();
    EXPECT_EQ(requests(), rejected + 1u);
    const auto record = RecordNamed(harness, entity, Runtime::kSurfaceAppearanceTextureOutput);
    EXPECT_EQ(record.State, Runtime::PropertyTextureBakeOutputState::Pending) << record.Diagnostic;
    EXPECT_EQ(record.Width, 2048u);
    EXPECT_EQ(record.Height, 2048u);
    EXPECT_EQ(record.ChartCount, 2u);
    harness.RunMaintenance();
    EXPECT_EQ(requests(), rejected + 1u) << "a current appearance bake is not resubmitted";
}

TEST(RuntimeTextureBakeModule, AppearanceBakeFollowsArbitraryRecordedExtentsOnlyWhileTheUvsAreCurrent)
{
    BakeHarness harness{};
    ASSERT_TRUE(harness.Start());
    const auto entity = MakeSeamedQuad(harness.Scene());
    (void)EnableHeatAppearance(harness.Scene(), entity);
    auto& raw = harness.Scene().Raw();
    const auto appearance = [&harness, entity]
    {
        return RecordNamed(harness, entity, Runtime::kSurfaceAppearanceTextureOutput);
    };
    // A rebake reloads the previous pair in place, which needs it Ready.
    const auto settle = [&harness, &appearance]
    {
        const auto record = appearance();
        ForceReady(harness.Assets(), record.Texture);
        ForceReady(harness.Assets(), record.CoverageTexture);
    };

    ASSERT_TRUE(Runtime::PublishMeshUvAtlasExtent(raw, entity, 1536u, 1536u));
    harness.RunMaintenance();
    EXPECT_EQ(appearance().Width, 1536u) << appearance().Diagnostic;
    EXPECT_EQ(appearance().Height, 1536u);
    EXPECT_EQ(appearance().Freshness, PropertyTextureBakeFreshness::Fresh);

    // Shadow vertex UVs do not change the canonical corner atlas or its grid.
    auto shadow = raw.get<GS::Vertices>(entity).Properties.GetOrAdd<glm::vec2>("v:texcoord");
    shadow[0] = {42.0f, -7.0f};
    ASSERT_TRUE(Runtime::FindCurrentMeshUvAtlasExtent(raw, entity));
    ASSERT_TRUE(Runtime::RefreshMeshUvAtlasExtent(raw, entity));

    // A move of the geometry component rebases property revisions without
    // changing a UV; the content binding keeps the extent current.
    auto& halfedges = raw.get<GS::Halfedges>(entity).Properties;
    auto moved = std::move(halfedges);
    halfedges = std::move(moved);
    ASSERT_TRUE(Runtime::FindCurrentMeshUvAtlasExtent(raw, entity).has_value());

    // A metadata-only change with identical UVs rebakes at the new extent.
    settle();
    ASSERT_TRUE(Runtime::PublishMeshUvAtlasExtent(raw, entity, 512u, 512u));
    harness.RunMaintenance();
    EXPECT_EQ(appearance().Width, 512u) << appearance().Diagnostic;
    EXPECT_EQ(appearance().State, Runtime::PropertyTextureBakeOutputState::Pending);

    // A manual UV edit makes the recorded extent stale: the default applies.
    SetTriangleCornerUvs(harness.Scene(), entity, "h:texcoord", kAtlasExtent,
                         {{7.0f, 1.0f}, {7.0f, 7.0f}, {1.0f, 1.0f},
                          {15.0f, 7.0f}, {9.0f, 7.0f}, {9.5f, 1.0f}});
    EXPECT_FALSE(Runtime::FindCurrentMeshUvAtlasExtent(raw, entity).has_value());
    settle();
    harness.RunMaintenance();
    EXPECT_EQ(appearance().Width, 1024u) << appearance().Diagnostic;
    EXPECT_TRUE(raw.all_of<Runtime::MeshUvAtlasExtent>(entity)) << "retain its binding for UV undo";
    EXPECT_FALSE(Runtime::FindCurrentMeshUvAtlasExtent(raw, entity));
}

TEST(RuntimeTextureBakeModule, RecordedExtentAboveTheBakeCapFailsOnceWithoutAllocating)
{
    BakeHarness harness{};
    ASSERT_TRUE(harness.Start());
    const auto entity = MakeSeamedQuad(harness.Scene());
    (void)EnableHeatAppearance(harness.Scene(), entity);
    const std::uint32_t atlas = Runtime::kPropertyTextureBakeMaxExtent + 4096u;
    ASSERT_TRUE(Runtime::PublishMeshUvAtlasExtent(harness.Scene().Raw(), entity, atlas, atlas))
        << "UV generation accepts atlases larger than the bake cap";
    const std::size_t live = harness.Assets().LiveAssetCount();

    harness.RunMaintenance();
    const auto record = RecordNamed(harness, entity, Runtime::kSurfaceAppearanceTextureOutput);
    EXPECT_EQ(record.State, Runtime::PropertyTextureBakeOutputState::Failed);
    EXPECT_NE(record.Diagnostic.find(std::to_string(atlas)), std::string::npos) << record.Diagnostic;
    EXPECT_FALSE(record.Texture.IsValid());
    EXPECT_EQ(harness.Assets().LiveAssetCount(), live);
    const std::uint64_t requests = harness.Service->Stats().BakeRequests;
    for (int frame = 0; frame < 3; ++frame)
        harness.RunMaintenance();
    EXPECT_EQ(harness.Service->Stats().BakeRequests, requests);
}

TEST(RuntimeTextureBakeModule, SnapshotLargerThanTheWholeBudgetIsAPermanentFailure)
{
    BakeHarness harness{};
    ASSERT_TRUE(harness.Start());
    const auto entity = MakeSeamedQuad(harness.Scene());

    // Two triangles retain at least 32 bytes: rejected before coverage.
    harness.Service->SetSourceSnapshotBudgetForTest(16u);
    const auto floor = harness.Service->Bake(HeatRequest(harness, entity, "floor"));
    EXPECT_EQ(floor.Status, PropertyTextureBakeStatus::BakeFailed) << floor.Diagnostic;
    EXPECT_NE(floor.Diagnostic.find("budget"), std::string::npos) << floor.Diagnostic;
    // Above the floor but below the prepared snapshot, with an empty queue.
    harness.Service->SetSourceSnapshotBudgetForTest(40u);
    const auto exact = harness.Service->Bake(HeatRequest(harness, entity, "exact"));
    EXPECT_EQ(exact.Status, PropertyTextureBakeStatus::BakeFailed) << exact.Diagnostic;
    EXPECT_NE(exact.Diagnostic.find("budget"), std::string::npos) << exact.Diagnostic;
    EXPECT_TRUE(harness.Service->Snapshot(Runtime::StableEntityLookup::ToRenderId(entity)).Textures.empty());

    // Automatic appearance records the failure once and does not retry an
    // unchanged request, where a transient rejection would retry every frame.
    (void)EnableHeatAppearance(harness.Scene(), entity);
    harness.RunMaintenance();
    const auto record = RecordNamed(harness, entity, Runtime::kSurfaceAppearanceTextureOutput);
    EXPECT_EQ(record.State, Runtime::PropertyTextureBakeOutputState::Failed);
    EXPECT_NE(record.Diagnostic.find("budget"), std::string::npos) << record.Diagnostic;
    const std::uint64_t requests = harness.Service->Stats().BakeRequests;
    for (int frame = 0; frame < 3; ++frame)
        harness.RunMaintenance();
    EXPECT_EQ(harness.Service->Stats().BakeRequests, requests);

    harness.Service->SetSourceSnapshotBudgetForTest(64u * 1024u * 1024u);
    EXPECT_EQ(harness.Service->Bake(HeatRequest(harness, entity, "fits")).Status,
              PropertyTextureBakeStatus::Scheduled);
}

TEST(RuntimeTextureBakeModule, RejectedAppearanceBakeRetriesOnlyAfterItsDependenciesOrConfigChange)
{
    BakeHarness harness{};
    ASSERT_TRUE(harness.Start());
    const auto entity = MakeSeamedQuad(harness.Scene());
    auto& vertexProperties = harness.Scene().Raw().get<GS::Vertices>(entity).Properties;
    (void)vertexProperties.GetOrAdd<float>("v:unrelated", 0.0f);
    // Face 1 folded inside face 0: overlapping at every extent.
    SetTriangleCornerUvs(harness.Scene(), entity, "h:texcoord", kAtlasExtent,
                         {{7.0f, 1.0f}, {7.0f, 7.0f}, {1.0f, 1.0f},
                          {6.0f, 6.0f}, {6.0f, 2.0f}, {2.0f, 2.0f}});
    auto& config = EnableHeatAppearance(harness.Scene(), entity);

    harness.RunMaintenance();
    const auto rejected = RecordNamed(harness, entity, Runtime::kSurfaceAppearanceTextureOutput);
    EXPECT_EQ(rejected.State, Runtime::PropertyTextureBakeOutputState::Failed);
    EXPECT_NE(rejected.Diagnostic.find("non-overlapping"), std::string::npos) << rejected.Diagnostic;
    EXPECT_EQ(rejected.Freshness, PropertyTextureBakeFreshness::Unknown);
    EXPECT_FALSE(Runtime::IsPropertyTextureBakeRecordBindable(
        rejected, Runtime::ComputePropertyTextureBakeRevisionToken(rejected, LiveLookup(harness.Scene(), entity))));

    const auto requests = [&harness] { return harness.Service->Stats().BakeRequests; };
    const std::uint64_t first = requests();
    for (int frame = 0; frame < 3; ++frame)
        harness.RunMaintenance();
    EXPECT_EQ(requests(), first) << "an unchanged rejected request is not resubmitted every frame";

    Mutate<float>(vertexProperties, "v:unrelated", [](auto& values) { values[0] = 1.0f; });
    harness.RunMaintenance();
    EXPECT_EQ(requests(), first) << "properties the bake does not read never trigger a retry";

    config.Scalar.Map = Graphics::Colormap::Type::Plasma;
    harness.RunMaintenance();
    EXPECT_EQ(requests(), first + 1u) << "a changed appearance request is submitted";
    harness.RunMaintenance();
    EXPECT_EQ(requests(), first + 1u) << "and gated again once rejected for the same atlas";

    // Republishing a valid atlas resubmits and schedules at the default extent.
    SetTriangleCornerUvs(harness.Scene(), entity, "h:texcoord", kAtlasExtent,
                         {{7.0f, 1.0f}, {7.0f, 7.0f}, {1.0f, 1.0f},
                          {15.0f, 7.0f}, {9.0f, 7.0f}, {9.0f, 1.0f}});
    harness.RunMaintenance();
    EXPECT_EQ(requests(), first + 2u);
    const auto scheduled = RecordNamed(harness, entity, Runtime::kSurfaceAppearanceTextureOutput);
    EXPECT_EQ(scheduled.State, Runtime::PropertyTextureBakeOutputState::Pending) << scheduled.Diagnostic;
    EXPECT_EQ(scheduled.Freshness, PropertyTextureBakeFreshness::Fresh);
    EXPECT_EQ(scheduled.Width, 1024u) << "atlases resolved at the default scale allocate no more";
    EXPECT_EQ(scheduled.EncodingColormap, Graphics::Colormap::Type::Plasma);
}

TEST(RuntimeTextureBakeModule, RebakeReloadsTheAssetPairInPlaceOrLeavesItUntouched)
{
    BakeHarness harness{};
    ASSERT_TRUE(harness.Start());
    const auto entity = MakeSeamedQuad(harness.Scene());
    auto& assets = harness.Assets();
    ASSERT_EQ(harness.Service->Bake(HeatRequest(harness, entity, "heat")).Status,
              PropertyTextureBakeStatus::Scheduled);
    const auto first = RecordNamed(harness, entity, "heat");
    ForceReady(assets, first.Texture);
    ForceReady(assets, first.CoverageTexture);
    const std::size_t live = assets.LiveAssetCount();
    const auto valueTicket = assets.GetPayloadTicket(first.Texture);
    const auto coverageTicket = assets.GetPayloadTicket(first.CoverageTexture);
    ASSERT_TRUE(valueTicket.has_value());
    ASSERT_TRUE(coverageTicket.has_value());

    // The coverage asset is still loading: the pair is not admitted, so the
    // ready value asset must not be reloaded ahead of it.
    ASSERT_TRUE(assets.ForceAssetState(first.CoverageTexture, Extrinsic::Assets::AssetState::Ready,
                                       Extrinsic::Assets::AssetState::QueuedIO).has_value());
    auto rebake = HeatRequest(harness, entity, "heat");
    rebake.PaddingTexels = 4u;
    const auto busy = harness.Service->Bake(rebake);
    EXPECT_EQ(busy.Status, PropertyTextureBakeStatus::JobSubmitFailed) << busy.Diagnostic;
    const auto unchanged = RecordNamed(harness, entity, "heat");
    EXPECT_EQ(unchanged.Texture, first.Texture);
    EXPECT_EQ(unchanged.CoverageTexture, first.CoverageTexture);
    EXPECT_EQ(unchanged.Generation, first.Generation);
    EXPECT_EQ(unchanged.PaddingTexels, 2u);
    EXPECT_EQ(assets.GetPayloadTicket(first.Texture).value(), *valueTicket);
    EXPECT_EQ(assets.LiveAssetCount(), live);

    ASSERT_TRUE(assets.ForceAssetState(first.CoverageTexture, Extrinsic::Assets::AssetState::QueuedIO,
                                       Extrinsic::Assets::AssetState::Ready).has_value());
    const auto rebaked = harness.Service->Bake(rebake);
    ASSERT_EQ(rebaked.Status, PropertyTextureBakeStatus::Scheduled) << rebaked.Diagnostic;
    const auto second = RecordNamed(harness, entity, "heat");
    EXPECT_EQ(second.Texture, first.Texture) << "a successful rebake keeps the generated texture id";
    EXPECT_EQ(second.CoverageTexture, first.CoverageTexture);
    EXPECT_NE(second.Generation, first.Generation);
    EXPECT_EQ(second.PaddingTexels, 4u);
    EXPECT_NE(assets.GetPayloadTicket(first.Texture).value(), *valueTicket) << "both assets carry the new metadata";
    EXPECT_NE(assets.GetPayloadTicket(first.CoverageTexture).value(), *coverageTicket);
    EXPECT_EQ(assets.LiveAssetCount(), live);

    ASSERT_TRUE(harness.Service->Remove(Runtime::StableEntityLookup::ToRenderId(entity), "heat").Succeeded());
    EXPECT_FALSE(assets.IsAlive(first.Texture));
    EXPECT_FALSE(assets.IsAlive(first.CoverageTexture));
    EXPECT_EQ(assets.LiveAssetCount(), live - 2u);
}

TEST(RuntimeTextureBakeModule, FailedCoverageCreationDestroysTheNewValueAsset)
{
    BakeHarness harness{};
    ASSERT_TRUE(harness.Start());
    const auto entity = MakeSeamedQuad(harness.Scene());
    auto& assets = harness.Assets();
    // A fresh service names its first pair's generated assets with serials 1
    // (value) and 2 (coverage). A foreign payload at the coverage path makes
    // only the second creation of the pair fail.
    const std::string coveragePath =
        "intrinsic-runtime-generated/property-texture/v1/entity-" +
        std::to_string(Runtime::StableEntityLookup::ToRenderId(entity)) + "-2.metadata";
    ASSERT_TRUE(assets.Load<std::uint32_t>(
                          coveragePath,
                          [](std::string_view, Extrinsic::Assets::AssetId) -> Core::Expected<std::uint32_t>
                          {
                              return 7u;
                          })
                    .has_value());
    const std::size_t live = assets.LiveAssetCount();

    const auto result = harness.Service->Bake(HeatRequest(harness, entity, "heat"));
    EXPECT_EQ(result.Status, PropertyTextureBakeStatus::AssetLoadFailed) << result.Diagnostic;
    EXPECT_NE(result.Diagnostic.find("coverage"), std::string::npos) << result.Diagnostic;
    EXPECT_EQ(assets.LiveAssetCount(), live) << "the value asset created for the rejected pair is destroyed";
    EXPECT_TRUE(harness.Service->Snapshot(Runtime::StableEntityLookup::ToRenderId(entity)).Textures.empty());
}

TEST(RuntimeTextureBakeModule, FailedGeneratedAssetIsAPermanentLoadFailureUntilRemoved)
{
    BakeHarness harness{};
    ASSERT_TRUE(harness.Start());
    const auto entity = MakeSeamedQuad(harness.Scene());
    auto& assets = harness.Assets();
    ASSERT_EQ(harness.Service->Bake(HeatRequest(harness, entity, "heat")).Status,
              PropertyTextureBakeStatus::Scheduled);
    const auto first = RecordNamed(harness, entity, "heat");
    ForceReady(assets, first.Texture);
    ForceReady(assets, first.CoverageTexture);
    ASSERT_TRUE(assets.ForceAssetState(first.Texture, Extrinsic::Assets::AssetState::Ready,
                                       Extrinsic::Assets::AssetState::Failed).has_value());
    const auto coverageTicket = assets.GetPayloadTicket(first.CoverageTexture);
    ASSERT_TRUE(coverageTicket.has_value());

    const auto failed = harness.Service->Bake(HeatRequest(harness, entity, "heat"));
    EXPECT_EQ(failed.Status, PropertyTextureBakeStatus::AssetLoadFailed) << failed.Diagnostic;
    EXPECT_NE(failed.Diagnostic.find("remove this bake output"), std::string::npos) << failed.Diagnostic;
    const auto unchanged = RecordNamed(harness, entity, "heat");
    EXPECT_EQ(unchanged.Texture, first.Texture);
    EXPECT_EQ(unchanged.Generation, first.Generation);
    EXPECT_EQ(assets.GetPayloadTicket(first.CoverageTexture).value(), *coverageTicket)
        << "the healthy half of the pair is not reloaded";

    ASSERT_TRUE(harness.Service->Remove(Runtime::StableEntityLookup::ToRenderId(entity), "heat").Succeeded());
    EXPECT_FALSE(assets.IsAlive(first.Texture));
    const auto rebaked = harness.Service->Bake(HeatRequest(harness, entity, "heat"));
    ASSERT_EQ(rebaked.Status, PropertyTextureBakeStatus::Scheduled) << rebaked.Diagnostic;
    EXPECT_NE(RecordNamed(harness, entity, "heat").Texture, first.Texture);
}
