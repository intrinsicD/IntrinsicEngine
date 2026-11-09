module;
#include <atomic>
#include <chrono>
#include <cstring>
#include <functional>
#include <entt/entity/fwd.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <entt/entity/registry.hpp>
#include <glm/geometric.hpp>
#include <glm/matrix.hpp>
#include <glm/mat3x3.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <glm/gtc/quaternion.hpp>

module Extrinsic.Runtime.RegistrationOperations;

import Geometry.Validation;
import Extrinsic.Core.Dag.Scheduler;
import Extrinsic.Core.Error;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.ECS.Component.Transform;
import Extrinsic.Runtime.EditorCommon;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.EditorJobProjection;
import Extrinsic.Runtime.GeometryAvailability;
import Extrinsic.Runtime.GeometryPresentation;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.WorldHandle;
import Geometry.Properties;
import Extrinsic.Runtime.SpatialIndexCache;
import Extrinsic.Runtime.StepRuns;
import Extrinsic.RHI.Device;
import Geometry.Registration;
import Geometry.PointLBVH;

#include "Editor/internal/Runtime.EditorGeometryHelpers.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.JobFailure.hpp"
#include "Editor/internal/Runtime.EditorTransformHelpers.hpp"

#include "Editor/internal/Runtime.EditorProcessingAccess.hpp"

#include "Editor/Operations/Runtime.GeometryProcessingOperations.PointFields.hpp"

