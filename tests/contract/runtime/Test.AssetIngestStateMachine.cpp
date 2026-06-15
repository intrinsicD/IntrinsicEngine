#include <cstdint>
#include <string>
#include <utility>

#include <gtest/gtest.h>

import Extrinsic.Asset.ImportRouter;
import Extrinsic.Asset.Registry;
import Extrinsic.Core.Error;
import Extrinsic.Runtime.AssetIngestStateMachine;

namespace Assets = Extrinsic::Assets;
namespace Core = Extrinsic::Core;
namespace Runtime = Extrinsic::Runtime;

namespace
{
    [[nodiscard]] Runtime::RuntimeAssetIngestRequest ManualMeshRequest(
        std::string path = "mesh.obj")
    {
        return Runtime::RuntimeAssetIngestRequest{
            .Source = Runtime::RuntimeAssetIngestSource::ManualImport,
            .Path = std::move(path),
            .PayloadKind = Assets::AssetPayloadKind::Mesh,
        };
    }

    [[nodiscard]] Assets::AssetRouteDiagnostic ReadyRoute(
        const Assets::AssetPayloadKind payloadKind = Assets::AssetPayloadKind::Mesh)
    {
        return Assets::AssetRouteDiagnostic{
            .Status = Assets::AssetRouteStatus::Ready,
            .Error = Core::ErrorCode::Success,
            .Extension = "obj",
            .RequestedPayloadKind = payloadKind,
        };
    }

    void DriveToDecodeQueued(
        Runtime::RuntimeAssetIngestStateMachine& machine,
        const Runtime::RuntimeAssetIngestHandle handle)
    {
        ASSERT_TRUE(machine.ResolveRoute(handle, ReadyRoute()).Succeeded());
        ASSERT_TRUE(machine.QueueDecode(handle).Succeeded());
    }

    void DriveToApplying(
        Runtime::RuntimeAssetIngestStateMachine& machine,
        const Runtime::RuntimeAssetIngestHandle handle)
    {
        DriveToDecodeQueued(machine, handle);
        ASSERT_TRUE(machine.MarkDecoding(handle).Succeeded());
        ASSERT_TRUE(machine.CompleteDecode(handle, handle.Generation).Succeeded());
        ASSERT_TRUE(machine.BeginApply(handle).Succeeded());
    }
}

TEST(RuntimeAssetIngestStateMachine, AcceptsManualDropAndReimportSources)
{
    Runtime::RuntimeAssetIngestStateMachine machine;

    const auto manual = machine.Submit(ManualMeshRequest());
    ASSERT_TRUE(manual.Succeeded());
    EXPECT_EQ(manual.Phase, Runtime::RuntimeAssetIngestPhase::Queued);

    const auto dropped = machine.Submit(
        Runtime::RuntimeAssetIngestRequest{
            .Source = Runtime::RuntimeAssetIngestSource::DroppedFile,
            .Path = "cloud.ply",
            .PayloadKind = Assets::AssetPayloadKind::PointCloud,
        });
    ASSERT_TRUE(dropped.Succeeded());

    const auto reimport = machine.Submit(
        Runtime::RuntimeAssetIngestRequest{
            .Source = Runtime::RuntimeAssetIngestSource::Reimport,
            .Path = "texture.png",
            .PayloadKind = Assets::AssetPayloadKind::Texture2D,
            .ExistingAsset = Assets::AssetId{7u, 3u},
        });
    ASSERT_TRUE(reimport.Succeeded());

    EXPECT_EQ(machine.ActiveCount(), 3u);
    EXPECT_EQ(
        Runtime::DebugNameForRuntimeAssetIngestSource(Runtime::RuntimeAssetIngestSource::ManualImport),
        std::string{"ManualImport"});
    EXPECT_EQ(
        Runtime::DebugNameForRuntimeAssetIngestSource(Runtime::RuntimeAssetIngestSource::DroppedFile),
        std::string{"DroppedFile"});
    EXPECT_EQ(
        Runtime::DebugNameForRuntimeAssetIngestSource(Runtime::RuntimeAssetIngestSource::Reimport),
        std::string{"Reimport"});
    EXPECT_EQ(
        Runtime::DebugNameForRuntimeAssetIngestPhase(Runtime::RuntimeAssetIngestPhase::AwaitingMainThreadApply),
        std::string{"AwaitingMainThreadApply"});
    EXPECT_EQ(
        Runtime::DebugNameForRuntimeAssetIngestDiagnostic(Runtime::RuntimeAssetIngestDiagnostic::MaterializationFailed),
        std::string{"MaterializationFailed"});
}

