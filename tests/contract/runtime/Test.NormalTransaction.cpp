// RUNTIME-296: the Vulkan vertex-normals run as a GPU property transaction (ADR 0030 decisions
// 6-7) on a null/mock device: Accept publishes through the undoable "Estimate normals" entry and
// binds the ring front as the canonical slot of the new revision; Discard, stale input and
// output behave as the ADR says; the topology bundle is resident per topology revision; the
// backend admission refuses what it cannot run.
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>
#include <gtest/gtest.h>
#include "MockRHI.hpp"
#include "PointDomainFixture.hpp"
#include "SandboxEditorJobHarness.hpp"
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.WorldHandle;
import Extrinsic.Runtime.NormalOperations;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.GpuPropertyBinding;
import Extrinsic.Graphics.GpuPropertyResidency;
import Geometry.Properties;
namespace R = Extrinsic::Runtime;
namespace G = Extrinsic::Graphics;
namespace GS = Extrinsic::ECS::Components::GeometrySources;
using D = R::GeometryElementDomain;
using K = Geometry::PropertyValueKind;
namespace
{
    struct Harness
    {
        Extrinsic::ECS::Scene::Registry Scene;
        R::EditorCommandHistory History;
        entt::entity Entity;
        R::EditorProcessingContext Context;
        R::NormalEstimationConfig Config;
        Extrinsic::Tests::MockDevice Device;
        G::GpuPropertyResidency Residency{Device};
        Extrinsic::Tests::EditorJobHarness Jobs;
        // The point-domain fixture's mesh: four vertices, two triangles.
        Harness()
        {
            Entity = Intrinsic::Tests::MakePointDomainSource(Scene, D::MeshVertex);
            Config.StableEntityId = R::SelectionController::ToStableEntityId(Entity);
            Config.Method = R::NormalEstimationMethod::MeshFaceWeighted;
            Config.Backend = R::NormalEstimationBackend::Vulkan;
            Config.Positions = {D::MeshVertex, "v:position", K::Vec3};
            Config.Output = {D::MeshVertex, "v:normal", K::Vec3};
            Device.TransferQueue.AcceptBufferUploads = true;
            Context.Scene = &Scene;
            Context.CommandHistory = &History;
            Jobs.Attach(Context);
        }
        Geometry::PropertySet& Vertices() { return Intrinsic::Tests::PointDomainProperties(Scene, Entity, D::MeshVertex); }
        Geometry::PropertySet& Faces() { return Scene.Raw().get<GS::Faces>(Entity).Properties; }
        auto Commands() { return R::BindEditorProcessingCommands(Context); }
        std::size_t Rows() { return Vertices().Size(); }
        // A transaction waiting for Accept whose device result is `value` on every row.
        R::EditorNormalTransactionHandle Ready(const glm::vec3 value, const bool withResidency = true)
        {
            return R::MakeEditorNormalTransactionForTest(Commands(), Config, std::vector<glm::vec3>(Rows(), value),
                                                         withResidency ? &Residency : nullptr);
        }
        G::GpuPropertyKey Key() const { return R::MakeGpuPropertyKey(Context.World, Entity, Config.Output); }
        std::optional<Geometry::PropertyRevision> OutputRevision()
        {
            const auto p = std::as_const(Vertices()).Get<glm::vec3>("v:normal");
            return p ? std::optional{p.Revision()} : std::nullopt;
        }
        std::optional<R::EditorNormalTopologyResidency> Topology(std::string& why)
        {
            return R::ResolveEditorNormalTopology(Commands(), Config, Residency, why);
        }
    };
}

