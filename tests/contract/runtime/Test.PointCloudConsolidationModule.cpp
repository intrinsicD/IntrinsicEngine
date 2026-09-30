#include "Modules/PointCloudConsolidation/Runtime.LopPaging.TestSupport.hpp"
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <limits>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <glm/glm.hpp>

#include "RuntimeTestModule.hpp"
#include "MockRHI.hpp"
#include "SandboxEditorJobHarness.hpp"
#include <cstring>

import Extrinsic.Runtime.Module;
import Extrinsic.Runtime.AgentOperations;
import Extrinsic.Runtime.EditorWorkspaceAttachment;
import Extrinsic.Runtime.EditorWorkspaceSnapshots;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.ServiceRegistry;
import Extrinsic.Graphics.Renderer;
import Extrinsic.Runtime.GpuPropertyBinding;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Core.Config.Window;
import Extrinsic.Core.Tasks;
import Extrinsic.ECS.Component.DirtyTags;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Runtime.CommandBus;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.SpatialIndexCache;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.PointCloudConsolidationConfig;
import Extrinsic.Runtime.PointCloudConsolidationModule;
import Extrinsic.Runtime.SceneDocumentModule;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.WorldRegistry;
import Extrinsic.Runtime.WorldHandle;
import Geometry.PointCloud.Consolidation;
import Geometry.Properties;

namespace CoreConfig = Extrinsic::Core::Config;
namespace Dirty = Extrinsic::ECS::Components::DirtyTags;
namespace ECS = Extrinsic::ECS;
namespace GS = Extrinsic::ECS::Components::GeometrySources;
namespace Runtime = Extrinsic::Runtime;

TEST(PointCloudConsolidationModule, PropertyReferenceValidationPreservesOptionalNormalsAndAliasing)
{
    using Domain = Runtime::GeometryElementDomain;
    using Kind = Geometry::PropertyValueKind;
    for (const auto domain : {Domain::MeshVertex, Domain::MeshEdge, Domain::MeshHalfedge,
             Domain::MeshFace, Domain::GraphNode, Domain::GraphEdge, Domain::GraphHalfedge,
             Domain::PointCloudPoint})
    {
        SCOPED_TRACE(static_cast<unsigned>(domain));
        auto refs = Runtime::MakePointCloudConsolidationPropertyRefs(domain, "position", "normal");
        EXPECT_TRUE(Runtime::IsValidPointCloudConsolidationPropertyRefs(refs));
        refs.OutputPositions.Name = "smoothed";
        refs.OutputNormals->Name = "smoothed_normal";
        EXPECT_TRUE(Runtime::IsValidPointCloudConsolidationPropertyRefs(refs));
        refs.InputNormals.reset();
        EXPECT_TRUE(Runtime::IsValidPointCloudConsolidationPropertyRefs(refs));
        refs.OutputNormals.reset();
        EXPECT_TRUE(Runtime::IsValidPointCloudConsolidationPropertyRefs(refs));
        refs.InputNormals = Runtime::GeometryPropertyRef{domain, "normal", Kind::Vec3};
        EXPECT_TRUE(Runtime::IsValidPointCloudConsolidationPropertyRefs(refs));
    }

    for (unsigned invalid = 0; invalid < 17; ++invalid)
    {
        SCOPED_TRACE(invalid);
        auto refs = Runtime::MakePointCloudConsolidationPropertyRefs(Domain::MeshFace, "position", "normal");
        switch (invalid)
        {
        case 0: refs.InputPositions.Domain = Domain::Unknown; break;
        case 1: refs.InputPositions.Name.clear(); break;
        case 2: refs.InputPositions.ValueKind = Kind::Float; break;
        case 3: refs.OutputPositions.Domain = Domain::MeshVertex; break;
        case 4: refs.OutputPositions.Name.clear(); break;
        case 5: refs.OutputPositions.ValueKind = Kind::Float; break;
        case 6: refs.InputNormals->Domain = Domain::MeshVertex; break;
        case 7: refs.InputNormals->Name.clear(); break;
        case 8: refs.InputNormals->ValueKind = Kind::Float; break;
        case 9: refs.OutputNormals->Domain = Domain::MeshVertex; break;
        case 10: refs.OutputNormals->Name.clear(); break;
        case 11: refs.OutputNormals->ValueKind = Kind::Float; break;
        case 12:
            refs.OutputPositions.Name = "smoothed";
            refs.OutputNormals->Name = "smoothed_normal";
            refs.InputNormals->Name = refs.InputPositions.Name;
            break;
        case 13:
            refs.OutputPositions.Name = "smoothed";
            refs.OutputNormals->Name = refs.OutputPositions.Name;
            break;
        case 14:
            refs.OutputPositions.Name = "smoothed";
            refs.OutputNormals->Name = refs.InputPositions.Name;
            break;
        case 15:
            refs.OutputNormals->Name = "smoothed_normal";
            refs.OutputPositions.Name = refs.InputNormals->Name;
            break;
        case 16:
            refs = Runtime::MakePointCloudConsolidationPropertyRefs(Domain::Unknown, "position");
            break;
        }
        EXPECT_FALSE(Runtime::IsValidPointCloudConsolidationPropertyRefs(refs));
    }
}

namespace
{
    using namespace std::chrono_literals;

    struct OpaqueDomainValue
    {
        std::uint32_t Id{0u};

        [[nodiscard]] bool operator==(
            const OpaqueDomainValue&) const noexcept = default;
    };

    struct DomainSourceEntities
    {
        ECS::EntityHandle Mesh{ECS::InvalidEntityHandle};
        ECS::EntityHandle Graph{ECS::InvalidEntityHandle};
        ECS::EntityHandle PointCloud{ECS::InvalidEntityHandle};
    };

    constexpr std::array<Runtime::GeometryElementDomain, 8u>
        kAllElementDomains{
            Runtime::GeometryElementDomain::MeshVertex,
            Runtime::GeometryElementDomain::MeshEdge,
            Runtime::GeometryElementDomain::MeshHalfedge,
            Runtime::GeometryElementDomain::MeshFace,
            Runtime::GeometryElementDomain::GraphNode,
            Runtime::GeometryElementDomain::GraphHalfedge,
            Runtime::GeometryElementDomain::GraphEdge,
            Runtime::GeometryElementDomain::PointCloudPoint,
        };

    [[nodiscard]] CoreConfig::EngineConfig HeadlessConfig(
        const unsigned workers = 2u)
    {
        CoreConfig::EngineConfig config{};
        config.ReferenceScene.Enabled = false;
        config.Camera.Enabled = false;
        config.Window.Backend = CoreConfig::WindowBackend::Null;
        config.Simulation.WorkerThreadCount = workers;
        return config;
    }

    [[nodiscard]] std::vector<glm::vec3> NoisyPlane()
    {
        std::vector<glm::vec3> points{};
        for (int y = 0; y < 5; ++y)
        {
            for (int x = 0; x < 5; ++x)
            {
                const float noise =
                    ((x * 13 + y * 7) % 5 - 2) * 0.025f;
                points.emplace_back(
                    (static_cast<float>(x) - 2.0f) * 0.2f,
                    (static_cast<float>(y) - 2.0f) * 0.2f,
                    noise);
            }
        }
        return points;
    }

    [[nodiscard]] std::vector<glm::vec3> SparsePlane()
    {
        std::vector<glm::vec3> points = NoisyPlane();
        for (glm::vec3& point : points)
            point *= 100.0f;
        return points;
    }

    [[nodiscard]] ECS::EntityHandle AddPointCloud(
        ECS::Scene::Registry& scene,
        const std::vector<glm::vec3>& positions)
    {
        const ECS::EntityHandle entity = scene.Create();
        auto& vertices = scene.Raw().emplace<GS::Vertices>(entity);
        vertices.Properties.Resize(positions.size());
        auto position = vertices.Properties.GetOrAdd<glm::vec3>(
            std::string{GS::PropertyNames::kPosition},
            glm::vec3{0.0f});
        position.Vector() = positions;
        auto provenance = vertices.Properties.GetOrAdd<std::uint32_t>(
            "p:source_index", 0u);
        for (std::size_t index = 0u; index < positions.size(); ++index)
            provenance[index] = static_cast<std::uint32_t>(index);
        return entity;
    }

    void SetVec3Property(
        Geometry::PropertySet& properties,
        const std::string& name,
        const std::vector<glm::vec3>& values)
    {
        auto property = properties.GetOrAdd<glm::vec3>(
            name, glm::vec3{0.0f});
        property.Vector() = values;
    }

    void SetSequentialProperty(
        Geometry::PropertySet& properties,
        const std::string& name,
        const std::uint32_t offset)
    {
        auto property = properties.GetOrAdd<std::uint32_t>(name, 0u);
        for (std::size_t index = 0u; index < properties.Size(); ++index)
        {
            property[index] = offset + static_cast<std::uint32_t>(index);
        }
    }