TEST(RuntimeAssetIngestStateMachine, RejectsInvalidSubmissionInputsAsFailedRecords)
{
    Runtime::RuntimeAssetIngestStateMachine machine;

    const auto missingPath = machine.Submit(ManualMeshRequest(""));
    EXPECT_FALSE(missingPath.Succeeded());
    EXPECT_EQ(missingPath.Phase, Runtime::RuntimeAssetIngestPhase::Failed);
    EXPECT_EQ(missingPath.Diagnostic, Runtime::RuntimeAssetIngestDiagnostic::MissingPath);
    EXPECT_EQ(missingPath.Error, Core::ErrorCode::InvalidPath);

    const auto invalidReimport = machine.Submit(
        Runtime::RuntimeAssetIngestRequest{
            .Source = Runtime::RuntimeAssetIngestSource::Reimport,
            .Path = "mesh.obj",
            .PayloadKind = Assets::AssetPayloadKind::Mesh,
        });
    EXPECT_FALSE(invalidReimport.Succeeded());
    EXPECT_EQ(invalidReimport.Phase, Runtime::RuntimeAssetIngestPhase::Failed);
    EXPECT_EQ(
        invalidReimport.Diagnostic,
        Runtime::RuntimeAssetIngestDiagnostic::InvalidReimportTarget);
    EXPECT_EQ(machine.ActiveCount(), 0u);
    EXPECT_EQ(machine.TotalCount(), 2u);
}

TEST(RuntimeAssetIngestStateMachine, SuppressesDuplicateActiveRequestsUntilTerminal)
{
    Runtime::RuntimeAssetIngestStateMachine machine;

    const auto first = machine.Submit(ManualMeshRequest());
    ASSERT_TRUE(first.Succeeded());
    const auto duplicate = machine.Submit(ManualMeshRequest());

    EXPECT_TRUE(duplicate.Duplicate);
    EXPECT_FALSE(duplicate.Mutated);
    EXPECT_EQ(duplicate.Handle, first.Handle);
    EXPECT_EQ(
        duplicate.Diagnostic,
        Runtime::RuntimeAssetIngestDiagnostic::DuplicateActiveRequest);
    EXPECT_EQ(duplicate.Error, Core::ErrorCode::ResourceBusy);
    EXPECT_EQ(machine.TotalCount(), 1u);
    EXPECT_EQ(machine.ActiveCount(), 1u);

    DriveToApplying(machine, first.Handle);
    Runtime::RuntimeAssetIngestResult result{};
    result.PayloadKind = Assets::AssetPayloadKind::Mesh;
    result.Asset = Assets::AssetId{11u, 1u};
    result.PrimitiveEntitiesCreated = 1u;
    const auto complete =
        machine.CompleteApply(first.Handle, first.Handle.Generation, result);
    ASSERT_TRUE(complete.Succeeded());
    EXPECT_EQ(complete.Phase, Runtime::RuntimeAssetIngestPhase::Complete);
    EXPECT_EQ(machine.ActiveCount(), 0u);

    const auto afterTerminal = machine.Submit(ManualMeshRequest());
    EXPECT_TRUE(afterTerminal.Succeeded());
    EXPECT_FALSE(afterTerminal.Duplicate);
    EXPECT_NE(afterTerminal.Handle, first.Handle);
    EXPECT_EQ(machine.TotalCount(), 2u);
    EXPECT_EQ(machine.ActiveCount(), 1u);
}