TEST(NormalTransaction, AcceptPublishesUndoablyThenBindsTheFrontAsTheCanonicalRevision)
{
    Harness h;
    const glm::vec3 value{0.f, 0.f, 1.f};
    const auto run = h.Ready(value);
    ASSERT_TRUE(run);
    auto snapshot = R::SnapshotEditorNormalEstimation(h.Commands(), run);
    EXPECT_EQ(snapshot.Phase, R::EditorGpuTransactionPhase::ReadyToAccept);
    EXPECT_TRUE(snapshot.CanAccept) << snapshot.AcceptDisabledReason;
    EXPECT_EQ(snapshot.Result.Status, R::EditorCommandStatus::Pending);
    EXPECT_EQ(snapshot.Result.RequestedBackend, R::NormalEstimationBackend::Vulkan);
    EXPECT_TRUE(h.Residency.HasRing(h.Key())) << "the seam publishes a front on the output ring";
    EXPECT_FALSE(h.Vertices().Exists("v:normal")) << "nothing is published while the result waits";

    std::optional<R::EditorNormalEstimationResult> delivered;
    const auto accepted = R::AcceptEditorNormalEstimation(h.Commands(), run, [&](R::EditorNormalEstimationResult r) { delivered = r; });
    ASSERT_EQ(accepted.Status, R::EditorCommandStatus::Pending) << accepted.Message;
    snapshot = R::SnapshotEditorNormalEstimation(h.Commands(), run);
    EXPECT_EQ(snapshot.Phase, R::EditorGpuTransactionPhase::Accepting);
    EXPECT_EQ(snapshot.Result.Status, R::EditorCommandStatus::Pending) << "\"Applied\" only after the CPU publication";
    EXPECT_FALSE(delivered);

    ASSERT_TRUE(h.Jobs.DrainUntilTerminal());
    snapshot = R::SnapshotEditorNormalEstimation(h.Commands(), run);
    EXPECT_EQ(snapshot.Phase, R::EditorGpuTransactionPhase::Applied);
    ASSERT_TRUE(delivered);
    EXPECT_EQ(delivered->Status, R::EditorCommandStatus::Applied) << delivered->Message;
    EXPECT_EQ(delivered->ActualBackend, "vulkan_mesh_face_weighted");
    EXPECT_EQ(delivered->WrittenCount, h.Rows());
    EXPECT_EQ(delivered->ChangedCount, h.Rows());
    const auto normals = std::as_const(h.Vertices()).Get<glm::vec3>("v:normal");
    ASSERT_TRUE(normals);
    for (std::size_t i = 0; i < h.Rows(); ++i) EXPECT_EQ(normals[i], value) << "row " << i;

    // The front is now the canonical slot of the published revision: the ring is gone and the
    // next GPU use of this revision hits without uploading.
    EXPECT_FALSE(h.Residency.HasRing(h.Key()));
    const auto front = h.Residency.Front(h.Key());
    ASSERT_TRUE(front);
    ASSERT_TRUE(h.OutputRevision());
    EXPECT_EQ(front->Revision, *h.OutputRevision());
    const auto before = h.Residency.Stats();
    const auto input = R::ResolveGpuPropertyInput(h.Residency, h.Scene, h.Context.World, h.Entity, h.Config.Output);
    ASSERT_TRUE(input);
    EXPECT_EQ(input->Buffer, front->Buffer);
    const auto after = h.Residency.Stats();
    EXPECT_EQ(after.Uploads, before.Uploads) << "a resident revision uploads nothing";
    EXPECT_EQ(after.UploadBytes, before.UploadBytes);
    EXPECT_EQ(after.Hits, before.Hits + 1u);

    // Undo changes the CPU revision (the property is removed again); redo republishes under a
    // new revision, which uploads once.
    ASSERT_TRUE(h.History.Undo().Succeeded());
    EXPECT_FALSE(h.Vertices().Exists("v:normal"));
    EXPECT_FALSE(R::ResolveGpuPropertyInput(h.Residency, h.Scene, h.Context.World, h.Entity, h.Config.Output));
    ASSERT_TRUE(h.History.Redo().Succeeded());
    ASSERT_TRUE(h.OutputRevision());
    EXPECT_NE(*h.OutputRevision(), front->Revision);
    const auto uploadsBefore = h.Residency.Stats().Uploads;
    const auto reuploaded = R::ResolveGpuPropertyInput(h.Residency, h.Scene, h.Context.World, h.Entity, h.Config.Output);
    ASSERT_TRUE(reuploaded);
    EXPECT_EQ(h.Residency.Stats().Uploads, uploadsBefore + 1u);
    EXPECT_EQ(reuploaded->Revision, *h.OutputRevision());
}

