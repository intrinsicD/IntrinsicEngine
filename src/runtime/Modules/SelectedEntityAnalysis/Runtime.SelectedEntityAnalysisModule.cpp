module;

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>

module Extrinsic.Runtime.SelectedEntityAnalysisModule;

import Extrinsic.Core.Error;
import Extrinsic.ECS.Component.DirtyTags;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Runtime.CommandBus;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.ServiceRegistry;
import Extrinsic.Runtime.VertexChannelBindings;
import Extrinsic.Runtime.WorldRegistry;
import Geometry.Properties;

namespace Extrinsic::Runtime
{
    namespace
    {
        namespace Dirty = ECS::Components::DirtyTags;
        namespace GS = ECS::Components::GeometrySources;

        struct RequestSelectedEntityAnalysis
        {
            SelectedEntityAnalysisKey Key{};
        };

        struct ChannelSnapshot
        {
            bool HasBinding{false};
            VertexChannel Channel{VertexChannel::Normal};
            AttributeSourceType SourceType{AttributeSourceType::Vec3};
            std::string SourceProperty{};
            bool PropertyExists{false};
            std::size_t ExpectedCount{0u};
            std::vector<glm::vec3> Vec3Values{};
            std::vector<glm::vec4> Vec4Values{};
        };

        struct AnalysisSnapshot
        {
            SelectedEntityAnalysisKey Key{};
            ChannelSnapshot Normal{.Channel = VertexChannel::Normal};
            ChannelSnapshot Color{.Channel = VertexChannel::Color};
            bool IsMesh{false};
            bool HasTexcoords{false};
            std::size_t VertexCount{0u};
            std::vector<glm::vec2> Texcoords{};
        };

        struct AnalysisJobResult
        {
            SelectedEntityAnalysisKey Key{};
            SelectedEntityAnalysisResult Result{};
            bool Cancelled{false};
        };

        struct SelectedEntityAnalysisCompleted
        {
            AnalysisJobResult Job{};
        };

        [[nodiscard]] std::size_t ConsumerIndex(
            const SelectedEntityAnalysisConsumer consumer) noexcept
        {
            return static_cast<std::size_t>(consumer);
        }

        [[nodiscard]] ECS::EntityHandle ResolveEntity(
            const entt::registry& raw,
            const std::uint32_t stableEntityId) noexcept
        {
            const ECS::EntityHandle entity =
                SelectionController::ToEntityHandle(stableEntityId);
            return entity != ECS::InvalidEntityHandle && raw.valid(entity)
                ? entity
                : ECS::InvalidEntityHandle;
        }

        [[nodiscard]] const Geometry::PropertySet* VertexProperties(
            const GS::ConstSourceView& view) noexcept
        {
            if (view.ActiveDomain == GS::Domain::Graph)
                return view.NodeSource != nullptr
                    ? &view.NodeSource->Properties
                    : nullptr;
            return view.VertexSource != nullptr
                ? &view.VertexSource->Properties
                : nullptr;
        }

        [[nodiscard]] std::uint64_t BindingGeneration(
            const entt::registry& raw,
            const ECS::EntityHandle entity) noexcept
        {
            const auto* bindings = raw.try_get<VertexChannelBindingSet>(entity);
            return bindings != nullptr ? bindings->BindingGeneration : 0u;
        }

        [[nodiscard]] bool HasAnalysisDirtyTag(
            const entt::registry& raw,
            const ECS::EntityHandle entity) noexcept
        {
            return raw.any_of<Dirty::GpuDirty,
                              Dirty::DirtyVertexPositions,
                              Dirty::DirtyVertexAttributes,
                              Dirty::DirtyVertexTexcoords,
                              Dirty::DirtyVertexNormals,
                              Dirty::DirtyVertexColors,
                              Dirty::DirtyEdgeTopology,
                              Dirty::DirtyFaceTopology>(entity);
        }

