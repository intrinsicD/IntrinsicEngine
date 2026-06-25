#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

import Extrinsic.Graphics.RenderingContract;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.RenderArtifactPublication;

namespace Graphics = Extrinsic::Graphics;
namespace Runtime = Extrinsic::Runtime;

namespace
{
    [[nodiscard]] Runtime::RenderArtifactDeclaration MakeDeclaration(
        std::string artifactId,
        Runtime::RenderArtifactPublicationKind kind =
            Runtime::RenderArtifactPublicationKind::CandidateProjectResult,
        std::vector<std::string> sourceRevisions = {"scene:1"})
    {
        return Runtime::RenderArtifactDeclaration{
            .Metadata =
                Graphics::RenderArtifactMetadata{
                    .ArtifactId = std::move(artifactId),
                    .RendererId = "contract-renderer",
                    .SnapshotId = "snapshot-main",
                    .ViewOutputRecipeId = "main-view",
                    .SourceRevisions = std::move(sourceRevisions),
                    .Status = Graphics::RenderArtifactStatus::Available,
                    .Lifetime = Graphics::RenderArtifactLifetime::Cached,
                    .Purpose = "color",
                },
            .Kind = kind,
            .PayloadUri = "memory://artifact",
            .ProducerLabel = "contract renderer",
        };
    }

    [[nodiscard]] Runtime::EditorCommandHistoryStatus StatusFor(
        const Runtime::RenderArtifactOperationResult& result) noexcept
    {
        return result.Succeeded()
            ? Runtime::EditorCommandHistoryStatus::Applied
            : Runtime::EditorCommandHistoryStatus::CommandFailed;
    }
}

TEST(RenderArtifactPublication, RegistersObservableArtifactBeforeProjectMutation)
{
    Runtime::RenderArtifactRegistry registry;

    const Runtime::RenderArtifactOperationResult result =
        registry.RegisterArtifact(MakeDeclaration("artifact-color"));

    ASSERT_TRUE(result.Succeeded());
    EXPECT_EQ(result.Status, Runtime::RenderArtifactOperationStatus::Registered);
    EXPECT_EQ(result.State, Runtime::RenderArtifactPublicationState::Unpublished);
    EXPECT_FALSE(result.ProjectMutationAuthorized);
    EXPECT_FALSE(result.ProjectMutationPerformed);
    ASSERT_EQ(registry.Size(), 1u);

    const std::optional<Runtime::RenderArtifactRecord> record =
        registry.Find("artifact-color");
    ASSERT_TRUE(record.has_value());
    EXPECT_EQ(record->Metadata.RendererId, "contract-renderer");
    EXPECT_EQ(record->Metadata.SnapshotId, "snapshot-main");
    EXPECT_EQ(record->Metadata.ViewOutputRecipeId, "main-view");
    EXPECT_EQ(record->Metadata.SourceRevisions, std::vector<std::string>({"scene:1"}));
    EXPECT_EQ(record->Kind,
              Runtime::RenderArtifactPublicationKind::CandidateProjectResult);
    EXPECT_EQ(Runtime::ToUiStatus(*record),
              Runtime::RenderArtifactUiStatus::Unpublished);

    const std::vector<Runtime::RenderArtifactAuditRecord> audit = registry.AuditLog();
    ASSERT_EQ(audit.size(), 1u);
    EXPECT_EQ(audit.front().Action, Runtime::RenderArtifactAuditAction::Registered);
}

TEST(RenderArtifactPublication, SupersedesOlderSourceRevisionForSameLogicalOutput)
{
    Runtime::RenderArtifactRegistry registry;
    ASSERT_TRUE(registry.RegisterArtifact(MakeDeclaration("artifact-a", 
                                                          Runtime::RenderArtifactPublicationKind::PreviewOnly,
                                                          {"scene:1"}))
                    .Succeeded());

    const Runtime::RenderArtifactOperationResult newer =
        registry.RegisterArtifact(MakeDeclaration("artifact-b",
                                                  Runtime::RenderArtifactPublicationKind::PreviewOnly,
                                                  {"scene:2"}));

    ASSERT_TRUE(newer.Succeeded());
    const std::optional<Runtime::RenderArtifactRecord> oldRecord =
        registry.Find("artifact-a");
    const std::optional<Runtime::RenderArtifactRecord> newRecord =
        registry.Find("artifact-b");
    ASSERT_TRUE(oldRecord.has_value());
    ASSERT_TRUE(newRecord.has_value());
    EXPECT_EQ(oldRecord->State, Runtime::RenderArtifactPublicationState::Superseded);
    EXPECT_EQ(newRecord->State, Runtime::RenderArtifactPublicationState::Unpublished);
    ASSERT_FALSE(oldRecord->Diagnostics.empty());
    EXPECT_EQ(oldRecord->Diagnostics.back().Code,
              Runtime::RenderArtifactDiagnosticCode::SupersededByNewerArtifact);
    EXPECT_EQ(registry.ListByStatus(Runtime::RenderArtifactPublicationState::Superseded).size(),
              1u);
}

