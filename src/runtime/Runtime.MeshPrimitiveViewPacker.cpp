module;

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <vector>

#include <glm/glm.hpp>

module Extrinsic.Runtime.MeshPrimitiveViewPacker;

import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.Graphics.GpuWorld;
import Geometry.Properties;

namespace Extrinsic::Runtime
{
    namespace
    {
        constexpr const char* kEdgeViewDebugName = "Runtime.MeshEdgeView";
        constexpr const char* kVertexViewDebugName = "Runtime.MeshVertexView";

        [[nodiscard]] MeshPrimitiveViewResult Failure(MeshPrimitiveViewStatus status,
                                                      MeshPrimitiveViewBuffer& outBuffer) noexcept
        {
            outBuffer.Clear();
            return MeshPrimitiveViewResult{status, std::nullopt};
        }

        [[nodiscard]] bool IsFinite(const glm::vec3& p) noexcept
        {
            return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
        }

        // Resolve and validate the mesh vertex positions shared by both views.
        // On success, returns the positions span; on failure, fills `status`
        // (the caller turns it into a `Failure`).
        [[nodiscard]] const std::vector<glm::vec3>* ResolvePositions(
            const ECS::Components::GeometrySources::ConstSourceView& view,
            MeshPrimitiveViewStatus& status) noexcept
        {
            using namespace ECS::Components::GeometrySources;

            if (view.ActiveDomain != Domain::Mesh)
            {
                status = MeshPrimitiveViewStatus::WrongDomain;
                return nullptr;
            }
            if (view.VertexSource == nullptr)
            {
                status = MeshPrimitiveViewStatus::MissingPositions;
                return nullptr;
            }
            const auto posProp = view.VertexSource->Properties.Get<glm::vec3>(PropertyNames::kPosition);
            if (!posProp)
            {
                status = MeshPrimitiveViewStatus::MissingPositions;
                return nullptr;
            }
            const auto& positions = posProp.Vector();
            if (positions.empty())
            {
                status = MeshPrimitiveViewStatus::EmptyMesh;
                return nullptr;
            }
            status = MeshPrimitiveViewStatus::Success;
            return &positions;
        }

        // Write the shared vertex buffer from `positions`, validating finiteness
        // and accumulating the local AABB. Returns false (and fills `status`)
        // on a non-finite position.
        [[nodiscard]] bool WriteVertexBuffer(const std::vector<glm::vec3>& positions,
                                             MeshPrimitiveViewBuffer& outBuffer,
                                             glm::vec3& minP,
                                             glm::vec3& maxP,
                                             MeshPrimitiveViewStatus& status) noexcept
        {
            const std::size_t vertexCount = positions.size();
            outBuffer.VertexBytes.resize(sizeof(MeshPrimitiveVertex) * vertexCount);
            auto* vData = reinterpret_cast<MeshPrimitiveVertex*>(outBuffer.VertexBytes.data());

            constexpr float kInf = std::numeric_limits<float>::infinity();
            minP = glm::vec3{+kInf, +kInf, +kInf};
            maxP = glm::vec3{-kInf, -kInf, -kInf};

            for (std::size_t i = 0; i < vertexCount; ++i)
            {
                const glm::vec3 p = positions[i];
                if (!IsFinite(p))
                {
                    status = MeshPrimitiveViewStatus::NonFinitePosition;
                    return false;
                }
                vData[i] = MeshPrimitiveVertex{p.x, p.y, p.z, 0.0f, 0.0f};
                minP = glm::min(minP, p);
                maxP = glm::max(maxP, p);
            }
            status = MeshPrimitiveViewStatus::Success;
            return true;
        }

        void FillLocalSphere(Extrinsic::Graphics::GpuWorld::GeometryUploadDesc& desc,
                             const glm::vec3& minP,
                             const glm::vec3& maxP) noexcept
        {
            const glm::vec3 center = 0.5f * (minP + maxP);
            const float radius = 0.5f * glm::length(maxP - minP);
            desc.LocalBounds.LocalSphere = glm::vec4{center, radius};
        }
    }

    const char* DebugNameForMeshPrimitiveViewStatus(MeshPrimitiveViewStatus status) noexcept
    {
        switch (status)
        {
            case MeshPrimitiveViewStatus::Success:             return "MeshPrimitiveView.Success";
            case MeshPrimitiveViewStatus::WrongDomain:         return "MeshPrimitiveView.WrongDomain";
            case MeshPrimitiveViewStatus::MissingPositions:    return "MeshPrimitiveView.MissingPositions";
            case MeshPrimitiveViewStatus::EmptyMesh:           return "MeshPrimitiveView.EmptyMesh";
            case MeshPrimitiveViewStatus::MissingEdgeTopology: return "MeshPrimitiveView.MissingEdgeTopology";
            case MeshPrimitiveViewStatus::InvalidEdge:         return "MeshPrimitiveView.InvalidEdge";
            case MeshPrimitiveViewStatus::NonFinitePosition:   return "MeshPrimitiveView.NonFinitePosition";
        }
        return "MeshPrimitiveView.Unknown";
    }