        [[nodiscard]] ChannelSnapshot CaptureChannel(
            const Geometry::PropertySet& properties,
            const VertexChannel channel,
            const VertexChannelSourceBinding* binding)
        {
            ChannelSnapshot snapshot{
                .Channel = channel,
                .ExpectedCount = properties.Size(),
            };
            if (binding == nullptr ||
                !IsVertexChannelBindingEnabled(*binding))
            {
                return snapshot;
            }

            snapshot.HasBinding = true;
            snapshot.SourceType = binding->SourceType;
            snapshot.SourceProperty = binding->SourceProperty;
            snapshot.PropertyExists =
                properties.Exists(snapshot.SourceProperty);

            if (snapshot.SourceType == AttributeSourceType::Vec3)
            {
                const auto values =
                    properties.Get<glm::vec3>(snapshot.SourceProperty);
                if (values)
                    snapshot.Vec3Values = values.Vector();
            }
            else if (snapshot.SourceType == AttributeSourceType::Vec4)
            {
                const auto values =
                    properties.Get<glm::vec4>(snapshot.SourceProperty);
                if (values)
                    snapshot.Vec4Values = values.Vector();
            }
            return snapshot;
        }

        [[nodiscard]] std::optional<AnalysisSnapshot> CaptureSnapshot(
            ECS::Scene::Registry& scene,
            const SelectedEntityAnalysisKey& key)
        {
            entt::registry& raw = scene.Raw();
            const ECS::EntityHandle entity =
                ResolveEntity(raw, key.StableEntityId);
            if (entity == ECS::InvalidEntityHandle)
                return std::nullopt;

            const GS::ConstSourceView view = GS::BuildConstView(raw, entity);
            const Geometry::PropertySet* properties = VertexProperties(view);
            if (!view.Valid() || properties == nullptr)
                return std::nullopt;

            AnalysisSnapshot snapshot{.Key = key};
            const auto* bindings = raw.try_get<VertexChannelBindingSet>(entity);
            snapshot.Normal = CaptureChannel(
                *properties,
                VertexChannel::Normal,
                bindings != nullptr ? &bindings->Normal : nullptr);
            snapshot.Color = CaptureChannel(
                *properties,
                VertexChannel::Color,
                bindings != nullptr ? &bindings->Color : nullptr);

            const GS::SourceAvailability availability =
                GS::BuildSourceAvailability(view);
            snapshot.IsMesh =
                availability.ProvenanceDomain == GS::Domain::Mesh &&
                availability.Has(GS::SourceCapability::VertexPoints) &&
                availability.Has(GS::SourceCapability::Halfedges) &&
                availability.Has(GS::SourceCapability::Faces);
            snapshot.VertexCount = view.VerticesAlive();
            if (snapshot.IsMesh && view.VertexSource != nullptr)
            {
                const auto texcoords =
                    view.VertexSource->Properties.Get<glm::vec2>(
                        "v:texcoord");
                snapshot.HasTexcoords = static_cast<bool>(texcoords);
                if (texcoords)
                    snapshot.Texcoords = texcoords.Vector();
            }
            return snapshot;
        }

        [[nodiscard]] bool IsFinite(const glm::vec2& value) noexcept
        {
            return std::isfinite(value.x) && std::isfinite(value.y);
        }

        [[nodiscard]] bool IsFinite(const glm::vec3& value) noexcept
        {
            return std::isfinite(value.x) && std::isfinite(value.y) &&
                   std::isfinite(value.z);
        }

        [[nodiscard]] bool IsFinite(const glm::vec4& value) noexcept
        {
            return std::isfinite(value.x) && std::isfinite(value.y) &&
                   std::isfinite(value.z) && std::isfinite(value.w);
        }