TEST(RenderArtifactPublication, PublishAndApplyAreExplicitAuditedAndUndoAware)
{
    Runtime::RenderArtifactRegistry registry;
    Runtime::EditorCommandHistory history;
    ASSERT_TRUE(registry.RegisterArtifact(MakeDeclaration("artifact-candidate"))
                    .Succeeded());

    const Runtime::RenderArtifactOperationResult publish =
        registry.PublishArtifact(Runtime::RenderArtifactPublishCommand{
            .ArtifactId = "artifact-candidate",
            .Provenance = "renderer contract test",
            .TargetUri = "memory://published/candidate",
            .Label = "Publish Candidate",
            .UndoLabel = "Unpublish Candidate",
        });
    ASSERT_TRUE(publish.Succeeded());
    ASSERT_TRUE(publish.Undo.has_value());
    EXPECT_EQ(publish.State, Runtime::RenderArtifactPublicationState::Published);
    EXPECT_FALSE(publish.ProjectMutationAuthorized);

    std::optional<Runtime::RenderArtifactOperationResult> lastApply;
    Runtime::EditorCommandRecord applyCommand{
        .Label = "Apply Candidate Artifact",
        .Redo = [&registry, &lastApply]()
        {
            lastApply = registry.ApplyArtifact(Runtime::RenderArtifactApplyCommand{
                .ArtifactId = "artifact-candidate",
                .Provenance = "user accepted candidate",
                .ProjectTarget = "scene.material.base_color",
                .Label = "Apply Candidate Artifact",
                .UndoLabel = "Revert Candidate Artifact",
            });
            return StatusFor(*lastApply);
        },
        .Undo = [&registry]()
        {
            const Runtime::RenderArtifactOperationResult undo =
                registry.RevertAppliedArtifact(Runtime::RenderArtifactUndoCommand{
                    .ArtifactId = "artifact-candidate",
                    .Provenance = "undo accepted candidate",
                    .Label = "Revert Candidate Artifact",
                });
            return StatusFor(undo);
        },
        .Dirtying = true,
    };

    const Runtime::EditorCommandHistoryResult executed =
        history.Execute(std::move(applyCommand));
    ASSERT_TRUE(executed.Succeeded());
    ASSERT_TRUE(lastApply.has_value());
    EXPECT_TRUE(lastApply->ProjectMutationAuthorized);
    EXPECT_FALSE(lastApply->ProjectMutationPerformed);
    EXPECT_TRUE(history.IsDirty());
    EXPECT_EQ(registry.Find("artifact-candidate")->State,
              Runtime::RenderArtifactPublicationState::Applied);

    ASSERT_TRUE(history.Undo().Succeeded());
    EXPECT_EQ(registry.Find("artifact-candidate")->State,
              Runtime::RenderArtifactPublicationState::Published);

    const std::vector<Runtime::RenderArtifactAuditRecord> audit = registry.AuditLog();
    ASSERT_GE(audit.size(), 4u);
    EXPECT_EQ(audit[audit.size() - 2u].Action,
              Runtime::RenderArtifactAuditAction::Applied);
    EXPECT_EQ(audit.back().Action,
              Runtime::RenderArtifactAuditAction::ApplyReverted);
}

