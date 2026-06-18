module;

#include <string_view>
#include <utility>

module Extrinsic.Runtime.AssetGeometryIO;

import Extrinsic.Asset.GeometryIOBridge;
import Extrinsic.Asset.ImportRouter;
import Extrinsic.Core.Error;
import Geometry.Graph.IO;
import Geometry.HalfedgeMesh.IO;
import Geometry.PointCloud.IO;

namespace Extrinsic::Runtime
{
    namespace
    {
        namespace Assets = Extrinsic::Assets;

        [[nodiscard]] Extrinsic::Core::Result MapMeshWriteStatus(
            const Geometry::MeshIO::MeshIOWriteStatus status) noexcept
        {
            using Geometry::MeshIO::MeshIOWriteStatus;
            switch (status)
            {
            case MeshIOWriteStatus::Success:
                return Extrinsic::Core::Ok();
            case MeshIOWriteStatus::EmptyMesh:
                return Extrinsic::Core::Err(Extrinsic::Core::ErrorCode::AssetInvalidData);
            case MeshIOWriteStatus::InvalidFace:
                return Extrinsic::Core::Err(Extrinsic::Core::ErrorCode::InvalidFormat);
            case MeshIOWriteStatus::InvalidPath:
                return Extrinsic::Core::Err(Extrinsic::Core::ErrorCode::InvalidPath);
            case MeshIOWriteStatus::FileWriteError:
                return Extrinsic::Core::Err(Extrinsic::Core::ErrorCode::FileWriteError);
            }
            return Extrinsic::Core::Err(Extrinsic::Core::ErrorCode::Unknown);
        }

        [[nodiscard]] Extrinsic::Core::Result MapPointCloudWriteStatus(
            const Geometry::PointCloudIO::PointCloudIOWriteStatus status) noexcept
        {
            using Geometry::PointCloudIO::PointCloudIOWriteStatus;
            switch (status)
            {
            case PointCloudIOWriteStatus::Success:
                return Extrinsic::Core::Ok();
            case PointCloudIOWriteStatus::EmptyCloud:
                return Extrinsic::Core::Err(Extrinsic::Core::ErrorCode::AssetInvalidData);
            case PointCloudIOWriteStatus::InvalidPath:
                return Extrinsic::Core::Err(Extrinsic::Core::ErrorCode::InvalidPath);
            case PointCloudIOWriteStatus::FileWriteError:
                return Extrinsic::Core::Err(Extrinsic::Core::ErrorCode::FileWriteError);
            }
            return Extrinsic::Core::Err(Extrinsic::Core::ErrorCode::Unknown);
        }

        [[nodiscard]] Extrinsic::Core::Result MapGraphWriteStatus(
            const Geometry::GraphIO::GraphIOWriteStatus status) noexcept
        {
            using Geometry::GraphIO::GraphIOWriteStatus;
            switch (status)
            {
            case GraphIOWriteStatus::Success:
                return Extrinsic::Core::Ok();
            case GraphIOWriteStatus::InvalidPath:
                return Extrinsic::Core::Err(Extrinsic::Core::ErrorCode::InvalidPath);
            case GraphIOWriteStatus::EmptyGraph:
                return Extrinsic::Core::Err(Extrinsic::Core::ErrorCode::AssetInvalidData);
            case GraphIOWriteStatus::FileWriteError:
                return Extrinsic::Core::Err(Extrinsic::Core::ErrorCode::FileWriteError);
            }
            return Extrinsic::Core::Err(Extrinsic::Core::ErrorCode::Unknown);
        }

        template <class Loader>
        [[nodiscard]] Extrinsic::Core::Result RegisterMeshImporter(
            Assets::AssetGeometryIOBridge& bridge,
            const Assets::AssetFileFormat format,
            Loader&& loader)
        {
            return bridge.RegisterTypedImporter<Geometry::MeshIO::MeshIOResult>(
                format,
                Assets::AssetPayloadKind::Mesh,
                [loader = std::forward<Loader>(loader)](
                    const Assets::AssetGeometryIORequest& request)
                    -> Extrinsic::Core::Expected<Geometry::MeshIO::MeshIOResult>
                {
                    return loader(request.Path);
                },
                "Geometry::MeshIO::MeshIOResult");
        }

        template <class Loader>
        [[nodiscard]] Extrinsic::Core::Result RegisterPointCloudImporter(
            Assets::AssetGeometryIOBridge& bridge,
            const Assets::AssetFileFormat format,
            Loader&& loader)
        {
            return bridge.RegisterTypedImporter<Geometry::PointCloudIO::PointCloudIOResult>(
                format,
                Assets::AssetPayloadKind::PointCloud,
                [loader = std::forward<Loader>(loader)](
                    const Assets::AssetGeometryIORequest& request)
                    -> Extrinsic::Core::Expected<Geometry::PointCloudIO::PointCloudIOResult>
                {
                    return loader(request.Path);
                },
                "Geometry::PointCloudIO::PointCloudIOResult");
        }

        template <class Loader>
        [[nodiscard]] Extrinsic::Core::Result RegisterGraphImporter(
            Assets::AssetGeometryIOBridge& bridge,
            const Assets::AssetFileFormat format,
            Loader&& loader)
        {
            return bridge.RegisterTypedImporter<Geometry::GraphIO::GraphIOResult>(
                format,
                Assets::AssetPayloadKind::Graph,
                [loader = std::forward<Loader>(loader)](
                    const Assets::AssetGeometryIORequest& request)
                    -> Extrinsic::Core::Expected<Geometry::GraphIO::GraphIOResult>
                {
                    return loader(request.Path);
                },
                "Geometry::GraphIO::GraphIOResult");
        }

        [[nodiscard]] Extrinsic::Core::Result RegisterMeshExporter(
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
                         const Geometry::MeshIO::MeshIOResult& mesh) -> Extrinsic::Core::Result
                {
                    return MapMeshWriteStatus(writer(request.Path, mesh));
                });
        }

        [[nodiscard]] Extrinsic::Core::Result RegisterPointCloudExporter(
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
                         const Geometry::PointCloudIO::PointCloudIOResult& cloud) -> Extrinsic::Core::Result
                {
                    return MapPointCloudWriteStatus(writer(request.Path, cloud));
                });
        }

        [[nodiscard]] Extrinsic::Core::Result RegisterGraphExporter(
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
                         const Geometry::GraphIO::GraphIOResult& graph) -> Extrinsic::Core::Result
                {
                    return MapGraphWriteStatus(writer(request.Path, graph));
                });
        }

        [[nodiscard]] Extrinsic::Core::Result Combine(Extrinsic::Core::Result lhs, const Extrinsic::Core::Result& rhs)
        {
            if (!lhs.has_value())
            {
                return lhs;
            }
            return rhs;
        }
    }

    Extrinsic::Core::Result RegisterPromotedGeometryIOCallbacks(
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