        [[nodiscard]] SelectedEntityChannelAnalysis AnalyzeChannel(
            const ChannelSnapshot& snapshot,
            const JobCancellation& cancellation,
            std::size_t& elementsScanned)
        {
            SelectedEntityChannelAnalysis analysis{
                .HasBinding = snapshot.HasBinding,
                .Channel = snapshot.Channel,
            };
            if (!snapshot.HasBinding)
                return analysis;

            AttributeBindResult& result = analysis.Resolver;
            if (snapshot.SourceProperty.empty())
            {
                result.Status = AttributeBindStatus::EmptyBinding;
                return analysis;
            }

            const bool allowed =
                (snapshot.Channel == VertexChannel::Normal &&
                 snapshot.SourceType == AttributeSourceType::Vec3) ||
                (snapshot.Channel == VertexChannel::Color &&
                 (snapshot.SourceType == AttributeSourceType::Vec3 ||
                  snapshot.SourceType == AttributeSourceType::Vec4));
            if (!allowed)
            {
                result.Status = AttributeBindStatus::TypeMismatch;
                return analysis;
            }
            if (!snapshot.PropertyExists)
            {
                result.Status = AttributeBindStatus::PropertyMissing;
                return analysis;
            }

            const std::size_t actualCount =
                snapshot.SourceType == AttributeSourceType::Vec4
                ? snapshot.Vec4Values.size()
                : snapshot.Vec3Values.size();
            if (actualCount != snapshot.ExpectedCount ||
                snapshot.ExpectedCount >
                    std::numeric_limits<std::uint32_t>::max())
            {
                result.Status = actualCount == 0u
                    ? AttributeBindStatus::TypeMismatch
                    : AttributeBindStatus::CountMismatch;
                return analysis;
            }

            constexpr float kDegenerateLengthEpsilon = 1.0e-6f;
            for (std::size_t i = 0u; i < actualCount; ++i)
            {
                if ((i & 255u) == 0u && cancellation.IsCancelled())
                    return analysis;

                ++elementsScanned;
                bool finite = false;
                bool degenerate = false;
                if (snapshot.SourceType == AttributeSourceType::Vec4)
                {
                    finite = IsFinite(snapshot.Vec4Values[i]);
                }
                else
                {
                    const glm::vec3 value = snapshot.Vec3Values[i];
                    finite = IsFinite(value);
                    degenerate = snapshot.Channel == VertexChannel::Normal &&
                                 finite &&
                                 glm::length(value) <=
                                     kDegenerateLengthEpsilon;
                }

                if (!finite || degenerate)
                {
                    ++result.FallbackCount;
                    if (!finite)
                        ++result.NonFiniteCount;
                }
                else
                {
                    ++result.SourceCount;
                }
            }
            result.Status = AttributeBindStatus::Bound;
            result.FullyPopulated = true;
            return analysis;
        }

        [[nodiscard]] AnalysisJobResult AnalyzeSnapshot(
            AnalysisSnapshot snapshot,
            const JobCancellation& cancellation)
        {
            AnalysisJobResult job{.Key = snapshot.Key};
            job.Result.Normal = AnalyzeChannel(
                snapshot.Normal,
                cancellation,
                job.Result.ChannelElementsScanned);
            if (cancellation.IsCancelled())
            {
                job.Cancelled = true;
                return job;
            }
            job.Result.Color = AnalyzeChannel(
                snapshot.Color,
                cancellation,
                job.Result.ChannelElementsScanned);
            if (cancellation.IsCancelled())
            {
                job.Cancelled = true;
                return job;
            }

            SelectedEntityUvAnalysis& uv = job.Result.Uv;
            uv.IsMesh = snapshot.IsMesh;
            uv.HasTexcoords = snapshot.HasTexcoords;
            uv.VertexCount = snapshot.VertexCount;
            uv.TexcoordCount = snapshot.Texcoords.size();
            uv.TexcoordCountMatchesVertices =
                uv.HasTexcoords && uv.TexcoordCount == uv.VertexCount;
            uv.TexcoordsFinite = uv.TexcoordCountMatchesVertices;
            if (uv.TexcoordCountMatchesVertices)
            {
                for (std::size_t i = 0u; i < snapshot.Texcoords.size(); ++i)
                {
                    if ((i & 255u) == 0u && cancellation.IsCancelled())
                    {
                        job.Cancelled = true;
                        return job;
                    }
                    ++uv.ElementsScanned;
                    if (!IsFinite(snapshot.Texcoords[i]))
                    {
                        uv.TexcoordsFinite = false;
                        break;
                    }
                }
            }
            return job;
        }
    }

    struct SelectedEntityAnalysisModule::Impl
    {
        struct Slot
        {
            bool Used{false};
            SelectedEntityAnalysisView View{};
            JobToken Job{};
            bool SnapshotRequested{false};
            bool DirtyObserved{false};
        };

        explicit Impl(const std::size_t applyBudget)
            : ApplyBudget(std::max<std::size_t>(1u, applyBudget))
        {
        }

        [[nodiscard]] Slot* FindSlot(
            const WorldHandle world,
            const std::uint32_t stableEntityId,
            const SelectedEntityAnalysisConsumer consumer) noexcept
        {
            const std::size_t index = ConsumerIndex(consumer);
            if (index >= Slots.size())
                return nullptr;
            Slot& slot = Slots[index];
            if (!slot.Used || slot.View.Key.World != world ||
                slot.View.Key.StableEntityId != stableEntityId)
            {
                return nullptr;
            }
            return &slot;
        }