TEST(RuntimeAssetIngestStateMachine, MapsRouteDiagnosticsToFailureTaxonomy)
{
    Runtime::RuntimeAssetIngestStateMachine machine;

    const auto missingExtension = machine.Submit(ManualMeshRequest("mesh"));
    ASSERT_TRUE(missingExtension.Succeeded());
    const auto missingExtensionResult = machine.ResolveRoute(
        missingExtension.Handle,
        Assets::AssetRouteDiagnostic{
            .Status = Assets::AssetRouteStatus::MissingExtension,
            .Error = Core::ErrorCode::InvalidPath,
        });
    EXPECT_EQ(
        missingExtensionResult.Diagnostic,
        Runtime::RuntimeAssetIngestDiagnostic::MissingExtension);
    EXPECT_EQ(missingExtensionResult.Phase, Runtime::RuntimeAssetIngestPhase::Failed);

    const auto unsupported = machine.Submit(ManualMeshRequest("mesh.foo"));
    ASSERT_TRUE(unsupported.Succeeded());
    const auto unsupportedResult = machine.ResolveRoute(
        unsupported.Handle,
        Assets::AssetRouteDiagnostic{
            .Status = Assets::AssetRouteStatus::UnsupportedExtension,
            .Error = Core::ErrorCode::AssetUnsupportedFormat,
        });
    EXPECT_EQ(
        unsupportedResult.Diagnostic,
        Runtime::RuntimeAssetIngestDiagnostic::UnsupportedExtension);

    const auto ambiguous = machine.Submit(ManualMeshRequest("mesh.ply"));
    ASSERT_TRUE(ambiguous.Succeeded());
    const auto ambiguousResult = machine.ResolveRoute(
        ambiguous.Handle,
        Assets::AssetRouteDiagnostic{
            .Status = Assets::AssetRouteStatus::AmbiguousPayloadKind,
            .Error = Core::ErrorCode::InvalidArgument,
        });
    EXPECT_EQ(
        ambiguousResult.Diagnostic,
        Runtime::RuntimeAssetIngestDiagnostic::AmbiguousPayloadKind);

    const auto unsupportedPayload = machine.Submit(ManualMeshRequest("image.png"));
    ASSERT_TRUE(unsupportedPayload.Succeeded());
    const auto unsupportedPayloadResult = machine.ResolveRoute(
        unsupportedPayload.Handle,
        Assets::AssetRouteDiagnostic{
            .Status = Assets::AssetRouteStatus::PayloadKindNotSupported,
            .Error = Core::ErrorCode::AssetUnsupportedFormat,
        });
    EXPECT_EQ(
        unsupportedPayloadResult.Diagnostic,
        Runtime::RuntimeAssetIngestDiagnostic::PayloadKindNotSupported);
}

TEST(RuntimeAssetIngestStateMachine, RecordsDecodeCallbackApplyAndMissingFileFailures)
{
    Runtime::RuntimeAssetIngestStateMachine machine;

    const auto missingFile = machine.Submit(ManualMeshRequest("missing.obj"));
    ASSERT_TRUE(missingFile.Succeeded());
    const auto missingFileResult = machine.MarkMissingFile(missingFile.Handle);
    EXPECT_EQ(
        missingFileResult.Diagnostic,
        Runtime::RuntimeAssetIngestDiagnostic::MissingFile);
    EXPECT_EQ(missingFileResult.Error, Core::ErrorCode::FileNotFound);

    const auto decodeFailure = machine.Submit(ManualMeshRequest("decode.obj"));
    ASSERT_TRUE(decodeFailure.Succeeded());
    DriveToDecodeQueued(machine, decodeFailure.Handle);
    const auto decodeResult = machine.FailDecode(
        decodeFailure.Handle,
        decodeFailure.Handle.Generation,
        Core::ErrorCode::AssetDecodeFailed);
    EXPECT_EQ(
        decodeResult.Diagnostic,
        Runtime::RuntimeAssetIngestDiagnostic::DecodeFailed);
    EXPECT_EQ(decodeResult.Phase, Runtime::RuntimeAssetIngestPhase::Failed);

    const auto callbackFailure = machine.Submit(ManualMeshRequest("callback.obj"));
    ASSERT_TRUE(callbackFailure.Succeeded());
    DriveToDecodeQueued(machine, callbackFailure.Handle);
    const auto callbackResult = machine.FailCallback(
        callbackFailure.Handle,
        Core::ErrorCode::AssetLoaderMissing);
    EXPECT_EQ(
        callbackResult.Diagnostic,
        Runtime::RuntimeAssetIngestDiagnostic::CallbackFailed);

    const auto applyFailure = machine.Submit(ManualMeshRequest("apply.obj"));
    ASSERT_TRUE(applyFailure.Succeeded());
    DriveToApplying(machine, applyFailure.Handle);
    const auto applyResult = machine.FailApply(
        applyFailure.Handle,
        applyFailure.Handle.Generation,
        Core::ErrorCode::AssetInvalidData);
    EXPECT_EQ(
        applyResult.Diagnostic,
        Runtime::RuntimeAssetIngestDiagnostic::MaterializationFailed);
    EXPECT_EQ(applyResult.Phase, Runtime::RuntimeAssetIngestPhase::Failed);
}

