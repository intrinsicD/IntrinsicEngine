#include <gtest/gtest.h>

import Extrinsic.Asset.OperationStatus;
import Extrinsic.Core.Error;

using namespace Extrinsic::Assets;
using Extrinsic::Core::ErrorCode;

TEST(AssetOperationStatus, ClassifiesSuccessfulOperations)
{
    const AssetOperationDiagnostic diagnostic =
        DiagnoseAssetOperation(AssetOperation::Load, ErrorCode::Success);

    EXPECT_EQ(diagnostic.Operation, AssetOperation::Load);
    EXPECT_EQ(diagnostic.Status, AssetOperationStatus::Succeeded);
    EXPECT_EQ(diagnostic.Error, ErrorCode::Success);
    EXPECT_STREQ(DebugNameForAssetOperation(diagnostic.Operation), "Load");
    EXPECT_STREQ(
        DebugNameForAssetOperationStatus(diagnostic.Status),
        "Succeeded");
}

TEST(AssetOperationStatus, ClassifiesCallbackValidationAndFormatFailures)
{
    EXPECT_EQ(
        ClassifyAssetOperationStatus(ErrorCode::AssetLoaderMissing),
        AssetOperationStatus::LoaderMissing);
    EXPECT_EQ(
        ClassifyAssetOperationStatus(ErrorCode::AssetDecodeFailed),
        AssetOperationStatus::CallbackFailed);
    EXPECT_EQ(
        ClassifyAssetOperationStatus(ErrorCode::AssetInvalidData),
        AssetOperationStatus::ValidationFailed);
    EXPECT_EQ(
        ClassifyAssetOperationStatus(ErrorCode::AssetUnsupportedFormat),
        AssetOperationStatus::UnsupportedFormat);
}

TEST(AssetOperationStatus, ClassifiesStateTypeIoAndUploadFailures)
{
    EXPECT_EQ(
        ClassifyAssetOperationStatus(ErrorCode::InvalidState),
        AssetOperationStatus::InvalidState);
    EXPECT_EQ(
        ClassifyAssetOperationStatus(ErrorCode::AssetTypeMismatch),
        AssetOperationStatus::TypeMismatch);
    EXPECT_EQ(
        ClassifyAssetOperationStatus(ErrorCode::FileReadError),
        AssetOperationStatus::IoFailed);
    EXPECT_EQ(
        ClassifyAssetOperationStatus(ErrorCode::AssetUploadFailed),
        AssetOperationStatus::UploadFailed);
}

TEST(AssetOperationStatus, ProvidesStableDebugNames)
{
    EXPECT_STREQ(
        DebugNameForAssetOperation(AssetOperation::Reload),
        "Reload");
    EXPECT_STREQ(
        DebugNameForAssetOperation(AssetOperation::Destroy),
        "Destroy");
    EXPECT_STREQ(
        DebugNameForAssetOperationStatus(AssetOperationStatus::LoaderMissing),
        "LoaderMissing");
    EXPECT_STREQ(
        DebugNameForAssetOperationStatus(AssetOperationStatus::UnknownFailure),
        "UnknownFailure");
}
