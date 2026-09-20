module;
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

struct RegistrationAlignmentOutcome {
  bool HasResult{false};
  Reg::RegistrationResult Result{};
  std::vector<Reg::IterationTrace> Traces{};

  [[nodiscard]] std::size_t IterationCount() const noexcept {
    return Traces.size();
  }
};

[[nodiscard]] RegistrationAlignmentOutcome
AlignPointClouds(const std::span<const glm::vec3> sourcePoints,
                 const std::span<const glm::vec3> targetPoints,
                 const std::span<const glm::vec3> targetNormals,
                 const Reg::RegistrationParams &params, const Reg::NearestQuery& query = {}) {
  RegistrationAlignmentOutcome outcome;
  outcome.Traces.reserve(params.MaxIterations);
  const Reg::IterationObserver observe = [&outcome](const Reg::IterationTrace& trace) { outcome.Traces.push_back(trace); };
  const auto result = query ? Reg::AlignICPWithQueries(sourcePoints, targetPoints, targetNormals, params, query, observe)
                            : Reg::AlignICP(sourcePoints, targetPoints, targetNormals, params, observe);
  if (result) {
    outcome.HasResult = true;
    outcome.Result = *result;
  }
  return outcome;
}

[[nodiscard]] glm::mat4
TrajectoryPose(const RegistrationAlignmentOutcome &outcome,
               const std::size_t index) {
  if (index == 0u || outcome.Traces.empty())
    return glm::mat4(1.0f);

  const std::size_t clamped = std::min(index, outcome.Traces.size());
  return glm::mat4(outcome.Traces[clamped - 1u].Transform);
}

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
                if (!FinitePosition(normalized))
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

        // Compose an entity model matrix (translate * rotate * scale) from its
        // local Transform::Component so ICP can run in world space.
        [[nodiscard]] glm::mat4 ModelMatrixFromTransform(
            const ECSC::Transform::Component& transform) noexcept
        {
            glm::mat4 model = glm::mat4_cast(transform.Rotation);
            model[0] *= transform.Scale.x;
            model[1] *= transform.Scale.y;
            model[2] *= transform.Scale.z;
            model[3] = glm::vec4(transform.Position, 1.0f);
            return model;
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
        struct EditorRegistrationCpuJobState
        {
            std::uint32_t SourceStableEntityId{0u};
            std::uint32_t TargetStableEntityId{0u};
            EditorRegistrationCommand Command{};
            PointInputCapture SourceBinding{}, TargetBinding{};
            // Point-to-point leaves normals empty. Every binding owns local rows,
            // source-row IDs and revision watches for publication revalidation.
            PointInputCapture NormalBinding{};
            ECSC::Transform::Component SourceBeforeTransform{};
            bool TargetHadTransform{false};
            ECSC::Transform::Component TargetBeforeTransform{};
            EditorRegistrationResult Result{};
            // A rejected publication also runs the unpublished finalizer;
            // Delivered prevents a second terminal callback.
            std::function<void(EditorRegistrationResult)> Sink{};
            bool Delivered{false};
            // Last answer this job's `ValidateBeforeApply` gave the drain. The
            // finalizer takes no arguments, so the reason a completion was
            // refused has to be recorded where it was decided.
            JobApplyValidation LastApplyValidation{JobApplyValidation::Current};
            ECSC::Transform::Component SourceAfterTransform{};
            SpatialIndexHandle TargetIndex{};
            std::shared_ptr<const SpatialIndexSnapshot> IndexSnapshot{};
            std::shared_ptr<SpatialNearestBatch> Batch{};
            std::vector<glm::vec3> SourceWorld{}, TargetWorld{}, WorldNormals{};
            glm::mat4 PrealignPose{1.f};
            Reg::RegistrationParams Params{};
            RegistrationAlignmentOutcome Outcome{};
        };

        [[nodiscard]] std::vector<glm::vec3> TransformPointsToWorld(
            const std::vector<glm::vec3>& points,
            const ECSC::Transform::Component& transform)
        {
            const glm::mat4 model = ModelMatrixFromTransform(transform);
            std::vector<glm::vec3> world;
            world.reserve(points.size());
            for (const glm::vec3& point : points)
                world.push_back(glm::vec3(model * glm::vec4(point, 1.0f)));
            return world;
        }

        [[nodiscard]] JobApplyValidation
        ValidateRegistrationCpuJobApply(
            const EditorProcessingContext& context,
            const EditorRegistrationCpuJobState& job)
        {
            // Epoch first: after detachment the borrowed scene pointer may name
            // a freed registry, so nothing below may read it.
            if (context.AttachmentActive && !context.AttachmentActive())
                return JobApplyValidation::StaleWorld;
            if (context.Scene == nullptr)
                return JobApplyValidation::MissingTarget;

            entt::registry& raw = context.Scene->Raw();
            const std::optional<ECS::EntityHandle> sourceEntity =
                ResolveStableEntity(raw, job.SourceStableEntityId);
            const std::optional<ECS::EntityHandle> targetEntity =
                ResolveStableEntity(raw, job.TargetStableEntityId);
            if (!sourceEntity.has_value() || !targetEntity.has_value())
                return JobApplyValidation::MissingTarget;

            const auto current = [&](ECS::EntityHandle entity, GeometryPropertyRef ref,
                                     const PointInputCapture& before)
            {
                PointInputCapture now;
                std::string diagnostic;
                return CapturePointInput(BuildGeometryAvailability(raw, entity), ref, true, now, diagnostic) &&
                       now.Inputs == before.Inputs && now.Slots == before.Slots &&
                       SameGeometryPositions(now.Points, before.Points);
            };
            if (!current(*sourceEntity, job.Command.SourcePositions, job.SourceBinding) ||
                !current(*targetEntity, job.Command.TargetPositions, job.TargetBinding) ||
                (!job.NormalBinding.Points.empty() &&
                 !current(*targetEntity, job.Command.TargetNormals, job.NormalBinding)))
                return JobApplyValidation::StaleGeneration;

            const ECSC::Transform::Component* sourceTransform =
                raw.try_get<ECSC::Transform::Component>(*sourceEntity);
            if (sourceTransform == nullptr ||
                !SameTransformComponent(*sourceTransform,
                                        job.SourceBeforeTransform))
            {
                return JobApplyValidation::StaleGeneration;
            }

            const ECSC::Transform::Component* targetTransform =
                raw.try_get<ECSC::Transform::Component>(*targetEntity);
            if (targetTransform == nullptr)
                return job.TargetHadTransform
                    ? JobApplyValidation::StaleGeneration
                    : JobApplyValidation::Current;
            if (!job.TargetHadTransform ||
                !SameTransformComponent(*targetTransform,
                                        job.TargetBeforeTransform))
            {
                return JobApplyValidation::StaleGeneration;
            }

            return JobApplyValidation::Current;
        }

        void PublishRegistrationResultSink(
            EditorRegistrationCpuJobState& job,
            EditorRegistrationResult result)
        {
            if (job.Delivered)
                return;
            job.Delivered = true;
            if (job.Sink)
                job.Sink(std::move(result));
        }

        void FinishRegistrationSolve(EditorRegistrationCpuJobState& state)
        {
            auto& result = state.Result;
            const auto& outcome = state.Outcome;
            result.HasResult = true;
            result.IterationsPerformed = outcome.Result.IterationsPerformed;
            result.TrajectoryLength = outcome.IterationCount();
            result.FinalRMSE = outcome.Result.FinalRMSE;
            result.Converged = outcome.Result.Converged;
            result.FinalInlierCount = outcome.Result.FinalInlierCount;

            const std::size_t step =
                std::min(state.Command.TrajectoryStep,
                         outcome.IterationCount());
            result.AppliedStep = step;
            const glm::mat4 pose =
                step == 0u ? glm::mat4(1.0f)
                           : TrajectoryPose(outcome, step) * state.PrealignPose;

            state.SourceAfterTransform = state.SourceBeforeTransform;
            // Compose the rigid delta directly; decomposing column lengths would
            // erase signed scale and lose the orientation of collapsed axes.
            state.SourceAfterTransform.Position = glm::vec3(
                pose * glm::vec4(state.SourceBeforeTransform.Position, 1.f));
            state.SourceAfterTransform.Rotation = glm::normalize(
                glm::quat_cast(glm::mat3(pose)) * state.SourceBeforeTransform.Rotation);

            result.Status = EditorCommandStatus::Applied;
            result.Error = Core::ErrorCode::Success;
        }

        [[nodiscard]] JobResultEnvelope RunRegistrationCpuWorker(
            const std::shared_ptr<EditorRegistrationCpuJobState>& state)
        {
            EditorRegistrationResult& result = state->Result;
            result.SourcePointCount = state->SourceBinding.Points.size();
            result.TargetPointCount = state->TargetBinding.Points.size();

            const std::vector<glm::vec3> sourceWorld =
                TransformPointsToWorld(state->SourceBinding.Points,
                                       state->SourceBeforeTransform);
            const std::vector<glm::vec3> targetWorld =
                state->TargetHadTransform
                    ? TransformPointsToWorld(state->TargetBinding.Points,
                                             state->TargetBeforeTransform)
                    : state->TargetBinding.Points;

            const glm::vec3 prealignDelta =
                ComputePointCentroid(std::span<const glm::vec3>(targetWorld)) -
                ComputePointCentroid(std::span<const glm::vec3>(sourceWorld));
            std::vector<glm::vec3> prealignedSourceWorld = sourceWorld;
            for (glm::vec3& point : prealignedSourceWorld)
                point += prealignDelta;
            glm::mat4 prealignPose(1.0f);
            prealignPose[3] = glm::vec4(prealignDelta, 1.0f);

            std::vector<glm::vec3> targetWorldNormals{};
            if (!state->NormalBinding.Points.empty())
            {
                const glm::mat4 targetModel =
                    state->TargetHadTransform
                        ? ModelMatrixFromTransform(state->TargetBeforeTransform)
                        : glm::mat4(1.0f);
                const RegistrationNormalStatus worldStatus =
                    TransformRegistrationNormalsToWorld(
                        state->NormalBinding.Points,
                        targetModel,
                        &targetWorldNormals);
                if (worldStatus != RegistrationNormalStatus::Ok)
                {
                    result.Status =
                        EditorCommandStatus::InvalidProcessingParameters;
                    result.Error = Core::ErrorCode::InvalidArgument;
                    result.Message =
                        BuildRegistrationNormalRejectionMessage(worldStatus);
                    return JobResultEnvelope::Make<EditorJobResult>(
                        EditorJobResult{
                            .Diagnostic = result.Message,
                        });
                }
            }
            result.TargetNormalCount = targetWorldNormals.size();

            Reg::RegistrationParams params{};
            params.Variant = ToGeometryICPVariant(state->Command.Variant);
            params.MaxIterations = state->Command.MaxIterations;
            params.ConvergenceThreshold = state->Command.ConvergenceThreshold;
            params.MaxCorrespondenceDistance =
                state->Command.MaxCorrespondenceDistance > 0.0
                    ? state->Command.MaxCorrespondenceDistance
                    : 1.0e6;
            params.InlierRatio = state->Command.InlierRatio;
            result.EffectiveVariant =
                targetWorldNormals.size() == targetWorld.size() &&
                        !targetWorldNormals.empty()
                    ? EditorICPVariant::PointToPlane
                    : EditorICPVariant::PointToPoint;

            state->SourceWorld = std::move(prealignedSourceWorld);
            state->TargetWorld = targetWorld;
            state->WorldNormals = std::move(targetWorldNormals);
            state->PrealignPose = prealignPose;
            state->Params = params;
            if (result.ActualBackend == RegistrationBackend::VulkanLBVH)
                return JobResultEnvelope::Make<EditorJobResult>(EditorJobResult{.Diagnostic = "ICP GPU correspondences ready to queue"});
            Reg::NearestQuery query;
            if (state->IndexSnapshot)
                query = [index = state->IndexSnapshot](std::span<const glm::vec3> queries, std::span<std::uint32_t> ids) {
                    for (std::size_t i = 0; i < queries.size(); ++i)
                    {
                        if (!Geometry::PointLBVH::ValidPoint(queries[i])) return false;
                        ids[i] = index->Index.Nearest(queries[i]).Index;
                    }
                    return true;
                };
            const RegistrationAlignmentOutcome outcome = AlignPointClouds(
                state->SourceWorld, state->TargetWorld, state->WorldNormals, params, query);
            if (!outcome.HasResult)
            {
                result.Status =
                    EditorCommandStatus::GeometryProcessingFailed;
                result.Error = Core::ErrorCode::InvalidArgument;
                result.Message = "ICP rejected the selected point clouds (fewer than 3 "
                                 "points or invalid parameters).";
                return JobResultEnvelope::Make<EditorJobResult>(
                    EditorJobResult{
                        .Diagnostic = result.Message,
                    });
            }

            state->Outcome = outcome;
            FinishRegistrationSolve(*state);
            return JobResultEnvelope::Make<EditorJobResult>(EditorJobResult{.Diagnostic = "ICP registration result ready"});
        }

        bool AdvanceRegistrationGpu(const EditorProcessingContext& context,
                                    EditorRegistrationCpuJobState& state)
        {
            if (state.Result.ActualBackend != RegistrationBackend::VulkanLBVH ||
                state.Result.Status != EditorCommandStatus::NoChange) return true;
            auto fail = [&](std::string message) {
                state.Result.Status = EditorCommandStatus::GeometryProcessingFailed;
                state.Result.Error = Core::ErrorCode::InvalidState;
                state.Result.BackendDiagnostic = message;
                state.Result.Message = std::move(message);
                // Release this job's share of the borrowed query lease; the
                // cache owns the batch and may be torn down right after.
                state.Batch.reset();
                return true;
            };
            // The spatial cache is borrowed from the session: once the epoch
            // closes, neither the pointer nor the batch it handed out may be
            // read, so the readiness gate closes before any of them.
            if (context.AttachmentActive && !context.AttachmentActive())
                return fail("ICP GPU correspondences were abandoned when the world detached.");
            if (!context.SpatialIndices || ValidateRegistrationCpuJobApply(context, state) != JobApplyValidation::Current)
                return fail("ICP inputs changed while GPU correspondences were pending.");
            if (state.Batch)
            {
                if (state.Batch->State == SpatialQueryState::Failed) return fail(state.Batch->Diagnostic);
                if (state.Batch->State != SpatialQueryState::Ready) return false;
                std::vector<std::uint32_t> indices;
                const auto& slots = state.IndexSnapshot->Slots;
                for (const auto neighbor : state.Batch->Neighbors)
                {
                    const auto found = std::lower_bound(slots.begin(), slots.end(), neighbor.Index);
                    indices.push_back(found != slots.end() && *found == neighbor.Index
                        ? static_cast<std::uint32_t>(found - slots.begin()) : ~0u);
                }
                const auto status = Reg::AdvanceICP(state.SourceWorld, state.TargetWorld, state.WorldNormals,
                    state.Params, indices, state.Outcome.Result,
                    [&](const Reg::IterationTrace& trace) { state.Outcome.Traces.push_back(trace); });
                if (status == Reg::ICPStepStatus::InvalidInput) return fail("ICP rejected GPU correspondences.");
                if (status == Reg::ICPStepStatus::Finished)
                {
                    state.Outcome.HasResult = true;
                    FinishRegistrationSolve(state);
                    state.Batch.reset();
                    return true;
                }
            }
            state.Batch = context.SpatialIndices->QueueGpuNearest(state.TargetIndex,
                Reg::MakeICPQueries(state.SourceWorld, state.Outcome.Result.Transform), state.Batch);
            if (state.Batch->State == SpatialQueryState::Failed) return fail(state.Batch->Diagnostic);
            return false;
        }

        [[nodiscard]] Core::Result PublishRegistrationCpuJob(
            const EditorProcessingContext& context,
            EditorRegistrationCpuJobState& job)
        {
            EditorRegistrationResult result = job.Result;
            if (!result.Succeeded())
            {
                job.Result = result;
                PublishRegistrationResultSink(job, result);
                return Core::Err(ResultErrorOrUnknown(result.Error));
            }

            if (context.Scene == nullptr)
            {
                result.Status = EditorCommandStatus::MissingScene;
                result.Error = Core::ErrorCode::InvalidState;
                result.Message = "ICP registration requires an attached scene.";
                job.Result = result;
                PublishRegistrationResultSink(job, result);
                return Core::Err(result.Error);
            }

            entt::registry& raw = context.Scene->Raw();
            const std::optional<ECS::EntityHandle> sourceEntity =
                ResolveStableEntity(raw, job.SourceStableEntityId);
            if (!sourceEntity.has_value())
            {
                result.Status = EditorCommandStatus::StaleEntity;
                result.Error = Core::ErrorCode::ResourceNotFound;
                result.Message =
                    "ICP registration source entity is stale or no longer live.";
                job.Result = result;
                PublishRegistrationResultSink(job, result);
                return Core::Err(result.Error);
            }

            ECSC::Transform::Component* transform =
                raw.try_get<ECSC::Transform::Component>(*sourceEntity);
            if (transform == nullptr)
            {
                result.Status = EditorCommandStatus::MissingTransform;
                result.Error = Core::ErrorCode::InvalidState;
                result.Message =
                    "ICP registration source entity has no Transform to drive.";
                job.Result = result;
                PublishRegistrationResultSink(job, result);
                return Core::Err(result.Error);
            }

            if (context.CommandHistory != nullptr)
            {
                const EditorCommandHistoryResult history =
                    ExecuteEditorTransformMutation(
                        *context.CommandHistory,
                        context.Scene,
                        context.World,
                        job.SourceStableEntityId,
                        job.SourceBeforeTransform,
                        job.SourceAfterTransform,
                        "Align point clouds (ICP)");
                result.Status = ToEditorCommandStatus(history.Status);
            }
            else
            {
                *transform = job.SourceAfterTransform;
                raw.emplace_or_replace<ECSC::Transform::IsDirtyTag>(
                    *sourceEntity);
                result.Status = EditorCommandStatus::Applied;
            }

            if (result.Status != EditorCommandStatus::Applied)
            {
                result.Error = Core::ErrorCode::Unknown;
                result.Message =
                    "ICP registration pose failed during editor history commit.";
                job.Result = result;
                PublishRegistrationResultSink(job, result);
                return Core::Err(result.Error);
            }

            result.Error = Core::ErrorCode::Success;
            result.Message = BuildRegistrationSuccessMessage(result);
            job.Result = result;
                PublishRegistrationResultSink(job, result);
            return Core::Ok();
        }

        // A queued ICP job that terminates without publishing — cancelled,
        // stale, detached, or dropped — still owes the editor exactly one
        // terminal result, otherwise its panel row stays `Pending` forever.
        // Reads only the job's own state: the scene and the spatial cache may
        // already be gone, and no pose or history entry is published here.
        void FinalizeUnpublishedRegistrationJob(
            EditorRegistrationCpuJobState& job)
        {
            if (job.Delivered)
                return;
            auto failure = BuildUnpublishedEditorJobFailure(
                job.LastApplyValidation,
                "Sandbox.RegistrationICP",
                !job.Result.Succeeded()
                    ? std::string_view{job.Result.Message} : std::string_view{});
            EditorRegistrationResult result = job.Result;
            result.Status = failure.Status;
            result.Error = failure.Error;
            result.Message = std::move(failure.Message);
            job.Result = result;
            PublishRegistrationResultSink(job, std::move(result));
        }

        // Output identity serializes requests on a source/domain; binding and
        // transform snapshots are revalidated immediately before publication.
        [[nodiscard]] EditorJobIdentity MakeRegistrationCpuJobIdentity(
            const EditorRegistrationCpuJobState& state)
        {
            return EditorJobIdentity{
                .EntityId = state.SourceStableEntityId,
                .Scope = ToEditorJobScope(state.Command.SourcePositions.Domain),
                .OutputSemantic = GeometryPresentationSlotSemantic::Displacement,
                .OutputName = "registration_transform",
            };
        }

        [[nodiscard]] JobDesc MakeRegistrationCpuJobDesc(
            const EditorProcessingContext& context,
            const std::shared_ptr<EditorRegistrationCpuJobState>& state)
        {
            const std::uint32_t estimatedCost =
                std::max<std::uint32_t>(
                    1u,
                    static_cast<std::uint32_t>(
                        (std::max(state->SourceBinding.Points.size(),
                                  state->TargetBinding.Points.size()) +
                         1023u) /
                        1024u));
            return JobDesc{
                .DebugName = "Sandbox.RegistrationICP",
                .Scope = context.World,
                .Priority = Core::Dag::TaskPriority::Normal,
                .Kind = RuntimeTaskKinds::GeometryProcess,
                .EstimatedCost = estimatedCost,
                .Work =
                    [state](const JobCancellation&) -> JobResultEnvelope
                    {
                        return RunRegistrationCpuWorker(state);
                    },
                .IsReadyToApply = [context, state]() { return AdvanceRegistrationGpu(context, *state); },
                .ValidateBeforeApply =
                    [context, state]()
                    {
                        const JobApplyValidation validation =
                            ValidateRegistrationCpuJobApply(context, *state);
                        state->LastApplyValidation = validation;
                        return validation;
                    },
                .PublishCompletion =
                    [context, state](KernelEventBus&,
                                     const JobResultEnvelope& result) -> bool
                    {
                        if (result.TryGet<EditorJobResult>() == nullptr)
                            return false;
                        return PublishRegistrationCpuJob(context, *state)
                            .has_value();
                    },
                .FinalizeUnpublishedOnMainThread =
                    [state]() { FinalizeUnpublishedRegistrationJob(*state); },
            };
        }

        [[nodiscard]] EditorRegistrationResult
        SubmitRegistrationCpuJob(
            const EditorProcessingContext& context,
            const EditorRegistrationCommand& command,
            PointInputCapture source,
            PointInputCapture target,
            PointInputCapture normals,
            const ECSC::Transform::Component& sourceTransform,
            const ECSC::Transform::Component* targetTransform,
            std::function<void(EditorRegistrationResult)> onComplete)
        {
            auto state =
                std::make_shared<EditorRegistrationCpuJobState>();
            state->SourceStableEntityId = command.SourceStableEntityId;
            state->TargetStableEntityId = command.TargetStableEntityId;
            state->Command = command;
            state->SourceBinding = std::move(source);
            state->TargetBinding = std::move(target);
            state->NormalBinding = std::move(normals);
            state->SourceBeforeTransform = sourceTransform;
            if (targetTransform != nullptr)
            {
                state->TargetHadTransform = true;
                state->TargetBeforeTransform = *targetTransform;
            }
            state->Result = MakeRegistrationBaseResult(command);
            state->Result.SourcePointCount = state->SourceBinding.Points.size();
            state->Result.TargetPointCount = state->TargetBinding.Points.size();

            const auto targetEntity = ResolveStableEntity(context.Scene->Raw(), command.TargetStableEntityId);
            if (command.Backend != RegistrationBackend::CpuKDTree)
            {
                if (context.SpatialIndices)
                {
                    const auto acquired = context.SpatialIndices->Acquire(context.World, *targetEntity,
                        command.TargetPositions, SpatialIndexSpace::EntityTransform);
                    state->TargetIndex = acquired.Handle;
                    state->IndexSnapshot = context.SpatialIndices->Snapshot(acquired.Handle);
                    state->Result.TargetIndexReused = acquired.Reused;
                    state->Result.BackendDiagnostic = acquired.Diagnostic;
                }
                if (state->IndexSnapshot)
                    state->Result.ActualBackend = RegistrationBackend::CpuLBVH;
                else
                    state->Result.BackendDiagnostic = "Shared target index unavailable; using CPU KD-tree.";
                if (command.Backend == RegistrationBackend::VulkanLBVH)
                {
                    if (state->IndexSnapshot && context.JobCommands.Available() && context.Device && context.Device->IsOperational() &&
                        state->TargetBinding.Points.size() <= (1u << 20))
                        state->Result.ActualBackend = RegistrationBackend::VulkanLBVH;
                    else
                    {
                        state->Result.FellBackToCPU = true;
                        state->Result.BackendDiagnostic = "Vulkan correspondence execution unavailable; using " +
                            std::string(ToString(state->Result.ActualBackend)) + ".";
                    }
                }
            }
            if (!context.JobCommands.Available())
            {
                // Immediate outcome: the caller gets it as the return value and
                // no completion callback is registered for it.
                (void)RunRegistrationCpuWorker(state);
                (void)PublishRegistrationCpuJob(context, *state);
                return state->Result;
            }

            const EditorJobIdentity identity =
                MakeRegistrationCpuJobIdentity(*state);
            if (const std::optional<EditorJobRecord> active =
                    FindActiveEditorJob(context, identity))
            {
                // The active job already owns the callback that will deliver
                // this output's terminal result; a duplicate request registers
                // none, so this state is discarded without a sink.
                EditorRegistrationResult pending =
                    MakePendingRegistrationResult(
                        command,
                        state->SourceBinding.Points.size(),
                        state->TargetBinding.Points.size(),
                        active->Token);
                pending.Message =
                    BuildActiveDerivedJobMessage("ICP registration CPU", *active);
                return pending;
            }

            state->Sink = GuardEditorProcessingResult(context, std::move(onComplete));
            JobDesc desc = MakeRegistrationCpuJobDesc(context, state);
            auto pending = state->Result;
            const JobToken handle = context.JobCommands.Submit(
                std::move(desc),
                identity);
            if (!handle.IsValid())
            {
                // The job was never enqueued, so no finalizer will run for it
                // and this state dies here with its sink uncalled.
                EditorRegistrationResult result =
                    MakeRegistrationBaseResult(command);
                result.Status =
                    EditorCommandStatus::GeometryProcessingFailed;
                result.SourcePointCount = state->SourceBinding.Points.size();
                result.TargetPointCount = state->TargetBinding.Points.size();
                result.Error = Core::ErrorCode::InvalidState;
                result.Message          = "ICP registration CPU job submission was rejected by the "
                                          "runtime job lane.";
                return result;
            }

            pending.Status = EditorCommandStatus::Pending;
            pending.Message = "ICP registration job queued (" + std::string(ToString(pending.ActualBackend)) + ")";
            AppendDerivedJobHandleToMessage(pending.Message, handle);
            return pending;
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
        std::function<void(EditorRegistrationResult)> onComplete = {})
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
        if (!ValidEditorICPVariant(command.Variant) ||
            command.MaxIterations == 0u ||
            !(command.InlierRatio > 0.0 && command.InlierRatio <= 1.0) ||
            !std::isfinite(command.MaxCorrespondenceDistance) ||
            !std::isfinite(command.ConvergenceThreshold) || command.ConvergenceThreshold < 0 ||
            command.Backend > RegistrationBackend::VulkanLBVH)
        {
            result.Status =
                EditorCommandStatus::InvalidProcessingParameters;
            result.Error = Core::ErrorCode::InvalidArgument;
            result.Message = "ICP registration requires a valid variant, a positive "
                             "iteration count, an inlier ratio in (0, 1], and a finite "
                             "correspondence distance.";
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
            const auto model = targetTransform ? ModelMatrixFromTransform(*targetTransform) : glm::mat4(1.f);
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
        return SubmitRegistrationCpuJob(context, command, std::move(source), std::move(target), std::move(normals),
            *transform, targetTransform, std::move(onComplete));
    }

    EditorRegistrationResult ApplyEditorRegistrationCommand(
        const EditorProcessingCommands& commands, const EditorRegistrationCommand& command,
        std::function<void(EditorRegistrationResult)> onComplete)
    {
        return ApplyRegistrationChecked(EditorProcessingCommandsAccess::Resolve(commands), command, false,
                                        std::move(onComplete));
    }
    ActionReadiness PreviewEditorRegistrationCommand(
        const EditorProcessingCommands& commands, const EditorRegistrationCommand& command)
    {
        const auto result =
            ApplyRegistrationChecked(EditorProcessingCommandsAccess::Resolve(commands), command, true);
        return {result.Succeeded(), result.Succeeded() ? std::string{} : result.Message};
    }
    EditorRegistrationResult ApplyEditorConfiguredRegistrationCommand(
        const EditorProcessingCommands& commands, std::function<void(EditorRegistrationResult)> onComplete)
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
        return ApplyEditorRegistrationCommand(commands, *config, std::move(onComplete));
    }
    GeometryPropertyCatalogSnapshot GetEditorRegistrationInputCatalog(
        const EditorProcessingCommands& commands, std::uint32_t stableId)
    {
        return GeometryProcessingDetail::BuildPointInputCatalog(
            EditorProcessingCommandsAccess::Resolve(commands), stableId, 3);
    }
} // namespace Extrinsic::Runtime