namespace Extrinsic::Runtime
{
    // The panel's view of one ICP run: written on the main thread only (the run's start and its Show).
    struct EditorRegistrationProgress
    {
        EditorRegistrationProgressSnapshot Snapshot{};
    };

extern "C++"
{
namespace GeometryProcessingDetail::MeshSupport
{

        using EditorFeatureDetail::ResolveStableEntity;
        using EditorFeatureDetail::ToEditorCommandStatus;
        using EditorFeatureDetail::SameTransformComponent;
        using EditorFeatureDetail::ExecuteEditorTransformMutation;
        inline constexpr std::array<EditorICPVariant, 2>
            kEditorICPVariants{{
                EditorICPVariant::PointToPoint,
                EditorICPVariant::PointToPlane,
            }};

        namespace ECSC = Extrinsic::ECS::Components;
namespace Reg = Geometry::Registration;

        // Point-to-plane ICP requires a validated target-normal span. Invalid
        // normals fail closed before `Geometry.Registration` can select its
        // point-to-point fallback; this status lets the editor name the exact
        // rejection cause.
        enum class RegistrationNormalStatus : std::uint8_t
        {
            Ok,
            Absent,
            CountMismatch,
            NonFinite,
            ZeroLength,
            TargetTransformNotInvertible,
        };

        [[nodiscard]] const char* DescribeRegistrationNormalStatus(
            const RegistrationNormalStatus status) noexcept
        {
            switch (status)
            {
            case RegistrationNormalStatus::Ok:
                return "";
            case RegistrationNormalStatus::Absent:
                return "the target normal binding is missing, non-finite or not float3";
            case RegistrationNormalStatus::CountMismatch:
                return "the target v:normal property does not carry exactly "
                       "one vector per target point";
            case RegistrationNormalStatus::NonFinite:
                return "the target v:normal property contains a non-finite "
                       "value";
            case RegistrationNormalStatus::ZeroLength:
                return "the target normal binding contains a zero-length "
                       "vector";
            case RegistrationNormalStatus::TargetTransformNotInvertible:
                return "the target entity transform is not invertible, so "
                       "normals cannot be carried into world space";
            }
            return "the target normals are unusable";
        }

        // Normals transform by the inverse transpose of the model's linear
        // part, not by the model matrix itself: under non-uniform scale the
        // two disagree, and using the position transform would tilt every
        // normal off the surface it describes.
        template <class NormalAt>
        [[nodiscard]] RegistrationNormalStatus TransformRegistrationNormalRows(
            std::size_t count, NormalAt normalAt, const glm::mat4& model,
            std::vector<glm::vec3>* out = nullptr)
        {
            if (out) out->clear();
            const glm::mat3 linear{model};
            const float determinant = glm::determinant(linear);
            if (!std::isfinite(determinant) || determinant == 0.0f)
                return RegistrationNormalStatus::TargetTransformNotInvertible;

            const glm::mat3 normalMatrix = glm::transpose(glm::inverse(linear));
            if (out) out->reserve(count);
            for (std::size_t i = 0; i < count; ++i)
            {
                const auto local = normalAt(i);
                if (!local) continue;
                const glm::vec3 world = normalMatrix * *local;
                const float lengthSquared = glm::dot(world, world);
                if (!std::isfinite(lengthSquared))
                {
                    if (out) out->clear();
                    return RegistrationNormalStatus::NonFinite;
                }
                if (lengthSquared <= 0.0f)
                {
                    if (out) out->clear();
                    return RegistrationNormalStatus::ZeroLength;
                }
                const glm::vec3 normalized = world / std::sqrt(lengthSquared);
                if (!Geometry::Validation::IsFinite(normalized))
                {
                    if (out) out->clear();
                    return RegistrationNormalStatus::NonFinite;
                }
                if (out) out->push_back(normalized);
            }
            return RegistrationNormalStatus::Ok;
        }

        [[nodiscard]] RegistrationNormalStatus TransformRegistrationNormalsToWorld(
            const std::vector<glm::vec3>& normals, const glm::mat4& model,
            std::vector<glm::vec3>* out = nullptr)
        {
            return TransformRegistrationNormalRows(normals.size(),
                [&](std::size_t i) { return std::optional{normals[i]}; }, model, out);
        }

        [[nodiscard]] std::string BuildRegistrationNormalRejectionMessage(
            const RegistrationNormalStatus status)
        {
            std::string message =
                "Point-to-plane ICP registration requires target normals: ";
            message += DescribeRegistrationNormalStatus(status);
            message += ". Estimate point-cloud normals on the target, or "
                       "select the point-to-point variant.";
            return message;
        }

        [[nodiscard]] bool ValidEditorICPVariant(
            const EditorICPVariant variant) noexcept
        {
            return std::find(kEditorICPVariants.begin(),
                             kEditorICPVariants.end(),
                             variant) != kEditorICPVariants.end();
        }

        [[nodiscard]] Reg::ICPVariant ToGeometryICPVariant(
            const EditorICPVariant variant) noexcept
        {
            return variant == EditorICPVariant::PointToPlane
                       ? Reg::ICPVariant::PointToPlane
                       : Reg::ICPVariant::PointToPoint;
        }

        [[nodiscard]] std::string BuildRegistrationSuccessMessage(
            const EditorRegistrationResult& result)
        {
            std::string message = "ICP registration completed (variant=";
            message += DebugNameForEditorICPVariant(result.Variant);
            message += ", iterations=";
            message += std::to_string(result.IterationsPerformed);
            message += ", step=";
            message += std::to_string(result.AppliedStep);
            message += "/";
            message += std::to_string(result.TrajectoryLength);
            message += ", converged=";
            message += result.Converged ? "yes" : "no";
            message += ").";
            return message;
        }

        [[nodiscard]] glm::vec3 ComputePointCentroid(
            const std::span<const glm::vec3> points) noexcept
        {
            if (points.empty())
                return glm::vec3(0.0f);

            glm::dvec3 sum(0.0);
            for (const glm::vec3& point : points)
                sum += glm::dvec3(point);
            return glm::vec3(sum / static_cast<double>(points.size()));
        }

        [[nodiscard]] bool IsVulkanRegistrationBackend(const RegistrationBackend backend) noexcept
        {
            return backend == RegistrationBackend::VulkanLBVH || backend == RegistrationBackend::VulkanBruteForce;
        }

        // Why Vulkan ICP correspondences cannot run here (empty: they can). Each iteration's
        // queries are queued from the main thread, so they need the job lane, the spatial index
        // service and an operational device.
        [[nodiscard]] std::string VulkanCorrespondencesUnavailable(const EditorProcessingContext& context)
        {
            if (!context.JobCommands.Available()) return "No job lane";
            if (context.SpatialIndices == nullptr) return "No spatial index service";
            if (context.Device == nullptr || !context.Device->IsOperational()) return "No operational Vulkan device";
            return {};
        }

        [[nodiscard]] EditorRegistrationResult
        MakeRegistrationBaseResult(
            const EditorRegistrationCommand& command)
        {
            return EditorRegistrationResult{
                .Status = EditorCommandStatus::NoChange,
                .RequestedBackend = command.Backend,
                .Variant = command.Variant,
                // A result that never reached the solver has run
                // nothing, so the effective variant stays point-to-point until
                // a validated normal span makes it point-to-plane.
                .EffectiveVariant = EditorICPVariant::PointToPoint,
                .Error = Core::ErrorCode::Success,
            };
        }

        [[nodiscard]] EditorRegistrationResult
        MakePendingRegistrationResult(
            const EditorRegistrationCommand& command,
            const std::size_t sourcePointCount,
            const std::size_t targetPointCount,
            const JobToken handle)
        {
            EditorRegistrationResult result =
                MakeRegistrationBaseResult(command);
            result.Status = EditorCommandStatus::Pending;
            result.SourcePointCount = sourcePointCount;
            result.TargetPointCount = targetPointCount;
            result.Message = "ICP registration CPU job queued";
            AppendDerivedJobHandleToMessage(result.Message, handle);
            result.Message += ".";
            return result;
        }

        GeometryPropertyRef ResolveRegistrationDefault(const GeometryEntityAvailability& available, GeometryPropertyRef ref)
        {
            if (ref.Domain == GeometryElementDomain::Unknown)
                for (auto domain : {GeometryElementDomain::PointCloudPoint, GeometryElementDomain::MeshVertex,
                                    GeometryElementDomain::GraphNode})
                    if (SupportsGeometryElementDomain(available, domain)) { ref.Domain = domain; break; }
            return ref;
        }
        // One ICP run (ARCH-023): a step run whose step is one ICP iteration (Geometry::Registration::AdvanceICP),
        // always on a worker. A CPU run queries the shared LBVH snapshot or its KD-tree; a Vulkan run queries the
        // target through the worker query session prepared at its start (one page per iteration, submitted and waited
        // for on the worker: no pump, frame or main-thread callback). Nothing is published before the run's one Accept.
        //
        // Written once on the main thread before the run starts: the context, command, captures, solve inputs, Base
        // and Session. Query, Solve, Trajectory, Started/Last and StepFailure are the iteration state the Step changes
        // on a worker; Preview, Commit and Restart touch them only while no step runs (StepRunsService's guarantee).
        // Valid, Show and Discard read the captures alone (Discard sets Abort, which a waiting page observes; the
        // result waits for Finished). Sink and Delivered are main-thread state.
        struct IcpRun
        {
            EditorProcessingContext Context{};
            EditorRegistrationCommand Command{};
            ECS::EntityHandle SourceEntity{entt::null}, TargetEntity{entt::null};
            // Every binding owns local rows, source-row IDs and revision watches; point-to-point leaves normals empty.
            PointInputCapture SourceBinding{}, TargetBinding{}, NormalBinding{};
            ECSC::Transform::Component SourceBeforeTransform{};
            bool TargetHadTransform{false};
            ECSC::Transform::Component TargetBeforeTransform{};
            EditorRegistrationResult Base{}; // backends, counts and effective variant; no iteration data
            std::vector<glm::vec3> SourceWorld{}, TargetWorld{}, WorldNormals{}; // the source pre-aligned
            glm::mat4 PrealignPose{1.f};
            Reg::RegistrationParams Params{};
            SpatialIndexHandle TargetIndex{};
            std::shared_ptr<const SpatialIndexSnapshot> IndexSnapshot{};

            Reg::NearestQuery Query{}; // a CPU run's correspondences, built by its first step
            Reg::RegistrationResult Solve{};
            std::vector<glm::dmat4> Trajectory{}; // the cumulative pose after each iteration
            std::chrono::steady_clock::time_point Started{}, Last{};
            std::string StepFailure{}; // why a failed step failed

            // Vulkan: the prepared correspondence session (its target, poses or uploads, one page an iteration), kept
            // across iterations and restarts and released by the cache once this run is gone and its page completed.
            std::shared_ptr<SpatialQuerySession> Session{};
            bool Float64{false}; // the device transforms the queries (the pose per page); else the step uploads them
            std::atomic<bool> Abort{false};
            SpatialNearestBatch Page{}; // the worker's page scratch
            StepRunDriver Driver{};
            std::uint64_t Run{0u};
            std::function<void(EditorRegistrationResult)> Sink{};
            bool Delivered{false};
            EditorRegistrationProgressHandle Progress{}; // the panel's view, optional
        };

        inline constexpr std::string_view kIcpJobLabel = "Sandbox.RegistrationICP";

        [[nodiscard]] std::vector<glm::vec3> TransformPointsToWorld(
            const std::vector<glm::vec3>& points,
            const ECSC::Transform::Component& transform)
        {
            const glm::mat4 model = ECSC::Transform::GetMatrix(transform);
            std::vector<glm::vec3> world;
            world.reserve(points.size());
            for (const glm::vec3& point : points)
                world.push_back(glm::vec3(model * glm::vec4(point, 1.0f)));
            return world;
        }

        // The solve inputs in world space, the source pre-aligned to the target's centroid. O(n) on the main thread,
        // like the capture itself. False with the reason when the normals cannot be carried into world space.
        [[nodiscard]] bool PrepareIcpRun(IcpRun& run, std::string& rejection)
        {
            run.SourceWorld = TransformPointsToWorld(run.SourceBinding.Points, run.SourceBeforeTransform);
            run.TargetWorld = run.TargetHadTransform
                ? TransformPointsToWorld(run.TargetBinding.Points, run.TargetBeforeTransform)
                : run.TargetBinding.Points;
            const glm::vec3 delta = ComputePointCentroid(run.TargetWorld) - ComputePointCentroid(run.SourceWorld);
            for (glm::vec3& point : run.SourceWorld)
                point += delta;
            run.PrealignPose = glm::mat4(1.0f);
            run.PrealignPose[3] = glm::vec4(delta, 1.0f);
            if (!run.NormalBinding.Points.empty())
            {
                const glm::mat4 model =
                    run.TargetHadTransform ? ECSC::Transform::GetMatrix(run.TargetBeforeTransform) : glm::mat4(1.0f);
                const RegistrationNormalStatus status =
                    TransformRegistrationNormalsToWorld(run.NormalBinding.Points, model, &run.WorldNormals);
                if (status != RegistrationNormalStatus::Ok)
                {
                    rejection = BuildRegistrationNormalRejectionMessage(status);
                    return false;
                }
            }
            run.Base.TargetNormalCount = run.WorldNormals.size();
            run.Base.EffectiveVariant = run.WorldNormals.size() == run.TargetWorld.size() && !run.WorldNormals.empty()
                ? EditorICPVariant::PointToPlane
                : EditorICPVariant::PointToPoint;
            run.Params.Variant = ToGeometryICPVariant(run.Command.Variant);
            run.Params.MaxIterations = run.Command.Execution.IterationCap; // the run's cap is the solver's
            run.Params.ConvergenceThreshold = run.Command.ConvergenceThreshold;
            run.Params.MaxCorrespondenceDistance =
                run.Command.MaxCorrespondenceDistance > 0.0 ? run.Command.MaxCorrespondenceDistance : 1.0e6;
            run.Params.InlierRatio = run.Command.InlierRatio;
            return true;
        }

        [[nodiscard]] double Seconds(const std::chrono::steady_clock::duration duration) noexcept
        {
            return std::chrono::duration<double>(duration).count();
        }

        void BeginIcpTiming(IcpRun& run)
        {
            if (run.Started == std::chrono::steady_clock::time_point{})
                run.Started = run.Last = std::chrono::steady_clock::now();
        }

        // One ICP iteration from `indices` (per source point a compact target index, or UINT32_MAX). Metrics: rmse,
        // inliers, seconds since the first step and this step's seconds. A solve that ends without iterating (fewer
        // than three correspondences) cannot move further and ends the run as converged; the iteration cap is the
        // run's own (Params.MaxIterations is Execution.IterationCap), so the solver's cap continues into it.
        [[nodiscard]] StepOutcome AdvanceIcpRun(
            IcpRun& run, const std::span<const std::uint32_t> indices, StepRunMetric& metric)
        {
            const std::size_t before = run.Solve.IterationsPerformed;
            const auto status = Reg::AdvanceICP(run.SourceWorld,
                run.TargetWorld,
                run.WorldNormals,
                run.Params,
                indices,
                run.Solve,
                [&run](const Reg::IterationTrace& trace)
                {
                    run.Trajectory.push_back(trace.Transform);
                });
            if (status == Reg::ICPStepStatus::InvalidInput)
            {
                run.StepFailure = "ICP rejected its correspondences";
                return StepOutcome::Failed;
            }
            const auto now = std::chrono::steady_clock::now();
            metric.Values = {run.Solve.FinalRMSE,
                double(run.Solve.FinalInlierCount),
                Seconds(now - run.Started),
                Seconds(now - run.Last)};
            run.Last = now;
            if (status == Reg::ICPStepStatus::Finished &&
                (run.Solve.Converged || run.Solve.IterationsPerformed == before))
                return StepOutcome::Converged;
            return StepOutcome::Continue;
        }

        // A CPU run's step, on a worker: the shared target LBVH snapshot, else the KD-tree AlignICP builds.
        [[nodiscard]] StepOutcome StepIcpOnCpu(IcpRun& run, StepRunMetric& metric)
        {
            BeginIcpTiming(run);
            if (!run.Query)
            {
                if (run.IndexSnapshot)
                    run.Query = [index = run.IndexSnapshot](
                                    std::span<const glm::vec3> queries, std::span<std::uint32_t> ids)
                    {
                        for (std::size_t i = 0; i < queries.size(); ++i)
                        {
                            if (!Geometry::PointLBVH::ValidPoint(queries[i]))
                                return false;
                            ids[i] = index->Index.Nearest(queries[i]).Index;
                        }
                        return true;
                    };
                else
                    run.Query = Reg::MakeKDTreeNearestQuery(run.TargetWorld, run.Params.KDTreeLeafSize);
                if (!run.Query)
                {
                    run.StepFailure = "the target KD-tree could not be built";
                    return StepOutcome::Failed;
                }
            }
            const auto queries = Reg::MakeICPQueries(run.SourceWorld, run.Solve.Transform);
            std::vector<std::uint32_t> indices(queries.size(), std::numeric_limits<std::uint32_t>::max());
            if (!run.Query(queries, indices))
            {
                run.StepFailure = "a correspondence query was refused (a non-finite point)";
                return StepOutcome::Failed;
            }
            return AdvanceIcpRun(run, indices, metric);
        }

        void ShowIcpPose(IcpRun& run, const glm::mat4& pose)
        {
            if (!run.Progress)
                return;
            run.Progress->Snapshot.Pose = pose;
            ++run.Progress->Snapshot.Revision;
        }

        // A Vulkan run's step, on a worker: one correspondence page over the prepared session (the device's query
        // precision: a float64 device transforms the fixed source by the pose itself, else the transformed queries are
        // uploaded on the compute lane), waited for here, then the iteration solved here. Owner state is never touched.
        [[nodiscard]] StepOutcome StepIcpOnGpu(IcpRun& run, StepRunMetric& metric)
        {
            BeginIcpTiming(run);
            auto& session = *run.Session;
            if (!run.Float64 &&
                !session.UploadQueries(Reg::MakeICPQueries(run.SourceWorld, run.Solve.Transform), run.StepFailure))
                return StepOutcome::Failed;
            const auto status = session.Query(
                0u, session.Rows(), -1.f, run.Float64 ? &run.Solve.Transform : nullptr, run.Abort, run.Page);
            if (status == SpatialPageStatus::Abandoned)
                return StepOutcome::Failed; // discarded: the run ended already, nothing to report
            if (status == SpatialPageStatus::Failed)
            {
                run.StepFailure = run.Page.Diagnostic;
                return StepOutcome::Failed;
            }
            std::vector<std::uint32_t> indices;
            indices.reserve(run.Page.Neighbors.size());
            const auto& slots = run.IndexSnapshot->Slots;
            for (const auto neighbor : run.Page.Neighbors)
            {
                const auto found = std::lower_bound(slots.begin(), slots.end(), neighbor.Index);
                indices.push_back(found != slots.end() && *found == neighbor.Index
                        ? static_cast<std::uint32_t>(found - slots.begin())
                        : std::numeric_limits<std::uint32_t>::max());
            }
            return AdvanceIcpRun(run, indices, metric);
        }

        // Full revalidation before publication (the run's Valid checks revisions every frame; values, row maps and
        // transforms are compared here, once). The scene is read only after the attachment epoch check.
        [[nodiscard]] JobApplyValidation ValidateIcpRunInputs(const IcpRun& run)
        {
            const auto& context = run.Context;
            if (context.AttachmentActive && !context.AttachmentActive())
                return JobApplyValidation::StaleWorld;
            if (context.Scene == nullptr)
                return JobApplyValidation::MissingTarget;
            entt::registry& raw = context.Scene->Raw();
            const auto sourceEntity = ResolveStableEntity(raw, run.Command.SourceStableEntityId);
            const auto targetEntity = ResolveStableEntity(raw, run.Command.TargetStableEntityId);
            if (!sourceEntity || !targetEntity || *sourceEntity != run.SourceEntity ||
                *targetEntity != run.TargetEntity)
                return JobApplyValidation::MissingTarget;
            const auto current = [&](ECS::EntityHandle entity, GeometryPropertyRef ref, const PointInputCapture& before)
            {
                PointInputCapture now;
                std::string diagnostic;
                return CapturePointInput(BuildGeometryAvailability(raw, entity), ref, true, now, diagnostic) &&
                    now.Inputs == before.Inputs && now.Slots == before.Slots &&
                    SameGeometryPositions(now.Points, before.Points);
            };
            if (!current(*sourceEntity, run.Command.SourcePositions, run.SourceBinding) ||
                !current(*targetEntity, run.Command.TargetPositions, run.TargetBinding) ||
                (!run.NormalBinding.Points.empty() &&
                    !current(*targetEntity, run.Command.TargetNormals, run.NormalBinding)))
                return JobApplyValidation::StaleGeneration;
            const auto* sourceTransform = raw.try_get<ECSC::Transform::Component>(*sourceEntity);
            if (sourceTransform == nullptr || !SameTransformComponent(*sourceTransform, run.SourceBeforeTransform))
                return JobApplyValidation::StaleGeneration;
            const auto* targetTransform = raw.try_get<ECSC::Transform::Component>(*targetEntity);
            if (targetTransform == nullptr)
                return run.TargetHadTransform ? JobApplyValidation::StaleGeneration : JobApplyValidation::Current;
            if (!run.TargetHadTransform || !SameTransformComponent(*targetTransform, run.TargetBeforeTransform))
                return JobApplyValidation::StaleGeneration;
            return JobApplyValidation::Current;
        }

        // The run's Valid, every frame: the world, both entities, the input revisions and both transforms.
        [[nodiscard]] bool IcpCapturesCurrent(const IcpRun& run)
        {
            const auto& context = run.Context;
            if ((context.AttachmentActive && !context.AttachmentActive()) || context.Scene == nullptr ||
                !EditorProcessingContextWorldCurrent(context))
                return false;
            entt::registry& raw = context.Scene->Raw();
            const auto source = ResolveStableEntity(raw, run.Command.SourceStableEntityId);
            const auto target = ResolveStableEntity(raw, run.Command.TargetStableEntityId);
            if (!source || !target || *source != run.SourceEntity || *target != run.TargetEntity ||
                !GeometryPropertiesCurrent(context, *source, run.SourceBinding.Inputs) ||
                !GeometryPropertiesCurrent(context, *target, run.TargetBinding.Inputs) ||
                !GeometryPropertiesCurrent(context, *target, run.NormalBinding.Inputs))
                return false;
            const auto* sourceTransform = raw.try_get<ECSC::Transform::Component>(*source);
            const auto* targetTransform = raw.try_get<ECSC::Transform::Component>(*target);
            return sourceTransform != nullptr && SameTransformComponent(*sourceTransform, run.SourceBeforeTransform) &&
                (run.TargetHadTransform ? targetTransform != nullptr &&
                            SameTransformComponent(*targetTransform, run.TargetBeforeTransform)
                                        : targetTransform == nullptr);
        }

        void DeliverIcpResult(IcpRun& run, EditorRegistrationResult result)
        {
            if (std::exchange(run.Delivered, true))
                return;
            if (run.Sink)
                run.Sink(std::move(result));
        }

        [[nodiscard]] EditorRegistrationResult IcpFailure(
            const IcpRun& run, const JobApplyValidation validation, std::string_view detail = {})
        {
            auto failure = BuildUnpublishedEditorJobFailure(validation, kIcpJobLabel, detail);
            EditorRegistrationResult result = run.Base;
            result.StepRun = run.Run;
            result.Status = failure.Status;
            result.Error = failure.Error;
            result.Message = std::move(failure.Message);
            return result;
        }

        // Accept (no step runs): after the full revalidation, one undoable transform of the source: the configured
        // trajectory step's pose (0: the start; clamped to the iterations done) composed with the pre-alignment.
        // Rigid deltas compose directly; decomposing column lengths would erase signed scale.
        [[nodiscard]] EditorRegistrationResult PublishIcpRun(IcpRun& run)
        {
            if (const auto validation = ValidateIcpRunInputs(run); validation != JobApplyValidation::Current)
                return IcpFailure(run, validation);
            EditorRegistrationResult result = run.Base;
            result.StepRun = run.Run;
            result.HasResult = true;
            result.IterationsPerformed = run.Solve.IterationsPerformed;
            result.TrajectoryLength = run.Trajectory.size();
            result.FinalRMSE = run.Solve.FinalRMSE;
            result.Converged = run.Solve.Converged;
            result.FinalInlierCount = run.Solve.FinalInlierCount;
            const std::size_t step = std::min(run.Command.TrajectoryStep, run.Trajectory.size());
            result.AppliedStep = step;
            const glm::mat4 pose =
                step == 0u ? glm::mat4(1.0f) : glm::mat4(run.Trajectory[step - 1u]) * run.PrealignPose;
            ECSC::Transform::Component after = run.SourceBeforeTransform;
            after.Position = glm::vec3(pose * glm::vec4(run.SourceBeforeTransform.Position, 1.f));
            after.Rotation = glm::normalize(glm::quat_cast(glm::mat3(pose)) * run.SourceBeforeTransform.Rotation);

            const auto& context = run.Context;
            if (context.CommandHistory != nullptr)
                result.Status = ToEditorCommandStatus(ExecuteEditorTransformMutation(*context.CommandHistory,
                    context.Scene,
                    context.World,
                    run.Command.SourceStableEntityId,
                    run.SourceBeforeTransform,
                    after,
                    "Align point clouds (ICP)")
                        .Status);
            else
            {
                context.Scene->Raw().get<ECSC::Transform::Component>(run.SourceEntity) = after;
                context.Scene->Raw().emplace_or_replace<ECSC::Transform::IsDirtyTag>(run.SourceEntity);
                result.Status = EditorCommandStatus::Applied;
            }
            if (result.Status != EditorCommandStatus::Applied)
            {
                result.Error = Core::ErrorCode::Unknown;
                result.Message = "ICP registration pose failed during editor history commit.";
                return result;
            }
            result.Error = Core::ErrorCode::Success;
            result.Message = BuildRegistrationSuccessMessage(result);
            return result;
        }

        // The run's settled end (StepRunCallbacks::Finished: no step runs and the holder released the source, so a new
        // run there is admitted from here on), from its final observation. Without a commit (discarded, cancelled,
        // stale or failed) nothing is published and the caller gets its one terminal result; after a Commit its
        // answer was delivered already.
        void EndIcpRun(IcpRun& run, const StepRunObservation& observed)
        {
            if (run.Delivered)
                return;
            const std::string& reason = observed.Diagnostic;
            if (observed.Phase == StepRunPhase::Failed)
            {
                EditorRegistrationResult result = run.Base;
                result.StepRun = run.Run;
                result.Status = EditorCommandStatus::GeometryProcessingFailed;
                result.Error = Core::ErrorCode::InvalidState;
                result.BackendDiagnostic = run.StepFailure.empty() ? run.Base.BackendDiagnostic : run.StepFailure;
                result.Message =
                    "ICP registration failed: " + (run.StepFailure.empty() ? reason : run.StepFailure) + ".";
                DeliverIcpResult(run, std::move(result));
                return;
            }
            if (reason.starts_with("stale"))
            {
                DeliverIcpResult(run, IcpFailure(run, JobApplyValidation::StaleGeneration));
                return;
            }
            EditorRegistrationResult result = run.Base;
            result.StepRun = run.Run;
            result.Status = EditorCommandStatus::StaleEntity;
            result.Error = Core::ErrorCode::InvalidState;
            result.Message = "ICP was cancelled; nothing was applied.";
            DeliverIcpResult(run, std::move(result));
        }

        // Restart (no step runs): a new generation from the captured inputs; the correspondence query and the
        // device session are kept (the target did not change).
        void RestartIcpRun(IcpRun& run)
        {
            run.Solve = {};
            run.Trajectory.clear();
            run.Started = run.Last = {};
            run.StepFailure.clear();
            ShowIcpPose(run, glm::mat4(1.0f));
        }

        [[nodiscard]] StepRunCallbacks MakeIcpCallbacks(const std::shared_ptr<IcpRun>& run)
        {
            StepRunCallbacks callbacks{
                .Valid =
                    [run]
                {
                    return IcpCapturesCurrent(*run);
                },
                .Commit =
                    [run](std::string& diagnostic)
                {
                    auto result = PublishIcpRun(*run);
                    const bool applied = result.Succeeded();
                    if (!applied)
                        diagnostic = result.Message;
                    DeliverIcpResult(*run, std::move(result));
                    return applied ? StepRunCommit::Committed : StepRunCommit::Refused;
                },
                // At once, also while a step runs: a waiting Vulkan page gives up (its bytes are released).
                .Discard =
                    [run]
                {
                    run->Abort.store(true, std::memory_order_release);
                },
                .Restart =
                    [run](std::string&)
                {
                    RestartIcpRun(*run);
                    return true;
                },
                .Finished =
                    [run](const StepRunObservation& observed)
                {
                    EndIcpRun(*run, observed);
                },
            };
            callbacks.Step = [run](StepRunMetric& metric)
            {
                return run->Session ? StepIcpOnGpu(*run, metric) : StepIcpOnCpu(*run, metric);
            };
            if (run->Progress)
            {
                // The pose alone (16 floats): the pre-aligned source stays with the panel's view.
                callbacks.Preview = [run](StepPreview& preview)
                {
                    const glm::mat4 pose{run->Solve.Transform};
                    preview.Values.assign(&pose[0][0], &pose[0][0] + 16);
                    return true;
                };
                callbacks.Show = [run](const StepPreview& preview)
                {
                    if (preview.Values.size() != 16u)
                        return StepRunShow::Never;
                    glm::mat4 pose{};
                    std::memcpy(&pose[0][0], preview.Values.data(), sizeof(pose));
                    ShowIcpPose(*run, pose);
                    return StepRunShow::Shown;
                };
            }
            return callbacks;
        }

        // Output identity serializes requests on a source/domain; every job of the run joins its holder.
        [[nodiscard]] EditorJobIdentity MakeRegistrationJobIdentity(const IcpRun& run)
        {
            return EditorJobIdentity{
                .EntityId = run.Command.SourceStableEntityId,
                .Scope = ToEditorJobScope(run.Command.SourcePositions.Domain),
                .OutputSemantic = GeometryPresentationSlotSemantic::Displacement,
                .OutputName = "registration_transform",
                .RequestedDomain = EditorJobDomainOfBackend(ToString(run.Command.Backend)),
            };
        }

        // Without a job lane the run computes and accepts at once (the immediate outcome); it has no controls, so
        // only start=run with auto_accept=end is honoured there.
        [[nodiscard]] EditorRegistrationResult RunIcpInline(IcpRun& run)
        {
            if (run.Command.Execution.Start != StepRunStart::Run ||
                run.Command.Execution.AutoAccept != StepRunAutoAccept::End)
            {
                EditorRegistrationResult result = run.Base;
                result.Status = EditorCommandStatus::InvalidProcessingParameters;
                result.Error = Core::ErrorCode::InvalidArgument;
                result.Message = "Without the job lane ICP computes and accepts at once: start=ready and an "
                                 "auto_accept other than end need the session's step runs.";
                return result;
            }
            StepRunMetric metric{};
            for (std::uint32_t step = 0u; step < run.Command.Execution.IterationCap; ++step)
            {
                const StepOutcome outcome = StepIcpOnCpu(run, metric);
                if (outcome == StepOutcome::Failed)
                {
                    EditorRegistrationResult result = run.Base;
                    result.Status = EditorCommandStatus::GeometryProcessingFailed;
                    result.Error = Core::ErrorCode::InvalidArgument;
                    result.Message = "ICP rejected the selected point clouds (" + run.StepFailure + ").";
                    return result;
                }
                if (outcome == StepOutcome::Converged)
                    break;
            }
            return PublishIcpRun(run);
        }

        [[nodiscard]] EditorRegistrationResult StartIcpRun(const EditorProcessingContext& context,
            const std::shared_ptr<IcpRun>& run,
            std::function<void(EditorRegistrationResult)> onComplete)
        {
            if (!context.JobCommands.Available())
                return RunIcpInline(*run);
            const auto refuse = [&](std::string message)
            {
                EditorRegistrationResult result = run->Base;
                result.Status = EditorCommandStatus::GeometryProcessingFailed;
                result.Error = Core::ErrorCode::InvalidState;
                result.Message = std::move(message);
                return result;
            };
            if (context.StepRuns == nullptr)
                return refuse("ICP registration needs the session's step runs.");
            EditorJobIdentity identity = MakeRegistrationJobIdentity(*run);
            if (const auto active = ActiveOutputJobRefusal(context, identity, "ICP registration"))
            {
                // The active run keeps delivering to its own caller; this request registers no callback.
                EditorRegistrationResult pending = MakePendingRegistrationResult(
                    run->Command, run->SourceBinding.Points.size(), run->TargetBinding.Points.size(), active->Token);
                pending.Message = active->Message;
                return pending;
            }
            const bool gpu = IsVulkanRegistrationBackend(run->Base.ActualBackend);
            if (gpu)
            {
                // The correspondence session, prepared once here on the main thread (resident target, hierarchy for
                // LBVH, every buffer and pipeline); the worker's iterations then need no frame.
                run->Float64 = context.Device && context.Device->SupportsShaderFloat64();
                std::string diagnostic;
                run->Session = context.SpatialIndices
                    ? context.SpatialIndices->PrepareQuerySession(run->TargetIndex,
                          {.Strategy = run->Base.ActualBackend == RegistrationBackend::VulkanBruteForce
                                  ? Graphics::PointQueryStrategy::BruteForce
                                  : Graphics::PointQueryStrategy::Lbvh,
                              .Capacity = 1u,
                              .PageQueries = std::uint32_t(std::min<std::size_t>(run->SourceWorld.size(), 1u << 20)),
                              .Queries = run->SourceWorld,
                              .Transformed = run->Float64,
                              .Uploads = !run->Float64},
                          diagnostic)
                    : nullptr;
                if (!run->Session)
                    return refuse("ICP registration could not prepare its Vulkan correspondences: " +
                        (diagnostic.empty() ? std::string("the spatial index cache is unavailable.") : diagnostic));
            }
            const auto holder = std::make_shared<JobToken>();
            std::string refusal;
            run->Sink = GuardEditorProcessingResult(context, std::move(onComplete));
            run->Run = context.StepRuns->Start({.Label = "ICP registration",
                                                   .Owner = "Runtime.RegistrationOperations",
                                                   .Output = {.StableEntityId = run->Command.SourceStableEntityId},
                                                   .Execution = run->Command.Execution,
                                                   .MetricNames = {"rmse", "inliers", "seconds", "iteration_seconds"}},
                MakeIcpCallbacks(run),
                EditorStepRunJobs(context.JobCommands, identity, holder),
                refusal);
            if (run->Run == 0u)
            {
                run->Delivered = true; // never started: no callback is owed
                return refuse(QueuedJobRejectedMessage("ICP registration") + " " + refusal);
            }
            run->Driver = context.StepRuns->Driver(run->Run);
            if (run->Progress)
                run->Progress->Snapshot = {.StepRun = run->Run,
                    .SourceWorld = std::make_shared<const std::vector<glm::vec3>>(run->SourceWorld),
                    .Revision = run->Progress->Snapshot.Revision + 1u};
            EditorRegistrationResult pending = run->Base;
            pending.StepRun = run->Run;
            pending.Status = EditorCommandStatus::Pending;
            pending.Message = "ICP registration queued (" + std::string(ToString(pending.ActualBackend)) + ")";
            AppendDerivedJobHandleToMessage(pending.Message, *holder);
            return pending;
        }

        [[nodiscard]] EditorRegistrationResult SubmitRegistrationRun(const EditorProcessingContext& context,
            const EditorRegistrationCommand& command,
            PointInputCapture source,
            PointInputCapture target,
            PointInputCapture normals,
            const ECSC::Transform::Component& sourceTransform,
            const ECSC::Transform::Component* targetTransform,
            std::function<void(EditorRegistrationResult)> onComplete,
            EditorRegistrationProgressHandle progress)
        {
            auto state = std::make_shared<IcpRun>();
            state->Context = context;
            state->Progress = std::move(progress);
            state->Command = command;
            entt::registry& raw = context.Scene->Raw();
            state->SourceEntity = *ResolveStableEntity(raw, command.SourceStableEntityId);
            state->TargetEntity = *ResolveStableEntity(raw, command.TargetStableEntityId);
            state->SourceBinding = std::move(source);
            state->TargetBinding = std::move(target);
            state->NormalBinding = std::move(normals);
            state->SourceBeforeTransform = sourceTransform;
            if (targetTransform != nullptr)
            {
                state->TargetHadTransform = true;
                state->TargetBeforeTransform = *targetTransform;
            }
            state->Base = MakeRegistrationBaseResult(command);
            state->Base.SourcePointCount = state->SourceBinding.Points.size();
            state->Base.TargetPointCount = state->TargetBinding.Points.size();

            if (command.Backend != RegistrationBackend::CpuKDTree)
            {
                // The Vulkan admission that needs no index first (brute-force work needs no device, so it is reported
                // even without one): a run that will query on the device acquires a GPU-only target, no CPU hierarchy
                // (ARCH-023); a CPU or fallen-back run acquires the CPU LBVH it steps with.
                const bool vulkan = IsVulkanRegistrationBackend(command.Backend);
                const auto count = [](std::size_t n)
                {
                    return std::uint32_t(std::min<std::size_t>(n, ~0u));
                };
                std::string reason = !vulkan || command.Backend != RegistrationBackend::VulkanBruteForce
                    ? std::string{}
                    : std::string(Graphics::PointBruteForceRefusal(
                          count(state->SourceBinding.Points.size()), count(state->TargetBinding.Points.size()), 1u));
                if (vulkan && reason.empty())
                    reason = VulkanCorrespondencesUnavailable(context);
                const bool gpuOnly = vulkan && reason.empty() && state->TargetBinding.Points.size() <= (1u << 20);
                if (context.SpatialIndices)
                {
                    const auto acquired = context.SpatialIndices->Acquire(context.World,
                        state->TargetEntity,
                        command.TargetPositions,
                        SpatialIndexSpace::EntityTransform,
                        !gpuOnly);
                    state->TargetIndex = acquired.Handle;
                    state->IndexSnapshot = context.SpatialIndices->Snapshot(acquired.Handle);
                    state->Base.TargetIndexReused = acquired.Reused;
                    state->Base.BackendDiagnostic = acquired.Diagnostic;
                }
                if (state->IndexSnapshot)
                    state->Base.ActualBackend = RegistrationBackend::CpuLBVH;
                else
                    state->Base.BackendDiagnostic = "Shared target index unavailable; using CPU KD-tree.";
                if (vulkan)
                {
                    if (reason.empty() && !state->IndexSnapshot) reason = "No shared target index";
                    if (reason.empty() && state->TargetBinding.Points.size() > (1u << 20))
                        reason = "More than 2^20 target points";
                    if (reason.empty())
                        state->Base.ActualBackend = command.Backend; // the requested strategy, never swapped
                    else
                    {
                        state->Base.FellBackToCPU = true;
                        state->Base.BackendDiagnostic = reason + "; Vulkan correspondences unavailable, using " +
                            std::string(ToString(state->Base.ActualBackend)) + ".";
                    }
                }
            }
            std::string rejection;
            if (!PrepareIcpRun(*state, rejection))
            {
                EditorRegistrationResult result = state->Base;
                result.Status = EditorCommandStatus::InvalidProcessingParameters;
                result.Error = Core::ErrorCode::InvalidArgument;
                result.Message = std::move(rejection);
                return result;
            }
            return StartIcpRun(context, state, std::move(onComplete));
        }
}
} // extern "C++"
using namespace GeometryProcessingDetail::MeshSupport;