        [[nodiscard]] const Slot* FindSlot(
            const WorldHandle world,
            const std::uint32_t stableEntityId,
            const SelectedEntityAnalysisConsumer consumer) const noexcept
        {
            return const_cast<Impl*>(this)->FindSlot(
                world, stableEntityId, consumer);
        }

        [[nodiscard]] std::uint64_t CurrentRevision(
            const WorldHandle world,
            const std::uint32_t stableEntityId,
            const SelectedEntityAnalysisConsumer consumer) const noexcept
        {
            if (const Slot* slot = FindSlot(world, stableEntityId, consumer))
                return std::max<std::uint64_t>(1u,
                                               slot->View.Key.GeometryRevision);
            return 1u;
        }

        void InvalidateForCurrentSource(Slot& slot,
                                        const std::uint64_t revision,
                                        const std::uint64_t bindingGeneration)
        {
            const bool requestNotYetSubmitted =
                slot.View.State == SelectedEntityAnalysisState::Pending &&
                slot.SnapshotRequested && !slot.Job.IsValid();
            if (requestNotYetSubmitted)
            {
                slot.View.Key.GeometryRevision = revision;
                slot.View.Key.BindingGeneration = bindingGeneration;
                return;
            }

            if (slot.Job.IsValid() && Jobs != nullptr && Jobs->Cancel(slot.Job))
                ++Stats.JobsCancelledForRevision;
            slot.Job = {};
            slot.SnapshotRequested = false;
            slot.View.State = SelectedEntityAnalysisState::Stale;
            slot.View.Key.GeometryRevision = revision;
            slot.View.Key.BindingGeneration = bindingGeneration;
            slot.View.PreviousReadyRetained =
                slot.View.Result.Normal.HasBinding ||
                slot.View.Result.Color.HasBinding ||
                slot.View.Result.Uv.IsMesh;
            slot.View.Diagnostic =
                "selected geometry changed; analysis result is stale";
        }

        void ObserveRevisions(RuntimeFrameHookContext& context)
        {
            for (Slot& slot : Slots)
            {
                if (!slot.Used ||
                    slot.View.Key.World != context.ActiveWorldHandle)
                {
                    continue;
                }

                entt::registry& raw = context.ActiveWorld.Raw();
                const ECS::EntityHandle entity = ResolveEntity(
                    raw, slot.View.Key.StableEntityId);
                if (entity == ECS::InvalidEntityHandle)
                {
                    InvalidateForCurrentSource(
                        slot,
                        slot.View.Key.GeometryRevision + 1u,
                        0u);
                    continue;
                }

                std::uint64_t revision = slot.View.Key.GeometryRevision;
                const bool dirty = HasAnalysisDirtyTag(raw, entity);
                if (dirty && !slot.DirtyObserved)
                {
                    ++revision;
                    ++Stats.GeometryRevisionAdvances;
                }
                slot.DirtyObserved = dirty;
                const std::uint64_t bindings = BindingGeneration(raw, entity);
                if (revision != slot.View.Key.GeometryRevision ||
                    bindings != slot.View.Key.BindingGeneration)
                {
                    InvalidateForCurrentSource(slot, revision, bindings);
                }
            }
        }

