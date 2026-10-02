// RUNTIME-313: the one setup/completion contract of queued editor jobs
// (`MeshSupport::ActiveOutputJobRefusal`, `ValidateQueuedJob`, `QueuedJobDelivery`),
// checked per operation through a scripted job lane: duplicate refusal before any
// submission, a rejected submission answered once without the callback, and an
// abandoned run that revalidates as Cancelled and delivers exactly once.
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>
#include <glm/glm.hpp>
#include <entt/entity/registry.hpp>
#include <gtest/gtest.h>
#include "EditorFeatureTestContext.hpp"

import Extrinsic.ECS.Scene.Registry;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.Runtime.EditorJobProjection;
import Extrinsic.Runtime.EditorProcessing;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.NormalOperations;
import Extrinsic.Runtime.PointAnalysisOperations;
import Extrinsic.Runtime.PointConstructionOperations;
import Extrinsic.Runtime.PointFieldOperations;
import Extrinsic.Runtime.PointSetOperations;
import Geometry.Properties;

namespace R = Extrinsic::Runtime;
namespace GS = Extrinsic::ECS::Components::GeometrySources;
using D = R::GeometryElementDomain;

namespace
{
    class QueuedEditorJobContract : public ::testing::Test
    {
    protected:
        Extrinsic::ECS::Scene::Registry Scene;
        entt::entity Entity{};
        std::uint32_t Id{};
        R::EditorProcessingContext Context{};
        std::vector<R::JobDesc> Queued;
        bool RejectSubmissions{false};
        std::optional<R::EditorJobRecord> Active;

        void SetUp() override
        {
            Entity = Scene.Create();
            auto& props = Scene.Raw().emplace<GS::Vertices>(Entity).Properties;
            props.Resize(8);
            auto samples = props.GetOrAdd<glm::vec3>("samples");
            auto directions = props.GetOrAdd<glm::vec3>("directions");
            for (std::size_t i = 0; i < 8; ++i)
            {
                samples[i] = {float(i % 2), float((i / 2) % 2), float(i / 4)};
                directions[i] = {0, 0, 1};
            }
            Id = R::SelectionController::ToStableEntityId(Entity);
            Context.Scene = &Scene;
            Context.JobCommands.Submit = [this](R::JobDesc desc, R::EditorJobIdentity) {
                if (RejectSubmissions) return R::JobToken{};
                Queued.push_back(std::move(desc));
                return R::JobToken{static_cast<std::uint32_t>(Queued.size()), 1u};
            };
            Context.JobCommands.FindActive = [this](const R::EditorJobIdentity& identity) {
                auto active = Active;
                if (active) active->Identity = identity;
                return active;
            };
        }
        [[nodiscard]] R::EditorProcessingCommands Commands() { return R::BindEditorProcessingCommands(Context); }
        [[nodiscard]] R::GeometryPropertyRef Ref(const char* name, Geometry::PropertyValueKind kind) const
        {
            return {D::PointCloudPoint, name, kind};
        }
        [[nodiscard]] R::GeometryPropertyRef Positions() const { return Ref("samples", Geometry::PropertyValueKind::Vec3); }
        [[nodiscard]] R::GeometryPropertyRef Normals() const { return Ref("directions", Geometry::PropertyValueKind::Vec3); }