TEST(NormalTransaction, AcceptKeepsDeletedRowsAndAnExistingOutputsBytes)
{
    Harness h;
    auto& vertices = h.Vertices();
    auto existing = vertices.GetOrAdd<glm::vec3>("v:normal", glm::vec3{7.f, 8.f, 9.f});
    vertices.GetOrAdd<bool>("v:deleted", false)[1] = true;
    // The device wrote the live rows; the ring kept the deleted row's published bytes.
    std::vector<glm::vec3> front(h.Rows(), glm::vec3{1.f, 0.f, 0.f});
    front[1] = existing[1];
    const auto run = R::MakeEditorNormalTransactionForTest(h.Commands(), h.Config, front, &h.Residency);
    ASSERT_TRUE(run);
    std::optional<R::EditorNormalEstimationResult> delivered;
    ASSERT_EQ(R::AcceptEditorNormalEstimation(h.Commands(), run, [&](R::EditorNormalEstimationResult r) { delivered = r; }).Status,
              R::EditorCommandStatus::Pending);
    ASSERT_TRUE(h.Jobs.DrainUntilTerminal());
    ASSERT_TRUE(delivered);
    EXPECT_EQ(delivered->Status, R::EditorCommandStatus::Applied) << delivered->Message;
    EXPECT_EQ(delivered->WrittenCount, h.Rows() - 1u) << "the live rows";
    const auto normals = std::as_const(h.Vertices()).Get<glm::vec3>("v:normal");
    ASSERT_TRUE(normals);
    EXPECT_EQ(normals[1], glm::vec3(7.f, 8.f, 9.f)) << "a deleted row keeps its published value";
    EXPECT_EQ(normals[0], glm::vec3(1.f, 0.f, 0.f));
    ASSERT_TRUE(h.History.Undo().Succeeded());
    EXPECT_EQ(std::as_const(h.Vertices()).Get<glm::vec3>("v:normal")[0], glm::vec3(7.f, 8.f, 9.f));
}

TEST(NormalTransaction, DiscardPublishesNothingAndReleasesTheRing)
{
    Harness h;
    const auto run = h.Ready({0.f, 1.f, 0.f});
    ASSERT_TRUE(run);
    ASSERT_TRUE(h.Residency.HasRing(h.Key()));
    R::DiscardEditorNormalEstimation(h.Commands(), run);
    const auto snapshot = R::SnapshotEditorNormalEstimation(h.Commands(), run);
    EXPECT_EQ(snapshot.Phase, R::EditorGpuTransactionPhase::Discarded);
    EXPECT_FALSE(snapshot.CanAccept);
    EXPECT_EQ(snapshot.Result.Status, R::EditorCommandStatus::StaleEntity);
    EXPECT_FALSE(h.Residency.HasRing(h.Key())) << "observation returns to the canonical slot";
    EXPECT_FALSE(h.Residency.Front(h.Key())) << "no canonical slot was ever bound";
    EXPECT_FALSE(h.Vertices().Exists("v:normal"));
    EXPECT_FALSE(h.History.Undo().Succeeded()) << "nothing entered the history";
    const auto refused = R::AcceptEditorNormalEstimation(h.Commands(), run);
    EXPECT_EQ(refused.Status, R::EditorCommandStatus::InvalidProcessingParameters) << "a discarded result cannot be accepted";
}

TEST(NormalTransaction, StaleInputOrOutputWhilePendingDisablesAcceptUntilDiscard)
{
    {
        Harness h;
        const auto run = h.Ready({0.f, 0.f, 1.f});
        ASSERT_TRUE(run);
        EXPECT_TRUE(R::SnapshotEditorNormalEstimation(h.Commands(), run).CanAccept);
        h.Vertices().Get<glm::vec3>("v:position")[0].x += 1.f; // the positions change under the waiting result
        const auto stale = R::SnapshotEditorNormalEstimation(h.Commands(), run);
        EXPECT_EQ(stale.Phase, R::EditorGpuTransactionPhase::ReadyToAccept);
        EXPECT_TRUE(stale.Stale);
        EXPECT_FALSE(stale.CanAccept);
        EXPECT_NE(stale.AcceptDisabledReason.find("changed"), std::string::npos) << stale.AcceptDisabledReason;
        const auto refused = R::AcceptEditorNormalEstimation(h.Commands(), run);
        EXPECT_EQ(refused.Status, R::EditorCommandStatus::StaleEntity);
        EXPECT_TRUE(h.Residency.HasRing(h.Key())) << "the result stays until Discard";
        R::DiscardEditorNormalEstimation(h.Commands(), run);
        EXPECT_FALSE(h.Residency.HasRing(h.Key()));
        EXPECT_FALSE(h.Vertices().Exists("v:normal"));
    }
    {
        Harness h;
        (void)h.Vertices().GetOrAdd<glm::vec3>("v:normal", glm::vec3{0.f});
        const auto run = h.Ready({0.f, 0.f, 1.f});
        ASSERT_TRUE(run);
        EXPECT_TRUE(R::SnapshotEditorNormalEstimation(h.Commands(), run).CanAccept);
        h.Vertices().Get<glm::vec3>("v:normal")[0].y = 2.f; // the output is edited under the waiting result
        const auto stale = R::SnapshotEditorNormalEstimation(h.Commands(), run);
        EXPECT_TRUE(stale.Stale);
        EXPECT_FALSE(stale.CanAccept);
        EXPECT_EQ(R::AcceptEditorNormalEstimation(h.Commands(), run).Status, R::EditorCommandStatus::StaleEntity);
    }
    {
        // A topology edit (a deleted face) is an input change too.
        Harness h;
        const auto run = h.Ready({0.f, 0.f, 1.f});
        ASSERT_TRUE(run);
        h.Faces().GetOrAdd<bool>("f:deleted", false)[0] = true;
        EXPECT_TRUE(R::SnapshotEditorNormalEstimation(h.Commands(), run).Stale);
    }
}