        void CaptureRequestedSnapshots(RuntimeFrameHookContext& context)
        {
            for (Slot& slot : Slots)
            {
                if (!slot.Used || !slot.SnapshotRequested ||
                    slot.View.State != SelectedEntityAnalysisState::Pending)
                {
                    continue;
                }
                slot.SnapshotRequested = false;

                if (slot.View.Key.World != context.ActiveWorldHandle)
                {
                    slot.View.State = SelectedEntityAnalysisState::Failed;
                    slot.View.Diagnostic =
                        "selected analysis request no longer targets the active world";
                    continue;
                }

                std::optional<AnalysisSnapshot> snapshot = CaptureSnapshot(
                    context.ActiveWorld, slot.View.Key);
                if (!snapshot.has_value())
                {
                    slot.View.State = SelectedEntityAnalysisState::Failed;
                    slot.View.Diagnostic =
                        "selected entity geometry snapshot is unavailable";
                    continue;
                }
                ++Stats.SnapshotsCaptured;

                slot.Job = context.Jobs.Submit(
                    MakeCpuJobDesc<AnalysisJobResult>(
                        "Runtime.SelectedEntityAnalysis.CPU",
                        slot.View.Key.World,
                        [snapshot = std::move(*snapshot)](
                            const JobCancellation& cancellation) mutable
                        {
                            return AnalyzeSnapshot(
                                std::move(snapshot), cancellation);
                        },
                        [](const AnalysisJobResult& result)
                        {
                            return SelectedEntityAnalysisCompleted{
                                .Job = result};
                        }));
                if (!slot.Job.IsValid())
                {
                    ++Stats.JobSubmissionFailures;
                    slot.View.State = SelectedEntityAnalysisState::Failed;
                    slot.View.Diagnostic =
                        "selected entity analysis job submission was rejected";
                    continue;
                }
                ++Stats.JobsSubmitted;
            }
        }

        void ApplyCompleted(RuntimeFrameHookContext& context)
        {
            std::size_t applied = 0u;
            while (applied < ApplyBudget && !PendingResults.empty())
            {
                AnalysisJobResult job = std::move(PendingResults.front());
                PendingResults.pop_front();

                Slot* slot = FindSlot(
                    job.Key.World,
                    job.Key.StableEntityId,
                    job.Key.Consumer);
                const bool identityMatches =
                    slot != nullptr && slot->View.Key == job.Key;
                bool liveSourceMatches = false;
                if (identityMatches && !job.Cancelled &&
                    job.Key.World == context.ActiveWorldHandle)
                {
                    entt::registry& raw = context.ActiveWorld.Raw();
                    const ECS::EntityHandle entity = ResolveEntity(
                        raw, job.Key.StableEntityId);
                    if (entity != ECS::InvalidEntityHandle)
                    {
                        const bool dirty = HasAnalysisDirtyTag(raw, entity);
                        if (dirty && !slot->DirtyObserved)
                        {
                            slot->DirtyObserved = true;
                            ++Stats.GeometryRevisionAdvances;
                            InvalidateForCurrentSource(
                                *slot,
                                slot->View.Key.GeometryRevision + 1u,
                                BindingGeneration(raw, entity));
                        }
                        else if (!dirty)
                        {
                            slot->DirtyObserved = false;
                        }
                    }
                    liveSourceMatches =
                        entity != ECS::InvalidEntityHandle &&
                        slot->View.Key == job.Key &&
                        BindingGeneration(raw, entity) ==
                            job.Key.BindingGeneration;
                }

                if (!identityMatches || !liveSourceMatches ||
                    job.Cancelled ||
                    slot->View.State != SelectedEntityAnalysisState::Pending)
                {
                    ++Stats.StaleResultsRejected;
                    ++applied;
                    continue;
                }

                slot->Job = {};
                slot->View.State = SelectedEntityAnalysisState::Ready;
                slot->View.Result = std::move(job.Result);
                slot->View.PreviousReadyRetained = false;
                slot->View.Diagnostic.clear();
                ++Stats.ResultsApplied;
                ++applied;
            }

            Stats.LastUiBuildApplied = applied;
            Stats.MaxUiBuildApplied =
                std::max<std::uint64_t>(Stats.MaxUiBuildApplied, applied);
            if (!PendingResults.empty())
                Stats.DeferredApplyResults += PendingResults.size();
            Stats.PendingApplyResults = PendingResults.size();
        }

        std::size_t ApplyBudget{2u};
        CommandBus* Commands{};
        KernelEventBus* Events{};
        JobService* Jobs{};
        WorldRegistry* Worlds{};
        std::array<Slot, 4u> Slots{};
        std::deque<AnalysisJobResult> PendingResults{};
        KernelEventSubscription CompletionSubscription{};
        SelectedEntityAnalysisStats Stats{};
        std::uint64_t NextRequestIdentity{1u};
    };

    bool SelectedEntityAnalysisService::Available() const noexcept
    {
        return m_Module != nullptr;
    }