TEST(RuntimeAssetIngestStateMachine, CancellationAndStaleCompletionsDoNotMutateRecords)
{
    Runtime::RuntimeAssetIngestStateMachine machine;

    const auto cancelled = machine.Submit(ManualMeshRequest("cancel.obj"));
    ASSERT_TRUE(cancelled.Succeeded());
    DriveToDecodeQueued(machine, cancelled.Handle);
    ASSERT_TRUE(machine.MarkDecoding(cancelled.Handle).Succeeded());
    const auto cancelResult = machine.Cancel(cancelled.Handle);
    EXPECT_EQ(cancelResult.Phase, Runtime::RuntimeAssetIngestPhase::Cancelled);
    EXPECT_EQ(
        cancelResult.Diagnostic,
        Runtime::RuntimeAssetIngestDiagnostic::Cancelled);

    const auto lateCompletion = machine.CompleteDecode(
        cancelled.Handle,
        cancelled.Handle.Generation);
    EXPECT_FALSE(lateCompletion.Mutated);
    EXPECT_EQ(
        lateCompletion.Diagnostic,
        Runtime::RuntimeAssetIngestDiagnostic::StaleCompletion);
    const auto cancelledSnapshot = machine.Snapshot(cancelled.Handle);
    ASSERT_TRUE(cancelledSnapshot.has_value());
    EXPECT_EQ(cancelledSnapshot->Phase, Runtime::RuntimeAssetIngestPhase::Cancelled);

    const auto stale = machine.Submit(ManualMeshRequest("stale.obj"));
    ASSERT_TRUE(stale.Succeeded());
    DriveToDecodeQueued(machine, stale.Handle);
    ASSERT_TRUE(machine.MarkDecoding(stale.Handle).Succeeded());
    const auto staleCompletion = machine.CompleteDecode(
        stale.Handle,
        stale.Handle.Generation + 1u);
    EXPECT_FALSE(staleCompletion.Mutated);
    EXPECT_EQ(
        staleCompletion.Diagnostic,
        Runtime::RuntimeAssetIngestDiagnostic::StaleCompletion);
    const auto staleSnapshot = machine.Snapshot(stale.Handle);
    ASSERT_TRUE(staleSnapshot.has_value());
    EXPECT_EQ(staleSnapshot->Phase, Runtime::RuntimeAssetIngestPhase::Decoding);
}

TEST(RuntimeAssetIngestStateMachine, RejectsInvalidTransitionsAndUnknownHandles)
{
    Runtime::RuntimeAssetIngestStateMachine machine;
    const auto submitted = machine.Submit(ManualMeshRequest());
    ASSERT_TRUE(submitted.Succeeded());

    const auto invalidTransition = machine.QueueDecode(submitted.Handle);
    EXPECT_FALSE(invalidTransition.Mutated);
    EXPECT_EQ(
        invalidTransition.Diagnostic,
        Runtime::RuntimeAssetIngestDiagnostic::InvalidTransition);
    EXPECT_EQ(invalidTransition.Error, Core::ErrorCode::InvalidState);

    const auto unknown = machine.ResolveRoute(
        Runtime::RuntimeAssetIngestHandle{999u, 1u},
        ReadyRoute());
    EXPECT_FALSE(unknown.Mutated);
    EXPECT_EQ(
        unknown.Diagnostic,
        Runtime::RuntimeAssetIngestDiagnostic::UnknownHandle);
    EXPECT_EQ(unknown.Error, Core::ErrorCode::ResourceNotFound);
}
