module;

#include <string_view>
#include <utility>

module Extrinsic.Runtime.AssetGeometryIO;

import Extrinsic.Asset.GeometryIOBridge;
import Extrinsic.Asset.ImportRouter;
import Extrinsic.Core.Error;
import Core.Error;
import Geometry.Graph.IO;
import Geometry.HalfedgeMesh.IO;
import Geometry.PointCloud.IO;

namespace Extrinsic::Runtime
{
    namespace
    {
        namespace Assets = Extrinsic::Assets;

        [[nodiscard]] Core::ErrorCode MapGeometryError(
            const ::Core::ErrorCode error) noexcept
        {
            using LegacyErrorCode = ::Core::ErrorCode;
            switch (error)
            {
            case LegacyErrorCode::Success:
                return Core::ErrorCode::Success;
            case LegacyErrorCode::OutOfMemory:
                return Core::ErrorCode::OutOfMemory;
            case LegacyErrorCode::ResourceNotFound:
                return Core::ErrorCode::ResourceNotFound;
            case LegacyErrorCode::ResourceBusy:
                return Core::ErrorCode::ResourceBusy;
            case LegacyErrorCode::ResourceCorrupted:
                return Core::ErrorCode::ResourceCorrupted;
            case LegacyErrorCode::FileNotFound:
                return Core::ErrorCode::FileNotFound;
            case LegacyErrorCode::FileReadError:
                return Core::ErrorCode::FileReadError;
            case LegacyErrorCode::FileWriteError:
                return Core::ErrorCode::FileWriteError;
            case LegacyErrorCode::InvalidPath:
                return Core::ErrorCode::InvalidPath;
            case LegacyErrorCode::PermissionDenied:
                return Core::ErrorCode::PermissionDenied;
            case LegacyErrorCode::InvalidArgument:
                return Core::ErrorCode::InvalidArgument;
            case LegacyErrorCode::InvalidState:
                return Core::ErrorCode::InvalidState;
            case LegacyErrorCode::InvalidFormat:
                return Core::ErrorCode::InvalidFormat;
            case LegacyErrorCode::OutOfRange:
                return Core::ErrorCode::OutOfRange;
            case LegacyErrorCode::TypeMismatch:
                return Core::ErrorCode::TypeMismatch;
            case LegacyErrorCode::AssetNotLoaded:
                return Core::ErrorCode::AssetNotLoaded;
            case LegacyErrorCode::AssetLoadFailed:
                return Core::ErrorCode::AssetLoadFailed;
            case LegacyErrorCode::AssetTypeMismatch:
                return Core::ErrorCode::AssetTypeMismatch;
            case LegacyErrorCode::ThreadViolation:
                return Core::ErrorCode::ThreadViolation;
            case LegacyErrorCode::DeadlockDetected:
                return Core::ErrorCode::DeadlockDetected;
            default:
                return Core::ErrorCode::Unknown;
            }
        }

        template <class T>
        [[nodiscard]] Core::Expected<T> PromoteGeometryExpected(
            ::Core::Expected<T> value)
        {
            if (!value.has_value())
            {
                return Core::Err<T>(MapGeometryError(value.error()));
            }
            return std::move(*value);
        }

        [[nodiscard]] Core::Result MapMeshWriteStatus(
            const Geometry::MeshIO::MeshIOWriteStatus status) noexcept
        {
            using Geometry::MeshIO::MeshIOWriteStatus;
            switch (status)
            {
            case MeshIOWriteStatus::Success:
                return Core::Ok();
            case MeshIOWriteStatus::EmptyMesh:
                return Core::Err(Core::ErrorCode::AssetInvalidData);
            case MeshIOWriteStatus::InvalidFace:
                return Core::Err(Core::ErrorCode::InvalidFormat);
            case MeshIOWriteStatus::InvalidPath:
                return Core::Err(Core::ErrorCode::InvalidPath);
            case MeshIOWriteStatus::FileWriteError:
                return Core::Err(Core::ErrorCode::FileWriteError);
            }
            return Core::Err(Core::ErrorCode::Unknown);
        }

        [[nodiscard]] Core::Result MapPointCloudWriteStatus(
            const Geometry::PointCloudIO::PointCloudIOWriteStatus status) noexcept
        {
            using Geometry::PointCloudIO::PointCloudIOWriteStatus;
            switch (status)
            {
            case PointCloudIOWriteStatus::Success:
                return Core::Ok();
            case PointCloudIOWriteStatus::EmptyCloud:
                return Core::Err(Core::ErrorCode::AssetInvalidData);
            case PointCloudIOWriteStatus::InvalidPath:
                return Core::Err(Core::ErrorCode::InvalidPath);
            case PointCloudIOWriteStatus::FileWriteError:
                return Core::Err(Core::ErrorCode::FileWriteError);
            }
            return Core::Err(Core::ErrorCode::Unknown);
        }

