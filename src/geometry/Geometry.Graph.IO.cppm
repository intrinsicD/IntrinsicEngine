module;

#include <string>
#include <string_view>

export module Geometry.Graph.IO;

import Geometry.Graph;
import Extrinsic.Core.Error;

export namespace Geometry::GraphIO
{
    struct GraphIOResult
    {
        Graph::Graph Graph{};

        std::string SourcePath;       // Original file path (for error messages)
        std::string BasePath;         // Directory containing the file (for relative refs)
    };

    Extrinsic::Core::Expected<GraphIOResult> LoadTGF(std::string_view absolute_path);
    Extrinsic::Core::Expected<GraphIOResult> LoadEdgeList(std::string_view absolute_path);

    enum class GraphIOWriteStatus
    {
        Success,
        InvalidPath,
        EmptyGraph,
        FileWriteError,
    };

    GraphIOWriteStatus WriteTGF(std::string_view absolute_path, const GraphIOResult& graph);
    GraphIOWriteStatus WriteEdgeList(std::string_view absolute_path, const GraphIOResult& graph);
}

