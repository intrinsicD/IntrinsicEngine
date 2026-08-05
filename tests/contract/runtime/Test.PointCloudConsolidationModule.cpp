#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <glm/glm.hpp>

#include "RuntimeTestModule.hpp"

import Extrinsic.Core.Config.Engine;
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
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.PointCloudConsolidationConfig;
import Extrinsic.Runtime.PointCloudConsolidationModule;
import Extrinsic.Runtime.SceneDocumentModule;
import Extrinsic.Runtime.SelectionController;
import Geometry.Properties;

namespace CoreConfig = Extrinsic::Core::Config;
namespace Dirty = Extrinsic::ECS::Components::DirtyTags;
namespace ECS = Extrinsic::ECS;
namespace GS = Extrinsic::ECS::Components::GeometrySources;
namespace Runtime = Extrinsic::Runtime;

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
    };

    class ConsolidationStaleSourceApp final
        : public Intrinsic::Tests::RuntimeTestModule
    {
    public:
        explicit ConsolidationStaleSourceApp(
            const Runtime::GeometryElementDomain domain)
            : Domain(domain)
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
            const DomainSourceEntities sources = AddDomainSources(*Scene);
            Entity = ResolveTestEntity(sources, Domain);
            InputProperty = InputPropertyForDomain(Domain);
            CompletionSubscription = Service->SubscribeCompleted(
                [this](const Runtime::PointCloudConsolidationResult& result)
                {
                    Completion = result;
                });

            Extrinsic::Core::Tasks::Scheduler::Dispatch(
                [this]
                {
                    BlockerStarted.store(true, std::memory_order_release);
                    while (!ReleaseBlocker.load(std::memory_order_acquire))
                        std::this_thread::sleep_for(1ms);
                });
            Correlation = Service->Run(MakeDomainRequest(
                Entity, Domain, InputProperty, "lop:stale_output"));
        }

        void Frame(double, double) override
        {
            auto& engine = Kernel();
            ++Ticks;
            if (!Mutated &&
                BlockerStarted.load(std::memory_order_acquire))
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
                    ReleaseBlocker.store(true, std::memory_order_release);
                }
            }

            if (Completion.has_value())
            {
                engine.RequestExit();
            }
            else if (Ticks > 240u)
            {
                TimedOut = true;
                ReleaseBlocker.store(true, std::memory_order_release);
                engine.RequestExit();
            }
        }

        void Shutdown() override
        {
            ReleaseBlocker.store(true, std::memory_order_release);
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
        std::atomic<bool> BlockerStarted{false};
        std::atomic<bool> ReleaseBlocker{false};
        std::uint32_t Ticks{0u};
        bool MissingService{false};
        bool Mutated{false};
        bool TimedOut{false};
    };

    class ConsolidationPropertyDomainsApp final
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
                Correlations.push_back(Service->Run(MakeDomainRequest(
                    ResolveTestEntity(Sources, domain),
                    domain,
                    InputPropertyForDomain(domain),
                    OutputPropertyForDomain(domain))));
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

TEST(PointCloudConsolidationModule, SourceMutationDropsQueuedWriteback)
{
    for (const Runtime::GeometryElementDomain domain : kAllElementDomains)
    {
        SCOPED_TRACE(std::string{Runtime::ToString(domain)});
        auto app = std::make_unique<ConsolidationStaleSourceApp>(domain);
        ConsolidationStaleSourceApp* appPtr = app.get();
        Intrinsic::Tests::RuntimeTestKernel engine{
            HeadlessConfig(1u), std::move(app)};
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