TEST(NormalTransaction, CancelWhileAcceptingPublishesNothing)
{
    Harness h;
    const auto run = h.Ready({0.f, 0.f, 1.f});
    ASSERT_TRUE(run);
    std::optional<R::EditorNormalEstimationResult> delivered;
    ASSERT_EQ(R::AcceptEditorNormalEstimation(h.Commands(), run, [&](R::EditorNormalEstimationResult r) { delivered = r; }).Status,
              R::EditorCommandStatus::Pending);
    R::DiscardEditorNormalEstimation(h.Commands(), run);
    EXPECT_EQ(R::SnapshotEditorNormalEstimation(h.Commands(), run).Phase, R::EditorGpuTransactionPhase::Discarded);
    ASSERT_TRUE(h.Jobs.DrainUntilTerminal());
    ASSERT_TRUE(delivered);
    EXPECT_EQ(delivered->Status, R::EditorCommandStatus::StaleEntity);
    EXPECT_FALSE(h.Vertices().Exists("v:normal"));
    EXPECT_FALSE(h.Residency.HasRing(h.Key()));
}

TEST(NormalTransaction, TopologyBundleIsResidentPerTopologyRevision)
{
    Harness h;
    std::string why;
    const auto first = h.Topology(why);
    ASSERT_TRUE(first) << why;
    EXPECT_TRUE(first->Uploaded);
    EXPECT_EQ(first->Faces, 2u);
    EXPECT_EQ(first->LiveRows, 4u);
    // faces + 1, six corners, vertices + 1, twelve incidence words, four live rows: 30 words.
    EXPECT_EQ(first->Bytes, 30u * sizeof(std::uint32_t));
    const auto stats = h.Residency.Stats();
    EXPECT_EQ(stats.Uploads, 1u);
    EXPECT_EQ(stats.UploadBytes, first->Bytes);

    const auto second = h.Topology(why);
    ASSERT_TRUE(second) << why;
    EXPECT_FALSE(second->Uploaded) << "the same topology revision is resident";
    EXPECT_EQ(second->Bytes, first->Bytes);
    EXPECT_EQ(h.Residency.Stats().Uploads, 1u);
    EXPECT_EQ(h.Residency.Stats().Hits, stats.Hits + 1u);

    // Positions are not topology: a position edit hits the same bundle.
    h.Vertices().Get<glm::vec3>("v:position")[0].z += 0.5f;
    const auto moved = h.Topology(why);
    ASSERT_TRUE(moved) << why;
    EXPECT_FALSE(moved->Uploaded);

    // A deleted face is a new topology revision: one upload, one face ring fewer.
    h.Faces().GetOrAdd<bool>("f:deleted", false)[0] = true;
    const auto fewerFaces = h.Topology(why);
    ASSERT_TRUE(fewerFaces) << why;
    EXPECT_TRUE(fewerFaces->Uploaded);
    EXPECT_EQ(fewerFaces->Faces, 2u) << "the bundle is indexed by source face";
    EXPECT_EQ(fewerFaces->Bytes, (3u + 3u + 5u + 6u + 4u) * sizeof(std::uint32_t));
    EXPECT_EQ(h.Residency.Stats().Uploads, 2u);

    // A deleted vertex changes the live rows and empties the rings that touch it.
    h.Vertices().GetOrAdd<bool>("v:deleted", false)[3] = true;
    const auto fewerRows = h.Topology(why);
    ASSERT_TRUE(fewerRows) << why;
    EXPECT_TRUE(fewerRows->Uploaded);
    EXPECT_EQ(fewerRows->LiveRows, 3u);
    EXPECT_EQ(fewerRows->Bytes, (3u + 0u + 5u + 0u + 3u) * sizeof(std::uint32_t)) << "the remaining face (2,1,3) touches the deleted vertex";
    EXPECT_EQ(h.Residency.Stats().Uploads, 3u);
}