TEST(RenderArtifactPublication, RejectsImplicitApplyAndNonCandidateOutputs)
{
    Runtime::RenderArtifactRegistry registry;
    ASSERT_TRUE(registry.RegisterArtifact(MakeDeclaration("artifact-preview",
                                                          Runtime::RenderArtifactPublicationKind::PreviewOnly))
                    .Succeeded());

    Runtime::RenderArtifactOperationResult applyBeforePublish =
        registry.ApplyArtifact(Runtime::RenderArtifactApplyCommand{
            .ArtifactId = "artifact-preview",
            .Provenance = "test",
            .ProjectTarget = "scene.preview",
        });
    EXPECT_FALSE(applyBeforePublish.Succeeded());
    EXPECT_TRUE(Runtime::HasDiagnostic(
        applyBeforePublish,
        Runtime::RenderArtifactDiagnosticCode::ApplyRequiresPublished));

    ASSERT_TRUE(registry.PublishArtifact(Runtime::RenderArtifactPublishCommand{
                            .ArtifactId = "artifact-preview",
                            .Provenance = "test",
                            .TargetUri = "memory://published/preview",
                        })
                    .Succeeded());

    Runtime::RenderArtifactOperationResult nonCandidateApply =
        registry.ApplyArtifact(Runtime::RenderArtifactApplyCommand{
            .ArtifactId = "artifact-preview",
            .Provenance = "test",
            .ProjectTarget = "scene.preview",
        });
    EXPECT_FALSE(nonCandidateApply.Succeeded());
    EXPECT_TRUE(Runtime::HasDiagnostic(
        nonCandidateApply,
        Runtime::RenderArtifactDiagnosticCode::ArtifactNotCandidate));

    Runtime::RenderArtifactOperationResult missingProvenance =
        registry.PublishArtifact(Runtime::RenderArtifactPublishCommand{
            .ArtifactId = "artifact-missing",
            .TargetUri = "memory://published/missing",
        });
    EXPECT_FALSE(missingProvenance.Succeeded());
    EXPECT_TRUE(Runtime::HasDiagnostic(
        missingProvenance,
        Runtime::RenderArtifactDiagnosticCode::MissingProvenance));
}

TEST(RenderArtifactPublication, StaleCanceledAndFailedArtifactsStayDistinct)
{
    Runtime::RenderArtifactRegistry registry;
    ASSERT_TRUE(registry.RegisterArtifact(MakeDeclaration("artifact-stale"))
                    .Succeeded());
    ASSERT_TRUE(registry.RegisterArtifact(MakeDeclaration("artifact-canceled",
                                                          Runtime::RenderArtifactPublicationKind::PreviewOnly,
                                                          {"scene:2"}))
                    .Succeeded());
    ASSERT_TRUE(registry.RegisterArtifact(MakeDeclaration("artifact-failed",
                                                          Runtime::RenderArtifactPublicationKind::ReadbackMetric,
                                                          {"scene:3"}))
                    .Succeeded());

    EXPECT_TRUE(registry.MarkStale("artifact-stale", "source revision advanced")
                    .Succeeded());
    EXPECT_TRUE(registry.MarkCanceled("artifact-canceled", "job canceled")
                    .Succeeded());
    EXPECT_TRUE(registry.MarkFailed("artifact-failed", "readback failed")
                    .Succeeded());

    EXPECT_EQ(Runtime::ToUiStatus(*registry.Find("artifact-stale")),
              Runtime::RenderArtifactUiStatus::Stale);
    EXPECT_EQ(Runtime::ToUiStatus(*registry.Find("artifact-canceled")),
              Runtime::RenderArtifactUiStatus::Canceled);
    EXPECT_EQ(Runtime::ToUiStatus(*registry.Find("artifact-failed")),
              Runtime::RenderArtifactUiStatus::Failed);

    const Runtime::RenderArtifactOperationResult stalePublish =
        registry.PublishArtifact(Runtime::RenderArtifactPublishCommand{
            .ArtifactId = "artifact-stale",
            .Provenance = "test",
            .TargetUri = "memory://published/stale",
        });
    const Runtime::RenderArtifactOperationResult canceledPublish =
        registry.PublishArtifact(Runtime::RenderArtifactPublishCommand{
            .ArtifactId = "artifact-canceled",
            .Provenance = "test",
            .TargetUri = "memory://published/canceled",
        });
    const Runtime::RenderArtifactOperationResult failedPublish =
        registry.PublishArtifact(Runtime::RenderArtifactPublishCommand{
            .ArtifactId = "artifact-failed",
            .Provenance = "test",
            .TargetUri = "memory://published/failed",
        });

    EXPECT_TRUE(Runtime::HasDiagnostic(
        stalePublish,
        Runtime::RenderArtifactDiagnosticCode::ArtifactStale));
    EXPECT_TRUE(Runtime::HasDiagnostic(
        canceledPublish,
        Runtime::RenderArtifactDiagnosticCode::ArtifactCanceled));
    EXPECT_TRUE(Runtime::HasDiagnostic(
        failedPublish,
        Runtime::RenderArtifactDiagnosticCode::ArtifactFailed));
}
