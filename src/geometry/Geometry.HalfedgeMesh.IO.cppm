module;

#include <string_view>
#include <string>

export module Geometry.HalfedgeMesh.IO;

import Geometry.Properties;
import Extrinsic.Core.Error;

export namespace Geometry::MeshIO
{
    // Importers treat file-declared row counts as untrusted input: payload
    // capacity is validated before allocation, byte-size arithmetic is
    // overflow-checked, and malformed/degenerate topology rows fail closed.
    struct MeshIOResult
    {
        PropertySet Vertices{};
        PropertySet Halfedges{};
        PropertySet Edges{};
        PropertySet Faces{};

        std::string SourcePath;            // Original file path (for error messages)
        std::string BasePath;              // Directory containing the file (for relative refs)
    };

    Extrinsic::Core::Expected<MeshIOResult> LoadOBJ(std::string_view absolute_path);
    Extrinsic::Core::Expected<MeshIOResult> LoadOFF(std::string_view absolute_path);
    Extrinsic::Core::Expected<MeshIOResult> LoadPLY(std::string_view absolute_path);
    Extrinsic::Core::Expected<MeshIOResult> LoadSTL(std::string_view absolute_path);
    // and other missing formats for meshes only. Complete models or scenes will be handled differently?

    enum class MeshIOWriteStatus
    {
        Success = 0,
        EmptyMesh,
        InvalidFace,
        InvalidPath,
        FileWriteError,
    };

    MeshIOWriteStatus WriteOBJ(std::string_view absolute_path, const MeshIOResult& mesh);
    MeshIOWriteStatus WritePLY(std::string_view absolute_path, const MeshIOResult& mesh);
    MeshIOWriteStatus WritePLYBinary(std::string_view absolute_path, const MeshIOResult& mesh);
    MeshIOWriteStatus WriteSTL(std::string_view absolute_path, const MeshIOResult& mesh);
    MeshIOWriteStatus WriteSTLBinary(std::string_view absolute_path, const MeshIOResult& mesh);
}
