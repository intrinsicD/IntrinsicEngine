module;

#include <string>
#include <string_view>

export module Geometry.PointCloud.IO;

import Geometry.PointCloud;
import Core.Error;

export namespace Geometry::PointCloudIO
{
    struct PointCloudIOResult
    {
        PointCloud::Cloud Cloud{};

        std::string SourcePath;       // Original file path (for error messages)
        std::string BasePath;         // Directory containing the file (for relative refs)
    };

    Core::Expected<PointCloudIOResult> LoadXYZ(std::string_view absolute_path);
    Core::Expected<PointCloudIOResult> LoadPCD(std::string_view absolute_path);
    Core::Expected<PointCloudIOResult> LoadPLY(std::string_view absolute_path);

    enum class PointCloudIOWriteStatus
    {
        Success = 0,
        EmptyCloud,
        InvalidPath,
        FileWriteError,
    };

    PointCloudIOWriteStatus WritePLY(std::string_view absolute_path, const PointCloudIOResult& cloud);
}