TEST(NormalTransaction, VulkanBackendIsAdmittedOnlyForMeshVertexNormalsOnACapableDevice)
{
    Harness h;
    // No device on the context: the readiness reports why.
    auto readiness = R::PreviewEditorNormalEstimationCommand(h.Commands(), h.Config);
    EXPECT_FALSE(readiness.Enabled);
    EXPECT_NE(readiness.DisabledReason.find("device"), std::string::npos) << readiness.DisabledReason;
    R::EditorNormalEstimationResult failure;
    EXPECT_FALSE(R::StartEditorNormalEstimationTransaction(h.Commands(), h.Config, failure));
    EXPECT_EQ(failure.Status, R::EditorCommandStatus::InvalidProcessingParameters);
    EXPECT_EQ(failure.RequestedBackend, R::NormalEstimationBackend::Vulkan);
    const auto applied = R::ApplyEditorNormalEstimationCommand(h.Commands(), h.Config);
    EXPECT_EQ(applied.Status, R::EditorCommandStatus::InvalidProcessingParameters) << "no silent CPU fallback";
    EXPECT_FALSE(h.Vertices().Exists("v:normal"));

    // The other methods and the angle weightings stay on the CPU (or on vulkan_lbvh for PCA).
    auto pca = h.Config;
    pca.Method = R::NormalEstimationMethod::PointSetPCA;
    readiness = R::PreviewEditorNormalEstimationCommand(h.Commands(), pca);
    EXPECT_FALSE(readiness.Enabled);
    EXPECT_NE(readiness.DisabledReason.find("vulkan_lbvh"), std::string::npos) << readiness.DisabledReason;
    auto angle = h.Config;
    angle.Weighting = Geometry::HalfedgeMesh::VertexNormals::AveragingMode::AngleWeighted;
    readiness = R::PreviewEditorNormalEstimationCommand(h.Commands(), angle);
    EXPECT_FALSE(readiness.Enabled);
    EXPECT_NE(readiness.DisabledReason.find("angle"), std::string::npos) << readiness.DisabledReason;
    auto positions = h.Config;
    positions.Positions.Name = "samples";
    positions.Output.Name = "v:position";
    (void)h.Vertices().GetOrAdd<glm::vec3>("samples", glm::vec3{0.f});
    readiness = R::PreviewEditorNormalEstimationCommand(h.Commands(), positions);
    EXPECT_FALSE(readiness.Enabled);
    EXPECT_NE(readiness.DisabledReason.find("v:position"), std::string::npos) << readiness.DisabledReason;

    // A subnormal coordinate is refused (a device may flush it on the float -> double
    // conversion); the CPU reference still accepts it.
    {
        Harness subnormal;
        subnormal.Vertices().Get<glm::vec3>("v:position")[0].x = 1e-40f;
        readiness = R::PreviewEditorNormalEstimationCommand(subnormal.Commands(), subnormal.Config);
        EXPECT_FALSE(readiness.Enabled);
        EXPECT_NE(readiness.DisabledReason.find("subnormal"), std::string::npos) << readiness.DisabledReason;
        auto reference = subnormal.Config;
        reference.Backend = R::NormalEstimationBackend::CpuKDTree;
        EXPECT_TRUE(R::PreviewEditorNormalEstimationCommand(subnormal.Commands(), reference).Enabled);
    }
    // A CPU run of the same config publishes at once, as before.
    auto cpu = h.Config;
    cpu.Backend = R::NormalEstimationBackend::CpuKDTree;
    EXPECT_TRUE(R::PreviewEditorNormalEstimationCommand(h.Commands(), cpu).Enabled);
    // The config round-trips the new backend name.
    EXPECT_EQ(std::string{R::ToString(R::NormalEstimationBackend::Vulkan)}, "vulkan");
    const auto validation = R::ValidateNormalEstimationConfigSection(R::SerializeNormalEstimationConfig(h.Config), {}, "test");
    EXPECT_TRUE(validation.Usable());
}