    SelectedEntityAnalysisView SelectedEntityAnalysisService::Request(
        const WorldHandle world,
        const std::uint32_t stableEntityId,
        const SelectedEntityAnalysisConsumer consumer)
    {
        return m_Module != nullptr
            ? m_Module->Request(world, stableEntityId, consumer)
            : SelectedEntityAnalysisView{};
    }

    SelectedEntityAnalysisView SelectedEntityAnalysisService::Query(
        const WorldHandle world,
        const std::uint32_t stableEntityId,
        const SelectedEntityAnalysisConsumer consumer) const
    {
        return m_Module != nullptr
            ? m_Module->Query(world, stableEntityId, consumer)
            : SelectedEntityAnalysisView{};
    }

    SelectedEntityAnalysisStats SelectedEntityAnalysisService::Stats() const noexcept
    {
        return m_Module != nullptr
            ? m_Module->Stats()
            : SelectedEntityAnalysisStats{};
    }

    void SelectedEntityAnalysisService::Bind(
        SelectedEntityAnalysisModule* module) noexcept
    {
        m_Module = module;
    }

    SelectedEntityAnalysisModule::SelectedEntityAnalysisModule(
        const std::size_t uiBuildApplyBudget)
        : m_Impl(std::make_unique<Impl>(uiBuildApplyBudget))
    {
    }

    SelectedEntityAnalysisModule::~SelectedEntityAnalysisModule() = default;

    std::string_view SelectedEntityAnalysisModule::Name() const noexcept
    {
        return "Runtime.SelectedEntityAnalysisModule";
    }

    Core::Result SelectedEntityAnalysisModule::OnRegister(EngineSetup& setup)
    {
        m_Impl->Commands = &setup.Commands();
        m_Impl->Events = &setup.Events();
        m_Impl->Jobs = &setup.Jobs();
        m_Impl->Worlds = &setup.Worlds();
        m_Service.Bind(this);

        if (Core::Result provided =
                setup.Services().Provide<SelectedEntityAnalysisService>(
                    m_Service, Name());
            !provided.has_value())
        {
            m_Service.Bind(nullptr);
            return provided;
        }

        setup.RegisterCommandHandler<RequestSelectedEntityAnalysis>(
            [this](CommandContext&,
                   const RequestSelectedEntityAnalysis& command)
                -> CommandOutcome
            {
                ++m_Impl->Stats.CommandsHandled;
                Impl::Slot* slot = m_Impl->FindSlot(
                    command.Key.World,
                    command.Key.StableEntityId,
                    command.Key.Consumer);
                if (slot == nullptr ||
                    slot->View.Key.RequestIdentity !=
                        command.Key.RequestIdentity ||
                    slot->View.State !=
                        SelectedEntityAnalysisState::Pending)
                {
                    return CommandOutcome::Ok();
                }
                slot->SnapshotRequested = true;
                return CommandOutcome::Ok();
            });

        m_Impl->CompletionSubscription =
            setup.Subscribe<SelectedEntityAnalysisCompleted>(
                [this](const SelectedEntityAnalysisCompleted& completed)
                {
                    ++m_Impl->Stats.CompletionEvents;
                    m_Impl->PendingResults.push_back(completed.Job);
                    m_Impl->Stats.PendingApplyResults =
                        m_Impl->PendingResults.size();
                });

        if (Core::Result registered = setup.RegisterFrameHook(
                FramePhase::AfterCommandDrain,
                [this](RuntimeFrameHookContext& context)
                {
                    m_Impl->ObserveRevisions(context);
                    m_Impl->CaptureRequestedSnapshots(context);
                });
            !registered.has_value())
        {
            return registered;
        }
        return setup.RegisterFrameHook(
            FramePhase::UiBuild,
            [this](RuntimeFrameHookContext& context)
            {
                m_Impl->ApplyCompleted(context);
            });
    }

    Core::Result SelectedEntityAnalysisModule::OnResolve(EngineSetup& setup)
    {
        (void)setup;
        return Core::Ok();
    }

    void SelectedEntityAnalysisModule::OnShutdown(
        RuntimeModuleShutdownContext& context)
    {
        for (Impl::Slot& slot : m_Impl->Slots)
        {
            if (slot.Job.IsValid())
                (void)context.Jobs.Cancel(slot.Job);
            slot = {};
        }
        if (m_Impl->CompletionSubscription.IsValid())
            context.Events.Unsubscribe(m_Impl->CompletionSubscription);
        m_Impl->CompletionSubscription = {};
        m_Impl->PendingResults.clear();
        m_Impl->Commands = nullptr;
        m_Impl->Events = nullptr;
        m_Impl->Jobs = nullptr;
        m_Impl->Worlds = nullptr;
        m_Service.Bind(nullptr);
    }