    [[nodiscard]] DomainSourceEntities AddDomainSources(
        ECS::Scene::Registry& scene)
    {
        const std::vector<glm::vec3> samples = NoisyPlane();

        const ECS::EntityHandle mesh = scene.Create();
        auto& meshVertices = scene.Raw().emplace<GS::Vertices>(mesh);
        meshVertices.Properties.Resize(samples.size());
        SetVec3Property(
            meshVertices.Properties,
            std::string{GS::PropertyNames::kPosition},
            samples);
        SetVec3Property(meshVertices.Properties, "sample:position", samples);

        auto& meshEdges = scene.Raw().emplace<GS::Edges>(mesh);
        meshEdges.Properties.Resize(samples.size());
        SetSequentialProperty(
            meshEdges.Properties,
            std::string{GS::PropertyNames::kEdgeV0},
            100u);
        SetSequentialProperty(
            meshEdges.Properties,
            std::string{GS::PropertyNames::kEdgeV1},
            200u);
        SetVec3Property(meshEdges.Properties, "sample:position", samples);

        auto& meshHalfedges = scene.Raw().emplace<GS::Halfedges>(mesh);
        meshHalfedges.Properties.Resize(samples.size());
        SetSequentialProperty(
            meshHalfedges.Properties,
            std::string{GS::PropertyNames::kHalfedgeToVertex},
            300u);
        SetSequentialProperty(
            meshHalfedges.Properties,
            std::string{GS::PropertyNames::kHalfedgeNext},
            400u);
        SetSequentialProperty(
            meshHalfedges.Properties,
            std::string{GS::PropertyNames::kHalfedgeFace},
            500u);
        SetVec3Property(
            meshHalfedges.Properties, "sample:position", samples);

        auto& meshFaces = scene.Raw().emplace<GS::Faces>(mesh);
        meshFaces.Properties.Resize(samples.size());
        SetSequentialProperty(
            meshFaces.Properties,
            std::string{GS::PropertyNames::kFaceHalfedge},
            600u);
        SetVec3Property(meshFaces.Properties, "f:center", samples);
        SetVec3Property(meshFaces.Properties, "sample:position", samples);
        auto opaque = meshFaces.Properties.GetOrAdd<OpaqueDomainValue>(
            "f:opaque", OpaqueDomainValue{});
        for (std::size_t index = 0u; index < samples.size(); ++index)
        {
            opaque[index].Id = 700u + static_cast<std::uint32_t>(index);
        }
        scene.Raw().emplace<GS::HasMeshTopology>(mesh);

        const ECS::EntityHandle graph = scene.Create();
        auto& graphVertices = scene.Raw().emplace<GS::Vertices>(graph);
        graphVertices.Properties.Resize(samples.size());
        SetVec3Property(
            graphVertices.Properties,
            std::string{GS::PropertyNames::kPosition},
            samples);
        SetVec3Property(graphVertices.Properties, "sample:position", samples);

        auto& graphEdges = scene.Raw().emplace<GS::Edges>(graph);
        graphEdges.Properties.Resize(samples.size());
        SetSequentialProperty(
            graphEdges.Properties,
            std::string{GS::PropertyNames::kEdgeV0},
            800u);
        SetSequentialProperty(
            graphEdges.Properties,
            std::string{GS::PropertyNames::kEdgeV1},
            900u);
        SetVec3Property(graphEdges.Properties, "sample:position", samples);

        auto& graphHalfedges = scene.Raw().emplace<GS::Halfedges>(graph);
        graphHalfedges.Properties.Resize(samples.size());
        SetSequentialProperty(
            graphHalfedges.Properties,
            std::string{GS::PropertyNames::kHalfedgeConnectivity},
            1'000u);
        SetVec3Property(
            graphHalfedges.Properties, "sample:position", samples);
        scene.Raw().emplace<GS::HasGraphTopology>(graph);

        const ECS::EntityHandle pointCloud = AddPointCloud(scene, samples);
        SetVec3Property(
            scene.Raw().get<GS::Vertices>(pointCloud).Properties,
            "sample:position",
            samples);

        return DomainSourceEntities{
            .Mesh = mesh,
            .Graph = graph,
            .PointCloud = pointCloud,
        };
    }

    [[nodiscard]] Geometry::PropertySet& ResolveTestPropertySet(
        ECS::Scene::Registry& scene,
        const ECS::EntityHandle entity,
        const Runtime::GeometryElementDomain domain)
    {
        switch (domain)
        {
        case Runtime::GeometryElementDomain::MeshVertex:
        case Runtime::GeometryElementDomain::GraphNode:
        case Runtime::GeometryElementDomain::PointCloudPoint:
            return scene.Raw().get<GS::Vertices>(entity).Properties;
        case Runtime::GeometryElementDomain::MeshEdge:
        case Runtime::GeometryElementDomain::GraphEdge:
            return scene.Raw().get<GS::Edges>(entity).Properties;
        case Runtime::GeometryElementDomain::MeshHalfedge:
        case Runtime::GeometryElementDomain::GraphHalfedge:
            return scene.Raw().get<GS::Halfedges>(entity).Properties;
        case Runtime::GeometryElementDomain::MeshFace:
            return scene.Raw().get<GS::Faces>(entity).Properties;
        case Runtime::GeometryElementDomain::Unknown:
            break;
        }
        std::terminate();
    }

    [[nodiscard]] ECS::EntityHandle ResolveTestEntity(
        const DomainSourceEntities& sources,
        const Runtime::GeometryElementDomain domain)
    {
        switch (domain)
        {
        case Runtime::GeometryElementDomain::MeshVertex:
        case Runtime::GeometryElementDomain::MeshEdge:
        case Runtime::GeometryElementDomain::MeshHalfedge:
        case Runtime::GeometryElementDomain::MeshFace:
            return sources.Mesh;
        case Runtime::GeometryElementDomain::GraphNode:
        case Runtime::GeometryElementDomain::GraphHalfedge:
        case Runtime::GeometryElementDomain::GraphEdge:
            return sources.Graph;
        case Runtime::GeometryElementDomain::PointCloudPoint:
            return sources.PointCloud;
        case Runtime::GeometryElementDomain::Unknown:
            break;
        }
        std::terminate();
    }

    [[nodiscard]] std::string InputPropertyForDomain(
        const Runtime::GeometryElementDomain domain)
    {
        switch (domain)
        {
        case Runtime::GeometryElementDomain::MeshVertex:
        case Runtime::GeometryElementDomain::GraphNode:
        case Runtime::GeometryElementDomain::PointCloudPoint:
            return std::string{GS::PropertyNames::kPosition};
        case Runtime::GeometryElementDomain::MeshFace:
            return "f:center";
        case Runtime::GeometryElementDomain::MeshEdge:
        case Runtime::GeometryElementDomain::MeshHalfedge:
        case Runtime::GeometryElementDomain::GraphHalfedge:
        case Runtime::GeometryElementDomain::GraphEdge:
            return "sample:position";
        case Runtime::GeometryElementDomain::Unknown:
            break;
        }
        std::terminate();
    }

    [[nodiscard]] std::string OutputPropertyForDomain(
        const Runtime::GeometryElementDomain domain)
    {
        return "lop:" + std::string{Runtime::ToString(domain)};
    }

    [[nodiscard]] Runtime::PointCloudConsolidationConfig
    SameCardinalityConfig()
    {
        return Runtime::PointCloudConsolidationConfig{
            .Strategy = Runtime::PointCloudConsolidationStrategy::Wlop,
            .SupportRadius = 0.65,
            .RepulsionWeight = 0.0,
            .MaxIterations = 1u,
            .ConvergenceTolerance = 1.0,
            .TargetPointCount = 0u,
            .Seed = 17u,
            .NormalRefinementRounds = 1u,
        };
    }

    [[nodiscard]] Runtime::PointCloudConsolidationRequest MakeDomainRequest(
        const ECS::EntityHandle entity,
        const Runtime::GeometryElementDomain domain,
        std::string inputName,
        std::string outputName)
    {
        Runtime::PointCloudConsolidationPropertyRefs properties =
            Runtime::MakePointCloudConsolidationPropertyRefs(
                domain, std::move(inputName), std::nullopt);
        properties.OutputPositions.Name = std::move(outputName);
        return Runtime::PointCloudConsolidationRequest{
            .StableEntityId =
                Runtime::SelectionController::ToStableEntityId(entity),
            .Properties = std::move(properties),
            .Config = SameCardinalityConfig(),
        };
    }

    [[nodiscard]] Runtime::PointCloudConsolidationRequest MakeRequest(
        const ECS::EntityHandle entity)
    {
        return Runtime::PointCloudConsolidationRequest{
            .StableEntityId =
                Runtime::SelectionController::ToStableEntityId(entity),
            .Config = Runtime::PointCloudConsolidationConfig{
                .Strategy =
                    Runtime::PointCloudConsolidationStrategy::Wlop,
                .SupportRadius = 0.65,
                .RepulsionWeight = 0.0,
                .MaxIterations = 1u,
                .ConvergenceTolerance = 1.0,
                .TargetPointCount = 16u,
                .Seed = 17u,
                .NormalRefinementRounds = 1u,
            },
        };
    }

    class ConsolidationSuccessApp final
        : public Intrinsic::Tests::RuntimeTestModule
    {
    public:
        explicit ConsolidationSuccessApp(
            const Runtime::PointCloudConsolidationBackend backend =
                Runtime::PointCloudConsolidationBackend::CpuReference,
            const double convergenceTolerance = 1.0)
            : Backend(backend)
            , ConvergenceTolerance(convergenceTolerance)
        {
        }

        void Resolve() override
        {
            auto& engine = Kernel();
            Service = engine.Services().Find<
                Runtime::PointCloudConsolidationService>();
            Scene = engine.Worlds().Get(engine.ActiveWorld());
            if (Service == nullptr || !Service->Available() || Scene == nullptr)
            {
                MissingService = true;
                engine.RequestExit();
                return;
            }
            Entity = AddPointCloud(*Scene, NoisyPlane());
            CompletionSubscription = Service->SubscribeCompleted(
                [this](const Runtime::PointCloudConsolidationResult& result)
                {
                    Completion = result;
                    CompletionThread = std::this_thread::get_id();
                });
            MainThread = std::this_thread::get_id();
            Runtime::PointCloudConsolidationRequest request =
                MakeRequest(Entity);
            request.Config.Backend = Backend;
            if (Backend == Runtime::PointCloudConsolidationBackend::CpuLBVH || Backend == Runtime::PointCloudConsolidationBackend::VulkanLBVH)
                request.Config.Strategy = Runtime::PointCloudConsolidationStrategy::Lop;
            request.Config.ConvergenceTolerance = ConvergenceTolerance;
            request.Config.SupportRadiusMode = Runtime::
                PointCloudConsolidationSupportRadiusMode::Manual;
            Correlation = Service->Run(std::move(request));
        }

        void Frame(double, double) override
        {
            auto& engine = Kernel();
            ++Ticks;
            if (Completion.has_value())
            {
                Stats = Service->Stats();
                engine.RequestExit();
            }
            else if (Ticks > 240u)
            {
                TimedOut = true;
                engine.RequestExit();
            }
        }

        void Shutdown() override
        {
            if (Service != nullptr)
                Service->Unsubscribe(CompletionSubscription);
        }

        Runtime::PointCloudConsolidationService* Service{};
        ECS::Scene::Registry* Scene{};
        ECS::EntityHandle Entity{ECS::InvalidEntityHandle};
        Runtime::KernelEventSubscription CompletionSubscription{};
        Runtime::CommandCorrelationId Correlation{};
        Runtime::PointCloudConsolidationModuleStats Stats{};
        std::optional<Runtime::PointCloudConsolidationResult> Completion{};
        std::thread::id MainThread{};
        std::thread::id CompletionThread{};
        std::uint32_t Ticks{0u};
        bool MissingService{false};
        bool TimedOut{false};
        Runtime::PointCloudConsolidationBackend Backend{
            Runtime::PointCloudConsolidationBackend::CpuReference};
        double ConvergenceTolerance{1.0};
    };

    class ConsolidationStaleSourceApp final
        : public Intrinsic::Tests::RuntimeTestModule
    {
    public:
        explicit ConsolidationStaleSourceApp(
            const Runtime::GeometryElementDomain domain,
            Runtime::PointCloudConsolidationBackend backend = Runtime::PointCloudConsolidationBackend::CpuReference)
            : Domain(domain), Backend(backend)
        {
        }

        Runtime::PointCloudConsolidationBackend Backend;
        [[nodiscard]] Extrinsic::Core::Result
        OnRegister(Runtime::EngineSetup& setup) override
        {
            // Commands capture the input before Simulation; completion drain
            // follows it, even when the main thread helps execute worker jobs.
            return setup.RegisterFrameHook(
                Runtime::FramePhase::Simulation,
                [this](Runtime::RuntimeFrameHookContext& context)
                { Frame(context.FixedStepAlpha, context.FrameDeltaSeconds); });
        }

        void Resolve() override
        {
            auto& engine = Kernel();
            Service = engine.Services().Find<
                Runtime::PointCloudConsolidationService>();
            Scene = engine.Worlds().Get(engine.ActiveWorld());
            if (Service == nullptr || !Service->Available() || Scene == nullptr)
            {
                MissingService = true;
                engine.RequestExit();
                return;
            }
            const DomainSourceEntities sources = AddDomainSources(*Scene);
            Entity = ResolveTestEntity(sources, Domain);
            InputProperty = InputPropertyForDomain(Domain);
            CompletionSubscription = Service->SubscribeCompleted(
                [this](const Runtime::PointCloudConsolidationResult& result)
                {
                    Completion = result;
                });

            auto request = MakeDomainRequest(Entity, Domain, InputProperty, "lop:stale_output");
            request.Config.Backend = Backend;
            if (Backend == Runtime::PointCloudConsolidationBackend::CpuLBVH)
                request.Config.Strategy = Runtime::PointCloudConsolidationStrategy::Lop;
            Correlation = Service->Run(std::move(request));
        }

        void Frame(double, double) override
        {
            auto& engine = Kernel();
            ++Ticks;
            if (!Mutated)
            {
                const std::vector<Runtime::JobSnapshot> jobs =
                    engine.Jobs().SnapshotAll();
                const auto found = std::find_if(
                    jobs.begin(),
                    jobs.end(),
                    [](const Runtime::JobSnapshot& job)
                    {
                        return job.DebugName ==
                            "Runtime.PointCloudConsolidation.CPU";
                    });
                if (found != jobs.end())
                {
                    auto position = ResolveTestPropertySet(
                        *Scene, Entity, Domain)
                        .Get<glm::vec3>(InputProperty);
                    position[0].z += 10.0f;
                    Mutated = true;
                }
            }

            if (Completion.has_value())
            {
                engine.RequestExit();
            }
            else if (Ticks > 240u)
            {
                TimedOut = true;
                engine.RequestExit();
            }
        }

        void Shutdown() override
        {
            if (Service != nullptr)
                Service->Unsubscribe(CompletionSubscription);
        }

        Runtime::PointCloudConsolidationService* Service{};
        ECS::Scene::Registry* Scene{};
        ECS::EntityHandle Entity{ECS::InvalidEntityHandle};
        Runtime::GeometryElementDomain Domain{
            Runtime::GeometryElementDomain::Unknown};
        std::string InputProperty{};
        Runtime::KernelEventSubscription CompletionSubscription{};
        Runtime::CommandCorrelationId Correlation{};
        std::optional<Runtime::PointCloudConsolidationResult> Completion{};
        std::uint32_t Ticks{0u};
        bool MissingService{false};
        bool Mutated{false};
        bool TimedOut{false};
    };

    class ConsolidationPropertyDomainsApp final
        : public Intrinsic::Tests::RuntimeTestModule
    {
    public:
        explicit ConsolidationPropertyDomainsApp(Runtime::PointCloudConsolidationBackend backend = Runtime::PointCloudConsolidationBackend::CpuReference) : Backend(backend) {}
        Runtime::PointCloudConsolidationBackend Backend;
        void Resolve() override
        {
            auto& engine = Kernel();
            Service = engine.Services().Find<
                Runtime::PointCloudConsolidationService>();
            Scene = engine.Worlds().Get(engine.ActiveWorld());
            if (Service == nullptr || !Service->Available() || Scene == nullptr)
            {
                MissingService = true;
                engine.RequestExit();
                return;
            }

            Sources = AddDomainSources(*Scene);
            MeshEdgeTopologyBefore =
                Scene->Raw()
                    .get<GS::Edges>(Sources.Mesh)
                    .Properties.Get<std::uint32_t>(
                        GS::PropertyNames::kEdgeV0)
                    .Vector();
            MeshHalfedgeTopologyBefore =
                Scene->Raw()
                    .get<GS::Halfedges>(Sources.Mesh)
                    .Properties.Get<std::uint32_t>(
                        GS::PropertyNames::kHalfedgeToVertex)
                    .Vector();
            MeshFaceTopologyBefore =
                Scene->Raw()
                    .get<GS::Faces>(Sources.Mesh)
                    .Properties.Get<std::uint32_t>(
                        GS::PropertyNames::kFaceHalfedge)
                    .Vector();
            MeshOpaqueBefore =
                Scene->Raw()
                    .get<GS::Faces>(Sources.Mesh)
                    .Properties.Get<OpaqueDomainValue>("f:opaque")
                    .Vector();
            GraphEdgeTopologyBefore =
                Scene->Raw()
                    .get<GS::Edges>(Sources.Graph)
                    .Properties.Get<std::uint32_t>(
                        GS::PropertyNames::kEdgeV0)
                    .Vector();
            GraphHalfedgeTopologyBefore =
                Scene->Raw()
                    .get<GS::Halfedges>(Sources.Graph)
                    .Properties.Get<std::uint32_t>(
                        GS::PropertyNames::kHalfedgeConnectivity)
                    .Vector();
            CompletionSubscription = Service->SubscribeCompleted(
                [this](const Runtime::PointCloudConsolidationResult& result)
                {
                    Completions.push_back(result);
                });

            for (const Runtime::GeometryElementDomain domain :
                 kAllElementDomains)
            {
                auto request = MakeDomainRequest(ResolveTestEntity(Sources, domain), domain,
                    InputPropertyForDomain(domain), OutputPropertyForDomain(domain));
                request.Config.Backend = Backend;
                if (Backend == Runtime::PointCloudConsolidationBackend::CpuLBVH)
                {
                    request.Config.Strategy = Runtime::PointCloudConsolidationStrategy::Lop;
                    auto* cache = engine.Services().Find<Runtime::SpatialIndexCache>();
                    if (cache) (void)cache->Acquire(engine.ActiveWorld(), ResolveTestEntity(Sources, domain), request.Properties.InputPositions);
                }
                Correlations.push_back(Service->Run(std::move(request)));
            }

            Runtime::PointCloudConsolidationRequest rejected =
                MakeDomainRequest(
                    Sources.Mesh,
                    Runtime::GeometryElementDomain::MeshFace,
                    "f:center",
                    "lop:rejected");
            rejected.Config.TargetPointCount = 16u;
            RejectedCorrelation = Service->Run(std::move(rejected));

            Runtime::PointCloudConsolidationRequest rejectedClop =
                MakeDomainRequest(
                    Sources.PointCloud,
                    Runtime::GeometryElementDomain::PointCloudPoint,
                    std::string{GS::PropertyNames::kPosition},
                    "lop:rejected_clop");
            rejectedClop.Config.Strategy =
                Runtime::PointCloudConsolidationStrategy::Clop;
            rejectedClop.Config.ClopMixtureComponentCount = 26u;
            RejectedClopCorrelation =
                Service->Run(std::move(rejectedClop));

            Runtime::PointCloudConsolidationRequest unsafe =
                MakeDomainRequest(
                    Sources.PointCloud,
                    Runtime::GeometryElementDomain::PointCloudPoint,
                    std::string{GS::PropertyNames::kPosition},
                    "lop:unsafe");
            unsafe.Config.MaxPredictedContributions = 1u;
            UnsafeCorrelation = Service->Run(std::move(unsafe));
        }

        void Frame(double, double) override
        {
            auto& engine = Kernel();
            ++Ticks;
            if (Completions.size() == 11u)
            {
                Stats = Service->Stats();
                engine.RequestExit();
            }
            else if (Ticks > 320u)
            {
                TimedOut = true;
                engine.RequestExit();
            }
        }

        void Shutdown() override
        {
            if (Service != nullptr)
                Service->Unsubscribe(CompletionSubscription);
        }

        Runtime::PointCloudConsolidationService* Service{};
        ECS::Scene::Registry* Scene{};
        DomainSourceEntities Sources{};
        Runtime::KernelEventSubscription CompletionSubscription{};
        std::vector<Runtime::CommandCorrelationId> Correlations{};
        Runtime::CommandCorrelationId RejectedCorrelation{};
        Runtime::CommandCorrelationId RejectedClopCorrelation{};
        Runtime::CommandCorrelationId UnsafeCorrelation{};
        std::vector<Runtime::PointCloudConsolidationResult> Completions{};
        Runtime::PointCloudConsolidationModuleStats Stats{};
        std::vector<std::uint32_t> MeshEdgeTopologyBefore{};
        std::vector<std::uint32_t> MeshHalfedgeTopologyBefore{};
        std::vector<std::uint32_t> MeshFaceTopologyBefore{};
        std::vector<OpaqueDomainValue> MeshOpaqueBefore{};
        std::vector<std::uint32_t> GraphEdgeTopologyBefore{};
        std::vector<std::uint32_t> GraphHalfedgeTopologyBefore{};
        std::uint32_t Ticks{0u};
        bool MissingService{false};
        bool TimedOut{false};
    };

    class ConsolidationDeterminismApp final
        : public Intrinsic::Tests::RuntimeTestModule
    {
    public:
        void Resolve() override
        {
            auto& engine = Kernel();
            Service = engine.Services().Find<
                Runtime::PointCloudConsolidationService>();
            Scene = engine.Worlds().Get(engine.ActiveWorld());
            if (Service == nullptr || !Service->Available() || Scene == nullptr)
            {
                MissingService = true;
                engine.RequestExit();
                return;
            }

            FirstEntity = AddPointCloud(*Scene, NoisyPlane());
            SecondEntity = AddPointCloud(*Scene, NoisyPlane());
            CompletionSubscription = Service->SubscribeCompleted(
                [this](const Runtime::PointCloudConsolidationResult& result)
                {
                    Completions.push_back(result);
                });
            FirstCorrelation = Service->Run(MakeRequest(FirstEntity));
            SecondCorrelation = Service->Run(MakeRequest(SecondEntity));
        }

        void Frame(double, double) override
        {
            auto& engine = Kernel();
            ++Ticks;
            if (Completions.size() == 2u)
                engine.RequestExit();
            else if (Ticks > 240u)
            {
                TimedOut = true;
                engine.RequestExit();
            }
        }

        void Shutdown() override
        {
            if (Service != nullptr)
                Service->Unsubscribe(CompletionSubscription);
        }

        Runtime::PointCloudConsolidationService* Service{};
        ECS::Scene::Registry* Scene{};
        ECS::EntityHandle FirstEntity{ECS::InvalidEntityHandle};
        ECS::EntityHandle SecondEntity{ECS::InvalidEntityHandle};
        Runtime::KernelEventSubscription CompletionSubscription{};
        Runtime::CommandCorrelationId FirstCorrelation{};
        Runtime::CommandCorrelationId SecondCorrelation{};
        std::vector<Runtime::PointCloudConsolidationResult> Completions{};
        std::uint32_t Ticks{0u};
        bool MissingService{false};
        bool TimedOut{false};
    };

    class ConsolidationWorkloadBoundaryApp final
        : public Intrinsic::Tests::RuntimeTestModule
    {
    public:
        void Resolve() override
        {
            auto& engine = Kernel();
            Service = engine.Services().Find<
                Runtime::PointCloudConsolidationService>();
            Scene = engine.Worlds().Get(engine.ActiveWorld());
            if (Service == nullptr || !Service->Available() || Scene == nullptr)
            {
                MissingService = true;
                engine.RequestExit();
                return;
            }

            Entity = AddPointCloud(*Scene, NoisyPlane());
            SparseEntity = AddPointCloud(*Scene, SparsePlane());
            AuthoredSparseEntity = AddPointCloud(*Scene, SparsePlane());
            auto& authoredSparseProperties = Scene->Raw()
                .get<GS::Vertices>(AuthoredSparseEntity).Properties;
            SetVec3Property(
                authoredSparseProperties,
                std::string{GS::PropertyNames::kNormal},
                std::vector<glm::vec3>(
                    authoredSparseProperties.Size(),
                    glm::vec3{0.0f, 0.0f, 1.0f}));
            CompletionSubscription = Service->SubscribeCompleted(
                [this](const Runtime::PointCloudConsolidationResult& result)
                {
                    Completions.push_back(result);
                });

            Runtime::PointCloudConsolidationRequest wlop =
                MakeRequest(Entity);
            wlop.Config.SupportRadiusMode = Runtime::
                PointCloudConsolidationSupportRadiusMode::Manual;
            wlop.Config.SupportRadius = 100.0;
            wlop.Config.MaxPredictedContributions = 2'224u;
            WlopCorrelation = Service->Run(std::move(wlop));

            Runtime::PointCloudConsolidationRequest clop =
                MakeRequest(Entity);
            clop.Config.Strategy =
                Runtime::PointCloudConsolidationStrategy::Clop;
            clop.Config.SupportRadiusMode = Runtime::
                PointCloudConsolidationSupportRadiusMode::Manual;
            clop.Config.SupportRadius = 100.0;
            clop.Config.ClopMixtureComponentCount = 3u;
            clop.Config.ClopMixtureMaxIterations = 1u;
            clop.Config.MaxPredictedContributions = 1'566u;
            ClopCorrelation = Service->Run(std::move(clop));

            Runtime::PointCloudConsolidationRequest ear =
                MakeRequest(Entity);
            ear.Config.Strategy =
                Runtime::PointCloudConsolidationStrategy::Ear;
            ear.Config.SupportRadiusMode = Runtime::
                PointCloudConsolidationSupportRadiusMode::Manual;
            ear.Config.SupportRadius = 100.0;
            ear.Config.TargetPointCount = 27u;
            ear.Config.MaxPredictedContributions = 31'232u;
            EarCorrelation = Service->Run(std::move(ear));

            Runtime::PointCloudConsolidationRequest anisotropic =
                MakeRequest(SparseEntity);
            anisotropic.Config.WlopAnisotropic = true;
            anisotropic.Config.SupportRadiusMode = Runtime::
                PointCloudConsolidationSupportRadiusMode::Manual;
            anisotropic.Config.SupportRadius = 0.001;
            anisotropic.Config.MaxPredictedContributions = 9'838u;
            AnisotropicCorrelation = Service->Run(std::move(anisotropic));

            Runtime::PointCloudConsolidationRequest anisotropicExact =
                MakeRequest(SparseEntity);
            anisotropicExact.Config.WlopAnisotropic = true;
            anisotropicExact.Config.SupportRadiusMode = Runtime::
                PointCloudConsolidationSupportRadiusMode::Manual;
            anisotropicExact.Config.SupportRadius = 0.001;
            anisotropicExact.Config.MaxPredictedContributions = 9'839u;
            AnisotropicExactCorrelation =
                Service->Run(std::move(anisotropicExact));

            Runtime::PointCloudConsolidationRequest authoredEar =
                MakeRequest(AuthoredSparseEntity);
            authoredEar.Config.Strategy =
                Runtime::PointCloudConsolidationStrategy::Ear;
            authoredEar.Config.SupportRadiusMode = Runtime::
                PointCloudConsolidationSupportRadiusMode::Manual;
            authoredEar.Config.SupportRadius = 0.001;
            authoredEar.Config.TargetPointCount = 27u;
            authoredEar.Config.MaxPredictedContributions = 18'532u;
            AuthoredEarCorrelation = Service->Run(std::move(authoredEar));
        }

        void Frame(double, double) override
        {
            auto& engine = Kernel();
            ++Ticks;
            if (Completions.size() == 6u)
                engine.RequestExit();
            else if (Ticks > 240u)
            {
                TimedOut = true;
                engine.RequestExit();
            }
        }

        void Shutdown() override
        {
            if (Service != nullptr)
                Service->Unsubscribe(CompletionSubscription);
        }

        Runtime::PointCloudConsolidationService* Service{};
        ECS::Scene::Registry* Scene{};
        ECS::EntityHandle Entity{ECS::InvalidEntityHandle};
        ECS::EntityHandle SparseEntity{ECS::InvalidEntityHandle};
        ECS::EntityHandle AuthoredSparseEntity{ECS::InvalidEntityHandle};
        Runtime::KernelEventSubscription CompletionSubscription{};
        Runtime::CommandCorrelationId WlopCorrelation{};
        Runtime::CommandCorrelationId ClopCorrelation{};
        Runtime::CommandCorrelationId EarCorrelation{};
        Runtime::CommandCorrelationId AnisotropicCorrelation{};
        Runtime::CommandCorrelationId AnisotropicExactCorrelation{};
        Runtime::CommandCorrelationId AuthoredEarCorrelation{};
        std::vector<Runtime::PointCloudConsolidationResult> Completions{};
        std::uint32_t Ticks{0u};
        bool MissingService{false};
        bool TimedOut{false};
    };
}

TEST(PointCloudConsolidationModule, VulkanLbvhVariantsShareDomainAndCapacityPreflight)
{
    ECS::Scene::Registry scene{};
    const auto sources = AddDomainSources(scene);
    for (const auto domain : kAllElementDomains)
    {
        SCOPED_TRACE(std::string{Runtime::ToString(domain)});
        const auto availability = Runtime::BuildGeometryAvailability(
            scene.Raw(), ResolveTestEntity(sources, domain));
        auto properties = Runtime::MakePointCloudConsolidationPropertyRefs(
            domain, "sample:position", std::nullopt);
        properties.OutputPositions.Name = "sample:projected";
        for (const auto strategy : {Runtime::PointCloudConsolidationStrategy::Wlop,
                 Runtime::PointCloudConsolidationStrategy::Clop,
                 Runtime::PointCloudConsolidationStrategy::Ear})
        {
            auto config = SameCardinalityConfig();
            config.Backend = Runtime::PointCloudConsolidationBackend::VulkanLBVH;
            config.Strategy = strategy;
            for (const bool anisotropic : {false, true})
            {
                config.WlopAnisotropic = anisotropic;
                const auto ready = Runtime::ResolvePointCloudConsolidationAvailability(
                    availability, properties, config);
                EXPECT_TRUE(ready.Available) << ready.Message;
            }
            config.GpuRadiusCapacity = 1025;
            EXPECT_FALSE(Runtime::ResolvePointCloudConsolidationAvailability(
                availability, properties, config).Available);
            config.GpuRadiusCapacity = 64;
            config.GpuQueryBatchSize = 0;
            EXPECT_FALSE(Runtime::ResolvePointCloudConsolidationAvailability(
                availability, properties, config).Available);
            config.GpuQueryBatchSize = 64;
            config.Backend = Runtime::PointCloudConsolidationBackend::CpuLBVH;
            EXPECT_FALSE(Runtime::ResolvePointCloudConsolidationAvailability(
                availability, properties, config).Available);
        }
    }
}

TEST(PointCloudConsolidationModule,
     AvailabilityIsTypedPropertyBasedAcrossEveryElementDomain)
{
    ECS::Scene::Registry scene{};
    const DomainSourceEntities sources = AddDomainSources(scene);
    const std::array<std::pair<Runtime::GeometryElementDomain,
                               ECS::EntityHandle>,
                     8u>
        cases{{
            {Runtime::GeometryElementDomain::MeshVertex, sources.Mesh},
            {Runtime::GeometryElementDomain::MeshEdge, sources.Mesh},
            {Runtime::GeometryElementDomain::MeshHalfedge, sources.Mesh},
            {Runtime::GeometryElementDomain::MeshFace, sources.Mesh},
            {Runtime::GeometryElementDomain::GraphNode, sources.Graph},
            {Runtime::GeometryElementDomain::GraphHalfedge, sources.Graph},
            {Runtime::GeometryElementDomain::GraphEdge, sources.Graph},
            {Runtime::GeometryElementDomain::PointCloudPoint,
             sources.PointCloud},
        }};

    std::optional<std::string> topologyRejection{};
    for (const auto& [domain, entity] : cases)
    {
        SCOPED_TRACE(std::string{Runtime::ToString(domain)});
        const Runtime::GeometryEntityAvailability availability =
            Runtime::BuildGeometryAvailability(scene.Raw(), entity);
        Runtime::PointCloudConsolidationPropertyRefs properties =
            Runtime::MakePointCloudConsolidationPropertyRefs(
                domain, "sample:position", std::nullopt);
        properties.OutputPositions.Name = "sample:lop";

        const Runtime::PointCloudConsolidationAvailability sameCount =
            Runtime::ResolvePointCloudConsolidationAvailability(
                availability, properties, SameCardinalityConfig());
        EXPECT_TRUE(sameCount.Available) << sameCount.Message;
        EXPECT_EQ(sameCount.InputPointCount, NoisyPlane().size());
        EXPECT_FALSE(sameCount.CardinalityChanging);

        Runtime::PointCloudConsolidationConfig countChanging =
            SameCardinalityConfig();
        countChanging.TargetPointCount = 16u;
        properties.OutputPositions.Name =
            std::string{GS::PropertyNames::kPosition};
        const Runtime::PointCloudConsolidationAvailability resized =
            Runtime::ResolvePointCloudConsolidationAvailability(
                availability, properties, countChanging);
        if (domain == Runtime::GeometryElementDomain::PointCloudPoint)
        {
            EXPECT_TRUE(resized.Available) << resized.Message;
        }
        else
        {
            EXPECT_FALSE(resized.Available);
            EXPECT_TRUE(resized.CardinalityChanging);
            if (!topologyRejection.has_value())
                topologyRejection = resized.Message;
            else
                EXPECT_EQ(resized.Message, *topologyRejection);
        }
    }

    Runtime::PointCloudConsolidationConfig oversizedClop =
        SameCardinalityConfig();
    oversizedClop.Strategy =
        Runtime::PointCloudConsolidationStrategy::Clop;
    oversizedClop.ClopMixtureComponentCount = 26u;
    Runtime::PointCloudConsolidationPropertyRefs pointProperties =
        Runtime::MakePointCloudConsolidationPropertyRefs(
            Runtime::GeometryElementDomain::PointCloudPoint,
            std::string{GS::PropertyNames::kPosition},
            std::nullopt);
    pointProperties.OutputPositions.Name = "lop:clop";
    const Runtime::PointCloudConsolidationAvailability clopAvailability =
        Runtime::ResolvePointCloudConsolidationAvailability(
            Runtime::BuildGeometryAvailability(
                scene.Raw(), sources.PointCloud),
            pointProperties,
            oversizedClop);
    EXPECT_FALSE(clopAvailability.Available);
    EXPECT_EQ(clopAvailability.InputPointCount, NoisyPlane().size());
    EXPECT_NE(clopAvailability.Message.find("mixture component count"),
              std::string::npos);

    Runtime::PointCloudConsolidationConfig vulkan =
        SameCardinalityConfig();
    vulkan.Backend =
        Runtime::PointCloudConsolidationBackend::VulkanCompute;
    const Runtime::PointCloudConsolidationAvailability vulkanWlop =
        Runtime::ResolvePointCloudConsolidationAvailability(
            Runtime::BuildGeometryAvailability(
                scene.Raw(), sources.PointCloud),
            pointProperties,
            vulkan);
    EXPECT_TRUE(vulkanWlop.Available) << vulkanWlop.Message;

    for (const Runtime::PointCloudConsolidationStrategy strategy : {
             Runtime::PointCloudConsolidationStrategy::Clop,
             Runtime::PointCloudConsolidationStrategy::Ear})
    {
        vulkan.Strategy = strategy;
        const Runtime::PointCloudConsolidationAvailability unsupported =
            Runtime::ResolvePointCloudConsolidationAvailability(
                Runtime::BuildGeometryAvailability(
                    scene.Raw(), sources.PointCloud),
                pointProperties,
                vulkan);
        EXPECT_FALSE(unsupported.Available);
        EXPECT_NE(unsupported.Message.find("ordinary LOP and isotropic WLOP"),
                  std::string::npos);
    }
    vulkan.Strategy = Runtime::PointCloudConsolidationStrategy::Wlop;
    vulkan.WlopAnisotropic = true;
    const Runtime::PointCloudConsolidationAvailability anisotropicVulkan =
        Runtime::ResolvePointCloudConsolidationAvailability(
            Runtime::BuildGeometryAvailability(
                scene.Raw(), sources.PointCloud),
            pointProperties,
            vulkan);
    EXPECT_FALSE(anisotropicVulkan.Available);
    EXPECT_NE(anisotropicVulkan.Message.find("ordinary LOP and isotropic WLOP"),
              std::string::npos);
}

TEST(PointCloudConsolidationModule,
     CommitsThroughGeometrySourcesAndOwnsUndoRedo)
{
    auto app = std::make_unique<ConsolidationSuccessApp>();
    ConsolidationSuccessApp* appPtr = app.get();
    Intrinsic::Tests::RuntimeTestKernel engine{
        HeadlessConfig(), std::move(app)};
    engine.EmplaceModule<Runtime::PointCloudConsolidationModule>();
    engine.EmplaceModule<Runtime::SceneDocumentModule>();
    engine.Initialize();
    engine.Run();

    EXPECT_FALSE(appPtr->MissingService);
    EXPECT_FALSE(appPtr->TimedOut);
    ASSERT_TRUE(appPtr->Correlation.IsValid());
    ASSERT_TRUE(appPtr->Completion.has_value());
    ASSERT_TRUE(appPtr->Completion->Succeeded())
        << appPtr->Completion->Message;
    EXPECT_EQ(appPtr->Completion->Correlation, appPtr->Correlation);
    EXPECT_EQ(appPtr->Completion->OutputPointCount, 16u);
    EXPECT_EQ(appPtr->Completion->ImplementationId, "cpu_reference");
    EXPECT_EQ(appPtr->Completion->StrategyToken, "wlop");
    EXPECT_EQ(appPtr->Completion->SupportRadiusSource, "manual");
    EXPECT_EQ(appPtr->Completion->SupportRadiusAnalysisStatus, "success");
    EXPECT_DOUBLE_EQ(appPtr->Completion->ResolvedSupportRadius, 0.65);
    EXPECT_GT(appPtr->Completion->SupportNeighborsP95, 0.0);
    EXPECT_GT(appPtr->Completion->PredictedContributionCount, 0u);
    EXPECT_EQ(appPtr->CompletionThread, appPtr->MainThread);
    EXPECT_EQ(appPtr->Stats.ResultsCommitted, 1u);

    ECS::Scene::Registry* scene =
        engine.Worlds().Get(engine.ActiveWorld());
    ASSERT_NE(scene, nullptr);
    const auto currentPositions =
        scene->Raw()
            .get<GS::Vertices>(appPtr->Entity)
            .Properties.Get<glm::vec3>(GS::PropertyNames::kPosition);
    ASSERT_TRUE(currentPositions);
    EXPECT_EQ(currentPositions.Vector().size(), 16u);
    EXPECT_FALSE(scene->Raw()
                     .get<GS::Vertices>(appPtr->Entity)
                     .Properties.Exists("p:source_index"));
    EXPECT_TRUE(scene->Raw().all_of<Dirty::GpuDirty>(appPtr->Entity));
    EXPECT_TRUE(scene->Raw().all_of<Dirty::DirtyVertexPositions>(
        appPtr->Entity));
    EXPECT_TRUE(scene->Raw().all_of<Dirty::DirtyVertexAttributes>(
        appPtr->Entity));
    EXPECT_TRUE(scene->Raw().all_of<Dirty::DirtyVertexNormals>(
        appPtr->Entity));

    Runtime::EditorCommandHistory* history =
        engine.Services().Find<Runtime::EditorCommandHistory>();
    ASSERT_NE(history, nullptr);
    ASSERT_EQ(history->UndoCount(), 1u);
    EXPECT_EQ(history->Undo().Status,
              Runtime::EditorCommandHistoryStatus::Undone);
    auto& restored = scene->Raw().get<GS::Vertices>(appPtr->Entity);
    ASSERT_EQ(restored.Properties.Size(), 25u);
    const auto restoredProvenance =
        restored.Properties.Get<std::uint32_t>("p:source_index");
    ASSERT_TRUE(restoredProvenance);
    EXPECT_EQ(restoredProvenance.Vector().front(), 0u);
    EXPECT_EQ(restoredProvenance.Vector().back(), 24u);

    EXPECT_EQ(history->Redo().Status,
              Runtime::EditorCommandHistoryStatus::Redone);
    EXPECT_EQ(scene->Raw()
                  .get<GS::Vertices>(appPtr->Entity)
                  .Properties.Size(),
              16u);

    engine.Shutdown();
}

TEST(PointCloudConsolidationModule,
     ExecutesEveryElementDomainPropertyWithoutTopologyConversion)
{
    auto app = std::make_unique<ConsolidationPropertyDomainsApp>();
    ConsolidationPropertyDomainsApp* appPtr = app.get();
    Intrinsic::Tests::RuntimeTestKernel engine{
        HeadlessConfig(), std::move(app)};
    engine.EmplaceModule<Runtime::PointCloudConsolidationModule>();
    engine.EmplaceModule<Runtime::SceneDocumentModule>();
    engine.Initialize();
    engine.Run();

    EXPECT_FALSE(appPtr->MissingService);
    EXPECT_FALSE(appPtr->TimedOut);
    ASSERT_EQ(appPtr->Correlations.size(), 8u);
    EXPECT_TRUE(std::all_of(
        appPtr->Correlations.begin(),
        appPtr->Correlations.end(),
        [](const Runtime::CommandCorrelationId correlation)
        {
            return correlation.IsValid();
        }));
    ASSERT_TRUE(appPtr->RejectedCorrelation.IsValid());
    ASSERT_TRUE(appPtr->RejectedClopCorrelation.IsValid());
    ASSERT_TRUE(appPtr->UnsafeCorrelation.IsValid());
    ASSERT_EQ(appPtr->Completions.size(), 11u);
    EXPECT_EQ(appPtr->Stats.CommandsHandled, 11u);
    EXPECT_EQ(appPtr->Stats.JobsSubmitted, 9u);
    EXPECT_EQ(appPtr->Stats.ResultsCommitted, 8u);

    const auto rejected = std::find_if(
        appPtr->Completions.begin(),
        appPtr->Completions.end(),
        [appPtr](const Runtime::PointCloudConsolidationResult& result)
        {
            return result.Correlation == appPtr->RejectedCorrelation;
        });
    ASSERT_NE(rejected, appPtr->Completions.end());
    EXPECT_EQ(rejected->Status,
              Runtime::PointCloudConsolidationRunStatus::
                  UnsupportedPropertySource);
    EXPECT_NE(rejected->Message.find("topology-bearing element domain"),
              std::string::npos);
    const auto rejectedClop = std::find_if(
        appPtr->Completions.begin(),
        appPtr->Completions.end(),
        [appPtr](const Runtime::PointCloudConsolidationResult& result)
        {
            return result.Correlation == appPtr->RejectedClopCorrelation;
        });
    ASSERT_NE(rejectedClop, appPtr->Completions.end());
    EXPECT_EQ(rejectedClop->Status,
              Runtime::PointCloudConsolidationRunStatus::
                  UnsupportedPropertySource);
    EXPECT_NE(rejectedClop->Message.find("mixture component count"),
              std::string::npos);
    const auto unsafe = std::find_if(
        appPtr->Completions.begin(),
        appPtr->Completions.end(),
        [appPtr](const Runtime::PointCloudConsolidationResult& result)
        {
            return result.Correlation == appPtr->UnsafeCorrelation;
        });
    ASSERT_NE(unsafe, appPtr->Completions.end());
    EXPECT_EQ(unsafe->Status,
              Runtime::PointCloudConsolidationRunStatus::
                  UnsafeSupportRadius);
    EXPECT_EQ(unsafe->SupportRadiusSource, "recommended");
    EXPECT_EQ(unsafe->SupportRadiusAnalysisStatus,
              "workload_limit_exceeded");
    EXPECT_GT(unsafe->PredictedContributionCount, 1u);
    for (const Runtime::PointCloudConsolidationResult& result :
         appPtr->Completions)
    {
        if (result.Correlation != appPtr->RejectedCorrelation &&
            result.Correlation != appPtr->RejectedClopCorrelation &&
            result.Correlation != appPtr->UnsafeCorrelation)
        {
            EXPECT_TRUE(result.Succeeded()) << result.Message;
            EXPECT_EQ(result.SupportRadiusSource, "recommended");
            EXPECT_EQ(result.SupportRadiusAnalysisStatus, "success");
            EXPECT_GT(result.ResolvedSupportRadius, 0.0);
            EXPECT_EQ(result.InputPointCount, NoisyPlane().size());
            EXPECT_EQ(result.OutputPointCount, NoisyPlane().size());
        }
    }

    ECS::Scene::Registry& scene = *appPtr->Scene;
    const auto& meshEdges =
        scene.Raw().get<GS::Edges>(appPtr->Sources.Mesh).Properties;
    const auto& meshHalfedges =
        scene.Raw().get<GS::Halfedges>(appPtr->Sources.Mesh).Properties;
    const auto& meshFaces =
        scene.Raw().get<GS::Faces>(appPtr->Sources.Mesh).Properties;
    const auto& graphEdges =
        scene.Raw().get<GS::Edges>(appPtr->Sources.Graph).Properties;
    const auto& graphHalfedges =
        scene.Raw().get<GS::Halfedges>(appPtr->Sources.Graph).Properties;
    const auto& pointVertices =
        scene.Raw().get<GS::Vertices>(appPtr->Sources.PointCloud).Properties;

    for (const Runtime::GeometryElementDomain domain : kAllElementDomains)
    {
        EXPECT_TRUE(ResolveTestPropertySet(
                        scene, ResolveTestEntity(appPtr->Sources, domain),
                        domain)
                        .Get<glm::vec3>(OutputPropertyForDomain(domain)));
    }
    EXPECT_FALSE(meshFaces.Exists("lop:rejected"));
    EXPECT_FALSE(pointVertices.Exists("lop:rejected_clop"));
    EXPECT_FALSE(pointVertices.Exists("lop:unsafe"));
    EXPECT_EQ(meshEdges.Get<std::uint32_t>(GS::PropertyNames::kEdgeV0).Vector(),
              appPtr->MeshEdgeTopologyBefore);
    EXPECT_EQ(meshHalfedges
                  .Get<std::uint32_t>(GS::PropertyNames::kHalfedgeToVertex)
                  .Vector(),
              appPtr->MeshHalfedgeTopologyBefore);
    EXPECT_EQ(meshFaces
                  .Get<std::uint32_t>(GS::PropertyNames::kFaceHalfedge)
                  .Vector(),
              appPtr->MeshFaceTopologyBefore);
    EXPECT_EQ(meshFaces.Get<OpaqueDomainValue>("f:opaque").Vector(),
              appPtr->MeshOpaqueBefore);
    EXPECT_EQ(graphEdges
                  .Get<std::uint32_t>(GS::PropertyNames::kEdgeV0)
                  .Vector(),
              appPtr->GraphEdgeTopologyBefore);
    EXPECT_EQ(graphHalfedges
                  .Get<std::uint32_t>(
                      GS::PropertyNames::kHalfedgeConnectivity)
                  .Vector(),
              appPtr->GraphHalfedgeTopologyBefore);
    EXPECT_TRUE(scene.Raw().all_of<GS::HasMeshTopology>(appPtr->Sources.Mesh));
    EXPECT_TRUE(
        scene.Raw().all_of<GS::HasGraphTopology>(appPtr->Sources.Graph));

    Runtime::EditorCommandHistory* history =
        engine.Services().Find<Runtime::EditorCommandHistory>();
    ASSERT_NE(history, nullptr);
    ASSERT_EQ(history->UndoCount(), 8u);
    for (std::size_t index = 0u; index < 8u; ++index)
    {
        EXPECT_EQ(history->Undo().Status,
                  Runtime::EditorCommandHistoryStatus::Undone);
    }
    for (const Runtime::GeometryElementDomain domain : kAllElementDomains)
    {
        EXPECT_FALSE(ResolveTestPropertySet(
                         scene, ResolveTestEntity(appPtr->Sources, domain),
                         domain)
                         .Exists(OutputPropertyForDomain(domain)));
    }
    EXPECT_EQ(meshFaces.Get<OpaqueDomainValue>("f:opaque").Vector(),
              appPtr->MeshOpaqueBefore);

    for (std::size_t index = 0u; index < 8u; ++index)
    {
        EXPECT_EQ(history->Redo().Status,
                  Runtime::EditorCommandHistoryStatus::Redone);
    }
    for (const Runtime::GeometryElementDomain domain : kAllElementDomains)
    {
        EXPECT_TRUE(ResolveTestPropertySet(
                        scene, ResolveTestEntity(appPtr->Sources, domain),
                        domain)
                        .Exists(OutputPropertyForDomain(domain)));
    }
    EXPECT_EQ(meshFaces
                  .Get<std::uint32_t>(GS::PropertyNames::kFaceHalfedge)
                  .Vector(),
              appPtr->MeshFaceTopologyBefore);
    EXPECT_EQ(meshFaces.Get<OpaqueDomainValue>("f:opaque").Vector(),
              appPtr->MeshOpaqueBefore);

    engine.Shutdown();
}

TEST(PointCloudConsolidationModule,
     VulkanRequestFallsBackHonestlyWhenNoOperationalGpuStateExists)
{
    auto app = std::make_unique<ConsolidationSuccessApp>(
        Runtime::PointCloudConsolidationBackend::VulkanCompute);
    ConsolidationSuccessApp* appPtr = app.get();
    Intrinsic::Tests::RuntimeTestKernel engine{
        HeadlessConfig(), std::move(app)};
    engine.EmplaceModule<Runtime::PointCloudConsolidationModule>();
    engine.Initialize();
    engine.Run();

    EXPECT_FALSE(appPtr->MissingService);
    EXPECT_FALSE(appPtr->TimedOut);
    ASSERT_TRUE(appPtr->Completion.has_value());
    ASSERT_TRUE(appPtr->Completion->Succeeded())
        << appPtr->Completion->Message;
    EXPECT_EQ(appPtr->Completion->RequestedBackend,
              Runtime::PointCloudConsolidationBackend::VulkanCompute);
    EXPECT_EQ(appPtr->Completion->ActualBackend,
              Runtime::PointCloudConsolidationBackend::CpuReference);
    EXPECT_TRUE(appPtr->Completion->FellBackToCpu);
    EXPECT_EQ(appPtr->Completion->ImplementationId, "cpu_reference");
    EXPECT_FALSE(appPtr->Completion->BackendDiagnostic.empty());
    EXPECT_EQ(appPtr->Stats.GpuRequestsAccepted, 0u);
    EXPECT_EQ(appPtr->Stats.GpuFallbacks, 1u);
    EXPECT_EQ(appPtr->Stats.GpuCompletions, 0u);
    EXPECT_EQ(appPtr->Stats.JobsSubmitted, 2u);
    EXPECT_EQ(appPtr->Stats.ResultsCommitted, 1u);

    engine.Shutdown();
}

TEST(PointCloudConsolidationModule,
     NonConvergedFiniteIteratePublishesAsAnExplicitPreview)
{
    auto app = std::make_unique<ConsolidationSuccessApp>(
        Runtime::PointCloudConsolidationBackend::CpuReference,
        0.0);
    ConsolidationSuccessApp* appPtr = app.get();
    Intrinsic::Tests::RuntimeTestKernel engine{
        HeadlessConfig(), std::move(app)};
    engine.EmplaceModule<Runtime::PointCloudConsolidationModule>();
    engine.Initialize();
    engine.Run();

    EXPECT_FALSE(appPtr->TimedOut);
    ASSERT_TRUE(appPtr->Completion.has_value());
    EXPECT_TRUE(appPtr->Completion->Succeeded())
        << appPtr->Completion->Message;
    EXPECT_EQ(appPtr->Completion->GeometryStatus,
              Geometry::PointCloud::Consolidation::Status::NotConverged);
    EXPECT_FALSE(appPtr->Completion->Converged);
    EXPECT_EQ(appPtr->Completion->Iterations, 1u);
    EXPECT_EQ(appPtr->Completion->OutputPointCount, 16u);

    engine.Shutdown();
}

TEST(PointCloudConsolidationModule,
     WorkloadGuardCoversReferencePassesAtConfiguredBoundaries)
{
    auto app = std::make_unique<ConsolidationWorkloadBoundaryApp>();
    ConsolidationWorkloadBoundaryApp* appPtr = app.get();
    Intrinsic::Tests::RuntimeTestKernel engine{
        HeadlessConfig(), std::move(app)};
    engine.EmplaceModule<Runtime::PointCloudConsolidationModule>();
    engine.EmplaceModule<Runtime::SceneDocumentModule>();
    engine.Initialize();
    engine.Run();

    EXPECT_FALSE(appPtr->MissingService);
    EXPECT_FALSE(appPtr->TimedOut);
    ASSERT_EQ(appPtr->Completions.size(), 6u);

    const auto expectBoundary = [appPtr](
        const Runtime::CommandCorrelationId correlation,
        const std::uint64_t expectedQueries,
        const std::uint64_t expectedContributions,
        const double expectedP95)
    {
        const auto found = std::find_if(
            appPtr->Completions.begin(),
            appPtr->Completions.end(),
            [correlation](
                const Runtime::PointCloudConsolidationResult& result)
            {
                return result.Correlation == correlation;
            });
        ASSERT_NE(found, appPtr->Completions.end());
        EXPECT_EQ(
            found->Status,
            Runtime::PointCloudConsolidationRunStatus::UnsafeSupportRadius);
        EXPECT_EQ(
            found->SupportRadiusAnalysisStatus,
            "workload_limit_exceeded");
        EXPECT_EQ(found->SupportNeighborsP95, expectedP95);
        EXPECT_EQ(found->PredictedSupportQueryCount, expectedQueries);
        EXPECT_EQ(
            found->PredictedContributionCount, expectedContributions);
        EXPECT_EQ(
            found->Config.MaxPredictedContributions,
            expectedContributions - 1u);
    };

    // Isotropic WLOP: source density + L2 initialization + projected
    // density/attraction/repulsion for one iteration.
    expectBoundary(appPtr->WlopCorrelation, 89u, 2'225u, 25.0);
    // CLOP: one projected repulsion pass plus the bounded K-means, EM,
    // continuous initialization, and three-term attraction work.
    expectBoundary(appPtr->ClopCorrelation, 16u, 1'567u, 25.0);
    // EAR: source density + four support-query passes, estimated-normal
    // preparation, and two conservative all-pairs/all-points insertion
    // envelopes.
    expectBoundary(appPtr->EarCorrelation, 150u, 31'233u, 25.0);
    // Sparse support occupancy must not hide estimated-normal KNN/MST and
    // local-orientation work for anisotropic WLOP.
    expectBoundary(appPtr->AnisotropicCorrelation, 114u, 9'839u, 1.0);
    // Authored normals avoid KNN/MST but still pay normalization and the
    // worst-case input-squared local-orientation scan.
    expectBoundary(
        appPtr->AuthoredEarCorrelation, 125u, 18'533u, 1.0);

    const auto exactLimit = std::find_if(
        appPtr->Completions.begin(),
        appPtr->Completions.end(),
        [appPtr](const Runtime::PointCloudConsolidationResult& result)
        {
            return result.Correlation ==
                appPtr->AnisotropicExactCorrelation;
        });
    ASSERT_NE(exactLimit, appPtr->Completions.end());
    EXPECT_NE(
        exactLimit->Status,
        Runtime::PointCloudConsolidationRunStatus::UnsafeSupportRadius);
    EXPECT_EQ(exactLimit->SupportNeighborsP95, 1.0);
    EXPECT_EQ(exactLimit->PredictedSupportQueryCount, 114u);
    EXPECT_EQ(exactLimit->PredictedContributionCount, 9'839u);

    const auto& vertices =
        appPtr->Scene->Raw().get<GS::Vertices>(appPtr->Entity).Properties;
    EXPECT_EQ(vertices.Size(), NoisyPlane().size());
    engine.Shutdown();
}

TEST(PointCloudConsolidationModule, SourceMutationDropsQueuedWriteback)
{
    for (const auto backend : {Runtime::PointCloudConsolidationBackend::CpuReference,
         Runtime::PointCloudConsolidationBackend::CpuLBVH})
    for (const Runtime::GeometryElementDomain domain : kAllElementDomains)
    {
        SCOPED_TRACE(std::string{Runtime::ToString(domain)});
        auto app = std::make_unique<ConsolidationStaleSourceApp>(domain, backend);
        ConsolidationStaleSourceApp* appPtr = app.get();
        Intrinsic::Tests::RuntimeTestKernel engine{
            HeadlessConfig(1u), std::move(app)};
        engine.EmplaceModule<Runtime::SpatialIndexCache>();
        engine.EmplaceModule<Runtime::PointCloudConsolidationModule>();
        engine.Initialize();
        engine.Run();

        EXPECT_FALSE(appPtr->MissingService);
        EXPECT_FALSE(appPtr->TimedOut);
        EXPECT_TRUE(appPtr->Mutated);
        ASSERT_TRUE(appPtr->Completion.has_value());
        EXPECT_EQ(appPtr->Completion->Correlation, appPtr->Correlation);
        EXPECT_EQ(appPtr->Completion->Status,
                  Runtime::PointCloudConsolidationRunStatus::StaleSource);
        const Geometry::PropertySet& properties = ResolveTestPropertySet(
            *appPtr->Scene, appPtr->Entity, domain);
        EXPECT_EQ(properties.Size(), NoisyPlane().size());
        EXPECT_FALSE(properties.Exists("lop:stale_output"));

        engine.Shutdown();
    }
}

TEST(PointCloudConsolidationModule,
     IdenticalInputAndConfigProduceIdenticalRuntimeOutput)
{
    auto app = std::make_unique<ConsolidationDeterminismApp>();
    ConsolidationDeterminismApp* appPtr = app.get();
    Intrinsic::Tests::RuntimeTestKernel engine{
        HeadlessConfig(), std::move(app)};
    engine.EmplaceModule<Runtime::PointCloudConsolidationModule>();
    engine.Initialize();
    engine.Run();

    EXPECT_FALSE(appPtr->MissingService);
    EXPECT_FALSE(appPtr->TimedOut);
    ASSERT_TRUE(appPtr->FirstCorrelation.IsValid());
    ASSERT_TRUE(appPtr->SecondCorrelation.IsValid());
    ASSERT_EQ(appPtr->Completions.size(), 2u);
    EXPECT_TRUE(appPtr->Completions[0].Succeeded());
    EXPECT_TRUE(appPtr->Completions[1].Succeeded());

    const auto first = appPtr->Scene->Raw()
                           .get<GS::Vertices>(appPtr->FirstEntity)
                           .Properties.Get<glm::vec3>(
                               GS::PropertyNames::kPosition);
    const auto second = appPtr->Scene->Raw()
                            .get<GS::Vertices>(appPtr->SecondEntity)
                            .Properties.Get<glm::vec3>(
                                GS::PropertyNames::kPosition);
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    EXPECT_EQ(first.Vector(), second.Vector());

    engine.Shutdown();
}

TEST(PointCloudConsolidationModule, CachedLopBackendPublishesAndOwnsUndo)
{
    auto app = std::make_unique<ConsolidationSuccessApp>(Runtime::PointCloudConsolidationBackend::CpuLBVH);
    auto* observed = app.get();
    Intrinsic::Tests::RuntimeTestKernel engine{HeadlessConfig(),std::move(app)};
    engine.EmplaceModule<Runtime::SpatialIndexCache>();
    engine.EmplaceModule<Runtime::PointCloudConsolidationModule>();
    engine.EmplaceModule<Runtime::SceneDocumentModule>();
    engine.Initialize(); engine.Run();
    ASSERT_TRUE(observed->Completion.has_value());
    ASSERT_TRUE(observed->Completion->Succeeded()) << observed->Completion->Message;
    EXPECT_EQ(observed->Completion->ActualBackend,Runtime::PointCloudConsolidationBackend::CpuLBVH);
    EXPECT_EQ(observed->Completion->ImplementationId,"cpu_lbvh");
    EXPECT_FALSE(observed->Completion->FellBackToCpu);
    auto* history=engine.Services().Find<Runtime::EditorCommandHistory>(); ASSERT_NE(history,nullptr);
    EXPECT_EQ(history->UndoCount(),1u); EXPECT_EQ(history->Undo().Status,Runtime::EditorCommandHistoryStatus::Undone);
    EXPECT_EQ(observed->Scene->Raw().get<GS::Vertices>(observed->Entity).Properties.Size(),25u);
    EXPECT_EQ(history->Redo().Status,Runtime::EditorCommandHistoryStatus::Redone);
    EXPECT_EQ(observed->Scene->Raw().get<GS::Vertices>(observed->Entity).Properties.Size(),16u);
    engine.Shutdown();
}

TEST(PointCloudConsolidationModule, CachedLopReusesAllEightPropertyDomainIndices)
{
    auto app=std::make_unique<ConsolidationPropertyDomainsApp>(Runtime::PointCloudConsolidationBackend::CpuLBVH);
    auto* observed=app.get(); Intrinsic::Tests::RuntimeTestKernel engine{HeadlessConfig(),std::move(app)};
    engine.EmplaceModule<Runtime::SpatialIndexCache>(); engine.EmplaceModule<Runtime::PointCloudConsolidationModule>();
    engine.Initialize(); engine.Run();
    EXPECT_FALSE(observed->TimedOut);
    std::size_t accepted=0;
    for(const auto& result:observed->Completions) if(result.RequestedBackend==Runtime::PointCloudConsolidationBackend::CpuLBVH)
    {
        ASSERT_TRUE(result.Succeeded()) << result.Message;
        EXPECT_TRUE(result.ReusedSpatialIndex); EXPECT_EQ(result.ActualBackend,Runtime::PointCloudConsolidationBackend::CpuLBVH);
        EXPECT_FALSE(result.FellBackToCpu); ++accepted;
    }
    EXPECT_EQ(accepted,8u); engine.Shutdown();
}

TEST(PointCloudConsolidationModule, VulkanLbvhRejectsUnavailableDeviceWithoutFallback)
{
    auto app=std::make_unique<ConsolidationSuccessApp>(Runtime::PointCloudConsolidationBackend::VulkanLBVH);
    auto* observed=app.get();Intrinsic::Tests::RuntimeTestKernel engine{HeadlessConfig(),std::move(app)};
    engine.EmplaceModule<Runtime::SpatialIndexCache>();engine.EmplaceModule<Runtime::PointCloudConsolidationModule>();
    engine.Initialize();engine.Run();ASSERT_TRUE(observed->Completion.has_value());
    EXPECT_FALSE(observed->Completion->Succeeded());EXPECT_FALSE(observed->Completion->FellBackToCpu);
    EXPECT_EQ(observed->Completion->ActualBackend,Runtime::PointCloudConsolidationBackend::None);
    EXPECT_EQ(observed->Scene->Raw().get<GS::Vertices>(observed->Entity).Properties.Size(),25u);
    EXPECT_EQ(observed->Stats.JobsSubmitted,0u);engine.Shutdown();
}

namespace
{
    class ConsolidationReadiness : public testing::Test
    {
    protected:
        void SetUp() override
        {
            Engine.EmplaceModule<Runtime::PointCloudConsolidationModule>();
            Engine.Initialize();
            Scene = Engine.Worlds().Get(Engine.ActiveWorld());
            Service = Engine.Services().Find<Runtime::PointCloudConsolidationService>();
            ASSERT_NE(Scene, nullptr);
            ASSERT_NE(Service, nullptr);
            Entity = AddPointCloud(*Scene, NoisyPlane());
            Request = MakeDomainRequest(Entity, Runtime::GeometryElementDomain::PointCloudPoint,
                "v:position", "projected");
        }
        void TearDown() override { Engine.Shutdown(); }
        Runtime::PointCloudConsolidationAvailability Prepare()
        {
            return Service->PrepareAvailability(Engine.ActiveWorld(), Request);
        }
        void Drain()
        {
            Engine.Commands().Drain(*Scene,
                {&Engine.Events(), &Engine.Jobs(), &Engine.Worlds()});
        }
        Geometry::PropertySet& Properties()
        {
            return Scene->Raw().get<GS::Vertices>(Entity).Properties;
        }
        void ExpectSameAsDirect(const Runtime::PointCloudConsolidationAvailability& cached)
        {
            const auto direct = Runtime::ResolvePointCloudConsolidationAvailability(
                Runtime::BuildGeometryAvailability(Scene->Raw(), Entity),
                Request.Properties, Request.Config);
            EXPECT_FALSE(cached.Pending);
            EXPECT_EQ(cached.Available, direct.Available);
            EXPECT_EQ(cached.Error, direct.Error);
            EXPECT_EQ(cached.Message, direct.Message);
            EXPECT_EQ(cached.InputPointCount, direct.InputPointCount);
            EXPECT_EQ(cached.CardinalityChanging, direct.CardinalityChanging);
        }
        Intrinsic::Tests::RuntimeTestKernel Engine{HeadlessConfig()};
        ECS::Scene::Registry* Scene{};
        Runtime::PointCloudConsolidationService* Service{};
        ECS::EntityHandle Entity{};
        Runtime::PointCloudConsolidationRequest Request{};
    };
}

TEST_F(ConsolidationReadiness, RepeatedPreparationOnlyReadsMetadataAndCachedVerdicts)
{
    for (int i = 0; i < 20; ++i)
    {
        const auto pending = Prepare();
        EXPECT_TRUE(pending.Pending);
        EXPECT_FALSE(pending.Available);
        EXPECT_FALSE(pending.Message.empty());
    }
    EXPECT_EQ(Service->Stats().ReadinessChecksQueued, 1u);
    EXPECT_EQ(Service->Stats().ReadinessPropertyScans, 0u);
    Drain();
    for (int i = 0; i < 20; ++i)
        EXPECT_TRUE(Prepare().Available);
    EXPECT_EQ(Service->Stats().ReadinessChecksQueued, 1u);
    EXPECT_EQ(Service->Stats().ReadinessPropertyScans, 1u);
    Properties().Get<std::uint32_t>("p:source_index")[0] = 99u;
    Request.Config.RepulsionWeight = 0.1;
    ExpectSameAsDirect(Prepare());
    EXPECT_EQ(Service->Stats().ReadinessChecksQueued, 1u);
    EXPECT_EQ(Service->Stats().ReadinessPropertyScans, 1u);
    (void)Properties().Add<float>("projected", 0.0f);
    EXPECT_FALSE(Prepare().Available);
    ExpectSameAsDirect(Prepare());
    EXPECT_EQ(Service->Stats().ReadinessChecksQueued, 1u);
}

TEST_F(ConsolidationReadiness, RepeatedEditsAndRetainedBorrowsInvalidateBeforeAndAfterDrain)
{
    auto positions = Properties().Get<glm::vec3>("v:position");
    positions[0].x = 1.0f;
    ASSERT_TRUE(Prepare().Pending);
    positions[0].x = std::numeric_limits<float>::infinity();
    Drain();
    EXPECT_EQ(Service->Stats().ReadinessPropertyScans, 0u);
    ASSERT_TRUE(Prepare().Pending);
    Drain();
    EXPECT_FALSE(Prepare().Available);
    ExpectSameAsDirect(Prepare());
    positions[0].x = 0.0f;
    ASSERT_TRUE(Prepare().Pending);
    Drain();
    ASSERT_TRUE(Prepare().Available);
    auto& retained = positions.Vector();
    ASSERT_TRUE(Prepare().Pending);
    Drain();
    ASSERT_TRUE(Prepare().Available);
    retained[0].x = std::numeric_limits<float>::quiet_NaN();
    positions.MarkModified();
    ASSERT_TRUE(Prepare().Pending);
    Drain();
    ExpectSameAsDirect(Prepare());
    EXPECT_FALSE(Prepare().Available);
}

TEST_F(ConsolidationReadiness, NormalChangesDoNotRescanUnchangedPositions)
{
    auto normals = Properties().Add<glm::vec3>("normal", glm::vec3(0, 0, 1));
    Request.Properties.InputNormals = Runtime::GeometryPropertyRef{
        Runtime::GeometryElementDomain::PointCloudPoint, "normal", Geometry::PropertyValueKind::Vec3};
    ASSERT_TRUE(Prepare().Pending);
    EXPECT_EQ(Service->Stats().ReadinessChecksQueued, 2u);
    Drain();
    ASSERT_TRUE(Prepare().Available);
    EXPECT_EQ(Service->Stats().ReadinessPropertyScans, 2u);
    normals[0].z = std::numeric_limits<float>::infinity();
    ASSERT_TRUE(Prepare().Pending);
    Drain();
    ExpectSameAsDirect(Prepare());
    EXPECT_FALSE(Prepare().Available);
    EXPECT_EQ(Service->Stats().ReadinessPropertyScans, 3u);
    Properties().Remove(normals);
    EXPECT_TRUE(Prepare().Available);
    Request.Config.Strategy = Runtime::PointCloudConsolidationStrategy::Ear;
    Request.Config.NormalSource = Runtime::PointCloudConsolidationNormalSource::RequireAuthored;
    EXPECT_FALSE(Prepare().Available);
    ExpectSameAsDirect(Prepare());
    (void)Properties().Add<glm::vec3>("normal", glm::vec3(0, 0, 1));
    ASSERT_TRUE(Prepare().Pending);
    Drain();
    ExpectSameAsDirect(Prepare());
    EXPECT_TRUE(Prepare().Available);
    EXPECT_EQ(Service->Stats().ReadinessPropertyScans, 4u);
}

TEST_F(ConsolidationReadiness, WorldEpochEntityAndPropertyReplacementDiscardOldChecks)
{
    ASSERT_TRUE(Prepare().Pending);
    Engine.Jobs().AdvanceWorldGeneration(Engine.ActiveWorld());
    Drain();
    EXPECT_EQ(Service->Stats().ReadinessPropertyScans, 0u);
    ASSERT_TRUE(Prepare().Pending);
    Drain();
    ASSERT_TRUE(Prepare().Available);
    auto positions = Properties().Get<glm::vec3>("v:position");
    Properties().Remove(positions);
    EXPECT_FALSE(Prepare().Available);
    SetVec3Property(Properties(), "v:position", NoisyPlane());
    ASSERT_TRUE(Prepare().Pending);
    Scene->Raw().destroy(Entity);
    Drain();
    EXPECT_EQ(Service->Stats().ReadinessPropertyScans, 1u);
    EXPECT_FALSE(Prepare().Available);
    Entity = AddPointCloud(*Scene, NoisyPlane());
    Request.StableEntityId = Runtime::SelectionController::ToStableEntityId(Entity);
    ASSERT_TRUE(Prepare().Pending);
    Drain();
    EXPECT_TRUE(Prepare().Available);
    const auto otherWorld = Engine.Worlds().CreateWorld("other readiness world");
    EXPECT_FALSE(Service->PrepareAvailability(otherWorld, Request).Available);
    ASSERT_TRUE(Engine.Worlds().RequestSetActiveWorld(otherWorld).has_value());
    (void)Engine.Worlds().ApplyMaintenance(Engine.Events(), Engine.Jobs());
    EXPECT_FALSE(Service->PrepareAvailability(Engine.ActiveWorld(), Request).Available);
}

TEST_F(ConsolidationReadiness, PendingCheckDoesNotSurviveShutdown)
{
    ASSERT_TRUE(Prepare().Pending);
    Engine.Shutdown();
    const auto unavailable = Service->PrepareAvailability({}, Request);
    EXPECT_FALSE(unavailable.Available);
    EXPECT_FALSE(unavailable.Pending);
    Engine.Initialize();
    Scene = Engine.Worlds().Get(Engine.ActiveWorld());
    Service = Engine.Services().Find<Runtime::PointCloudConsolidationService>();
    ASSERT_NE(Service, nullptr);
    ASSERT_NE(Scene, nullptr);
    Drain();
    EXPECT_EQ(Service->Stats().ReadinessPropertyScans, 0u);
    Entity = AddPointCloud(*Scene, NoisyPlane());
    Request.StableEntityId = Runtime::SelectionController::ToStableEntityId(Entity);
    ASSERT_TRUE(Prepare().Pending);
    Drain();
    EXPECT_TRUE(Prepare().Available);
    EXPECT_EQ(Service->Stats().ReadinessPropertyScans, 1u);
}

TEST_F(ConsolidationReadiness, EveryElementDomainAndStrategyMatchesDirectAdmission)
{
    const auto sources = AddDomainSources(*Scene);
    for (const auto domain : kAllElementDomains)
    {
        SCOPED_TRACE(std::string{Runtime::ToString(domain)});
        Entity = ResolveTestEntity(sources, domain);
        Request = MakeDomainRequest(Entity, domain, "sample:position", "projected");
        ASSERT_TRUE(Prepare().Pending);
        Drain();
        for (const auto strategy : {Runtime::PointCloudConsolidationStrategy::Lop,
            Runtime::PointCloudConsolidationStrategy::Wlop,
            Runtime::PointCloudConsolidationStrategy::Clop,
            Runtime::PointCloudConsolidationStrategy::Ear})
        {
            Request.Config.Strategy = strategy;
            EXPECT_TRUE(Prepare().Available);
            ExpectSameAsDirect(Prepare());
        }
    }
    EXPECT_EQ(Service->Stats().ReadinessPropertyScans, kAllElementDomains.size());
}

TEST_F(ConsolidationReadiness, RunRevalidatesAfterCachedReadiness)
{
    ASSERT_TRUE(Prepare().Pending);
    Drain();
    ASSERT_TRUE(Prepare().Available);
    Properties().Get<glm::vec3>("v:position")[0].x = std::numeric_limits<float>::infinity();
    std::optional<Runtime::PointCloudConsolidationResult> completion;
    const auto subscription = Service->SubscribeCompleted(
        [&](const Runtime::PointCloudConsolidationResult& result) { completion = result; });
    ASSERT_TRUE(Service->Run(Request).IsValid());
    Drain();
    (void)Engine.Events().Pump();
    Service->Unsubscribe(subscription);
    ASSERT_TRUE(completion.has_value());
    EXPECT_FALSE(completion->Succeeded());
    EXPECT_EQ(completion->Status, Runtime::PointCloudConsolidationRunStatus::UnsupportedPropertySource);
    EXPECT_EQ(Service->Stats().JobsSubmitted, 0u);
}


TEST_F(ConsolidationReadiness, MetadataFailuresDoNotQueueScansOrKeepObsoletePendingWork)
{
    for (const auto count : {0u, 1u, 1'000'001u})
    {
        Properties().Resize(count);
        const auto blocked = Prepare();
        EXPECT_FALSE(blocked.Available);
        EXPECT_FALSE(blocked.Pending);
        EXPECT_EQ(blocked.Error, Extrinsic::Core::ErrorCode::TypeMismatch);
        ExpectSameAsDirect(blocked);
    }
    EXPECT_EQ(Service->Stats().ReadinessChecksQueued, 0u);
    Properties().Resize(25u);
    Request.Config.Strategy = Runtime::PointCloudConsolidationStrategy::Clop;
    Request.Config.ClopMixtureComponentCount = 26u;
    EXPECT_FALSE(Prepare().Pending);
    EXPECT_FALSE(Prepare().Available);
    ExpectSameAsDirect(Prepare());
    EXPECT_EQ(Service->Stats().ReadinessChecksQueued, 0u);
    Request.Config = SameCardinalityConfig();
    ASSERT_TRUE(Prepare().Pending);
    (void)Properties().Add<float>("projected", 0.0f);
    const auto blocked = Prepare();
    EXPECT_FALSE(blocked.Pending);
    EXPECT_FALSE(blocked.Available);
    ExpectSameAsDirect(blocked);
    Drain();
    EXPECT_EQ(Service->Stats().ReadinessPropertyScans, 0u);
}

TEST_F(ConsolidationReadiness, ReplacingActivePreviewDiscardsOnlySupersededChecks)
{
    ASSERT_TRUE(Prepare().Pending);
    const auto first = Request;
    const auto secondEntity = AddPointCloud(*Scene, NoisyPlane());
    Request.StableEntityId = Runtime::SelectionController::ToStableEntityId(secondEntity);
    ASSERT_TRUE(Prepare().Pending);
    Drain();
    EXPECT_TRUE(Prepare().Available);
    EXPECT_EQ(Service->Stats().ReadinessPropertyScans, 1u);
    Request = first;
    ASSERT_TRUE(Prepare().Pending);
    Drain();
    EXPECT_TRUE(Prepare().Available);
    EXPECT_EQ(Service->Stats().ReadinessPropertyScans, 2u);
}

namespace
{
    class ResidentLop : public ::testing::Test
    {
    protected:
        Extrinsic::Tests::MockDevice Device;
        Extrinsic::Tests::EditorJobHarness Jobs;
        std::unique_ptr<Extrinsic::Graphics::IRenderer> Renderer{Extrinsic::Graphics::CreateRenderer()};
        Runtime::WorldRegistry Worlds;
        Runtime::CommandBus Commands;
        Runtime::KernelEventBus Events;
        Runtime::ServiceRegistry Services;
        Runtime::SpatialIndexCache Cache;
        Runtime::PointCloudConsolidationModule Module;
        Runtime::EditorCommandHistory History;
        Runtime::WorldHandle World;
        ECS::EntityHandle Entity;
        Runtime::PointCloudConsolidationService* Service{};
        Runtime::PointCloudConsolidationRequest Request;
        std::vector<Runtime::PointCloudConsolidationResult> Results;
        std::vector<glm::vec3> Before, Accepted;
        std::optional<Extrinsic::RHI::ReadbackSink> DiagnosticSink;
        Runtime::CommandCorrelationId Correlation;
        bool HoldCompletion{}, EarlyConvergence{};
        std::vector<std::function<void()>> ComputeCompletions;
        void Defer(Extrinsic::RHI::ReadbackSink sink, std::span<const std::byte> bytes)
        {
            ComputeCompletions.push_back([sink = std::move(sink), data = std::vector<std::byte>(bytes.begin(), bytes.end())]() mutable { sink.Deliver(data); });
        }
        std::optional<Extrinsic::RHI::ReadbackSink> PendingCompletion;
        Runtime::KernelEventSubscription Subscription;
        auto& Scene() { return *Worlds.Get(World); }
        auto& Properties() { return Scene().Raw().get<GS::Vertices>(Entity).Properties; }
        void SetUp() override
        {
            World = Worlds.CreateWorld("resident-lop");
            Entity = Scene().Create();
            Before = NoisyPlane();
            auto& vertices = Scene().Raw().emplace<GS::Vertices>(Entity);
            vertices.Properties.Resize(Before.size());
            vertices.Properties.GetOrAdd<glm::vec3>("v:position").Vector() = Before;
            Accepted = Before;
            for (auto& p : Accepted) p.z += 0.01f;
            Device.TransferQueue.AcceptBufferUploads = true;
            Renderer->Initialize(Device);
            Services.BeginRegistration();
            ASSERT_TRUE(Services.Provide<Extrinsic::RHI::IDevice>(Device, "test"));
            ASSERT_TRUE(Services.Provide<Extrinsic::Graphics::IRenderer>(*Renderer, "test"));
            ASSERT_TRUE(Services.Provide<Runtime::EditorCommandHistory>(History, "test"));
            Runtime::EngineSetup setup{Commands, Events, Jobs.Jobs(), Worlds, Services, [](Runtime::FramePhase, Runtime::RuntimeFrameHook) {}};
            ASSERT_TRUE(Cache.OnRegister(setup));
            ASSERT_TRUE(Module.OnRegister(setup));
            Services.BeginResolution();
            ASSERT_TRUE(Cache.OnResolve(setup));
            ASSERT_TRUE(Module.OnResolve(setup));
            Services.Lock();
            Service = Services.Find<Runtime::PointCloudConsolidationService>();
            ASSERT_NE(Service, nullptr);
            Subscription = Service->SubscribeCompleted([this](const auto& result) { Results.push_back(result); });
            Request.StableEntityId = Runtime::SelectionController::ToStableEntityId(Entity);
            Request.Config = SameCardinalityConfig();
            Request.Config.Strategy = Runtime::PointCloudConsolidationStrategy::Lop;
            Request.Config.Backend = Runtime::PointCloudConsolidationBackend::VulkanCompute;
            Request.Config.MaxIterations = 4u;
            Request.Config.GpuPreviewInterval = 2u;
            Request.Config.ConvergenceTolerance = 0.0;
            Request.AutoAccept = false;
            Request.Properties.InputNormals.reset();
            Request.Properties.OutputNormals.reset();
            Device.TransferQueue.BufferDownload = [this](auto, auto bytes, auto, auto sink) {
                EXPECT_EQ(bytes, 64u);
                DiagnosticSink = std::move(sink);
                return Extrinsic::RHI::ReadbackToken{1u};
            };
            Device.TransferQueue.BufferUploads.clear();
            Device.BufferWrites.clear();
            Device.ComputeReadback = [this](auto record, auto bytes, auto sink) {
                EXPECT_TRUE(record(Device.CommandContext).IsValid());
                if (HoldCompletion)
                {
                    PendingCompletion = std::move(sink);
                    return Extrinsic::RHI::ReadbackToken{2u};
                }
                if (bytes == 0u) Defer(std::move(sink), {});
                else if (bytes == 64u)
                {
                    const auto pipeline = std::find(Device.CreatedPipelineHandles.begin(),
                        Device.CreatedPipelineHandles.end(), Device.CommandContext.LastBoundPipeline);
                    EXPECT_NE(pipeline, Device.CreatedPipelineHandles.end());
                    const auto& path = Device.CreatedPipelineDescs[pipeline - Device.CreatedPipelineHandles.begin()].ComputeShaderPath;
                    if (path.find("lop_final_reduce") != std::string::npos)
                        DiagnosticSink = std::move(sink);
                    else
                    {
                        std::array<std::uint32_t, 16> data{};
                        data[3] = 1u;
                        data[11] = 2u * Before.size();
                        if (path.find("lop_iteration_finalize") != std::string::npos)
                        {
                            data[1] = Service->GpuRun(Correlation).Iterations + 1u;
                            data[9] = 1u;
                            data[10] = data[1];
                            data[2] = EarlyConvergence;
                            if (EarlyConvergence || data[1] == Request.Config.MaxIterations)
                            {
                                data[3] = 0u;
                                data[0] = EarlyConvergence ? 0u : 6u;
                            }
                        }
                        Defer(std::move(sink), std::as_bytes(std::span(data)));
                    }
                }
                else
                {
                    EXPECT_EQ(bytes, Accepted.size() * sizeof(glm::vec3));
                    Defer(std::move(sink), std::as_bytes(std::span(Accepted)));
                }
                return Extrinsic::RHI::ReadbackToken{2u};
            };
        }
        void TearDown() override
        {
            if (Service)
            {
                (void)Service->GpuRun(Correlation, Runtime::PointCloudConsolidationGpuAction::Discard);
                Service->Unsubscribe(Subscription);
            }
            Jobs.Jobs().CancelAndDrain();
            (void)Jobs.Jobs().ShutdownGpuQueueParticipants([this] { Device.WaitIdle(); });
            Runtime::RuntimeModuleShutdownContext shutdown{Commands, Events, Jobs.Jobs(), Worlds, Services};
            Module.OnShutdown(shutdown);
            Cache.OnShutdown(shutdown);
            Services.Reset();
            Renderer->Shutdown();
        }
        void Tick(bool advanceFrame = true)
        {
            auto completions = std::exchange(ComputeCompletions, {});
            for (auto& complete : completions) complete();
            Commands.Drain(Scene(), {.Events = &Events, .Jobs = &Jobs.Jobs(), .Worlds = &Worlds});
            (void)Jobs.Jobs().DrainCompletions(Events);
            (void)Events.Pump();
            Jobs.Jobs().RecordGpuQueueFrameCommands(Device.CommandContext);
            if (advanceFrame) ++Device.GlobalFrameNumber;
            (void)Jobs.Jobs().DrainGpuQueueCompletedTransfers();
        }
        template<class Predicate> bool Until(Predicate predicate)
        {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (!predicate() && std::chrono::steady_clock::now() < deadline)
            { Tick(); std::this_thread::yield(); }
            return predicate();
        }
        auto Observation() { return Service->GpuRun(Correlation); }
        void Start() { Correlation = Service->Run(Request); }
        void CompleteDiagnostics()
        {
            ASSERT_TRUE(DiagnosticSink);
            std::array<std::uint32_t, 16> bytes{};
            bytes[0] = EarlyConvergence ? 0u : 6u;
            bytes[2] = EarlyConvergence;
            bytes[1] = Observation().Iterations;
            bytes[9] = 1u;
            bytes[10] = bytes[1];
            DiagnosticSink->Deliver(std::as_bytes(std::span(bytes)));
            DiagnosticSink.reset();
        }
    };
}

TEST_F(ResidentLop, ResidentInputCompletionPagesPreviewCadenceAndDiscardReuse)
{
    HoldCompletion = true;
    Start();
    ASSERT_TRUE(Until([&] { return Observation().Submissions > 0u; }));
    EXPECT_EQ(Observation().InputUploadBytes, Before.size() * 12u);
    const auto submissions = Observation().Submissions;
    for (unsigned i = 0; i < 8u; ++i) Tick(false);
    EXPECT_EQ(Observation().Submissions, submissions) << "No completion means no next page";
    ASSERT_TRUE(PendingCompletion);
    std::array<std::uint32_t, 16> grid{};
    grid[3] = 1u;
    grid[11] = 2u * Before.size();
    HoldCompletion = false;
    PendingCompletion->Deliver(std::as_bytes(std::span(grid)));
    PendingCompletion.reset();
    ASSERT_TRUE(Until([&] { return DiagnosticSink.has_value(); }));
    EXPECT_EQ(Observation().Previews, 2u);
    EXPECT_EQ(std::count_if(Device.TransferQueue.BufferUploads.begin(), Device.TransferQueue.BufferUploads.end(),
        [&](const auto& upload) { return upload.Data.size() == Before.size() * 12u; }), 1);
    EXPECT_EQ(std::count_if(Device.TransferQueue.BufferUploads.begin(), Device.TransferQueue.BufferUploads.end(),
        [&](const auto& upload) { return upload.Data.size() == Before.size() * 16u; }), 0)
        << "LOP must not upload privately packed positions";
    EXPECT_GT(Observation().Submissions, Request.Config.MaxIterations);
    EXPECT_EQ(std::as_const(Properties()).Get<glm::vec3>("v:position").Vector(), Before);
    CompleteDiagnostics();
    ASSERT_TRUE(Until([&] { return Observation().ReadyToAccept; }));
    EXPECT_TRUE(Observation().CanAccept);
    EXPECT_TRUE(Results.empty());
    (void)Service->GpuRun(Correlation, Runtime::PointCloudConsolidationGpuAction::Discard);
    ASSERT_TRUE(Until([&] { return Results.size() == 1u; }));
    EXPECT_EQ(Results.back().Status, Runtime::PointCloudConsolidationRunStatus::Cancelled);
    Start();
    ASSERT_TRUE(Until([&] { return Observation().Submissions > 0u; }));
    EXPECT_EQ(Observation().InputUploadBytes, 0u);
    EXPECT_EQ(Observation().InputCacheHits, 1u);
}

TEST_F(ResidentLop, PreviewCopiesOnlyAtIntervalAndTerminalBoundaries)
{
    Request.Config.MaxIterations = 10u;
    Request.Config.GpuPreviewInterval = 5u;
    Start();
    ASSERT_TRUE(Until([&] { return DiagnosticSink.has_value(); }));
    EXPECT_EQ(Observation().Previews, 2u);
    // The first two copies seed the private ping-pong buffers. Every remaining
    // position copy must target a preview slot, exactly at iterations 5 and 10.
    const auto& copies = Device.CommandContext.CopyBufferRecords;
    ASSERT_EQ(copies.size(), 4u);
    EXPECT_EQ(copies[0].Src, copies[1].Src);
    for (std::size_t i = 2u; i < copies.size(); ++i)
    {
        EXPECT_TRUE(copies[i].Src == copies[0].Dst || copies[i].Src == copies[1].Dst);
        EXPECT_NE(copies[i].Dst, copies[0].Dst);
        EXPECT_NE(copies[i].Dst, copies[1].Dst);
    }
}

TEST_F(ResidentLop, BoundaryPublishesCopiedBackOnlyAfterCompletion)
{
    auto* residency = Cache.PropertyResidency();
    ASSERT_NE(residency, nullptr);
    const auto key = Runtime::MakeGpuPropertyKey(World, Entity, Request.Properties.OutputPositions);
    Extrinsic::RHI::BufferHandle copiedBack{}, copiedSource{}, input{};
    const auto submit = Device.ComputeReadback;
    Device.ComputeReadback = [&](auto record, auto bytes, auto sink) {
        const bool boundary = bytes == 0u && Observation().Previews == 0u &&
            Observation().Iterations == Request.Config.GpuPreviewInterval;
        HoldCompletion = boundary;
        const auto token = submit([&](auto& commands) {
            const auto before = Device.CommandContext.CopyBufferRecords.size();
            const auto output = record(commands);
            if (boundary)
            {
                const auto& copies = Device.CommandContext.CopyBufferRecords;
                EXPECT_EQ(copies.size(), before + 1u) << "Boundary must copy into the back before Publish";
                if (copies.size() == before + 1u && before >= 2u)
                {
                    input = copies[0].Src;
                    copiedBack = copies.back().Dst;
                    copiedSource = copies.back().Src;
                    // Upload seeds B, then A; iteration 2 completes in A.
                    EXPECT_EQ(copiedSource, copies[1].Dst);
                    EXPECT_NE(copiedBack, input);
                    EXPECT_NE(copiedBack, copies[0].Dst);
                    EXPECT_NE(copiedBack, copies[1].Dst);
                }
            }
            return output;
        }, bytes, std::move(sink));
        HoldCompletion = false;
        return token;
    };
    Start();
    ASSERT_TRUE(Until([&] { return PendingCompletion.has_value(); }));
    ASSERT_TRUE(copiedBack.IsValid());
    ASSERT_TRUE(residency->Front(key));
    EXPECT_EQ(Observation().Iterations, Request.Config.GpuPreviewInterval);
    EXPECT_EQ(Observation().Previews, 0u);
    EXPECT_EQ(residency->Front(key)->Buffer, input)
        << "Iteration count and a ring alone do not prove preview publication";
    for (unsigned i = 0; i < 4u; ++i) Tick();
    EXPECT_EQ(Observation().Previews, 0u);
    EXPECT_EQ(residency->Front(key)->Buffer, input);
    PendingCompletion->Deliver({});
    PendingCompletion.reset();
    ASSERT_TRUE(Until([&] { return Observation().Previews == 1u; }));
    ASSERT_TRUE(residency->Front(key));
    EXPECT_EQ(residency->Front(key)->Buffer, copiedBack);
    EXPECT_NE(residency->Front(key)->Publication, 0u);
    EXPECT_EQ(Observation().Iterations, Request.Config.GpuPreviewInterval);
}

TEST_F(ResidentLop, BatchesPagesWithinCandidateBudgetAndCoversReductionTail)
{
    struct RestoreLimits
    {
        Runtime::LopPagingLimits Saved{Runtime::LopPagingForTesting};
        ~RestoreLimits() { Runtime::LopPagingForTesting = Saved; }
    } restore;
    Runtime::LopPagingForTesting = {.PagePairs = 150u, .SubmissionPairs = 512u, .ReduceRows = 7u};
    Request.Config.MaxIterations = 2u;
    std::vector<std::uint32_t> initialized, projected, reduced;
    bool batched = false;
    const auto submit = Device.ComputeReadback;
    Device.ComputeReadback = [&](auto record, auto bytes, auto sink) {
        return submit([&](auto& commands) {
            const auto begin = Device.CommandContext.PushConstantPayloads.size();
            const auto output = record(commands);
            const auto pipeline = std::find(Device.CreatedPipelineHandles.begin(),
                Device.CreatedPipelineHandles.end(), Device.CommandContext.LastBoundPipeline);
            const auto& path = Device.CreatedPipelineDescs[pipeline - Device.CreatedPipelineHandles.begin()].ComputeShaderPath;
            const bool initialize = path.find("lop_initialize") != std::string::npos;
            const bool project = path.find("lop_project") != std::string::npos;
            const bool reduce = path.find("lop_final_reduce") != std::string::npos;
            if (initialize || project || reduce)
            {
                std::uint32_t rows = 0u;
                const auto& pushes = Device.CommandContext.PushConstantPayloads;
                if (!reduce && pushes.size() - begin > 1u) batched = true;
                for (auto i = begin; i < pushes.size(); ++i)
                {
                    EXPECT_EQ(pushes[i].size(), 88u);
                    std::uint32_t first{}, count{};
                    std::memcpy(&first, pushes[i].data() + 52u, sizeof(first));
                    std::memcpy(&count, pushes[i].data() + 80u, sizeof(count));
                    EXPECT_LE(count, reduce ? 7u : 3u);
                    auto& covered = initialize ? initialized : project ? projected : reduced;
                    for (auto row = first; row < first + count; ++row) covered.push_back(row);
                    rows += count;
                }
                if (!reduce) EXPECT_LE(rows * 50u, 512u);
            }
            return output;
        }, bytes, std::move(sink));
    };
    Start();
    ASSERT_TRUE(Until([&] { return DiagnosticSink.has_value(); }));
    EXPECT_TRUE(batched);
    ASSERT_EQ(initialized.size(), Before.size());
    ASSERT_EQ(projected.size(), Before.size() * 2u);
    ASSERT_EQ(reduced.size(), Before.size());
    for (std::uint32_t i = 0u; i < Before.size(); ++i)
    {
        EXPECT_EQ(initialized[i], i);
        EXPECT_EQ(projected[i], i);
        EXPECT_EQ(projected[i + Before.size()], i);
        EXPECT_EQ(reduced[i], i);
    }
    CompleteDiagnostics();
    ASSERT_TRUE(Until([&] { return Observation().CanAccept; }));
}

TEST_F(ResidentLop, TwoIterationFixtureRequiresValidNormalRefinementBudget)
{
    Request.Config.SupportRadius = 0.5;
    Request.Config.TargetPointCount = 0u;
    Request.Config.Seed = 42u;
    Request.Config.MaxIterations = 2u;
    Request.Config.NormalRefinementRounds = 3u;
    Start();
    ASSERT_TRUE(Until([&] { return Results.size() == 1u; }));
    EXPECT_FALSE(Results.back().Succeeded());
    EXPECT_NE(Results.back().Message.find("outside the validated runtime control surface"), std::string::npos);
    EXPECT_EQ(Observation().Submissions, 0u);

    Request.Config.NormalRefinementRounds = 1u;
    Start();
    ASSERT_TRUE(Until([&] { return DiagnosticSink.has_value(); }));
    CompleteDiagnostics();
    ASSERT_TRUE(Until([&] { return Observation().CanAccept; }));
    EXPECT_EQ(Observation().Iterations, 2u);
}

TEST_F(ResidentLop, SynchronousAcceptRejectionCompletesExactlyOnce)
{
    Start();
    ASSERT_TRUE(Until([&] { return DiagnosticSink.has_value(); }));
    CompleteDiagnostics();
    ASSERT_TRUE(Until([&] { return Observation().CanAccept; }));
    // Removing the front preserves CPU currency but makes Accept call its
    // rejection sink synchronously, then read the retained run result.
    auto* residency = Cache.PropertyResidency();
    ASSERT_NE(residency, nullptr);
    residency->Discard(Runtime::MakeGpuPropertyKey(World, Entity, Request.Properties.OutputPositions));
    (void)Service->GpuRun(Correlation, Runtime::PointCloudConsolidationGpuAction::Accept);
    ASSERT_TRUE(Until([&] { return Results.size() == 1u; }));
    EXPECT_EQ(Results.back().Status, Runtime::PointCloudConsolidationRunStatus::GeometryProcessingFailed);
    EXPECT_EQ(std::as_const(Properties()).Get<glm::vec3>("v:position").Vector(), Before);
    for (unsigned i = 0u; i < 10u; ++i) Tick();
    EXPECT_EQ(Results.size(), 1u);
}

TEST_F(ResidentLop, AcceptPublishesExactlyOnceAndStaleOnlyAllowsDiscard)
{
    Start();
    ASSERT_TRUE(Until([&] { return DiagnosticSink.has_value(); }));
    CompleteDiagnostics();
    ASSERT_TRUE(Until([&] { return Observation().CanAccept; }));
    (void)Service->GpuRun(Correlation, Runtime::PointCloudConsolidationGpuAction::Accept);
    ASSERT_TRUE(Until([&] { return Results.size() == 1u; }));
    EXPECT_TRUE(Results.back().Succeeded()) << Results.back().Message;
    EXPECT_EQ(std::as_const(Properties()).Get<glm::vec3>("v:position").Vector(), Accepted);
    for (unsigned i = 0; i < 10u; ++i) Tick();
    EXPECT_EQ(Results.size(), 1u);
    Start();
    ASSERT_TRUE(Until([&] { return DiagnosticSink.has_value(); }));
    EXPECT_EQ(Observation().InputUploadBytes, 0u) << "Accept bound the canonical revision";
    CompleteDiagnostics();
    ASSERT_TRUE(Until([&] { return Observation().ReadyToAccept; }));
    Properties().Get<glm::vec3>("v:position")[0].x += 0.25f;
    EXPECT_FALSE(Observation().CanAccept);
    EXPECT_FALSE(Observation().Message.empty());
    (void)Service->GpuRun(Correlation, Runtime::PointCloudConsolidationGpuAction::Accept);
    EXPECT_EQ(Results.size(), 1u);
}

TEST_F(ResidentLop, BatchAutoAcceptAndDetachDiscard)
{
    Request.AutoAccept = true;
    Start();
    ASSERT_TRUE(Until([&] { return DiagnosticSink.has_value(); }));
    CompleteDiagnostics();
    ASSERT_TRUE(Until([&] { return Results.size() == 1u; }));
    EXPECT_TRUE(Results.back().Succeeded());
    bool attached = true;
    Request.AttachmentActive = [&] { return attached; };
    Start();
    ASSERT_TRUE(Until([&] { return Observation().Submissions > 0u; }));
    attached = false;
    ASSERT_TRUE(Until([&] { return Results.size() == 2u; }));
    EXPECT_EQ(Results.back().Status, Runtime::PointCloudConsolidationRunStatus::Cancelled);
}

TEST_F(ResidentLop, StopPublishesATerminalFrontBetweenPreviewIntervals)
{
    Request.Config.MaxIterations = 9u;
    Request.Config.GpuPreviewInterval = 4u;
    Start();
    ASSERT_TRUE(Until([&] { return Observation().Iterations >= 1u; }));
    (void)Service->GpuRun(Correlation, Runtime::PointCloudConsolidationGpuAction::Stop);
    ASSERT_TRUE(Until([&] { return DiagnosticSink.has_value(); }));
    EXPECT_LT(Observation().Iterations, Request.Config.MaxIterations);
    EXPECT_GE(Observation().Previews, 1u);
    CompleteDiagnostics();
    ASSERT_TRUE(Until([&] { return Observation().CanAccept; }));
    EXPECT_EQ(std::as_const(Properties()).Get<glm::vec3>("v:position").Vector(), Before);
    (void)Service->GpuRun(Correlation, Runtime::PointCloudConsolidationGpuAction::Accept);
    ASSERT_TRUE(Until([&] { return Results.size() == 1u; }));
    EXPECT_EQ(Results.back().GeometryStatus, Geometry::PointCloud::Consolidation::Status::NotConverged);
    EXPECT_NE(Results.back().Error, Extrinsic::Core::ErrorCode::Success);
    EXPECT_FALSE(Results.back().Converged);
}

TEST_F(ResidentLop, ConvergencePublishesTerminalFrontBeforePreviewInterval)
{
    EarlyConvergence = true;
    Start();
    ASSERT_TRUE(Until([&] { return DiagnosticSink.has_value(); }));
    EXPECT_EQ(Observation().Iterations, 1u);
    EXPECT_EQ(Observation().Previews, 1u);
    CompleteDiagnostics();
    ASSERT_TRUE(Until([&] { return Observation().CanAccept; }));
    (void)Service->GpuRun(Correlation, Runtime::PointCloudConsolidationGpuAction::Accept);
    ASSERT_TRUE(Until([&] { return Results.size() == 1u; }));
    EXPECT_TRUE(Results.back().Converged);
    EXPECT_EQ(Results.back().Iterations, 1u);
}

TEST_F(ResidentLop, PendingAcceptDiscardCompletesExactlyOnce)
{
    Start();
    ASSERT_TRUE(Until([&] { return DiagnosticSink.has_value(); }));
    CompleteDiagnostics();
    ASSERT_TRUE(Until([&] { return Observation().CanAccept; }));
    HoldCompletion = true;
    (void)Service->GpuRun(Correlation, Runtime::PointCloudConsolidationGpuAction::Accept);
    ASSERT_TRUE(Until([&] { return PendingCompletion.has_value(); }));
    ASSERT_TRUE(Observation().Accepting);
    (void)Service->GpuRun(Correlation, Runtime::PointCloudConsolidationGpuAction::Discard);
    ASSERT_TRUE(Until([&] { return Results.size() == 1u; }));
    EXPECT_EQ(Results.back().Status, Runtime::PointCloudConsolidationRunStatus::Cancelled);
    PendingCompletion->Deliver(std::as_bytes(std::span(Accepted)));
    PendingCompletion.reset();
    for (unsigned i = 0; i < 10u; ++i) Tick();
    EXPECT_EQ(Results.size(), 1u);
    EXPECT_EQ(std::as_const(Properties()).Get<glm::vec3>("v:position").Vector(), Before);
}

TEST_F(ResidentLop, PendingAcceptStaleCompletesExactlyOnce)
{
    Start();
    ASSERT_TRUE(Until([&] { return DiagnosticSink.has_value(); }));
    CompleteDiagnostics();
    ASSERT_TRUE(Until([&] { return Observation().CanAccept; }));
    HoldCompletion = true;
    (void)Service->GpuRun(Correlation, Runtime::PointCloudConsolidationGpuAction::Accept);
    ASSERT_TRUE(Until([&] { return PendingCompletion.has_value(); }));
    Properties().Get<glm::vec3>("v:position")[0].x += 0.25f;
    const auto edited = std::as_const(Properties()).Get<glm::vec3>("v:position").Vector();
    ASSERT_TRUE(Until([&] { return Results.size() == 1u; }));
    EXPECT_EQ(Results.back().Status, Runtime::PointCloudConsolidationRunStatus::StaleSource);
    PendingCompletion->Deliver(std::as_bytes(std::span(Accepted)));
    PendingCompletion.reset();
    for (unsigned i = 0; i < 10u; ++i) Tick();
    EXPECT_EQ(Results.size(), 1u);
    EXPECT_EQ(std::as_const(Properties()).Get<glm::vec3>("v:position").Vector(), edited);
}

TEST_F(ResidentLop, BusyStartIsRefusedWithoutCpuFallback)
{
    Start();
    ASSERT_TRUE(Until([&] { return Observation().Submissions > 0u; }));
    const auto refused = Service->Run(Request);
    ASSERT_TRUE(Until([&] { return !Results.empty(); }));
    EXPECT_EQ(Results.front().Correlation, refused);
    EXPECT_FALSE(Results.front().Succeeded());
    EXPECT_FALSE(Results.front().FellBackToCpu);
    EXPECT_EQ(Service->Stats().GpuFallbacks, 0u);
    EXPECT_FALSE(Results.front().BackendDiagnostic.empty());
}

TEST_F(ResidentLop, RejectedAcceptSubmissionDeliversOneFailure)
{
    Start();
    ASSERT_TRUE(Until([&] { return DiagnosticSink.has_value(); }));
    CompleteDiagnostics();
    ASSERT_TRUE(Until([&] { return Observation().CanAccept; }));
    Jobs.Jobs().CancelAndDrain();
    (void)Service->GpuRun(Correlation, Runtime::PointCloudConsolidationGpuAction::Accept);
    ASSERT_TRUE(Until([&] { return !Results.empty(); }));
    EXPECT_EQ(Results.size(), 1u);
    EXPECT_FALSE(Results.back().Succeeded());
    EXPECT_EQ(std::as_const(Properties()).Get<glm::vec3>("v:position").Vector(), Before);
}

TEST_F(ResidentLop, StaleBatchFinishesWithoutPublishingOrWaitingForAccept)
{
    Request.AutoAccept = true;
    Start();
    ASSERT_TRUE(Until([&] { return DiagnosticSink.has_value(); }));
    Properties().Get<glm::vec3>("v:position")[0].x += 0.5f;
    const auto edited = std::as_const(Properties()).Get<glm::vec3>("v:position").Vector();
    CompleteDiagnostics();
    ASSERT_TRUE(Until([&] { return Results.size() == 1u; }));
    EXPECT_EQ(Results.back().Status, Runtime::PointCloudConsolidationRunStatus::StaleSource);
    EXPECT_EQ(std::as_const(Properties()).Get<glm::vec3>("v:position").Vector(), edited);
}

TEST_F(ResidentLop, SwitchingWorldsDiscardsABatchRun)
{
    Request.AutoAccept = true;
    Start();
    ASSERT_TRUE(Until([&] { return Observation().Submissions > 0u; }));
    const auto other = Worlds.CreateWorld("other");
    ASSERT_TRUE(Worlds.RequestSetActiveWorld(other));
    (void)Worlds.ApplyMaintenance(Events, Jobs.Jobs());
    ASSERT_TRUE(Until([&] { return Results.size() == 1u; }));
    EXPECT_EQ(Results.back().Status, Runtime::PointCloudConsolidationRunStatus::Cancelled);
    EXPECT_EQ(std::as_const(Properties()).Get<glm::vec3>("v:position").Vector(), Before);
}

TEST_F(ResidentLop, DiscardingQueuedPagesAllowsANewGpuRun)
{
    Start();
    ASSERT_TRUE(Until([&] { return Observation().Submissions >= 2u; }));
    (void)Service->GpuRun(Correlation, Runtime::PointCloudConsolidationGpuAction::Discard);
    ASSERT_TRUE(Until([&] { return Results.size() == 1u; }));
    EXPECT_EQ(Results.back().Status, Runtime::PointCloudConsolidationRunStatus::Cancelled);
    Start();
    ASSERT_TRUE(Until([&] { return Observation().Submissions > 0u; }));
    EXPECT_EQ(Observation().InputUploadBytes, 0u);
    EXPECT_EQ(Service->Stats().GpuFallbacks, 0u);
}

TEST(PointCloudConsolidationModule, AgentCompletionListenerReleasedOnDetachCancellationAndCompletion)
{
    auto config = HeadlessConfig();
    Runtime::SetPointCloudConsolidationConfig(config, SameCardinalityConfig());
    Intrinsic::Tests::RuntimeTestKernel engine{std::move(config)};
    engine.EmplaceModule<Runtime::PointCloudConsolidationModule>();
    engine.EmplaceModule<Runtime::SceneDocumentModule>();
    CoreConfig::EngineConfigSectionRegistry sections;
    ASSERT_TRUE(sections.Register(Runtime::MakePointCloudConsolidationConfigSectionRegistration()));
    engine.EmplaceModule<Runtime::EngineConfigControl>(std::move(sections));
    engine.Initialize();
    auto* scene = engine.Worlds().Get(engine.ActiveWorld());
    ASSERT_NE(scene, nullptr);
    const auto entity = AddPointCloud(*scene, NoisyPlane());
    Runtime::AgentOperationRegistry registry;
    Runtime::RegisterEditorAgentOperations(registry);
    Runtime::EditorWorkspaceAttachment attachment;
    const Runtime::AgentOperationContext context{.Attachment = &attachment};
    const auto listenerCalls = [&] {
        const auto before = engine.Events().Stats().ListenerInvocations;
        engine.Events().Publish(Runtime::PointCloudConsolidationResult{});
        (void)engine.Events().Pump();
        return engine.Events().Stats().ListenerInvocations - before;
    };
    const auto detachedListeners = listenerCalls();
    for (const auto mode : {"detach", "cancel", "complete"})
    {
        SCOPED_TRACE(mode);
        attachment.Attach(engine.Worlds(), engine.Services());
        ASSERT_TRUE(Runtime::PrepareEditorWorkspaceSnapshotFrame(attachment));
        const auto attachedListeners = listenerCalls();
        auto outcome = Runtime::InvokeAgentOperation(registry, "run_point_cloud_consolidation", context,
            "{\"entity\":" + std::to_string(Runtime::SelectionController::ToStableEntityId(entity)) +
            ",\"domain\":\"PointCloudPoint\"}", false);
        ASSERT_TRUE(outcome.Continuation) << outcome.Text;
        EXPECT_EQ(listenerCalls(), attachedListeners + 1u);
        Runtime::AgentOperationOutcome completed;
        if (std::string_view(mode) == "detach")
        {
            attachment.Detach();
            EXPECT_TRUE(outcome.Continuation(context, completed));
            EXPECT_TRUE(completed.IsError);
            EXPECT_EQ(listenerCalls(), detachedListeners);
        }
        else if (std::string_view(mode) == "cancel")
        {
            outcome.Continuation = {};
            EXPECT_EQ(listenerCalls(), attachedListeners);
        }
        else
        {
            bool done = false;
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (!done && std::chrono::steady_clock::now() < deadline)
            {
                engine.Commands().Drain(*scene, {&engine.Events(), &engine.Jobs(), &engine.Worlds()});
                (void)engine.Jobs().DrainCompletions(engine.Events());
                (void)engine.Events().Pump();
                done = outcome.Continuation(context, completed);
                std::this_thread::yield();
            }
            EXPECT_TRUE(done);
            EXPECT_EQ(listenerCalls(), attachedListeners);
        }
        outcome.Continuation = {};
        attachment.Detach();
    }
    engine.Shutdown();
}
