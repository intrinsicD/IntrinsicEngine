module;

#include <cstdio>
#include <cmath>
#include <fstream>
#include <ios>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

#include "Geometry.IOText.hpp"

module Geometry.Graph.IO;

import Geometry.Graph;
import Geometry.Properties;
import Core.Error;

namespace Geometry::GraphIO
{
    namespace
    {
        using Geometry::IOText::MakePathInfo;
        using Geometry::IOText::NextLine;
        using Geometry::IOText::ParseNumber;
        using Geometry::IOText::ReadTextFile;
        using Geometry::IOText::SplitWhitespace;
        using Geometry::IOText::TextFileError;
        using Geometry::IOText::Trim;

        [[nodiscard]] Core::ErrorCode ToCoreError(TextFileError error)
        {
            switch (error)
            {
            case TextFileError::FileNotFound:
                return Core::ErrorCode::FileNotFound;
            case TextFileError::FileReadError:
                return Core::ErrorCode::FileReadError;
            }
            return Core::ErrorCode::Unknown;
        }

        [[nodiscard]] std::string JoinTokens(const std::vector<std::string_view>& tokens, std::size_t first)
        {
            std::string joined;
            for (std::size_t i = first; i < tokens.size(); ++i)
            {
                if (!joined.empty())
                {
                    joined.push_back(' ');
                }
                joined.append(tokens[i]);
            }
            return joined;
        }

        [[nodiscard]] Core::Expected<GraphIOResult> InvalidGraphFormat()
        {
            return Core::Err<GraphIOResult>(Core::ErrorCode::InvalidFormat);
        }

        [[nodiscard]] bool IsFinite(const glm::vec3& value)
        {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }

        void ApplyPathInfo(GraphIOResult& result, std::string_view path)
        {
            const auto pathInfo = MakePathInfo(path);
            result.SourcePath = pathInfo.SourcePath;
            result.BasePath = pathInfo.BasePath;
        }
    }

    Core::Expected<GraphIOResult> LoadTGF(std::string_view absolute_path)
    {
        auto text = ReadTextFile(absolute_path);
        if (!text)
        {
            return Core::Err<GraphIOResult>(ToCoreError(text.error()));
        }

        GraphIOResult result;
        ApplyPathInfo(result, absolute_path);

        std::unordered_map<std::string, VertexHandle> verticesById;
        VertexProperty<std::string> vertexLabels;
        EdgeProperty<std::string> edgeLabels;
        EdgeProperty<float> edgeWeights;

        std::size_t cursor = 0;
        std::string_view line;
        bool parsingEdges = false;
        while (NextLine(*text, cursor, line))
        {
            if (line.empty())
            {
                continue;
            }
            if (line.front() == '#')
            {
                parsingEdges = true;
                continue;
            }

            const auto tokens = SplitWhitespace(line);
            if (tokens.empty())
            {
                continue;
            }

            if (!parsingEdges)
            {
                const std::string id(tokens[0]);
                if (verticesById.contains(id))
                {
                    return InvalidGraphFormat();
                }

                glm::vec3 position(0.0f);
                std::size_t labelStart = 1;
                if (tokens.size() >= 4)
                {
                    const auto x = ParseNumber<float>(tokens[1]);
                    const auto y = ParseNumber<float>(tokens[2]);
                    const auto z = ParseNumber<float>(tokens[3]);
                    if (x && y && z)
                    {
                        position = glm::vec3(*x, *y, *z);
                        if (!IsFinite(position))
                        {
                            return InvalidGraphFormat();
                        }
                        labelStart = 4;
                    }
                }

                const VertexHandle vertex = result.Graph.AddVertex(position);
                if (!vertex.IsValid())
                {
                    return InvalidGraphFormat();
                }
                verticesById.emplace(id, vertex);

                if (labelStart < tokens.size())
                {
                    if (!vertexLabels.IsValid())
                    {
                        vertexLabels = result.Graph.GetOrAddVertexProperty<std::string>("v:label", {});
                    }
                    vertexLabels[vertex] = JoinTokens(tokens, labelStart);
                }
            }
            else
            {
                if (tokens.size() < 2)
                {
                    return InvalidGraphFormat();
                }
                const auto from = verticesById.find(std::string(tokens[0]));
                const auto to = verticesById.find(std::string(tokens[1]));
                if (from == verticesById.end() || to == verticesById.end())
                {
                    return InvalidGraphFormat();
                }
                auto edge = result.Graph.AddEdge(from->second, to->second);
                if (!edge)
                {
                    return InvalidGraphFormat();
                }

                std::size_t labelStart = 2;
                if (tokens.size() >= 3)
                {
                    if (const auto weight = ParseNumber<float>(tokens[2]))
                    {
                        if (!std::isfinite(*weight))
                        {
                            return InvalidGraphFormat();
                        }
                        if (!edgeWeights.IsValid())
                        {
                            edgeWeights = result.Graph.GetOrAddEdgeProperty<float>("e:weight", 1.0f);
                        }
                        edgeWeights[*edge] = *weight;
                        labelStart = 3;
                    }
                }
                if (labelStart < tokens.size())
                {
                    if (!edgeLabels.IsValid())
                    {
                        edgeLabels = result.Graph.GetOrAddEdgeProperty<std::string>("e:label", {});
                    }
                    edgeLabels[*edge] = JoinTokens(tokens, labelStart);
                }
            }
        }

        if (result.Graph.VertexCount() == 0)
        {
            return InvalidGraphFormat();
        }
        return result;
    }