    MeshPrimitiveViewResult PackMeshEdgeView(
        const ECS::Components::GeometrySources::ConstSourceView& view,
        MeshPrimitiveViewBuffer& outBuffer)
    {
        outBuffer.Clear();

        using namespace ECS::Components::GeometrySources;

        MeshPrimitiveViewStatus status = MeshPrimitiveViewStatus::Success;
        const std::vector<glm::vec3>* positions = ResolvePositions(view, status);
        if (positions == nullptr)
        {
            return Failure(status, outBuffer);
        }
        const std::size_t vertexCount = positions->size();

        // Edge endpoints index into the mesh vertex rows. Validate the `Edges`
        // PropertySet and emit a line-list before writing the vertex buffer so a
        // failure leaves the buffer cleared.
        if (view.EdgeSource == nullptr)
        {
            return Failure(MeshPrimitiveViewStatus::MissingEdgeTopology, outBuffer);
        }
        const auto v0Prop = view.EdgeSource->Properties.Get<std::uint32_t>(PropertyNames::kEdgeV0);
        const auto v1Prop = view.EdgeSource->Properties.Get<std::uint32_t>(PropertyNames::kEdgeV1);
        if (!v0Prop || !v1Prop)
        {
            return Failure(MeshPrimitiveViewStatus::MissingEdgeTopology, outBuffer);
        }
        const auto& v0 = v0Prop.Vector();
        const auto& v1 = v1Prop.Vector();
        if (v0.size() != v1.size())
        {
            return Failure(MeshPrimitiveViewStatus::MissingEdgeTopology, outBuffer);
        }

        const auto vertexCountU32 = static_cast<std::uint32_t>(vertexCount);
        outBuffer.LineIndices.reserve(v0.size() * 2u);
        for (std::size_t e = 0; e < v0.size(); ++e)
        {
            if (v0[e] >= vertexCountU32 || v1[e] >= vertexCountU32)
            {
                return Failure(MeshPrimitiveViewStatus::InvalidEdge, outBuffer);
            }
            outBuffer.LineIndices.push_back(v0[e]);
            outBuffer.LineIndices.push_back(v1[e]);
        }

        glm::vec3 minP{};
        glm::vec3 maxP{};
        if (!WriteVertexBuffer(*positions, outBuffer, minP, maxP, status))
        {
            return Failure(status, outBuffer);
        }

        Extrinsic::Graphics::GpuWorld::GeometryUploadDesc desc{};
        desc.PackedVertexBytes = std::span<const std::byte>{outBuffer.VertexBytes};
        desc.SurfaceIndices = {};
        desc.LineIndices = std::span<const std::uint32_t>{outBuffer.LineIndices};
        desc.VertexCount = vertexCountU32;
        FillLocalSphere(desc, minP, maxP);
        desc.DebugName = kEdgeViewDebugName;

        return MeshPrimitiveViewResult{MeshPrimitiveViewStatus::Success, desc};
    }

    MeshPrimitiveViewResult PackMeshVertexView(
        const ECS::Components::GeometrySources::ConstSourceView& view,
        MeshPrimitiveViewBuffer& outBuffer)
    {
        outBuffer.Clear();

        MeshPrimitiveViewStatus status = MeshPrimitiveViewStatus::Success;
        const std::vector<glm::vec3>* positions = ResolvePositions(view, status);
        if (positions == nullptr)
        {
            return Failure(status, outBuffer);
        }

        glm::vec3 minP{};
        glm::vec3 maxP{};
        if (!WriteVertexBuffer(*positions, outBuffer, minP, maxP, status))
        {
            return Failure(status, outBuffer);
        }

        Extrinsic::Graphics::GpuWorld::GeometryUploadDesc desc{};
        desc.PackedVertexBytes = std::span<const std::byte>{outBuffer.VertexBytes};
        desc.SurfaceIndices = {};
        desc.LineIndices = {};
        desc.VertexCount = static_cast<std::uint32_t>(positions->size());
        FillLocalSphere(desc, minP, maxP);
        desc.DebugName = kVertexViewDebugName;

        return MeshPrimitiveViewResult{MeshPrimitiveViewStatus::Success, desc};
    }
}