        [[nodiscard]] Core::Result MapGraphWriteStatus(
            const Geometry::GraphIO::GraphIOWriteStatus status) noexcept
        {
            using Geometry::GraphIO::GraphIOWriteStatus;
            switch (status)
            {
            case GraphIOWriteStatus::Success:
                return Core::Ok();
            case GraphIOWriteStatus::InvalidPath:
                return Core::Err(Core::ErrorCode::InvalidPath);
            case GraphIOWriteStatus::EmptyGraph:
                return Core::Err(Core::ErrorCode::AssetInvalidData);
            case GraphIOWriteStatus::FileWriteError:
                return Core::Err(Core::ErrorCode::FileWriteError);
            }
            return Core::Err(Core::ErrorCode::Unknown);
        }

        template <class Loader>
        [[nodiscard]] Core::Result RegisterMeshImporter(
            Assets::AssetGeometryIOBridge& bridge,
            const Assets::AssetFileFormat format,
            Loader&& loader)
        {
            return bridge.RegisterTypedImporter<Geometry::MeshIO::MeshIOResult>(
                format,
                Assets::AssetPayloadKind::Mesh,
                [loader = std::forward<Loader>(loader)](
                    const Assets::AssetGeometryIORequest& request)
                    -> Core::Expected<Geometry::MeshIO::MeshIOResult>
                {
                    return PromoteGeometryExpected(loader(request.Path));
                },
                "Geometry::MeshIO::MeshIOResult");
        }

        template <class Loader>
        [[nodiscard]] Core::Result RegisterPointCloudImporter(
            Assets::AssetGeometryIOBridge& bridge,
            const Assets::AssetFileFormat format,
            Loader&& loader)
        {
            return bridge.RegisterTypedImporter<Geometry::PointCloudIO::PointCloudIOResult>(
                format,
                Assets::AssetPayloadKind::PointCloud,
                [loader = std::forward<Loader>(loader)](
                    const Assets::AssetGeometryIORequest& request)
                    -> Core::Expected<Geometry::PointCloudIO::PointCloudIOResult>
                {
                    return PromoteGeometryExpected(loader(request.Path));
                },
                "Geometry::PointCloudIO::PointCloudIOResult");
        }

        template <class Loader>
        [[nodiscard]] Core::Result RegisterGraphImporter(
            Assets::AssetGeometryIOBridge& bridge,
            const Assets::AssetFileFormat format,
            Loader&& loader)
        {
            return bridge.RegisterTypedImporter<Geometry::GraphIO::GraphIOResult>(
                format,
                Assets::AssetPayloadKind::Graph,
                [loader = std::forward<Loader>(loader)](
                    const Assets::AssetGeometryIORequest& request)
                    -> Core::Expected<Geometry::GraphIO::GraphIOResult>
                {
                    return PromoteGeometryExpected(loader(request.Path));
                },
                "Geometry::GraphIO::GraphIOResult");
        }

        [[nodiscard]] Core::Result RegisterMeshExporter(
            Assets::AssetGeometryIOBridge& bridge,
            const Assets::AssetFileFormat format,
            Geometry::MeshIO::MeshIOWriteStatus (*writer)(
                std::string_view,
                const Geometry::MeshIO::MeshIOResult&))
        {
            return bridge.RegisterTypedExporter<Geometry::MeshIO::MeshIOResult>(
                format,
                Assets::AssetPayloadKind::Mesh,
                [writer](const Assets::AssetGeometryIORequest& request,
                         const Geometry::MeshIO::MeshIOResult& mesh) -> Core::Result
                {
                    return MapMeshWriteStatus(writer(request.Path, mesh));
                });
        }

        [[nodiscard]] Core::Result RegisterPointCloudExporter(
            Assets::AssetGeometryIOBridge& bridge,
            const Assets::AssetFileFormat format,
            Geometry::PointCloudIO::PointCloudIOWriteStatus (*writer)(
                std::string_view,
                const Geometry::PointCloudIO::PointCloudIOResult&))
        {
            return bridge.RegisterTypedExporter<Geometry::PointCloudIO::PointCloudIOResult>(
                format,
                Assets::AssetPayloadKind::PointCloud,
                [writer](const Assets::AssetGeometryIORequest& request,
                         const Geometry::PointCloudIO::PointCloudIOResult& cloud) -> Core::Result
                {
                    return MapPointCloudWriteStatus(writer(request.Path, cloud));
                });
        }