    Core::Expected<GraphIOResult> LoadEdgeList(std::string_view absolute_path)
    {
        auto text = ReadTextFile(absolute_path);
        if (!text)
        {
            return Core::Err<GraphIOResult>(ToCoreError(text.error()));
        }

        GraphIOResult result;
        ApplyPathInfo(result, absolute_path);

        std::unordered_map<std::string, VertexHandle> verticesById;
        auto vertexIds = result.Graph.GetOrAddVertexProperty<std::string>("v:id", {});
        EdgeProperty<float> edgeWeights;
        EdgeProperty<std::string> edgeLabels;

        auto getOrCreateVertex = [&](std::string_view id) -> VertexHandle
        {
            const std::string key(id);
            if (const auto it = verticesById.find(key); it != verticesById.end())
            {
                return it->second;
            }
            const VertexHandle vertex = result.Graph.AddVertex(glm::vec3(0.0f));
            if (vertex.IsValid())
            {
                vertexIds[vertex] = key;
                verticesById.emplace(key, vertex);
            }
            return vertex;
        };

        std::size_t cursor = 0;
        std::string_view line;
        while (NextLine(*text, cursor, line))
        {
            const std::size_t comment = line.find('#');
            if (comment != std::string_view::npos)
            {
                line = Trim(line.substr(0, comment));
            }
            if (line.empty())
            {
                continue;
            }
            const auto tokens = SplitWhitespace(line);
            if (tokens.size() < 2)
            {
                return InvalidGraphFormat();
            }

            const VertexHandle from = getOrCreateVertex(tokens[0]);
            const VertexHandle to = getOrCreateVertex(tokens[1]);
            if (!from.IsValid() || !to.IsValid())
            {
                return InvalidGraphFormat();
            }
            auto edge = result.Graph.AddEdge(from, to);
            if (!edge)
            {
                return InvalidGraphFormat();
            }

            std::size_t labelStart = 2;
            if (tokens.size() >= 3)
            {
                if (const auto weight = ParseNumber<float>(tokens[2]))
                {
                    if (!std::isfinite(*weight))
                    {
                        return InvalidGraphFormat();
                    }
                    if (!edgeWeights.IsValid())
                    {
                        edgeWeights = result.Graph.GetOrAddEdgeProperty<float>("e:weight", 1.0f);
                    }
                    edgeWeights[*edge] = *weight;
                    labelStart = 3;
                }
            }
            if (labelStart < tokens.size())
            {
                if (!edgeLabels.IsValid())
                {
                    edgeLabels = result.Graph.GetOrAddEdgeProperty<std::string>("e:label", {});
                }
                edgeLabels[*edge] = JoinTokens(tokens, labelStart);
            }
        }

        if (result.Graph.VertexCount() == 0 || result.Graph.EdgeCount() == 0)
        {
            return InvalidGraphFormat();
        }
        return result;
    }