    const char*
DebugNameForEditorICPVariant(
        const EditorICPVariant variant) noexcept
    {
        switch (variant)
        {
        case EditorICPVariant::PointToPoint:
            return "Point-to-point";
        case EditorICPVariant::PointToPlane:
            return "Point-to-plane";
        }
        return "Unknown";
    }

    EditorRegistrationResult
ApplyRegistrationChecked(
        const EditorProcessingContext& context,
        const EditorRegistrationCommand& input, bool preview,
        std::function<void(EditorRegistrationResult)> onComplete = {},
        EditorRegistrationProgressHandle progress = {})
    {
        EditorRegistrationCommand command = input;
        EditorRegistrationResult result =
            MakeRegistrationBaseResult(command);

        if (context.Scene == nullptr)
        {
            result.Status = EditorCommandStatus::MissingScene;
            result.Error = Core::ErrorCode::InvalidState;
            result.Message = "ICP registration requires an attached scene.";
            return result;
        }
        if (command.SourceStableEntityId == command.TargetStableEntityId)
        {
            result.Status =
                EditorCommandStatus::InvalidProcessingParameters;
            result.Error = Core::ErrorCode::InvalidArgument;
            result.Message =
                "ICP registration requires two distinct source and target entities.";
            return result;
        }
        if (const auto refusal = StepRunExecutionRefusal(command.Execution, kMaxRegistrationIterations);
            !refusal.empty())
        {
            result.Status = EditorCommandStatus::InvalidProcessingParameters;
            result.Error = Core::ErrorCode::InvalidArgument;
            result.Message = "ICP registration: " + std::string(refusal);
            return result;
        }
        if (!ValidEditorICPVariant(command.Variant) || !(command.InlierRatio > 0.0 && command.InlierRatio <= 1.0) ||
            !std::isfinite(command.MaxCorrespondenceDistance) || !std::isfinite(command.ConvergenceThreshold) ||
            command.ConvergenceThreshold < 0 || command.Backend > RegistrationBackend::VulkanBruteForce)
        {
            result.Status =
                EditorCommandStatus::InvalidProcessingParameters;
            result.Error = Core::ErrorCode::InvalidArgument;
            result.Message = "ICP registration requires a valid variant, an inlier ratio in (0, 1], "
                             "and a finite correspondence distance.";
            return result;
        }

        entt::registry& raw = context.Scene->Raw();
        const std::optional<ECS::EntityHandle> sourceEntity =
            ResolveStableEntity(raw, command.SourceStableEntityId);
        if (!sourceEntity.has_value())
        {
            result.Status = EditorCommandStatus::StaleEntity;
            result.Error = Core::ErrorCode::ResourceNotFound;
            result.Message =
                "ICP registration source entity is stale or no longer live.";
            return result;
        }
        const std::optional<ECS::EntityHandle> targetEntity =
            ResolveStableEntity(raw, command.TargetStableEntityId);
        if (!targetEntity.has_value())
        {
            result.Status = EditorCommandStatus::StaleEntity;
            result.Error = Core::ErrorCode::ResourceNotFound;
            result.Message =
                "ICP registration target entity is stale or no longer live.";
            return result;
        }

        const auto sourceAvailable = BuildGeometryAvailability(raw, *sourceEntity);
        const auto targetAvailable = BuildGeometryAvailability(raw, *targetEntity);
        command.SourcePositions = ResolveRegistrationDefault(sourceAvailable, command.SourcePositions);
        command.TargetPositions = ResolveRegistrationDefault(targetAvailable, command.TargetPositions);
        if (command.TargetNormals.Domain == GeometryElementDomain::Unknown)
            command.TargetNormals.Domain = command.TargetPositions.Domain;
        if (!SupportsGeometryElementDomain(sourceAvailable, command.SourcePositions.Domain) ||
            !SupportsGeometryElementDomain(targetAvailable, command.TargetPositions.Domain))
        {
            result.Status = EditorCommandStatus::UnsupportedGeometryDomain;
            result.Error = Core::ErrorCode::InvalidArgument;
            result.Message = "ICP requires compatible source and target element domains.";
            return result;
        }
        GeometryProcessingDetail::PointInputCapture source, target, normals;
        std::string sourceDiagnostic, targetDiagnostic, normalDiagnostic;
        const auto capture = [&](entt::entity entity, const GeometryEntityAvailability& available,
                                 GeometryPropertyRef& ref, GeometryProcessingDetail::PointInputCapture& values, std::string& diagnostic)
        {
            if (ref.ValueKind != Geometry::PropertyValueKind::Vec3)
            {
                diagnostic = "ICP requires count-matched finite float3 position bindings on live rows.";
                return false;
            }
            return preview ? PreparePointInput(context, entity, available, ref, values, diagnostic)
                           : CapturePointInput(available, ref, true, values, diagnostic);
        };
        const bool sourceReady = capture(*sourceEntity, sourceAvailable, command.SourcePositions, source, sourceDiagnostic);
        const bool targetReady = capture(*targetEntity, targetAvailable, command.TargetPositions, target, targetDiagnostic);
        RegistrationNormalStatus normalStatus = RegistrationNormalStatus::Ok;
        bool normalsReady = true;
        if (command.Variant == EditorICPVariant::PointToPlane)
        {
            if (command.TargetNormals.Domain != command.TargetPositions.Domain)
                normalStatus = RegistrationNormalStatus::CountMismatch;
            else
            {
                const auto* props = ResolveGeometryPropertySet(targetAvailable, command.TargetNormals.Domain);
                if (!props || command.TargetNormals.ValueKind != Geometry::PropertyValueKind::Vec3 ||
                    !ResolveGeometryProperty(targetAvailable, command.TargetNormals, props->Size(), false).Resolved())
                    normalStatus = RegistrationNormalStatus::Absent;
                else if (props->Get<glm::vec3>(command.TargetNormals.Name).Size() != props->Size())
                    normalStatus = RegistrationNormalStatus::CountMismatch;
                else
                {
                    normalsReady = capture(*targetEntity, targetAvailable, command.TargetNormals, normals, normalDiagnostic);
                    if (normals.HasNonfiniteVectors) normalStatus = RegistrationNormalStatus::Absent;
                    else if (normalsReady && !(normals.MinimumSquaredNorm > 0))
                        normalStatus = RegistrationNormalStatus::ZeroLength;
                }
            }
        }
        if (!sourceReady || !targetReady)
        {
            result.Status = EditorCommandStatus::InvalidProcessingParameters;
            result.Error = Core::ErrorCode::InvalidArgument;
            result.Message = !sourceReady ? std::move(sourceDiagnostic) : std::move(targetDiagnostic);
            return result;
        }
        result.SourcePointCount = source.LiveCount;
        result.TargetPointCount = target.LiveCount;
        if (normalStatus != RegistrationNormalStatus::Ok || !normalsReady)
        {
            result.Status = EditorCommandStatus::InvalidProcessingParameters;
            result.Error = Core::ErrorCode::InvalidArgument;
            result.Message = normalStatus != RegistrationNormalStatus::Ok
                ? BuildRegistrationNormalRejectionMessage(normalStatus) : std::move(normalDiagnostic);
            return result;
        }

        ECSC::Transform::Component* transform =
            raw.try_get<ECSC::Transform::Component>(*sourceEntity);
        if (transform == nullptr)
        {
            result.Status = EditorCommandStatus::MissingTransform;
            result.Error = Core::ErrorCode::InvalidState;
            result.Message =
                "ICP registration source entity has no Transform to drive.";
            return result;
        }

        const ECSC::Transform::Component* targetTransform =
            raw.try_get<ECSC::Transform::Component>(*targetEntity);

        if (source.LiveCount < 3 || target.LiveCount < 3)
        {
            result.Status = EditorCommandStatus::InvalidProcessingParameters;
            result.Message = "ICP requires at least three live samples per operand.";
            return result;
        }
        if (command.Variant == EditorICPVariant::PointToPlane)
        {
            const auto model = targetTransform ? ECSC::Transform::GetMatrix(*targetTransform) : glm::mat4(1.f);
            RegistrationNormalStatus status;
            if (preview)
            {
                // The cached local verdict cannot prove float safety under an arbitrary
                // transform. Keep the exact world-space check, borrowing live rows.
                const auto values = ResolveGeometryPropertySet(targetAvailable, command.TargetNormals.Domain)
                    ->Get<glm::vec3>(command.TargetNormals.Name);
                const auto [domain, name, divisor] = GeometryProcessingDetail::ResolvePointDeletionSource(command.TargetNormals.Domain);
                const auto deleted = ResolveGeometryPropertySet(targetAvailable, domain)->Get<bool>(name);
                status = TransformRegistrationNormalRows(values.Size(), [&](std::size_t i) -> std::optional<glm::vec3> {
                    if (deleted && deleted[i / divisor]) return {};
                    return values[i];
                }, model);
            }
            else status = TransformRegistrationNormalsToWorld(normals.Points, model);
            if (status != RegistrationNormalStatus::Ok)
            {
                result.Status = EditorCommandStatus::InvalidProcessingParameters;
                result.Message = BuildRegistrationNormalRejectionMessage(status);
                return result;
            }
        }
        if (preview)
        {
            result.Status = EditorCommandStatus::Applied;
            result.Message = "Ready to register the selected property domains.";
            return result;
        }
        return SubmitRegistrationRun(context,
            command,
            std::move(source),
            std::move(target),
            std::move(normals),
            *transform,
            targetTransform,
            std::move(onComplete),
            std::move(progress));
    }