        [[nodiscard]] Core::Result RegisterGraphExporter(
            Assets::AssetGeometryIOBridge& bridge,
            const Assets::AssetFileFormat format,
            Geometry::GraphIO::GraphIOWriteStatus (*writer)(
                std::string_view,
                const Geometry::GraphIO::GraphIOResult&))
        {
            return bridge.RegisterTypedExporter<Geometry::GraphIO::GraphIOResult>(
                format,
                Assets::AssetPayloadKind::Graph,
                [writer](const Assets::AssetGeometryIORequest& request,
                         const Geometry::GraphIO::GraphIOResult& graph) -> Core::Result
                {
                    return MapGraphWriteStatus(writer(request.Path, graph));
                });
        }

        [[nodiscard]] Core::Result Combine(Core::Result lhs, const Core::Result& rhs)
        {
            if (!lhs.has_value())
            {
                return lhs;
            }
            return rhs;
        }
    }

    Core::Result RegisterPromotedGeometryIOCallbacks(
        Assets::AssetGeometryIOBridge& bridge)
    {
        auto result = RegisterMeshImporter(bridge, Assets::AssetFileFormat::OBJ, Geometry::MeshIO::LoadOBJ);
        result = Combine(std::move(result), RegisterMeshImporter(bridge, Assets::AssetFileFormat::OFF, Geometry::MeshIO::LoadOFF));
        result = Combine(std::move(result), RegisterMeshImporter(bridge, Assets::AssetFileFormat::STL, Geometry::MeshIO::LoadSTL));
        result = Combine(std::move(result), RegisterMeshImporter(bridge, Assets::AssetFileFormat::PLY, Geometry::MeshIO::LoadPLY));

        result = Combine(std::move(result), RegisterPointCloudImporter(bridge, Assets::AssetFileFormat::XYZ, Geometry::PointCloudIO::LoadXYZ));
        result = Combine(std::move(result), RegisterPointCloudImporter(bridge, Assets::AssetFileFormat::PTS, Geometry::PointCloudIO::LoadXYZ));
        result = Combine(std::move(result), RegisterPointCloudImporter(bridge, Assets::AssetFileFormat::XYZRGB, Geometry::PointCloudIO::LoadXYZ));
        result = Combine(std::move(result), RegisterPointCloudImporter(bridge, Assets::AssetFileFormat::PCD, Geometry::PointCloudIO::LoadPCD));
        result = Combine(std::move(result), RegisterPointCloudImporter(bridge, Assets::AssetFileFormat::PLY, Geometry::PointCloudIO::LoadPLY));

        result = Combine(std::move(result), RegisterGraphImporter(bridge, Assets::AssetFileFormat::TGF, Geometry::GraphIO::LoadTGF));
        result = Combine(std::move(result), RegisterGraphImporter(bridge, Assets::AssetFileFormat::EdgeList, Geometry::GraphIO::LoadEdgeList));

        result = Combine(std::move(result), RegisterMeshExporter(bridge, Assets::AssetFileFormat::OBJ, Geometry::MeshIO::WriteOBJ));
        result = Combine(std::move(result), RegisterMeshExporter(bridge, Assets::AssetFileFormat::STL, Geometry::MeshIO::WriteSTL));
        result = Combine(std::move(result), RegisterMeshExporter(bridge, Assets::AssetFileFormat::PLY, Geometry::MeshIO::WritePLY));

        result = Combine(std::move(result), RegisterPointCloudExporter(bridge, Assets::AssetFileFormat::PLY, Geometry::PointCloudIO::WritePLY));
        result = Combine(std::move(result), RegisterPointCloudExporter(bridge, Assets::AssetFileFormat::XYZ, Geometry::PointCloudIO::WriteXYZ));
        result = Combine(std::move(result), RegisterPointCloudExporter(bridge, Assets::AssetFileFormat::PCD, Geometry::PointCloudIO::WritePCD));

        result = Combine(std::move(result), RegisterGraphExporter(bridge, Assets::AssetFileFormat::TGF, Geometry::GraphIO::WriteTGF));
        result = Combine(std::move(result), RegisterGraphExporter(bridge, Assets::AssetFileFormat::EdgeList, Geometry::GraphIO::WriteEdgeList));
        return result;
    }
}