    GraphIOWriteStatus WriteTGF(std::string_view absolute_path, const GraphIOResult& graph)
    {
        if (absolute_path.empty())
        {
            return GraphIOWriteStatus::InvalidPath;
        }

        const auto& source = graph.Graph;
        if (source.VertexCount() == 0)
        {
            return GraphIOWriteStatus::EmptyGraph;
        }

        std::ofstream stream(std::string(absolute_path), std::ios::binary | std::ios::trunc);
        if (!stream)
        {
            return GraphIOWriteStatus::InvalidPath;
        }

        const auto vertexLabels = source.VertexProperties().Get<std::string>("v:label");
        const auto edgeWeights = source.EdgeProperties().Get<float>("e:weight");
        const auto edgeLabels = source.EdgeProperties().Get<std::string>("e:label");
        const bool hasEdgeLabels = edgeLabels.IsValid();
        const bool hasEdgeWeights = edgeWeights.IsValid();

        char buffer[256];
        const std::size_t vertexSlots = source.VerticesSize();
        for (std::size_t i = 0; i < vertexSlots; ++i)
        {
            const VertexHandle v{static_cast<PropertyIndex>(i)};
            if (source.IsDeleted(v))
            {
                continue;
            }
            const glm::vec3 p = source.VertexPosition(v);
            if (!IsFinite(p))
            {
                return GraphIOWriteStatus::FileWriteError;
            }
            int written = std::snprintf(buffer, sizeof(buffer),
                                        "%zu %.6f %.6f %.6f",
                                        i,
                                        static_cast<double>(p.x),
                                        static_cast<double>(p.y),
                                        static_cast<double>(p.z));
            if (written <= 0)
            {
                return GraphIOWriteStatus::FileWriteError;
            }
            stream.write(buffer, written);

            if (vertexLabels.IsValid())
            {
                const std::string& label = vertexLabels[v.Index];
                if (!label.empty())
                {
                    stream.put(' ');
                    stream.write(label.data(), static_cast<std::streamsize>(label.size()));
                }
            }
            stream.put('\n');
        }

        stream.write("#\n", 2);

        const std::size_t edgeSlots = source.EdgesSize();
        for (std::size_t i = 0; i < edgeSlots; ++i)
        {
            const EdgeHandle e{static_cast<PropertyIndex>(i)};
            if (source.IsDeleted(e))
            {
                continue;
            }
            const auto endpoints = source.EdgeVertices(e);
            const std::size_t fromIndex = endpoints.first.Index;
            const std::size_t toIndex = endpoints.second.Index;

            int written = std::snprintf(buffer, sizeof(buffer),
                                        "%zu %zu",
                                        fromIndex,
                                        toIndex);
            if (written <= 0)
            {
                return GraphIOWriteStatus::FileWriteError;
            }
            stream.write(buffer, written);

            const bool labelPresent = hasEdgeLabels && !edgeLabels[e.Index].empty();
            const bool weightPresent = hasEdgeWeights;
            if (weightPresent || labelPresent)
            {
                const float weight = weightPresent ? edgeWeights[e.Index] : 1.0f;
                if (!std::isfinite(weight))
                {
                    return GraphIOWriteStatus::FileWriteError;
                }
                written = std::snprintf(buffer, sizeof(buffer),
                                        " %.6f",
                                        static_cast<double>(weight));
                if (written <= 0)
                {
                    return GraphIOWriteStatus::FileWriteError;
                }
                stream.write(buffer, written);
            }

            if (labelPresent)
            {
                const std::string& label = edgeLabels[e.Index];
                stream.put(' ');
                stream.write(label.data(), static_cast<std::streamsize>(label.size()));
            }

            stream.put('\n');
        }

        stream.flush();
        if (!stream.good())
        {
            return GraphIOWriteStatus::FileWriteError;
        }
        return GraphIOWriteStatus::Success;
    }

    GraphIOWriteStatus WriteEdgeList(std::string_view absolute_path, const GraphIOResult& graph)
    {
        if (absolute_path.empty())
        {
            return GraphIOWriteStatus::InvalidPath;
        }

        const auto& source = graph.Graph;
        if (source.VertexCount() == 0 || source.EdgeCount() == 0)
        {
            return GraphIOWriteStatus::EmptyGraph;
        }

        std::ofstream stream(std::string(absolute_path), std::ios::binary | std::ios::trunc);
        if (!stream)
        {
            return GraphIOWriteStatus::InvalidPath;
        }

        const auto edgeWeights = source.EdgeProperties().Get<float>("e:weight");
        const auto edgeLabels = source.EdgeProperties().Get<std::string>("e:label");
        const bool hasEdgeWeights = edgeWeights.IsValid();
        const bool hasEdgeLabels = edgeLabels.IsValid();

        char buffer[256];
        const std::size_t edgeSlots = source.EdgesSize();
        for (std::size_t i = 0; i < edgeSlots; ++i)
        {
            const EdgeHandle e{static_cast<PropertyIndex>(i)};
            if (source.IsDeleted(e))
            {
                continue;
            }
            const auto endpoints = source.EdgeVertices(e);
            const std::size_t fromIndex = endpoints.first.Index;
            const std::size_t toIndex = endpoints.second.Index;

            int written = std::snprintf(buffer, sizeof(buffer),
                                        "%zu %zu",
                                        fromIndex,
                                        toIndex);
            if (written <= 0)
            {
                return GraphIOWriteStatus::FileWriteError;
            }
            stream.write(buffer, written);

            const bool labelPresent = hasEdgeLabels && !edgeLabels[e.Index].empty();
            if (hasEdgeWeights || labelPresent)
            {
                const float weight = hasEdgeWeights ? edgeWeights[e.Index] : 1.0f;
                if (!std::isfinite(weight))
                {
                    return GraphIOWriteStatus::FileWriteError;
                }
                written = std::snprintf(buffer, sizeof(buffer),
                                        " %.6f",
                                        static_cast<double>(weight));
                if (written <= 0)
                {
                    return GraphIOWriteStatus::FileWriteError;
                }
                stream.write(buffer, written);
            }

            if (labelPresent)
            {
                const std::string& label = edgeLabels[e.Index];
                stream.put(' ');
                stream.write(label.data(), static_cast<std::streamsize>(label.size()));
            }

            stream.put('\n');
        }

        stream.flush();
        if (!stream.good())
        {
            return GraphIOWriteStatus::FileWriteError;
        }
        return GraphIOWriteStatus::Success;
    }
}

