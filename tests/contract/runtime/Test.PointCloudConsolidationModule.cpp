#include "Modules/PointCloudConsolidation/Runtime.LopPaging.TestSupport.hpp"
#include <map>
#include <mutex>
#include <set>
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
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

#include "RuntimeTestModule.hpp"
#include "MockRHI.hpp"
#include "SandboxEditorJobHarness.hpp"
#include <cstring>

import Extrinsic.Runtime.Module;
import Extrinsic.Runtime.AgentOperations;
import Extrinsic.Runtime.PointCloudServiceOperations;
import Extrinsic.Runtime.EditorWorkspaceAttachment;
import Extrinsic.Runtime.EditorWorkspaceSnapshots;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.ServiceRegistry;
import Extrinsic.Runtime.StepRuns;
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
import Geometry.PointSampling;
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
            .Execution = {.IterationCap = 1u,
                .PreviewEvery = 5u,
                .PreviewIntervalMs = 0u,
                .AutoAccept = Runtime::StepRunAutoAccept::End,
                .Start = Runtime::StepRunStart::Run},
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
            .StableEntityId = Runtime::SelectionController::ToStableEntityId(entity),
            .Config =
                Runtime::PointCloudConsolidationConfig{
                    .Strategy = Runtime::PointCloudConsolidationStrategy::Wlop,
                    .SupportRadius = 0.65,
                    .RepulsionWeight = 0.0,
                    .Execution = {.IterationCap = 1u,
                        .PreviewEvery = 5u,
                        .PreviewIntervalMs = 0u,
                        .AutoAccept = Runtime::StepRunAutoAccept::End,
                        .Start = Runtime::StepRunStart::Run},
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
            if (Edit) Edit(request);
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
        std::function<void(Runtime::PointCloudConsolidationRequest&)> Edit{};
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
                    JobCorrelation = found->CorrelationId;
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
        std::uint64_t JobCorrelation{0u};
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
            // A property no run writes: its revision survives every publication, undo and redo (ARCH-022 Q90).
            KeepRevision = Scene->Raw()
                               .get<GS::Vertices>(Sources.PointCloud)
                               .Properties.GetOrAdd<float>("keep", 1.0f)
                               .Revision();
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
        Geometry::PropertyRevision KeepRevision{};
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

// RUNTIME-289: a farthest-point initial order asked on Vulkan without a spatial compute service
// runs on the CPU and says why; the seeded random subsample reports no ordering backend.
TEST(PointCloudConsolidationModule, VulkanInitialSamplesWithoutADeviceRunOnTheCpuAndSayWhy)
{
    for (const bool farthest : {true, false})
    {
        auto app = std::make_unique<ConsolidationSuccessApp>();
        ConsolidationSuccessApp* appPtr = app.get();
        app->Edit = [farthest](Runtime::PointCloudConsolidationRequest& request) {
            request.Config.InitialBackend = Runtime::PointSamplingBackend::Vulkan;
            if (farthest) request.Config.InitialSampling.Method = Runtime::PointSamplingMethod::FarthestPoint;
        };
        Intrinsic::Tests::RuntimeTestKernel engine{HeadlessConfig(), std::move(app)};
        engine.EmplaceModule<Runtime::PointCloudConsolidationModule>();
        engine.EmplaceModule<Runtime::StepRunsModule>(); // every consolidation run is a step run
        engine.EmplaceModule<Runtime::SceneDocumentModule>();
        engine.Initialize();
        engine.Run();
        ASSERT_TRUE(appPtr->Completion.has_value());
        ASSERT_TRUE(appPtr->Completion->Succeeded()) << appPtr->Completion->Message;
        EXPECT_EQ(appPtr->Completion->OutputPointCount, 16u);
        EXPECT_EQ(appPtr->Completion->InitialSamplingRequestedBackend, "gpu_vulkan_compute");
        EXPECT_EQ(appPtr->Completion->InitialSamplingBackend, farthest ? "cpu_reference" : "");
        EXPECT_EQ(appPtr->Completion->InitialSamplingDiagnostic,
                  farthest ? "No spatial compute service; sampling ran on the CPU." : "");
        engine.Shutdown();
    }
}

TEST(PointCloudConsolidationModule,
     CommitsThroughGeometrySourcesAndOwnsUndoRedo)
{
    auto app = std::make_unique<ConsolidationSuccessApp>();
    ConsolidationSuccessApp* appPtr = app.get();
    Intrinsic::Tests::RuntimeTestKernel engine{
        HeadlessConfig(), std::move(app)};
    engine.EmplaceModule<Runtime::PointCloudConsolidationModule>();
    engine.EmplaceModule<Runtime::StepRunsModule>(); // every consolidation run is a step run
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
    engine.EmplaceModule<Runtime::StepRunsModule>(); // every consolidation run is a step run
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

    const auto keepRevision = [&] { return pointVertices.Get<float>("keep").Revision(); };
    EXPECT_EQ(keepRevision(), appPtr->KeepRevision) << "a publication renumbered an untouched property";
    Runtime::EditorCommandHistory* history =
        engine.Services().Find<Runtime::EditorCommandHistory>();
    ASSERT_NE(history, nullptr);
    ASSERT_EQ(history->UndoCount(), 8u);
    for (std::size_t index = 0u; index < 8u; ++index)
    {
        EXPECT_EQ(history->Undo().Status,
                  Runtime::EditorCommandHistoryStatus::Undone);
    }
    EXPECT_EQ(keepRevision(), appPtr->KeepRevision) << "an undo renumbered an untouched property";
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
    EXPECT_EQ(keepRevision(), appPtr->KeepRevision) << "a redo renumbered an untouched property";

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
    engine.EmplaceModule<Runtime::StepRunsModule>(); // every consolidation run is a step run
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
    engine.EmplaceModule<Runtime::StepRunsModule>(); // every consolidation run is a step run
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
    engine.EmplaceModule<Runtime::StepRunsModule>(); // every consolidation run is a step run
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
        engine.EmplaceModule<Runtime::StepRunsModule>(); // every consolidation run is a step run
        engine.Initialize();
        engine.Run();

        EXPECT_FALSE(appPtr->MissingService);
        EXPECT_FALSE(appPtr->TimedOut);
        EXPECT_TRUE(appPtr->Mutated);
        // The run's job carries its submission's correlation id (what the shared progress model keys on).
        EXPECT_EQ(appPtr->JobCorrelation, appPtr->Correlation.Value);
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
    engine.EmplaceModule<Runtime::StepRunsModule>(); // every consolidation run is a step run
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
    engine.EmplaceModule<Runtime::StepRunsModule>(); // every consolidation run is a step run
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
    engine.EmplaceModule<Runtime::StepRunsModule>();
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
    engine.EmplaceModule<Runtime::StepRunsModule>();
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
            Engine.EmplaceModule<Runtime::StepRunsModule>(); // every consolidation run is a step run
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

TEST_F(ConsolidationReadiness, UnusedNormalRoundsDoNotRejectShortProjectionRuns)
{
    Request.Config.Execution.IterationCap = 1u;
    Request.Config.NormalRefinementRounds = 3u;
    for (const auto strategy : {Runtime::PointCloudConsolidationStrategy::Lop,
                               Runtime::PointCloudConsolidationStrategy::Wlop,
                               Runtime::PointCloudConsolidationStrategy::Clop})
    {
        Request.Config.Strategy = strategy;
        Request.Config.WlopAnisotropic = false;
        const auto result = Runtime::ResolvePointCloudConsolidationAvailability(
            Runtime::BuildGeometryAvailability(Scene->Raw(), Entity), Request.Properties, Request.Config);
        EXPECT_TRUE(result.Available) << result.Message;
    }
    Request.Config.Strategy = Runtime::PointCloudConsolidationStrategy::Ear;
    EXPECT_FALSE(Runtime::ResolvePointCloudConsolidationAvailability(
        Runtime::BuildGeometryAvailability(Scene->Raw(), Entity), Request.Properties, Request.Config).Available);
    Request.Config.Strategy = Runtime::PointCloudConsolidationStrategy::Wlop;
    Request.Config.WlopAnisotropic = true;
    EXPECT_FALSE(Runtime::ResolvePointCloudConsolidationAvailability(
        Runtime::BuildGeometryAvailability(Scene->Raw(), Entity), Request.Properties, Request.Config).Available);
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
        Runtime::StepRunsService StepRuns; // the in-place run's controls (outlives the module)
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
        std::atomic<bool> HoldCompletion{false}; // read by the submitting (worker) thread
        bool EarlyConvergence{};
        // ARCH-023: iteration pages are submitted and waited for on the step run's worker. The mock GPU completes a
        // page at the next Tick (an owner page's sink is delivered; a worker's pulled page becomes complete and its
        // bytes are taken by its worker through WaitCompute/TakeCompletedReadback), so the page order stays as
        // deterministic as the frame-paced one. Gpu guards what the worker's submissions and waits touch.
        std::recursive_mutex Gpu;
        std::vector<std::function<void()>> ComputeCompletions;
        std::uint64_t Tokens{0u};
        std::map<std::uint64_t, std::vector<std::byte>> PulledBytes{}; // completed pulled pages
        std::set<std::uint64_t> CompletedTokens{};
        std::uint32_t Finalized{0u}; // iterations the mock device finalized in this run (reset by Start)
        bool Stopping{false};
        // A worker's pulled page completes as soon as it is submitted (no Tick, frame or drain needed).
        bool CompletePulledAtOnce{false};
        Extrinsic::RHI::ReadbackToken Defer(Extrinsic::RHI::ReadbackSink sink, std::span<const std::byte> bytes)
        {
            std::scoped_lock lock{Gpu};
            const Extrinsic::RHI::ReadbackToken token{++Tokens};
            auto data = std::vector<std::byte>(bytes.begin(), bytes.end());
            if (sink.Pulled && CompletePulledAtOnce)
            {
                PulledBytes[token.Value] = std::move(data);
                CompletedTokens.insert(token.Value);
            }
            else if (sink.Pulled)
                ComputeCompletions.push_back(
                    [this, token, data = std::move(data)]() mutable
                    {
                        std::scoped_lock lock{Gpu};
                        PulledBytes[token.Value] = std::move(data);
                        CompletedTokens.insert(token.Value);
                    });
            else
                ComputeCompletions.push_back(
                    [this, token, sink = std::move(sink), data = std::move(data)]() mutable
                    {
                        sink.Deliver(data);
                        std::scoped_lock lock{Gpu};
                        CompletedTokens.insert(token.Value);
                    });
            return token;
        }
        std::optional<Extrinsic::RHI::ReadbackSink> PendingCompletion;
        std::uint64_t PendingToken{0u};
        // A held page is in flight (submitted by the worker or the owner).
        [[nodiscard]] bool HasHeld()
        {
            std::scoped_lock lock{Gpu};
            return PendingCompletion.has_value();
        }
        // The held page completes with `bytes`: its sink runs, or its worker takes them.
        void ReleaseHeld(std::span<const std::byte> bytes)
        {
            std::optional<Extrinsic::RHI::ReadbackSink> sink;
            {
                std::scoped_lock lock{Gpu};
                sink = std::exchange(PendingCompletion, std::nullopt);
                ASSERT_TRUE(sink);
                if (sink->Pulled)
                {
                    PulledBytes[PendingToken] = std::vector<std::byte>(bytes.begin(), bytes.end());
                    CompletedTokens.insert(PendingToken);
                    return;
                }
            }
            sink->Deliver(bytes);
            std::scoped_lock lock{Gpu};
            CompletedTokens.insert(PendingToken);
        }
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
            ASSERT_TRUE(Services.Provide<Runtime::StepRunsService>(StepRuns, "test"));
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
            Request.Config.Execution.IterationCap = 4u;
            Request.Config.Execution.PreviewEvery = 2u;
            Request.Config.ConvergenceTolerance = 0.0;
            Request.Config.Execution.AutoAccept = Runtime::StepRunAutoAccept::Never;
            Request.Properties.InputNormals.reset();
            Request.Properties.OutputNormals.reset();
            Device.TransferQueue.BufferDownload = [this](auto, auto bytes, auto, auto sink) {
                EXPECT_EQ(bytes, 64u);
                DiagnosticSink = std::move(sink);
                return Extrinsic::RHI::ReadbackToken{1u};
            };
            Device.TransferQueue.BufferUploads.clear();
            Device.BufferWrites.clear();
            // An invalid token names no submission and is complete (the Vulkan transfer queue's answer): a query
            // session's first UploadQueries waits on its last token, 0 before its first page.
            Device.TransferQueue.ComputeWait = [this](Extrinsic::RHI::ReadbackToken token, std::uint64_t)
            {
                {
                    std::scoped_lock lock{Gpu};
                    if (Stopping || !token.IsValid() || CompletedTokens.contains(token.Value))
                        return Extrinsic::RHI::ComputeWaitStatus::Complete;
                }
                std::this_thread::sleep_for(std::chrono::microseconds(100));
                return Extrinsic::RHI::ComputeWaitStatus::Pending;
            };
            Device.TransferQueue.ReadbackComplete = [this](Extrinsic::RHI::ReadbackToken token)
            {
                std::scoped_lock lock{Gpu};
                return !token.IsValid() || CompletedTokens.contains(token.Value);
            };
            Device.TransferQueue.TakeReadback =
                [this](Extrinsic::RHI::ReadbackToken token, std::span<std::byte> destination)
            {
                std::scoped_lock lock{Gpu};
                std::ranges::fill(destination, std::byte{0});
                const auto found = PulledBytes.find(token.Value);
                if (found == PulledBytes.end())
                    return Stopping;
                std::memcpy(
                    destination.data(), found->second.data(), std::min(destination.size(), found->second.size()));
                PulledBytes.erase(found);
                return true;
            };
            Device.ComputeReadback = [this](auto record, auto bytes, auto sink)
            {
                std::scoped_lock lock{Gpu};
                const auto copies = Device.CommandContext.CopyBufferRecords.size();
                EXPECT_TRUE(record(Device.CommandContext).IsValid());
                // An Upload page (it seeds both ping-pong buffers) begins a generation: the device counts afresh.
                if (bytes == 64u && Device.CommandContext.CopyBufferRecords.size() == copies + 2u)
                    Finalized = 0u;
                if (HoldCompletion)
                {
                    PendingCompletion = std::move(sink);
                    PendingToken = ++Tokens;
                    return Extrinsic::RHI::ReadbackToken{PendingToken};
                }
                if (bytes == 0u)
                    return Defer(std::move(sink), {});
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
                            data[1] = ++Finalized; // the mock device's own count (the worker records this page)
                            data[9] = 1u;
                            data[10] = data[1];
                            data[2] = EarlyConvergence;
                            if (EarlyConvergence || data[1] == Request.Config.Execution.IterationCap)
                            {
                                data[3] = 0u;
                                data[0] = EarlyConvergence ? 0u : 6u;
                            }
                        }
                        return Defer(std::move(sink), std::as_bytes(std::span(data)));
                    }
                }
                else
                {
                    EXPECT_EQ(bytes, Accepted.size() * sizeof(glm::vec3));
                    return Defer(std::move(sink), std::as_bytes(std::span(Accepted)));
                }
                return Extrinsic::RHI::ReadbackToken{++Tokens};
            };
        }
        void TearDown() override
        {
            if (Service)
            {
                (void)Service->GpuRun(Correlation, Runtime::PointCloudConsolidationGpuAction::Discard);
                Service->Unsubscribe(Subscription);
            }
            {
                std::scoped_lock lock{Gpu};
                Stopping = true; // a worker still waiting for its page returns
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
            Drain();
            Frame(advanceFrame);
        }
        // A Tick's first half: device completions, commands and the job drain (step-run pumps, Show).
        void Drain()
        {
            std::vector<std::function<void()>> completions;
            {
                std::scoped_lock lock{Gpu};
                completions = std::exchange(ComputeCompletions, {});
            }
            for (auto& complete : completions) complete();
            Commands.Drain(Scene(), {.Events = &Events, .Jobs = &Jobs.Jobs(), .Worlds = &Worlds});
            (void)Jobs.Jobs().DrainCompletions(Events);
            (void)Events.Pump();
        }
        // Ticks until `holds` right after a drain, before that Tick's frame callbacks (the owner's maintenance, which
        // offers the next live preview a free ring slot): what a reader sees between a publication and the next offer.
        template <class Predicate> bool UntilBeforeFrame(Predicate holds)
        {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (std::chrono::steady_clock::now() < deadline)
            {
                Drain();
                if (holds())
                    return true;
                Frame(true);
                std::this_thread::yield();
            }
            return false;
        }
        // A Tick's second half: the GPU-queue participants' frame commands, the frame and the maintenance hook.
        void Frame(bool advanceFrame)
        {
            Jobs.Jobs().RecordGpuQueueFrameCommands(Device.CommandContext);
            if (advanceFrame) ++Device.GlobalFrameNumber;
            (void)Jobs.Jobs().DrainGpuQueueCompletedTransfers();
            Cache.Prune(); // the Maintenance hook: returned method scratch is stamped and matures
        }
        template<class Predicate> bool Until(Predicate predicate)
        {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (!predicate() && std::chrono::steady_clock::now() < deadline)
            { Tick(); std::this_thread::yield(); }
            return predicate();
        }
        auto Observation() { return Service->GpuRun(Correlation); }
        void Start()
        {
            {
                std::scoped_lock lock{Gpu};
                Finalized = 0u;
            }
            Correlation = Service->Run(Request);
        }
        // ARCH-023: the in-place run's step run (run_control and run_observe act on it).
        Runtime::StepRunObservation Run(const std::uint64_t id)
        {
            return *StepRuns.Observe(id);
        }
        bool Control(const std::uint64_t id, const Runtime::StepRunAction action, const std::uint32_t count = 1u)
        {
            std::string refusal;
            return StepRuns.Control(id, action, count, refusal);
        }
        bool RunPaused(const std::uint64_t id, const std::uint32_t steps)
        {
            return Until(
                [&]
                {
                    const auto run = Run(id);
                    return run.Steps == steps && run.Phase == Runtime::StepRunPhase::Paused && !run.SegmentInFlight;
                });
        }
        const std::string& PipelinePath(Extrinsic::RHI::PipelineHandle handle) const
        {
            static const std::string none;
            const auto found = std::find(Device.CreatedPipelineHandles.begin(), Device.CreatedPipelineHandles.end(), handle);
            return found == Device.CreatedPipelineHandles.end() ? none
                : Device.CreatedPipelineDescs[found - Device.CreatedPipelineHandles.begin()].ComputeShaderPath;
        }
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
    ASSERT_TRUE(HasHeld());
    std::array<std::uint32_t, 16> grid{};
    grid[3] = 1u;
    grid[11] = 2u * Before.size();
    HoldCompletion = false;
    ReleaseHeld(std::as_bytes(std::span(grid)));
    ASSERT_TRUE(Until([&] { return DiagnosticSink.has_value(); }));
    EXPECT_EQ(Observation().Previews, 1u); // Terminal copy is still in flight.
    EXPECT_EQ(std::count_if(Device.TransferQueue.BufferUploads.begin(), Device.TransferQueue.BufferUploads.end(),
        [&](const auto& upload) { return upload.Data.size() == Before.size() * 12u; }), 1);
    EXPECT_EQ(std::count_if(Device.TransferQueue.BufferUploads.begin(), Device.TransferQueue.BufferUploads.end(),
        [&](const auto& upload) { return upload.Data.size() == Before.size() * 16u; }), 0)
        << "LOP must not upload privately packed positions";
    EXPECT_GT(Observation().Submissions, Request.Config.Execution.IterationCap);
    EXPECT_EQ(std::as_const(Properties()).Get<glm::vec3>("v:position").Vector(), Before);
    CompleteDiagnostics();
    ASSERT_TRUE(Until([&] { return Observation().ReadyToAccept; }));
    EXPECT_EQ(Observation().Previews, 2u);
    Runtime::EditorProcessingContext previewContext{.Scene = &Scene(), .World = World, .SpatialIndices = &Cache};
    const auto previewCommands = Runtime::BindEditorProcessingCommands(previewContext);
    const auto blocked = Runtime::PrepareEditorPointCloudConsolidationAvailability(previewCommands, Service, Request);
    EXPECT_FALSE(blocked.Available);
    EXPECT_NE(blocked.Message.find("Accept or Discard"), std::string::npos);
    EXPECT_EQ(Runtime::SubmitEditorPointCloudConsolidation(previewCommands, Service, Request).Message, blocked.Message);
    auto otherOutput = Request;
    otherOutput.Properties.OutputPositions.Name = "other_positions";
    EXPECT_TRUE(Service->GpuBusy());
    EXPECT_EQ(Runtime::PrepareEditorPointCloudConsolidationAvailability(previewCommands, Service, otherOutput).Message, blocked.Message);
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

TEST_F(ResidentLop, PendingLopKeepsUnrelatedWlopCpuFallbackAvailable)
{
    Start();
    ASSERT_TRUE(Until([&] { return DiagnosticSink.has_value(); }));
    CompleteDiagnostics();
    ASSERT_TRUE(Until([&] { return Observation().ReadyToAccept; }));
    auto fallback = Request;
    const auto independentEntity = Scene().Create();
    auto& independentVertices = Scene().Raw().emplace<GS::Vertices>(independentEntity);
    independentVertices.Properties.Resize(Before.size());
    independentVertices.Properties.GetOrAdd<glm::vec3>("v:position").Vector() = Before;
    fallback.StableEntityId = Runtime::SelectionController::ToStableEntityId(independentEntity);
    fallback.Config.Strategy = Runtime::PointCloudConsolidationStrategy::Wlop;
    fallback.Config.WlopAnisotropic = false;
    fallback.Config.Execution.AutoAccept = Runtime::StepRunAutoAccept::End; // the CPU fallback is a batch run here
    fallback.Properties.OutputPositions.Name = "wlop_positions";
    Runtime::EditorProcessingContext context{.Scene = &Scene(), .World = World, .SpatialIndices = &Cache};
    const auto commands = Runtime::BindEditorProcessingCommands(context);
    Runtime::PointCloudConsolidationAvailability available;
    ASSERT_TRUE(Until([&] {
        available = Runtime::PrepareEditorPointCloudConsolidationAvailability(commands, Service, fallback);
        return available.Available;
    })) << available.Message;
    const auto queued = Runtime::SubmitEditorPointCloudConsolidation(commands, Service, fallback);
    ASSERT_EQ(queued.Status, Runtime::PointCloudConsolidationRunStatus::Queued) << queued.Message;
    ASSERT_TRUE(Until([&] { return !Results.empty(); }));
    EXPECT_EQ(Results.back().Correlation, queued.Correlation);
    EXPECT_TRUE(Results.back().Succeeded()) << Results.back().Message;
    EXPECT_EQ(Results.back().RequestedBackend, Runtime::PointCloudConsolidationBackend::VulkanCompute);
    EXPECT_EQ(Results.back().ActualBackend, Runtime::PointCloudConsolidationBackend::CpuReference);
    EXPECT_TRUE(Observation().ReadyToAccept);
}

TEST_F(ResidentLop, PreviewCopiesOnlyAtIntervalAndTerminalBoundaries)
{
    Request.Config.Execution.IterationCap = 10u;
    Request.Config.Execution.PreviewEvery = 5u;
    Start();
    ASSERT_TRUE(Until([&] { return DiagnosticSink.has_value(); }));
    EXPECT_EQ(Observation().Previews, 1u); // Terminal copy is still in flight.
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
    CompleteDiagnostics();
    ASSERT_TRUE(Until([&] { return Observation().ReadyToAccept; }));
    EXPECT_EQ(Observation().Previews, 2u);
}

TEST_F(ResidentLop, BoundaryPublishesCopiedBackOnlyAfterCompletion)
{
    // ARCH-023: a live preview is its own completion-only copy (never fused into an iteration page) into the slot the
    // owner offered; Show publishes it only once that copy completed. The run steps to the preview boundary and rests
    // there, so the held copy is the run's last submission.
    auto* residency = Cache.PropertyResidency();
    ASSERT_NE(residency, nullptr);
    const auto key = Runtime::MakeGpuPropertyKey(World, Entity, Request.Properties.OutputPositions);
    Extrinsic::RHI::BufferHandle copiedBack{}, copiedSource{}, input{};
    std::uint64_t copyBytes = ~0ull;
    const auto submit = Device.ComputeReadback;
    Device.ComputeReadback = [&](auto record, auto bytes, auto sink)
    {
        return submit(
            [&](auto& commands)
            {
                const auto before = Device.CommandContext.CopyBufferRecords.size();
                const auto output = record(commands);
                const auto& copies = Device.CommandContext.CopyBufferRecords;
                if (!copiedBack.IsValid() && before >= 2u && copies.size() > before)
                {
                    HoldCompletion = true;
                    EXPECT_EQ(copies.size(), before + 1u) << "a copy of its own";
                    copyBytes = bytes;
                    input = copies[0].Src;
                    copiedBack = copies.back().Dst;
                    copiedSource = copies.back().Src;
                }
                return output;
            },
            bytes,
            std::move(sink));
    };
    Request.Config.Execution.Start = Runtime::StepRunStart::Ready;
    Start();
    ASSERT_TRUE(Until(
        [&]
        {
            return Observation().StepRun != 0u;
        }));
    const auto id = Observation().StepRun;
    ASSERT_TRUE(Control(id, Runtime::StepRunAction::Step, Request.Config.Execution.PreviewEvery));
    ASSERT_TRUE(RunPaused(id, Request.Config.Execution.PreviewEvery));
    ASSERT_TRUE(Until(
        [&]
        {
            return HasHeld();
        }));
    HoldCompletion = false;
    ASSERT_TRUE(copiedBack.IsValid());
    EXPECT_EQ(copyBytes, 0u) << "the copy reads nothing back";
    // Upload seeds B, then A; iteration 2 completes in A.
    const auto& copies = Device.CommandContext.CopyBufferRecords;
    EXPECT_EQ(copiedSource, copies[1].Dst);
    EXPECT_NE(copiedBack, input);
    EXPECT_NE(copiedBack, copies[0].Dst);
    EXPECT_NE(copiedBack, copies[1].Dst);
    ASSERT_TRUE(residency->Front(key));
    EXPECT_EQ(Observation().Iterations, Request.Config.Execution.PreviewEvery);
    EXPECT_EQ(Observation().Previews, 0u);
    EXPECT_EQ(residency->Front(key)->Buffer, input)
        << "Iteration count and a ring alone do not prove preview publication";
    for (unsigned i = 0; i < 4u; ++i) Tick();
    EXPECT_EQ(Observation().Previews, 0u);
    EXPECT_EQ(residency->Front(key)->Buffer, input);
    ReleaseHeld({});
    ASSERT_TRUE(Until([&] { return Observation().Previews == 1u; }));
    ASSERT_TRUE(residency->Front(key));
    EXPECT_EQ(residency->Front(key)->Buffer, copiedBack);
    EXPECT_NE(residency->Front(key)->Publication, 0u);
    EXPECT_EQ(Observation().Iterations, Request.Config.Execution.PreviewEvery);
}

TEST_F(ResidentLop, BatchesPagesWithinCandidateBudgetAndCoversReductionTail)
{
    struct RestoreLimits
    {
        Runtime::LopPagingLimits Saved{Runtime::LopPagingForTesting};
        ~RestoreLimits() { Runtime::LopPagingForTesting = Saved; }
    } restore;
    Runtime::LopPagingForTesting = {.PagePairs = 150u, .SubmissionPairs = 512u, .ReduceRows = 7u};
    Request.Config.Execution.IterationCap = 2u;
    std::vector<std::uint32_t> initialized, projected, reduced, finalizedAfterRow;
    bool batched = false;
    const auto submit = Device.ComputeReadback;
    Device.ComputeReadback = [&](auto record, auto bytes, auto sink) {
        return submit([&](auto& commands) {
            const auto begin = Device.CommandContext.PushConstantPayloads.size();
            const auto bindBegin = Device.CommandContext.BoundPipelines.size();
            const auto output = record(commands);
            const auto& pushes = Device.CommandContext.PushConstantPayloads;
            const auto& binds = Device.CommandContext.BoundPipelines;
            // Grid pages also record scan pushes; they carry no row-paged dispatch.
            if (pushes.size() - begin != binds.size() - bindBegin) return output;
            std::uint32_t rows = 0u, rowDispatches = 0u;
            bool finalized = false;
            for (auto i = begin; i < pushes.size(); ++i)
            {
                const auto& path = PipelinePath(binds[bindBegin + (i - begin)]);
                if (path.find("lop_iteration_finalize") != std::string::npos)
                {
                    finalized = true;
                    finalizedAfterRow.push_back(projected.empty() ? ~0u : projected.back());
                    EXPECT_EQ(bytes, 64u) << "The finalizing page reads back the diagnostics";
                    continue;
                }
                const bool initialize = path.find("lop_initialize") != std::string::npos;
                const bool project = path.find("lop_project") != std::string::npos;
                const bool reduce = path.find("lop_final_reduce") != std::string::npos;
                if (!initialize && !project && !reduce) continue;
                EXPECT_FALSE(finalized) << "Finalize closes the last projection page";
                EXPECT_EQ(pushes[i].size(), 88u);
                std::uint32_t first{}, count{};
                std::memcpy(&first, pushes[i].data() + 52u, sizeof(first));
                std::memcpy(&count, pushes[i].data() + 80u, sizeof(count));
                EXPECT_LE(count, reduce ? 7u : 3u);
                auto& covered = initialize ? initialized : project ? projected : reduced;
                for (auto row = first; row < first + count; ++row) covered.push_back(row);
                if (!reduce)
                {
                    rows += count;
                    ++rowDispatches;
                }
            }
            if (rowDispatches > 1u) batched = true;
            EXPECT_LE(rows * 50u, 512u);
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
    ASSERT_EQ(finalizedAfterRow.size(), Request.Config.Execution.IterationCap);
    for (const auto row : finalizedAfterRow) EXPECT_EQ(row, Before.size() - 1u);
    CompleteDiagnostics();
    ASSERT_TRUE(Until([&] { return Observation().CanAccept; }));
}

TEST_F(ResidentLop, RepeatRunsRetainScratchCapacityAndPipelines)
{
    // ARCH-023: LOP creates all of its buffers in its owner Init (after Start, before the first page); no page
    // recording (the worker's or the owner's) and no shared ring/residency allocates inside one.
    struct RunShape
    {
        int UploadCreates{-1}, LaterPageCreates{0}, PipelineCreates{0};
        Extrinsic::RHI::BufferHandle SeedB{}, SeedA{};
        std::uint32_t Submissions{}, Previews{};
    } shape;
    std::atomic<int> afterStart{0}; // buffers created once Start returned (the Init's follow)
    // ARCH-023: pages are recorded on the worker (and terminal ones on the owner) while the owner may offer the live
    // preview a residency ring slot (created per run, never pooled), so the shared creation counter is kept as index
    // ranges and classified once the run ended: ring slots are the residency's, not the method's scratch.
    std::mutex pagesMutex;
    int firstPage{-1}; // CreateBufferCount when the first (Upload) page was recorded
    std::vector<std::pair<int, int>> pageCreates; // [before, after) of every page recording
    const auto submit = Device.ComputeReadback;
    Device.ComputeReadback = [&](auto record, auto bytes, auto sink) {
        return submit([&](auto& commands) {
            const int created = Device.CreateBufferCount;
            const auto copies = Device.CommandContext.CopyBufferRecords.size();
            const auto output = record(commands);
            const int after = Device.CreateBufferCount;
            std::scoped_lock lock{pagesMutex};
            pageCreates.emplace_back(created, after);
            if (firstPage < 0)
            {
                firstPage = created;
                const auto& records = Device.CommandContext.CopyBufferRecords;
                EXPECT_EQ(records.size(), copies + 2u) << "Upload seeds both ping-pong buffers";
                if (records.size() == copies + 2u)
                {
                    shape.SeedB = records[copies].Dst;
                    shape.SeedA = records[copies + 1u].Dst;
                }
            }
            return output;
        }, bytes, std::move(sink));
    };
    // Main thread, once no page records: the method's buffers created in [from, to), residency ring slots excluded.
    const auto methodCreates = [&](const int from, const int to)
    {
        const auto& descs = Device.CreatedBufferDescs;
        return int(std::count_if(descs.begin() + from,
            descs.begin() + to,
            [](const auto& desc)
            {
                return !desc.DebugName || std::string_view{desc.DebugName} != "GpuPropertyResidency.Ring";
            }));
    };
    const auto run = [&](ECS::EntityHandle entity, std::size_t points) {
        shape = {};
        {
            std::scoped_lock lock{pagesMutex};
            firstPage = -1;
            pageCreates.clear();
        }
        const int pipelines = Device.CreatePipelineCount;
        const auto completed = Results.size();
        Request.StableEntityId = Runtime::SelectionController::ToStableEntityId(entity);
        Start();
        afterStart = Device.CreateBufferCount;
        EXPECT_TRUE(Until([&] { return DiagnosticSink.has_value(); }));
        shape.Submissions = Observation().Submissions;
        CompleteDiagnostics();
        EXPECT_TRUE(Until([&] { return Observation().ReadyToAccept; }));
        shape.Previews = Observation().Previews;
        (void)Service->GpuRun(Correlation, Runtime::PointCloudConsolidationGpuAction::Discard);
        EXPECT_TRUE(Until([&] { return Results.size() == completed + 1u; }));
        EXPECT_EQ(Results.back().Status, Runtime::PointCloudConsolidationRunStatus::Cancelled);
        EXPECT_EQ(Results.back().InputPointCount, points);
        // ARCH-023: the run's buffers went back to the method scratch cache; the next run reuses them once retired.
        EXPECT_TRUE(Until(
            [&]
            {
                const auto s = Cache.Stats();
                return s.ScratchActiveBytes == 0u && s.ScratchPendingBytes == 0u;
            }));
        shape.PipelineCreates = Device.CreatePipelineCount - pipelines;
        {
            std::scoped_lock lock{pagesMutex};
            EXPECT_GE(firstPage, afterStart.load()) << "the Init precedes the first page";
            shape.UploadCreates = firstPage < 0 ? -1 : methodCreates(afterStart.load(), firstPage); // the Init's
            for (const auto& [before, after] : pageCreates)
                shape.LaterPageCreates += methodCreates(before, after);
        }
        EXPECT_EQ(shape.LaterPageCreates, 0);
        return shape;
    };

    std::vector<glm::vec3> fewer = Before;
    fewer.resize(15u); // three of the five rows: a smaller cell grid and fewer rows
    const auto smaller = AddPointCloud(Scene(), fewer);

    const RunShape cold = run(smaller, fewer.size());
    EXPECT_GT(cold.UploadCreates, 0);
    EXPECT_GT(cold.PipelineCreates, 0);

    const RunShape grown = run(Entity, Before.size());
    EXPECT_GT(grown.UploadCreates, 0) << "Row and cell capacity must grow";
    EXPECT_LT(grown.UploadCreates, cold.UploadCreates) << "Fixed-size state and diagnostics stay retained";
    EXPECT_NE(grown.SeedA, cold.SeedA);
    EXPECT_EQ(grown.PipelineCreates, 0);

    // The cache, not the module, picks which retired buffer of a size class serves a role.
    const RunShape warm = run(Entity, Before.size());
    EXPECT_EQ(warm.UploadCreates, 0);
    EXPECT_EQ(warm.PipelineCreates, 0);
    // Upload + Initialize + four iterations of (Grid, Project+Finalize) + Reduce, and the live preview's copy: the
    // terminal front rides the Reduce page, while the live preview is the worker's own copy into the slot the owner
    // offered (ARCH-023: never waited for, so it cannot ride the next page, which the worker records itself).
    EXPECT_EQ(warm.Previews, 2u);
    EXPECT_EQ(warm.Submissions, 12u);

    const RunShape shrunk = run(smaller, fewer.size());
    EXPECT_EQ(shrunk.UploadCreates, 0) << "A smaller run reuses the retired buffers of its size classes";
    EXPECT_EQ(shrunk.PipelineCreates, 0);
    EXPECT_EQ(Service->Stats().GpuFallbacks, 0u);
}

TEST_F(ResidentLop, TwoIterationLopIgnoresUnusedNormalRefinementBudget)
{
    Request.Config.SupportRadius = 0.5;
    Request.Config.TargetPointCount = 0u;
    Request.Config.Seed = 42u;
    Request.Config.Execution.IterationCap = 2u;
    Request.Config.NormalRefinementRounds = 3u;
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
    Request.Config.Execution.AutoAccept = Runtime::StepRunAutoAccept::End;
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
    Request.Config.Execution.IterationCap = 9u;
    Request.Config.Execution.PreviewEvery = 4u;
    Start();
    ASSERT_TRUE(Until([&] { return Observation().Iterations >= 1u; }));
    (void)Service->GpuRun(Correlation, Runtime::PointCloudConsolidationGpuAction::Stop);
    ASSERT_TRUE(Until([&] { return DiagnosticSink.has_value(); }));
    EXPECT_LT(Observation().Iterations, Request.Config.Execution.IterationCap);
    CompleteDiagnostics();
    ASSERT_TRUE(Until([&] { return Observation().CanAccept; }));
    EXPECT_GE(Observation().Previews, 1u);
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
    EXPECT_EQ(Observation().Previews, 0u);
    CompleteDiagnostics();
    ASSERT_TRUE(Until([&] { return Observation().CanAccept; }));
    EXPECT_EQ(Observation().Previews, 1u);
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
    ASSERT_TRUE(Until(
        [&]
        {
            return HasHeld();
        }));
    ASSERT_TRUE(Observation().Accepting);
    (void)Service->GpuRun(Correlation, Runtime::PointCloudConsolidationGpuAction::Discard);
    ASSERT_TRUE(Until([&] { return Results.size() == 1u; }));
    EXPECT_EQ(Results.back().Status, Runtime::PointCloudConsolidationRunStatus::Cancelled);
    ReleaseHeld(std::as_bytes(std::span(Accepted)));
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
    ASSERT_TRUE(Until(
        [&]
        {
            return HasHeld();
        }));
    Properties().Get<glm::vec3>("v:position")[0].x += 0.25f;
    const auto edited = std::as_const(Properties()).Get<glm::vec3>("v:position").Vector();
    ASSERT_TRUE(Until([&] { return Results.size() == 1u; }));
    EXPECT_EQ(Results.back().Status, Runtime::PointCloudConsolidationRunStatus::StaleSource) << Results.back().Message;
    ReleaseHeld(std::as_bytes(std::span(Accepted)));
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
    Request.Config.Execution.AutoAccept = Runtime::StepRunAutoAccept::End;
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
    Request.Config.Execution.AutoAccept = Runtime::StepRunAutoAccept::End;
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
    engine.EmplaceModule<Runtime::StepRunsModule>(); // every consolidation run is a step run
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
    // Every observed result, by correlation: a run completes once (a second publication seen here is counted too).
    std::map<std::uint64_t, int> results;
    const auto counter = engine.Events().Subscribe<Runtime::PointCloudConsolidationResult>(
        [&](const Runtime::PointCloudConsolidationResult& result) {
            if (result.Correlation.Value != 0) ++results[result.Correlation.Value];
        });
    // The listeners of one probe event. A pump delivers only what was queued at its entry, so what listeners or
    // workers published during the last one is delivered first (results reach the bus through the main thread's
    // completion drain, which the probe does not run), so the probe's pump holds the probe alone.
    std::uint64_t probeBatch = 0;
    const auto listenerCalls = [&] {
        for (int i = 0; i < 8 && engine.Events().Pump() != 0; ++i) {}
        const auto before = engine.Events().Stats().ListenerInvocations;
        engine.Events().Publish(Runtime::PointCloudConsolidationResult{});
        (void)engine.Events().Pump();
        probeBatch = engine.Events().Stats().LastPumpEvents;
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
        EXPECT_EQ(listenerCalls(), attachedListeners + 1u) << "the probe's pump delivered " << probeBatch << " events";
        Runtime::AgentOperationOutcome completed;
        if (std::string_view(mode) == "detach")
        {
            attachment.Detach();
            EXPECT_TRUE(outcome.Continuation(context, completed));
            EXPECT_TRUE(completed.IsError);
            EXPECT_EQ(listenerCalls(), detachedListeners) << "the probe's pump delivered " << probeBatch << " events";
        }
        else if (std::string_view(mode) == "cancel")
        {
            outcome.Continuation = {};
            EXPECT_EQ(listenerCalls(), attachedListeners) << "the probe's pump delivered " << probeBatch << " events";
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
            EXPECT_EQ(listenerCalls(), attachedListeners) << "the probe's pump delivered " << probeBatch << " events";
            ASSERT_FALSE(results.empty());
            EXPECT_EQ(results.rbegin()->second, 1) << "the completed run's result arrives once";
        }
        outcome.Continuation = {};
        attachment.Detach();
    }
    (void)listenerCalls(); // deliver what is still queued
    for (const auto& [correlation, count] : results)
        EXPECT_EQ(count, 1) << "run " << correlation << " was seen published more than once";
    engine.Events().Unsubscribe(counter);
    engine.Shutdown();
}

// ARCH-023: an in-place LOP run is a step run. A step is a whole iteration however many pages it takes: Step n records
// exactly n iterations and nothing beyond (a paused run submits nothing), and pausing and stepping record exactly the
// dispatches of an uninterrupted run (live preview off, so no rest preview page can differ).
TEST_F(ResidentLop, StepsAreWholeIterationsAndAPausedRunRecordsTheUninterruptedDispatches)
{
    Request.Config.Execution.IterationCap = 6u;
    Request.Config.Execution.PreviewEvery = 0u;
    // Per page: its readback bytes and every LOP push constant without the state address.
    std::vector<std::pair<std::size_t, std::vector<std::vector<std::byte>>>> trace;
    const auto submit = Device.ComputeReadback;
    Device.ComputeReadback = [&](auto record, auto bytes, auto sink)
    {
        return submit(
            [&](auto& commands)
            {
                const auto begin = Device.CommandContext.PushConstantPayloads.size();
                const auto output = record(commands);
                auto& page = trace.emplace_back(bytes, std::vector<std::vector<std::byte>>{}).second;
                const auto& pushes = Device.CommandContext.PushConstantPayloads;
                for (auto i = begin; i < pushes.size(); ++i)
                    if (pushes[i].size() == 88u)
                        page.emplace_back(pushes[i].begin() + 8, pushes[i].end());
                return output;
            },
            bytes,
            std::move(sink));
    };
    Start();
    ASSERT_TRUE(Until(
        [&]
        {
            return DiagnosticSink.has_value();
        }));
    CompleteDiagnostics();
    ASSERT_TRUE(Until(
        [&]
        {
            return Observation().ReadyToAccept;
        }));
    const auto id = Observation().StepRun;
    ASSERT_NE(id, 0u);
    EXPECT_EQ(Run(id).Steps, 6u);
    EXPECT_EQ(Run(id).End, Runtime::StepRunEnd::IterationCap);
    const auto uninterrupted = std::exchange(trace, {});
    ASSERT_FALSE(uninterrupted.empty());
    (void)Service->GpuRun(Correlation, Runtime::PointCloudConsolidationGpuAction::Discard);
    ASSERT_TRUE(Until(
        [&]
        {
            return Results.size() == 1u;
        }));

    Start();
    ASSERT_TRUE(Until(
        [&]
        {
            return Observation().StepRun != 0u;
        }));
    const auto stepped = Observation().StepRun;
    ASSERT_TRUE(Control(stepped, Runtime::StepRunAction::Pause));
    ASSERT_TRUE(Until(
        [&]
        {
            return Run(stepped).Phase == Runtime::StepRunPhase::Paused && !Run(stepped).SegmentInFlight;
        }));
    std::uint32_t steps = Run(stepped).Steps;
    ASSERT_LE(steps, 2u) << "the pause lands at the first boundary it reaches";
    for (const std::uint32_t n : {1u, 2u})
    {
        ASSERT_TRUE(Control(stepped, Runtime::StepRunAction::Step, n));
        steps += n;
        ASSERT_TRUE(RunPaused(stepped, steps));
        const auto submissions = Observation().Submissions;
        for (unsigned i = 0u; i < 10u; ++i)
            Tick();
        EXPECT_EQ(Run(stepped).Steps, steps) << "no step beyond the n asked for";
        EXPECT_EQ(Observation().Iterations, steps) << "a logical step is one iteration";
        EXPECT_EQ(Observation().Submissions, submissions) << "a paused run records nothing";
    }
    ASSERT_TRUE(Control(stepped, Runtime::StepRunAction::Run));
    ASSERT_TRUE(Until(
        [&]
        {
            return DiagnosticSink.has_value();
        }));
    CompleteDiagnostics();
    ASSERT_TRUE(Until(
        [&]
        {
            return Observation().ReadyToAccept;
        }));
    EXPECT_EQ(Observation().Iterations, 6u);
    EXPECT_EQ(Run(stepped).End, Runtime::StepRunEnd::IterationCap);
    EXPECT_EQ(trace, uninterrupted);
}

// Live preview off: no intermediate copy (the two ping-pong seeds and the terminal front only), the same terminal
// result and one undoable Accept.
TEST_F(ResidentLop, PreviewOffCopiesOnlyTheTerminalFrontAndAcceptsTheSameResult)
{
    Request.Config.Execution.IterationCap = 6u;
    Request.Config.Execution.PreviewEvery = 0u;
    Start();
    ASSERT_TRUE(Until(
        [&]
        {
            return DiagnosticSink.has_value();
        }));
    EXPECT_EQ(Device.CommandContext.CopyBufferRecords.size(), 3u) << "seeds and the terminal front, no live preview";
    CompleteDiagnostics();
    ASSERT_TRUE(Until(
        [&]
        {
            return Observation().CanAccept;
        }));
    EXPECT_EQ(Observation().Previews, 1u) << "the terminal front is the result, not a preview";
    EXPECT_EQ(Observation().Iterations, 6u);
    const auto undo = History.UndoCount();
    (void)Service->GpuRun(Correlation, Runtime::PointCloudConsolidationGpuAction::Accept);
    ASSERT_TRUE(Until(
        [&]
        {
            return Results.size() == 1u;
        }));
    EXPECT_TRUE(Results.back().Succeeded()) << Results.back().Message;
    EXPECT_EQ(Results.back().Iterations, 6u);
    EXPECT_EQ(std::as_const(Properties()).Get<glm::vec3>("v:position").Vector(), Accepted);
    EXPECT_EQ(History.UndoCount(), undo + 1u);
}

// A paused run shows its boundary once (a copy-only page, off the preview cadence), then records nothing; Accept
// from the rest publishes that iteration's positions as a stopped, unconverged result in one undo entry.
TEST_F(ResidentLop, APausedRunShowsItsBoundaryOnceAndAcceptsThatIteration)
{
    Request.Config.Execution.IterationCap = 9u;
    Request.Config.Execution.PreviewEvery = 4u;
    Start();
    ASSERT_TRUE(Until(
        [&]
        {
            return Observation().StepRun != 0u;
        }));
    const auto id = Observation().StepRun;
    ASSERT_TRUE(Control(id, Runtime::StepRunAction::Pause));
    ASSERT_TRUE(Until(
        [&]
        {
            return Run(id).Phase == Runtime::StepRunPhase::Paused && !Run(id).SegmentInFlight;
        }));
    if (Run(id).Steps == 0u)
    {
        ASSERT_TRUE(Control(id, Runtime::StepRunAction::Step, 1u));
        ASSERT_TRUE(RunPaused(id, 1u));
    }
    const auto steps = Run(id).Steps;
    ASSERT_LT(steps, 4u);
    ASSERT_TRUE(Until(
        [&]
        {
            return Observation().Previews == 1u && Observation().CanAccept;
        }));
    const auto submissions = Observation().Submissions;
    for (unsigned i = 0u; i < 10u; ++i)
        Tick();
    EXPECT_EQ(Observation().Submissions, submissions);
    EXPECT_EQ(Observation().Previews, 1u) << "one rest preview";
    EXPECT_EQ(std::as_const(Properties()).Get<glm::vec3>("v:position").Vector(), Before);
    const auto undo = History.UndoCount();
    ASSERT_TRUE(Control(id, Runtime::StepRunAction::Accept));
    EXPECT_FALSE(Control(id, Runtime::StepRunAction::Accept)) << "one Accept";
    ASSERT_TRUE(Until(
        [&]
        {
            return DiagnosticSink.has_value();
        }));
    CompleteDiagnostics();
    ASSERT_TRUE(Until(
        [&]
        {
            return Results.size() == 1u;
        }));
    EXPECT_EQ(Results.back().Status, Runtime::PointCloudConsolidationRunStatus::Applied) << Results.back().Message;
    EXPECT_EQ(Results.back().Iterations, steps);
    EXPECT_FALSE(Results.back().Converged);
    EXPECT_EQ(Results.back().GeometryStatus, Geometry::PointCloud::Consolidation::Status::NotConverged);
    EXPECT_EQ(std::as_const(Properties()).Get<glm::vec3>("v:position").Vector(), Accepted);
    EXPECT_EQ(History.UndoCount(), undo + 1u);
    ASSERT_TRUE(Until(
        [&]
        {
            return Run(id).Phase == Runtime::StepRunPhase::Committed;
        }));
    for (unsigned i = 0u; i < 10u; ++i)
        Tick();
    EXPECT_EQ(Results.size(), 1u);
}

// run_control's Accept of a result whose inputs changed publishes nothing and fails the run; its Discard while a page
// is in flight ends the run unpublished exactly once, after that page completed.
TEST_F(ResidentLop, RunControlStaleAcceptAndInFlightDiscardPublishNothing)
{
    Start();
    ASSERT_TRUE(Until(
        [&]
        {
            return DiagnosticSink.has_value();
        }));
    CompleteDiagnostics();
    ASSERT_TRUE(Until(
        [&]
        {
            return Observation().CanAccept;
        }));
    const auto stale = Observation().StepRun;
    Properties().Get<glm::vec3>("v:position")[0].x += 0.25f;
    const auto edited = std::as_const(Properties()).Get<glm::vec3>("v:position").Vector();
    const auto undo = History.UndoCount();
    ASSERT_TRUE(Control(stale, Runtime::StepRunAction::Accept));
    ASSERT_TRUE(Until(
        [&]
        {
            return Results.size() == 1u;
        }));
    EXPECT_EQ(Results.back().Status, Runtime::PointCloudConsolidationRunStatus::StaleSource);
    EXPECT_EQ(std::as_const(Properties()).Get<glm::vec3>("v:position").Vector(), edited);
    EXPECT_EQ(History.UndoCount(), undo);
    ASSERT_TRUE(Until(
        [&]
        {
            return Run(stale).Phase == Runtime::StepRunPhase::Failed;
        }));

    HoldCompletion = true;
    Start();
    ASSERT_TRUE(Until(
        [&]
        {
            return HasHeld();
        }));
    const auto discarded = Observation().StepRun;
    ASSERT_NE(discarded, 0u);
    ASSERT_TRUE(Control(discarded, Runtime::StepRunAction::Discard));
    EXPECT_EQ(Run(discarded).Phase, Runtime::StepRunPhase::Discarded) << "terminal at once";
    for (unsigned i = 0u; i < 5u; ++i)
        Tick();
    EXPECT_EQ(Results.size(), 1u) << "the run ends once its page completed";
    EXPECT_TRUE(Run(discarded).SegmentInFlight) << "the page holds the run";
    HoldCompletion = false;
    std::array<std::uint32_t, 16> grid{};
    grid[3] = 1u;
    grid[11] = 2u * Before.size();
    ReleaseHeld(std::as_bytes(std::span(grid)));
    ASSERT_TRUE(Until(
        [&]
        {
            return Results.size() == 2u;
        }));
    EXPECT_EQ(Results.back().Status, Runtime::PointCloudConsolidationRunStatus::Cancelled);
    EXPECT_FALSE(Run(discarded).SegmentInFlight);
    for (unsigned i = 0u; i < 10u; ++i)
        Tick();
    EXPECT_EQ(Results.size(), 2u);
    EXPECT_EQ(std::as_const(Properties()).Get<glm::vec3>("v:position").Vector(), edited);
    EXPECT_EQ(History.UndoCount(), undo);
}

namespace
{
    // Every LOP page's readback bytes and push constants (without the state address), as the step test records them.
    // The interposer shares the trace's ownership: a worker page recorded after a test returned early (a failed
    // assertion, before its teardown stopped the worker) still writes into a live trace, never a dead stack frame.
    using LopTrace = std::vector<std::pair<std::size_t, std::vector<std::vector<std::byte>>>>;
    [[nodiscard]] std::shared_ptr<LopTrace> TraceLopPages(Extrinsic::Tests::MockDevice& device)
    {
        auto trace = std::make_shared<LopTrace>();
        const auto submit = device.ComputeReadback;
        device.ComputeReadback = [&device, trace, submit](auto record, auto bytes, auto sink)
        {
            return submit(
                [&device, trace, record, bytes](auto& commands)
                {
                    const auto begin = device.CommandContext.PushConstantPayloads.size();
                    const auto output = record(commands);
                    auto& page = trace->emplace_back(bytes, std::vector<std::vector<std::byte>>{}).second;
                    const auto& pushes = device.CommandContext.PushConstantPayloads;
                    for (auto i = begin; i < pushes.size(); ++i)
                        if (pushes[i].size() == 88u)
                            page.emplace_back(pushes[i].begin() + 8, pushes[i].end());
                    return output;
                },
                bytes,
                std::move(sink));
        };
        return trace;
    }
    // A worker's live-preview copy into the slot the owner offered: its own submission, no readback, no dispatch.
    [[nodiscard]] bool PreviewCopyPage(const LopTrace::value_type& page)
    {
        return page.first == 0u && page.second.empty();
    }
}

// ARCH-023 L1: a live preview is counted once its copy is submitted, not when the run allowed it. With the position
// ring's free slot held elsewhere, the interval previews at iterations 4, 6 and 8 are dropped (the residency counts
// them) and the run's copy count stays at the one copy made; the dispatches and the result equal a preview-off run.
// Predictions: PreviewCopies 1 (before the fix 4), Previews 2 (one interval copy and the terminal front), at least 3
// dropped previews, one copy page and otherwise the preview-off trace. Mutation: counting in MayPreview again gives 4.
TEST_F(ResidentLop, ABusyRingDropsLivePreviewsWithoutCountingThemAndChangesNothingElse)
{
    Request.Config.Execution.IterationCap = 9u;
    const auto traced = TraceLopPages(Device);
    auto& trace = *traced; // shared with the interposer
    Request.Config.Execution.PreviewEvery = 0u;
    Start();
    ASSERT_TRUE(Until(
        [&]
        {
            return DiagnosticSink.has_value();
        }));
    CompleteDiagnostics();
    ASSERT_TRUE(Until(
        [&]
        {
            return Observation().ReadyToAccept;
        }));
    const auto off = std::exchange(trace, {});
    (void)Service->GpuRun(Correlation, Runtime::PointCloudConsolidationGpuAction::Discard);
    ASSERT_TRUE(Until(
        [&]
        {
            return Results.size() == 1u;
        }));

    auto* residency = Cache.PropertyResidency();
    ASSERT_NE(residency, nullptr);
    const auto key = Runtime::MakeGpuPropertyKey(World, Entity, Request.Properties.OutputPositions);
    // The second run steps to iteration 2 and rests there (start=ready, step 2: supported controls, no race with the
    // worker). Its copy at 2 is published as the front in a drain, and the owner offers the next copy the ring's other
    // slot in that Tick's frame callbacks: between the two, a reader takes that slot, so the ring has none free.
    Request.Config.Execution.PreviewEvery = 2u;
    Request.Config.Execution.Start = Runtime::StepRunStart::Ready;
    Start();
    ASSERT_TRUE(Until(
        [&]
        {
            return Observation().StepRun != 0u;
        }));
    const auto id = Observation().StepRun;
    ASSERT_TRUE(Control(id, Runtime::StepRunAction::Step, 2u));
    ASSERT_TRUE(UntilBeforeFrame(
        [&]
        {
            return Observation().Previews == 1u;
        }))
        << "the copy at iteration 2 published";
    const auto front = residency->Front(key);
    ASSERT_TRUE(front);
    auto held = residency->AcquireBack(key, front->Layout, 2u); // the ring's only free slot, held elsewhere
    ASSERT_TRUE(held);
    ASSERT_TRUE(RunPaused(id, 2u));
    EXPECT_EQ(Observation().Iterations, 2u);
    EXPECT_EQ(Run(id).PreviewCopies, 1u);
    const auto dropped = residency->Stats().DroppedPreviews;
    ASSERT_TRUE(Control(id, Runtime::StepRunAction::Run));
    ASSERT_TRUE(Until(
        [&]
        {
            return Observation().Iterations == 9u;
        }));
    for (unsigned i = 0u; i < 5u; ++i)
        Tick();
    EXPECT_FALSE(DiagnosticSink) << "the terminal front waits for a slot";
    EXPECT_EQ(Observation().Previews, 1u);
    EXPECT_EQ(Run(id).PreviewCopies, 1u) << "the dropped previews are not counted";
    EXPECT_GE(residency->Stats().DroppedPreviews - dropped, 3u) << "4, 6 and 8 were dropped";
    held.reset();
    ASSERT_TRUE(Until(
        [&]
        {
            return DiagnosticSink.has_value();
        }));
    CompleteDiagnostics();
    ASSERT_TRUE(Until(
        [&]
        {
            return Observation().CanAccept;
        }));
    EXPECT_EQ(Observation().Previews, 2u) << "the interval copy and the terminal front";
    EXPECT_EQ(Run(id).PreviewCopies, 1u) << "the terminal front is the result, not a live preview";
    // ARCH-023: a live preview is the worker's own copy submission (no dispatch, no readback); the terminal front rides
    // the Reduce page. One copy page, the copy at 2: the dropped previews submitted nothing, and every other page
    // equals the preview-off run's (stepping to 2 and running on records the uninterrupted run's pages).
    EXPECT_EQ(std::ranges::count_if(trace, PreviewCopyPage), 1);
    auto pages = trace;
    std::erase_if(pages, PreviewCopyPage);
    EXPECT_EQ(std::ranges::count_if(off, PreviewCopyPage), 0);
    EXPECT_EQ(pages, off) << "the dispatches are the preview-off run's";
    (void)Service->GpuRun(Correlation, Runtime::PointCloudConsolidationGpuAction::Accept);
    ASSERT_TRUE(Until(
        [&]
        {
            return Results.size() == 2u;
        }));
    EXPECT_TRUE(Results.back().Succeeded()) << Results.back().Message;
    EXPECT_EQ(std::as_const(Properties()).Get<glm::vec3>("v:position").Vector(), Accepted);
}

// ARCH-023: start=ready runs Init (the upload, the source grid and the L2 initialization pages) and records no
// iteration until one step is asked for; Step(1) then records exactly one iteration (one Grid page and its
// projection pages) and rests. Predictions: Init's 2 submissions (Upload, one Initialize page) and no Grid page while
// ready, 0 iterations; after Step(1) 1 iteration, exactly 2 more submissions (Grid, one Project page with Finalize)
// and no further page. Mutation: an Init that waits for the first step records 0 submissions while ready; a start
// that ignores the start mode records iterations at once.
TEST_F(ResidentLop, AReadyRunInitializesButRecordsNoIterationUntilOneStepAndThenExactlyOne)
{
    Request.Config.Execution.Start = Runtime::StepRunStart::Ready;
    Request.Config.Execution.IterationCap = 6u;
    Request.Config.Execution.PreviewEvery = 0u;
    Start();
    ASSERT_TRUE(Until(
        [&]
        {
            return Observation().StepRun != 0u && !Run(Observation().StepRun).SegmentInFlight;
        }));
    const auto id = Observation().StepRun;
    for (unsigned i = 0u; i < 10u; ++i)
        Tick();
    EXPECT_EQ(Run(id).Phase, Runtime::StepRunPhase::Ready);
    // ARCH-023: the owner's Init allocates the working set and uploads its initial values; its pages (the input
    // copy, the source grid, the initialization) are recorded by the first step on the worker.
    EXPECT_EQ(Observation().Submissions, 0u) << "a ready run records no page";
    EXPECT_GT(Device.TransferQueue.BufferUploads.size(), 0u) << "but its Init uploaded the initial values";
    EXPECT_EQ(Observation().Iterations, 0u);
    ASSERT_TRUE(Control(id, Runtime::StepRunAction::Step, 1u));
    ASSERT_TRUE(RunPaused(id, 1u));
    const auto submissions = Observation().Submissions;
    for (unsigned i = 0u; i < 10u; ++i)
        Tick();
    EXPECT_EQ(Observation().Iterations, 1u) << "exactly one logical step";
    EXPECT_EQ(submissions, 4u) << "the Init's upload and initialization pages, then one Grid and one projection page";
    EXPECT_EQ(Observation().Submissions, submissions) << "and nothing after it";
}

// ARCH-023: run_control restart of a paused in-place run (its owner's initializer once no page is in flight): a new
// generation that uploads the input again and records exactly the pages of a fresh uninterrupted run, so nothing of
// the older output is reused; it runs on (the run starts running). Predictions: generation 1, 6 steps, the new
// generation's trace equal to a fresh run's. Mutation: an initializer that keeps the iteration (or the scratch) state
// records a different trace.
TEST_F(ResidentLop, ARestartIsAFreshRunOfTheSameInputsInANewGeneration)
{
    Request.Config.Execution.IterationCap = 6u;
    Request.Config.Execution.PreviewEvery = 0u;
    const auto traced = TraceLopPages(Device);
    auto& trace = *traced; // shared with the interposer
    Start();
    ASSERT_TRUE(Until(
        [&]
        {
            return DiagnosticSink.has_value();
        }));
    CompleteDiagnostics();
    ASSERT_TRUE(Until(
        [&]
        {
            return Observation().ReadyToAccept;
        }));
    const auto fresh = std::exchange(trace, {});
    (void)Service->GpuRun(Correlation, Runtime::PointCloudConsolidationGpuAction::Discard);
    ASSERT_TRUE(Until(
        [&]
        {
            return Results.size() == 1u;
        }));

    Start();
    ASSERT_TRUE(Until(
        [&]
        {
            return Observation().StepRun != 0u;
        }));
    const auto id = Observation().StepRun;
    EXPECT_TRUE(Run(id).CanRestart);
    ASSERT_TRUE(Control(id, Runtime::StepRunAction::Pause));
    ASSERT_TRUE(Until(
        [&]
        {
            return Run(id).Phase == Runtime::StepRunPhase::Paused && !Run(id).SegmentInFlight;
        }));
    if (Run(id).Steps == 0u)
    {
        ASSERT_TRUE(Control(id, Runtime::StepRunAction::Step, 1u));
        ASSERT_TRUE(RunPaused(id, 1u));
    }
    ASSERT_GE(Observation().Iterations, 1u);
    trace.clear(); // nothing is in flight: every later page is the new generation's
    ASSERT_TRUE(Control(id, Runtime::StepRunAction::Restart));
    ASSERT_TRUE(Until(
        [&]
        {
            return Run(id).Generation == 1u;
        }));
    ASSERT_TRUE(Until(
        [&]
        {
            return DiagnosticSink.has_value();
        }));
    CompleteDiagnostics();
    ASSERT_TRUE(Until(
        [&]
        {
            return Observation().ReadyToAccept;
        }));
    EXPECT_EQ(Run(id).Steps, 6u);
    EXPECT_EQ(Observation().Iterations, 6u);
    EXPECT_EQ(trace, fresh) << "the restart is the fresh run";
    EXPECT_EQ(Results.size(), 1u) << "the restart ended nothing";
}

// ARCH-023 M4: the module shuts down with a page of an active run in flight, in the engine's order: BeginShutdown
// waits for the device and shuts the GPU-queue participants down (the consolidation participant destroys its GPU
// state), then Shutdown drains the jobs and shuts the modules down. The run ends discarded and its callables, which
// capture the destroyed GPU state, are dropped at that point: nothing retains or calls into the dead owner, and the
// worker's page wait gives up (the state sealed its pages), so the drain returns. The held page completes at the
// device-idle wait, as every submitted page does on a device. Prediction: restart offered before the shutdown
// (callables alive), not after it; the run Discarded; the segment ended after the drain. Mutation: removing the
// Abandon in the GPU state's destructor keeps the callables (CanRestart true after the shutdown).
TEST_F(ResidentLop, AShutdownWithAPageInFlightDropsTheRunsCallables)
{
    HoldCompletion = true;
    Start();
    ASSERT_TRUE(Until(
        [&]
        {
            return HasHeld();
        }));
    const auto id = Observation().StepRun;
    ASSERT_NE(id, 0u);
    EXPECT_TRUE(Run(id).WorkerInFlight) << "the worker waits for its held page";
    EXPECT_TRUE(Run(id).CanRestart) << "its owner's callables are alive";
    Service->Unsubscribe(Subscription);
    Service = nullptr; // the teardown below is this test's
    (void)Jobs.Jobs().ShutdownGpuQueueParticipants(
        [this]
        {
            // Device idle: the held page completed (its diagnostics: one active row, no error).
            HoldCompletion = false;
            if (HasHeld())
            {
                std::array<std::uint32_t, 16> upload{};
                upload[3] = 1u;
                upload[11] = 2u * Before.size();
                ReleaseHeld(std::as_bytes(std::span(upload)));
            }
            Device.WaitIdle();
        });
    EXPECT_EQ(Run(id).Phase, Runtime::StepRunPhase::Discarded) << "the GPU state's end discarded the run";
    EXPECT_FALSE(Run(id).CanRestart) << "the owner's callables went with its state";
    Jobs.Jobs().CancelAndDrain(); // returns: the sealed worker gave its wait up
    EXPECT_FALSE(Run(id).SegmentInFlight);
    Runtime::RuntimeModuleShutdownContext shutdown{Commands, Events, Jobs.Jobs(), Worlds, Services};
    Module.OnShutdown(shutdown);
    EXPECT_EQ(Run(id).Phase, Runtime::StepRunPhase::Discarded);
    EXPECT_FALSE(Run(id).CanRestart);
    std::string refusal;
    EXPECT_FALSE(StepRuns.Control(id, Runtime::StepRunAction::Restart, 1u, refusal));
}

// ARCH-023 H3 in the module: run_control's discard of a run whose Accept readback is pending wins before the
// publication: nothing is written, no undo entry, one Cancelled result, the run Discarded (in the natural drain order;
// the reverse order is GpuPositionsAccept.TheProducersStepRunDecidesAtThePublicationBoundary).
TEST_F(ResidentLop, RunControlDiscardDuringTheAcceptReadbackPublishesNothing)
{
    Start();
    ASSERT_TRUE(Until(
        [&]
        {
            return DiagnosticSink.has_value();
        }));
    CompleteDiagnostics();
    ASSERT_TRUE(Until(
        [&]
        {
            return Observation().CanAccept;
        }));
    const auto id = Observation().StepRun;
    const auto undo = History.UndoCount();
    HoldCompletion = true;
    ASSERT_TRUE(Control(id, Runtime::StepRunAction::Accept));
    ASSERT_TRUE(Until(
        [&]
        {
            return HasHeld();
        }));
    ASSERT_EQ(Run(id).Phase, Runtime::StepRunPhase::Accepting);
    ASSERT_TRUE(Control(id, Runtime::StepRunAction::Discard)) << "before the publication a discard still wins";
    ReleaseHeld(std::as_bytes(std::span(Accepted))); // the readback lands in the same tick
    ASSERT_TRUE(Until(
        [&]
        {
            return Results.size() == 1u;
        }));
    for (unsigned i = 0u; i < 10u; ++i)
        Tick();
    EXPECT_EQ(Results.size(), 1u);
    EXPECT_EQ(Results.back().Status, Runtime::PointCloudConsolidationRunStatus::Cancelled) << Results.back().Message;
    EXPECT_EQ(std::as_const(Properties()).Get<glm::vec3>("v:position").Vector(), Before);
    EXPECT_EQ(History.UndoCount(), undo);
    EXPECT_EQ(Run(id).Phase, Runtime::StepRunPhase::Discarded);
}

namespace
{
    namespace Consolidation = Geometry::PointCloud::Consolidation;

    // The CPU reference of a request at `iterations` iterations, with the parameters the module builds from it.
    Consolidation::Result CpuReferenceOf(const std::vector<glm::vec3>& points,
        const Runtime::PointCloudConsolidationConfig& config,
        const double radius,
        const std::uint32_t iterations)
    {
        Consolidation::Params params{};
        params.SupportRadius = radius;
        params.RepulsionWeight = config.RepulsionWeight;
        params.MaxIterations = iterations;
        params.ConvergenceTolerance = config.ConvergenceTolerance;
        params.TargetPointCount = config.TargetPointCount;
        params.Seed = config.Seed;
        params.MaxInputPointCount = 1'000'000u;
        params.MaxOutputPointCount = 1'000'000u;
        switch (config.Strategy)
        {
        case Runtime::PointCloudConsolidationStrategy::Lop:  params.Method = Consolidation::LopStrategy{}; break;
        case Runtime::PointCloudConsolidationStrategy::Wlop: params.Method = Consolidation::WlopStrategy{}; break;
        case Runtime::PointCloudConsolidationStrategy::Clop:
            params.Method = Consolidation::ClopStrategy{.MixtureComponentCount = config.ClopMixtureComponentCount,
                .MixtureMaxIterations = config.ClopMixtureMaxIterations,
                .MixtureRelativeTolerance = config.ClopMixtureRelativeTolerance,
                .CovarianceFloor = config.ClopCovarianceFloor};
            break;
        case Runtime::PointCloudConsolidationStrategy::Ear: params.Method = Consolidation::EarStrategy{}; break;
        }
        return Consolidation::Consolidate(points, params);
    }

    // A CPU run of the fixture's entity: the reference backend, `cap` iterations without a convergence stop, no
    // live preview, a new output of the same cardinality, waiting initialized for its controls.
    void UseCpuRun(Runtime::PointCloudConsolidationRequest& request,
        const Runtime::PointCloudConsolidationStrategy strategy,
        const std::uint32_t cap,
        std::string output)
    {
        request.Config.Backend = Runtime::PointCloudConsolidationBackend::CpuReference;
        request.Config.Strategy = strategy;
        request.Config.ClopMixtureComponentCount = 1u;
        request.Config.Execution = {.IterationCap = cap,
            .PreviewEvery = 0u,
            .PreviewIntervalMs = 0u,
            .AutoAccept = Runtime::StepRunAutoAccept::Never,
            .Start = Runtime::StepRunStart::Ready};
        request.Config.ConvergenceTolerance = 0.0;
        request.Properties.OutputPositions.Name = std::move(output);
    }
}

// ARCH-023 B8: every CPU strategy is a worker step run of NeighborhoodProjection: Init before Ready (no step), one step
// one projection iteration, Run equal to repeated Step(1), one Accept publishing once with one undo entry, all equal
// to the CPU reference. Predictions for LOP, WLOP and CLOP: Ready with 0 steps; Step(1) three times rests after each
// with 1, 2, 3 steps, the third ending at the cap; the accepted output bitwise equal to Consolidate at 3 iterations and
// to a Run/End run's; one undo entry per Accept; the result names the step run. Mutation: a step that advances twice,
// or Init counted as a step, gives other step counts or positions.
TEST_F(ResidentLop, CpuStepRunsMatchTheReferenceAndRunEqualsRepeatedSteps)
{
    for (const auto strategy : {Runtime::PointCloudConsolidationStrategy::Lop,
             Runtime::PointCloudConsolidationStrategy::Wlop,
             Runtime::PointCloudConsolidationStrategy::Clop})
    {
        SCOPED_TRACE(static_cast<int>(strategy));
        Results.clear();
        const auto stepped = std::string{"stepped_"} + std::to_string(static_cast<int>(strategy));
        UseCpuRun(Request, strategy, 3u, stepped);
        const auto undo = History.UndoCount();
        Start();
        ASSERT_TRUE(Until(
            [&]
            {
                return Observation().StepRun != 0u;
            }));
        const auto id = Observation().StepRun;
        EXPECT_EQ(Run(id).Phase, Runtime::StepRunPhase::Ready);
        EXPECT_EQ(Run(id).Steps, 0u) << "Init is not a step";
        for (std::uint32_t k = 1u; k <= 3u; ++k)
        {
            ASSERT_TRUE(Control(id, Runtime::StepRunAction::Step, 1u));
            ASSERT_TRUE(RunPaused(id, k));
        }
        EXPECT_EQ(Run(id).End, Runtime::StepRunEnd::IterationCap);
        EXPECT_TRUE(Results.empty()) << "nothing publishes before Accept";
        ASSERT_TRUE(Control(id, Runtime::StepRunAction::Accept));
        ASSERT_TRUE(Until(
            [&]
            {
                return Results.size() == 1u;
            }));
        ASSERT_TRUE(Results[0].Succeeded()) << Results[0].Message;
        EXPECT_EQ(Results[0].StepRun, id);
        EXPECT_EQ(Results[0].Iterations, 3u);
        EXPECT_EQ(History.UndoCount(), undo + 1u);
        const auto output = std::as_const(Properties()).Get<glm::vec3>(stepped).Vector();
        const auto reference = CpuReferenceOf(Before, Request.Config, Results[0].ResolvedSupportRadius, 3u);
        ASSERT_TRUE(reference.State == Consolidation::Status::NotConverged || reference.Succeeded())
            << Consolidation::DebugName(reference.State);
        EXPECT_EQ(output, reference.Positions);

        const auto ran = stepped + "_run";
        Request.Properties.OutputPositions.Name = ran;
        Request.Config.Execution.Start = Runtime::StepRunStart::Run;
        Request.Config.Execution.AutoAccept = Runtime::StepRunAutoAccept::End;
        Start();
        ASSERT_TRUE(Until(
            [&]
            {
                return Results.size() == 2u;
            }));
        ASSERT_TRUE(Results[1].Succeeded()) << Results[1].Message;
        EXPECT_EQ(std::as_const(Properties()).Get<glm::vec3>(ran).Vector(), output) << "Run is the same steps";
        EXPECT_EQ(History.UndoCount(), undo + 2u);
    }
}

// ARCH-023 B8: Pause and Step(n) act at iteration boundaries, Stop keeps a valid partial result and Accept publishes it.
// Predictions: Step(2) then Step(1) rest at 2 and 3 steps; Stop ends the run (End Stopped, nothing published); Accept
// publishes the third iteration's positions (bitwise the reference at 3 iterations), NotConverged, not converged, with
// the stopped message and one undo entry. A Run paused at some boundary k and resumed ends at the cap with the
// reference's 6-iteration positions. Mutation: a Stop that discards publishes nothing; a partial result taken from the
// initial samples differs from the reference at 3.
TEST_F(ResidentLop, CpuStepRunPausesStopsAndAcceptsThePartialResult)
{
    UseCpuRun(Request, Runtime::PointCloudConsolidationStrategy::Wlop, 6u, "partial");
    const auto undo = History.UndoCount();
    Start();
    ASSERT_TRUE(Until(
        [&]
        {
            return Observation().StepRun != 0u;
        }));
    auto id = Observation().StepRun;
    ASSERT_TRUE(Control(id, Runtime::StepRunAction::Step, 2u));
    ASSERT_TRUE(RunPaused(id, 2u));
    ASSERT_TRUE(Control(id, Runtime::StepRunAction::Step, 1u));
    ASSERT_TRUE(RunPaused(id, 3u));
    ASSERT_TRUE(Control(id, Runtime::StepRunAction::Stop));
    ASSERT_TRUE(Until(
        [&]
        {
            return Run(id).End == Runtime::StepRunEnd::Stopped;
        }));
    for (unsigned i = 0u; i < 5u; ++i)
        Tick();
    EXPECT_TRUE(Results.empty()) << "a stopped run waits for Accept";
    ASSERT_TRUE(Control(id, Runtime::StepRunAction::Accept));
    ASSERT_TRUE(Until(
        [&]
        {
            return Results.size() == 1u;
        }));
    const auto& partial = Results[0];
    ASSERT_TRUE(partial.Succeeded()) << partial.Message;
    EXPECT_EQ(partial.Iterations, 3u);
    EXPECT_FALSE(partial.Converged);
    EXPECT_EQ(partial.GeometryStatus, Consolidation::Status::NotConverged);
    EXPECT_NE(partial.Message.find("stopped"), std::string::npos) << partial.Message;
    EXPECT_EQ(History.UndoCount(), undo + 1u);
    EXPECT_EQ(std::as_const(Properties()).Get<glm::vec3>("partial").Vector(),
        CpuReferenceOf(Before, Request.Config, partial.ResolvedSupportRadius, 3u).Positions);

    Request.Properties.OutputPositions.Name = "resumed";
    Request.Config.Execution.Start = Runtime::StepRunStart::Run;
    Start();
    ASSERT_TRUE(Until(
        [&]
        {
            return Observation().StepRun != 0u;
        }));
    id = Observation().StepRun;
    (void)Control(id, Runtime::StepRunAction::Pause); // refused once the run already ended
    ASSERT_TRUE(Until(
        [&]
        {
            return Run(id).Phase == Runtime::StepRunPhase::Paused && !Run(id).SegmentInFlight;
        }));
    EXPECT_LE(Run(id).Steps, 6u);
    if (Run(id).End == Runtime::StepRunEnd::None)
        ASSERT_TRUE(Control(id, Runtime::StepRunAction::Run));
    ASSERT_TRUE(Until(
        [&]
        {
            return Run(id).End == Runtime::StepRunEnd::IterationCap && !Run(id).SegmentInFlight;
        }));
    EXPECT_EQ(Run(id).Steps, 6u);
    ASSERT_TRUE(Control(id, Runtime::StepRunAction::Accept));
    ASSERT_TRUE(Until(
        [&]
        {
            return Results.size() == 2u;
        }));
    ASSERT_TRUE(Results[1].Succeeded()) << Results[1].Message;
    EXPECT_EQ(std::as_const(Properties()).Get<glm::vec3>("resumed").Vector(),
        CpuReferenceOf(Before, Request.Config, Results[1].ResolvedSupportRadius, 6u).Positions);
}

// ARCH-023 B8: run_control restart of a CPU run initializes its projection again: a new generation from step 0 in the
// run's start mode, and nothing of the older iterations carries over. Predictions: generation 1, phase Ready, 0 steps;
// Step(3) then Accept publishes the reference at 3 iterations (not at 2 + 3 = 5) and one result. Mutation: a restart
// that only clears the counters publishes the 5-iteration positions.
TEST_F(ResidentLop, CpuStepRunRestartIsAFreshRunInANewGeneration)
{
    UseCpuRun(Request, Runtime::PointCloudConsolidationStrategy::Lop, 6u, "restarted");
    Start();
    ASSERT_TRUE(Until(
        [&]
        {
            return Observation().StepRun != 0u;
        }));
    const auto id = Observation().StepRun;
    EXPECT_TRUE(Run(id).CanRestart);
    ASSERT_TRUE(Control(id, Runtime::StepRunAction::Step, 2u));
    ASSERT_TRUE(RunPaused(id, 2u));
    ASSERT_TRUE(Control(id, Runtime::StepRunAction::Restart));
    ASSERT_TRUE(Until(
        [&]
        {
            return Run(id).Generation == 1u;
        }));
    EXPECT_EQ(Run(id).Phase, Runtime::StepRunPhase::Ready);
    EXPECT_EQ(Run(id).Steps, 0u);
    ASSERT_TRUE(Control(id, Runtime::StepRunAction::Step, 3u));
    ASSERT_TRUE(RunPaused(id, 3u));
    ASSERT_TRUE(Control(id, Runtime::StepRunAction::Accept));
    ASSERT_TRUE(Until(
        [&]
        {
            return Results.size() == 1u;
        }));
    ASSERT_TRUE(Results[0].Succeeded()) << Results[0].Message;
    const double radius = Results[0].ResolvedSupportRadius;
    const auto output = std::as_const(Properties()).Get<glm::vec3>("restarted").Vector();
    EXPECT_EQ(output, CpuReferenceOf(Before, Request.Config, radius, 3u).Positions);
    EXPECT_NE(output, CpuReferenceOf(Before, Request.Config, radius, 5u).Positions);
}

// ARCH-023 B8: an in-place CPU run shows its iterations as the positions' ring front (the scene is unchanged), a discard
// publishes nothing and releases that front, and live preview changes no result. Predictions: preview on: copies made,
// a step shown, a ring while paused, positions unchanged; discard: one Cancelled result, positions and undo count
// unchanged, no ring. Two batch runs (preview every step, preview off) accept bitwise equal positions, the second with
// no copy. Mutation: a preview that writes the property changes the scene before Accept.
TEST_F(ResidentLop, CpuStepRunPreviewIsAResidentFrontAndChangesNoResult)
{
    UseCpuRun(Request, Runtime::PointCloudConsolidationStrategy::Wlop, 4u, "v:position");
    Request.Config.Execution.PreviewEvery = 1u;
    Request.Config.Execution.Start = Runtime::StepRunStart::Run;
    auto* residency = Cache.PropertyResidency();
    ASSERT_NE(residency, nullptr);
    const auto key = Runtime::MakeGpuPropertyKey(World, Entity, Request.Properties.OutputPositions);
    const auto undo = History.UndoCount();
    Start();
    ASSERT_TRUE(Until(
        [&]
        {
            return Observation().StepRun != 0u;
        }));
    auto id = Observation().StepRun;
    ASSERT_TRUE(Until(
        [&]
        {
            return Run(id).End == Runtime::StepRunEnd::IterationCap && Run(id).ShownStep;
        }));
    EXPECT_GT(Run(id).PreviewCopies, 0u);
    EXPECT_TRUE(residency->HasRing(key)) << "the preview is the ring's front";
    EXPECT_EQ(std::as_const(Properties()).Get<glm::vec3>("v:position").Vector(), Before);
    ASSERT_TRUE(Control(id, Runtime::StepRunAction::Discard));
    ASSERT_TRUE(Until(
        [&]
        {
            return Results.size() == 1u;
        }));
    EXPECT_EQ(Results[0].Status, Runtime::PointCloudConsolidationRunStatus::Cancelled);
    EXPECT_EQ(std::as_const(Properties()).Get<glm::vec3>("v:position").Vector(), Before);
    EXPECT_EQ(History.UndoCount(), undo);
    EXPECT_FALSE(residency->HasRing(key));

    Request.Config.Execution.AutoAccept = Runtime::StepRunAutoAccept::End;
    std::vector<glm::vec3> previewed;
    for (const std::uint32_t every : {1u, 0u})
    {
        Request.Config.Execution.PreviewEvery = every;
        const auto before = Results.size();
        Start();
        ASSERT_TRUE(Until(
            [&]
            {
                return Observation().StepRun != 0u || Results.size() > before;
            }));
        id = Observation().StepRun != 0u ? Observation().StepRun : Results.back().StepRun;
        ASSERT_TRUE(Until(
            [&]
            {
                return Results.size() == before + 1u;
            }));
        ASSERT_TRUE(Results.back().Succeeded()) << Results.back().Message;
        const auto accepted = std::as_const(Properties()).Get<glm::vec3>("v:position").Vector();
        if (every == 0u)
        {
            EXPECT_EQ(Run(id).PreviewCopies, 0u) << "preview off makes no copy";
            EXPECT_EQ(accepted, previewed) << "and changes no result";
        }
        else
            previewed = accepted;
        ASSERT_EQ(History.Undo().Status, Runtime::EditorCommandHistoryStatus::Undone);
    }
}

// ARCH-023 B8: Accept is revision-safe: a CPU run whose source changed after its capture publishes nothing. Predictions:
// one StaleSource result, the edited positions kept, no undo entry, the step run Failed (its Commit refused).
TEST_F(ResidentLop, CpuStepRunStaleSourceAcceptPublishesNothing)
{
    UseCpuRun(Request, Runtime::PointCloudConsolidationStrategy::Lop, 4u, "stale_output");
    const auto undo = History.UndoCount();
    Start();
    ASSERT_TRUE(Until(
        [&]
        {
            return Observation().StepRun != 0u;
        }));
    const auto id = Observation().StepRun;
    ASSERT_TRUE(Control(id, Runtime::StepRunAction::Step, 1u));
    ASSERT_TRUE(RunPaused(id, 1u));
    Properties().Get<glm::vec3>("v:position")[0].x += 0.5f;
    const auto edited = std::as_const(Properties()).Get<glm::vec3>("v:position").Vector();
    ASSERT_TRUE(Control(id, Runtime::StepRunAction::Accept));
    ASSERT_TRUE(Until(
        [&]
        {
            return Results.size() == 1u;
        }));
    EXPECT_EQ(Results[0].Status, Runtime::PointCloudConsolidationRunStatus::StaleSource);
    EXPECT_FALSE(std::as_const(Properties()).Exists("stale_output"));
    EXPECT_EQ(std::as_const(Properties()).Get<glm::vec3>("v:position").Vector(), edited);
    EXPECT_EQ(History.UndoCount(), undo);
    EXPECT_EQ(Run(id).Phase, Runtime::StepRunPhase::Failed);
}

// ARCH-023 B8: the module shuts down with a paused CPU run: the run ends discarded and its callables, which capture the
// module's services, are dropped then. Predictions: restart offered before, Discarded and no restart after.
TEST_F(ResidentLop, AShutdownWithAPausedCpuRunDropsItsCallables)
{
    UseCpuRun(Request, Runtime::PointCloudConsolidationStrategy::Lop, 4u, "shutdown_output");
    Start();
    ASSERT_TRUE(Until(
        [&]
        {
            return Observation().StepRun != 0u;
        }));
    const auto id = Observation().StepRun;
    ASSERT_TRUE(Control(id, Runtime::StepRunAction::Step, 1u));
    ASSERT_TRUE(RunPaused(id, 1u));
    EXPECT_TRUE(Run(id).CanRestart);
    Service->Unsubscribe(Subscription);
    Service = nullptr; // the teardown below is this test's
    Runtime::RuntimeModuleShutdownContext shutdown{Commands, Events, Jobs.Jobs(), Worlds, Services};
    Module.OnShutdown(shutdown);
    EXPECT_EQ(Run(id).Phase, Runtime::StepRunPhase::Discarded);
    EXPECT_FALSE(Run(id).CanRestart);
    std::string refusal;
    EXPECT_FALSE(StepRuns.Control(id, Runtime::StepRunAction::Run, 1u, refusal));
}

// ARCH-023 B5: isotropic WLOP runs on the resident LOP pages: its density weights are row pages (the sources' in Init,
// the projected samples' in every iteration), its positions are the resident input (no packed upload), and a step is
// one iteration. Predictions: while ready, Init's 3 submissions (Upload, one source-density page, one initialization
// page) and no iteration; each Step(1) adds 3 (Grid, one projected-density page, one projection page); density
// dispatches 1 + 6; no upload of N*16 bytes; Step(1) x6 records exactly the dispatches of an uninterrupted Run.
// Mutation: dropping the projected-density pages changes the trace and the density count; recording all iterations
// in one submission breaks the per-step counts.
TEST_F(ResidentLop, WlopRunsOnTheResidentPagesAndItsStepsAreTheRunsIterations)
{
    Request.Config.Strategy = Runtime::PointCloudConsolidationStrategy::Wlop;
    Request.Config.Execution.IterationCap = 6u;
    Request.Config.Execution.PreviewEvery = 0u;
    const auto traced = TraceLopPages(Device);
    auto& trace = *traced; // shared with the interposer
    const auto density = [&](const std::size_t from)
    {
        const auto& bound = Device.CommandContext.BoundPipelines;
        return std::count_if(bound.begin() + static_cast<std::ptrdiff_t>(from),
            bound.end(),
            [&](auto handle)
            {
                return PipelinePath(handle).find("lop_density") != std::string::npos;
            });
    };
    auto firstBind = Device.CommandContext.BoundPipelines.size();
    Start();
    ASSERT_TRUE(Until(
        [&]
        {
            return DiagnosticSink.has_value();
        }));
    CompleteDiagnostics();
    ASSERT_TRUE(Until(
        [&]
        {
            return Observation().ReadyToAccept;
        }));
    EXPECT_EQ(density(firstBind), 7) << "one source pass and one projected pass per iteration";
    EXPECT_EQ(std::count_if(Device.TransferQueue.BufferUploads.begin(),
                  Device.TransferQueue.BufferUploads.end(),
                  [&](const auto& upload)
                  {
                      return upload.Data.size() == Before.size() * 16u;
                  }),
        0)
        << "WLOP reads the resident positions";
    const auto run = std::exchange(trace, {});
    (void)Service->GpuRun(Correlation, Runtime::PointCloudConsolidationGpuAction::Discard);
    ASSERT_TRUE(Until(
        [&]
        {
            return Results.size() == 1u;
        }));

    Request.Config.Execution.Start = Runtime::StepRunStart::Ready;
    firstBind = Device.CommandContext.BoundPipelines.size();
    Start();
    ASSERT_TRUE(Until(
        [&]
        {
            return Observation().StepRun != 0u && !Run(Observation().StepRun).SegmentInFlight;
        }));
    const auto id = Observation().StepRun;
    for (unsigned i = 0u; i < 5u; ++i)
        Tick();
    // ARCH-023: the owner Init prepares buffers and uploads only; its three pages (upload, source density,
    // initialization) ride the first step on the worker, so a ready run has recorded no page and no density pass.
    EXPECT_EQ(Observation().Submissions, 0u) << "a ready run records no page";
    EXPECT_EQ(Observation().Iterations, 0u);
    EXPECT_EQ(density(firstBind), 0);
    for (std::uint32_t k = 1u; k <= 5u; ++k)
    {
        ASSERT_TRUE(Control(id, Runtime::StepRunAction::Step, 1u));
        ASSERT_TRUE(RunPaused(id, k));
        EXPECT_EQ(Observation().Submissions, 3u + 3u * k) << "one iteration's pages";
    }
    ASSERT_TRUE(Control(id, Runtime::StepRunAction::Step, 1u));
    ASSERT_TRUE(Until(
        [&]
        {
            return DiagnosticSink.has_value();
        })); // the cap: the terminal front
    CompleteDiagnostics();
    ASSERT_TRUE(Until(
        [&]
        {
            return Observation().ReadyToAccept;
        }));
    EXPECT_EQ(Run(id).Steps, 6u);
    EXPECT_EQ(trace, run) << "Step(1) x6 records the uninterrupted run's dispatches";
}

// ARCH-023 B5: a resident run into a new output is the same step run; it previews nothing (no resident front of that
// output exists), waits for Accept, then reads its positions back once and the module publishes them with one undo
// entry. A discard during that readback publishes nothing. Predictions: Previews 0 and no ring for the output; one
// readback of N*12 bytes after Accept; the output equals the read-back rows, v:position unchanged, one undo entry, one
// Succeeded result naming the step run. The second run, discarded with its readback pending: one Cancelled result, no
// output, no undo entry. Mutation: publishing at the iteration cap without Accept publishes before the Accept.
TEST_F(ResidentLop, ANewOutputWaitsForAcceptAndPublishesOnceThroughTheModule)
{
    Request.Properties.OutputPositions.Name = "lop_out";
    Request.Config.Execution.PreviewEvery = 1u;
    const auto undo = History.UndoCount();
    Start();
    ASSERT_TRUE(Until(
        [&]
        {
            return DiagnosticSink.has_value();
        }));
    CompleteDiagnostics();
    ASSERT_TRUE(Until(
        [&]
        {
            return Observation().CanAccept;
        }));
    const auto id = Observation().StepRun;
    ASSERT_NE(id, 0u);
    EXPECT_EQ(Observation().Previews, 0u);
    EXPECT_FALSE(Cache.PropertyResidency()->HasRing(
        Runtime::MakeGpuPropertyKey(World, Entity, Request.Properties.OutputPositions)));
    for (unsigned i = 0u; i < 5u; ++i)
        Tick();
    EXPECT_TRUE(Results.empty()) << "it waits for Accept";
    ASSERT_TRUE(Control(id, Runtime::StepRunAction::Accept));
    ASSERT_TRUE(Until(
        [&]
        {
            return Results.size() == 1u;
        }));
    ASSERT_TRUE(Results[0].Succeeded()) << Results[0].Message;
    EXPECT_EQ(Results[0].StepRun, id);
    EXPECT_EQ(std::as_const(Properties()).Get<glm::vec3>("lop_out").Vector(), Accepted);
    EXPECT_EQ(std::as_const(Properties()).Get<glm::vec3>("v:position").Vector(), Before);
    EXPECT_EQ(History.UndoCount(), undo + 1u);

    Request.Properties.OutputPositions.Name = "lop_out_discarded";
    Start();
    ASSERT_TRUE(Until(
        [&]
        {
            return DiagnosticSink.has_value();
        }));
    CompleteDiagnostics();
    ASSERT_TRUE(Until(
        [&]
        {
            return Observation().CanAccept;
        }));
    const auto second = Observation().StepRun;
    HoldCompletion = true;
    ASSERT_TRUE(Control(second, Runtime::StepRunAction::Accept));
    ASSERT_TRUE(Until(
        [&]
        {
            return HasHeld();
        }));
    ASSERT_TRUE(Control(second, Runtime::StepRunAction::Discard)) << "before the publication a discard still wins";
    ReleaseHeld(std::as_bytes(std::span(Accepted)));
    HoldCompletion = false;
    ASSERT_TRUE(Until(
        [&]
        {
            return Results.size() == 2u;
        }));
    EXPECT_EQ(Results[1].Status, Runtime::PointCloudConsolidationRunStatus::Cancelled);
    EXPECT_FALSE(std::as_const(Properties()).Exists("lop_out_discarded"));
    EXPECT_EQ(History.UndoCount(), undo + 1u);
    EXPECT_EQ(Run(second).Phase, Runtime::StepRunPhase::Discarded);
}

// ARCH-023: an in-place LOP run advances on its worker with no frame, tick, drain or GPU-queue completion on this
// thread: once its segment started, every iteration's pages are submitted, waited for and taken by the worker. The
// mock device completes a pulled page at submission. Predictions: Steps reaches 3 (and the cap 6) with no Tick; the
// owner's Observation is unchanged until a Tick folds it; Discard afterwards, with the ended run's terminal page on
// the device, ends the run Cancelled once that page completed, with one result. Mutation: a page
// queued through QueueGpuCompute (an owner sink) or a page advanced in AdvanceLop leaves Steps at 0 without Ticks.
TEST_F(ResidentLop, ARunningRunAdvancesOnItsWorkerWithNoTickOrDrain)
{
    CompletePulledAtOnce = true;
    Request.Config.Execution.IterationCap = 6u;
    Request.Config.Execution.PreviewEvery = 0u;
    Request.Config.Execution.Start = Runtime::StepRunStart::Ready;
    Start();
    ASSERT_TRUE(Until(
        [&]
        {
            return Observation().StepRun != 0u;
        }));
    const auto id = Observation().StepRun;
    // The owner's Init (a Tick: allocation, uploads) happens while ready; the run computes nothing yet.
    for (int i = 0; i < 4; ++i)
        Tick();
    EXPECT_EQ(Run(id).Phase, Runtime::StepRunPhase::Ready);
    EXPECT_EQ(Run(id).Steps, 0u);
    // The worker's first page (the Upload) is held until this thread stopped ticking: every iteration counted below
    // ran after the last Tick.
    HoldCompletion = true;
    ASSERT_TRUE(Control(id, Runtime::StepRunAction::Run));
    ASSERT_TRUE(Until(
        [&]
        {
            return HasHeld();
        })); // drains let the pump submit the segment
    HoldCompletion = false;
    EXPECT_EQ(Run(id).Steps, 0u);
    std::array<std::uint32_t, 16> upload{};
    upload[3] = 1u;
    upload[11] = 2u * Before.size();
    ReleaseHeld(std::as_bytes(std::span(upload))); // from here no Tick, drain or completion runs on this thread
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (Run(id).Steps < 6u && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    EXPECT_GE(Run(id).Steps, 3u) << "at least three iterations with no frame";
    EXPECT_EQ(Run(id).Steps, 6u) << "the cap, every iteration on the worker";
    // The worker's segment ended at the cap; its end is recorded when that completion reaches this thread (the
    // step run's main-thread side), which takes a drain: no iteration remains for it to run.
    ASSERT_TRUE(Until(
        [&]
        {
            return Run(id).End == Runtime::StepRunEnd::IterationCap;
        }));
    EXPECT_EQ(Run(id).Steps, 6u) << "the drain ran no iteration";
    // The ended run's terminal front and final reduction are an owner page; a discard ends the run once it completed
    // (the mock device holds that page's diagnostics until CompleteDiagnostics).
    ASSERT_TRUE(Until(
        [&]
        {
            return DiagnosticSink.has_value();
        }));
    (void)Service->GpuRun(Correlation, Runtime::PointCloudConsolidationGpuAction::Discard);
    CompleteDiagnostics();
    ASSERT_TRUE(Until(
        [&]
        {
            return Results.size() == 1u;
        }));
    for (int i = 0; i < 3; ++i)
        Tick();
    EXPECT_EQ(Results.size(), 1u) << "one publication";
    EXPECT_EQ(Results.back().Status, Runtime::PointCloudConsolidationRunStatus::Cancelled);
}

// ARCH-023: a discard while a hybrid run's worker step waits for its page publishes an owner-only result at once and
// reads nothing the step writes; the step gives its page up (abort) without the device finishing it. Predictions:
// one result, Cancelled, the fixed discard message; the segment ends while the page is still held; no second result
// after the worker returned. Mutation: reading the run's failure or projection in Discard while the step runs (the
// race this closes) is what TSan reports; skipping the abort leaves the segment in flight until the page completes.
TEST_F(ResidentLop, ADiscardDuringAHeldHybridStepPublishesAFixedResultAndAbortsTheWorker)
{
    Request.Config.Backend = Runtime::PointCloudConsolidationBackend::VulkanLBVH;
    Request.Config.GpuQueryBatchSize = 64u;
    Request.Config.GpuRadiusCapacity = 128u;
    Request.Config.Execution.Start = Runtime::StepRunStart::Ready;
    Request.Config.Execution.PreviewEvery = 0u;
    Start();
    ASSERT_TRUE(Until(
        [&]
        {
            return Observation().StepRun != 0u;
        }));
    const auto id = Observation().StepRun;
    ASSERT_TRUE(Until(
        [&]
        {
            return Run(id).Phase == Runtime::StepRunPhase::Ready;
        }));
    HoldCompletion = true; // the worker's first query page stays on the device
    ASSERT_TRUE(Control(id, Runtime::StepRunAction::Step, 1u));
    ASSERT_TRUE(Until(
        [&]
        {
            return HasHeld();
        }));
    HoldCompletion = false;
    ASSERT_TRUE(Run(id).WorkerInFlight);
    (void)Service->GpuRun(Correlation, Runtime::PointCloudConsolidationGpuAction::Discard);
    ASSERT_TRUE(Until(
        [&]
        {
            return Results.size() == 1u;
        }));
    EXPECT_EQ(Results.back().Status, Runtime::PointCloudConsolidationRunStatus::Cancelled);
    EXPECT_EQ(Results.back().Message, "Point-set consolidation discarded; previous output retained.");
    ASSERT_TRUE(Until(
        [&]
        {
            return !Run(id).SegmentInFlight;
        }))
        << "the held step gives its page up";
    EXPECT_TRUE(HasHeld()) << "the device never finished the page";
    for (int i = 0; i < 10; ++i)
        Tick();
    EXPECT_EQ(Results.size(), 1u) << "nothing more is published after the worker returned";
    EXPECT_EQ(Run(id).Phase, Runtime::StepRunPhase::Discarded);
}

namespace
{
    // The device result of a farthest-point order of `order.size()` samples: the clearances, then the indices.
    std::vector<std::byte> FarthestPointBytes(
        const std::vector<double>& clearance, const std::vector<std::uint32_t>& order)
    {
        std::vector<std::byte> bytes(order.size() * (sizeof(double) + sizeof(std::uint32_t)), std::byte{0});
        std::memcpy(bytes.data(), clearance.data(), clearance.size() * sizeof(double));
        std::memcpy(bytes.data() + order.size() * sizeof(double), order.data(), order.size() * sizeof(std::uint32_t));
        return bytes;
    }

    // A CPU batch of the fixture's entity (run at once, accepted at its end) whose farthest-point initial samples are
    // ordered on `backend`.
    void UseOrderedCpuBatch(Runtime::PointCloudConsolidationRequest& request,
        const Runtime::PointSamplingBackend backend,
        std::string output)
    {
        UseCpuRun(request, Runtime::PointCloudConsolidationStrategy::Lop, 2u, std::move(output));
        request.Config.Execution.AutoAccept = Runtime::StepRunAutoAccept::End;
        request.Config.Execution.Start = Runtime::StepRunStart::Run;
        request.Config.InitialSampling.Method = Runtime::PointSamplingMethod::FarthestPoint;
        request.Config.InitialBackend = backend;
    }
}

// ARCH-023 (RUNTIME-355 F3): a farthest-point initial order asked on Vulkan is one bounded device page that a worker
// submits, pulls and checks against the CPU (no frame, owner poll or framed job), and it is adopted when it matches:
// the run reports the device and publishes the CPU-ordered run's output bitwise. A mismatch inside the verified prefix
// falls back to the CPU order with the reason (the same output). Red before F3: the page was submitted on this thread
// from the framed job's main-thread readiness, with an owner-delivered sink, not pulled by a worker.
TEST_F(ResidentLop, VulkanInitialOrderIsAWorkerPageMatchingTheCpuOrder)
{
    Device.ShaderFloat64 = true;
    const auto reference = Geometry::PointSampling::Order(std::span<const glm::vec3>(Before), {}, Before.size());
    ASSERT_TRUE(reference.Succeeded());
    // The device's answer and what each submission looked like; shared, so the hook may outlive this body.
    struct Pages
    {
        std::vector<std::byte> Answer{};
        std::vector<bool> Pulled{};
        std::vector<std::thread::id> Submitters{};
    };
    const auto pages = std::make_shared<Pages>();
    Device.ComputeReadback = [this, pages](auto record, const std::uint64_t bytes, Extrinsic::RHI::ReadbackSink sink)
    {
        EXPECT_TRUE(record(Device.CommandContext).IsValid());
        std::scoped_lock lock{Gpu};
        EXPECT_EQ(bytes, pages->Answer.size()) << "a CPU run's only device page is its initial order";
        pages->Pulled.push_back(sink.Pulled);
        pages->Submitters.push_back(std::this_thread::get_id());
        return Defer(std::move(sink), pages->Answer);
    };
    CompletePulledAtOnce = true; // a pulled page completes as soon as it is submitted
    const auto run =
        [&](const Runtime::PointSamplingBackend backend, std::string output, const std::vector<std::uint32_t>& order)
    {
        {
            std::scoped_lock lock{Gpu};
            pages->Answer = FarthestPointBytes(reference.Clearance, order);
            pages->Pulled.clear();
            pages->Submitters.clear();
        }
        Results.clear();
        UseOrderedCpuBatch(Request, backend, std::move(output));
        Start();
        return Until(
            [&]
            {
                return Results.size() == 1u;
            });
    };
    const auto rows = [&](const std::string& name)
    {
        return std::as_const(Properties()).Get<glm::vec3>(name).Vector();
    };

    ASSERT_TRUE(run(Runtime::PointSamplingBackend::Cpu, "cpu_ordered", reference.Order));
    ASSERT_TRUE(Results[0].Succeeded()) << Results[0].Message;
    EXPECT_EQ(Results[0].InitialSamplingBackend, "cpu_reference");
    {
        std::scoped_lock lock{Gpu};
        EXPECT_TRUE(pages->Pulled.empty()) << "the CPU order submits no device page";
    }
    const auto expected = rows("cpu_ordered");
    ASSERT_FALSE(expected.empty());

    ASSERT_TRUE(run(Runtime::PointSamplingBackend::Vulkan, "vulkan_ordered", reference.Order));
    ASSERT_TRUE(Results[0].Succeeded()) << Results[0].Message;
    EXPECT_EQ(Results[0].InitialSamplingRequestedBackend, "gpu_vulkan_compute");
    EXPECT_EQ(Results[0].InitialSamplingBackend, "gpu_vulkan_compute") << Results[0].InitialSamplingDiagnostic;
    EXPECT_EQ(Results[0].InitialSamplingDiagnostic, "");
    {
        std::scoped_lock lock{Gpu};
        ASSERT_EQ(pages->Pulled.size(), 1u) << "one bounded page orders the 25 points";
        EXPECT_TRUE(pages->Pulled[0]) << "a worker pulls the page's bytes (no owner sink, no frame)";
        EXPECT_NE(pages->Submitters[0], std::this_thread::get_id())
            << "the page is submitted on a worker, not by the owner";
    }
    EXPECT_EQ(rows("vulkan_ordered"), expected) << "the device order is the CPU's";

    auto swapped = reference.Order;
    std::swap(swapped[1], swapped[2]);
    ASSERT_TRUE(run(Runtime::PointSamplingBackend::Vulkan, "fallback_ordered", swapped));
    ASSERT_TRUE(Results[0].Succeeded()) << Results[0].Message;
    EXPECT_EQ(Results[0].InitialSamplingBackend, "cpu_reference");
    EXPECT_NE(
        Results[0].InitialSamplingDiagnostic.find("differs from the CPU reference at sample 1"), std::string::npos)
        << Results[0].InitialSamplingDiagnostic;
    EXPECT_EQ(rows("fallback_ordered"), expected) << "the CPU order replaces the rejected one";
}

// ARCH-023 (RUNTIME-355 F3): while a Vulkan initial-order page is held on the device: a world cancel wakes its worker,
// which gives the page up at once (ReleaseReadback), and the run answers once, Cancelled, publishing nothing; a source
// edit lets the page finish, but the stale order is never used (Cancelled, nothing published); a shutdown's
// CancelAndDrain wakes the waiting worker itself instead of waiting for the device (a watchdog completes the page
// after 2 s and records that it had to). No case claims a CPU fallback. Red before F3: the framed page is never given
// up by a worker (ReleaseReadback stays 0).
TEST_F(ResidentLop, AHeldInitialOrderEndsOnCancelStaleOrShutdownWithoutPublishing)
{
    Device.ShaderFloat64 = true;
    const auto reference = Geometry::PointSampling::Order(std::span<const glm::vec3>(Before), {}, Before.size());
    ASSERT_TRUE(reference.Succeeded());
    const auto answer = FarthestPointBytes(reference.Clearance, reference.Order);
    const auto released = std::make_shared<std::uint32_t>(0u); // shared: the hook may outlive this body
    Device.TransferQueue.ReleaseReadbackHook = [this, released](Extrinsic::RHI::ReadbackToken)
    {
        std::scoped_lock lock{Gpu};
        ++*released;
    };
    const auto releases = [&]
    {
        std::scoped_lock lock{Gpu};
        return *released;
    };
    // Starts the batch with its initial order's page held on the device; true once it is held.
    const auto start = [&](std::string output)
    {
        Results.clear();
        UseOrderedCpuBatch(Request, Runtime::PointSamplingBackend::Vulkan, std::move(output));
        HoldCompletion = true;
        Start();
        const bool held = Until(
            [&]
            {
                return HasHeld();
            });
        HoldCompletion = false;
        return held;
    };
    const auto published = [&](const std::string& output)
    {
        return std::as_const(Properties()).Exists(output);
    };

    // A world cancel while the page is held.
    ASSERT_TRUE(start("cancelled_order")) << "the initial order's page never reached the device";
    (void)Jobs.Jobs().CancelAllForWorld(World);
    ASSERT_TRUE(Until(
        [&]
        {
            return Results.size() == 1u;
        }))
        << "the cancel settles the run while its page is held";
    EXPECT_TRUE(Until(
        [&]
        {
            return releases() == 1u;
        }))
        << "the waiting worker gives the held page up";
    EXPECT_EQ(Results[0].Status, Runtime::PointCloudConsolidationRunStatus::Cancelled) << Results[0].Message;
    EXPECT_EQ(Results[0].InitialSamplingDiagnostic.find("ran on the CPU"), std::string::npos)
        << Results[0].InitialSamplingDiagnostic;
    EXPECT_FALSE(published("cancelled_order"));
    EXPECT_EQ(std::as_const(Properties()).Get<glm::vec3>("v:position").Vector(), Before);
    ReleaseHeld(answer); // the device finished the given-up page; nobody takes its bytes
    for (int i = 0; i < 5; ++i)
        Tick();
    EXPECT_EQ(Results.size(), 1u) << "nothing more is published";

    // A source edit while the page is held: the page completes, but its order is never used.
    ASSERT_TRUE(start("stale_order"));
    Properties().Get<glm::vec3>("v:position")[0].x += 0.5f;
    ReleaseHeld(answer);
    ASSERT_TRUE(Until(
        [&]
        {
            return Results.size() == 1u;
        }));
    EXPECT_EQ(Results[0].Status, Runtime::PointCloudConsolidationRunStatus::Cancelled) << Results[0].Message;
    EXPECT_FALSE(published("stale_order"));
    EXPECT_EQ(releases(), 1u) << "a completed page is taken, not given up";

    // A shutdown while the page is held: CancelAndDrain wakes the waiting worker; it does not wait for the device.
    ASSERT_TRUE(start("shutdown_order"));
    std::atomic<bool> drained{false}, forced{false};
    std::thread watchdog(
        [&]
        {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
            while (!drained && std::chrono::steady_clock::now() < deadline)
                std::this_thread::sleep_for(std::chrono::milliseconds{1});
            if (!drained)
            {
                std::scoped_lock lock{Gpu};
                Stopping = true; // a worker still waiting for its page returns
                forced = true;
            }
        });
    Jobs.Jobs().CancelAndDrain();
    drained = true;
    watchdog.join();
    EXPECT_FALSE(forced) << "CancelAndDrain must wake the worker waiting for the held page";
    EXPECT_EQ(releases(), 2u) << "the shutdown's waiting worker gives its page up";
    (void)Events.Pump();
    ASSERT_EQ(Results.size(), 1u);
    EXPECT_EQ(Results[0].Status, Runtime::PointCloudConsolidationRunStatus::Cancelled) << Results[0].Message;
    EXPECT_FALSE(published("shutdown_order"));
}