        // Every queued point operation, by its shared label.
        template <class Check>
        void ForEachOperation(Check check)
        {
            const auto commands = Commands();
            check("Outlier estimation", [&](auto done) {
                return R::ApplyEditorOutlierAnalysisCommand(commands, {.StableEntityId = Id, .Positions = Positions(),
                    .Mask = Ref("outliers", Geometry::PropertyValueKind::UInt32),
                    .Score = Ref("scores", Geometry::PropertyValueKind::Float), .KNeighbors = 2}, done); });
            check("Normal estimation", [&](auto done) {
                return R::ApplyEditorNormalEstimationCommand(commands, {.StableEntityId = Id, .Positions = Positions(),
                    .Output = Ref("normals", Geometry::PropertyValueKind::Vec3)}, done); });
            check("Density estimation", [&](auto done) {
                return R::ApplyEditorKernelDensityCommand(commands, {.StableEntityId = Id, .Positions = Positions(),
                    .Density = Ref("density", Geometry::PropertyValueKind::Float), .KNeighbors = 2}, done); });
            check("Radii estimation", [&](auto done) {
                return R::ApplyEditorPointSpacingCommand(commands, {.StableEntityId = Id, .Positions = Positions(),
                    .Radii = Ref("radii", Geometry::PropertyValueKind::Float)}, done); });
            check("Density weights", [&](auto done) {
                return R::ApplyEditorDensityWeightCommand(commands, {.StableEntityId = Id, .Positions = Positions(),
                    .Weights = Ref("weights", Geometry::PropertyValueKind::Float)}, done); });
            check("Keypoint analysis", [&](auto done) {
                return R::ApplyEditorKeypointAnalysisCommand(commands, {.StableEntityId = Id, .Positions = Positions(),
                    .Mask = Ref("keypoints", Geometry::PropertyValueKind::UInt32),
                    .Score = Ref("saliency", Geometry::PropertyValueKind::Float), .MinimumNeighbors = 1}, done); });
            check("Descriptor analysis", [&](auto done) {
                return R::ApplyEditorDescriptorAnalysisCommand(commands, {.StableEntityId = Id, .Positions = Positions(),
                    .Normals = Normals(), .Outputs = R::MakeDescriptorOutputProperties(D::PointCloudPoint, "descriptor")}, done); });
            check("Point construction", [&](auto done) {
                return R::ApplyEditorPointConstructionCommand(commands, {.StableEntityId = Id, .Positions = Positions(),
                    .Method = R::PointConstructionMethod::KnnGraph, .KNeighbors = 2}, done); });
        }
    };
}

TEST_F(QueuedEditorJobContract, DuplicateOutputIsRefusedBeforeSubmissionWithTheSharedMessage)
{
    Active = R::EditorJobRecord{.Token = R::JobToken{3u, 1u}, .State = R::JobState::Running};
    ForEachOperation([&](const std::string& label, auto apply) {
        SCOPED_TRACE(label);
        Queued.clear();
        unsigned calls{0u};
        const auto result = apply([&](auto) { ++calls; });
        EXPECT_EQ(result.Status, R::EditorCommandStatus::Pending);
        EXPECT_EQ(result.Message, label + " already has an active running job (job 3:1).");
        EXPECT_TRUE(Queued.empty());
        EXPECT_EQ(calls, 0u);
    });
}

TEST_F(QueuedEditorJobContract, RejectedSubmissionAnswersOnceWithoutTheCallback)
{
    RejectSubmissions = true;
    ForEachOperation([&](const std::string& label, auto apply) {
        SCOPED_TRACE(label);
        unsigned calls{0u};
        const auto result = apply([&](auto) { ++calls; });
        EXPECT_EQ(result.Status, R::EditorCommandStatus::GeometryProcessingFailed) << result.Message;
        EXPECT_EQ(result.Message.rfind(label + " job submission was rejected", 0), 0u) << result.Message;
        // The immediate answer is the report (panels publish it); a callback would report twice.
        EXPECT_EQ(calls, 0u);
    });
}

TEST_F(QueuedEditorJobContract, AbandonedRunRevalidatesAsCancelledAndDeliversOnce)
{
    ForEachOperation([&](const std::string& label, auto apply) {
        SCOPED_TRACE(label);
        Queued.clear();
        unsigned calls{0u};
        R::EditorCommandStatus status{};
        std::string message;
        const auto result = apply([&](auto delivered) {
            ++calls;
            status = delivered.Status;
            message = delivered.Message;
        });
        ASSERT_EQ(result.Status, R::EditorCommandStatus::Pending) << result.Message;
        ASSERT_FALSE(Queued.empty());
        auto& last = Queued.back();
        ASSERT_EQ(last.ValidateBeforeApply(), R::JobApplyValidation::Current);
        last.FinalizeUnpublishedOnMainThread();
        EXPECT_EQ(calls, 1u);
        EXPECT_EQ(status, R::EditorCommandStatus::StaleEntity);
        EXPECT_EQ(message, label + " was cancelled or its source became stale; nothing was applied.");
        // An abandoned run never publishes: any stage revalidated afterwards is Cancelled.
        EXPECT_EQ(last.ValidateBeforeApply(), R::JobApplyValidation::Cancelled);
        last.FinalizeUnpublishedOnMainThread();
        EXPECT_EQ(calls, 1u);
    });
}
