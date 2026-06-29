module;

#include <string>
#include <string_view>

export module Geometry.PointCloud.IO;

import Geometry.PointCloud;
import Extrinsic.Core.Error;

export namespace Geometry::PointCloudIO
{
    struct PointCloudIOResult
    {
        PointCloud::Cloud Cloud{};

        std::string SourcePath;       // Original file path (for error messages)
        std::string BasePath;         // Directory containing the file (for relative refs)
    };

    Extrinsic::Core::Expected<PointCloudIOResult> LoadXYZ(std::string_view absolute_path);
    Extrinsic::Core::Expected<PointCloudIOResult> LoadPTS(std::string_view absolute_path);
    Extrinsic::Core::Expected<PointCloudIOResult> LoadPWN(std::string_view absolute_path);
    Extrinsic::Core::Expected<PointCloudIOResult> LoadCSV(std::string_view absolute_path);
    Extrinsic::Core::Expected<PointCloudIOResult> Load3D(std::string_view absolute_path);
    Extrinsic::Core::Expected<PointCloudIOResult> LoadTXT(std::string_view absolute_path);
    Extrinsic::Core::Expected<PointCloudIOResult> LoadPCD(std::string_view absolute_path);
    Extrinsic::Core::Expected<PointCloudIOResult> LoadPLY(std::string_view absolute_path);

    enum class PointCloudIOWriteStatus
    {
        Success = 0,
        EmptyCloud,
        InvalidPath,
        FileWriteError,
    };

    PointCloudIOWriteStatus WritePLY(std::string_view absolute_path, const PointCloudIOResult& cloud);
    PointCloudIOWriteStatus WritePLYBinary(std::string_view absolute_path, const PointCloudIOResult& cloud);
    PointCloudIOWriteStatus WriteXYZ(std::string_view absolute_path, const PointCloudIOResult& cloud);
    PointCloudIOWriteStatus WritePCD(std::string_view absolute_path, const PointCloudIOResult& cloud);
    PointCloudIOWriteStatus WritePCDBinary(std::string_view absolute_path, const PointCloudIOResult& cloud);
}