    SelectedEntityAnalysisStats SelectedEntityAnalysisModule::Stats() const noexcept
    {
        return m_Impl != nullptr ? m_Impl->Stats
                                 : SelectedEntityAnalysisStats{};
    }

    SelectedEntityAnalysisView SelectedEntityAnalysisModule::Request(
        const WorldHandle world,
        const std::uint32_t stableEntityId,
        const SelectedEntityAnalysisConsumer consumer)
    {
        if (m_Impl == nullptr || m_Impl->Commands == nullptr ||
            !world.IsValid() || stableEntityId == 0u ||
            ConsumerIndex(consumer) >= m_Impl->Slots.size())
        {
            return SelectedEntityAnalysisView{};
        }

        ++m_Impl->Stats.Requests;
        if (Impl::Slot* existing =
                m_Impl->FindSlot(world, stableEntityId, consumer))
        {
            if (existing->View.State ==
                SelectedEntityAnalysisState::Pending)
            {
                ++m_Impl->Stats.RequestDeduplications;
                return existing->View;
            }
            if (existing->View.State == SelectedEntityAnalysisState::Ready)
            {
                ++m_Impl->Stats.RequestDeduplications;
                ++m_Impl->Stats.ReadyCacheHits;
                return existing->View;
            }
        }

        Impl::Slot& slot = m_Impl->Slots[ConsumerIndex(consumer)];
        if (slot.Job.IsValid() && m_Impl->Jobs != nullptr)
            (void)m_Impl->Jobs->Cancel(slot.Job);

        SelectedEntityAnalysisResult previous{};
        bool retained = false;
        bool dirtyObserved = false;
        std::uint64_t revision = 1u;
        std::uint64_t bindingGeneration = 0u;
        if (slot.Used && slot.View.Key.World == world &&
            slot.View.Key.StableEntityId == stableEntityId)
        {
            previous = slot.View.Result;
            retained = slot.View.State == SelectedEntityAnalysisState::Stale ||
                       slot.View.State == SelectedEntityAnalysisState::Ready;
            revision = std::max<std::uint64_t>(
                1u, slot.View.Key.GeometryRevision);
            bindingGeneration = slot.View.Key.BindingGeneration;
            dirtyObserved = slot.DirtyObserved;
        }
        else if (m_Impl->Worlds != nullptr)
        {
            if (ECS::Scene::Registry* scene = m_Impl->Worlds->Get(world))
            {
                const ECS::EntityHandle entity = ResolveEntity(
                    scene->Raw(), stableEntityId);
                if (entity != ECS::InvalidEntityHandle)
                    bindingGeneration = BindingGeneration(
                        scene->Raw(), entity);
            }
        }

        slot = Impl::Slot{
            .Used = true,
            .View = SelectedEntityAnalysisView{
                .State = SelectedEntityAnalysisState::Pending,
                .Key = SelectedEntityAnalysisKey{
                    .World = world,
                    .StableEntityId = stableEntityId,
                    .Consumer = consumer,
                    .GeometryRevision = revision,
                    .BindingGeneration = bindingGeneration,
                    .RequestIdentity = m_Impl->NextRequestIdentity++,
                },
                .Result = std::move(previous),
                .PreviousReadyRetained = retained,
                .Diagnostic = "selected entity analysis is pending",
            },
            .DirtyObserved = dirtyObserved,
        };
        (void)m_Impl->Commands->Enqueue(RequestSelectedEntityAnalysis{
            .Key = slot.View.Key});
        return slot.View;
    }

    SelectedEntityAnalysisView SelectedEntityAnalysisModule::Query(
        const WorldHandle world,
        const std::uint32_t stableEntityId,
        const SelectedEntityAnalysisConsumer consumer) const
    {
        if (m_Impl != nullptr)
        {
            if (const Impl::Slot* slot =
                    m_Impl->FindSlot(world, stableEntityId, consumer))
            {
                return slot->View;
            }
        }
        return SelectedEntityAnalysisView{};
    }
}