    EditorRegistrationResult ApplyEditorRegistrationCommand(
        const EditorProcessingCommands& commands, const EditorRegistrationCommand& command,
        std::function<void(EditorRegistrationResult)> onComplete, EditorRegistrationProgressHandle progress)
    {
        return ApplyRegistrationChecked(EditorProcessingCommandsAccess::Resolve(commands), command, false,
                                        std::move(onComplete), std::move(progress));
    }
    std::string RegistrationVulkanUnavailableReason(const EditorProcessingCommands& commands)
    {
        return VulkanCorrespondencesUnavailable(EditorProcessingCommandsAccess::Resolve(commands));
    }

    ActionReadiness PreviewEditorRegistrationCommand(
        const EditorProcessingCommands& commands, const EditorRegistrationCommand& command)
    {
        const auto result =
            ApplyRegistrationChecked(EditorProcessingCommandsAccess::Resolve(commands), command, true);
        return {result.Succeeded(), result.Succeeded() ? std::string{} : result.Message};
    }
    EditorRegistrationResult ApplyEditorConfiguredRegistrationCommand(
        const EditorProcessingCommands& commands, std::function<void(EditorRegistrationResult)> onComplete,
        EditorRegistrationProgressHandle progress)
    {
        const auto config = GetEditorRegistrationConfig(commands);
        if (!config)
        {
            EditorRegistrationResult result{};
            result.Status = EditorCommandStatus::InvalidProcessingParameters;
            result.Error = Core::ErrorCode::InvalidState;
            result.Message = "ICP registration requires an available engine config.";
            return result;
        }
        return ApplyEditorRegistrationCommand(commands, *config, std::move(onComplete), std::move(progress));
    }

    EditorRegistrationProgressHandle MakeEditorRegistrationProgress()
    {
        return std::make_shared<EditorRegistrationProgress>();
    }

    EditorRegistrationProgressSnapshot SnapshotEditorRegistrationProgress(const EditorRegistrationProgressHandle& progress)
    {
        return progress ? progress->Snapshot : EditorRegistrationProgressSnapshot{};
    }
    GeometryPropertyCatalogSnapshot GetEditorRegistrationInputCatalog(
        const EditorProcessingCommands& commands, std::uint32_t stableId)
    {
        return GeometryProcessingDetail::BuildPointInputCatalog(
            EditorProcessingCommandsAccess::Resolve(commands), stableId, 3);
    }
} // namespace Extrinsic::Runtime
